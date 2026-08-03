#include "threadlistmodel.h"

#include <QFont>

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

    if (role == Qt::FontRole && thread.isUnread()) {
        QFont font;
        font.setBold(true);
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
