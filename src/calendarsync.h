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
#include <QTimer>

/// Runs calendar_sync_command a short delay after the last write.
///
/// The calendar's counterpart to MailSync, and deliberately smaller: no lock
/// monitoring and no status file, because the command itself takes the
/// flock that cron shares. A write arriving while a run is in progress is not
/// dropped: it queues exactly one more run for when this one finishes, since
/// the running sync may have listed the vdir before the write landed.
class CalendarSync : public QObject
{
    Q_OBJECT

public:
    CalendarSync(const QString &command, int delayMs, QObject *parent = nullptr);

    void schedule();
    bool isRunning() const;

signals:
    void started();
    void finished(bool ok, const QString &output);

private:
    void run();

    QString m_command;
    QTimer m_timer;
    QProcess m_process;
    QByteArray m_output;
    bool m_again = false;
};
