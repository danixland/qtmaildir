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

#include "messageview.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <algorithm>

#include "cidschemehandler.h"
#include "htmlbuilder.h"
#include "requestinterceptor.h"
#include "tagstrip.h"
#include "threadcidmap.h"

namespace {

/// Intercepts link clicks so a message can never navigate the pane.
class MessagePage : public QWebEnginePage
{
public:
    MessagePage(QWebEngineProfile *profile, QObject *parent)
        : QWebEnginePage(profile, parent) {}

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type,
                                 bool isMainFrame) override
    {
        // setHtml() does NOT navigate to the base URL it is given: it
        // navigates to a data: URL carrying the markup, and applies the base
        // URL afterwards as the document's origin. Verified empirically on Qt
        // 6.11; an earlier version of this function compared against
        // documentUrl() here and rejected every document load, so nothing
        // rendered at all.
        //
        // A typed main-frame navigation is therefore one we initiated
        // ourselves, and is accepted on that basis. This is not the security
        // boundary: RequestInterceptor still vets every request the document
        // goes on to make, including the qtmaildir: origin itself.
        if (type == NavigationTypeTyped && isMainFrame)
            return true;

        if (type == NavigationTypeLinkClicked) {
            QDesktopServices::openUrl(url);
            return false;
        }

        // Subframe loads are still subject to the interceptor; a main-frame
        // navigation would replace the pane, which no message may do.
        return !isMainFrame;
    }
};

} // namespace

MessageView::MessageView(QWidget *parent)
    : QWidget(parent)
{
    // Off-the-record profile: no cookies, no cache, nothing persisted.
    m_profile = new QWebEngineProfile(this);
    m_profile->setHttpCacheType(QWebEngineProfile::NoCache);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

    m_interceptor = new RequestInterceptor(this);
    m_profile->setUrlRequestInterceptor(m_interceptor);

    m_cidHandler = new CidSchemeHandler(this);
    m_profile->installUrlSchemeHandler(QByteArrayLiteral("cid"), m_cidHandler);

    m_view = new QWebEngineView(this);
    m_view->setPage(new MessagePage(m_profile, m_view));

    QWebEngineSettings *settings = m_view->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setTextFormat(Qt::RichText);
    m_headerLabel->setWordWrap(true);
    m_headerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_blockedLabel = new QLabel(tr("Remote content blocked"), this);
    m_loadRemoteButton = new QPushButton(tr("Load remote content"), this);
    connect(m_loadRemoteButton, &QPushButton::clicked,
            this, &MessageView::loadRemoteContent);

    auto *blockedRow = new QHBoxLayout;
    blockedRow->addWidget(m_blockedLabel);
    blockedRow->addWidget(m_loadRemoteButton);
    blockedRow->addStretch();

    m_attachmentBar = new QWidget(this);
    new QHBoxLayout(m_attachmentBar);

    // Tags live under the message rather than in the thread list, where
    // spelling them out cost most of the list's width.
    m_tagStrip = new TagStrip(this);
    m_tagStrip->hide();

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_headerLabel);
    layout->addLayout(blockedRow);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_attachmentBar);
    layout->addWidget(m_tagStrip);

    clear();
}

MessageView::~MessageView() = default;

void MessageView::setTagColors(const TagColors *colours)
{
    m_tagStrip->setTagColors(colours);
}

void MessageView::setTags(const QStringList &tags)
{
    m_tagStrip->setTags(tags);
}

/// The single place that loads a document into the view.
///
/// RequestInterceptor trusts exactly one qtmaildir: URL and denies every other
/// URL on that scheme, so the base URL given to setHtml() and the one given to
/// setDocumentUrl() must be identical. Routing every load through here is what
/// makes that true by construction rather than by remembering to pair two calls
/// at each site.
void MessageView::setDocument(const QString &html)
{
    m_interceptor->setDocumentUrl(documentUrl());
    m_view->setHtml(html, documentUrl());
}

