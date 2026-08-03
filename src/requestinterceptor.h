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

#include <QSet>
#include <QUrl>
#include <QWebEngineUrlRequestInterceptor>

/// Deny-by-default request policy for the message view.
///
/// A message body is untrusted input from a stranger. Everything is blocked
/// unless explicitly permitted: remote loads leak the fact that a message was
/// read (tracking pixels) and file: loads would expose the local filesystem.
class RequestInterceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    explicit RequestInterceptor(QObject *parent = nullptr);

    /// The whole policy, as a pure function so it can be tested directly.
    bool shouldAllow(const QUrl &url);

    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

    /// Content-IDs belonging to the currently displayed message.
    void setAllowedCids(const QSet<QString> &cids) { m_allowedCids = cids; }

    /// The document's own base URL, i.e. the exact QUrl that MessageView passes
    /// as the base URL argument to setHtml(). This is the ONLY qtmaildir: URL
    /// that shouldAllow() will pass; every other URL on that scheme, including
    /// sub-paths of this one, is denied. Task 11's MessageView MUST call this
    /// with the same QUrl it hands to setHtml(), before rendering, or every
    /// qtmaildir: load (including the document itself) will be blocked.
    /// Defaults to empty, which denies all qtmaildir: URLs (fail closed).
    void setDocumentUrl(const QUrl &url) { m_documentUrl = url; }
    QUrl documentUrl() const { return m_documentUrl; }

    /// Per-message opt-in, triggered by the user clicking "Load remote content".
    /// Never persisted, never carried to the next message.
    void setAllowRemote(bool allow) { m_allowRemote = allow; }
    bool allowRemote() const { return m_allowRemote; }

    /// True once any request has been denied, so the UI can offer the button.
    bool blockedAnything() const { return m_blockedAnything; }

    /// Called before rendering a new message: clears both the remote grant and
    /// the blocked flag. Does NOT clear the document URL: the base URL is a
    /// property of the view (it is the same qtmaildir: origin the WebEngine
    /// page navigates within), not of any one message, so MessageView is
    /// expected to call setDocumentUrl() itself whenever that URL changes
    /// rather than have it silently reset here.
    void resetForNewMessage();

private:
    QSet<QString> m_allowedCids;
    QUrl m_documentUrl;
    bool m_allowRemote = false;
    bool m_blockedAnything = false;
};
