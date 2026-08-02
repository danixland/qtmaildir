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

    /// Per-message opt-in, triggered by the user clicking "Load remote content".
    /// Never persisted, never carried to the next message.
    void setAllowRemote(bool allow) { m_allowRemote = allow; }
    bool allowRemote() const { return m_allowRemote; }

    /// True once any request has been denied, so the UI can offer the button.
    bool blockedAnything() const { return m_blockedAnything; }

    /// Called before rendering a new message: clears both the remote grant and
    /// the blocked flag.
    void resetForNewMessage();

private:
    QSet<QString> m_allowedCids;
    bool m_allowRemote = false;
    bool m_blockedAnything = false;
};
