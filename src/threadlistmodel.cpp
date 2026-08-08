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
    // to register at column width beside a paperclip.
    static const QString glyph = [] {
        const char32_t star = 0x2605;
        const QString preferred = QString::fromUcs4(&star, 1);
        const QFontMetrics metrics{QFontDatabase::systemFont(
            QFontDatabase::GeneralFont)};
        return metrics.inFontUcs4(star) ? preferred : QStringLiteral("*");
    }();
    return glyph;
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

    return m_threads.at(parent.row()).children.size();
}

int ThreadListModel::columnCount(const QModelIndex &parent) const
{
    // Every level has the same columns. Returning 0 for a valid parent, as the
    // table version did, would give message rows no columns at all and render
    // them blank.
    Q_UNUSED(parent);
    return ColumnCount;
}

QVariant ThreadListModel::data(const QModelIndex &index, int role) const
{
    // A stale index from a view that has not caught up with a clear() can carry
    // any row or column, so both bounds are checked rather than trusted.
    if (!index.isValid() || index.row() < 0
        || index.column() < 0 || index.column() >= ColumnCount) {
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
        case AccountLabelRole:
            return QString();
        case Qt::DisplayRole:
            switch (index.column()) {
            case AuthorsColumn:
                // The REPLY's sender, not the thread's author summary. Reading
                // the thread's fields here would look almost right, since the
                // first sender usually appears in both.
                return node.from;
            case SubjectColumn:
                return node.subject;
            case DateColumn:
                return node.date.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
            case AttachmentColumn:
                return node.hasAttachment() ? attachmentGlyph() : QString();
            case FlagColumn:
                return node.isFlagged() ? flagGlyph() : QString();
            default:
                return {};
            }
        case Qt::ForegroundRole:
            // Same rule as a thread row: read recedes, unread stays at the
            // palette's own colour.
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

    if (role == MessageIdRole)
        return QString();

    if (role == MessageDepthRole)
        return 0;

    if (role == TagsRole)
        return thread.tags;

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

    if (role == Qt::ToolTipRole && index.column() == AttachmentColumn)
        return thread.hasAttachment() ? tr("Has an attachment") : QVariant();

    if (role == Qt::ToolTipRole && index.column() == FlagColumn)
        return thread.isFlagged() ? tr("Flagged") : QVariant();

    // Both marker columns: a glyph reads as a marker only when it sits in the
    // middle of its column rather than against the text beside it.
    if (role == Qt::TextAlignmentRole
        && (index.column() == AttachmentColumn || index.column() == FlagColumn)) {
        return QVariant::fromValue(Qt::AlignCenter);
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case AttachmentColumn:
            // A glyph rather than an icon resource: no new asset to ship, and
            // it inherits the row's font, so it strikes through with a doomed
            // thread like every other cell.
            return thread.hasAttachment() ? attachmentGlyph() : QString();
        case FlagColumn:
            // A glyph rather than an icon, for the same reasons as the
            // paperclip: no asset to ship, and it inherits the row's font so
            // it strikes through with a doomed thread.
            return thread.isFlagged() ? flagGlyph() : QString();
        case DateColumn:
            return thread.date.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        case AuthorsColumn:
            return thread.authors;
        case SubjectColumn:
            return thread.totalCount > 1
                ? QStringLiteral("%1 (%2)").arg(thread.subject)
                      .arg(thread.totalCount)
                : thread.subject;
        default:
            return {};
        }
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

QVariant ThreadListModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    // No label: any text would set a minimum width far wider than the icon,
    // which defeats the point of a narrow column.
    case AttachmentColumn: return QString();
    case FlagColumn:       return QString();
    case DateColumn:    return tr("Date");
    case AuthorsColumn: return tr("From");
    case SubjectColumn: return tr("Subject");
    default:            return {};
    }
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
        m_threads.append(ThreadNode{ summary, {}, false });
    endInsertRows();
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

        QVector<MessageNode> children;
        children.reserve(nodes.size());
        for (const MessageNode &node : nodes) {
            if (node.depth > 0)
                children.append(node);
        }

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

        // The whole row repaints: unread state drives the font of every column,
        // not just the tags one.
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        return;
    }
}
