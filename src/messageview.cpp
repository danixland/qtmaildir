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

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QBuffer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStandardPaths>
#include <QtNumeric>
#include <QResizeEvent>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QStyle>
#include <QSizePolicy>
#include <QToolBar>
#include <QWheelEvent>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <algorithm>
#include <functional>
#include <utility>

#include "cidschemehandler.h"
#include "htmlbuilder.h"
#include "messagedetailsdialog.h"
#include "requestinterceptor.h"
#include "searchterm.h"
#include "tagstrip.h"
#include "threadcidmap.h"
#include "version.h"

namespace {

} // namespace

MessageView::LinkOpener &linkOpenerRef()
{
    static MessageView::LinkOpener opener;
    return opener;
}

void MessageView::setLinkOpener(LinkOpener opener)
{
    linkOpenerRef() = std::move(opener);
}

void MessageView::openExternally(const QUrl &url)
{
    if (const LinkOpener &opener = linkOpenerRef())
        opener(url);
    else
        QDesktopServices::openUrl(url);
}

namespace {

/// Intercepts link clicks so a message can never navigate the pane.
class MessagePage : public QWebEnginePage
{
public:
    using QueryHandler = std::function<bool(const QString &)>;

    MessagePage(QWebEngineProfile *profile, QObject *parent,
                QueryHandler onQuery)
        : QWebEnginePage(profile, parent), m_onQuery(std::move(onQuery)) {}

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
            // The placeholder's helper lines. JavaScript is off in this
            // profile, so a clickable count can only be a real link, and this
            // is where it is turned back into an action.
            //
            // The handler decides whether to accept it, not this function: the
            // view refuses unless the placeholder is what is actually
            // displayed, so a qtmaildir-query: link inside a message body is
            // dropped rather than handed a query to run.
            if (url.scheme() == QLatin1String("qtmaildir-query")) {
                // path() already percent-decodes; verified against Qt 6.11,
                // which returns tag:unread for qtmaildir-query:tag%3Aunread.
                // Decoding it a second time would corrupt a query carrying a
                // literal '%', which notmuch accepts in a quoted term.
                if (m_onQuery)
                    m_onQuery(url.path());
                return false;
            }

            MessageView::openExternally(url);
            return false;
        }

        // Subframe loads are still subject to the interceptor; a main-frame
        // navigation would replace the pane, which no message may do.
        return !isMainFrame;
    }

    /// Item 126. An anchor carrying target="_blank" never reaches
    /// acceptNavigationRequest: Chromium asks for a new window instead, and
    /// the base implementation returns nullptr, so the click is discarded with
    /// no error and nothing on screen. Marketing HTML sets _blank on
    /// practically every link, which is what made "HTML mail" look broken
    /// while a plain-text mail's links worked.
    ///
    /// The obvious override cannot work: createWindow() is handed a
    /// WebWindowType and NO url. The target arrives afterwards, as a
    /// navigation on whatever page is returned, so returning nullptr throws it
    /// away before it can be read.
    ///
    /// So return a page whose only job is to receive that navigation. It
    /// reuses the same handler the plain-link path uses rather than sourcing
    /// the URL a second way, which is what keeps the two kinds of link from
    /// drifting apart. No view is ever created and nothing is ever fetched:
    /// the page refuses the navigation, and deleteLater() disposes of it once
    /// the URL has been handed on.
    QWebEnginePage *createWindow(WebWindowType type) override
    {
        return makeRelay(type);
    }

public:
    /// The same call Chromium makes, reachable from a test. See
    /// MessageView::relayBlankTargetForTest().
    QWebEnginePage *createWindowForTest(WebWindowType type)
    {
        return makeRelay(type);
    }

    /// The same call Chromium makes for a clicked anchor. See
    /// MessageView::clickLinkForTest().
    bool clickLinkForTest(const QUrl &url)
    {
        return acceptNavigationRequest(url, NavigationTypeLinkClicked, true);
    }

protected:

private:
    QWebEnginePage *makeRelay(WebWindowType)
    {
        return new LinkRelayPage(profile(), this);
    }

    /// Receives the navigation createWindow() could not see, hands the URL to
    /// the external browser, and refuses. Never shown, never given a view.
    class LinkRelayPage : public QWebEnginePage
    {
    public:
        LinkRelayPage(QWebEngineProfile *profile, QObject *parent)
            : QWebEnginePage(profile, parent) {}

    protected:
        bool acceptNavigationRequest(const QUrl &url, NavigationType,
                                     bool) override
        {
            // Whatever the type, this page exists for exactly one URL and is
            // finished the moment it has it.
            if (url.isValid() && !url.scheme().isEmpty())
                MessageView::openExternally(url);
            deleteLater();
            return false;
        }
    };

    QueryHandler m_onQuery;
};

} // namespace

bool MessageView::clickLinkForTest(const QUrl &url)
{
    auto *page = static_cast<MessagePage *>(m_view->page());
    if (!page)
        return false;
    return page->clickLinkForTest(url);
}

