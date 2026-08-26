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

#include "threadlistmodel.h"

#include "composecontext.h"

#include <QSet>

#include <QBrush>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QFontDatabase>
#include <QFontMetrics>

namespace {

/// Whether a tag is already drawn on the card as a mark, and so must not also
/// appear as a chip.
///
/// One list, consulted by both PillTagsRole (a thread's chips) and
/// MessageOwnTagsRole (a reply's own chips). Two copies drifted apart is
/// exactly how a tag ends up drawn twice on one row and not at all on another.
bool isDrawnAsAMark(const QString &tag)
{
    static const QStringList marks = {
        QStringLiteral("flagged"),
        QStringLiteral("attachment"),
        QStringLiteral("passed"),
        QStringLiteral("replied"),
    };
    return marks.contains(tag);
}

}   // namespace

QColor ThreadListModel::deletedColour()
{
    // Desaturated crimson: legible under white text on a dark theme, and calm
    // enough that deleting fifty threads does not repaint the list as a
    // warning banner.
    return QColor(0x8b, 0x2c, 0x2c);
}

QColor ThreadListModel::spamColour()
{
    // Distinct hue rather than a lighter red, so spam and deleted are told
    // apart by colour and not by shade.
    return QColor(0xa8, 0x5c, 0x18);
}

QColor ThreadListModel::replyBackground()
{
    // Mixed from the palette rather than fixed, for the same reason as
    // readColour: a tint that reads as "grouped" on a light theme is either
    // invisible or muddy on a dark one.
    //
    // Toward Text rather than toward a hue, so it darkens on a light theme and
    // lightens on a dark one without picking a colour that means something
    // else. 0.07 is deliberately near the threshold of noticing: it is a
    // grouping cue sitting beside the deleted and spam fills, which carry
    // actual meaning and must stay the loudest thing in the list.
    const QPalette palette = QGuiApplication::palette();
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);

    constexpr qreal kWeight = 0.07;
    const qreal inverse = 1.0 - kWeight;
    return QColor::fromRgbF(
        text.redF() * kWeight + base.redF() * inverse,
        text.greenF() * kWeight + base.greenF() * inverse,
        text.blueF() * kWeight + base.blueF() * inverse);
}

QColor ThreadListModel::threadLineColour()
{
    // Stronger than the tint, weaker than the text: the line is structure, so
    // it has to be followable down a long expansion without competing with the
    // senders beside it.
    const QPalette palette = QGuiApplication::palette();
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);

    constexpr qreal kWeight = 0.35;
    const qreal inverse = 1.0 - kWeight;
    return QColor::fromRgbF(
        text.redF() * kWeight + base.redF() * inverse,
        text.greenF() * kWeight + base.greenF() * inverse,
        text.blueF() * kWeight + base.blueF() * inverse);
}

QColor ThreadListModel::readColour()
{
    // Derived from the palette, never hardcoded: a fixed grey that reads as
    // "quiet" on a light theme is nearly invisible on a dark one, which is the
    // rule item 12 established for the message pane.
    //
    // Mixed toward the background rather than simply made transparent, so it
    // composites the same over a selected row as over an unselected one.
    const QPalette palette = QGuiApplication::palette();
    const QColor text = palette.color(QPalette::Text);
    const QColor background = palette.color(QPalette::Base);

    // 0.55 of the text colour: clearly recessive beside an undimmed row, and
    // still comfortably readable on its own. A read thread is not disabled,
    // it is simply not the thing being pointed at.
    constexpr qreal kWeight = 0.55;
    const qreal inverse = 1.0 - kWeight;
    return QColor::fromRgbF(
        text.redF() * kWeight + background.redF() * inverse,
        text.greenF() * kWeight + background.greenF() * inverse,
        text.blueF() * kWeight + background.blueF() * inverse);
}

ThreadListModel::ThreadListModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex ThreadListModel::index(int row, int column,
                                   const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    // A root row. -1 as the internal id marks it, so parent() can tell the two
    // kinds apart without storing a node pointer per index.
    if (!parent.isValid())
        return createIndex(row, column, static_cast<quintptr>(-1));

    // A child row: the internal id is its parent's row, which is all parent()
    // needs to rebuild the thread index.
    return createIndex(row, column, static_cast<quintptr>(parent.row()));
}

QModelIndex ThreadListModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};

    const quintptr id = child.internalId();
    if (id == static_cast<quintptr>(-1))
        return {};

    // Column 0, always. Qt requires a parent index in the first column, and
    // returning the child's own column instead breaks selection and the
    // expander, silently and only for the other columns.
    return createIndex(static_cast<int>(id), 0, static_cast<quintptr>(-1));
}

void ThreadListModel::setFlatMode(bool flat)
{
    if (m_flatMode == flat)
        return;

    // A full reset, not dataChanged. Flat mode changes what hasChildren() and
    // rowCount() answer for every thread row, and a view that has already
    // expanded one is holding indexes below it: dataChanged says "these rows
    // are different", not "the shape under them is gone", and leaves the view
    // drawing children the model no longer offers.
    beginResetModel();
    m_flatMode = flat;
    endResetModel();
}

void ThreadListModel::setTrashView(bool trash)
{
    if (m_trashView == trash)
        return;

    m_trashView = trash;

    // A repaint, NOT a reset: this changes two colour roles and nothing about
    // the shape of the tree, so unlike setFlatMode() there are no child rows
    // to invalidate and a reset would collapse every expanded thread for a
    // change of paint. Emitted over the whole list including children, since
    // the message-row branch reads the same flag.
    if (m_threads.isEmpty())
        return;
    const QVector<int> roles{ Qt::BackgroundRole, Qt::ForegroundRole };
    emit dataChanged(index(0, 0, QModelIndex()),
                     index(m_threads.size() - 1, 0, QModelIndex()), roles);
    for (int row = 0; row < m_threads.size(); ++row) {
        const QModelIndex parent = index(row, 0, QModelIndex());
        const int children = rowCount(parent);
        if (children > 0) {
            emit dataChanged(index(0, 0, parent),
                             index(children - 1, 0, parent), roles);
        }
    }
}

