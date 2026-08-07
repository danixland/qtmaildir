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
    : QAbstractTableModel(parent)
{
}

int ThreadListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_threads.size();
}

int ThreadListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ThreadListModel::data(const QModelIndex &index, int role) const
{
    // A stale index from a view that has not caught up with a clear() can carry
    // any row or column, so both bounds are checked rather than trusted.
    if (!index.isValid() || index.row() < 0 || index.row() >= m_threads.size()
        || index.column() < 0 || index.column() >= ColumnCount) {
        return {};
    }

    const ThreadSummary &thread = m_threads.at(index.row());

    if (role == ThreadIdRole)
        return thread.threadId;

    if (role == TagsRole)
        return thread.tags;

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

    if (role == Qt::TextAlignmentRole && index.column() == AttachmentColumn)
        return QVariant::fromValue(Qt::AlignCenter);

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case AttachmentColumn:
            // A glyph rather than an icon resource: no new asset to ship, and
            // it inherits the row's font, so it strikes through with a doomed
            // thread like every other cell.
            return thread.hasAttachment() ? attachmentGlyph() : QString();
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

    // Unread's cue, and it deliberately does NOT rely on the bold below.
    //
    // Bold was the only cue until 2026-08-07, when it turned out to render
    // identically to regular on the user's system: confirmed with a bare
    // QTableView and a plain QStandardItemModel, so the fault is in Qt or
    // fontconfig, below this application, and nothing here can reach it.
    //
    // So the emphasis is inverted instead. Unread rows are left at the
    // palette's own text colour, and READ rows are dimmed toward the
    // background. That way the cue rides on ForegroundRole, which the delegate
    // already honours, and it costs no column. It also suits the real ratio:
    // with a few dozen unread among thousands read, dimming the bulk is calmer
    // than highlighting it.
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
    case DateColumn:    return QStringLiteral("Date");
    case AuthorsColumn: return QStringLiteral("From");
    case SubjectColumn: return QStringLiteral("Subject");
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
    m_threads.append(batch);
    endInsertRows();
}

void ThreadListModel::clear()
{
    beginResetModel();
    m_threads.clear();
    endResetModel();
}

ThreadSummary ThreadListModel::threadAt(int row) const
{
    if (row < 0 || row >= m_threads.size())
        return {};
    return m_threads.at(row);
}

void ThreadListModel::applyTagChange(const QString &threadId,
                                     const QStringList &added,
                                     const QStringList &removed)
{
    for (int row = 0; row < m_threads.size(); ++row) {
        if (m_threads.at(row).threadId != threadId)
            continue;

        QStringList &tags = m_threads[row].tags;
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