bool MessageView::relayBlankTargetForTest(const QUrl &url)
{
    // static_cast, not qobject_cast: MessagePage carries no Q_OBJECT, and the
    // page is one this class constructed itself, so the type is not in doubt.
    auto *page = static_cast<MessagePage *>(m_view->page());
    if (!page)
        return false;
    // Chromium's own sequence: ask for the window, then navigate it. The type
    // is what a target="_blank" anchor produces.
    QWebEnginePage *relay = page->createWindowForTest(
        QWebEnginePage::WebBrowserTab);
    if (!relay)
        return false;
    relay->setUrl(url);
    return true;
}

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
    // The gate the queryRequested() documentation describes: a helper link is
    // only honoured while the placeholder is what is on screen, so the same
    // URL inside a message body reaches here and is dropped.
    m_view->setPage(new MessagePage(m_profile, m_view,
                                    [this](const QString &query) {
        if (!m_showingPlaceholder)
            return false;
        emit queryRequested(query);
        return true;
    }));

    QWebEngineSettings *settings = m_view->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);

    // Item 85: a selection in the body is searchable. CustomContextMenu so the
    // page's standard entries survive and the search is added to them.
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested,
            this, &MessageView::showBodyContextMenu);

    // Item 115. Chromium's copy entries all work and none of them says so, so
    // the pane reports for them. Connected to the page's own QActions, which
    // are the same instances the standard context menu holds, so this covers
    // the entry wherever it is triggered from and needs no menu of our own.
    //
    // Each message names WHAT was copied. "Copied" alone is worse than nothing
    // when three of these sit together in one menu.
    //
    // The status bar rather than a floating overlay, at the item's insistence:
    // this is where the application already reports transient results and
    // where they already expire (item 33). A second mechanism for one job is
    // what item 45 recorded when two Sync buttons disagreed.
    static const struct {
        QWebEnginePage::WebAction action;
        const char *message;
    } kCopyReports[] = {
        { QWebEnginePage::Copy, QT_TR_NOOP("Copied the selected text") },
        { QWebEnginePage::CopyLinkToClipboard, QT_TR_NOOP("Copied the link address") },
        { QWebEnginePage::CopyImageToClipboard, QT_TR_NOOP("Copied the image") },
        { QWebEnginePage::CopyImageUrlToClipboard, QT_TR_NOOP("Copied the image address") },
    };

    // The toast itself, a child of the pane rather than a layout item: it
    // floats OVER the message, so nothing reflows when it appears and the text
    // the user just copied does not jump under the cursor.
    m_copyToast = new QLabel(this);
    m_copyToast->setObjectName(QStringLiteral("copyToast"));
    // Plain text, deliberately. The strings are ours, but a label that guesses
    // under Qt::AutoText is one careless change away from rendering markup,
    // and this pane's whole job is displaying input from strangers.
    m_copyToast->setTextFormat(Qt::PlainText);
    m_copyToast->setAlignment(Qt::AlignCenter);
    // Opaque, or the message underneath shows through and the confirmation is
    // unreadable over exactly the content it is confirming.
    m_copyToast->setAutoFillBackground(true);
    applyToastPalette();
    m_copyToast->hide();

    m_copyToastTimer = new QTimer(this);
    m_copyToastTimer->setSingleShot(true);
    m_copyToastTimer->setInterval(kToastMs);
    connect(m_copyToastTimer, &QTimer::timeout,
            m_copyToast, &QWidget::hide);

    for (const auto &report : kCopyReports) {
        QAction *action = m_view->page()->action(report.action);
        if (!action)
            continue;
        const QString message = tr(report.message);
        connect(action, &QAction::triggered, this, [this, message]() {
            // In the pane, at the user's request, rather than in the status
            // bar item 115 first used: a copy happens here, and the status bar
            // is at the other end of the window, so the confirmation was
            // landing far from the gesture that caused it.
            showCopyToast(message);
        });
    }

    // Ctrl+wheel zoom. The filter goes on the application rather than on
    // m_view: the wheel event is delivered to an internal QQuickWidget the
    // view creates lazily, so there is no child to filter at this point and a
    // filter on m_view itself would never see it. eventFilter() narrows by
    // ancestry, so no event outside this pane is touched.
    qApp->installEventFilter(this);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setObjectName(QStringLiteral("messageHeader"));
    m_headerLabel->setTextFormat(Qt::RichText);
    m_headerLabel->setWordWrap(true);
    m_headerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Item 85: the header's values are searchable. CustomContextMenu rather
    // than an action list, since the entries depend on what is displayed.
    m_headerLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_headerLabel, &QWidget::customContextMenuRequested,
            this, &MessageView::showHeaderContextMenu);

    // To the right of the header, per the user's decision: the summary answers
    // "who is this from", this answers "what actually happened to it". A button
    // and not only a shortcut, since "everything needs a memorized key" is the
    // complaint this whole backlog started from.
    m_detailsButton = new QPushButton(tr("Details..."), this);
    m_detailsButton->setObjectName(QStringLiteral("messageDetails"));
    m_detailsButton->setToolTip(tr("Show the full headers of every message"));
    connect(m_detailsButton, &QPushButton::clicked,
            this, &MessageView::showDetailsDialog);
    m_detailsButton->hide();

    auto *headerRow = new QHBoxLayout;
    headerRow->addWidget(m_headerLabel, 1);
    // Top-aligned so it stays put as the header grows to four rows.
    headerRow->addWidget(m_detailsButton, 0, Qt::AlignTop);

    m_blockedLabel = new QLabel(tr("Remote content blocked"), this);
    m_loadRemoteButton = new QPushButton(tr("Load remote content"), this);
    connect(m_loadRemoteButton, &QPushButton::clicked,
            this, &MessageView::loadRemoteContent);

    // A WIDGET rather than a bare layout, because a layout has nothing to
    // paint a ground on and this bar now carries one.
    m_blockedBar = new QWidget(this);
    m_blockedBar->setObjectName(QStringLiteral("blockedContentBar"));
    auto *blockedRow = new QHBoxLayout(m_blockedBar);
    blockedRow->addWidget(m_blockedLabel);
    // The stretch BEFORE the button, so the thing to act on sits at the right
    // edge where the eye ends up after reading the sentence.
    blockedRow->addStretch();
    blockedRow->addWidget(m_loadRemoteButton);
    m_blockedBar->hide();

    // The stale-thread notice, deliberately the same shape as the row above:
    // a sentence and a button, above the message, leaving it readable. The
    // user asked for this rather than for a dialog, and a dialog would be
    // wrong anyway, since nothing here needs an answer before the message can
    // be read.
    m_staleBar = new QWidget(this);
    m_staleBar->setObjectName(QStringLiteral("staleThreadBar"));
    m_staleLabel = new QLabel(
        tr("This thread no longer matches the current query."), m_staleBar);
    m_staleButton = new QPushButton(tr("Show it anyway"), m_staleBar);
    m_staleButton->setObjectName(QStringLiteral("staleThreadButton"));
    connect(m_staleButton, &QPushButton::clicked, this, [this] {
        if (m_staleThreadId.isEmpty())
            return;

        // COPIES, not the members themselves, and this is load-bearing rather
        // than tidy. A direct connection passes these by reference all the way
        // into MainWindow::recoverStaleThread(), which calls runCurrentQuery(),
        // which blanks the pane, which calls setStaleThread() and assigns to
        // the very members those references name. The ids then read as empty
        // for the rest of the slot, so the recovery target was stored as an
        // empty string and nothing was ever recovered: the thread came back
        // collapsed with a blank pane, which is exactly the reported symptom.
        //
        // Invisible to a test that reaches the slot through invokeMethod,
        // because that copies the arguments; it needs the real signal.
        const QString threadId = m_staleThreadId;
        const QString messageId = m_staleMessageId;

        // The message on screen goes with the request. Recovering the thread
        // alone would reopen it at its first message, and the user was reading
        // message four of eight.
        emit staleThreadRecoveryRequested(threadId, messageId);
    });
    auto *staleRow = new QHBoxLayout(m_staleBar);
    staleRow->addWidget(m_staleLabel);
    staleRow->addStretch();
    staleRow->addWidget(m_staleButton);
    m_staleBar->hide();

    // Receive-only ribbon (item 123). Hidden until a message from an account
    // with no send_command is displayed.
    m_receiveOnlyRibbon = new QLabel(this);
    m_receiveOnlyRibbon->setObjectName(QStringLiteral("receiveOnlyRibbon"));
    // Qt::PlainText explicitly. The account key comes from configuration
    // rather than from a stranger, but a QLabel guesses under Qt::AutoText and
    // this is the same protection MessageDetailsDialog states on every value.
    m_receiveOnlyRibbon->setTextFormat(Qt::PlainText);
    m_receiveOnlyRibbon->setWordWrap(true);
    m_receiveOnlyRibbon->hide();

    m_attachmentBar = new QWidget(this);
    m_attachmentBar->setObjectName(QStringLiteral("attachmentBar"));
    new QHBoxLayout(m_attachmentBar);

    // Tags live under the message rather than in the thread list, where
    // spelling them out cost most of the list's width.
    m_tagStrip = new TagStrip(this);
    m_tagStrip->hide();

    // Item 85: a tag chip is searchable. The strip reports which chip was hit
    // and where; what a tag can do is decided here, beside the other menus, so
    // all three surfaces offer the same pair of operations.
    connect(m_tagStrip, &TagStrip::tagContextMenuRequested, this,
            [this](const QString &tag, const QPoint &globalPos) {
                const QString query = SearchTerm::tag(tag);
                if (query.isEmpty())
                    return;

                QMenu menu(this);
                addSearchEntries(&menu, { { tr("tag %1").arg(tag), query } });
                menu.exec(globalPos);
            });

    // The pane's own action bar (items 139 to 141). Empty until MainWindow
    // fills it: the actions belong to the window, and MessageView deliberately
    // knows nothing about the action map.
    m_messageBar = new QToolBar(this);
    m_messageBar->setObjectName(QStringLiteral("message_toolbar"));
    // The desktop's own button style, for the reason the main toolbar records:
    // a hardcoded setToolButtonStyle() overrides the user's "Icon only".
    m_messageBar->setToolButtonStyle(static_cast<Qt::ToolButtonStyle>(
        style()->styleHint(QStyle::SH_ToolButtonStyle, nullptr, m_messageBar)));
    m_messageBar->setMovable(false);
    m_messageBar->hide();
    // Set by MainWindow, which owns the configured size this is derived from.
    // Left at the style's own default until then, which is what a MessageView
    // built on its own in a test gets.

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(headerRow);
    layout->addWidget(m_blockedBar);
    layout->addWidget(m_receiveOnlyRibbon);
    layout->addWidget(m_staleBar);
    // Directly above the message it acts on, below the subject and details
    // rows: the bar belongs to the body, not to the pane's heading. The
    // transient notice bars stay above it, since they explain the message
    // rather than offer an action on it.
    layout->addWidget(m_messageBar);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_attachmentBar);
    layout->addWidget(m_tagStrip);

    applyNoticeBarStyles();

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