void ThreadListModel::removeThreadsWithoutTag(const QString &tag)
{
    if (tag.isEmpty() || m_threads.isEmpty())
        return;

    // The tags a row is judged on are the ones its CARD draws: the loaded
    // message's own when there is one, the thread's union otherwise. That is
    // the same substitution data() makes for a thread row, and using the
    // summary alone would keep a row whose displayed message lost the tag
    // while a sibling still carries it.
    const auto keeps = [&tag](const ThreadNode &node) {
        if (!node.first.messageId.isEmpty())
            return node.first.tags.contains(tag);
        return node.summary.tags.contains(tag);
    };

    // Backwards, in contiguous runs, exactly as reconcile() removes: each
    // beginRemoveRows renumbers everything after it, so walking forwards
    // removes the wrong rows after the first deletion.
    for (int row = m_threads.size() - 1; row >= 0; --row) {
        if (keeps(m_threads.at(row)))
            continue;
        int first = row;
        while (first > 0 && !keeps(m_threads.at(first - 1)))
            --first;
        beginRemoveRows({}, first, row);
        m_threads.remove(first, row - first + 1);
        endRemoveRows();
        row = first;
    }
}

int ThreadListModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_threads.size();

    // Only a thread row has children, and only in its first column. A tree
    // takes one set of children per row; offering them under every column makes
    // the view draw an expander in each one.
    if (parent.parent().isValid() || parent.column() != 0)
        return 0;

    if (parent.row() < 0 || parent.row() >= m_threads.size())
        return 0;

    // Hidden, not discarded: leaving flat mode restores the tree with no
    // reload, and an expansion loaded before the switch is still there.
    if (m_flatMode)
        return 0;

    return m_threads.at(parent.row()).children.size();
}

bool ThreadListModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return !m_threads.isEmpty();

    // A message row is always a leaf. Reply depth is drawn from the node's own
    // depth, not from further nesting, so nothing hangs under a reply.
    if (parent.parent().isValid())
        return false;

    if (parent.column() != 0)
        return false;

    if (parent.row() < 0 || parent.row() >= m_threads.size())
        return false;

    // No expander in a flat list, whatever the thread turns out to contain.
    if (m_flatMode)
        return false;

    const ThreadNode &node = m_threads.at(parent.row());

    // Once loaded the children are the truth, including "there are none", which
    // is how a thread whose totalCount counted duplicates stops offering an
    // expander that opens onto nothing.
    if (node.loaded)
        return !node.children.isEmpty();

    // Before loading, the summary's count is all there is. A thread of one
    // message has no replies and must not offer an expander.
    return node.summary.totalCount > 1;
}

int ThreadListModel::columnCount(const QModelIndex &parent) const
{
    // One column: the card is drawn whole by CardDelegate. The five-column
    // grid is what item 53 removed.
    //
    // Answered for a valid parent too. Returning 0 there, as the table version
    // did, would give message rows no columns at all and render them blank.
    Q_UNUSED(parent);
    return 1;
}

