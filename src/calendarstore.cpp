/*
 * qtmaildir - a Qt6 mail client for notmuch-indexed Maildirs
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "calendarstore.h"
#include "icalraii.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <QUuid>

#include <algorithm>

namespace {

QString str(const char *s) { return s ? QString::fromUtf8(s) : QString(); }

QByteArray tzidOf(icalproperty *prop)
{
    if (!prop)
        return {};
    icalparameter *param = icalproperty_get_first_parameter(prop, ICAL_TZID_PARAMETER);
    return param ? QByteArray(icalparameter_get_tzid(param)) : QByteArray();
}

/// One icaltimetype to a QDateTime, in this order (spec, "Time and
/// recurrence"): a date is local midnight; UTC is UTC; a zone libical resolved
/// (an embedded VTIMEZONE) converts through libical; a TZID libical could not
/// resolve is tried as an IANA name through QTimeZone; anything else is local
/// wall-clock time, and an unresolvable TZID sets *unknownZone.
QDateTime toDateTime(icaltimetype t, const QByteArray &tzid, bool *unknownZone)
{
    if (icaltime_is_null_time(t))
        return {};
    const QDate date(t.year, t.month, t.day);
    if (t.is_date)
        return QDateTime(date, QTime(0, 0));
    const QTime time(t.hour, t.minute, t.second);
    if (icaltime_is_utc(t))
        return QDateTime(date, time, QTimeZone::utc());
    if (t.zone) {
        const icaltimetype utc =
            icaltime_convert_to_zone(t, icaltimezone_get_utc_timezone());
        return QDateTime(QDate(utc.year, utc.month, utc.day),
                         QTime(utc.hour, utc.minute, utc.second), QTimeZone::utc());
    }
    if (!tzid.isEmpty()) {
        const QTimeZone zone(tzid);
        if (zone.isValid())
            return QDateTime(date, time, zone);
        if (unknownZone)
            *unknownZone = true;
    }
    return QDateTime(date, time);
}

CalPerson personOf(icalproperty *prop)
{
    CalPerson person;
    QString value = str(icalproperty_get_value_as_string(prop));
    if (value.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive))
        value = value.mid(7);
    person.address = value;
    person.name = str(icalproperty_get_parameter_as_string(prop, "CN"));
    person.partstat = str(icalproperty_get_parameter_as_string(prop, "PARTSTAT"));
    return person;
}

/// The VEVENT without a RECURRENCE-ID, or the first VEVENT if every one has.
icalcomponent *masterOf(icalcomponent *root)
{
    icalcomponent *first = nullptr;
    for (icalcomponent *c = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT);
         c; c = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
        if (!first)
            first = c;
        if (!icalcomponent_get_first_property(c, ICAL_RECURRENCEID_PROPERTY))
            return c;
    }
    return first;
}

IcalComponent parseRoot(const QByteArray &text)
{
    IcalComponent root(icalparser_parse_string(text.constData()));
    if (root && icalcomponent_isa(root.get()) == ICAL_VEVENT_COMPONENT) {
        // A bare VEVENT with no VCALENDAR around it: wrap it, so every
        // caller walks the same shape.
        IcalComponent cal(icalcomponent_new(ICAL_VCALENDAR_COMPONENT));
        icalcomponent_add_component(cal.get(), root.release());
        return cal;
    }
    return root;
}

/// The end of a component: DTEND, else DTSTART + DURATION (libical computes
/// both through get_dtend), else one day for a date and the start otherwise.
QDateTime endOf(icalcomponent *c, const QDateTime &start, bool allDay, bool *unknownZone)
{
    icalproperty *endProp = icalcomponent_get_first_property(c, ICAL_DTEND_PROPERTY);
    const icaltimetype end = icalcomponent_get_dtend(c);
    if (!icaltime_is_null_time(end))
        return toDateTime(end, tzidOf(endProp ? endProp
                                  : icalcomponent_get_first_property(c, ICAL_DTSTART_PROPERTY)),
                          unknownZone);
    return allDay ? start.addDays(1) : start;
}

/// ponytail: iterates from DTSTART rather than icalrecur_iterator_set_start,
/// which is unsupported with COUNT. The cap guards against a pathological rule
/// (FREQ=SECONDLY over a wide window), not against age: a daily series from
/// 1970 to a 22nd-century window is under 50k steps, well inside 100000. 100k
/// iterator steps is still microseconds, so the cap is not a performance knob.
constexpr int kMaxIterations = 100000;

bool overlaps(const QDateTime &start, const QDateTime &end,
              const QDateTime &from, const QDateTime &to)
{
    // A zero-length event (no DTEND) still shows on its instant.
    return start < to && (end > from || (end == start && start >= from));
}

/// The occurrence start times of the master in `text`, up to `to`, in the
/// event's own zone and converted after: a weekly 10:00 Rome meeting stays
/// 10:00 in Rome across DST (spec, "Time and recurrence").
QList<QDateTime> seriesStarts(const QByteArray &text, const QDateTime &to)
{
    QList<QDateTime> starts;
    IcalComponent root = parseRoot(text);
    icalcomponent *master = root ? masterOf(root.get()) : nullptr;
    icalproperty *rrule = master ? icalcomponent_get_first_property(master, ICAL_RRULE_PROPERTY) : nullptr;
    if (!rrule)
        return starts;
    icalproperty *startProp = icalcomponent_get_first_property(master, ICAL_DTSTART_PROPERTY);
    const QByteArray tzid = tzidOf(startProp);
    const icaltimetype dtstart = icalcomponent_get_dtstart(master);

    IcalRecurIterator it(icalrecur_iterator_new(icalproperty_get_rrule(rrule), dtstart));
    if (!it)
        return starts;
    for (int i = 0; i < kMaxIterations; ++i) {
        icaltimetype t = icalrecur_iterator_next(it.get());
        if (icaltime_is_null_time(t))
            break;
        t.zone = dtstart.zone;
        t.is_date = dtstart.is_date;
        const QDateTime start = toDateTime(t, tzid, nullptr);
        if (start >= to)
            break;
        starts.append(start);
    }
    return starts;
}

} // namespace

namespace {

QString readTrimmed(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

/// A colour for a collection with no `color` file, derived from its directory
/// name so it is the same on every load and on every machine.
QColor hashedColour(const QString &dir)
{
    const QByteArray hash = QCryptographicHash::hash(dir.toUtf8(), QCryptographicHash::Md5);
    const int hue = (static_cast<unsigned char>(hash[0]) * 256
                     + static_cast<unsigned char>(hash[1])) % 360;
    return QColor::fromHsl(hue, 150, 140);
}

} // namespace

namespace {

/// How a component's times are written, read off its DTSTART and reused for
/// every time written back, so an edit keeps the event's zone.
struct TimeForm
{
    enum Kind { Date, Utc, Zoned, Floating } kind = Floating;
    QByteArray tzid;
    icaltimezone *zone = nullptr;  ///< Embedded or built-in; not owned.
};

TimeForm formOf(icalcomponent *root, icalcomponent *c)
{
    TimeForm form;
    icalproperty *prop = icalcomponent_get_first_property(c, ICAL_DTSTART_PROPERTY);
    const icaltimetype t = icalcomponent_get_dtstart(c);
    if (t.is_date) {
        form.kind = TimeForm::Date;
    } else if (icaltime_is_utc(t)) {
        form.kind = TimeForm::Utc;
    } else if (!tzidOf(prop).isEmpty()) {
        form.kind = TimeForm::Zoned;
        form.tzid = tzidOf(prop);
        form.zone = icalcomponent_get_timezone(root, form.tzid.constData());
        if (!form.zone)
            form.zone = icaltimezone_get_builtin_timezone(form.tzid.constData());
    }
    return form;
}

TimeForm zonedForm(const QByteArray &tzid)
{
    TimeForm form;
    form.kind = TimeForm::Zoned;
    form.tzid = tzid;
    form.zone = icaltimezone_get_builtin_timezone(tzid.constData());
    return form;
}

/// A QDateTime as the wall-clock icaltimetype `form` writes, zone left unset:
/// the TZID travels as a parameter, so the output never depends on whether
/// the file carried a VTIMEZONE.
icaltimetype wallTime(const QDateTime &dt, const TimeForm &form)
{
    if (form.kind == TimeForm::Date) {
        icaltimetype t = icaltime_null_date();
        const QDate d = dt.date();
        t.year = d.year(); t.month = d.month(); t.day = d.day();
        return t;
    }
    QDateTime local;
    if (form.kind == TimeForm::Utc) {
        local = dt.toUTC();
    } else if (form.kind == TimeForm::Zoned && form.zone) {
        icaltimetype t = icaltime_from_timet_with_zone(dt.toSecsSinceEpoch(), 0, form.zone);
        t.zone = nullptr;
        return t;
    } else if (form.kind == TimeForm::Zoned && QTimeZone(form.tzid).isValid()) {
        local = dt.toTimeZone(QTimeZone(form.tzid));
    } else {
        local = dt.toLocalTime();
    }
    icaltimetype t = icaltime_null_time();
    t.is_date = 0;
    t.year = local.date().year(); t.month = local.date().month(); t.day = local.date().day();
    t.hour = local.time().hour(); t.minute = local.time().minute(); t.second = local.time().second();
    if (form.kind == TimeForm::Utc)
        t = icaltime_convert_to_zone(t, icaltimezone_get_utc_timezone());
    return t;
}

/// Replaces every `kind` property on `c` with one holding `dt` in `form`.
/// DTSTART, DTEND and RECURRENCE-ID only: EXDATE is ADDITIVE, and replacing
/// it would drop every exception but the newest (deleteOccurrence adds its
/// own).
void setTime(icalcomponent *c, icalproperty_kind kind, const QDateTime &dt, const TimeForm &form)
{
    while (icalproperty *old = icalcomponent_get_first_property(c, kind)) {
        icalcomponent_remove_property(c, old);
        icalproperty_free(old);
    }
    const icaltimetype t = wallTime(dt, form);
    icalproperty *prop = kind == ICAL_DTSTART_PROPERTY ? icalproperty_new_dtstart(t)
                       : kind == ICAL_DTEND_PROPERTY   ? icalproperty_new_dtend(t)
                                                       : icalproperty_new_recurrenceid(t);
    if (form.kind == TimeForm::Zoned)
        icalproperty_add_parameter(prop, icalparameter_new_tzid(form.tzid.constData()));
    icalcomponent_add_property(c, prop);
}

void removeAll(icalcomponent *c, icalproperty_kind kind)
{
    while (icalproperty *p = icalcomponent_get_first_property(c, kind)) {
        icalcomponent_remove_property(c, p);
        icalproperty_free(p);
    }
}

void setText(icalcomponent *c, icalproperty_kind kind, const QString &value)
{
    removeAll(c, kind);
    if (value.isEmpty())
        return;
    const QByteArray utf8 = value.toUtf8();
    icalproperty *p = kind == ICAL_SUMMARY_PROPERTY ? icalproperty_new_summary(utf8.constData())
                    : kind == ICAL_LOCATION_PROPERTY ? icalproperty_new_location(utf8.constData())
                                                     : icalproperty_new_description(utf8.constData());
    icalcomponent_add_property(c, p);
}

/// Embeds the VTIMEZONE for `tzid` from libical's built-in database unless
/// the calendar already carries one, so what this application writes is
/// RFC-correct even where what it read was not.
void ensureVtimezone(icalcomponent *root, const QByteArray &tzid)
{
    if (tzid.isEmpty() || icalcomponent_get_timezone(root, tzid.constData()))
        return;
    icaltimezone *zone = icaltimezone_get_builtin_timezone(tzid.constData());
    if (!zone)
        return;
    icalcomponent *vtz = icalcomponent_new_clone(icaltimezone_get_component(zone));
    // libical's built-in components carry a "/freeassociation.sourceforge.net/..."
    // style TZID; rewrite it to the plain IANA name the events reference.
    removeAll(vtz, ICAL_TZID_PROPERTY);
    icalcomponent_add_property(vtz, icalproperty_new_tzid(tzid.constData()));
    icalcomponent_add_component(root, vtz);
}

void stamp(icalcomponent *c, bool bumpSequence)
{
    const icaltimetype now = icaltime_current_time_with_zone(icaltimezone_get_utc_timezone());
    removeAll(c, ICAL_DTSTAMP_PROPERTY);
    icalcomponent_add_property(c, icalproperty_new_dtstamp(now));
    removeAll(c, ICAL_LASTMODIFIED_PROPERTY);
    icalcomponent_add_property(c, icalproperty_new_lastmodified(now));
    if (bumpSequence)
        icalcomponent_set_sequence(c, icalcomponent_get_sequence(c) + 1);
}

/// The form a written time takes after an edit: all-day is a date; a timed
/// event keeps its zone, or takes the system zone when it was all-day.
TimeForm editedForm(icalcomponent *root, icalcomponent *c, bool allDay)
{
    if (allDay) {
        TimeForm date;
        date.kind = TimeForm::Date;
        return date;
    }
    TimeForm form = formOf(root, c);
    if (form.kind == TimeForm::Date) {
        form = zonedForm(QTimeZone::systemTimeZoneId());
        ensureVtimezone(root, form.tzid);
    }
    return form;
}

/// Writes the fields the form owns onto `c`. RRULE only for a master.
void writeFields(icalcomponent *root, icalcomponent *c, const EventEdit &edit, bool master)
{
    setText(c, ICAL_SUMMARY_PROPERTY, edit.summary);
    setText(c, ICAL_LOCATION_PROPERTY, edit.location);
    setText(c, ICAL_DESCRIPTION_PROPERTY, edit.description);
    const TimeForm form = editedForm(root, c, edit.allDay);
    removeAll(c, ICAL_DURATION_PROPERTY);
    setTime(c, ICAL_DTSTART_PROPERTY, edit.start, form);
    setTime(c, ICAL_DTEND_PROPERTY, edit.end, form);

    if (!master || edit.repeat.custom)
        return;  // a custom RRULE is never rewritten (spec, repeat control)
    removeAll(c, ICAL_RRULE_PROPERTY);
    if (edit.repeat.freq == RepeatRule::Freq::None)
        return;
    // UNTIL follows DTSTART's type: a DATE for all-day, else UTC (RFC 5545).
    QString until;
    if (edit.repeat.end == RepeatRule::End::Until) {
        until = edit.allDay
            ? edit.repeat.until.toString(QStringLiteral("yyyyMMdd"))
            : QDateTime(edit.repeat.until, QTime(23, 59, 59),
                        form.kind == TimeForm::Zoned && QTimeZone(form.tzid).isValid()
                            ? QTimeZone(form.tzid) : QTimeZone::systemTimeZone())
                  .toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    }
    const QByteArray rule = "RRULE:" + edit.repeat.toRRule(until).toUtf8();
    icalcomponent_add_property(c, icalproperty_new_from_string(rule.constData()));
}

QByteArray serialise(icalcomponent *root)
{
    IcalString text(icalcomponent_as_ical_string_r(root));
    return text ? QByteArray(text.get()) : QByteArray();
}

icalcomponent *overrideFor(icalcomponent *, icalcomponent *master, const QDateTime &)
{
    return master;  // replaced in Task 7
}

} // namespace

namespace CalendarStore {

CalEvent parseEvent(const QByteArray &text, const QString &filePath,
                    const QString &collectionDir, bool *unknownZone)
{
    CalEvent event;
    event.filePath = filePath;
    event.collectionDir = collectionDir;
    event.rawText = text;
    if (unknownZone)
        *unknownZone = false;

    IcalComponent root = parseRoot(text);
    if (!root)
        return event;
    icalcomponent *master = masterOf(root.get());
    if (!master)
        return event;

    event.uid = str(icalcomponent_get_uid(master));
    event.summary = str(icalcomponent_get_summary(master));
    event.location = str(icalcomponent_get_location(master));
    event.description = str(icalcomponent_get_description(master));
    event.sequence = icalcomponent_get_sequence(master);

    icalproperty *startProp = icalcomponent_get_first_property(master, ICAL_DTSTART_PROPERTY);
    const icaltimetype start = icalcomponent_get_dtstart(master);
    event.allDay = start.is_date;
    event.start = toDateTime(start, tzidOf(startProp), unknownZone);
    event.end = endOf(master, event.start, event.allDay, unknownZone);

    event.hasAlarm = icalcomponent_get_first_component(master, ICAL_VALARM_COMPONENT);
    if (icalproperty *org = icalcomponent_get_first_property(master, ICAL_ORGANIZER_PROPERTY))
        event.organizer = personOf(org);
    for (icalproperty *a = icalcomponent_get_first_property(master, ICAL_ATTENDEE_PROPERTY);
         a; a = icalcomponent_get_next_property(master, ICAL_ATTENDEE_PROPERTY))
        event.attendees.append(personOf(a));

    if (icalproperty *rrule = icalcomponent_get_first_property(master, ICAL_RRULE_PROPERTY)) {
        // The value text rather than icalproperty_get_rrule(): RepeatRule is
        // pure text, and libical re-serialising the struct would reorder parts.
        IcalString value(icalproperty_get_value_as_string_r(rrule));
        event.repeat = RepeatRule::fromRRule(str(value.get()), event.start.date());
    }
    for (icalproperty *ex = icalcomponent_get_first_property(master, ICAL_EXDATE_PROPERTY);
         ex; ex = icalcomponent_get_next_property(master, ICAL_EXDATE_PROPERTY))
        event.exdates.append(toDateTime(icalproperty_get_exdate(ex), tzidOf(ex), unknownZone));

    for (icalcomponent *c = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT);
         c; c = icalcomponent_get_next_component(root.get(), ICAL_VEVENT_COMPONENT)) {
        icalproperty *rid = icalcomponent_get_first_property(c, ICAL_RECURRENCEID_PROPERTY);
        if (c == master || !rid)
            continue;
        CalOverride ov;
        ov.recurrenceId = toDateTime(icalcomponent_get_recurrenceid(c), tzidOf(rid), unknownZone);
        icalproperty *sp = icalcomponent_get_first_property(c, ICAL_DTSTART_PROPERTY);
        const icaltimetype s = icalcomponent_get_dtstart(c);
        ov.allDay = s.is_date;
        ov.start = toDateTime(s, tzidOf(sp), unknownZone);
        ov.end = endOf(c, ov.start, ov.allDay, unknownZone);
        ov.summary = str(icalcomponent_get_summary(c));
        ov.location = str(icalcomponent_get_location(c));
        ov.description = str(icalcomponent_get_description(c));
        ov.cancelled = icalcomponent_get_status(c) == ICAL_STATUS_CANCELLED;
        event.overrides.append(ov);
    }
    return event;
}

LoadResult load(const QString &dir)
{
    LoadResult result;
    const QFileInfoList subdirs =
        QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &sub : subdirs) {
        CalCollection collection;
        collection.dir = sub.fileName();
        collection.path = sub.absoluteFilePath();
        collection.displayName = readTrimmed(collection.path + QStringLiteral("/displayname"));
        if (collection.displayName.isEmpty())
            collection.displayName = collection.dir;
        collection.color = QColor(readTrimmed(collection.path + QStringLiteral("/color")));
        if (!collection.color.isValid())
            collection.color = hashedColour(collection.dir);
        collection.readOnly = !sub.isWritable();
        result.collections.append(collection);

        const QFileInfoList files =
            QDir(collection.path).entryInfoList({ QStringLiteral("*.ics") }, QDir::Files, QDir::Name);
        for (const QFileInfo &file : files) {
            QFile f(file.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) {
                ++result.unparsable;
                continue;
            }
            bool unknown = false;
            CalEvent event = parseEvent(f.readAll(), file.absoluteFilePath(),
                                        collection.dir, &unknown);
            if (event.uid.isEmpty() || !event.start.isValid()) {
                ++result.unparsable;
                continue;
            }
            if (unknown)
                ++result.unknownZones;
            result.events.append(event);
        }
    }
    std::sort(result.collections.begin(), result.collections.end(),
              [](const CalCollection &a, const CalCollection &b) {
                  return a.displayName.localeAwareCompare(b.displayName) < 0;
              });
    return result;
}
QList<Occurrence> occurrences(const QList<CalEvent> &events,
                              const QDateTime &from, const QDateTime &to)
{
    QList<Occurrence> result;
    for (int i = 0; i < events.size(); ++i) {
        const CalEvent &e = events[i];
        const qint64 length = e.start.msecsTo(e.end);

        if (e.repeat.freq == RepeatRule::Freq::None) {
            if (overlaps(e.start, e.end, from, to))
                result.append({ i, e.start, e.end, e.allDay, {}, false, -1 });
            continue;
        }

        for (const QDateTime &slot : seriesStarts(e.rawText, to)) {
            // An EXDATE or an override takes this slot. Compared as instants:
            // an EXDATE may be written in UTC while DTSTART carries a TZID.
            const auto sameInstant = [&](const QDateTime &d) { return d == slot; };
            if (std::any_of(e.exdates.cbegin(), e.exdates.cend(), sameInstant))
                continue;
            if (std::any_of(e.overrides.cbegin(), e.overrides.cend(),
                            [&](const CalOverride &o) { return o.recurrenceId == slot; }))
                continue;
            const QDateTime end = e.allDay ? slot.addDays(e.start.daysTo(e.end))
                                           : slot.addMSecs(length);
            if (overlaps(slot, end, from, to))
                result.append({ i, slot, end, e.allDay, slot, false, -1 });
        }
        // Placed by their OWN start: an override may move into a window its
        // slot is outside of, or out of the window its slot is in.
        for (int o = 0; o < e.overrides.size(); ++o) {
            const CalOverride &ov = e.overrides[o];
            if (!ov.cancelled && overlaps(ov.start, ov.end, from, to))
                result.append({ i, ov.start, ov.end, ov.allDay, ov.recurrenceId, true, o });
        }
    }
    std::sort(result.begin(), result.end(),
              [](const Occurrence &a, const Occurrence &b) { return a.start < b.start; });
    return result;
}
bool isEditable(const CalEvent &event, const CalCollection &collection,
                const QStringList &ownAddresses)
{
    if (collection.readOnly)
        return false;
    if (event.organizer.address.isEmpty())
        return true;
    return ownAddresses.contains(event.organizer.address, Qt::CaseInsensitive);
}
QByteArray applyEdit(const QByteArray &text, const EventEdit &edit,
                     Scope scope, const QDateTime &recurrenceId)
{
    IcalComponent root = parseRoot(text);
    icalcomponent *master = root ? masterOf(root.get()) : nullptr;
    if (!master)
        return {};
    icalcomponent *target = master;
    if (scope == Scope::ThisOccurrence)
        target = overrideFor(root.get(), master, recurrenceId);  // Task 7
    writeFields(root.get(), target, edit, target == master);
    stamp(target, true);
    return serialise(root.get());
}

QByteArray newEvent(const EventEdit &edit, const QByteArray &zoneId)
{
    IcalComponent root(icalcomponent_new(ICAL_VCALENDAR_COMPONENT));
    icalcomponent_add_property(root.get(), icalproperty_new_version("2.0"));
    icalcomponent_add_property(root.get(), icalproperty_new_prodid("-//qtmaildir//EN"));
    icalcomponent *event = icalcomponent_new(ICAL_VEVENT_COMPONENT);
    icalcomponent_add_component(root.get(), event);
    icalcomponent_set_uid(event, QUuid::createUuid()
                                     .toString(QUuid::WithoutBraces).toUtf8().constData());
    // A DTSTART must exist before editedForm() reads it; a date one makes the
    // form fall through to the system zone for a timed event, which is
    // exactly the rule for new events, so pass the requested zone instead.
    if (!edit.allDay) {
        ensureVtimezone(root.get(), zoneId);
        setTime(event, ICAL_DTSTART_PROPERTY, edit.start, zonedForm(zoneId));
    } else {
        TimeForm date;
        date.kind = TimeForm::Date;
        setTime(event, ICAL_DTSTART_PROPERTY, edit.start, date);
    }
    writeFields(root.get(), event, edit, true);
    icalcomponent_set_sequence(event, 0);
    stamp(event, false);
    return serialise(root.get());
}

QByteArray deleteOccurrence(const QByteArray &, const QDateTime &) { return {}; }

bool sameMeaning(const QByteArray &a, const QByteArray &b)
{
    bool ignored = false;
    const CalEvent x = parseEvent(a, {}, {}, &ignored);
    const CalEvent y = parseEvent(b, {}, {}, &ignored);
    if (x.uid.isEmpty() || y.uid.isEmpty())
        return x.uid.isEmpty() && y.uid.isEmpty();
    if (x.uid != y.uid)
        return false;
    if (x.overrides.size() != y.overrides.size())
        return false;
    // Overrides are separate VEVENTs whose file order is not semantic, and a
    // server may normalise it, so pair them by recurrence id, not by index.
    QList<CalOverride> xo = x.overrides, yo = y.overrides;
    const auto byRecurrenceId = [](const CalOverride &a, const CalOverride &b) {
        return a.recurrenceId < b.recurrenceId;
    };
    std::stable_sort(xo.begin(), xo.end(), byRecurrenceId);
    std::stable_sort(yo.begin(), yo.end(), byRecurrenceId);
    for (int i = 0; i < xo.size(); ++i) {
        const CalOverride &p = xo[i], &q = yo[i];
        if (p.recurrenceId != q.recurrenceId || p.start != q.start
            || p.end != q.end || p.summary != q.summary || p.cancelled != q.cancelled)
            return false;
    }
    return x.summary == y.summary && x.start == y.start && x.end == y.end
        && x.allDay == y.allDay && x.repeat == y.repeat && x.exdates == y.exdates;
}

}  // namespace CalendarStore
