#pragma once

#include <QHash>
#include <QWebEngineUrlSchemeHandler>

#include "mimeparser.h"

/// Serves cid: URLs from the currently displayed thread only.
///
/// Keys are the namespaced form "<prefix>!<content-id>" produced by
/// HtmlBuilder, so two messages in one thread that share a Content-ID do not
/// collide. The map is replaced wholesale on every thread change, so a thread
/// can never reference another thread's parts.
class CidSchemeHandler : public QWebEngineUrlSchemeHandler
{
    Q_OBJECT
public:
    explicit CidSchemeHandler(QObject *parent = nullptr);

    void setParts(const QHash<QString, InlinePart> &parts) { m_parts = parts; }

    /// Builds the namespaced key HtmlBuilder's rewritten URLs will request.
    ///
    /// The '!' separator disambiguates a hostile Content-ID from the prefix
    /// only because prefix is guaranteed '!'-free: the FIRST '!' in the
    /// result is always the separator, so an attacker-controlled contentId
    /// containing '!' (even several) cannot make one message's key collide
    /// with another's, it only extends the id half after that first '!'.
    /// This is asserted here rather than merely documented, since two call
    /// sites (this one and HtmlBuilder::namespaceCids) perform the same
    /// concatenation independently and neither should trust the other to
    /// have checked it. Q_ASSERT is compiled out in release builds; the
    /// property that matters there (distinct pairs never collide, and the
    /// key always splits at its first '!' back to the original prefix) is
    /// pinned by a test instead, since it holds unconditionally regardless
    /// of whether this assertion fires.
    static QString namespacedKey(const QString &prefix, const QString &contentId)
    {
        Q_ASSERT_X(!prefix.contains(QLatin1Char('!')), "CidSchemeHandler::namespacedKey",
                   "cidPrefix must never contain '!': it is the separator, and a "
                   "prefix containing one would make the split ambiguous");
        return prefix + QLatin1Char('!') + contentId;
    }

    void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
    QHash<QString, InlinePart> m_parts;
};