QVariant ThreadListModel::data(const QModelIndex &index, int role) const
{
    // A stale index from a view that has not caught up with a clear() can carry
    // any row or column, so both bounds are checked rather than trusted.
    if (!index.isValid() || index.row() < 0
        || index.column() != 0) {
        return {};
    }

    // A message row. Handled before the bounds check below, since a child row's
    // number indexes its siblings, not m_threads.
    if (isMessageRow(index)) {
        const MessageNode node = messageAt(index);
        if (node.messageId.isEmpty())
            return {};

        switch (role) {
        case IsMessageRole:
            return true;
        case MessageIdRole:
            return node.messageId;
        case MessageDepthRole:
            return node.depth;
        case HasRepliesRole:
            // A reply never has its own expander: nesting past the first level
            // is drawn from depth, not from further parent-child structure.
            return false;
        case ThreadIdRole:
            // A message row still belongs to a thread, and a caller that only
            // needs the containing thread must not have to walk up itself.
            return node.threadId;
        case TagsRole:
        case PillTagsRole:
            // No strip under a child row: the strip is a ROW-wide band carrying
            // the thread's tags, and one under every reply would stripe the
            // list and repeat the same tags down the whole expansion.
            return QStringList();
        case PillColoursRole:
            return QVariantList();
        case MessageOwnTagsRole: {
            // Set difference against the parent THREAD, not against a global
            // list: "own" means "not already said by the card above this one".
            // The parent row indexes m_threads directly, which is the same
            // mapping messageAt() uses to reach this node.
            const int threadRow = index.parent().row();
            const QStringList threadTags =
                (threadRow >= 0 && threadRow < m_threads.size())
                    ? m_threads.at(threadRow).summary.tags
                    : QStringList();
            QStringList own;
            for (const QString &tag : node.tags) {
                // Drawn as a mark on this row's own line two since items 69
                // and 70, so a chip would repeat it. Filtered here as well as
                // in PillTagsRole because "own" is a difference against the
                // THREAD, and a reply that is flagged where its thread is not
                // would otherwise show both the mark and the word.
                if (isDrawnAsAMark(tag))
                    continue;
                if (!threadTags.contains(tag))
                    own.append(tag);
            }
            // Sorted, so a reply does not reshuffle its own chips between
            // repaints, matching what PillTagsRole already guarantees.
            own.sort();
            return own;
        }
        case MessageOwnColoursRole: {
            const QStringList own =
                data(index, MessageOwnTagsRole).toStringList();
            QVariantList colours;
            colours.reserve(own.size());
            for (const QString &tag : own) {
                colours.append(m_tagColors ? m_tagColors->colourFor(tag)
                                           : TagColors().colourFor(tag));
            }
            return colours;
        }
        case AccountLabelRole:
            return QString();
        case Qt::DisplayRole:
        case SubjectRole:
            return node.subject;
        case SendersRole:
            // The REPLY's sender, not the thread's author summary. Reading the
            // thread's fields here would look almost right, since the first
            // sender usually appears in both.
            return node.from;
        case DateRole:
            return node.date;
        case HasAttachmentRole:
            return node.hasAttachment();
        case IsFlaggedRole:
            return node.isFlagged();
        case IsPassedRole:
            return node.isPassed();
        case IsReceivedForwardRole:
            return ComposeContextBuilder::subjectIsForwarded(node.subject,
                                                             m_forwardPrefixes);
        case IsRepliedRole:
            return node.isReplied();
        case ReplyCountRole:
            // A reply never offers an expander: nesting past the first level is
            // drawn from depth, not from further parent-child structure.
            return 0;
        case DateFormatRole:
            return m_dateFormat;
        case Qt::BackgroundRole:
            // Doomed first: a reply tagged deleted or spam is on its way out
            // and the user has to see that the moment they act, exactly as a
            // thread row does. Without this branch a message-scoped Delete
            // repainted a reply identically to an undeleted one, so the
            // pending count moved and nothing on screen did.
            // Suppressed in the trash view, exactly as on a thread row: see
            // the comment there. Both branches must agree, or an expanded
            // thread in the trash paints its replies crimson under an
            // untinted root.
            if (node.isDoomed()
                && !(m_trashView && node.isDeleted() && !node.isSpam())) {
                return QBrush(node.isDeleted() ? deletedColour()
                                               : spamColour());
            }

            // Tinted, so an expanded thread reads as one block rather than as
            // more table rows. Applied per cell here; ThreadListView fills the
            // same colour across the strip's band so the row does not end up
            // half tinted.
            return replyBackground();
        case Qt::FontRole: {
            // A size down from the thread rows, so a thread reads as the
            // heading and its replies as the contents. The size is what keeps
            // a reply subordinate; bold on top of it is the unread cue, at the
            // user's request on 2026-08-16.
            //
            // Replies were unbolded deliberately at first, on the reasoning
            // that the thread row above already says the conversation has
            // unread mail. That is true of the THREAD and useless for the
            // reply: once a thread is expanded, the row telling the user which
            // messages in it are unread is the only one that can, and dimming
            // alone left the user unable to see a read/unread change at all.
            QFont font = QGuiApplication::font();
            if (font.pointSize() > 0)
                font.setPointSize(qMax(6, font.pointSize() - 1));
            else if (font.pixelSize() > 0)
                font.setPixelSize(qMax(8, font.pixelSize() - 2));

            // Bold combines with the dimming rather than replacing it: two
            // cues for one state, which is what the thread row has had since
            // 2026-08-07 and for the same reason. If the desktop's own font is
            // configured Bold, setBold() changes nothing and the dimming is
            // the whole cue, which CLAUDE.md records as a real configuration
            // on this user's machine.
            if (node.isUnread())
                font.setBold(true);

            // Struck through when doomed, for the same reason the thread row
            // is: the state then survives a screenshot, a colourblind reader,
            // and a theme that overrides the background. A reply had neither
            // this nor the fill, so a message-scoped Delete was invisible.
            if (node.isDoomed())
                font.setStrikeOut(true);
            return font;
        }
        case Qt::ForegroundRole:
            // White over the doomed fill, matching the thread row. The dimmed
            // read colour is mixed toward the BACKGROUND, so leaving it here
            // would compute a grey against the pane's base and then paint it
            // over red.
            //
            // Tied to the FILL, not to isDoomed(): where the fill is
            // suppressed in the trash view there is no red to sit on, and
            // white text would land on the ordinary background unreadable.
            // The strike-out below is deliberately NOT suppressed, since it
            // is the cue that survives without colour at all.
            if (node.isDoomed()
                && !(m_trashView && node.isDeleted() && !node.isSpam())) {
                return QBrush(QColor(Qt::white));
            }

            // Dimmed whether read or not, for the same reason as the font: a
            // reply is subordinate content. An unread one is left undimmed so
            // it can still be found.
            return node.isUnread() ? QVariant() : QVariant(readColour());
        default:
            return {};
        }
    }

    if (index.row() >= m_threads.size())
        return {};

    const ThreadNode &rowNode = m_threads.at(index.row());

    // A card stands for ONE message since item 108, so it must draw that
    // message's tags and not the thread's. `ThreadSummary::tags` is notmuch's
    // UNION over the conversation: a four-message thread whose third message
    // is signed reads as signed, and the card said so about a message that was
    // not (item 110).
    //
    // Only the tags are substituted. Everything else on the card, the subject,
    // the authors, the date and the reply count, describes the THREAD and is
    // correct as it stands; only the tags were ever the union that lied.
    //
    // `first.tags` is populated when the message is loaded, which is when the
    // user selects the row. Before that the union is the only answer available
    // and is what the card shows, which is why an unopened row can still
    // display a sibling's mark. Narrowing that further needs per-message state
    // in the query itself.
    ThreadSummary thread = rowNode.summary;
    if (!rowNode.first.messageId.isEmpty())
        thread.tags = rowNode.first.tags;

    if (role == ThreadIdRole)
        return thread.threadId;

    // Answered rather than left to fall through as an invalid QVariant. An
    // invalid one converts to false and an empty string anyway, so the
    // behaviour is the same, but a role the model never mentions is a latent
    // bug the next reader has to prove is harmless.
    if (role == IsMessageRole)
        return false;

    if (role == MessageIdRole) {
        // The thread's FIRST message, because the root card IS that message:
        // selecting it renders one message, never the whole conversation.
        //
        // The summary carries this from the query, so it is known before the
        // thread has ever been expanded. It used to come only from `first`,
        // populated when the replies loaded, which left this empty on a fresh
        // row and sent the caller down a whole-thread render instead. The same
        // click then behaved differently once the thread had been opened,
        // which is what the user reported as item 66.
        //
        // `first` is still preferred when present: after an expansion it is
        // the same message, read from the tree that is now authoritative for
        // this thread's shape.
        const ThreadNode &node = m_threads.at(index.row());
        return node.first.messageId.isEmpty() ? node.summary.firstMessageId
                                              : node.first.messageId;
    }

    if (role == MessageDepthRole)
        return 0;

    if (role == HasRepliesRole)
        return hasChildren(index.siblingAtColumn(0));

    if (role == TagsRole)
        return thread.tags;

    if (role == MessageOwnTagsRole)
        return QStringList();

    if (role == MessageOwnColoursRole)
        return QVariantList();

    if (role == PillTagsRole || role == PillColoursRole
        || role == PillOwnCountRole) {
        // Everything the row already says another way is dropped: the account
        // is the chip in the subject cell, flagged is the star column,
        // attachment is the paperclip, unread is the row not being dimmed, and
        // inbox is structural rather than informative. Spending the pill row on
        // any of those would repeat what is already on screen.
        //
        // deleted and spam are kept: they repaint the whole row, so a pill is
        // redundant there too, but a doomed thread is rare and worth naming.
        // passed and replied joined this list with item 69: they are drawn
        // marks on line two now, so a chip repeating the word is the same
        // duplication flagged and attachment were already dropped for. This is
        // what item 69 asked for, "tags like Passed and Replied should use
        // icons instead", and the chip has to go or both appear at once.
        static const QStringList hidden = {
            QStringLiteral("inbox"),
            QStringLiteral("unread"),
        };

        const auto pillsFrom = [&](const QStringList &tags) {
            QStringList pills;
            for (const QString &tag : tags) {
                if (hidden.contains(tag) || isDrawnAsAMark(tag)
                    || TagColors::isAccountTag(tag))
                    continue;
                pills.append(tag);
            }
            // Sorted rather than in notmuch's order, which is not guaranteed
            // stable: a row whose pills reordered between repaints would
            // flicker.
            pills.sort();
            return pills;
        };

        // `thread.tags` is the displayed message's own tags once the row has
        // been opened, and the thread's union before that (see the
        // substitution above). The union is always the full set, so the
        // difference is what belongs only to siblings.
        QStringList pills = pillsFrom(thread.tags);
        const int ownCount = pills.size();

        // The sibling tier, appended after the message's own. Shown rather
        // than dropped at the user's request: a card sits above a
        // conversation, so what the rest of it carries is worth seeing, just
        // not at the same weight. The delegate draws these smaller and muted.
        //
        // Empty until the row has been opened, because before that
        // `thread.tags` IS the union and the difference is nothing. That is
        // what makes a chip shrink rather than appear.
        for (const QString &tag : pillsFrom(rowNode.summary.tags)) {
            if (!pills.contains(tag))
                pills.append(tag);
        }

        if (role == PillOwnCountRole)
            return ownCount;

        if (role == PillTagsRole)
            return pills;

        // Same order as the names, so the delegate can walk the two together.
        QVariantList colours;
        colours.reserve(pills.size());
        for (const QString &tag : pills) {
            colours.append(m_tagColors ? m_tagColors->colourFor(tag)
                                       : TagColors().colourFor(tag));
        }
        return colours;
    }

    if (role == AccountLabelRole || role == AccountColourRole) {
        // At most one account tag per thread in practice, but a thread whose
        // messages landed in two mailboxes carries both; the first is shown.
        for (const QString &tag : thread.tags) {
            if (!TagColors::isAccountTag(tag))
                continue;
            if (role == AccountLabelRole) {
                // The configured label when there is one, otherwise the key.
                return m_tagColors ? m_tagColors->labelForAccountTag(tag)
                                   : TagColors::accountKeyForTag(tag);
            }
            return m_tagColors ? m_tagColors->colourFor(tag)
                               : TagColors().colourFor(tag);
        }
        return {};
    }

    if (role == Qt::ToolTipRole) {
        // One tooltip for the whole card, since the marks no longer have
        // columns of their own to be hovered separately. "Important" matches
        // the action's own wording (item 57); the underlying tag is still
        // `flagged` and isFlagged() still tests for it.
        QStringList marks;
        if (thread.isFlagged())
            marks.append(tr("Important"));
        if (thread.hasAttachment())
            marks.append(tr("Has an attachment"));
        return marks.isEmpty() ? QVariant() : marks.join(QStringLiteral(", "));
    }

    switch (role) {
    case Qt::DisplayRole:
    case SubjectRole:
        // Bare, with no "(3)" message-count suffix. The count is drawn on the
        // card's second line as the expander, so a suffix here would state it
        // twice on the same card.
        return thread.subject;
    case SendersRole:
        // Recipients in their place when the query supplied them, which only a
        // Sent query does. The sender there is the user on every row and says
        // nothing; who it went TO is the question the view exists to answer.
        //
        // Falls back to authors when the fold found no usable To, so a message
        // with a malformed or absent recipient header shows the sender rather
        // than a blank line where a name belongs.
        if (!thread.recipients.isEmpty())
            return thread.recipients;
        return thread.authors;
    case DateRole:
        // The QDateTime itself. Formatting belongs to the delegate now: the
        // card decides how much of a date it has room for, and a pre-formatted
        // string takes that decision away from it.
        return thread.date;
    case HasAttachmentRole:
        return thread.hasAttachment();
    case IsFlaggedRole:
        return thread.isFlagged();
    case IsPassedRole:
        return thread.isPassed();
    case IsReceivedForwardRole:
        return ComposeContextBuilder::subjectIsForwarded(thread.subject,
                                                         m_forwardPrefixes);
    case IsRepliedRole:
        return thread.isReplied();
    case ReplyCountRole:
        // Zero in a flat list, so the card draws no expander pill. The count
        // and hasChildren() must agree: a card advertising "3 replies" that
        // cannot be opened is the inert-glyph defect this project has already
        // shipped once.
        if (m_flatMode)
            return 0;
        // totalCount includes the root message, which is the card itself.
        return qMax(0, thread.totalCount - 1);
    case DateFormatRole:
        return m_dateFormat;
    default:
        break;
    }

    // A thread tagged deleted or spam is on its way out, and the user needs to
    // see that the moment they act. Every one of these roles applies to the
    // whole row: a cue on a single column disappears as soon as that column
    // scrolls out of view, which is exactly how the tag change used to go
    // unnoticed.
    //
    // In the TRASH view the deleted fill is suppressed: every row there is
    // deleted, so a list painted entirely crimson tells the user nothing they
    // did not ask for by opening the trash, and costs the legibility the fill
    // borrows. Only `deleted` is suppressed; a SPAM row keeps its tint, since
    // "this is junk" is still news in a folder that only promises "this is
    // thrown away".
    const bool suppressed = m_trashView && thread.isDeleted() && !thread.isSpam();
    if (thread.isDoomed() && !suppressed) {
        if (role == Qt::BackgroundRole)
            return QBrush(thread.isDeleted() ? deletedColour() : spamColour());
        if (role == Qt::ForegroundRole)
            return QBrush(QColor(Qt::white));
    }

    // Unread's second cue, independent of the bold below.
    //
    // Bold alone was the only distinction until 2026-08-07, which leaves
    // nothing to see when the desktop's own font is configured bold: every row
    // renders bold and setBold() changes nothing. That is a font setting
    // rather than a defect here, but a cue with a single point of failure is
    // worth reinforcing.
    //
    // So the emphasis is inverted as well. Unread rows are left at the
    // palette's own text colour, and READ rows are dimmed toward the
    // background. The cue rides on ForegroundRole, which the delegate already
    // honours, and costs no column. It also suits the real ratio: with a few
    // dozen unread among thousands read, dimming the bulk is calmer than
    // highlighting it.
    //
    // BELOW the doomed branch on purpose, and that ordering is the whole
    // protection: a deleted or spam thread has already returned white text for
    // this role above, and dimming it because it is also read would drop that
    // to unreadable against the crimson. Do not hoist this.
    if (role == Qt::ForegroundRole && !thread.isUnread())
        return QBrush(readColour());

    if (role == Qt::FontRole) {
        QFont font;
        bool styled = false;
        if (thread.isUnread()) {
            font.setBold(true);
            styled = true;
        }
        // Struck through as well as filled, so the state survives a
        // screenshot, a colourblind reader, and a theme that overrides the
        // background.
        if (thread.isDoomed()) {
            font.setStrikeOut(true);
            styled = true;
        }
        if (styled)
            return font;
    }

    return {};
}

