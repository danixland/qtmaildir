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

#include "syncmonitor.h"

#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>

namespace {

/// Two seconds. The user chose continuous polling over a poll-only-while-
/// quitting variant, and this is a read of one small procfs file, so the cost
/// is negligible next to noticing a cron sync within a couple of seconds.
constexpr int kDefaultIntervalMs = 2000;

}   // namespace

SyncMonitor::SyncMonitor(const QString &lockPath, const QString &locksPath,
                         QObject *parent)
    : QObject(parent), m_lockPath(lockPath), m_locksPath(locksPath)
{
    m_timer.setInterval(kDefaultIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &SyncMonitor::poll);
}

void SyncMonitor::setInterval(int ms)
{
    m_timer.setInterval(ms);
}

void SyncMonitor::start()
{
    // Poll once immediately: a window opened during a cron sync should say so
    // at once rather than after the first interval.
    poll();
    m_timer.start();
}

void SyncMonitor::stop()
{
    m_timer.stop();
}

QString SyncMonitor::defaultLockPath()
{
    // Must stay equal to LOCKFILE in assets/mailsync.sh.
    return QStringLiteral("/tmp/mbsync.lock");
}

qint64 SyncMonitor::inodeOf(const QString &path)
{
    // Qt exposes no inode accessor, and /proc/locks identifies a file only by
    // device and inode, so this has to come from stat(2) directly. That is also
    // why the whole class is Linux-shaped; see the Unknown state for what
    // happens where /proc/locks does not exist.
    struct stat st;
    if (::stat(QFile::encodeName(path).constData(), &st) != 0)
        return -1;

    return static_cast<qint64>(st.st_ino);
}

bool SyncMonitor::lockHeldIn(const QString &content, qint64 inode)
{
    if (content.isEmpty() || inode < 0)
        return false;

    // A /proc/locks line looks like:
    //   82: FLOCK  ADVISORY  WRITE 9051 fc:00:12058676 0 EOF
    // The inode is the last colon-separated part of the major:minor:inode
    // field. Matching the raw number anywhere in the line would also match a
    // pid or a byte range, so the field is located first and then split.
    const QList<QStringView> lines = QStringView(content).split(u'\n',
                                                               Qt::SkipEmptyParts);
    for (const QStringView &line : lines) {
        const QList<QStringView> fields =
            line.split(u' ', Qt::SkipEmptyParts);

        // Shortest real line still has: index, type, ADVISORY, WRITE, pid,
        // dev:inode. Anything shorter is truncated or not a lock line, and is
        // skipped rather than guessed at.
        if (fields.size() < 6)
            continue;

        // flock(2) only. mailsync.sh uses flock, and a POSIX record lock on
        // the same file belongs to somebody else: the two namespaces cannot
        // see each other, so treating a POSIX entry as ours would report a
        // sync that is not running.
        if (fields.at(1) != QLatin1String("FLOCK"))
            continue;

        for (const QStringView &field : fields) {
            const qsizetype lastColon = field.lastIndexOf(u':');
            if (lastColon < 0)
                continue;

            bool ok = false;
            const qint64 candidate =
                field.mid(lastColon + 1).toLongLong(&ok);
            if (ok && candidate == inode)
                return true;
        }
    }

    return false;
}

void SyncMonitor::poll()
{
    State next = State::Unknown;

    QFile locks(m_locksPath);
    if (locks.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Read in full rather than line by line: /proc/locks is small, and a
        // partial read while the kernel is editing the table could truncate a
        // line mid-field.
        const QString content = QString::fromUtf8(locks.readAll());

        const qint64 inode = inodeOf(m_lockPath);
        if (inode < 0) {
            // No lock file yet, before the first sync ever runs. The table was
            // readable, so this is a real answer and not Unknown.
            next = State::Idle;
        } else {
            next = lockHeldIn(content, inode) ? State::Running : State::Idle;
        }
    }

    if (next == m_state)
        return;

    m_state = next;
    emit stateChanged(m_state);
}