void MessageView::showPlaceholder(
    const QList<HtmlBuilder::PlaceholderHelper> &helpers)
{
    // Everything clear() drops, dropped again: this is reachable directly and
    // must not leave a previous thread's parts serveable behind the logo.
    m_items.clear();
    m_tagStrip->setTags({});
    m_cidHandler->setParts({});
    m_interceptor->setAllowedCids({});
    m_interceptor->resetForNewMessage();

    m_headerLabel->clear();
    m_detailsButton->hide();
    m_blockedBar->hide();
    rebuildAttachmentBar();

    // Set before the document loads, not after: acceptNavigationRequest reads
    // it, and a click cannot arrive before setDocument() returns, but ordering
    // it this way makes that independent of how the load is scheduled.
    m_showingPlaceholder = true;

    // The widget's own palette, not qApp's, for the reason the render path
    // uses it: a style sheet or a themed parent can give this pane different
    // colours from the application.
    setDocument(HtmlBuilder::buildPlaceholder(
        helpers, QStringLiteral(QTMAILDIR_VERSION),
        HtmlBuilder::brandPaletteFrom(palette())));
}

void MessageView::applyNoticeBarStyles()
{
    // QPalette::Base, the same surface HtmlBuilder reads, so a bar and the
    // message under it never disagree about which way round the theme is.
    const bool dark = palette().color(QPalette::Base).lightnessF() < 0.5;

    // Yellow for a warning, blue for an action, as the user asked. The dark
    // values are not the light ones dimmed: the same nominal tint behaves
    // differently against near-black, so each set carries its own ground,
    // border and text, and every ground states its text colour rather than
    // inheriting one that may be near-white on a pale tint.
    const QString warningGround = dark ? QStringLiteral("#3a2f0b")
                                       : QStringLiteral("#fdf6d8");
    const QString warningBorder = dark ? QStringLiteral("#6b5a15")
                                       : QStringLiteral("#e3d08a");
    const QString warningText   = dark ? QStringLiteral("#f0e2a8")
                                       : QStringLiteral("#4a3c05");

    const QString actionGround  = dark ? QStringLiteral("#0e2740")
                                       : QStringLiteral("#e3f0fb");
    const QString actionBorder  = dark ? QStringLiteral("#1d4a70")
                                       : QStringLiteral("#a8cbe8");
    const QString actionText    = dark ? QStringLiteral("#cfe4f7")
                                       : QStringLiteral("#0d3355");

    const QString sheet = QStringLiteral(
        "QWidget#%1 { background: %2; border: 1px solid %3; "
        "border-radius: 4px; } QWidget#%1 QLabel { color: %4; }");

    m_receiveOnlyRibbon->setStyleSheet(
        QStringLiteral("QLabel#receiveOnlyRibbon { background: %1; "
                       "border: 1px solid %2; border-radius: 4px; "
                       "color: %3; padding: 6px 8px; }")
            .arg(warningGround, warningBorder, warningText));

    for (QWidget *bar : { m_blockedBar, m_staleBar }) {
        bar->setStyleSheet(
            sheet.arg(bar->objectName(), actionGround, actionBorder,
                      actionText));
    }
}

