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

    // data: is never allowed here. It is permitted for the main-frame
    // document only, which is handled in interceptRequest() where the resource
    // type is known: a message body can put data: in <img src> or
    // <iframe src>, and those must stay blocked.

    // The document's origin is the qtmaildir: base URL, so requests can still
    // arrive on that scheme once the document is live.
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
    // The main-frame document arrives as a data: URL, because setHtml() does
    // not fetch the base URL it is given: it navigates to a data: URL carrying
    // the markup and applies the base URL afterwards as the document's origin.
    // (Verified empirically on Qt 6.11. The qtmaildir: rule in shouldAllow()
    // was written on the opposite assumption, and until this was found every
    // document load was blocked and the pane rendered blank.)
    //
    // Scoping this to ResourceTypeMainFrame is what keeps it from being a
    // hole: those bytes are the ones HtmlBuilder produced a moment earlier and
    // they arrive in the navigation itself rather than over any transport,
    // while a data: URL written into a message body reaches this function as
    // an image, stylesheet or subframe and is still denied by shouldAllow().
    if (info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMainFrame
        && info.requestUrl().scheme() == QLatin1String("data")) {
        return;
    }

    if (!shouldAllow(info.requestUrl()))
        info.block(true);
}

void RequestInterceptor::resetForNewMessage()
{
    m_allowRemote = false;
    m_blockedAnything = false;
}
