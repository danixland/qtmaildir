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

} // namespace

class TestCalendarStore : public QObject
{
    Q_OBJECT

private slots:
    void parsesAUtcEvent();
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

QTEST_MAIN(TestCalendarStore)
#include "test_calendarstore.moc"