void MessageView::setBarActions(const QList<QAction *> &messageActions,
                                const QList<QAction *> &viewControls,
                                int iconSize)
{
    m_messageBar->clear();
    if (iconSize > 0)
        m_messageBar->setIconSize(QSize(iconSize, iconSize));

    for (QAction *action : messageActions) {
        if (action)
            m_messageBar->addAction(action);
    }

    // The stretch is what separates the two scopes, so the view controls end
    // up at the right edge. A QToolBar has no addStretch(), so it takes an
    // expanding spacer widget.
    if (!viewControls.isEmpty()) {
        auto *spacer = new QWidget(m_messageBar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_messageBar->addWidget(spacer);

        for (QAction *action : viewControls) {
            if (action)
                m_messageBar->addAction(action);
        }
    }

    // Hidden over an empty pane whatever it holds, so it comes and goes with
    // the subject and the details button rather than hovering over the logo.
    // The user's call, and the reason is consistency with those two: a bar
    // that persists was the only piece of header furniture that did.
    //
    // This guard covers the HIDING only. MainWindow refills the bar from
    // showPlaceholderPane(), which every route that blanks the pane passes
    // through, so the refill re-reads m_items and this line answers. Nothing
    // refills it when a message ARRIVES, so updateHeader() shows it there;
    // see the note beside the details button, which it rides with.
    m_messageBar->setVisible(!m_messageBar->actions().isEmpty()
                             && !m_items.isEmpty());
}

void MessageView::clear()
{
    m_items.clear();
    m_showingPlaceholder = false;
    m_tagStrip->setTags({});

    // No thread is displayed, so nothing may be served or allowed. Without
    // this, the previous thread's parts would stay reachable.
    m_cidHandler->setParts({});
    m_interceptor->setAllowedCids({});
    m_interceptor->resetForNewMessage();

    setDocument(QString());
    m_headerLabel->clear();
    m_blockedBar->hide();

    // The stale notice describes the message that WAS rendered, so it goes with
    // it, for the same reason as the blocked-content bar above. Left behind, it
    // sits over a blank pane naming a thread that is no longer shown, and its
    // button offers to recover a thread the user has navigated away from.
    setStaleThread(QString(), QString());

    // The ribbon explains ONE message's account, so it goes with the message
    // for the same reason as the two bars above. Only setReceiveOnlyAccount()
    // hid it, which every SELECTION change reaches, so the ribbon survived
    // every other route to a blank pane: clear_pane, clear_selection, a new
    // query and a multi-row selection all blanked the message underneath it
    // and left it contradicting the Reply button beside it.
    m_receiveOnlyRibbon->hide();

    // clear() does not go through render(), so the bar has to be emptied
    // here or the previous thread's attachments stay offered.
    rebuildAttachmentBar();
}

void MessageView::showThread(const QList<ThreadRenderItem> &items)
{
    m_items = items;
    m_preferHtml = true;
    m_showingPlaceholder = false;

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
    m_showingPlaceholder = false;

    // An error card references nothing, so the policy is emptied rather than
    // left holding the previous thread's parts.
    m_cidHandler->setParts({});
    m_interceptor->setAllowedCids({});
    m_interceptor->resetForNewMessage();

    m_headerLabel->setText(tr("<b>Cannot display message</b>"));
    m_blockedBar->hide();

    const QString html = QStringLiteral(
        "<html><body><p>%1</p><p><code>%2</code></p></body></html>")
        .arg(text.toHtmlEscaped(), filePath.toHtmlEscaped());
    setDocument(html);
}

QString MessageView::headerMark(Marks::Mark mark) const
{
    // A data: URI rather than a resource path, for the same reason the marks
    // are compiled in rather than shipped in a .qrc, and one more besides: this
    // string goes into a QLabel's rich text, and Qt resolves a src= against the
    // resource system only when one is registered. The image travels with the
    // markup instead.
    //
    // Rendered at the label's OWN text colour so the mark tracks the palette
    // exactly as the subject beside it does, on a light or a dark theme.
    const int side = QFontMetrics(m_headerLabel->font()).ascent();
    const QPixmap pm = Marks::pixmap(mark, QSize(side, side),
                                     m_headerLabel->palette().color(
                                         QPalette::WindowText),
                                     m_headerLabel->devicePixelRatioF());
    if (pm.isNull())
        return {};

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!pm.save(&buffer, "PNG"))
        return {};

    // A hair of margin on both sides, so a mark does not touch the subject.
    return QStringLiteral(
               "<img src=\"data:image/png;base64,%1\" width=\"%2\" "
               "height=\"%3\" style=\"vertical-align: middle;\">&nbsp;")
        .arg(QString::fromLatin1(png.toBase64()))
        .arg(side)
        .arg(side);
}

