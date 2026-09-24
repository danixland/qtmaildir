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

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>

#include "calendarstore.h"

namespace {

/// Wraps VEVENT lines (and optional extra components) into a VCALENDAR.
QByteArray ics(const QString &body)
{
    return QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                          "PRODID:-//example//test//EN\r\n%1END:VCALENDAR\r\n")
        .arg(body).toUtf8();
}

QString vevent(const QString &lines)
{
    return QStringLiteral("BEGIN:VEVENT\r\n%1END:VEVENT\r\n").arg(lines);
}

const char *kRomeVtimezone =
    "BEGIN:VTIMEZONE\r\nTZID:Europe/Rome\r\n"
    "BEGIN:DAYLIGHT\r\nTZOFFSETFROM:+0100\r\nTZOFFSETTO:+0200\r\nTZNAME:CEST\r\n"
    "DTSTART:19700329T020000\r\nRRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU\r\nEND:DAYLIGHT\r\n"
    "BEGIN:STANDARD\r\nTZOFFSETFROM:+0200\r\nTZOFFSETTO:+0100\r\nTZNAME:CET\r\n"
    "DTSTART:19701025T030000\r\nRRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r\nEND:STANDARD\r\n"
    "END:VTIMEZONE\r\n";

CalEvent parse(const QString &body, bool *unknown = nullptr)
{
    bool ignored = false;
    return CalendarStore::parseEvent(ics(body), QStringLiteral("/x/e.ics"),
                                     QStringLiteral("x"), unknown ? unknown : &ignored);
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(file.errorString()));
    file.write(content);
}

QList<Occurrence> expand(const QString &body, const QDateTime &from, const QDateTime &to,
                         CalEvent *out = nullptr)
{
    const CalEvent event = parse(body);
    if (out)
        *out = event;
    return CalendarStore::occurrences({ event }, from, to);
}

QDateTime utc(int y, int m, int d, int h = 0, int min = 0)
{
    return QDateTime(QDate(y, m, d), QTime(h, min), QTimeZone::utc());
}

EventEdit editOf(const CalEvent &e)
{
    EventEdit edit;
    edit.summary = e.summary;
    edit.location = e.location;
    edit.description = e.description;
    edit.start = e.start;
    edit.end = e.end;
    edit.allDay = e.allDay;
    edit.repeat = e.repeat;
    edit.collectionDir = e.collectionDir;
    return edit;
}

CalEvent reparse(const QByteArray &text)
{
    bool unknown = false;
    return CalendarStore::parseEvent(text, QStringLiteral("/x/e.ics"), QStringLiteral("x"), &unknown);
}

const QString kDailySeries = QStringLiteral(
    "UID:d@example.org\r\nDTSTAMP:20260901T000000Z\r\n"
    "DTSTART;TZID=Europe/Rome:20260921T100000\r\nDTEND;TZID=Europe/Rome:20260921T110000\r\n"
    "RRULE:FREQ=DAILY;COUNT=5\r\nSUMMARY:Daily\r\n");

QDateTime rome(int d, int h)
{
    return QDateTime(QDate(2026, 9, d), QTime(h, 0), QTimeZone("Europe/Rome"));
}

const QString kRichEvent = QStringLiteral(
    "UID:rich@example.org\r\nDTSTAMP:20260901T000000Z\r\nSEQUENCE:2\r\n"
    "DTSTART;TZID=Europe/Rome:20260922T100000\r\nDTEND;TZID=Europe/Rome:20260922T110000\r\n"
    "SUMMARY:Review\r\nORGANIZER;CN=Me:mailto:me@example.org\r\n"
    "ATTENDEE;CN=Other;PARTSTAT=ACCEPTED:mailto:other@example.org\r\n"
    "X-EXAMPLE-FLAG:keep me\r\n"
    "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Reminder\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n");

} // namespace

class TestCalendarStore : public QObject
{
    Q_OBJECT

private slots:
    void parsesAUtcEvent();
    void readsATzidWithItsVtimezone();
    void readsATzidWithoutAVtimezoneAsIana();
    void readsAnUnknownTzidAsLocalAndSaysSo();
    void readsFloatingTimeAsLocal();
    void readsAnAllDayEventWithAnExclusiveEnd();
    void loadReadsCollectionsAndFallsBack();
    void loadSkipsAndCountsABrokenFile();
    void expandsAWeeklyEventAcrossDstInItsOwnZone();
    void expandsAnAllDayEventOntoOneDay();
    void removesAnExdate();
    void anOverrideReplacesItsSlotAndMovesFreely();
    void anInfiniteSeriesYieldsOnlyTheWindow();
    void honoursCountAndUntil();
    void expandsMonthlyLastFridayAndYearlyByMonth();
    void readsTheRepeatRuleExdatesAndOverrides();
    void expandsADenseOldSeriesToTheWindow();
    void anAllDayOverrideKeepsItsOwnAllDayFlag();
    void editableOnlyWhenTheUserOrganisesIt();

