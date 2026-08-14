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

#include <QList>
#include <QUrl>
#include <QWidget>

#include "htmlbuilder.h"
#include "marks.h"
#include "mimeparser.h"
#include "searchterm.h"

class QLabel;
class QMenu;
class QPushButton;
class QWebEngineView;
class QWebEngineProfile;
class CidSchemeHandler;
class TagColors;
class TagStrip;
class RequestInterceptor;

/// The message pane: thread header, body, attachment bar.
///
/// A whole thread renders into one web view. A newsletter thread can hold
/// dozens of messages, and one view per message would spawn one Chromium
/// render process per message.
class MessageView : public QWidget
{
    Q_OBJECT
public:
    explicit MessageView(QWidget *parent = nullptr);
    ~MessageView() override;

    /// The base URL every document in this pane is loaded with, and the only
    /// qtmaildir: URL the interceptor trusts. Defined once so setHtml() and
    /// setDocumentUrl() cannot drift apart: if they ever disagree, the
    /// interceptor fails closed and the pane renders nothing at all.
    static QUrl documentUrl() { return QUrl(QStringLiteral("qtmaildir://message")); }

    /// Renders a whole thread, oldest first. Items whose expanded flag is
    /// false collapse to a one-line stub.
    void showThread(const QList<ThreadRenderItem> &items);

    void showError(const QString &text, const QString &filePath);
    void clear();

    /// Shows the branded pane used when no thread is displayed.
    ///
    /// Separate from clear(), which still exists and still blanks: clear()
    /// drops the previous thread's state, and a caller that wants the
    /// placeholder asks for it afterwards. Keeping them apart is what stops
    /// the pane flashing a logo between selecting a thread and rendering it,
    /// which the item lists as a constraint.
    ///
    /// helpers are already-translated lines; an empty query makes one plain
    /// text rather than a link.
    void showPlaceholder(const QList<HtmlBuilder::PlaceholderHelper> &helpers);

    /// True while the placeholder is what the view is showing. Lets the window
    /// re-render it with fresh counts without guessing what is on screen.
    bool showingPlaceholder() const { return m_showingPlaceholder; }

    /// Supplies the tag strip's colours. Not owned; must outlive the view.
    void setTagColors(const TagColors *colours);

    /// Tags of the thread on display, shown as chips along the bottom.
    void setTags(const QStringList &tags);

    /// The full headers of every message in the thread, read-only. Also
    /// reachable from the button beside the header; public so the window's
    /// message_details action can call it.
    ///
    /// Plain text, not rich: header values are attacker-controlled and this
    /// dialog exists to show them verbatim, so the format that cannot
    /// interpret markup is the right one.
    void showDetailsDialog();

    /// The body zoom factor. Chromium's own range is roughly 0.25 to 5.0;
    /// these are tighter, since a pane at either extreme is unusable and the
    /// only visible way back is a menu entry the user cannot read.
    static constexpr qreal kMinZoom = 0.5;
    static constexpr qreal kMaxZoom = 3.0;
    static constexpr qreal kDefaultZoom = 1.0;

    /// Clamps to [kMinZoom, kMaxZoom]. A non-finite or non-positive value,
    /// which is what a corrupt state file yields, falls back to kDefaultZoom.
    static qreal clampZoom(qreal factor);

    qreal zoomFactor() const;
    void setZoomFactor(qreal factor);

    /// Shows or hides the notice saying the rendered thread no longer matches
    /// the current query.
    ///
    /// Modelled on the remote-content bar rather than on a dialog: the message
    /// stays readable underneath, and the way back is one click. Passing an
    /// empty id hides it.
    ///
    /// The pane does not decide this for itself. It renders whatever it was
    /// last given and has no idea what the thread list holds, so the window
    /// tells it after a refresh.
    /// `messageId` is the message on screen, empty when a whole thread is
    /// rendered. It rides along so recovery can restore the reader's place
    /// rather than reopening the thread at its first message.
    void setStaleThread(const QString &threadId, const QString &messageId);

    /// The thread the stale notice offers to bring back, empty when hidden.
    QString staleThreadId() const { return m_staleThreadId; }

    /// The message the stale notice would restore, empty when a whole thread
    /// is rendered or the notice is hidden.
    QString staleMessageId() const { return m_staleMessageId; }

    /// What the header can be searched for, given what it is currently showing.
    ///
    /// From, To and Cc appear only for a single-message thread, which is
    /// exactly when the header displays them: for a real thread the recipient
    /// differs message to message and the header says only the subject and the
    /// count. The menu must never offer a value the header is not stating.
    ///
    /// The values come from the same pass that renders the label, never from
    /// parsing it back: rich text does not survive a second parse.
    QList<SearchOffer> headerSearchOffers() const { return m_headerOffers; }

public slots:
    void toggleHtml();
    void loadRemoteContent();
    void zoomIn();
    void zoomOut();
    void zoomReset();

signals:
    void statusMessage(const QString &text);

