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

#include "calendarwriter.h"

namespace {

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestCalendarWriter : public QObject
{
    Q_OBJECT

private slots:
    void createsAFileThatDidNotExist();
    void refusesToCreateOverAnExistingFile();
    void replacesWhenTheFileIsWhatWasExpected();
    void refusesWhenTheFileChangedUnderneath();
    void deletes();
    void leavesNoTemporaryFileBehind();
};

void TestCalendarWriter::createsAFileThatDidNotExist()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.ics"));
    QString error;
    QCOMPARE(CalendarWriter::replace(path, std::nullopt, QByteArray("new"), &error),
             CalendarWriter::Result::Ok);
    QCOMPARE(read(path), QByteArray("new"));
}

void TestCalendarWriter::refusesToCreateOverAnExistingFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.ics"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("synced in meanwhile");
    f.close();
    QString error;
    QCOMPARE(CalendarWriter::replace(path, std::nullopt, QByteArray("new"), &error),
             CalendarWriter::Result::Stale);
    QCOMPARE(read(path), QByteArray("synced in meanwhile"));
}

void TestCalendarWriter::replacesWhenTheFileIsWhatWasExpected()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.ics"));
    QString error;
    CalendarWriter::replace(path, std::nullopt, QByteArray("one"), &error);
    QCOMPARE(CalendarWriter::replace(path, QByteArray("one"), QByteArray("two"), &error),
             CalendarWriter::Result::Ok);
    QCOMPARE(read(path), QByteArray("two"));
}

void TestCalendarWriter::refusesWhenTheFileChangedUnderneath()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.ics"));
    QString error;
    CalendarWriter::replace(path, std::nullopt, QByteArray("synced"), &error);
    // The caller loaded "one"; a sync wrote "synced" since. Nothing is clobbered.
    QCOMPARE(CalendarWriter::replace(path, QByteArray("one"), QByteArray("two"), &error),
             CalendarWriter::Result::Stale);
    QCOMPARE(read(path), QByteArray("synced"));
    // Also stale: expecting a file that a sync has removed.
    QFile::remove(path);
    QCOMPARE(CalendarWriter::replace(path, QByteArray("synced"), QByteArray("two"), &error),
             CalendarWriter::Result::Stale);
    QVERIFY(!QFile::exists(path));
}

void TestCalendarWriter::deletes()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.ics"));
    QString error;
    CalendarWriter::replace(path, std::nullopt, QByteArray("one"), &error);
    QCOMPARE(CalendarWriter::replace(path, QByteArray("one"), std::nullopt, &error),
             CalendarWriter::Result::Ok);
    QVERIFY(!QFile::exists(path));
}

void TestCalendarWriter::leavesNoTemporaryFileBehind()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.ics"));
    QString error;
    CalendarWriter::replace(path, std::nullopt, QByteArray("one"), &error);
    CalendarWriter::replace(path, QByteArray("one"), QByteArray("two"), &error);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden),
             QStringList{ QStringLiteral("a.ics") });
}

QTEST_MAIN(TestCalendarWriter)
#include "test_calendarwriter.moc"
