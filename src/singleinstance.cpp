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

#include "singleinstance.h"

#include <QDebug>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

namespace {

/// How long a client waits for the running instance, and how long the server
/// keeps a connection open waiting for its payload. Short: both processes are
/// on this machine and the peer is either there or it is not. A long wait
/// would stall a launch behind a wedged instance, which is worse than opening
/// a window.
constexpr int kTimeoutMs = 2000;

}  // namespace

SingleInstance::SingleInstance(const QString &socketPath, QObject *parent)
    : QObject(parent), m_socketPath(socketPath)
{
    // Registered here rather than at a call site so any connection carrying
    // this type works, including a queued one a future caller might add. A
    // Q_DECLARE_METATYPE alone gives the type a metatype but does not register
    // it under the name a queued invoke resolves, which is the trap AGENTS.md
    // records for Q_ENUM.
    qRegisterMetaType<LaunchSelectors>();
}

SingleInstance::~SingleInstance()
{
    if (m_server) {
        m_server->close();
        // Removes the filesystem entry, so an orderly exit leaves nothing for
        // the next launch to reclaim.
        QLocalServer::removeServer(m_socketPath);
    }
}

bool SingleInstance::isServer() const
{
    return m_server != nullptr && m_server->isListening();
}

bool SingleInstance::tryBecomeServer()
{
    if (isServer())
        return true;

    // CONNECT FIRST, and the order is the design. A successful connection means
    // a live instance owns this socket and this process is the messenger. A
    // refused connection on an EXISTING file means the file is stale, left by a
    // crash, and can be removed; doing it in this order is what stops a live
    // instance being removed out from under itself.
    {
        QLocalSocket probe;
        probe.connectToServer(m_socketPath);
        if (probe.waitForConnected(kTimeoutMs)) {
            probe.disconnectFromServer();
            return false;
        }
    }

    auto *server = new QLocalServer(this);
    // The socket is the user's own, in their own state directory. Nothing else
    // has any business connecting to it.
    //
    // With this option Qt binds in a private temporary directory and RENAMES
    // the socket onto the path, and a rename replaces whatever is there: a
    // stale file and a LIVE instance's socket alike (measured on Qt 6.11). So
    // listen() does not refuse a taken path, and the probe above is the only
    // thing standing between a second launch and the first one's socket.
    server->setSocketOptions(QLocalServer::UserAccessOption);

    if (!server->listen(m_socketPath)) {
        if (server->serverError() == QAbstractSocket::AddressInUseError
            && QFileInfo::exists(m_socketPath)) {
            // Nothing answered the probe above, so this file is stale. Not
            // reached on Qt 6.11, for the reason above; kept because a listen
            // that binds in place (no socket options, or a future Qt) fails
            // here with AddressInUse on a stale file.
            QLocalServer::removeServer(m_socketPath);
            server->listen(m_socketPath);
        }
    }

    if (!server->isListening()) {
        // A read-only state directory, a filesystem that has no unix sockets,
        // or a path whose parent does not exist. Report and carry on: the
        // window must still open. Losing single-instance behaviour is a
        // degradation; losing the mail client is not acceptable.
        qWarning() << "qtmaildir: cannot create the single-instance socket at"
                   << m_socketPath << ":" << server->errorString()
                   << "- continuing without it";
        delete server;
        return false;
    }

    m_server = server;
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            // Read ASYNCHRONOUSLY and parse on disconnect. The client's whole
            // life is one write followed by a disconnect, so the disconnect is
            // what says the payload is complete; a readAll() after the first
            // readyRead could see only part of it. And no waitForReadyRead():
            // this runs on the UI thread, and a peer that connects and writes
            // nothing (the probe in tryBecomeServer() is exactly that) must
            // not freeze the window.
            auto *buffer = new QByteArray;
            connect(socket, &QLocalSocket::readyRead, socket, [socket, buffer]() {
                buffer->append(socket->readAll());
                // A peer still writing past the cap is not a launch payload.
                if (buffer->size() > LaunchSelectors::kMaxPayloadBytes)
                    socket->abort();
            });
            connect(socket, &QLocalSocket::disconnected, this,
                    [this, socket, buffer]() {
                buffer->append(socket->readAll());
                const QByteArray payload = *buffer;
                delete buffer;
                socket->deleteLater();

                // Nothing written at all is the other launch's connect-first
                // probe, not a request. A real request is never empty, even
                // with no selectors, because the payload carries a version.
                if (payload.isEmpty())
                    return;

                QString error;
                const LaunchSelectors selectors =
                    LaunchSelectors::fromPayload(payload, &error);
                if (!error.isEmpty()) {
                    qWarning() << "qtmaildir: ignoring a launch payload:"
                               << error;
                    return;
                }

                // Emitted even when empty: a bare launch means "raise
                // yourself".
                emit selectorsReceived(selectors);
            });
            // A peer that connects and never hangs up is dropped rather than
            // held open for the life of the window.
            QTimer::singleShot(kTimeoutMs, socket, [socket]() {
                socket->abort();
            });
        }
    });

    return true;
}

bool SingleInstance::sendToRunningInstance(const LaunchSelectors &selectors)
{
    QLocalSocket socket;
    socket.connectToServer(m_socketPath);
    if (!socket.waitForConnected(kTimeoutMs))
        return false;

    socket.write(selectors.toPayload());
    // Flushed before returning, because the caller exits immediately
    // afterwards and an unflushed write would be lost with the process.
    if (!socket.waitForBytesWritten(kTimeoutMs))
        return false;

    // The disconnect is what tells the server the payload is complete.
    socket.disconnectFromServer();
    return true;
}
