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
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "calendarsync.h"

namespace {

/// A script that appends a line to `log` each run and exits with `code`.
QString script(const QTemporaryDir &dir, int code)
{
    const QString path = dir.filePath(QStringLiteral("sync.sh"));
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QStringLiteral("#!/bin/sh\necho run >> %1\necho output\nexit %2\n")
                .arg(dir.filePath(QStringLiteral("log"))).arg(code).toUtf8());
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    return path;
}

int runs(const QTemporaryDir &dir)
{
    QFile f(dir.filePath(QStringLiteral("log")));
    return f.open(QIODevice::ReadOnly) ? f.readAll().count('\n') : 0;
}

} // namespace

class TestCalendarSync : public QObject
{
    Q_OBJECT

private slots:
    void aBurstOfWritesRunsOnce();
    void reportsFailureWithTheOutput();
    void anEmptyCommandNeverRuns();
};

void TestCalendarSync::aBurstOfWritesRunsOnce()
{
    QTemporaryDir dir;
    CalendarSync sync(script(dir, 0), 50);
    QSignalSpy finished(&sync, &CalendarSync::finished);
    sync.schedule();
    sync.schedule();
    sync.schedule();
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(0).toBool(), true);
    QTest::qWait(200);  // nothing else is queued, so nothing else may run
    QCOMPARE(runs(dir), 1);
}

void TestCalendarSync::reportsFailureWithTheOutput()
{
    QTemporaryDir dir;
    CalendarSync sync(script(dir, 3), 0);
    QSignalSpy finished(&sync, &CalendarSync::finished);
    sync.schedule();
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.first().at(0).toBool(), false);
    QVERIFY(finished.first().at(1).toString().contains(QStringLiteral("output")));
}

void TestCalendarSync::anEmptyCommandNeverRuns()
{
    CalendarSync sync(QString(), 0);
    QSignalSpy started(&sync, &CalendarSync::started);
    sync.schedule();
    QTest::qWait(100);
    QCOMPARE(started.count(), 0);
}

QTEST_MAIN(TestCalendarSync)
#include "test_calendarsync.moc"