    /// A helper line on the placeholder was clicked. The window runs the query;
    /// the view has no business driving the query bar itself.
    ///
    /// **Gated on the placeholder being what is displayed.** A message body is
    /// attacker-controlled HTML and can carry a qtmaildir-query: link as easily
    /// as any other; without the gate, clicking one would let a stranger's mail
    /// drive the thread list. The consequence is mild (a query runs, nothing is
    /// mutated or sent), but "a link in a message does something inside the
    /// app" is a boundary worth keeping shut rather than arguing about.
    void queryRequested(const QString &query);

    /// The user asked to see a thread that stopped matching the current query.
    ///
    /// Carries the thread id and the message that was on screen, because
    /// recovering the thread alone would land the user on its first message
    /// rather than the one they were reading. The window runs the query,
    /// expands the thread and restores the selection; the view knows none of
    /// that.
    void staleThreadRecoveryRequested(const QString &threadId,
                                      const QString &messageId);

    /// The user chose a search from one of the pane's context menus.
    ///
    /// `extend` narrows the current query rather than replacing it. The view
    /// does not know what the query bar holds and must not: the window owns
    /// that field and does the combining.
    ///
    /// Separate from queryRequested(), which carries a gate against a link in
    /// a rendered document driving the thread list. These menus are chrome
    /// built by our own code from values we extracted, so they need no gate,
    /// and widening the existing signal would change what that gate protects.
    void searchRequested(const QString &query, bool extend);

protected:
    /// Turns Ctrl+wheel over the body into zoom, and Ctrl+middle-click into a
    /// reset. Both events are delivered to the web view's internal QQuickWidget
    /// focus proxy, not to the view itself, so this filters the whole subtree
    /// rather than one widget.
    bool eventFilter(QObject *watched, QEvent *event) override;

    /// Re-renders when the desktop theme changes.
    ///
    /// The document's colours are baked into its stylesheet at build time, so
    /// unlike a widget it does not restyle itself: switching the desktop from
    /// light to dark would otherwise leave the open thread on the old palette
    /// until the next selection.
    void changeEvent(QEvent *event) override;

private:
    void render();
    void updateHeader();

    /// One header mark as an <img> data: URI, sized and coloured to the header
    /// label's own font and palette. Empty when the mark cannot be rendered.
    QString headerMark(Marks::Mark mark) const;
    void setDocument(const QString &html);

    /// Rebuilds the attachment bar from m_items. Called from render(), so a
    /// toggle between HTML and plain text keeps the bar in step with what is
    /// on screen.
    ///
    /// The bar holds ONE button however many attachments a thread carries. A
    /// button per file resized the splitter and crushed the thread list on a
    /// thread with fifteen of them.
    void rebuildAttachmentBar();

    /// The list of attachments, with a save button each and a "save all".
    void showAttachmentDialog();

    /// Saves one attachment, asking for the target directory. Writing goes
    /// through Attachment::saveTo(), which is where the path-traversal guard
    /// lives; the filename in a message is attacker-controlled.
    void saveAttachment(const Attachment &attachment);

    /// Saves every attachment into a new subdirectory of a directory the user
    /// picks, so fifteen files do not land loose among hundreds of others and
    /// cannot collide with what is already there.
    void saveAllAttachments();

    /// Every attachment in the thread, in the order the messages render.
    QList<Attachment> allAttachments() const;

    /// Builds and pops the header's menu at `pos`, in the label's coordinates.
    void showHeaderContextMenu(const QPoint &pos);

    /// Appends a "Search for ..." submenu per offer, each holding the replace
    /// and the narrow operation.
    ///
    /// Shared with the web view's menu in a later task so the two cannot grow
    /// different wording or a different pair of operations.
    void addSearchEntries(QMenu *menu, const QList<SearchOffer> &offers);

    QList<ThreadRenderItem> m_items;
    bool m_preferHtml = true;

    /// Gates queryRequested(), so a link in a message body cannot run a query.
    bool m_showingPlaceholder = false;

    QWebEngineProfile *m_profile = nullptr;
    QWebEngineView *m_view = nullptr;
    RequestInterceptor *m_interceptor = nullptr;
    CidSchemeHandler *m_cidHandler = nullptr;

    QLabel *m_headerLabel = nullptr;
    QLabel *m_blockedLabel = nullptr;
    QPushButton *m_loadRemoteButton = nullptr;

    /// The stale-thread notice and the thread it offers to restore.
    QWidget *m_staleBar = nullptr;
    QLabel *m_staleLabel = nullptr;
    QPushButton *m_staleButton = nullptr;
    QString m_staleThreadId;
    QString m_staleMessageId;
    QPushButton *m_detailsButton = nullptr;
    QWidget *m_attachmentBar = nullptr;
    TagStrip *m_tagStrip = nullptr;

    /// Populated by updateHeader(), consumed by the header's context menu.
    QList<SearchOffer> m_headerOffers;
};
