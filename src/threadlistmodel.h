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

#include <QAbstractTableModel>
#include <QColor>
#include <QVector>

#include "types.h"

/// Table model over query results, filled in batches so a large query paints
/// its first screenful immediately.
class ThreadListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /// Subject stretches to fill the view, so it must come last: anything
    /// after it is pushed out of sight. Tags leads, being the column that
    /// changes when the user acts on a thread.
    enum Column {
        TagsColumn = 0,
        DateColumn,
        AuthorsColumn,
        SubjectColumn,
        ColumnCount,
    };

    enum Role {
        /// The thread id behind a row. Views hand out QModelIndexes, but the
        /// worker speaks thread ids, so the mapping belongs on the model
        /// rather than in every caller.
        ThreadIdRole = Qt::UserRole + 1,
    };

    /// Row fill for a thread tagged `deleted`, and for one tagged `spam`.
    /// Muted rather than saturated: a bulk delete paints every selected row,
    /// and a wall of pure red is harder to read than the list it replaces.
    /// Exposed so a test names the same colour the model uses.
    static QColor deletedColour();
    static QColor spamColour();

    explicit ThreadListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    void appendBatch(const QVector<ThreadSummary> &batch);
    void clear();

    ThreadSummary threadAt(int row) const;

    /// Applies a tag change locally so the UI updates before the worker
    /// confirms. To revert a failed write, call again with added and removed
    /// swapped.
    void applyTagChange(const QString &threadId, const QStringList &added,
                        const QStringList &removed);

private:
    QVector<ThreadSummary> m_threads;
};