ThreadListModel::ThreadNode
ThreadListModel::nodeFor(const ThreadSummary &summary)
{
    ThreadNode node{ summary, {}, {}, false };

    // Only when the query actually supplied them. An empty list here would be
    // indistinguishable from "this message carries nothing", which would put
    // every chip in the sibling tier and mute the whole card.
    if (!summary.firstMessageId.isEmpty()
        && !summary.firstMessageTags.isEmpty()) {
        node.first.messageId = summary.firstMessageId;
        node.first.threadId = summary.threadId;
        node.first.tags = summary.firstMessageTags;
        // Carried alongside the tags, for the same reason messageById()
        // carries it onto a synthesised root: an unexpanded row has to know
        // which account it belongs to before Delete can name a folder.
        node.first.filePath = summary.firstMessagePath;
    }
    return node;
}

void ThreadListModel::appendBatch(const QVector<ThreadSummary> &batch)
{
    // beginInsertRows with an empty range violates Qt's contract, so the guard
    // has to come before the signal, not inside it.
    if (batch.isEmpty())
        return;

    const int first = m_threads.size();
    beginInsertRows({}, first, first + batch.size() - 1);
    for (const ThreadSummary &summary : batch)
        m_threads.append(nodeFor(summary));
    endInsertRows();
}