void MessageView::updateHeader()
{
    // A stale offer list must not survive either exit: the early return below
    // leaves nothing on screen to search, and the normal path rebuilds it from
    // scratch a few lines down.
    m_headerOffers.clear();

    // The bar rides with the details button, but only the SHOWING half belongs
    // here. Hiding is covered by setBarActions(), since MainWindow refills the
    // bar on every route that blanks the pane, and a hide() in the empty
    // branch below was measured to change nothing. Nothing refills the bar
    // when a message ARRIVES, though, so without the show() below it stayed
    // hidden for the first message opened after any blanking and appeared on
    // the second, when m_items still held the first: one selection behind for
    // as long as the view lasted.
    if (m_items.isEmpty()) {
        m_headerLabel->clear();
        m_detailsButton->hide();
        return;
    }

    m_detailsButton->show();
    m_messageBar->setVisible(!m_messageBar->actions().isEmpty());

    // The thread's subject comes from its first message; later replies carry
    // Re: prefixes that add nothing.
    const QString subject = m_items.first().message.subject;

    // Collected by the pass that renders the label, from the same values, so
    // nothing has to parse the rendered markup back into structure.
    auto elided = [](const QString &value) {
        constexpr int kMaxLabel = 40;
        return value.size() > kMaxLabel
                   ? value.left(kMaxLabel) + QStringLiteral("...")
                   : value;
    };

    auto offer = [this](const QString &label, const QString &query) {
        if (query.isEmpty())
            return;
        m_headerOffers.append({ label, query });
    };

    offer(tr("subject \"%1\"").arg(elided(subject)),
          SearchTerm::field(QStringLiteral("subject"), subject));

    const QDateTime sent = MimeParser::parseDate(m_items.first().message.date);
    if (sent.isValid()) {
        offer(tr("mail from %1").arg(sent.date().toString(Qt::ISODate)),
              SearchTerm::onDate(sent.date()));
    }

    // Item 70's marks, beside the subject and OUTSIDE the message area. The
    // user asked for these two only: whether the thread is flagged and whether
    // it carries an attachment, which are the two states worth knowing before
    // reading. They belong to the header label, which is application chrome,
    // rather than to the generated document, which is untrusted content in a
    // sandboxed web view.
    //
    // Any message in the thread having the state marks the whole thread, since
    // the header describes the thread: an attachment on reply four is still an
    // attachment the reader wants to know about.
    const bool anyFlagged = std::any_of(
        m_items.cbegin(), m_items.cend(),
        [](const ThreadRenderItem &item) { return item.flagged; });
    const bool anyAttachment = std::any_of(
        m_items.cbegin(), m_items.cend(), [](const ThreadRenderItem &item) {
            return !item.message.attachments.isEmpty();
        });

    QString text;
    if (anyFlagged)
        text += headerMark(Marks::Mark::Flagged);
    text += QStringLiteral("<b>%1</b>").arg(subject.toHtmlEscaped());
    if (anyAttachment)
        text += headerMark(Marks::Mark::Attachment);

    // The header adapts to what it can say honestly. From, To and Cc are
    // per-message, and the pane shows a whole thread, so they are only
    // unambiguous when the thread holds exactly one message. For a real thread
    // the recipient differs message to message (once the user replies, one is
    // addressed to them and the next to the other party), and neither the union
    // nor the intersection is "the" recipient. Rather than pick one or compute
    // a participants list, the thread case says only the subject and the count,
    // and the per-message detail belongs to the dialog.
    if (m_items.size() == 1) {
        const ParsedMessage &message = m_items.first().message;

        // Every value here is attacker-controlled and the label is RichText, so
        // escaping is not cosmetic: an unescaped From injects markup into the
        // application's own chrome rather than into the sandboxed page.
        auto row = [&text](const QString &label, const QString &value) {
            if (value.isEmpty())
                return;   // An empty row reads as a rendering fault.
            text += QStringLiteral("<br><small>%1 %2</small>")
                        .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
        };

        row(tr("From:"), message.from);
        row(tr("To:"), message.to);
        row(tr("Cc:"), message.cc);

        // Only here, sharing the condition with the header's own display: for
        // a real thread these differ message to message, and the details
        // dialog is where they are unambiguous.
        offer(tr("sender %1").arg(elided(message.from)),
              SearchTerm::field(QStringLiteral("from"), message.from));
        offer(tr("recipient %1").arg(elided(message.to)),
              SearchTerm::field(QStringLiteral("to"), message.to));
        offer(tr("copied to %1").arg(elided(message.cc)),
              SearchTerm::field(QStringLiteral("cc"), message.cc));
    } else {
        text += QStringLiteral("<br><small>%1</small>")
                    .arg(tr("%n message(s) in thread", "", m_items.size()));
    }

    m_headerLabel->setText(text);
}

void MessageView::addSearchEntries(QMenu *menu, const QList<SearchOffer> &offers)
{
    for (const SearchOffer &entry : offers) {
        auto *sub = menu->addMenu(tr("Search for %1").arg(entry.label));

        auto *replace = sub->addAction(tr("Search for this"));
        connect(replace, &QAction::triggered, this, [this, entry]() {
            emit searchRequested(entry.query, SearchTerm::SearchMode::Replace);
        });

        auto *narrow = sub->addAction(tr("Add to search"));
        connect(narrow, &QAction::triggered, this, [this, entry]() {
            emit searchRequested(entry.query, SearchTerm::SearchMode::Narrow);
        });

        auto *exclude = sub->addAction(tr("Exclude from search"));
        // Visible but greyed rather than hidden, as in the details dialog:
        // there must be a query to exclude FROM.
        exclude->setEnabled(m_hasQuery);
        connect(exclude, &QAction::triggered, this, [this, entry]() {
            emit searchRequested(entry.query, SearchTerm::SearchMode::Exclude);
        });
    }
}

void MessageView::showHeaderContextMenu(const QPoint &pos)
{
    if (m_headerOffers.isEmpty())
        return;

    QMenu menu(this);
    addSearchEntries(&menu, m_headerOffers);
    menu.exec(m_headerLabel->mapToGlobal(pos));
}

SearchOffer MessageView::selectionSearchOffer(const QString &selectedText) const
{
    const QString query = SearchTerm::quote(selectedText);
    if (query.isEmpty())
        return {};

    constexpr int kMaxLabel = 40;
    const QString shown = selectedText.simplified();
    return { shown.size() > kMaxLabel
                 ? shown.left(kMaxLabel) + QStringLiteral("...")
                 : shown,
             query };
}

