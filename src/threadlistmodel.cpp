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

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case DateColumn:
            return thread.date.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        case AuthorsColumn:
            return thread.authors;
        case SubjectColumn:
            return thread.totalCount > 1
                ? QStringLiteral("%1 (%2)").arg(thread.subject)
                      .arg(thread.totalCount)
                : thread.subject;
        case TagsColumn:
            return thread.tags.join(QLatin1Char(' '));
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
    case DateColumn:    return QStringLiteral("Date");
    case AuthorsColumn: return QStringLiteral("From");
    case SubjectColumn: return QStringLiteral("Subject");
    case TagsColumn:    return QStringLiteral("Tags");
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