void ThreadListModel::reconcile(const QVector<ThreadSummary> &threads)
{
    // Removals first, walking BACKWARDS. Each beginRemoveRows renumbers
    // everything after it, so a forward walk would delete by stale indices; a
    // backward one only ever disturbs rows it has already passed.
    //
    // One signal per contiguous run rather than per row: a view rebuilds its
    // selection and its persistent indexes on every one, and an Unread view
    // emptied by a sync can drop dozens at once.
    QSet<QString> wanted;
    wanted.reserve(threads.size());
    for (const ThreadSummary &summary : threads)
        wanted.insert(summary.threadId);

    for (int row = m_threads.size() - 1; row >= 0; --row) {
        if (wanted.contains(m_threads.at(row).summary.threadId))
            continue;
        int first = row;
        while (first > 0
               && !wanted.contains(m_threads.at(first - 1).summary.threadId))
            --first;
        beginRemoveRows({}, first, row);
        m_threads.remove(first, row - first + 1);
        endRemoveRows();
        row = first;
    }

    // What survived, by id, so the second pass can tell an arrival from a
    // thread that merely moved.
    QHash<QString, int> present;
    present.reserve(m_threads.size());
    for (int row = 0; row < m_threads.size(); ++row)
        present.insert(m_threads.at(row).summary.threadId, row);

    // Insertions, forwards, at the position the RESULT gives them. Walking the
    // result in order means each new thread is placed against rows already
    // agreed on, so the model ends in the result's order without this having to
    // know what that order means.
    for (int target = 0; target < threads.size(); ++target) {
        const ThreadSummary &summary = threads.at(target);
        const auto it = present.constFind(summary.threadId);

        if (it == present.constEnd()) {
            const int at = qMin(target, m_threads.size());
            beginInsertRows({}, at, at);
            m_threads.insert(at, nodeFor(summary));
            endInsertRows();

            // Every later row shifted by one, and the map is read again on the
            // next iteration.
            for (auto entry = present.begin(); entry != present.end(); ++entry) {
                if (entry.value() >= at)
                    ++entry.value();
            }
            continue;
        }

        // A survivor that MOVED, which is neither an arrival nor a departure
        // and is the commonest reordering there is: a new reply bumps an old
        // thread to the front under newest-first. beginMoveRows, not a
        // remove-and-insert pair, because a removed row takes its persistent
        // index, its selection and its expansion with it, which is exactly what
        // this method exists to keep.
        //
        // Always UPWARDS, and that is a property of the walk rather than an
        // assumption about the data. Positions ahead of `target` are already
        // final, so a survivor found at a later row is pulled forward and one
        // found earlier cannot exist: it would have been placed on a previous
        // iteration. A downward branch here would be unreachable, so there
        // isn't one, and the destination needs no adjustment (Qt reads it
        // before the source is removed, which only shifts a downward move).
        int row = it.value();
        if (row != target) {
            Q_ASSERT(row > target);
            beginMoveRows({}, row, row, {}, target);
            m_threads.move(row, target);
            endMoveRows();

            // Every row between the two shifted one place later.
            for (auto entry = present.begin(); entry != present.end(); ++entry) {
                if (entry.value() >= target && entry.value() < row)
                    ++entry.value();
            }
            present[summary.threadId] = target;
            row = target;
        }

        // Keep the ROW, replace the summary. The node is not reconstructed,
        // because its children and its loaded flag are the expansion state this
        // whole method exists to preserve.
        if (m_threads.at(row).summary.tags != summary.tags
            || m_threads.at(row).summary.subject != summary.subject
            || m_threads.at(row).summary.authors != summary.authors
            || m_threads.at(row).summary.date != summary.date
            || m_threads.at(row).summary.totalCount != summary.totalCount
            || m_threads.at(row).summary.matchedCount != summary.matchedCount
            // The card's OWN message, which can move while the thread's union
            // does not: a root read elsewhere leaves the thread unread as long
            // as any reply is. Without this the card kept the tags it was
            // first given, and the sibling tier with them.
            || m_threads.at(row).summary.firstMessageTags
                   != summary.firstMessageTags) {
            m_threads[row].summary = summary;

            // The node too, since the card draws its tags from there. Only the
            // tags: the node's children and loaded flag are the expansion
            // state this whole method exists to preserve, and `first` carries
            // no children.
            if (!summary.firstMessageId.isEmpty()
                && !summary.firstMessageTags.isEmpty()) {
                m_threads[row].first.messageId = summary.firstMessageId;
                m_threads[row].first.threadId = summary.threadId;
                m_threads[row].first.tags = summary.firstMessageTags;
            }

            emit dataChanged(index(row, 0), index(row, 0));
        }
    }
}

