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

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

/// Notices syncs this process did not start.
///
/// The user's cron runs mailsync.sh every ten minutes, so mail can appear and
/// tags can change while the window sits idle. The script holds an flock for
/// the whole run, which is already the signal: no status file is needed, and a
/// kernel lock cannot go stale because it dies with the process holding it.
///
/// **Read the lock, never take it.** Three ways to observe an flock look
/// plausible and two are wrong, both verified on Slackware, Linux 6.18:
///
///   - `flock -n` acquires in order to test. Polling every two seconds would
///     open a window every two seconds in which a starting mailsync.sh is
///     refused the lock and exits 75. It would cause the very skips the sync
///     script reports.
///   - `fcntl(F_OFD_GETLK)` never acquires, and looks ideal, but reports
///     UNLOCKED against a lock held by flock(2): the two are separate lock
///     namespaces in the kernel and cannot see each other. A silent false
///     negative, which is the worst failure available here.
///   - /proc/locks is a pure read. It observes flock(2) entries correctly and
///     cannot acquire, steal, or contend, so it can also never disturb the
///     Xapian write lock notmuch new holds during the same run.
///
/// Do not "simplify" this to flock -n.
class SyncMonitor : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Unknown,   ///< The lock table cannot be read; claim nothing.
        Idle,      ///< Readable, and nothing holds the lock.
        Running,   ///< Something holds the lock: a sync is in progress.
    };
    Q_ENUM(State)

    /// @param lockPath  the file mailsync.sh flocks, /tmp/mbsync.lock.
    /// @param locksPath the kernel lock table; injectable so tests can drive
    ///                  transitions without holding real locks.
    explicit SyncMonitor(const QString &lockPath,
                         const QString &locksPath = QStringLiteral("/proc/locks"),
                         QObject *parent = nullptr);

    State state() const { return m_state; }

    /// True only for State::Running. Unknown is deliberately not "running":
    /// callers use this to decide whether to wait, and waiting forever on a
    /// platform with no /proc/locks would be worse than not noticing a sync.
    bool isRunning() const { return m_state == State::Running; }

    void setInterval(int ms);
    void start();
    void stop();

    /// One observation. Public so tests can step it without a running timer.
    void poll();

    /// Whether @p content holds an flock(2) entry for @p inode.
    ///
    /// Static and content-based: this is the part worth testing, and it is
    /// testable only while it is separate from reading the file.
    static bool lockHeldIn(const QString &content, qint64 inode);

    /// The inode of @p path, or -1 when it does not exist.
    static qint64 inodeOf(const QString &path);

    /// The lock file assets/mailsync.sh takes, and the only one worth watching.
    ///
    /// Hardcoded to match LOCKFILE in that script. Two sources of truth is the
    /// standing hazard here: change one and the monitor silently reports Idle
    /// forever, since a missing lock file is a legitimate "no sync running".
    static QString defaultLockPath();

signals:
    /// Emitted only when the state actually changes, never once per poll: the
    /// status bar must not be repainted every two seconds forever.
    void stateChanged(SyncMonitor::State state);

private:
    QString m_lockPath;
    QString m_locksPath;
    State m_state = State::Unknown;
    QTimer m_timer;
};
