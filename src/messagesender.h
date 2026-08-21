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

/// Runs an account's send_command with the message on stdin.
///
/// EXACTLY TWO OUTCOMES: sent, or not sent with a reason. Exit code 75 has no
/// special meaning here, unlike in the sync path. Item 125 is open precisely
/// because mailsync.sh treats 75 as neither success nor failure and hangs on
/// it; that exists because the script contends for a lock and there is no lock
/// here. Recorded so the two paths are not later "harmonised".
///
/// **The exit status is the only authority on whether a message was sent.**
/// This is the same rule assets/mailsync.sh exists to honour, and the same
/// class of bug is available here: a sender that reported success on anything
/// other than exit 0 would file a sent copy and close the composer for a
/// message that never left the machine. Nothing is derived from the command's
/// output, which belongs to whatever the user installed behind send_command.
///
/// **No shell, ever.** The command is a config value and is split into an
/// argument list with QProcess::splitCommand, then handed to QProcess, which
/// calls execve directly. A `;`, `&&`, `$(...)` or a backtick in the
/// configured string therefore arrives as a literal argument with nothing to
/// interpret it. Note that splitCommand understands DOUBLE quotes only:
/// `-a 'my acct'` splits into three arguments, so a path or an argument
/// containing a space must be written with double quotes. Measured, not
/// assumed.
///
/// **No message content ever reaches the argument list.** The bytes go on
/// stdin and only on stdin; the command reads its recipients from the
/// message's own headers, which is what `-t` means in the documented example.
/// A recipient address or a display name therefore cannot become an argument
/// however it is spelled.
///
/// This is the outbox seam. An outbox is built by calling this from a drain
/// loop; nothing in the composer would need to change.
///
/// Nothing here blocks the GUI thread DURING a send. send() hands the process
/// to the event loop and returns; there is no waitForStarted() and no
/// waitForFinished() on that path, so a command that hangs leaves the
/// interface responsive and the caller waiting on finished(). Timing a hung
/// command out is deliberately NOT this class's job: a timeout here would kill
/// a slow but working send. The one place this class does block is its
/// destructor, and that is the subject of the next paragraph.
///
/// **Destruction mid-send waits, briefly, and then kills.** A send is a live
/// SMTP conversation, so the outcome of abandoning one is genuinely unknown:
/// the message may be fully delivered, partially delivered, or not sent at
/// all. Measured with a one-second command destroyed 100ms in: plain
/// destruction returns in 100ms, kills the child, and the work does NOT
/// complete, announced by nothing but a `QProcess: Destroyed while process is
/// still running` warning on stderr. That is the mailsync.sh failure in a new
/// place, an unknown real outcome reported as a definite one, and it is
/// reachable by closing the composer with the window manager's X button while
/// a send is in flight.
///
/// So the destructor waits up to kShutdownWaitMs for the command to finish on
/// its own, which is the outcome that makes the report truthful: the same
/// measurement with a bounded wait completes the child and costs only the
/// ~1s the command actually needed. A command still running after that is
/// killed, because a destructor cannot block a quitting application forever.
///
/// **No finished() is emitted from the destructor, in either branch, and that
/// is deliberate rather than an omission.** After a kill the outcome is
/// unknown, and this class reports two outcomes only; inventing a third by
/// guessing would be the exact lie the rest of this header is built to avoid.
/// After a successful late finish the emit would reach handlers on a
/// half-destroyed caller. A caller that must know the result has to keep the
/// sender alive until finished() arrives, which is what refusing to close a
/// composer mid-send would express.
///
/// **There is no cancel(), and the caller does not have one either.** An
/// earlier revision of this comment deferred cancellation to "the caller's
/// popup", which overstated what exists: SendDialog offers an undo BEFORE the
/// send is committed and none after, by an explicit design decision that a
/// post-commit cancel is worse than either clean outcome. If a real cancel is
/// ever wanted it belongs HERE, killing the process and emitting one
/// finished(false, ...) through m_reported, which is the shape that flag
/// already has. It is not built now, and this header does not promise it.
class MessageSender : public QObject
{
    Q_OBJECT

public:
    explicit MessageSender(QObject *parent = nullptr);

    /// Waits briefly for an in-flight send, then kills it. See the class
    /// comment: this is the one blocking call in the class, and it emits
    /// nothing.
    ~MessageSender() override;

    /// How long the destructor gives an in-flight command to finish on its
    /// own before killing it. Long enough for a local MTA handing off to a
    /// queue, short enough not to hang a quitting application.
    static constexpr int kShutdownWaitMs = 5000;

    /// Starts \p command with \p bytes on stdin.
    ///
    /// Returns false without emitting anything when the command is empty or
    /// only whitespace, when it splits to nothing, or when a send is already
    /// running. A true return means the process was handed to the event loop,
    /// NOT that it launched: a missing or non-executable binary surfaces
    /// asynchronously through finished(false, ...), exactly as MailSync
    /// documents.
    bool send(const QString &command, const QByteArray &bytes);

    bool isRunning() const;

signals:
    /// \p error is empty on success and carries the command's stderr, or a
    /// description of why it could not start, on failure.
    ///
    /// EMITTED exactly once per accepted send, and the distinction between
    /// emitted and RECEIVED is the whole of this paragraph. QProcess can report
    /// both an error and a finish for one run (measured: a command that exits
    /// without draining a large stdin emits errorOccurred(WriteError) and then
    /// finished()), and m_reported collapses that to one emit.
    ///
    /// **m_reported guards the emit, not the receivers, and a caller can still
    /// see one result twice.** A MessageSender is normally a long-lived member
    /// reused for every send, so a caller that connects INSIDE its send path
    /// adds a permanent connection each time: send, fail, correct the
    /// recipient, send again, and the second result runs BOTH lambdas. The
    /// first still holds the first message's bytes, so it files a sent copy of
    /// the wrong message and acts on a dialog it already destroyed. That is
    /// precisely the harm this signal's contract exists to prevent, arriving
    /// by the one route no guard inside this class can cover.
    ///
    /// A caller connecting per-send must therefore pass
    /// `Qt::SingleShotConnection` (Qt 6.0+; this project is on 6.11), which
    /// disconnects the moment the lambda runs. Connecting ONCE in the caller's
    /// constructor and keeping the per-send state in members is the other
    /// correct shape. What is not correct, and what reads as permitted if this
    /// paragraph is skipped, is a bare connect() next to a send() call.
    void finished(bool sent, const QString &error);

private:
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void handleError(QProcess::ProcessError error);

    QProcess m_process;
    QString m_command;
    bool m_reported = false;
};
