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

#include "tagcolors.h"
#include "types.h"

/// Table model over query results, filled in batches so a large query paints
/// its first screenful immediately.
class ThreadListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /// No tags column: spelling out a dozen tags per row cost most of the
    /// list's width and was unreadable. Functional tags moved to a chip strip
    /// under the message pane, and the account tag renders as a chip in front
    /// of the subject.
    enum Column {
        DateColumn = 0,
        AuthorsColumn,
        SubjectColumn,
        ColumnCount,
    };

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
    };

    /// Row fill for a thread tagged `deleted`, and for one tagged `spam`.
    /// Muted rather than saturated: a bulk delete paints every selected row,
    /// and a wall of pure red is harder to read than the list it replaces.
    /// Exposed so a test names the same colour the model uses.
    static QColor deletedColour();
    static QColor spamColour();

    explicit ThreadListModel(QObject *parent = nullptr);

    /// Supplies the account chip colours. Not owned; must outlive the model.
    /// Without one, chips fall back to a colour generated from the tag name.
    void setTagColors(const TagColors *colours) { m_tagColors = colours; }

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
    const TagColors *m_tagColors = nullptr;
};
