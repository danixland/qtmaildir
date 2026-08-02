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
    static QString namespacedKey(const QString &prefix, const QString &contentId)
    { return prefix + QLatin1Char('!') + contentId; }

    void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
    QHash<QString, InlinePart> m_parts;
};
