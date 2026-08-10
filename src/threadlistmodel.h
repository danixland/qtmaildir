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

#include <QAbstractItemModel>
#include <QColor>
#include <QVector>

#include "tagcolors.h"
#include "types.h"

/// Tree model over query results, filled in batches so a large query paints
/// its first screenful immediately.
///
/// A tree rather than a table since item 20: a thread's replies are child rows
/// under it. The tree is at most two levels deep in the MODEL (a thread, then
/// its messages) even though the messages carry a reply depth of their own; the
/// visual nesting beyond the first level comes from that depth, not from
/// further parent-child structure. A deeper model would buy nothing and make
/// every index calculation recursive.
class ThreadListModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Role {
        /// The thread id behind a row. Views hand out QModelIndexes, but the
        /// worker speaks thread ids, so the mapping belongs on the model
        /// rather than in every caller.
        ThreadIdRole = Qt::UserRole + 1,

        /// The account tag on this thread without its "account-" prefix, for
        /// the chip drawn in front of the subject. Empty when the thread
        /// carries none.
        AccountLabelRole,

        /// Fill colour for that chip.
        AccountColourRole,

        /// Every tag on the thread, for the strip under the message pane.
        TagsRole,

        /// The tags worth drawing as pills under the subject: every tag except
        /// the ones the row already shows another way. Sorted, so a row does
        /// not reshuffle its own pills between repaints.
        PillTagsRole,

        /// The colours for PillTagsRole, in the same order. Supplied by the
        /// model because it owns the TagColors instance; a delegate reading
        /// config itself would be a second source of truth.
        PillColoursRole,

        /// True when the row is a MESSAGE row rather than a thread root.
        /// Drives both the action scope and whether the view paints a tag
        /// strip under the row.
        IsMessageRole,

        /// The message id behind a message row. Empty on a thread root.
        MessageIdRole,

        /// The message's reply depth, for the view's indentation. 1 for a
        /// direct reply, since depth 0 is the root row itself.
        MessageDepthRole,

        /// True when the row is a thread that has replies to show.
        ///
        /// Read by SubjectDelegate, which draws the expander itself: the
        /// delegate cannot call hasChildren without the model, and the same
        /// answer has to reach the cell that reserves room for the glyph.
        HasRepliesRole,

        /// The tags this MESSAGE carries that its thread does not.
        ///
        /// A reply card shows these and nothing else. Showing a reply's full
        /// tag set instead was measured against the user's own database and
        /// rejected: of 48691 messages, 7 carry `unread` and 75 carry
        /// `flagged`, and both are already drawn another way (the sender's
        /// weight, and the mark on line 2). Every other tag is applied to a
        /// whole thread and is identical on all its messages, so full sets
        /// would repeat the thread's own chips down the entire expansion,
        /// which is the striping the old row-wide strip existed to avoid.
        ///
        /// Empty on a thread row, which has no thread to differ from.
        MessageOwnTagsRole,

        /// The colours for MessageOwnTagsRole, in the same order. Supplied by
        /// the model for the same reason as PillColoursRole: it owns the
        /// TagColors instance, and a delegate reading config itself would be a
        /// second source of truth.
        MessageOwnColoursRole,

