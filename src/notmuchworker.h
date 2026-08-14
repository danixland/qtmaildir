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

    /// The sort orders offered to the user.
    ///
    /// Two, not four. notmuch also has NOTMUCH_SORT_MESSAGE_ID and
    /// NOTMUCH_SORT_UNSORTED, and neither is an order a human wants. Sorting
    /// by sender or subject is deliberately absent: notmuch cannot do it, so
    /// the model would have to sort after results arrive, which fights the
    /// batching that makes a 10k-thread query paint immediately.
    enum SortOrder {
        NewestFirst,
        OldestFirst,
    };
    Q_ENUM(SortOrder)

public slots:
    /// Runs a query. generation lets the UI discard results from a superseded
    /// query without the worker needing to know about cancellation.
    /// `withRecipients` fills ThreadSummary::recipients by reading each
    /// thread's To headers. OFF by default and deliberately opt-in: To is not
    /// in notmuch's index, so this reads message FILES, at roughly 8.7 ms per
    /// thread. Only a Sent query asks for it; turning it on for an inbox query
    /// costs tens of seconds and changes nothing a user can see.
    void runQuery(const QString &query, quint64 generation,
                  SortOrder sort = NewestFirst, bool withRecipients = false);

    /// Loads the messages of one thread, oldest first. matchQuery is the
    /// user's current query; messages matching it render expanded, the rest
    /// as stubs.
    /// **No UI caller since item 66, and that is deliberate.** This was how a
    /// thread root rendered the whole conversation, stubs plus the last few
    /// messages expanded. The user asked for that view to go: selecting any
    /// row, root or reply, now renders exactly one message via loadMessage,
    /// and `ThreadSummary::firstMessageId` is what makes the root's own
    /// message known without expanding the thread first.
    ///
    /// Kept as a worker capability rather than deleted. It is a tested way to
    /// read every message of a thread with the match set resolved, which the
    /// worker's own tests use as a helper and a future feature may want. If
    /// you are adding a caller, be sure you are not rebuilding the
    /// conversation pane that was removed on purpose.
    ///
    /// `matchedOnly` drops the messages that did not match `matchQuery` rather
    /// than rendering them as stubs. For the Sent view, where the thread is not
    /// the unit the user is reading: a sent message pulls in the replies it
    /// received, and a pane claiming to show what they sent then shows a
    /// conversation. Ignored when `matchQuery` is empty, since nothing was
    /// filtered and every message counts as matched.
    void loadThread(const QString &threadId, const QString &matchQuery,
                    quint64 generation, bool matchedOnly = false);

    /// Loads a thread as a reply TREE, for the message rows in the list.
    ///
    /// Separate from loadThread rather than replacing it, for a reason that is
    /// not stylistic: loadThread walks notmuch_query_search_messages, and a
    /// message obtained that way returns NULL from
    /// notmuch_message_get_replies (notmuch.h:1617-1628), so that walk cannot
    /// produce reply depth at all. The tree has to come from
    /// notmuch_thread_get_toplevel_messages instead. The message pane still
    /// wants the flat list; only the list wants the tree.
    ///
    /// matchQuery is accepted for signature symmetry with loadThread and is
    /// deliberately unused: see the comment on the walk in the .cpp.
    void loadThreadTree(const QString &threadId, const QString &matchQuery,
                        quint64 generation);

    /// Loads ONE message, for a message row selected in the list.
    ///
    /// Emits messageLoaded with an empty vector when the id is unknown, which
    /// is an ordinary race after a reindex rather than an error worth
    /// reporting.
    void loadMessage(const QString &messageId, quint64 generation);

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

    /// Message counts for each query, positionally paired with the input.
    ///
    /// Beside requestCounts rather than replacing it: that one counts THREADS,
    /// which is right for the placeholder pane because a click there produces
    /// thread rows. A tagging rule tags messages, so a thread count would
    /// understate any rule matching part of a large thread.
    void requestMessageCounts(const QStringList &queries, quint64 generation);

    /// Database-level facts for the Maildir overview (item 34): total messages,
    /// total threads, and the number of tags.
    ///
    /// **Messages, not threads**, which is what distinguishes this from
    /// requestCounts above. That one answers "how many rows will this query
    /// produce" and counts threads to match the list; this one describes the
    /// database, where the message total is the number a user means by "how
    /// much mail is in here".
    ///
    /// Counting every message is not free on a large database, so this is
    /// called when the dialog is opened and never on a timer.
    void requestDatabaseStats(quint64 generation);

    /// Every Maildir folder under the database root, as paths relative to it.
    ///
    /// From the DISK, not from the index: a folder mbsync created and nothing
    /// has landed in yet is still a folder a tagging rule may target, and one
    /// derived from indexed message paths would not offer it.
    ///
    /// Here rather than in MainWindow because the database root is
    /// notmuch's `database.path` and this class owns the only handle that can
    /// answer for it. Duplicating the path into config is exactly the second
    /// source of truth the design refuses.
    void requestFolders();

signals:
    void threadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void queryFinished(int totalThreads, quint64 generation);
    void threadLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void threadTreeLoaded(const QVector<MessageNode> &nodes,
                          quint64 generation);
    void messageLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void tagsApplied(const TagChange &change);
    void allTagsReady(const QStringList &tags, quint64 generation);

    /// One entry per requested query, in the order they were asked for. A query
    /// notmuch rejects yields -1 rather than dropping the entry, so the
    /// positional correspondence the caller relies on always holds.
    void countsReady(const QVector<int> &counts, quint64 generation);

    /// The same positional contract as countsReady, over messages rather than
    /// threads. A dry run over tagging rules pairs these with its own rules by
    /// index, so an entry is never dropped.
    void messageCountsReady(const QVector<int> &counts, quint64 generation);

    /// Fields left at -1 are ones notmuch could not answer, which the dialog
    /// renders as unknown rather than as zero.
    void databaseStatsReady(const DatabaseStats &stats, quint64 generation);

    /// Maildir folders relative to the database root, sorted. No generation:
    /// the tree on disk does not change under a query, and the one consumer
    /// asks once when its dialog opens.
    void foldersReady(const QStringList &folders);

    void errorOccurred(const QString &message);

private:
    bool openReadOnly();
    void close();

    QByteArray configPathArg() const;

    QString m_configPath;
    notmuch_database_t *m_db = nullptr;
};