    void anEditKeepsWhatTheFormDoesNotOwn();
    void anEditKeepsTheEventsZone();
    void anEditRewritesTheRepeatRuleButNeverACustomOne();
    void anEditCanTurnAnEventAllDayAndBack();
    void aNewEventIsCompleteAndEmbedsItsZone();
    void sameMeaningIgnoresFormatting();
    void sameMeaningMatchesOverridesRegardlessOfOrder();
    void sameMeaningTreatsExdatesAsASet();
    void sameMeaningRejectsDifferentUids();

    void editingOneOccurrenceWritesAnOverride();
    void editingTheSameOccurrenceAgainReplacesItsOverride();
    void stoppingASeriesDropsItsOverrides();
    void deletingOneOccurrenceAddsAnExdateAndDropsItsOverride();
    void deletingOneOccurrenceKeepsExistingExdates();

    void theLiveVdirLoadsCleanly();
};

void TestCalendarStore::parsesAUtcEvent()
{
    bool unknownZone = true;
    const CalEvent event = CalendarStore::parseEvent(
        ics(vevent(QStringLiteral(
            "UID:utc-1@example.org\r\nDTSTAMP:20260901T000000Z\r\n"
            "SUMMARY:Standup\r\nLOCATION:Room 2\r\n"
            "DTSTART:20260922T080000Z\r\nDTEND:20260922T081500Z\r\n"
            "SEQUENCE:3\r\n"))),
        QStringLiteral("/tmp/x/utc-1.ics"), QStringLiteral("x"), &unknownZone);

    QCOMPARE(event.uid, QStringLiteral("utc-1@example.org"));
    QCOMPARE(event.summary, QStringLiteral("Standup"));
    QCOMPARE(event.location, QStringLiteral("Room 2"));
    QCOMPARE(event.start, QDateTime(QDate(2026, 9, 22), QTime(8, 0), QTimeZone::utc()));
    QCOMPARE(event.end, QDateTime(QDate(2026, 9, 22), QTime(8, 15), QTimeZone::utc()));
    QVERIFY(!event.allDay);
    QCOMPARE(event.sequence, 3);
    QCOMPARE(event.filePath, QStringLiteral("/tmp/x/utc-1.ics"));
    QVERIFY(!unknownZone);
    QVERIFY(!event.rawText.isEmpty());
}

void TestCalendarStore::readsATzidWithItsVtimezone()
{
    const CalEvent e = parse(QString::fromLatin1(kRomeVtimezone) + vevent(QStringLiteral(
        "UID:a@example.org\r\nDTSTART;TZID=Europe/Rome:20260922T100000\r\n"
        "DTEND;TZID=Europe/Rome:20260922T110000\r\n")));
    // 10:00 in Rome in September is 08:00 UTC (CEST, +2).
    QCOMPARE(e.start.toUTC(), QDateTime(QDate(2026, 9, 22), QTime(8, 0), QTimeZone::utc()));
    QCOMPARE(e.end.toUTC(), QDateTime(QDate(2026, 9, 22), QTime(9, 0), QTimeZone::utc()));
}

void TestCalendarStore::readsATzidWithoutAVtimezoneAsIana()
{
    // 120 of the user's 312 files are shaped like this: RFC 5545 requires the
    // VTIMEZONE, and real servers omit it.
    bool unknown = true;
    const CalEvent e = parse(vevent(QStringLiteral(
        "UID:b@example.org\r\nDTSTART;TZID=Europe/Rome:20260922T100000\r\n"
        "DTEND;TZID=Europe/Rome:20260922T110000\r\n")), &unknown);
    QCOMPARE(e.start.toUTC(), QDateTime(QDate(2026, 9, 22), QTime(8, 0), QTimeZone::utc()));
    QVERIFY(!unknown);
}

void TestCalendarStore::readsAnUnknownTzidAsLocalAndSaysSo()
{
    bool unknown = false;
    const CalEvent e = parse(vevent(QStringLiteral(
        "UID:c@example.org\r\nDTSTART;TZID=Not A Zone:20260922T100000\r\n"
        "DTEND;TZID=Not A Zone:20260922T110000\r\n")), &unknown);
    QVERIFY(unknown);
    QCOMPARE(e.start, QDateTime(QDate(2026, 9, 22), QTime(10, 0)));
}

void TestCalendarStore::readsFloatingTimeAsLocal()
{
    const CalEvent e = parse(vevent(QStringLiteral(
        "UID:d@example.org\r\nDTSTART:20260922T100000\r\nDTEND:20260922T110000\r\n")));
    QCOMPARE(e.start, QDateTime(QDate(2026, 9, 22), QTime(10, 0)));
    QCOMPARE(e.start.timeSpec(), Qt::LocalTime);
}

void TestCalendarStore::readsAnAllDayEventWithAnExclusiveEnd()
{
    const CalEvent e = parse(vevent(QStringLiteral(
        "UID:e@example.org\r\nDTSTART;VALUE=DATE:20260924\r\nDTEND;VALUE=DATE:20260925\r\n")));
    QVERIFY(e.allDay);
    QCOMPARE(e.start.date(), QDate(2026, 9, 24));
    // Exclusive: a one-day event on the 24th ends at the START of the 25th.
    QCOMPARE(e.end.date(), QDate(2026, 9, 25));
    QCOMPARE(e.end.time(), QTime(0, 0));
}

