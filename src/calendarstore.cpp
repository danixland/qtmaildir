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

    // Recurrence, exceptions and overrides: Task 4.
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
QList<Occurrence> occurrences(const QList<CalEvent> &, const QDateTime &, const QDateTime &) { return {}; }
bool isEditable(const CalEvent &, const CalCollection &, const QStringList &) { return false; }
QByteArray applyEdit(const QByteArray &, const EventEdit &, Scope, const QDateTime &) { return {}; }
QByteArray newEvent(const EventEdit &, const QByteArray &) { return {}; }
QByteArray deleteOccurrence(const QByteArray &, const QDateTime &) { return {}; }
bool sameMeaning(const QByteArray &, const QByteArray &) { return false; }

}  // namespace CalendarStore
