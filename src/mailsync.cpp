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

#include "mailsync.h"

MailSync::MailSync(const QString &command, QObject *parent)
    : QObject(parent), m_command(command)
{
    // mbsync reports failures on stderr, so both channels go into one log:
    // splitting them would leave the pane empty for the runs worth reading.
    m_process.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_process, &QProcess::readyRead,
            this, &MailSync::handleReadyRead);
    connect(&m_process, &QProcess::finished,
            this, &MailSync::handleFinished);
    connect(&m_process, &QProcess::errorOccurred,
            this, &MailSync::handleError);
}

bool MailSync::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

bool MailSync::start()
{
    if (!isAvailable() || isRunning())
        return false;

    // splitCommand handles quoted arguments; running through a shell would make
    // a config value into an injection point.
    const QStringList parts = QProcess::splitCommand(m_command);
    if (parts.isEmpty())
        return false;

    m_log.clear();

    m_process.setProgram(parts.first());
    m_process.setArguments(parts.mid(1));

    // Deliberately no waitForStarted(): the spec requires the UI stay usable
    // during sync, and a failed launch arrives via errorOccurred() instead.
    m_process.start();

    emit started();
    return true;
}

void MailSync::handleReadyRead()
{
    const QByteArray data = m_process.readAll();
    if (data.isEmpty())
        return;

    const QString chunk = QString::fromUtf8(data);
    m_log += chunk;
    emit outputReceived(chunk);
}

void MailSync::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    // Drain anything buffered at exit.
    handleReadyRead();

    // No guard against a preceding launch failure is needed: verified that
    // QProcess emits errorOccurred(FailedToStart) *instead of* finished(),
    // not before it.
    const bool success = status == QProcess::NormalExit && exitCode == 0;
    emit finished(success, exitCode);
}

void MailSync::handleError(QProcess::ProcessError error)
{
    // Config validates the path at load time, but the script can be deleted or
    // its filesystem unmounted afterwards. Without this the spinner would stay
    // up forever with nothing explaining why.
    if (error != QProcess::FailedToStart)
        return;

    const QString message =
        QStringLiteral("Failed to start sync command: %1\n").arg(m_command);
    m_log += message;
    emit outputReceived(message);
    emit finished(false, -1);
}
