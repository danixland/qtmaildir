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

#include <QCoreApplication>
#include <QRegularExpression>

namespace {

/// Longest status text put into the label. Sync output is unstructured and
/// arrives from a script rather than from this code, so a line is truncated
/// rather than trusted to be a sensible length: an unbounded one would resize
/// the status bar and push the permanent widgets beside it off.
constexpr int kMaxStatusChars = 120;

/// Strips the script's "HH:MM:SS " prefix and anything that could disturb a
/// single-line label. Returns plain text, never markup.
QString sanitiseLine(const QString &line)
{
    QString text = line.trimmed();

    static const QRegularExpression timestamp(
        QStringLiteral("^\\d{2}:\\d{2}:\\d{2}\\s+"));
    text.remove(timestamp);

    // Collapse every control character, not just newlines: a stray \r would
    // otherwise leave the label showing the tail of the line only.
    static const QRegularExpression controls(QStringLiteral("[\\x00-\\x1f\\x7f]+"));
    text.replace(controls, QStringLiteral(" "));
    text = text.simplified();

    if (text.size() > kMaxStatusChars)
        text = text.left(kMaxStatusChars - 1) + QStringLiteral("…");

    return text;
}

} // namespace

void SyncPhaseTracker::reset()
{
    m_phase = SyncPhase::Starting;
    m_status.clear();
}

bool SyncPhaseTracker::feed(const QString &line)
{
    const QString text = sanitiseLine(line);
    if (text.isEmpty())
        return false;

    // The script's own banners. Skipped before anything else: RUN START would
    // otherwise read as mbsync output, and RUN END carries a status= field that
    // must not be parsed, since the exit code is the authority on the outcome.
    if (text.startsWith(QLatin1String("=====")))
        return false;

    const QString before = m_status;

    // notmuch new announces itself by what it reports, since it prints no
    // banner. Any of these means mbsync is done and the reindex is running.
    // Matched loosely and case-insensitively: the wording varies by version.
    static const QRegularExpression notmuchLine(
        QStringLiteral("^(processed \\d|added \\d|no new mail|found \\d)"),
        QRegularExpression::CaseInsensitiveOption);

    if (notmuchLine.match(text).hasMatch()) {
        m_phase = SyncPhase::Notmuch;
        m_status = QCoreApplication::translate("SyncPhaseTracker",
                                               "Reindexing (notmuch)...");
        return m_status != before;
    }

    // The channel mbsync is working on, which is the account name the user
    // wants to see. Only printed under -V, which is why the shipped script
    // passes it: without -V mbsync is silent until it exits.
    //
    // Taken from the output rather than from config, so what is shown is what
    // is actually happening, and in the order it actually happens.
    static const QRegularExpression channel(
        QStringLiteral("^Channel\\s+(\\S.*)$"),
        QRegularExpression::CaseInsensitiveOption);

    if (const auto match = channel.match(text); match.hasMatch()) {
        m_phase = SyncPhase::Mbsync;
        // The name comes from a config file this app does not own and lands in
        // a label, so it is truncated on its own before being interpolated:
        // bounding only the finished string would let a long name push the
        // wording out instead of itself.
        QString name = match.captured(1).trimmed();
        constexpr int kMaxNameChars = 60;
        if (name.size() > kMaxNameChars)
            name = name.left(kMaxNameChars - 1) + QStringLiteral("…");

        m_status = QCoreApplication::translate("SyncPhaseTracker",
                                               "Syncing %1...").arg(name);
        return m_status != before;
    }

    // mbsync's end-of-run summary, printed with or without -V. It arrives after
    // every channel is done, so it reports rather than progresses; the
    // "Far:/Near:" tail is dropped as unreadable at a glance.
    static const QRegularExpression summary(
        QStringLiteral("^Channels:\\s*(\\d+)\\s+Boxes:\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);

    if (const auto match = summary.match(text); match.hasMatch()) {
        m_phase = SyncPhase::Mbsync;
        m_status = QCoreApplication::translate(
                       "SyncPhaseTracker", "Syncing mail: %1 channels, %2 boxes")
                       .arg(match.captured(1), match.captured(2));
        if (m_status.size() > kMaxStatusChars)
            m_status = m_status.left(kMaxStatusChars - 1) + QStringLiteral("…");
        return m_status != before;
    }

    // Everything else while mbsync runs. The bulk of a real run is one
    // "Ignoring non-mail file" line per Maildir, so individual lines are never
    // shown: only the fact that mbsync is the phase.
    //
    // Only ever an upgrade from Starting, never a downgrade. Once the summary
    // has given real counts, a later noise line must not overwrite them with
    // the generic wording: the label would flicker back to saying less than it
    // already said, for every one of thousands of ignored files.
    if (m_phase == SyncPhase::Starting) {
        m_phase = SyncPhase::Mbsync;
        m_status = QCoreApplication::translate("SyncPhaseTracker",
                                               "Syncing mail (mbsync)...");
    }

    return m_status != before;
}

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

bool MailSync::start(const QStringList &channels)
{
    if (!isAvailable() || isRunning())
        return false;

    // splitCommand handles quoted arguments; running through a shell would make
    // a config value into an injection point.
    const QStringList parts = QProcess::splitCommand(m_command);
    if (parts.isEmpty())
        return false;

    m_log.clear();

    QStringList arguments = parts.mid(1);

    // Appended as separate list entries, never spliced into the command string:
    // these names come from config, the same trust boundary as the command
    // itself, and QProcess passes an argument list without a shell.
    for (const QString &channel : channels) {
        // An empty name would reach mbsync as a channel called "", failing the
        // whole run, so a stray blank costs the user nothing here.
        if (!channel.trimmed().isEmpty())
            arguments.append(channel);
    }

    m_process.setProgram(parts.first());
    m_process.setArguments(arguments);

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
