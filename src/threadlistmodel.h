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

        /// How many of PillTagsRole's entries belong to the message the card
        /// DISPLAYS, the rest belonging only to its siblings.
        ///
        /// The card stands for one message but sits above a conversation, so
        /// it shows both: the message's own tags first at full size, then the
        /// thread's other tags smaller and muted. Without the split a card
        /// either claimed a sibling's tag as its own (item 110) or dropped it
        /// and looked like it had lost information.
        ///
        /// Equals the whole list until the row has been opened, since the
        /// per-message tags arrive with the message load and before that the
        /// union is the only answer there is. Chips therefore SHRINK when the
        /// split becomes known; none ever disappears.
        PillOwnCountRole,

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

        /// bool; the message was forwarded, from the Maildir "P" flag.
        /// Item 69 draws this as a mark where it used to read as the word
        /// "passed" in the tag strip.
        IsPassedRole,
        /// True when the SUBJECT reads as a forward someone sent the user.
        ///
        /// Item 68. Derived from the subject at query time, not from a tag or
        /// a Maildir flag: `passed` means "I forwarded this", which is a
        /// different fact. Nothing is stored and nothing reaches the server.
        IsReceivedForwardRole,

        /// bool; the message was replied to, from the Maildir "R" flag.
        IsRepliedRole,
        ReplyCountRole,    ///< int; 0 when a thread has no replies.

        /// The [general] date_format pattern, or empty for the system's short
        /// format. Same row value for every row.
        ///
        /// Supplied by the model for the same reason as PillColoursRole: it is
        /// the one thing here that holds config, and a delegate reading config
        /// itself would be a second source of truth.
        DateFormatRole,
    };



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

    /// The pattern DateFormatRole answers with. Empty means the system format.
    void setDateFormat(const QString &format) { m_dateFormat = format; }

    /// Extra subject prefixes counting as a received forward (item 68).
    ///
    /// Pushed in from the config exactly as setDateFormat() is, rather than
    /// giving the model a Config: both are display values the window already
    /// holds, and the model draws rather than resolves.
    void setForwardPrefixes(const QStringList &prefixes)
    {
        m_forwardPrefixes = prefixes;
    }

    /// One row per thread, with no expander and no reply count.
    ///
    /// For the Sent view, where a thread is the wrong unit: the user's model of
    /// "what I sent" is a list, and a matching sent message otherwise drags in
    /// the replies they RECEIVED, under a view that claims to be their outbox.
    ///
    /// A flag on this model rather than a second model or a filtered query, and
    /// that is what keeps it from leaking: it is off by default, only the Sent
    /// button turns it on, and every other query turns it off again. The
    /// expander already comes from hasChildren() and the card's count from
    /// ReplyCountRole, so flat mode is those two answering differently and
    /// nothing else changes.
    ///
    /// The children are not discarded, only hidden. Leaving flat mode restores
    /// the tree without reloading anything.
    void setFlatMode(bool flat);

    /// Whether the list is showing the trash view.
    ///
    /// The doomed fill exists to tell the user a message is on its way out of
    /// a view it is still sitting in. In the trash that is redundant: every
    /// row is deleted, and a list painted entirely crimson says nothing while
    /// costing legibility. Set on EVERY query run, like flat mode, so it
    /// cannot leak into the next view.
    void setTrashView(bool trash);

    /// Drops any top-level row whose message no longer carries \p tag.
    ///
    /// The optimistic counterpart to a row simply vanishing at the next query.
    /// Delete strips `inbox`, and in the Inbox view the row it stripped it
    /// from stops belonging there; leaving it until the next sync is what made
    /// a deleted message sit in the inbox looking undeleted.
    ///
    /// Top-level rows ONLY, and deliberately: a reply that no longer matches
    /// still belongs to the conversation the user has open, and removing it
    /// would collapse a thread under the reader's hands. \p tag is the tag the
    /// CURRENT VIEW requires, so a caller passes what the query filters on and
    /// nothing else.
    void removeThreadsWithoutTag(const QString &tag);
    bool flatMode() const { return m_flatMode; }

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

    /// Brings the model to `threads` without resetting it.
    ///
    /// Used by the automatic refresh after a sync, where clear() plus
    /// appendBatch() is the wrong tool: a reset invalidates every index, so the
    /// selection, the expanded threads and the message being read all go with
    /// it. Rows are matched by thread id, so a surviving thread keeps its
    /// identity, its persistent index and its loaded replies.
    ///
    /// Order comes from `threads` and is never imposed here. The worker sorts
    /// the query, so a new thread lands at the front under newest-first and at
    /// the back under oldest-first; forcing new rows to the top would
    /// contradict the sort the user selected.
    void reconcile(const QVector<ThreadSummary> &threads);

    /// The thread at a TOP-LEVEL row.
    ///
    /// **Wrong for any index that might be a reply**, and that is item 88. A
    /// tree numbers rows per parent, so a reply's row() indexes its siblings:
    /// threadAt(0) on the first reply of any thread returns the FIRST THREAD IN
    /// THE LIST, and the caller acts on unrelated mail while every id it
    /// compares looks right. Safe only for a row number that came from a loop
    /// over rowCount(), never from an index the user selected.
    ///
    /// Prefer threadFor(index), which cannot be handed the wrong number.
    ThreadSummary threadAt(int row) const;

    /// The thread an index belongs to, whichever kind of row it is.
    ///
    /// A thread row resolves to itself; a message row resolves through its
    /// PARENT rather than through its own row number. This is the accessor
    /// every caller holding a QModelIndex wants, and it exists because the
    /// row-taking one above silently answers about unrelated mail for a reply.
    ///
    /// An invalid or unknown index gives a default-constructed summary, whose
    /// empty threadId every caller here already treats as "nothing to do".
    ThreadSummary threadFor(const QModelIndex &index) const;

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

    /// Records the tags a MESSAGE really carries, as the worker reported them.
    ///
    /// Exists because `ThreadSummary::tags` is notmuch's UNION over the
    /// thread, which is right for a card standing for a conversation and wrong
    /// for one standing for a message. A four-message thread whose third
    /// message is signed makes the whole thread read as signed, so the root
    /// card and the message pane both claimed a tag the displayed message did
    /// not have.
    ///
    /// Only the ROOT needs this: reply rows already carry their own nodes from
    /// setThreadMessages. Calling it for anything else is a no-op.
    ///
    /// The thread's summary is deliberately NOT rewritten. It describes the
    /// conversation, and three unread siblings do not stop being unread
    /// because this message was read.
    void setRootMessageTags(const QString &messageId, const QStringList &tags);

    /// A loaded message row's node, found by id rather than by position.
    ///
    /// For callers that know WHICH message they mean and must not depend on it
    /// being the row the user has selected. Default-constructed when no
    /// expanded thread holds it.
    MessageNode messageById(const QString &messageId) const;

    /// Resolves a selection into whole THREADS, for the thread-scoped actions.
    ///
    /// A thread row contributes its thread; a message row still contributes
    /// only itself, since a reply's own row cannot be widened into its
    /// conversation without escalating silently. Mixed selections are honoured
    /// as given: a thread root and an unrelated reply act on that whole thread
    /// and that one message.
    ///
    /// **Not the default any more.** Since item 108 the ordinary actions use
    /// messageScopeFor(); this is what the explicit "whole thread" submenu
    /// resolves through.
    ActionScope scopeFor(const QModelIndexList &selection) const;

    /// Resolves a selection into individual MESSAGES, which is what the
    /// ordinary tag actions act on since item 108.
    ///
    /// A thread row contributes the ONE message its card displays, not its
    /// whole conversation. That is `ThreadSummary::firstMessageId`, carried
    /// from the query, so this needs no expansion and no worker round trip.
    /// In the Sent view that field is the first MATCHED message rather than
    /// the thread's opening one, which is right here for the same reason it is
    /// right on the card: both answer "the message this row shows".
    ///
    /// A thread row whose `firstMessageId` is empty contributes nothing. That
    /// is a row the model cannot name a message for, and acting on the whole
    /// thread instead would be the silent escalation this exists to remove.
    ActionScope messageScopeFor(const QModelIndexList &selection) const;

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

    /// The same, for a change scoped to ONE message.
    ///
    /// Repaints that message's own row and leaves the thread alone. The thread
    /// row deliberately does not follow: it stands for the whole conversation,
    /// so redrawing it for a one-message edit would claim every message in it
    /// had changed. That reasoning is why no optimistic update existed here at
    /// all, which left Delete and Toggle unread on a reply moving the pending
    /// count and changing nothing on screen.
    ///
    /// A message id that no expanded thread holds is a no-op: only expanded
    /// threads have message rows, so there is nothing to repaint.
    void applyMessageTagChange(const QString &messageId,
                               const QStringList &added,
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

    /// A newly arrived thread, with its card's own message seeded from the
    /// query.
    ///
    /// `ThreadSummary::tags` is notmuch's union over the conversation, and the
    /// card stands for ONE message. The worker reads that message's own tags
    /// in the same walk that finds its id, so the two tiers are known from the
    /// first paint (item 111). Deriving them from the message LOAD instead
    /// left every unopened row drawing one tier and correcting itself when the
    /// user selected it, which is most of the list.
    ///
    /// `first` is left empty when the query carried no per-message tags, so
    /// anything that supplies only a summary keeps the old behaviour rather
    /// than claiming the union as one message's.
    static ThreadNode nodeFor(const ThreadSummary &summary);

    QVector<ThreadNode> m_threads;
    const TagColors *m_tagColors = nullptr;
    QString m_dateFormat;
    QStringList m_forwardPrefixes;
    bool m_flatMode = false;
    bool m_trashView = false;
};
