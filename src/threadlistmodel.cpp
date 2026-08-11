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

#include <QSet>

#include <QBrush>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QFontDatabase>
#include <QFontMetrics>

QString ThreadListModel::attachmentGlyph()
{
    // U+1F4CE PAPERCLIP, with a fallback for a system whose default font
    // cannot draw it: an unrenderable codepoint shows as a tofu box, which
    // reads as "something is broken" rather than "this has an attachment".
    // Computed once; the font does not change under a running application.
    static const QString glyph = [] {
        const char32_t paperclip = 0x1F4CE;
        const QString preferred = QString::fromUcs4(&paperclip, 1);
        const QFontMetrics metrics{QFontDatabase::systemFont(
            QFontDatabase::GeneralFont)};
        // "*" as the fallback: ASCII, present in every practical font, and
        // unambiguous in a column that shows nothing else.
        return metrics.inFontUcs4(paperclip) ? preferred
                                             : QStringLiteral("*");
    }();
    return glyph;
}

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

QString ThreadListModel::flagGlyph()
{
    // U+2605 BLACK STAR, with the same fallback reasoning as the paperclip: an
    // unrenderable codepoint shows as tofu, which reads as breakage rather
    // than as "flagged". The solid star, not the outlined U+2606, since it has
    // to register at small size beside a paperclip.
    static const QString glyph = [] {
        const char32_t star = 0x2605;
        const QString preferred = QString::fromUcs4(&star, 1);
        const QFontMetrics metrics{QFontDatabase::systemFont(
            QFontDatabase::GeneralFont)};
        return metrics.inFontUcs4(star) ? preferred : QStringLiteral("*");
    }();
    return glyph;
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
        case ReplyCountRole:
            // A reply never offers an expander: nesting past the first level is
            // drawn from depth, not from further parent-child structure.
            return 0;
        case DateFormatRole:
            return m_dateFormat;
        case Qt::BackgroundRole:
            // Tinted, so an expanded thread reads as one block rather than as
            // more table rows. Applied per cell here; ThreadListView fills the
            // same colour across the strip's band so the row does not end up
            // half tinted.
            return replyBackground();
        case Qt::FontRole: {
            // A size down from the thread rows, so a thread reads as the
            // heading and its replies as the contents. Never bold: an unread
            // reply is still subordinate to the thread it belongs to, and the
            // thread row above already carries the unread cue for the whole
            // conversation.
            QFont font = QGuiApplication::font();
            if (font.pointSize() > 0)
                font.setPointSize(qMax(6, font.pointSize() - 1));
            else if (font.pixelSize() > 0)
                font.setPixelSize(qMax(8, font.pixelSize() - 2));
            return font;
        }
        case Qt::ForegroundRole:
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

    const ThreadSummary &thread = m_threads.at(index.row()).summary;

    if (role == ThreadIdRole)
        return thread.threadId;

    // Answered rather than left to fall through as an invalid QVariant. An
    // invalid one converts to false and an empty string anyway, so the
    // behaviour is the same, but a role the model never mentions is a latent
    // bug the next reader has to prove is harmless.
    if (role == IsMessageRole)
        return false;

    if (role == MessageIdRole) {
        // The thread's FIRST message, once known, because the root card is
        // that message: selecting it renders one message rather than the whole
        // conversation. Empty before the replies are loaded, which is the
        // caller's signal to load the thread instead of guessing at a message.
        return m_threads.at(index.row()).first.messageId;
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

    if (role == PillTagsRole || role == PillColoursRole) {
        // Everything the row already says another way is dropped: the account
        // is the chip in the subject cell, flagged is the star column,
        // attachment is the paperclip, unread is the row not being dimmed, and
        // inbox is structural rather than informative. Spending the pill row on
        // any of those would repeat what is already on screen.
        //
        // deleted and spam are kept: they repaint the whole row, so a pill is
        // redundant there too, but a doomed thread is rare and worth naming.
        static const QStringList hidden = {
            QStringLiteral("inbox"),
            QStringLiteral("unread"),
            QStringLiteral("flagged"),
            QStringLiteral("attachment"),
        };

        QStringList pills;
        for (const QString &tag : thread.tags) {
            if (hidden.contains(tag) || TagColors::isAccountTag(tag))
                continue;
            pills.append(tag);
        }
        // Sorted rather than in notmuch's order, which is not guaranteed
        // stable: a row whose pills reordered between repaints would flicker.
        pills.sort();

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
    if (thread.isDoomed()) {
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

void ThreadListModel::appendBatch(const QVector<ThreadSummary> &batch)
{
    // beginInsertRows with an empty range violates Qt's contract, so the guard
    // has to come before the signal, not inside it.
    if (batch.isEmpty())
        return;

    const int first = m_threads.size();
    beginInsertRows({}, first, first + batch.size() - 1);
    for (const ThreadSummary &summary : batch)
        m_threads.append(ThreadNode{ summary, {}, {}, false });
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
            m_threads.insert(at, ThreadNode{ summary, {}, {}, false });
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
            || m_threads.at(row).summary.matchedCount != summary.matchedCount) {
            m_threads[row].summary = summary;
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
        emit dataChanged(index(row, 0), index(row, 0));
        return;
    }
}
