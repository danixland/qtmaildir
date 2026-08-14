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
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
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

            QDesktopServices::openUrl(url);
            return false;
        }

        // Subframe loads are still subject to the interceptor; a main-frame
        // navigation would replace the pane, which no message may do.
        return !isMainFrame;
    }

private:
    QueryHandler m_onQuery;
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

    // Ctrl+wheel zoom. The filter goes on the application rather than on
    // m_view: the wheel event is delivered to an internal QQuickWidget the
    // view creates lazily, so there is no child to filter at this point and a
    // filter on m_view itself would never see it. eventFilter() narrows by
    // ancestry, so no event outside this pane is touched.
    qApp->installEventFilter(this);

    m_headerLabel = new QLabel(this);
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

    auto *blockedRow = new QHBoxLayout;
    blockedRow->addWidget(m_blockedLabel);
    blockedRow->addWidget(m_loadRemoteButton);
    blockedRow->addStretch();

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
    staleRow->setContentsMargins(0, 0, 0, 0);
    staleRow->addWidget(m_staleLabel);
    staleRow->addWidget(m_staleButton);
    staleRow->addStretch();
    m_staleBar->hide();

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

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(headerRow);
    layout->addLayout(blockedRow);
    layout->addWidget(m_staleBar);
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
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();
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
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();

    // The stale notice describes the message that WAS rendered, so it goes with
    // it, for the same reason as the blocked-content bar above. Left behind, it
    // sits over a blank pane naming a thread that is no longer shown, and its
    // button offers to recover a thread the user has navigated away from.
    setStaleThread(QString(), QString());

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
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();

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

    if (m_items.isEmpty()) {
        m_headerLabel->clear();
        m_detailsButton->hide();
        return;
    }

    m_detailsButton->show();

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

void MessageView::showBodyContextMenu(const QPoint &pos)
{
    // The page's own menu first: copy, select all and the rest stay exactly as
    // they were. This adds to that menu rather than replacing it.
    QMenu *menu = m_view->createStandardContextMenu();
    if (!menu)
        menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

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

void MessageView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);

    // Only when there is something to re-render: rendering an empty item list
    // would replace a deliberately blank pane with an empty document.
    if (event->type() == QEvent::PaletteChange && !m_items.isEmpty())
        render();
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
        m_blockedLabel->setVisible(blocked);
        m_loadRemoteButton->setVisible(blocked);
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
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();
    render();
}
