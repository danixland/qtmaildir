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

QTEST_MAIN(TestCalendarStore)
#include "test_calendarstore.moc"
