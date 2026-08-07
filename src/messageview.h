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
#include "mimeparser.h"

class QLabel;
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

public slots:
    void toggleHtml();
    void loadRemoteContent();
    void zoomIn();
    void zoomOut();
    void zoomReset();

signals:
    void statusMessage(const QString &text);

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


    QList<ThreadRenderItem> m_items;
    bool m_preferHtml = true;

    QWebEngineProfile *m_profile = nullptr;
    QWebEngineView *m_view = nullptr;
    RequestInterceptor *m_interceptor = nullptr;
    CidSchemeHandler *m_cidHandler = nullptr;

    QLabel *m_headerLabel = nullptr;
    QLabel *m_blockedLabel = nullptr;
    QPushButton *m_loadRemoteButton = nullptr;
    QPushButton *m_detailsButton = nullptr;
    QWidget *m_attachmentBar = nullptr;
    TagStrip *m_tagStrip = nullptr;
};
