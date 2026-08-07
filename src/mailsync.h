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
#include <QProcess>
#include <QString>

/// Which half of the sync script is running.
///
/// The script runs mbsync and then `notmuch new`, so the phase is derived from
/// the output rather than announced: there is no side channel, and adding one
/// would mean the script and the app had to agree on a protocol.
enum class SyncPhase {
    Starting,   ///< Launched, nothing recognised yet.
    Mbsync,     ///< Fetching mail.
    Notmuch,    ///< Reindexing.
};

/// Derives a short status line from the sync script's output as it streams.
///
/// Kept separate from MailSync so it can be tested against captured output
/// without running a process, and free of any widget so the matching rules stay
/// one thing rather than being spread through a UI handler.
///
/// **Matching is deliberately loose.** mbsync's and notmuch's exact wording
/// varies by version, and a status line that goes blank because a string moved
/// is worse than the fixed "Syncing..." this replaces. Nothing here decides
/// whether the run succeeded: the exit status is the only authority on that, and
/// a second opinion derived from text would eventually disagree with it.
class SyncPhaseTracker
{
public:
    /// Feeds one line. Returns true when the status text changed as a result,
    /// so the caller can avoid rewriting the label for every line of noise.
    bool feed(const QString &line);

    /// Clears back to Starting for a new run.
    void reset();

    SyncPhase phase() const { return m_phase; }

    /// Plain text, already truncated, safe to put straight into a label.
    QString statusText() const { return m_status; }

private:
    SyncPhase m_phase = SyncPhase::Starting;
    QString m_status;
};

/// Runs the configured external sync command.
///
/// qtmaildir deliberately does not implement sync itself. The existing script
/// holds a flock that is the shared mutex between the user's cron sync, which
/// runs every 10 minutes, and any manual sync; running the script joins that
/// mutex, whereas a built-in implementation would sit outside it and could run
/// mbsync concurrently with cron, corrupting Maildir UID state.
class MailSync : public QObject
{
    Q_OBJECT
public:
    explicit MailSync(const QString &command, QObject *parent = nullptr);

    /// False when no command is configured; the UI disables its Sync button.
    bool isAvailable() const { return !m_command.isEmpty(); }
    bool isRunning() const;

    /// Returns false if unavailable or already running. A true return means the
    /// process was handed to the event loop, not that it launched successfully:
    /// a missing binary surfaces asynchronously through finished(false, ...).
    bool start();

    QString log() const { return m_log; }

signals:
    void started();
    void outputReceived(const QString &chunk);
    void finished(bool success, int exitCode);

private:
    void handleReadyRead();
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void handleError(QProcess::ProcessError error);

    QString m_command;
    QProcess m_process;
    QString m_log;
};