void TestCalendarStore::loadReadsCollectionsAndFallsBack()
{
    QTemporaryDir dir;
    const QString root = dir.path();
    writeFile(root + QStringLiteral("/52/displayname"), "Work");
    writeFile(root + QStringLiteral("/52/color"), "#60a5fa\n");
    writeFile(root + QStringLiteral("/52/one.ics"), ics(vevent(QStringLiteral(
        "UID:one@example.org\r\nDTSTART:20260922T080000Z\r\n"))));
    writeFile(root + QStringLiteral("/31/two.ics"), ics(vevent(QStringLiteral(
        "UID:two@example.org\r\nDTSTART:20260923T080000Z\r\n"))));
    writeFile(root + QStringLiteral("/31/notes.txt"), "not a calendar file");

    const LoadResult r = CalendarStore::load(root);
    QCOMPARE(r.collections.size(), 2);
    QCOMPARE(r.events.size(), 2);
    QCOMPARE(r.unparsable, 0);

    // Sorted by display name, so "31" (no displayname) sorts before "Work".
    QCOMPARE(r.collections[0].dir, QStringLiteral("31"));
    QCOMPARE(r.collections[0].displayName, QStringLiteral("31"));
    QVERIFY(r.collections[0].color.isValid());
    QCOMPARE(r.collections[1].displayName, QStringLiteral("Work"));
    QCOMPARE(r.collections[1].color, QColor(QStringLiteral("#60a5fa")));
    QVERIFY(!r.collections[1].readOnly);

    // The hashed fallback is stable across loads, or colours would shuffle.
    QCOMPARE(CalendarStore::load(root).collections[0].color, r.collections[0].color);
}

void TestCalendarStore::loadSkipsAndCountsABrokenFile()
{
    QTemporaryDir dir;
    writeFile(dir.path() + QStringLiteral("/c/good.ics"), ics(vevent(QStringLiteral(
        "UID:good@example.org\r\nDTSTART:20260922T080000Z\r\n"))));
    writeFile(dir.path() + QStringLiteral("/c/bad.ics"), "this is not iCalendar");
    const LoadResult r = CalendarStore::load(dir.path());
    QCOMPARE(r.events.size(), 1);
    QCOMPARE(r.unparsable, 1);
}

void TestCalendarStore::expandsAWeeklyEventAcrossDstInItsOwnZone()
{
    // 2026-10-25 is the last Sunday of October: Rome leaves CEST (+2) for CET
    // (+1). A weekly 10:00 meeting must stay at 10:00 in Rome on both sides,
    // which means its UTC time MOVES. Expanding in UTC would keep 08:00Z and
    // draw the second one at 09:00 local.
    const QTimeZone rome("Europe/Rome");
    const QList<Occurrence> occ = expand(vevent(QStringLiteral(
        "UID:w@example.org\r\nDTSTART;TZID=Europe/Rome:20261019T100000\r\n"
        "DTEND;TZID=Europe/Rome:20261019T110000\r\nRRULE:FREQ=WEEKLY\r\n")),
        utc(2026, 10, 18), utc(2026, 11, 1));
    QCOMPARE(occ.size(), 2);
    QCOMPARE(occ[0].start.toTimeZone(rome).time(), QTime(10, 0));
    QCOMPARE(occ[1].start.toTimeZone(rome).time(), QTime(10, 0));
    QCOMPARE(occ[0].start.toUTC().time(), QTime(8, 0));
    QCOMPARE(occ[1].start.toUTC().time(), QTime(9, 0));
    QCOMPARE(occ[1].end.toTimeZone(rome).time(), QTime(11, 0));
}

void TestCalendarStore::expandsAnAllDayEventOntoOneDay()
{
    const QList<Occurrence> occ = expand(vevent(QStringLiteral(
        "UID:ad@example.org\r\nDTSTART;VALUE=DATE:20260924\r\nDTEND;VALUE=DATE:20260925\r\n")),
        QDateTime(QDate(2026, 9, 25), QTime(0, 0)), QDateTime(QDate(2026, 9, 26), QTime(0, 0)));
    // The window starts on the 25th, where the event has already ENDED: an
    // inclusive end would draw it on the 25th too.
    QCOMPARE(occ.size(), 0);
}

void TestCalendarStore::removesAnExdate()
{
    const QList<Occurrence> occ = expand(vevent(QStringLiteral(
        "UID:x@example.org\r\nDTSTART:20260921T080000Z\r\nDTEND:20260921T090000Z\r\n"
        "RRULE:FREQ=DAILY\r\nEXDATE:20260922T080000Z\r\n")),
        utc(2026, 9, 21), utc(2026, 9, 24));
    QCOMPARE(occ.size(), 2);
    QCOMPARE(occ[0].start, utc(2026, 9, 21, 8));
    QCOMPARE(occ[1].start, utc(2026, 9, 23, 8));
}

