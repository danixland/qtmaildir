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

#include "messagesender.h"

MessageSender::MessageSender(QObject *parent)
    : QObject(parent)
{
    // Separate channels, unlike MailSync's MergedChannels: there is no log
    // pane to fill here, and stderr alone is what a failure has to report.
    // Merging them would put the command's ordinary chatter into the error
    // message shown for a rejected send.
    m_process.setProcessChannelMode(QProcess::SeparateChannels);

    connect(&m_process, &QProcess::finished,
            this, &MessageSender::handleFinished);
    connect(&m_process, &QProcess::errorOccurred,
            this, &MessageSender::handleError);
}

MessageSender::~MessageSender()
{
    if (m_process.state() == QProcess::NotRunning)
        return;

    // A send is a live SMTP conversation and abandoning one has a genuinely
    // unknown outcome, so give the command a bounded chance to finish rather
    // than killing it outright. Measured: without this, a one-second command
    // destroyed 100ms in is killed and its work does not complete, announced
    // only by a Qt warning on stderr. With it, the same command completes and
    // the destructor costs the ~1s the command actually needed.
    //
    // The write channel is closed first because the command may still be
    // reading: a command blocked on stdin would otherwise never reach EOF and
    // would burn the whole timeout for no reason.
    m_process.closeWriteChannel();
    if (m_process.waitForFinished(kShutdownWaitMs))
        return;

    // Still running. A destructor cannot block a quitting application forever,
    // so the process is killed deliberately here rather than by ~QProcess.
    //
    // NOTHING IS EMITTED. The outcome after a kill is unknown: the message may
    // have been fully delivered, partially delivered, or not sent at all, and
    // this class reports two outcomes only. Emitting finished(false, ...) would
    // report "not sent" for a message that may well have been, which is the
    // mailsync.sh mistake pointing the other way. Emitting finished(true, ...)
    // would be worse. A caller that must know has to keep this object alive
    // until finished() arrives.
    //
    // Claiming the report BEFORE the kill is what makes that true, and it is
    // not optional: kill() makes QProcess deliver finished(CrashExit), which
    // reaches handleFinished and would emit exactly the untruthful "not sent"
    // this comment forbids. Measured, by a test that failed against the
    // version without these two lines. This is also the one place m_reported
    // does live work, rather than the defence-in-depth it is on the signal
    // paths.
    m_reported = true;
    m_process.kill();
    m_process.waitForFinished(kShutdownWaitMs);
}

bool MessageSender::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

bool MessageSender::send(const QString &command, const QByteArray &bytes)
{
    if (command.trimmed().isEmpty() || isRunning())
        return false;

    // splitCommand gives an argument list; running through a shell would make
    // every recipient address, display name and config value a potential
    // injection point. QProcess hands the list to execve, so a `;` or a
    // `$(...)` in the configured command is a literal argument with nothing to
    // interpret it. Note that splitCommand strips DOUBLE quotes only.
    //
    // Nothing from the message reaches the argument list at all: the command
    // reads its recipients from the message's own headers, which is what `-t`
    // means in the documented example.
    const QStringList parts = QProcess::splitCommand(command);
    if (parts.isEmpty())
        return false;

    m_command = command;
    m_reported = false;

    m_process.setProgram(parts.first());
    m_process.setArguments(parts.mid(1));

    // Deliberately no waitForStarted(): this runs on the GUI thread and the
    // interface must stay responsive while a send is in flight. A failed
    // launch arrives via errorOccurred(FailedToStart) instead, which QProcess
    // emits INSTEAD OF finished() rather than before it (measured).
    m_process.start();

    // Written after start() and before the process has necessarily launched,
    // which is safe: QProcess buffers and drains as the reader consumes.
    // Measured with a 320KB payload against a `cat` stub, which arrived
    // byte-identical, so a message with an attachment does not deadlock on the
    // 64KB pipe buffer.
    m_process.write(bytes);

    // The message goes on stdin and the channel is closed, so a command
    // reading to EOF terminates. Without closeWriteChannel() a command like
    // `cat` waits forever and the popup never leaves its Sending stage.
    m_process.closeWriteChannel();

    return true;
}

void MessageSender::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    // errorOccurred may already have reported this failure. Reporting twice
    // would close the popup and then act on a second result.
    //
    // This guard IS load-bearing, on exactly one path: the destructor sets
    // m_reported before kill(), because kill() makes QProcess deliver
    // finished(CrashExit) and without the flag this handler would emit a
    // "not sent" for a message whose fate is genuinely unknown. A test fails
    // against its removal.
    //
    // On the two signal paths it is defence in depth and currently cannot
    // fire: handleError is filtered to FailedToStart, and FailedToStart is
    // never followed by finished() (measured). An instrumented run of the
    // whole suite recorded zero hits there, including on the crash and
    // write-error paths that DO emit both signals. It stays because the day
    // someone widens handleError to report another error, the double report is
    // silent and costs a duplicate sent copy.
    if (m_reported)
        return;
    m_reported = true;

    // The exit status is the only authority. Nothing is inferred from what the
    // command printed: mailsync.sh records what a wrong answer here costs, and
    // a send reported as succeeding files a sent copy for a message that never
    // left the machine.
    const bool sent = status == QProcess::NormalExit && exitCode == 0;
    if (sent) {
        emit finished(true, QString());
        return;
    }

    // Exit 75 is deliberately NOT special. See the header.
    QString error = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    if (error.isEmpty()) {
        // A failure with a blank explanation gives the user nothing to act on,
        // so the status stands in for the reason the command did not give.
        error = status == QProcess::CrashExit
                    ? tr("The send command crashed.")
                    : tr("The send command exited with status %1 and said nothing.")
                          .arg(exitCode);
    }
    emit finished(false, error);
}

void MessageSender::handleError(QProcess::ProcessError error)
{
    // QProcess emits errorOccurred(FailedToStart) INSTEAD OF finished(), so
    // without this the caller waits forever. Measured on Qt 6.11 for both a
    // missing binary and a non-executable file: one errorOccurred, no
    // finished().
    //
    // Every other error IS followed by finished() and is left to it, which is
    // not merely tidiness. A command that exits without draining a large stdin
    // emits errorOccurred(WriteError) and then finished() with the command's
    // real exit code and its real stderr; reporting the write error here would
    // replace the server's own rejection message with a plumbing detail, and
    // reporting it as well as finished() would deliver two results for one
    // message.
    if (error != QProcess::FailedToStart)
        return;
    if (m_reported)
        return;
    m_reported = true;

    emit finished(false,
                  tr("The send command '%1' could not be started. Check that "
                     "the path is correct and the file is executable.")
                      .arg(m_command));
}