void MessageView::removeBrowserActions(QMenu *menu, QWebEnginePage *page)
{
    if (!menu || !page)
        return;

    // Item 100. Every one of these needs a history, a network or a file, and
    // this pane has none of the three.
    //
    // ViewSource is NOT in this list, and that is deliberate. It was removed
    // here first, on the reasoning that it was the same kind of thing; it is
    // not. The four below have nothing to act on, while view-source has a real
    // document and a real use. Chromium's own entry cannot work here either
    // (it navigates to view-source:<url>, which MessagePage refuses), so item
    // 113 implements it as our own plain-text dialog. Removing it in the
    // meantime would delete the gesture the user reaches for.
    static constexpr QWebEnginePage::WebAction kUnwanted[] = {
        QWebEnginePage::Back,
        QWebEnginePage::Forward,
        QWebEnginePage::Reload,
        QWebEnginePage::SavePage,
        // Item 127. Chromium adds these only when the menu is raised over a
        // LINK, so the four above, which are page actions, were the whole list
        // until now and this was tested by right-clicking the page.
        //
        // None of the three can be honoured. There are no tabs, and a window
        // means a second QWebEngineView, which the pane deliberately never
        // creates: one view per message is one Chromium render process per
        // message. "In this window" would navigate the pane away from the
        // message, which no message may do.
        //
        // They became MORE dangerous with item 126, not less: that fix gives
        // the page a real createWindow(), so an entry that used to be merely
        // dead would now do something, and what it would do is open a link the
        // user asked to open in a tab that does not exist.
        //
        // CopyLinkToClipboard is deliberately NOT here. It works, and it is
        // the fallback for any link that still will not open.
        QWebEnginePage::OpenLinkInNewTab,
        QWebEnginePage::OpenLinkInNewWindow,
        QWebEnginePage::OpenLinkInThisWindow,
        // Save link, and this one is a SECURITY decision rather than tidying.
        //
        // It is inert today, since no downloadRequested handler exists
        // anywhere, which is why it was first deferred to item 114 alongside
        // Save image. That was wrong: the two are not the same question.
        //
        // Save image is content the message already carries, and item 114 is
        // about making it work. Save link fetches a REMOTE URL chosen by the
        // sender, through this pane's profile, which is the one profile in the
        // application that must never fetch remote content: that is what
        // m_allowRemote and the whole interceptor exist to prevent. Answering
        // it with a download handler would put a network fetch of
        // attacker-controlled content behind a single context-menu entry, and
        // the request would carry whatever the profile holds.
        //
        // Saving what the user actually wants already has a path that does not
        // touch the network: saveAttachment(), which writes a MIME part
        // already parsed into memory and sanitises the filename. Do not
        // "restore" this entry by implementing downloadRequested for it.
        QWebEnginePage::DownloadLinkToDisk,
    };

    for (const QWebEnginePage::WebAction which : kUnwanted) {
        // pageAction() is the same QAction instance the standard menu holds,
        // so the pointer identifies it whatever language it is displayed in.
        if (QAction *action = page->action(which))
            menu->removeAction(action);
    }

    // Removing entries can leave a separator at an edge or two in a row, which
    // reads as a menu that lost something. Qt has no "tidy separators", so
    // this walks what is left.
    const QList<QAction *> remaining = menu->actions();
    bool previousWasSeparator = true;  // leading separators are unwanted too
    for (QAction *action : remaining) {
        if (!action->isSeparator()) {
            previousWasSeparator = false;
            continue;
        }
        if (previousWasSeparator)
            menu->removeAction(action);
        else
            previousWasSeparator = true;
    }
    if (!menu->actions().isEmpty() && menu->actions().constLast()->isSeparator())
        menu->removeAction(menu->actions().constLast());
}

void MessageView::addPaneActions(QMenu *menu, QWebEnginePage *page)
{
    if (!menu || !page)
        return;

    // Item 117. Added explicitly rather than relied upon: Chromium's standard
    // menu for this pane does not offer Select all and never did, measured by
    // hand with a selection active and against a build with
    // removeBrowserActions() reverted. The filter is not what removed it, so
    // relaxing the filter would not bring it back.
    //
    // The action itself already exists and already works; only the entry was
    // missing.
    if (QAction *selectAll = page->action(QWebEnginePage::SelectAll))
        menu->addAction(selectAll);
}

void MessageView::showBodyContextMenu(const QPoint &pos)
{
    // The page's own menu first: Copy and the rest stay exactly as they were.
    // This adds to that menu rather than replacing it.
    //
    // "and select all" used to be in that sentence and was wrong: Chromium's
    // menu here has never offered it. Item 117 measured that and addPaneActions()
    // supplies it below.
    QMenu *menu = m_view->createStandardContextMenu();
    if (!menu)
        menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // ...minus the browser's own navigation and page actions, which cannot
    // apply here. Item 100.
    removeBrowserActions(menu, m_view->page());

    // ...plus the ones it needs and Chromium does not supply. Item 117.
    // Before the search entries, so it sits with Copy rather than after a
    // separator at the bottom.
    addPaneActions(menu, m_view->page());

    // selectedText() reads the selection out of the render process with no
    // script injection. JavaScript is disabled in this profile and stays so.
    const SearchOffer offer = selectionSearchOffer(m_view->page()->selectedText());
    if (!offer.query.isEmpty()) {
        menu->addSeparator();
        addSearchEntries(menu, { offer });
    }

    // popup() rather than exec(): the menu owns itself via WA_DeleteOnClose and
    // must not block this handler.
    menu->popup(m_view->mapToGlobal(pos));
}

void MessageView::showDetailsDialog()
{
    if (m_items.isEmpty())
        return;

    MessageDetailsDialog dialog(m_items, m_hasQuery, this);

    // The dialog's searches are the pane's searches: one signal reaches the
    // window whichever surface the user used.
    //
    // It CLOSES on the way out, and that is not tidiness. The dialog is modal,
    // so without this the query runs and the thread list repaints behind a
    // window the user still has to dismiss, making the search look like it did
    // nothing.
    //
    // accept() BEFORE the emit, not after. The connection is direct, so the
    // emit runs the query synchronously: the model clears and this pane blanks
    // while the modal dialog is still up, holding the m_items it was built
    // from. Closing first leaves no window in which the dialog describes a
    // thread the pane has already dropped.
    connect(&dialog, &MessageDetailsDialog::searchRequested, this,
            [this, &dialog](const QString &query, SearchTerm::SearchMode mode) {
                dialog.accept();
                emit searchRequested(query, mode);
            });

    dialog.exec();
}