void TestCalendarStore::anOverrideReplacesItsSlotAndMovesFreely()
{
    // A five-day series (21st-25th) whose 22nd is moved to the 30th. A window
    // over the 21st-23rd must NOT show the 22nd, since its slot is taken; a
    // window over the 30th, where the series has no slot at all, MUST show
    // the override, since an override is placed by its own start.
    const QString body = vevent(QStringLiteral(
        "UID:o@example.org\r\nDTSTART:20260921T080000Z\r\nDTEND:20260921T090000Z\r\n"
        "RRULE:FREQ=DAILY;COUNT=5\r\nSUMMARY:Series\r\n"))
        + vevent(QStringLiteral(
        "UID:o@example.org\r\nRECURRENCE-ID:20260922T080000Z\r\n"
        "DTSTART:20260930T150000Z\r\nDTEND:20260930T160000Z\r\nSUMMARY:Moved\r\n"));
    CalEvent event;
    QList<Occurrence> occ = expand(body, utc(2026, 9, 21), utc(2026, 9, 24), &event);
    QCOMPARE(occ.size(), 2);  // the 21st and 23rd
    QCOMPARE(event.overrides.size(), 1);
    QCOMPARE(event.overrides[0].summary, QStringLiteral("Moved"));

    occ = expand(body, utc(2026, 9, 30), utc(2026, 10, 1));
    QCOMPARE(occ.size(), 1);  // COUNT=5 ends on the 25th; only the override is here
    QVERIFY(occ[0].isOverride);
    QCOMPARE(occ[0].start, utc(2026, 9, 30, 15));
    QCOMPARE(occ[0].recurrenceId, utc(2026, 9, 22, 8));
}

void TestCalendarStore::anAllDayOverrideKeepsItsOwnAllDayFlag()
{
    // The master is timed, the override is an all-day "day off". The override
    // occurrence must carry its OWN all-day state, not the master's.
    const QString body = vevent(QStringLiteral(
        "UID:adov@example.org\r\nDTSTART:20260921T080000Z\r\nDTEND:20260921T090000Z\r\n"
        "RRULE:FREQ=DAILY\r\n"))
        + vevent(QStringLiteral(
        "UID:adov@example.org\r\nRECURRENCE-ID:20260922T080000Z\r\n"
        "DTSTART;VALUE=DATE:20260922\r\nDTEND;VALUE=DATE:20260923\r\nSUMMARY:Day off\r\n"));
    const QDateTime day0(QDate(2026, 9, 22), QTime(0, 0));
    const QList<Occurrence> occ = expand(body, day0, day0.addDays(1));
    QCOMPARE(occ.size(), 1);
    QVERIFY(occ[0].isOverride);
    QVERIFY(occ[0].allDay);
    QCOMPARE(occ[0].recurrenceId, utc(2026, 9, 22, 8));
}

void TestCalendarStore::anInfiniteSeriesYieldsOnlyTheWindow()
{
    const QList<Occurrence> occ = expand(vevent(QStringLiteral(
        "UID:inf@example.org\r\nDTSTART:20000101T080000Z\r\nRRULE:FREQ=DAILY\r\n")),
        utc(2026, 9, 1), utc(2026, 9, 8));
    QCOMPARE(occ.size(), 7);
    QCOMPARE(occ.first().start, utc(2026, 9, 1, 8));
}

void TestCalendarStore::expandsADenseOldSeriesToTheWindow()
{
    // A daily series from 1970 needs ~20700 iterator steps to reach a 2026
    // window. At a 10000 cap the walk dies around 1997 and returns nothing.
    const QList<Occurrence> occ = expand(vevent(QStringLiteral(
        "UID:dense@example.org\r\nDTSTART:19700101T080000Z\r\n"
        "DTEND:19700101T090000Z\r\nRRULE:FREQ=DAILY\r\n")),
        utc(2026, 9, 21), utc(2026, 9, 28));
    QCOMPARE(occ.size(), 7);
    QCOMPARE(occ.first().start, utc(2026, 9, 21, 8));
    QCOMPARE(occ.last().start, utc(2026, 9, 27, 8));
}

void TestCalendarStore::honoursCountAndUntil()
{
    QCOMPARE(expand(vevent(QStringLiteral(
        "UID:c@example.org\r\nDTSTART:20260901T080000Z\r\nRRULE:FREQ=DAILY;COUNT=3\r\n")),
        utc(2026, 9, 1), utc(2026, 10, 1)).size(), 3);
    QCOMPARE(expand(vevent(QStringLiteral(
        "UID:u@example.org\r\nDTSTART:20260901T080000Z\r\n"
        "RRULE:FREQ=DAILY;UNTIL=20260905T080000Z\r\n")),
        utc(2026, 9, 1), utc(2026, 10, 1)).size(), 5);
}

