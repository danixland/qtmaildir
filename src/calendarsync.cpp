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

#include "calendarsync.h"

CalendarSync::CalendarSync(const QString &command, int delayMs, QObject *parent)
    : QObject(parent), m_command(command)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(delayMs);
    connect(&m_timer, &QTimer::timeout, this, &CalendarSync::run);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this,
            [this]() { m_output += m_process.readAll(); });
    connect(&m_process, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
        emit finished(status == QProcess::NormalExit && code == 0,
                      QString::fromLocal8Bit(m_output));
        if (m_again) {
            m_again = false;
            m_timer.start();
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit finished(false, m_process.errorString());
    });
}

void CalendarSync::schedule()
{
    if (m_command.isEmpty())
        return;
    if (isRunning())
        m_again = true;
    else
        m_timer.start();  // restarting is the debounce
}

bool CalendarSync::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

void CalendarSync::run()
{
    // splitCommand, not a shell: quoting works and nothing is interpreted.
    QStringList parts = QProcess::splitCommand(m_command);
    if (parts.isEmpty())
        return;
    const QString program = parts.takeFirst();
    m_output.clear();
    emit started();
    m_process.start(program, parts);
}
