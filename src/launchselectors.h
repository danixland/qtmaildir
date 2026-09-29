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

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>

/// What a launch asked the window to show (item 200).
///
/// A value type with no Qt GUI dependency, deliberately: it is parsed before
/// QApplication exists, it crosses a socket, and both halves need tests that
/// no window has to be built for.
///
/// The three selectors COMPOSE rather than excluding each other. "Open this
/// message, in this account's view" is one request, and nothing about it is
/// contradictory.
struct LaunchSelectors
{
    /// An account key as written in the config, e.g. `work` from
    /// `[account.work]`. Validated against the configured accounts by the
    /// window, not here: this unit knows nothing about a Config.
    QString account;

    /// A notmuch thread id.
    QString threadId;

    /// A Message-ID, with or without the angle brackets.
    QString messageId;

    bool isEmpty() const
    {
        return account.isEmpty() && threadId.isEmpty() && messageId.isEmpty();
    }

    /// Longest payload accepted off the socket.
    ///
    /// A local socket hands over whatever the peer sends, and the peer here is
    /// another copy of this program running as the same user, so this is not a
    /// defence against an attacker. It is what stops a confused or truncated
    /// writer from being read as an unbounded selector. Three ids and an
    /// account key are a few hundred bytes; 64 KiB is room to spare.
    static constexpr int kMaxPayloadBytes = 64 * 1024;

    /// Parses a command line, `arguments[0]` being the program name.
    ///
    /// Takes a QStringList rather than argc/argv so it can be called before
    /// QCoreApplication exists, which is what lets --version keep answering on
    /// a machine where the GUI cannot open.
    ///
    /// On an unknown option, returns an empty result and sets \p error. The
    /// caller prints it; it is NOT fatal to the window, since a typo should
    /// not cost the user their mail client.
    static LaunchSelectors parse(const QStringList &arguments, QString *error);

    /// The help text, for `--help`. Translatable prose; the option NAMES are
    /// wire format and are never translated.
    static QString helpText(const QString &versionDisplay);

    /// Serialises for the socket. The inverse of fromPayload().
    QByteArray toPayload() const;

    /// Parses a socket payload. On anything malformed, oversized or truncated,
    /// returns an empty result and sets \p error.
    static LaunchSelectors fromPayload(const QByteArray &payload,
                                       QString *error);
};

/// Declared in the header that DEFINES the type, as types.h and threaddigest.h
/// do for theirs. A consumer declaring it instead would leave any other
/// consumer without it, and QSignalSpy needs it to carry the type.
Q_DECLARE_METATYPE(LaunchSelectors)