void MessageView::applyToastPalette()
{
    if (!m_copyToast)
        return;

    // From the PALETTE, never hardcoded. The pane already re-renders its
    // document on a PaletteChange so the message follows the desktop theme;
    // a toast painted in fixed colours would be the one part of the pane that
    // did not, and would be unreadable under whichever theme it was not
    // designed for.
    //
    // ToolTipBase/ToolTipText specifically: a toast IS a tooltip in everything
    // but how it is triggered, so this is the role the theme already styles
    // for "small transient thing floating over content".
    QPalette toastPalette = m_copyToast->palette();
    toastPalette.setColor(QPalette::Window,
                          palette().color(QPalette::ToolTipBase));
    toastPalette.setColor(QPalette::WindowText,
                          palette().color(QPalette::ToolTipText));
    m_copyToast->setPalette(toastPalette);
}

void MessageView::showCopyToast(const QString &text)
{
    if (!m_copyToast)
        return;

    // A checkmark, per the user's description. Prepended here rather than
    // baked into each string so the four messages stay translatable as plain
    // sentences and the mark cannot go missing from one of them.
    m_copyToast->setText(QStringLiteral("\u2713  ") + text);
    m_copyToast->adjustSize();
    positionToast();
    m_copyToast->show();
    m_copyToast->raise();

    // Restarted, not merely started: a second copy while the first toast is up
    // must get its own full reading time rather than inheriting what is left
    // of the previous countdown.
    m_copyToastTimer->start();
}

void MessageView::positionToast()
{
    if (!m_copyToast)
        return;

    // Anchored to the pane's bottom right, inset by a margin so it does not
    // touch the edges. Placed against the WIDGET rather than against m_view:
    // the web view's geometry shifts as the header grows and the attachment
    // bar appears, and the toast should sit in the same corner regardless.
    constexpr int margin = 12;
    const QSize size = m_copyToast->sizeHint();
    m_copyToast->setGeometry(width() - size.width() - margin,
                             height() - size.height() - margin,
                             size.width(), size.height());
}

void MessageView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // A hand-placed child does not follow its parent the way a laid-out one
    // does, so without this the toast stays where the pane used to end.
    positionToast();
}

void MessageView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);

    // Only when there is something to re-render: rendering an empty item list
    // would replace a deliberately blank pane with an empty document.
    if (event->type() == QEvent::PaletteChange) {
        // The toast follows the theme too, and unconditionally: unlike the
        // document it has no items to guard against, and a toast left in the
        // old theme's colours would be unreadable the first time it appeared.
        applyToastPalette();
        if (!m_items.isEmpty())
            render();
    }
}

void MessageView::render()
{
    const HtmlBuilder::Mode mode =
        m_preferHtml ? HtmlBuilder::PreferHtml : HtmlBuilder::ForcePlain;

    // This widget's palette, not the application's: a style sheet or a themed
    // parent can give the pane different colours from qApp, and the document
    // has to match the frame it sits in rather than the app default.
    setDocument(HtmlBuilder::buildThread(m_items, mode,
                                         HtmlBuilder::paletteFrom(palette())));
    rebuildAttachmentBar();

    // Blocking is discovered during load, so check shortly afterwards.
    QTimer::singleShot(300, this, [this]() {
        const bool blocked = m_interceptor->blockedAnything()
                             && !m_interceptor->allowRemote();
        // The BAR, not its children: the wrapper carries the ground, so
        // hiding only the label and button would leave a painted empty strip.
        m_blockedBar->setVisible(blocked);
    });
}

QList<Attachment> MessageView::allAttachments() const
{
    QList<Attachment> all;
    for (const ThreadRenderItem &item : m_items)
        all.append(item.message.attachments);
    return all;
}

void MessageView::rebuildAttachmentBar()
{
    auto *layout = qobject_cast<QHBoxLayout *>(m_attachmentBar->layout());

    // Rebuilt rather than updated: a thread can change under the same widget
    // (toggle_html re-renders, and the next thread reuses this bar), and a
    // stale button would offer a save from the message before it.
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const int total = allAttachments().size();
    if (total == 0) {
        m_attachmentBar->hide();
        return;
    }

    // One button whatever the count. A button per attachment made the bar as
    // wide as the window on a thread with fifteen of them, which pushed the
    // splitter over and left the thread list a few pixels wide.
    auto *button = new QPushButton(tr("Attachments (%1)...").arg(total),
                                   m_attachmentBar);
    button->setToolTip(tr("List the attachments in this thread"));
    connect(button, &QPushButton::clicked,
            this, &MessageView::showAttachmentDialog);

    layout->addWidget(button);
    layout->addStretch();
    m_attachmentBar->show();
}

void MessageView::showAttachmentDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Attachments"));

    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QTreeWidget(&dialog);
    list->setColumnCount(4);
    // The fourth column holds the per-row Save button and needs no label.
    list->setHeaderLabels({ tr("Message"), tr("File"), tr("Size"), QString() });
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::NoSelection);

    // A thread renders as one document, so the message number is what says
    // which of them a file came from.
    for (int index = 0; index < m_items.size(); ++index) {
        const ParsedMessage &message = m_items.at(index).message;
        for (const Attachment &attachment : message.attachments) {
            auto *row = new QTreeWidgetItem(list);
            row->setText(0, QString::number(index + 1));
            // safeFilename(), never the raw filename: the name in a message is
            // attacker-controlled and may carry separators or "..".
            row->setText(1, attachment.safeFilename());
            row->setText(2, QLocale().formattedDataSize(attachment.data.size()));

            auto *save = new QPushButton(tr("Save..."), list);
            // Copied into the lambda: m_items is replaced wholesale by the
            // next showThread(), so a reference would dangle.
            connect(save, &QPushButton::clicked, this,
                    [this, attachment]() { saveAttachment(attachment); });
            list->setItemWidget(row, 3, save);
        }
    }
    for (int column = 0; column < 3; ++column)
        list->resizeColumnToContents(column);

    layout->addWidget(list);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    // Only worth offering for more than one file: with a single attachment it
    // is the same action as its own Save button, one dialog deeper.
    if (allAttachments().size() > 1) {
        auto *saveAll = buttons->addButton(tr("Save all..."),
                                           QDialogButtonBox::ActionRole);
        connect(saveAll, &QPushButton::clicked, this,
                [this, &dialog]() {
            saveAllAttachments();
            dialog.accept();
        });
    }
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(560, 320);
    dialog.exec();
}