void TestCalendarStore::expandsMonthlyLastFridayAndYearlyByMonth()
{
    // Last Fridays: 2026-09-25, 2026-10-30.
    QList<Occurrence> occ = expand(vevent(QStringLiteral(
        "UID:lf@example.org\r\nDTSTART:20260925T080000Z\r\nRRULE:FREQ=MONTHLY;BYDAY=-1FR\r\n")),
        utc(2026, 9, 1), utc(2026, 11, 1));
    QCOMPARE(occ.size(), 2);
    QCOMPARE(occ[1].start.date(), QDate(2026, 10, 30));

    // Last Friday of September: 2026-09-25, 2027-09-24.
    occ = expand(vevent(QStringLiteral(
        "UID:ly@example.org\r\nDTSTART:20260925T080000Z\r\n"
        "RRULE:FREQ=YEARLY;BYMONTH=9;BYDAY=-1FR\r\n")),
        utc(2026, 1, 1), utc(2028, 1, 1));
    QCOMPARE(occ.size(), 2);
    QCOMPARE(occ[1].start.date(), QDate(2027, 9, 24));
}

void TestCalendarStore::readsTheRepeatRuleExdatesAndOverrides()
{
    CalEvent e;
    expand(vevent(QStringLiteral(
        "UID:r@example.org\r\nDTSTART:20260921T080000Z\r\n"
        "RRULE:FREQ=MONTHLY;BYDAY=-1FR\r\nEXDATE:20261030T080000Z\r\n")),
        utc(2026, 9, 1), utc(2026, 9, 2), &e);
    QCOMPARE(e.repeat.freq, RepeatRule::Freq::Monthly);
    QCOMPARE(e.repeat.ordinal, -1);
    QCOMPARE(e.exdates, QList<QDateTime>{ utc(2026, 10, 30, 8) });
}

void TestCalendarStore::editableOnlyWhenTheUserOrganisesIt()
{
    const QStringList own = { QStringLiteral("me@example.org"), QStringLiteral("alt@example.org") };
    CalCollection writable;
    writable.dir = QStringLiteral("x");
    CalCollection readOnly = writable;
    readOnly.readOnly = true;

    const CalEvent mine = parse(vevent(QStringLiteral(
        "UID:1@example.org\r\nDTSTART:20260922T080000Z\r\n"
        "ORGANIZER;CN=Me:mailto:ME@Example.org\r\n"
        "ATTENDEE;CN=Other;PARTSTAT=ACCEPTED:mailto:other@example.org\r\n")));
    const CalEvent theirs = parse(vevent(QStringLiteral(
        "UID:2@example.org\r\nDTSTART:20260922T080000Z\r\n"
        "ORGANIZER;CN=Other:mailto:other@example.org\r\n"
        "ATTENDEE:mailto:me@example.org\r\n")));
    const CalEvent nobodys = parse(vevent(QStringLiteral(
        "UID:3@example.org\r\nDTSTART:20260922T080000Z\r\n")));

    // Case-insensitive, and "mailto:" is not part of the address.
    QVERIFY(CalendarStore::isEditable(mine, writable, own));
    QVERIFY(!CalendarStore::isEditable(theirs, writable, own));
    QVERIFY(CalendarStore::isEditable(nobodys, writable, own));
    QVERIFY(!CalendarStore::isEditable(nobodys, readOnly, own));

    QCOMPARE(mine.organizer.name, QStringLiteral("Me"));
    QCOMPARE(mine.attendees.size(), 1);
    QCOMPARE(mine.attendees[0].partstat, QStringLiteral("ACCEPTED"));
    QCOMPARE(mine.attendees[0].address, QStringLiteral("other@example.org"));
}

void TestCalendarStore::anEditKeepsWhatTheFormDoesNotOwn()
{
    const CalEvent before = parse(vevent(kRichEvent));
    EventEdit edit = editOf(before);
    edit.summary = QStringLiteral("Review, moved");
    const QByteArray after = CalendarStore::applyEdit(
        before.rawText, edit, CalendarStore::Scope::All, {});
    QVERIFY(!after.isEmpty());

    const CalEvent e = reparse(after);
    QCOMPARE(e.summary, QStringLiteral("Review, moved"));
    QCOMPARE(e.uid, before.uid);
    QCOMPARE(e.sequence, 3);  // bumped
    // Value for value, not byte for byte: libical re-serialises (plan ruling 2).
    QVERIFY(e.hasAlarm);
    QVERIFY(after.contains("TRIGGER:-PT15M"));
    QVERIFY(after.contains("X-EXAMPLE-FLAG:keep me"));
    QCOMPARE(e.attendees.size(), 1);
    QCOMPARE(e.attendees[0].partstat, QStringLiteral("ACCEPTED"));
    QVERIFY(after.contains("LAST-MODIFIED:"));
}

void TestCalendarStore::anEditKeepsTheEventsZone()
{
    // The source has NO VTIMEZONE, as 120 of the user's files do: the TZID
    // must still be written back, not converted to UTC or floating.
    const CalEvent before = parse(vevent(kRichEvent));
    EventEdit edit = editOf(before);
    edit.start = before.start.addSecs(3600);
    edit.end = before.end.addSecs(3600);
    const QByteArray after = CalendarStore::applyEdit(before.rawText, edit, CalendarStore::Scope::All, {});
    QVERIFY2(after.contains("DTSTART;TZID=Europe/Rome:20260922T110000"), after.constData());
    QCOMPARE(reparse(after).start, edit.start);
}

