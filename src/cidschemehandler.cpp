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

#include "cidschemehandler.h"

#include <QBuffer>
#include <QWebEngineUrlRequestJob>

CidSchemeHandler::CidSchemeHandler(QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
{
}

void CidSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    // QUrl::path() returns the percent-DECODED body of a cid: URL (confirmed
    // in Task 5's RequestInterceptor tests, and re-verified here for the
    // namespaced "<prefix>!<id>" form specifically: '!' needs no percent
    // escaping per RFC 3986 sub-delims, and even when a Content-ID's own
    // characters are percent-encoded by the sender, decoding is idempotent
    // with how the prefix was concatenated in HtmlBuilder::namespaceCids, so
    // the string handed to path() here is exactly the map key that was
    // inserted for this part).
    const QString id = job->requestUrl().path();

    if (!m_parts.contains(id)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    const InlinePart part = m_parts.value(id);

    // The buffer is parented to the job so it lives exactly as long as needed.
    auto *buffer = new QBuffer(job);
    buffer->setData(part.data);
    buffer->open(QIODevice::ReadOnly);

    job->reply(part.mimeType.toUtf8(), buffer);
}