void MessageView::saveAllAttachments()
{
    const QList<Attachment> attachments = allAttachments();
    if (attachments.isEmpty())
        return;

    // The subfolder is stated up front rather than discovered afterwards: the
    // user picks a parent, and what lands in it is one directory, not fifteen
    // loose files among whatever is already there.
    const QString subject = m_items.isEmpty() ? QString()
                                              : m_items.first().message.subject;
    const QString rfc822Date = m_items.isEmpty() ? QString()
                                                 : m_items.first().message.date;
    const QString folder = attachmentFolderName(rfc822Date, subject);

    const QString parent = QFileDialog::getExistingDirectory(
        this,
        tr("Choose a folder. A subfolder \"%1\" will be created inside it.")
            .arg(folder),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (parent.isEmpty())
        return;  // cancelled

    // Never overwrite an existing directory: a second save of the same thread
    // gets its own folder rather than merging into the first.
    QDir parentDir(parent);
    QString unique = folder;
    for (int suffix = 2; parentDir.exists(unique); ++suffix)
        unique = tr("%1 (%2)").arg(folder).arg(suffix);

    if (!parentDir.mkpath(unique)) {
        emit statusMessage(tr("Could not create %1").arg(unique));
        return;
    }
    const QString target = parentDir.absoluteFilePath(unique);

    int saved = 0;
    QStringList failures;
    for (const Attachment &attachment : attachments) {
        QString error;
        // Not saveTo(): several messages in a thread commonly attach the same
        // filename, and overwriting silently lost six of sixteen files while
        // still reporting every one as saved.
        if (attachment.saveWithoutOverwriting(target, &error).isEmpty())
            failures.append(attachment.safeFilename());
        else
            ++saved;
    }

    if (failures.isEmpty()) {
        emit statusMessage(tr("Saved %1 attachment(s) to %2")
                               .arg(saved).arg(target));
    } else {
        emit statusMessage(tr("Saved %1 of %2 to %3; failed: %4")
                               .arg(saved).arg(attachments.size())
                               .arg(target, failures.join(QStringLiteral(", "))));
    }
}

void MessageView::saveAttachment(const Attachment &attachment)
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Save attachment to"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (directory.isEmpty())
        return;  // cancelled

    QString error;
    const QString written = attachment.saveTo(directory, &error);
    if (written.isEmpty()) {
        emit statusMessage(tr("Could not save attachment: %1").arg(error));
        return;
    }

    // Reported, not silent: a save with no feedback is the same failure as
    // acting on a thread and seeing nothing change.
    emit statusMessage(tr("Saved %1").arg(written));
}

void MessageView::setReceiveOnlyAccount(const QString &accountKey)
{
    if (accountKey.isEmpty()) {
        m_receiveOnlyRibbon->hide();
        return;
    }

    // Names the account AND the key to add. A ribbon saying only "you cannot
    // reply" leaves the user with nothing to do about it, and the shape is
    // expressed by omission, so there is no setting to go and look for.
    m_receiveOnlyRibbon->setText(
        tr("This account is receive-only. Add send_command to [account.%1] "
           "to send from it.")
            .arg(accountKey));
    m_receiveOnlyRibbon->show();
}

void MessageView::setStaleThread(const QString &threadId,
                                 const QString &messageId)
{
    m_staleThreadId = threadId;
    m_staleMessageId = messageId;
    m_staleBar->setVisible(!threadId.isEmpty());
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

bool MessageView::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::Wheel && type != QEvent::MouseButtonPress)
        return QWidget::eventFilter(watched, event);

    // Application-wide filter: only events inside this pane are ours. Anything
    // else, including a Ctrl+wheel over the thread list, passes untouched.
    // isAncestorOf() is false for the widget itself, so test that separately.
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || (widget != m_view && !m_view->isAncestorOf(widget)))
        return QWidget::eventFilter(watched, event);

    if (type == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (!(wheel->modifiers() & Qt::ControlModifier))
            return QWidget::eventFilter(watched, event);

        // angleDelta is in eighths of a degree; one detent is 120. A high
        // resolution wheel sends smaller steps, so scale rather than treating
        // every event as one full step.
        const int delta = wheel->angleDelta().y();
        if (delta != 0)
            setZoomFactor(zoomFactor() + 0.1 * delta / 120.0);

        // Consumed, or Chromium's own Ctrl+wheel zoom would run on top of
        // ours and the factor we track would no longer be what is on screen.
        return true;
    }

    // Ctrl+middle-click resets: the same hand that just zoomed with the wheel
    // puts it back, without reaching for the keyboard.
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::MiddleButton
        || !(mouse->modifiers() & Qt::ControlModifier)) {
        return QWidget::eventFilter(watched, event);
    }

    zoomReset();

    // Consumed: a plain middle click is paste-on-X11 in some contexts, and
    // this gesture must do one thing only.
    return true;
}

qreal MessageView::clampZoom(qreal factor)
{
    // qIsFinite rejects the NaN and infinity a corrupt or hand-edited state
    // file can produce; qFuzzyIsNull rejects the 0.0 that a missing or
    // non-numeric value converts to, which would render nothing at all.
    if (!qIsFinite(factor) || factor <= 0.0)
        return kDefaultZoom;
    return qBound(kMinZoom, factor, kMaxZoom);
}

qreal MessageView::zoomFactor() const
{
    // The web view is the single source of truth. It keeps the factor across
    // setHtml(), verified on Qt 6.11, so there is no second copy to drift.
    return m_view->zoomFactor();
}

void MessageView::setZoomFactor(qreal factor)
{
    m_view->setZoomFactor(clampZoom(factor));
}

void MessageView::zoomIn()
{
    setZoomFactor(zoomFactor() + 0.1);
}

void MessageView::zoomOut()
{
    setZoomFactor(zoomFactor() - 0.1);
}

void MessageView::zoomReset()
{
    setZoomFactor(kDefaultZoom);
}

void MessageView::loadRemoteContent()
{
    // Applies to this thread only and is cleared by the next showThread().
    m_interceptor->setAllowRemote(true);
    m_blockedBar->hide();
    render();
}