void TestCalendarStore::anEditRewritesTheRepeatRuleButNeverACustomOne()
{
    const CalEvent weekly = parse(vevent(QStringLiteral(
        "UID:r@example.org\r\nDTSTART:20260921T080000Z\r\nRRULE:FREQ=WEEKLY;WKST=MO\r\n")));
    EventEdit edit = editOf(weekly);
    edit.repeat = RepeatRule::fromRRule(QStringLiteral("FREQ=MONTHLY;BYDAY=-1FR"), weekly.start.date());
    QByteArray after = CalendarStore::applyEdit(weekly.rawText, edit, CalendarStore::Scope::All, {});
    QCOMPARE(reparse(after).repeat.toRRule(), QStringLiteral("FREQ=MONTHLY;BYDAY=-1FR"));
    QCOMPARE(after.count("RRULE"), 1);

    const CalEvent custom = parse(vevent(QStringLiteral(
        "UID:c@example.org\r\nDTSTART:20260921T080000Z\r\n"
        "RRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1\r\n")));
    edit = editOf(custom);
    edit.summary = QStringLiteral("Renamed");
    after = CalendarStore::applyEdit(custom.rawText, edit, CalendarStore::Scope::All, {});
    QVERIFY(after.contains("BYSETPOS=-1"));

    edit.repeat = RepeatRule();  // Does not repeat
    after = CalendarStore::applyEdit(custom.rawText, edit, CalendarStore::Scope::All, {});
    QVERIFY(!after.contains("RRULE"));
}

void TestCalendarStore::anEditCanTurnAnEventAllDayAndBack()
{
    const CalEvent timed = parse(vevent(kRichEvent));
    EventEdit edit = editOf(timed);
    edit.allDay = true;
    edit.start = QDateTime(QDate(2026, 9, 22), QTime(0, 0));
    edit.end = QDateTime(QDate(2026, 9, 23), QTime(0, 0));
    const QByteArray allDay = CalendarStore::applyEdit(timed.rawText, edit, CalendarStore::Scope::All, {});
    QVERIFY(allDay.contains("DTSTART;VALUE=DATE:20260922"));
    QVERIFY(allDay.contains("DTEND;VALUE=DATE:20260923"));

    const CalEvent back = reparse(allDay);
    edit = editOf(back);
    edit.allDay = false;
    edit.start = QDateTime(QDate(2026, 9, 22), QTime(9, 0), QTimeZone("Europe/Rome"));
    edit.end = edit.start.addSecs(3600);
    const QByteArray timedAgain = CalendarStore::applyEdit(allDay, edit, CalendarStore::Scope::All, {});
    const CalEvent e = reparse(timedAgain);
    QVERIFY(!e.allDay);
    QCOMPARE(e.start, edit.start);
}

void TestCalendarStore::aNewEventIsCompleteAndEmbedsItsZone()
{
    EventEdit edit;
    edit.summary = QStringLiteral("Dentist");
    edit.start = QDateTime(QDate(2026, 9, 24), QTime(15, 0), QTimeZone("Europe/Rome"));
    edit.end = edit.start.addSecs(1800);
    const QByteArray text = CalendarStore::newEvent(edit, "Europe/Rome");
    QVERIFY(text.contains("BEGIN:VCALENDAR"));
    QVERIFY(text.contains("PRODID:-//qtmaildir//EN"));
    QVERIFY(text.contains("BEGIN:VTIMEZONE"));
    QVERIFY(text.contains("TZID:Europe/Rome"));
    QVERIFY(text.contains("SEQUENCE:0"));
    QVERIFY(text.contains("DTSTAMP:"));
    const CalEvent e = reparse(text);
    QVERIFY(!e.uid.isEmpty());
    QCOMPARE(e.summary, QStringLiteral("Dentist"));
    QCOMPARE(e.start, edit.start);
    QCOMPARE(e.end, edit.end);

    // Two new events never share a UID.
    QVERIFY(reparse(CalendarStore::newEvent(edit, "Europe/Rome")).uid != e.uid);
}

void TestCalendarStore::sameMeaningIgnoresFormatting()
{
    const QByteArray a = ics(vevent(QStringLiteral(
        "UID:s@example.org\r\nDTSTART:20260922T080000Z\r\nSUMMARY:Standup\r\n")));
    // Folded, properties reordered, DTSTAMP added: what a server may send back.
    const QByteArray b = ics(vevent(QStringLiteral(
        "SUMMARY:Stand\r\n up\r\nDTSTAMP:20260930T000000Z\r\n"
        "DTSTART:20260922T080000Z\r\nUID:s@example.org\r\n")));
    const QByteArray moved = ics(vevent(QStringLiteral(
        "UID:s@example.org\r\nDTSTART:20260922T090000Z\r\nSUMMARY:Standup\r\n")));
    QVERIFY(CalendarStore::sameMeaning(a, b));
    QVERIFY(!CalendarStore::sameMeaning(a, moved));
}