void ThreadListModel::clear()
{
    beginResetModel();
    m_threads.clear();
    endResetModel();
}

void ThreadListModel::setThreadMessages(const QString &threadId,
                                        const QVector<MessageNode> &nodes)
{
    for (int row = 0; row < m_threads.size(); ++row) {
        if (m_threads.at(row).summary.threadId != threadId)
            continue;

        const QModelIndex parent = index(row, 0, QModelIndex());

        // Replace, not append. A thread reloaded after a sync would otherwise
        // list every reply twice.
        if (!m_threads.at(row).children.isEmpty()) {
            beginRemoveRows(parent, 0, m_threads.at(row).children.size() - 1);
            m_threads[row].children.clear();
            endRemoveRows();
        }

        // Every message EXCEPT the first, which is the root card itself.
        //
        // Selecting on depth > 0 instead was wrong, and wrong in a way that
        // only showed on real mail: notmuch_thread_get_toplevel_messages
        // returns every message at depth 0 when a thread carries no usable
        // In-Reply-To, so a flat thread contributed no children at all. The
        // card advertised "3 replies" and expanded onto nothing. Measured in
        // the user's database: of 396 inbox threads, three are flat, one of
        // them nine messages long, and every two-message thread of this kind
        // was affected, which is why the fault looked like "the expander only
        // works with more than one reply".
        //
        // Position also happens to be the right rule rather than a workaround.
        // The root card IS the thread's first message, so the row under it is
        // the second message whatever depth notmuch assigns it.
        QVector<MessageNode> children = nodes.mid(1);

        // Kept so the root card can render its own message. It is the card the
        // user clicks to read the thread's opening message.
        m_threads[row].first = nodes.isEmpty() ? MessageNode() : nodes.first();

        if (!children.isEmpty()) {
            beginInsertRows(parent, 0, children.size() - 1);
            m_threads[row].children = children;
            endInsertRows();
        }

        // Set even when there are no replies: that is the difference between a
        // single-message thread and one whose replies were never fetched.
        m_threads[row].loaded = true;
        return;
    }
}