        /// The card's own fields, by role rather than by column.
        ///
        /// Five columns used to answer these through Qt::DisplayRole. One
        /// column cannot, and a card needs all five values at once, so each
        /// gets a role and Qt::DisplayRole answers the subject alone (which is
        /// what keyboard search and accessibility read).
        SubjectRole,
        SendersRole,
        DateRole,          ///< A QDateTime. The delegate formats it.
        HasAttachmentRole, ///< bool
        IsFlaggedRole,     ///< bool
        ReplyCountRole,    ///< int; 0 when a thread has no replies.
    };

    /// The mark drawn on a card's second line when the message has an
    /// attachment. A paperclip when the system font can draw it, "*" otherwise.
    static QString attachmentGlyph();

    /// The mark drawn on a card's second line when the message is flagged.
    /// A star when the system font can draw it, "*" otherwise.
    static QString flagGlyph();

    /// Row fill for a thread tagged `deleted`, and for one tagged `spam`.
    /// Muted rather than saturated: a bulk delete paints every selected row,
    /// and a wall of pure red is harder to read than the list it replaces.
    /// Exposed so a test names the same colour the model uses.
    static QColor deletedColour();
    static QColor spamColour();

    /// Background for a reply row, so an expanded thread reads as one block
    /// rather than as more table rows.
    ///
    /// Derived from the palette and deliberately subtle: it marks a grouping,
    /// and a tint strong enough to notice on its own would compete with the
    /// deleted and spam row colours, which carry real meaning.
    static QColor replyBackground();

    /// The line drawn down the left of an expanded thread's replies.
    static QColor threadLineColour();

    /// The dimmed text colour a READ thread carries.
    ///
    /// Unread rows are left at the palette's own colour and read ones recede,
    /// rather than unread being emphasised. Bold alone used to be the only
    /// cue, which leaves nothing to see when the desktop font is itself
    /// configured bold; colour is a second cue that survives that. Derived
    /// from the palette, never hardcoded.
    static QColor readColour();

    explicit ThreadListModel(QObject *parent = nullptr);

    /// Supplies the account chip colours. Not owned; must outlive the model.
    /// Without one, chips fall back to a colour generated from the tag name.
    void setTagColors(const TagColors *colours) { m_tagColors = colours; }

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;

    /// Whether a thread row should offer an expander.
    ///
    /// Answered from totalCount rather than from the loaded children, and that
    /// is what makes lazy loading possible at all: rowCount is 0 until the
    /// worker has walked the thread, so a view left to infer this from rowCount
    /// alone draws no expander, the user can never expand, and the replies are
    /// never asked for. The count is already in the summary, so this costs
    /// nothing.
    bool hasChildren(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void appendBatch(const QVector<ThreadSummary> &batch);
    void clear();

    ThreadSummary threadAt(int row) const;

    /// Fills in a thread's message rows once the worker has walked its tree.
    ///
    /// The depth-0 message is dropped: it is the thread's first message and the
    /// ROOT row already stands for it. Keeping it would show a thread of seven
    /// as one root and seven children, contradicting the reply count the row
    /// advertises. Calling again replaces the rows rather than appending, so a
    /// thread reloaded after a sync does not list its replies twice.
    void setThreadMessages(const QString &threadId,
                           const QVector<MessageNode> &nodes);

    /// True when the index is a message row rather than a thread root.
    bool isMessageRow(const QModelIndex &index) const;

    /// The message row's node, or a default-constructed one for any index that
    /// is not a message row.
    MessageNode messageAt(const QModelIndex &index) const;

    /// The thread a loaded message row belongs to, or empty when no expanded
    /// thread holds it. Only expanded threads have message rows at all, so a
    /// message the user could select is always findable here.
    QString threadIdForMessage(const QString &messageId) const;

    /// Resolves a selection into what an action should touch.
    ///
    /// Mixed selections are honoured as given: a thread root and an unrelated
    /// reply act on that whole thread and that one message. Nothing is
    /// escalated or narrowed silently, which is the point of the scope being
    /// visible in the first place.
    ActionScope scopeFor(const QModelIndexList &selection) const;

    /// The account keys behind a thread's account tags, for item 49's
    /// per-account sync.
    ///
    /// Returns every one of them, not the first: the thread list shows only one
    /// chip per row, but a thread whose messages landed in two mailboxes really
    /// does span two accounts, and tagging it touches files under both. Syncing
    /// only the one that happens to be shown would strand the other's edits.
    /// Empty when the thread is unknown or carries no account tag.
    QStringList accountKeysForThread(const QString &threadId) const;

    /// Applies a tag change locally so the UI updates before the worker
    /// confirms. To revert a failed write, call again with added and removed
    /// swapped.
    void applyTagChange(const QString &threadId, const QStringList &added,
                        const QStringList &removed);

private:
    /// One thread root and the message rows expanded under it.
    ///
    /// Children live beside the summary rather than in a separate map keyed by
    /// thread id, so a row and its expansion are appended, cleared and
    /// destroyed together. The model is rebuilt wholesale on every query, so
    /// nothing here has to survive a reset.
    struct ThreadNode
    {
        ThreadSummary summary;
        QVector<MessageNode> children;  ///< Empty until the thread is expanded.

        /// The thread's FIRST message, which the root card itself draws.
        ///
        /// Kept because the root card is that message: selecting it must
        /// render one message rather than the whole conversation, and without
        /// this the first message of every thread is unreachable, since the
        /// only rows offering a message are the replies and it is not one of
        /// them. Empty until the replies are loaded.
        MessageNode first;

        /// Distinguishes "this thread has no replies" from "its replies have
        /// not been asked for yet". Without it an expander would be drawn over
        /// every thread, including the ones that turn out to be single
        /// messages.
        bool loaded = false;
    };

    QVector<ThreadNode> m_threads;
    const TagColors *m_tagColors = nullptr;
};
