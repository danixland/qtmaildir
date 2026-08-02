#include "requestinterceptor.h"

#include <QWebEngineUrlRequestInfo>

RequestInterceptor::RequestInterceptor(QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
}

bool RequestInterceptor::shouldAllow(const QUrl &url)
{
    // QUrl::scheme() always normalizes to lowercase (verified: QUrl("HTTP://x/y")
    // .scheme() == "http"), so a lowercase-literal compare cannot be bypassed
    // by unusual casing, in either the allow or the deny direction.
    const QString scheme = url.scheme();

    // The document itself is loaded via setHtml() with a qtmaildir: base URL,
    // so a request for exactly that URL must pass or nothing renders at all.
    // This is the ONLY trusted qtmaildir: URL: everything else on this scheme
    // is denied, including sub-paths of it. A hostile message body can put
    // arbitrary qtmaildir: URLs in <img src>, <link href>, etc., so this
    // cannot be a whole-scheme allow; it must be an exact match against the
    // one URL the application itself chose. If setDocumentUrl() was never
    // called, m_documentUrl is a default-constructed (invalid, empty) QUrl,
    // which cannot equal any real request URL, so this fails closed.
    if (scheme == QLatin1String("qtmaildir")) {
        if (!m_documentUrl.isEmpty() && url == m_documentUrl)
            return true;
        m_blockedAnything = true;
        return false;
    }

    // Inline parts of the current message only.
    if (scheme == QLatin1String("cid")) {
        // QUrl keeps a cid: body in path(), not host() or userName(), even
        // when it contains '@' (verified empirically: QUrl("cid:logo@example.org")
        // .path() == "logo@example.org", host() and userName() are empty).
        // path() also returns the percent-decoded form, so a percent-encoded
        // id (e.g. "%6Cogo@example.org") compares equal to its decoded form,
        // not to some other allowed id: it cannot be used to smuggle a
        // foreign id past the allowlist, only to spell an already-legitimate
        // id differently.
        const QString id = url.path();
        if (m_allowedCids.contains(id))
            return true;
        m_blockedAnything = true;
        return false;
    }

    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        if (m_allowRemote)
            return true;
        m_blockedAnything = true;
        return false;
    }

    // Everything else, including file:, javascript:, data:, blob:, about:,
    // chrome:, qrc:, filesystem:, protocol-relative URLs (empty scheme with a
    // host), and empty/malformed URLs (empty scheme), is denied
    // unconditionally. There is no flag that enables it.
    m_blockedAnything = true;
    return false;
}

void RequestInterceptor::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    if (!shouldAllow(info.requestUrl()))
        info.block(true);
}

void RequestInterceptor::resetForNewMessage()
{
    m_allowRemote = false;
    m_blockedAnything = false;
}