bool ThreadListModel::isMessageRow(const QModelIndex &index) const
{
    return index.isValid() && index.parent().isValid();
}

MessageNode ThreadListModel::messageAt(const QModelIndex &index) const
{
    if (!isMessageRow(index))
        return {};

    const int threadRow = index.parent().row();
    if (threadRow < 0 || threadRow >= m_threads.size())
        return {};

    const QVector<MessageNode> &children = m_threads.at(threadRow).children;
    if (index.row() < 0 || index.row() >= children.size())
        return {};

    return children.at(index.row());
}

QString ThreadListModel::threadIdForMessage(const QString &messageId) const
{
    for (const ThreadNode &node : m_threads) {
        for (const MessageNode &child : node.children) {
            if (child.messageId == messageId)
                return node.summary.threadId;
        }
    }
    return {};
}

void ThreadListModel::setRootMessageTags(const QString &messageId,
                                         const QStringList &tags)
{
    if (messageId.isEmpty())
        return;

    for (int row = 0; row < m_threads.size(); ++row) {
        ThreadNode &node = m_threads[row];
        if (node.summary.firstMessageId != messageId
            && node.first.messageId != messageId) {
            continue;
        }

        if (node.first.tags == tags && !node.first.messageId.isEmpty())
            return;   // Nothing changed; do not churn the view.

        // Enough of a node for the card to draw from. The rest of the display
        // still comes from the summary, which is correct for it: the subject,
        // the authors and the date describe the thread, and only the TAGS were
        // ever the union that lied about this message.
        node.first.messageId = messageId;
        node.first.threadId = node.summary.threadId;
        node.first.tags = tags;

        const QModelIndex threadIndex = index(row, 0, QModelIndex());
        emit dataChanged(threadIndex, threadIndex);
        return;
    }
}

MessageNode ThreadListModel::messageById(const QString &messageId) const
{
    if (messageId.isEmpty())
        return {};

    for (const ThreadNode &node : m_threads) {
        // The root's own message first, and it is not among the children:
        // setThreadMessages drops depth 0 because the root row stands for it.
        // Searching only the children returned a default-constructed node for
        // every root message, and a caller that trusted it set the message
        // pane's tag strip to that empty tag list, wiping a strip that had
        // been correct.
        if (!node.first.messageId.isEmpty()
            && node.first.messageId == messageId) {
            return node.first;
        }

        // Before expansion there is no node, so the answer is assembled from
        // the summary: for a thread of one, its tags ARE this message's, since
        // a thread's tags are a union over its messages. For a longer thread
        // they are a union over messages this one is only part of, which is
        // wider than the truth but is also exactly what the card shows, so a
        // caller repainting from it stays consistent with the row beside it.
        if (node.summary.firstMessageId == messageId) {
            MessageNode root;
            root.messageId = node.summary.firstMessageId;
            root.threadId = node.summary.threadId;
            root.subject = node.summary.subject;
            root.date = node.summary.date;
            root.tags = node.summary.tags;
            // Carried from the query, so an UNEXPANDED row still knows which
            // account it belongs to. Delete needs that to name a trash folder,
            // and an unexpanded row is the ordinary case rather than an edge
            // one: without this every thread row resolved to no account and
            // Delete reported "no trash folder configured" for all of them.
            root.filePath = node.summary.firstMessagePath;
            return root;
        }

        for (const MessageNode &child : node.children) {
            if (child.messageId == messageId)
                return child;
        }
    }
    return {};
}

ActionScope ThreadListModel::messageScopeFor(
    const QModelIndexList &selection) const
{
    ActionScope scope;

    for (const QModelIndex &index : selection) {
        QString messageId;
        if (isMessageRow(index)) {
            messageId = messageAt(index).messageId;
        } else {
            if (index.row() < 0 || index.row() >= m_threads.size())
                continue;
            // The message the CARD displays, which the query already named.
            // Not the loaded children: a thread the user never expanded still
            // shows its first message, and this must work without one.
            messageId = m_threads.at(index.row()).summary.firstMessageId;
        }

        // Skipped rather than widened. Falling back to the thread here would
        // silently act on messages the row does not display, which is the
        // behaviour item 108 removed.
        if (messageId.isEmpty() || scope.messageIds.contains(messageId))
            continue;

        scope.messageIds.append(messageId);
        scope.messageCount += 1;
    }

    return scope;
}

ActionScope ThreadListModel::scopeFor(const QModelIndexList &selection) const
{
    ActionScope scope;

    for (const QModelIndex &index : selection) {
        if (isMessageRow(index)) {
            const MessageNode node = messageAt(index);
            if (node.messageId.isEmpty()
                || scope.messageIds.contains(node.messageId))
                continue;
            scope.messageIds.append(node.messageId);
            scope.messageCount += 1;
            continue;
        }

        if (index.row() < 0 || index.row() >= m_threads.size())
            continue;

        const ThreadSummary &summary = m_threads.at(index.row()).summary;
        if (scope.threadIds.contains(summary.threadId))
            continue;

        scope.threadIds.append(summary.threadId);

        // totalCount, not the loaded children: a thread that was never expanded
        // still has all of its messages, and counting only what happens to be
        // on screen would understate what the action does. Floored at 1, since
        // a summary with no count still stands for at least the message that
        // produced it.
        scope.messageCount += qMax(1, summary.totalCount);
        scope.wholeThread = true;
    }

    return scope;
}

