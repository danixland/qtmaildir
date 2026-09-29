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

#include "launchselectors.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>

namespace {

/// Bumped only if the payload's shape changes incompatibly. Both ends of the
/// socket are the same binary in the ordinary case, but an upgrade can leave an
/// old instance running while a new one is launched, and a version the reader
/// does not know is refused rather than misread.
constexpr quint16 kPayloadVersion = 1;

}  // namespace

LaunchSelectors LaunchSelectors::parse(const QStringList &arguments,
                                       QString *error)
{
    if (error)
        error->clear();

    QCommandLineParser parser;
    // No addHelpOption()/addVersionOption(): those are handled in main() before
    // QApplication exists, and Qt's own versions call exit() through
    // QCoreApplication, which is not constructed at that point.
    QCommandLineOption accountOption(
        QStringLiteral("account"),
        QCoreApplication::translate(
            "LaunchSelectors", "Open this account's view."),
        QStringLiteral("key"));
    QCommandLineOption threadOption(
        QStringLiteral("thread"),
        QCoreApplication::translate(
            "LaunchSelectors", "Open this thread."),
        QStringLiteral("id"));
    QCommandLineOption messageOption(
        QStringLiteral("message"),
        QCoreApplication::translate(
            "LaunchSelectors", "Open this message, inside its thread."),
        QStringLiteral("id"));

    parser.addOption(accountOption);
    parser.addOption(threadOption);
    parser.addOption(messageOption);

    // parse(), not process(): process() prints to stderr and calls exit() on an
    // error, which would take the window down over a typo. The error is
    // returned instead and main() decides.
    if (!parser.parse(arguments)) {
        if (error)
            *error = parser.errorText();
        return {};
    }

    // --version and --help are consumed in main() before this runs, so they
    // never reach the parser. An unknown option does, and is an error rather
    // than something to ignore: today's strcmp loop ignores everything it does
    // not recognise, so a typo silently produces an ordinary window.
    const QStringList unknown = parser.unknownOptionNames();
    if (!unknown.isEmpty()) {
        if (error) {
            *error = QCoreApplication::translate(
                         "LaunchSelectors", "Unknown option: %1")
                         .arg(unknown.join(QStringLiteral(", ")));
        }
        return {};
    }

    LaunchSelectors selectors;
    selectors.account = parser.value(accountOption);
    selectors.threadId = parser.value(threadOption);
    selectors.messageId = parser.value(messageOption);
    return selectors;
}

QString LaunchSelectors::helpText(const QString &versionDisplay)
{
    // The option NAMES are wire format and are never translated; the prose
    // beside them is. Kept as one block rather than assembled from pieces so a
    // translator sees the layout they are translating.
    return QCoreApplication::translate(
               "LaunchSelectors",
               "qtmaildir %1 - a Qt6 mail client for notmuch-indexed Maildirs\n"
               "\n"
               "Usage: qtmaildir [options]\n"
               "\n"
               "  -h, --help         Show this help and exit\n"
               "  -v, --version      Show the version and exit\n"
               "  --account <key>    Open this account's view\n"
               "  --thread <id>      Open this thread\n"
               "  --message <id>     Open this message, inside its thread\n"
               "\n"
               "The three selectors combine. When qtmaildir is already "
               "running,\n"
               "a second launch hands its selectors to that window and exits "
               "rather\n"
               "than opening a second one.\n"
               "\n"
               "Configuration: ~/.config/qtmaildir/qtmaildir.conf\n"
               "qtmaildir reads a notmuch-indexed Maildir. It does no network\n"
               "protocol work: fetching and sending are external commands.\n")
        .arg(versionDisplay);
}

QByteArray LaunchSelectors::toPayload() const
{
    // QDataStream rather than a line-based format: a Message-ID can contain
    // almost anything, a newline included, and a length-prefixed encoding does
    // not care. The round-trip test carries an embedded newline for exactly
    // this reason.
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kPayloadVersion << account << threadId << messageId;
    return payload;
}

LaunchSelectors LaunchSelectors::fromPayload(const QByteArray &payload,
                                             QString *error)
{
    if (error)
        error->clear();

    if (payload.size() > kMaxPayloadBytes) {
        if (error) {
            *error = QCoreApplication::translate(
                "LaunchSelectors", "Launch payload too large");
        }
        return {};
    }

    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_6_0);

    quint16 version = 0;
    stream >> version;
    if (stream.status() != QDataStream::Ok || version != kPayloadVersion) {
        if (error) {
            *error = QCoreApplication::translate(
                "LaunchSelectors", "Unrecognised launch payload");
        }
        return {};
    }

    LaunchSelectors selectors;
    stream >> selectors.account >> selectors.threadId >> selectors.messageId;

    // Checked AFTER every read, which is what catches a truncated payload: a
    // short read leaves the stream in ReadPastEnd and the fields
    // default-constructed, so without this a half-written message id would be
    // applied as an empty one.
    if (stream.status() != QDataStream::Ok) {
        if (error) {
            *error = QCoreApplication::translate(
                "LaunchSelectors", "Truncated launch payload");
        }
        return {};
    }

    return selectors;
}
