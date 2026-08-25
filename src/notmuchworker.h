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

#include <QMap>
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

    /// Moves messages into `destFolder`, relative to the database path.
    ///
    /// A folder NAME rather than a "move to trash" call, because v2's Send
    /// needs exactly this operation for Drafts and Sent. Nothing
    /// trash-specific belongs here.
    ///
    /// The first mutation in this class that is not a notmuch tag: a rename on
    /// disk plus a reindex. Ordering is rename, index the new path, drop the
    /// old one. Indexing first is required, not stylistic: removing the last
    /// filename for a message id deletes the database entry and every tag on
    /// it, so removing before indexing loses the message's tags.
    void moveMessages(const QStringList &messageIds, const QString &destFolder);

    /// Destroys mail: removes each file from disk and each message from the
    /// index. **This is the only irreversible operation in the application**
    /// (item 118), which is why it is a separate entry point rather than a
    /// flag on moveMessages(): the two look alike and one of them can be
    /// undone.
    ///
    /// Named ids only, never a folder-wide sweep, so the blast radius is
    /// whatever the caller enumerated and confirmed. A message with several
    /// files loses every file it has, since leaving one behind would leave
    /// the message alive in a folder the user emptied.
    ///
    /// The caller is responsible for confirming: CLAUDE.md rules out
    /// confirmation dialogs for mutations because undo replaces them, and
    /// this is the one action where undo cannot exist.
    void purgeMessages(const QStringList &messageIds);

    /// Indexes one freshly written file, so it appears in a `path:` query
    /// without a full `notmuch new` (item 158).
    ///
    /// The draft-save path writes the file and stops, and the Drafts view is a
    /// path query, so an unindexed draft is invisible until the next sync. A
    /// draft rewrite writes a NEW file (MessageBuilder generates a fresh
    /// Message-ID on every build) and unlinks the old, so \p previousPath is
    /// removed after the new one is indexed, mirroring moveMessages()'s
    /// ordering: the old entry must not linger as a ghost draft.
    ///
    /// \p path is absolute, as DraftStore::write() returns it. The Maildir
    /// flags on the file (the "D" flag a draft carries) drive its tags exactly
    /// as they would on a later `notmuch new`.
    void indexDraftFile(const QString &path, const QString &previousPath = {});

    /// Removes one file from the index, without touching the file on disk.
    ///
    /// The counterpart to indexDraftFile() for the send path: a draft that was
    /// indexed while being composed is unlinked when it is sent, and its entry
    /// must not linger as a ghost draft until the next sync.
    void removeIndexedFile(const QString &path);

    /// Batch tagging over whole threads. The UI holds thread ids, not message
    /// ids, for rows it has not opened, so the resolution happens here where
    /// the database handle lives. This is the path the archive/flag/delete
    /// actions use on a multi-row selection.
    void applyTagsToThreads(const QStringList &threadIds,
                            const QStringList &add,
                            const QStringList &remove,
                            const QString &description);

    /// Resolves whole threads to the message ids and file paths they contain.
    ///
    /// Delete thread MOVES every message, and a move needs message ids, which
    /// the UI does not hold for a thread it never expanded. The resolution
    /// happens here for the same reason applyTagsToThreads() does it here:
    /// the database handle lives on this thread, and one combined query beats
    /// reopening the cursor per thread.
    ///
    /// Paths come back beside the ids because the destination is per ACCOUNT
    /// and the UI resolves an account from a message's path. Without them the
    /// caller would know which messages to move and not where any of them
    /// belongs.
    void resolveThreadMessages(const QStringList &threadIds,
                               const QString &requestTag);

    /// The same walk for a set of MESSAGE ids rather than thread ids.
    ///
    /// Restore needs each message's tags and path to decide where to send it,
    /// and must not read them from the model: the model's tags come from the
    /// query, so a row whose delete has not been re-queried still carries its
    /// pre-delete tags and the origin tag is missing. A restore that guesses
    /// the destination is worse than one that does nothing.
    void resolveMessages(const QStringList &messageIds,
                         const QString &requestTag);

    /// The same walk for an arbitrary QUERY, which is what Empty Trash needs:
    /// it has to enumerate what it is about to destroy before it can say how
    /// much that is, and the answer must not come from the model, which holds
    /// whatever the current view happens to be showing.
    void resolveQueryMessages(const QString &query, const QString &requestTag);

