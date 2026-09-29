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

#include "launchselectors.h"

class QLocalServer;

/// Makes a launch either the running instance or a messenger to it (item 200).
///
/// **This is not network protocol work.** A QLocalServer is a unix domain
/// socket between two copies of this program, owned by the user, in the user's
/// own state directory. The rule in AGENTS.md is about IMAP and SMTP.
///
/// Knows nothing about queries, accounts or mail: it carries a LaunchSelectors
/// from one process to another and emits what arrived.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    /// \p socketPath is a filesystem path, so a test can point it inside a
    /// QTemporaryDir and never touch the user's real one.
    explicit SingleInstance(const QString &socketPath,
                            QObject *parent = nullptr);
    ~SingleInstance() override;

    /// Tries to become the instance others talk to.
    ///
    /// Returns true when this process is now listening, false when another
    /// instance already is OR when no socket could be created at all. The
    /// caller treats both falses the same way for the second case: **a socket
    /// that cannot be created must not stop the window opening**, or a
    /// read-only state directory costs the user their mail client.
    ///
    /// Handles the stale socket file, which is the ordinary aftermath of a
    /// crash: it attempts a CONNECTION first, and a refused connection on an
    /// existing file proves nothing is serving it, so the file may be
    /// replaced. Connecting first is what stops a live instance being removed
    /// out from under itself, and it is the ONLY guard: listen() replaces a
    /// taken path rather than refusing it (see the .cpp).
    bool tryBecomeServer();

    /// True when tryBecomeServer() succeeded and this process is listening.
    bool isServer() const;

    /// Sends \p selectors to the running instance. Returns false when there is
    /// none, or when the write could not be completed.
    ///
    /// An EMPTY selector set is still sent: a bare `qtmaildir` against a
    /// running window means "raise yourself", which is a request and not a
    /// no-op.
    bool sendToRunningInstance(const LaunchSelectors &selectors);

signals:
    /// A later launch handed these over. Emitted on the server side only.
    void selectorsReceived(const LaunchSelectors &selectors);

private:
    QString m_socketPath;
    QLocalServer *m_server = nullptr;
};
