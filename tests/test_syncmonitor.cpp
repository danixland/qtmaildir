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

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "syncmonitor.h"

/// SyncMonitor answers one question: is a sync running that this process did
/// not start? The parsing of /proc/locks is the testable part and is kept
/// separate from the polling for exactly that reason.
class TestSyncMonitor : public QObject
{
    Q_OBJECT
private slots:
    void anFlockOnTheWatchedInodeIsHeld();
    void anFlockOnAnotherInodeIsIgnored();
    void aPosixLockOnTheWatchedInodeIsIgnored();
    void emptyContentMeansNotHeld();
    void garbageLinesAreSkippedRatherThanMisread();
    void anUnreadableLockTableIsUnknownNotIdle();
    void aMissingLockFileIsNotHeld();
    void theStateChangeSignalFiresOnlyOnTransitions();
};

void TestSyncMonitor::anFlockOnTheWatchedInodeIsHeld()
{
    // The real shape of a held lock, taken verbatim from /proc/locks while
    // mailsync.sh held /tmp/mbsync.lock: the inode is the last colon-separated
    // field of the device:inode column, not the whole column.
    const QString content =
        QStringLiteral("82: FLOCK  ADVISORY  WRITE 9051 fc:00:12058676 0 EOF\n");
    QVERIFY(SyncMonitor::lockHeldIn(content, 12058676));
}

void TestSyncMonitor::anFlockOnAnotherInodeIsIgnored()
{
    // A busy machine has many flocks. Matching anything but our own inode would
    // report a sync whenever some unrelated program took a lock.
    const QString content =
        QStringLiteral("82: FLOCK  ADVISORY  WRITE 9051 fc:00:99999999 0 EOF\n");
    QVERIFY(!SyncMonitor::lockHeldIn(content, 12058676));
}

void TestSyncMonitor::aPosixLockOnTheWatchedInodeIsIgnored()
{
    // flock(2) and fcntl(2) are separate namespaces in the kernel and cannot
    // see each other; mailsync.sh uses flock(2). A POSIX lock on the same file
    // is somebody else's, and treating it as ours would report a sync that is
    // not running. Verified: fcntl(F_OFD_GETLK) reports UNLOCKED against a
    // held flock, which is why this distinction is not academic.
    const QString content =
        QStringLiteral("1: POSIX  ADVISORY  WRITE 1234 fc:00:12058676 0 EOF\n");
    QVERIFY(!SyncMonitor::lockHeldIn(content, 12058676));
}

void TestSyncMonitor::emptyContentMeansNotHeld()
{
    QVERIFY(!SyncMonitor::lockHeldIn(QString(), 12058676));
}

void TestSyncMonitor::garbageLinesAreSkippedRatherThanMisread()
{
    // /proc/locks gains fields across kernel versions, and a line can be
    // truncated as it is read. A short line must not match by accident, and
    // must not stop the lines after it from being read.
    const QString content = QStringLiteral(
        "not a lock line at all\n"
        "3: FLOCK\n"
        "82: FLOCK  ADVISORY  WRITE 9051 fc:00:12058676 0 EOF\n");
    QVERIFY(SyncMonitor::lockHeldIn(content, 12058676));

    const QString onlyGarbage = QStringLiteral("nonsense\n3: FLOCK\n");
    QVERIFY(!SyncMonitor::lockHeldIn(onlyGarbage, 12058676));
}

void TestSyncMonitor::anUnreadableLockTableIsUnknownNotIdle()
{
    // /proc/locks is Linux-only. Where it cannot be read the honest answer is
    // "unknown", and the indicator stays hidden. Reporting idle would be a
    // claim the monitor cannot support, and it is the claim that matters:
    // "no sync is running" is what lets the window quit.
    SyncMonitor monitor(QStringLiteral("/nonexistent/mbsync.lock"),
                        QStringLiteral("/nonexistent/proc/locks"));
    QCOMPARE(monitor.state(), SyncMonitor::State::Unknown);
    monitor.poll();
    QCOMPARE(monitor.state(), SyncMonitor::State::Unknown);
}

void TestSyncMonitor::aMissingLockFileIsNotHeld()
{
    // Before the first sync ever runs there is no lock file. That is not a
    // sync in progress, and it must not read as unknown either: the lock table
    // is perfectly readable, there is simply nothing holding anything.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QTemporaryFile locks;
    QVERIFY(locks.open());
    locks.write("82: FLOCK  ADVISORY  WRITE 9051 fc:00:12058676 0 EOF\n");
    locks.flush();

    SyncMonitor monitor(dir.filePath(QStringLiteral("never-created.lock")),
                        locks.fileName());
    monitor.poll();
    QCOMPARE(monitor.state(), SyncMonitor::State::Idle);
}

void TestSyncMonitor::theStateChangeSignalFiresOnlyOnTransitions()
{
    // The UI reacts to a sync starting and finishing, so a signal on every
    // poll would repaint the status bar every two seconds forever and would
    // stamp over whatever else had been written there.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString lockPath = dir.filePath(QStringLiteral("mbsync.lock"));
    {
        QFile lock(lockPath);
        QVERIFY(lock.open(QIODevice::WriteOnly));
    }

    const qint64 inode = SyncMonitor::inodeOf(lockPath);
    QVERIFY(inode > 0);

    // A stand-in for /proc/locks whose contents the test controls.
    const QString locksPath = dir.filePath(QStringLiteral("locks"));
    auto writeLocks = [&locksPath](const QString &text) {
        QFile f(locksPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text.toUtf8());
    };
    writeLocks(QString());

    SyncMonitor monitor(lockPath, locksPath);
    QSignalSpy spy(&monitor, &SyncMonitor::stateChanged);

    monitor.poll();
    QCOMPARE(monitor.state(), SyncMonitor::State::Idle);
    QCOMPARE(spy.count(), 1);   // Unknown -> Idle is a real transition.

    monitor.poll();
    monitor.poll();
    QCOMPARE(spy.count(), 1);   // Still idle: no further signals.

    writeLocks(QStringLiteral("82: FLOCK  ADVISORY  WRITE 9051 fc:00:%1 0 EOF\n")
                   .arg(inode));
    monitor.poll();
    QCOMPARE(monitor.state(), SyncMonitor::State::Running);
    QCOMPARE(spy.count(), 2);

    monitor.poll();
    QCOMPARE(spy.count(), 2);   // Still running.

    writeLocks(QString());
    monitor.poll();
    QCOMPARE(monitor.state(), SyncMonitor::State::Idle);
    QCOMPARE(spy.count(), 3);
}

QTEST_MAIN(TestSyncMonitor)

#include "test_syncmonitor.moc"