ThreadSummary ThreadListModel::threadAt(int row) const
{
    if (row < 0 || row >= m_threads.size())
        return {};
    return m_threads.at(row).summary;
}

ThreadSummary ThreadListModel::threadFor(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    // The parent's row for a message, its own for a thread. Both are top-level
    // numbers by the time threadAt() sees them, which is the whole point: the
    // conversion happens once, here, instead of at every call site that has to
    // remember which kind of row it is holding.
    const QModelIndex threadIndex = isMessageRow(index) ? index.parent() : index;
    return threadAt(threadIndex.row());
}

QStringList ThreadListModel::accountKeysForThread(const QString &threadId) const
{
    QStringList keys;
    for (const ThreadNode &node : m_threads) {
        const ThreadSummary &thread = node.summary;
        if (thread.threadId != threadId)
            continue;
        for (const QString &tag : thread.tags) {
            if (!TagColors::isAccountTag(tag))
                continue;
            const QString key = TagColors::accountKeyForTag(tag);
            if (!key.isEmpty() && !keys.contains(key))
                keys.append(key);
        }
        break;
    }
    return keys;
}

void ThreadListModel::applyTagChange(const QString &threadId,
                                     const QStringList &added,
                                     const QStringList &removed)
{
    for (int row = 0; row < m_threads.size(); ++row) {
        if (m_threads.at(row).summary.threadId != threadId)
            continue;

        QStringList &tags = m_threads[row].summary.tags;
        for (const QString &tag : removed)
            tags.removeAll(tag);
        for (const QString &tag : added) {
            if (!tags.contains(tag))
                tags.append(tag);
        }

        // The whole card repaints: unread state drives its font, and the tags
        // it draws on line 3 have just changed.
        const QModelIndex threadIndex = index(row, 0);
        emit dataChanged(threadIndex, threadIndex);

        // And every LOADED reply, because a thread-scoped write reaches every
        // message in the thread. Updating only the summary left an expanded
        // thread showing replies that still carried the old tags: marking a
        // thread read repainted the card and left its replies bold and
        // undimmed, describing a state the database no longer held. They
        // corrected themselves on the next query, which is what made it look
        // like a repaint bug rather than a stale model.
        //
        // Only the loaded ones exist to update. An unexpanded thread has no
        // child rows, and the replies it does not hold are the database's
        // business, not this model's.
        QVector<MessageNode> &children = m_threads[row].children;
        if (children.isEmpty())
            return;

        for (MessageNode &child : children) {
            for (const QString &tag : removed)
                child.tags.removeAll(tag);
            for (const QString &tag : added) {
                if (!child.tags.contains(tag))
                    child.tags.append(tag);
            }
        }

        // One span for the whole expansion rather than a signal per reply: the
        // rows are contiguous under this parent and a view coalesces them
        // anyway.
        emit dataChanged(index(0, 0, threadIndex),
                         index(children.size() - 1, 0, threadIndex));
        return;
    }
}

void ThreadListModel::applyMessageTagChange(const QString &messageId,
                                            const QStringList &added,
                                            const QStringList &removed)
{
    if (messageId.isEmpty())
        return;

    const auto retag = [&](QStringList &tags) {
        for (const QString &tag : removed)
            tags.removeAll(tag);
        for (const QString &tag : added) {
            if (!tags.contains(tag))
                tags.append(tag);
        }
    };

    for (int row = 0; row < m_threads.size(); ++row) {
        ThreadNode &node = m_threads[row];
        const QModelIndex threadIndex = index(row, 0, QModelIndex());

        // The ROOT card's own message, which is not among the children:
        // setThreadMessages drops depth 0 because the root row stands for it.
        // Searching only the children meant a write to the message a root card
        // displays found nothing and repainted nothing, and item 108 made that
        // the ordinary gesture rather than an edge case.
        //
        // Matched on the summary's id as well as the loaded node's, because the
        // node is empty until the thread has been expanded and the user acts on
        // unexpanded threads constantly.
        const bool isRoot =
            node.summary.firstMessageId == messageId
            || (!node.first.messageId.isEmpty()
                && node.first.messageId == messageId);
        if (isRoot) {
            // The root's own node, which is what the card draws its tags from
            // once the message has been loaded. Seeded from the summary when
            // the message has never been loaded, so an edit made before the
            // row was ever opened still has somewhere to land; the summary is
            // the union, which is the widest honest starting point.
            if (node.first.messageId.isEmpty()) {
                node.first.messageId = node.summary.firstMessageId;
                node.first.threadId = node.summary.threadId;
                node.first.tags = node.summary.tags;
                node.first.filePath = node.summary.firstMessagePath;
            }
            retag(node.first.tags);

            // The SUMMARY only for a single-message thread. A thread's tags are
            // a UNION over its messages: for a thread of one that union IS this
            // message, so keeping the two in step is exact; for a longer
            // thread, deleting one message does not delete the conversation,
            // and the summary must keep describing the conversation because
            // that is what the thread-scoped actions and the query read.
            //
            // The CARD does not depend on this either way: since item 110 it
            // draws its tags from first.tags, which was just updated. This
            // keeps the summary honest for everything else that reads it.
            if (node.summary.totalCount <= 1)
                retag(node.summary.tags);

            emit dataChanged(threadIndex, threadIndex);
            return;
        }

        QVector<MessageNode> &children = node.children;
        for (int child = 0; child < children.size(); ++child) {
            if (children.at(child).messageId != messageId)
                continue;

            retag(children[child].tags);

            // The reply's own row, and only that row. Its chips, its marks,
            // its dimming and its doomed fill all read the node's tags.
            const QModelIndex replyIndex = index(child, 0, threadIndex);
            emit dataChanged(replyIndex, replyIndex);
            return;
        }
    }
}
