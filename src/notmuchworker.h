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

#include <QObject>
#include <QStringList>
#include <QVector>

#include "types.h"

struct _notmuch_database;
typedef struct _notmuch_database notmuch_database_t;

/// Owns the only notmuch database handle in the process.
///
/// libnotmuch is not thread-safe and queries over a large database block, so
/// this object lives on its own thread and the UI reaches it only through
/// queued signals. No notmuch pointer ever leaves this class.
class NotmuchWorker : public QObject
{
    Q_OBJECT
public:
    /// notmuchConfigPath may be empty, in which case notmuch resolves its own
    /// config and therefore its own database.path.
    explicit NotmuchWorker(const QString &notmuchConfigPath, QObject *parent = nullptr);
    ~NotmuchWorker() override;

    /// Threads emitted per threadsReady() signal.
    static constexpr int kBatchSize = 200;

public slots:
    /// Runs a query. generation lets the UI discard results from a superseded
    /// query without the worker needing to know about cancellation.
    void runQuery(const QString &query, quint64 generation);

    /// Loads the messages of one thread, oldest first. matchQuery is the
    /// user's current query; messages matching it render expanded, the rest
    /// as stubs.
    void loadThread(const QString &threadId, const QString &matchQuery,
                    quint64 generation);

    /// Applies tag changes. Opens the database read-write, applies, and closes
    /// immediately: notmuch's write lock is exclusive process-wide, so holding
    /// it would block the user's cron `notmuch new`.
    void applyTags(const TagChange &change);

    /// Batch tagging over whole threads. The UI holds thread ids, not message
    /// ids, for rows it has not opened, so the resolution happens here where
    /// the database handle lives. This is the path the archive/flag/delete
    /// actions use on a multi-row selection.
    void applyTagsToThreads(const QStringList &threadIds,
                            const QStringList &add,
                            const QStringList &remove,
                            const QString &description);

    /// Every tag in the database, sorted. Feeds query bar completion, which
    /// cannot offer tag names it has no way to enumerate. Called at startup,
    /// after a sync, and after a tag mutation introduces an unknown tag.
    void requestAllTags(quint64 generation);

    /// Thread counts for the placeholder pane's helper lines, one per query,
    /// answered in the order asked. Counts rather than results: the pane says
    /// how much there is, and clicking a line runs the query properly.
    ///
    /// Requested when the pane is about to go blank rather than kept fresh in
    /// the background. A count goes stale the moment a tag is edited, and
    /// refreshing one nobody is looking at is work for nothing.
    void requestCounts(const QStringList &queries, quint64 generation);

signals:
    void threadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void queryFinished(int totalThreads, quint64 generation);
    void threadLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void tagsApplied(const TagChange &change);
    void allTagsReady(const QStringList &tags, quint64 generation);

    /// One entry per requested query, in the order they were asked for. A query
    /// notmuch rejects yields -1 rather than dropping the entry, so the
    /// positional correspondence the caller relies on always holds.
    void countsReady(const QVector<int> &counts, quint64 generation);

    void errorOccurred(const QString &message);

private:
    bool openReadOnly();
    void close();

    QByteArray configPathArg() const;

    QString m_configPath;
    notmuch_database_t *m_db = nullptr;
};