private:
    /// The shared walk behind resolveMessages() and resolveThreadMessages():
    /// runs `query` and emits threadMessagesResolved() with each match's id,
    /// database-relative path and tab-joined tags.
    void resolveQuery(const QString &query, const QString &requestTag);

public slots:

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

    /// The Maildir root, for whatever has to compose a path under it.
    ///
    /// This class owns the only database handle, and the root is a property of
    /// the DATABASE rather than of config: notmuch can split the index from
    /// the mail with `mail_root` and `path` as separate keys, so there is no
    /// config key the UI could read instead. Item 124 records what the wrong
    /// accessor costs. `notmuch_database_get_path()` returns the INDEX
    /// directory under that layout, and a destination composed from it writes
    /// into the Xapian tree.
    ///
    /// Requested at startup beside requestAllTags(), and answered once. The
    /// root does not change while the application runs.
    void requestMailRoot();

signals:
    void threadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void queryFinished(int totalThreads, quint64 generation);
    void threadLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void threadTreeLoaded(const QVector<MessageNode> &nodes,
                          quint64 generation);
    void messageLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void tagsApplied(const TagChange &change);

    /// Carries the ids that ACTUALLY moved, which may be fewer than requested.
    /// A stale id, a missing folder or a failed rename drops out here rather
    /// than aborting the batch.
    void messagesMoved(const QStringList &messageIds, const QString &destFolder);

    /// What a purge actually destroyed. Unlike a move there is no new path to
    /// observe afterwards, so this is the only report the UI has.
    void messagesPurged(const QStringList &messageIds);

    /// The same move, reported per message with the folder it came FROM.
    ///
    /// Emitted alongside messagesMoved rather than replacing it: that signal's
    /// shape is what test_notmuchworker asserts on, and a caller wanting only
    /// "did it move" should not have to unpack a map.
    ///
    /// The origin has to be reported from HERE because nowhere else knows it.
    /// A Maildir filename does not record the folder a message came from, and
    /// once the file has moved notmuch cannot answer either; the UI holds no
    /// path at all for a thread row it has not expanded. This is the one
    /// moment the old filename exists, so it is the only place the origin can
    /// be derived.
    ///
    /// Folders are relative to the database path and carry no `cur`/`new`
    /// segment, matching the `destFolder` moveMessages() takes, so a value
    /// from here can be passed straight back to move a message home.
    void messagesMovedFrom(const QMap<QString, QString> &originByMessageId,
                           const QString &destFolder);
    /// The answer to resolveThreadMessages(), as parallel lists: `messageIds`
    /// and the database-relative `paths` of the same messages, in the same
    /// order. `requestTag` is echoed back so a caller can tell which request
    /// this answers.
    /// `tags` carries each message's tags joined by a space, in the same
    /// order. Needed because Restore reads a message's `deleted-from:` tag to
    /// decide where to send it, and an unexpanded thread's messages have no
    /// node in the model to read tags from.
    void threadMessagesResolved(const QStringList &messageIds,
                                const QStringList &paths,
                                const QStringList &tags,
                                const QString &requestTag);
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

    /// The Maildir root, absolute. No generation: it is a property of the
    /// database rather than of any query, so a late answer is still the right
    /// one. Empty when the database could not be opened, which a consumer must
    /// treat as "cannot compose a path yet" rather than as the root being "".
    void mailRootReady(const QString &mailRoot);

    void errorOccurred(const QString &message);

private:
    bool openReadOnly();
    void close();

    QByteArray configPathArg() const;

    QString m_configPath;
    notmuch_database_t *m_db = nullptr;
};
