#pragma once

#include <QAbstractTableModel>
#include <QVector>

#include "types.h"

/// Table model over query results, filled in batches so a large query paints
/// its first screenful immediately.
class ThreadListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        DateColumn = 0,
        AuthorsColumn,
        SubjectColumn,
        TagsColumn,
        ColumnCount,
    };

    enum Role {
        /// The thread id behind a row. Views hand out QModelIndexes, but the
        /// worker speaks thread ids, so the mapping belongs on the model
        /// rather than in every caller.
        ThreadIdRole = Qt::UserRole + 1,
    };

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