void TestCalendarStore::sameMeaningMatchesOverridesRegardlessOfOrder()
{
    // Two SERVER-normalised texts: the same master and the same two overrides,
    // but the overrides are separate VEVENTs and their file order is not
    // semantic, so a reordered pair still describes one event.
    const QString master = vevent(QStringLiteral(
        "UID:ord@example.org\r\nDTSTART:20260921T080000Z\r\nDTEND:20260921T090000Z\r\n"
        "RRULE:FREQ=DAILY;COUNT=5\r\nSUMMARY:Series\r\n"));
    const QString first = vevent(QStringLiteral(
        "UID:ord@example.org\r\nRECURRENCE-ID:20260922T080000Z\r\n"
        "DTSTART:20260922T080000Z\r\nDTEND:20260922T090000Z\r\nSUMMARY:Moved A\r\n"));
    const QString second = vevent(QStringLiteral(
        "UID:ord@example.org\r\nRECURRENCE-ID:20260923T080000Z\r\n"
        "DTSTART:20260923T100000Z\r\nDTEND:20260923T110000Z\r\nSUMMARY:Moved B\r\n"));
    const QByteArray a = ics(master + first + second);
    const QByteArray b = ics(master + second + first);
    QVERIFY(CalendarStore::sameMeaning(a, b));

    // The order is only ignored, not the contents.
    const QString changed = vevent(QStringLiteral(
        "UID:ord@example.org\r\nRECURRENCE-ID:20260923T080000Z\r\n"
        "DTSTART:20260923T100000Z\r\nDTEND:20260923T110000Z\r\nSUMMARY:Moved C\r\n"));
    QVERIFY(!CalendarStore::sameMeaning(a, ics(master + second + changed)));
}

void TestCalendarStore::sameMeaningTreatsExdatesAsASet()
{
    // EXDATE properties are a set; a server may reorder them on the round
    // trip, and a list comparison would then warn about a version it kept.
    const QByteArray a = ics(vevent(QStringLiteral(
        "UID:ex@example.org\r\nDTSTART:20260922T080000Z\r\n"
        "EXDATE:20260923T080000Z\r\nEXDATE:20260924T080000Z\r\n")));
    const QByteArray b = ics(vevent(QStringLiteral(
        "UID:ex@example.org\r\nDTSTART:20260922T080000Z\r\n"
        "EXDATE:20260924T080000Z\r\nEXDATE:20260923T080000Z\r\n")));
    QVERIFY(CalendarStore::sameMeaning(a, b));

    // Only the order is ignored, not the contents.
    const QByteArray changed = ics(vevent(QStringLiteral(
        "UID:ex@example.org\r\nDTSTART:20260922T080000Z\r\n"
        "EXDATE:20260923T080000Z\r\nEXDATE:20260925T080000Z\r\n")));
    QVERIFY(!CalendarStore::sameMeaning(a, changed));
}

void TestCalendarStore::sameMeaningRejectsDifferentUids()
{
    // Identical fields, different events: the UID is the identity.
    const QByteArray a = ics(vevent(QStringLiteral(
        "UID:one@example.org\r\nDTSTART:20260922T080000Z\r\nSUMMARY:Standup\r\n")));
    const QByteArray b = ics(vevent(QStringLiteral(
        "UID:two@example.org\r\nDTSTART:20260922T080000Z\r\nSUMMARY:Standup\r\n")));
    QVERIFY(!CalendarStore::sameMeaning(a, b));
    QVERIFY(CalendarStore::sameMeaning(a, a));
}

void TestCalendarStore::editingOneOccurrenceWritesAnOverride()
{
    const CalEvent series = parse(vevent(kDailySeries));
    EventEdit edit = editOf(series);
    edit.summary = QStringLiteral("Just this one");
    edit.start = rome(22, 15);
    edit.end = rome(22, 16);
    const QByteArray after = CalendarStore::applyEdit(
        series.rawText, edit, CalendarStore::Scope::ThisOccurrence, rome(22, 10));

    const CalEvent e = reparse(after);
    QCOMPARE(e.summary, QStringLiteral("Daily"));          // master untouched
    QCOMPARE(e.repeat.count, 5);
    QCOMPARE(e.overrides.size(), 1);
    QCOMPARE(e.overrides[0].recurrenceId, rome(22, 10));
    QCOMPARE(e.overrides[0].summary, QStringLiteral("Just this one"));
    QCOMPARE(after.count("RRULE"), 1);                     // the override carries none

    const QList<Occurrence> occ = CalendarStore::occurrences(
        { e }, rome(21, 0), rome(26, 0));
    QCOMPARE(occ.size(), 5);                               // still five, one moved
    QVERIFY(std::any_of(occ.cbegin(), occ.cend(),
                        [](const Occurrence &o) { return o.isOverride && o.start == rome(22, 15); }));
}

void TestCalendarStore::editingTheSameOccurrenceAgainReplacesItsOverride()
{
    const CalEvent series = parse(vevent(kDailySeries));
    EventEdit edit = editOf(series);
    edit.start = rome(22, 15);
    edit.end = rome(22, 16);
    QByteArray text = CalendarStore::applyEdit(
        series.rawText, edit, CalendarStore::Scope::ThisOccurrence, rome(22, 10));
    edit.start = rome(22, 17);
    edit.end = rome(22, 18);
    text = CalendarStore::applyEdit(text, edit, CalendarStore::Scope::ThisOccurrence, rome(22, 10));
    const CalEvent e = reparse(text);
    QCOMPARE(e.overrides.size(), 1);
    QCOMPARE(e.overrides[0].start, rome(22, 17));
}