void MessageView::clear()
{
    m_items.clear();
    m_tagStrip->setTags({});

    // No thread is displayed, so nothing may be served or allowed. Without
    // this, the previous thread's parts would stay reachable.
    m_cidHandler->setParts({});
    m_interceptor->setAllowedCids({});
    m_interceptor->resetForNewMessage();

    setDocument(QString());
    m_headerLabel->clear();
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();
}

void MessageView::showThread(const QList<ThreadRenderItem> &items)
{
    m_items = items;
    m_preferHtml = true;

    // Every thread starts from a clean policy: no remote grant carries over.
    m_interceptor->resetForNewMessage();

    // Resetting the policy is not enough on its own. Anything fetched under a
    // previous grant stays in the engine's caches, and a cached resource is
    // painted without the interceptor being consulted at all, so returning to
    // a thread would show its remote images again with the grant switched off.
    // The policy would be right and the pane would still be lying.
    //
    // clearHttpCache() empties the profile's store, but the render process
    // keeps its own decoded-image cache keyed on the document, and that one
    // outlives a setHtml() of the same URL. Loading about:blank first discards
    // the previous document entirely, which is what actually drops those
    // images. Verified against a local server: the image is fetched once under
    // the grant and never re-fetched afterwards, so anything still visible on
    // return could only have come from that cache.
    //
    // This belongs here rather than in render(): render() also runs for the
    // remote-content grant itself, where throwing the document away would
    // discard exactly what the user just asked to see.
    m_profile->clearHttpCache();
    m_view->setUrl(QUrl(QStringLiteral("about:blank")));

    // Two messages in one thread commonly share a Content-ID, and the thread is
    // one document, so the parts are namespaced per message.
    const ThreadCidMap cidMap = buildThreadCidMap(m_items);
    m_interceptor->setAllowedCids(cidMap.allowedCids);
    m_cidHandler->setParts(cidMap.parts);

    updateHeader();
    render();
}

void MessageView::showError(const QString &text, const QString &filePath)
{
    m_items.clear();

    // An error card references nothing, so the policy is emptied rather than
    // left holding the previous thread's parts.
    m_cidHandler->setParts({});
    m_interceptor->setAllowedCids({});
    m_interceptor->resetForNewMessage();

    m_headerLabel->setText(tr("<b>Cannot display message</b>"));
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();

    const QString html = QStringLiteral(
        "<html><body><p>%1</p><p><code>%2</code></p></body></html>")
        .arg(text.toHtmlEscaped(), filePath.toHtmlEscaped());
    setDocument(html);
}

void MessageView::updateHeader()
{
    if (m_items.isEmpty()) {
        m_headerLabel->clear();
        return;
    }

    // The thread's subject comes from its first message; later replies carry
    // Re: prefixes that add nothing.
    const QString subject = m_items.first().message.subject;

    m_headerLabel->setText(
        QStringLiteral("<b>%1</b><br><small>%2</small>")
            .arg(subject.toHtmlEscaped(),
                 tr("%n message(s) in thread", "", m_items.size())));
}

void MessageView::render()
{
    const HtmlBuilder::Mode mode =
        m_preferHtml ? HtmlBuilder::PreferHtml : HtmlBuilder::ForcePlain;

    setDocument(HtmlBuilder::buildThread(m_items, mode));

    // Blocking is discovered during load, so check shortly afterwards.
    QTimer::singleShot(300, this, [this]() {
        const bool blocked = m_interceptor->blockedAnything()
                             && !m_interceptor->allowRemote();
        m_blockedLabel->setVisible(blocked);
        m_loadRemoteButton->setVisible(blocked);
    });
}

void MessageView::toggleHtml()
{
    const bool anyHtml = std::any_of(
        m_items.cbegin(), m_items.cend(),
        [](const ThreadRenderItem &item) { return item.message.hasHtml(); });

    if (!anyHtml) {
        emit statusMessage(tr("No message in this thread has an HTML part"));
        return;
    }
    m_preferHtml = !m_preferHtml;
    render();
}

void MessageView::loadRemoteContent()
{
    // Applies to this thread only and is cleared by the next showThread().
    m_interceptor->setAllowRemote(true);
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();
    render();
}