void TestCalendarStore::stoppingASeriesDropsItsOverrides()
{
    // A series with one overridden occurrence, then the repeat is stopped
    // (Scope::All with RepeatRule() = Freq::None). The override is not a real
    // occurrence any more: occurrences() never reads it for a non-repeating
    // event, so leaving it in the file is invalid iCalendar that other clients
    // may render as a phantom, and it would spring back if a repeat were
    // re-enabled. EXDATEs are left inert.
    const CalEvent series = parse(vevent(kDailySeries));
    EventEdit edit = editOf(series);
    edit.summary = QStringLiteral("Just this one");
    edit.start = rome(22, 15);
    edit.end = rome(22, 16);
    const QByteArray withOverride = CalendarStore::applyEdit(
        series.rawText, edit, CalendarStore::Scope::ThisOccurrence, rome(22, 10));
    QCOMPARE(reparse(withOverride).overrides.size(), 1);

    EventEdit stop = editOf(reparse(withOverride));
    stop.repeat = RepeatRule();  // does not repeat
    const QByteArray after = CalendarStore::applyEdit(
        withOverride, stop, CalendarStore::Scope::All, {});

    const CalEvent e = reparse(after);
    QVERIFY(e.overrides.isEmpty());
    QVERIFY2(!after.contains("RECURRENCE-ID"), after.constData());
    QVERIFY2(!after.contains("RRULE"), after.constData());
}

void TestCalendarStore::deletingOneOccurrenceAddsAnExdateAndDropsItsOverride()
{
    const CalEvent series = parse(vevent(kDailySeries));
    EventEdit edit = editOf(series);
    edit.start = rome(23, 15);
    edit.end = rome(23, 16);
    const QByteArray withOverride = CalendarStore::applyEdit(
        series.rawText, edit, CalendarStore::Scope::ThisOccurrence, rome(23, 10));

    const QByteArray after = CalendarStore::deleteOccurrence(withOverride, rome(23, 10));
    const CalEvent e = reparse(after);
    QCOMPARE(e.overrides.size(), 0);
    QCOMPARE(e.exdates, QList<QDateTime>{ rome(23, 10) });
    QVERIFY2(after.contains("EXDATE;TZID=Europe/Rome:20260923T100000"), after.constData());
    QCOMPARE(CalendarStore::occurrences({ e }, rome(21, 0), rome(26, 0)).size(), 4);
}

void TestCalendarStore::deletingOneOccurrenceKeepsExistingExdates()
{
    // A series that already skips the 21st; deleting the 22nd must leave that
    // existing exception alone. A setTime-style replace would drop it.
    const QString body = QStringLiteral(
        "UID:d@example.org\r\nDTSTAMP:20260901T000000Z\r\n"
        "DTSTART;TZID=Europe/Rome:20260921T100000\r\nDTEND;TZID=Europe/Rome:20260921T110000\r\n"
        "RRULE:FREQ=DAILY;COUNT=5\r\n"
        "EXDATE;TZID=Europe/Rome:20260921T100000\r\nSUMMARY:Daily\r\n");
    const QByteArray after = CalendarStore::deleteOccurrence(ics(vevent(body)), rome(22, 10));

    const CalEvent e = reparse(after);
    QVERIFY(e.exdates.contains(rome(21, 10)));
    QVERIFY(e.exdates.contains(rome(22, 10)));
    QCOMPARE(e.exdates.size(), 2);
    QCOMPARE(CalendarStore::occurrences({ e }, rome(21, 0), rome(26, 0)).size(), 3);
}

void TestCalendarStore::theLiveVdirLoadsCleanly()
{
    // Opt-in, READ-ONLY: never writes. Run by hand before handing the build
    // over: QTMAILDIR_LIVE_CALENDARS=~/.local/share/calendars ./test_calendarstore
    const QString dir = qEnvironmentVariable("QTMAILDIR_LIVE_CALENDARS");
    if (dir.isEmpty())
        QSKIP("QTMAILDIR_LIVE_CALENDARS not set");
    const LoadResult r = CalendarStore::load(dir);
    qInfo("collections=%lld events=%lld unparsable=%d unknownZones=%d",
          qlonglong(r.collections.size()), qlonglong(r.events.size()),
          r.unparsable, r.unknownZones);
    QCOMPARE(r.unparsable, 0);
    const QList<Occurrence> year = CalendarStore::occurrences(
        r.events, QDateTime(QDate(2026, 1, 1), QTime(0, 0)),
        QDateTime(QDate(2027, 1, 1), QTime(0, 0)));
    qInfo("occurrences in 2026=%lld", qlonglong(year.size()));
}

QTEST_MAIN(TestCalendarStore)
#include "test_calendarstore.moc"
