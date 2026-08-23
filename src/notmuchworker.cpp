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

#include "notmuchworker.h"

#include <notmuch.h>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

#include <cstdlib>

#include "maildirname.h"
#include "mimeparser.h"
#include "nmraii.h"

namespace {

/// Where the MAIL lives, which is not always where the index lives.
///
/// Item 124. notmuch can be configured with `mail_root` and `path` as separate
/// keys, which is how the Xapian index moves to faster storage while the
/// Maildir stays put. Under that layout `notmuch_database_get_path()` returns
/// the INDEX directory, so every path composed from it lands in the wrong tree:
/// message paths resolve to `../..` escapes that match no account, and a move
/// writes into the index instead of the Maildir, where mbsync never sees it.
///
/// `NOTMUCH_CONFIG_MAIL_ROOT` is correct under BOTH layouts. With only `path`
/// set it returns that same directory, so this is not a special case for split
/// configurations but the right question to ask in every one. Measured against
/// notmuch 0.39: legacy config, `get_path()` and `MAIL_ROOT` agree; split
/// config, only `MAIL_ROOT` names the Maildir.
///
/// The string is owned by notmuch and must not be freed (notmuch.h:2585).
QString mailRootOf(notmuch_database_t *db)
{
    const char *root = notmuch_config_get(db, NOTMUCH_CONFIG_MAIL_ROOT);
    return root ? QString::fromUtf8(root) : QString();
}

QStringList tagsOf(notmuch_message_t *message)
{
    QStringList result;
    NmTags tags(notmuch_message_get_tags(message));
    for (; notmuch_tags_valid(tags.get()); notmuch_tags_move_to_next(tags.get()))
        result.append(QString::fromUtf8(notmuch_tags_get(tags.get())));
    return result;
}

QStringList tagsOf(notmuch_thread_t *thread)
{
    QStringList result;
    NmTags tags(notmuch_thread_get_tags(thread));
    for (; notmuch_tags_valid(tags.get()); notmuch_tags_move_to_next(tags.get()))
        result.append(QString::fromUtf8(notmuch_tags_get(tags.get())));
    return result;
}

/// Who a thread's messages were addressed to, summarised for one line.
///
/// EXPENSIVE, and only called when a query asks. "To" is not served from
/// notmuch's index, so every call here reads message FILES: 8.7 ms per thread
/// measured against a real database, which is 38 seconds over a 4411-thread
/// inbox. See ThreadSummary::recipients.
///
/// The messages come from the THREAD and are owned by it, freed when it is
/// freed (notmuch.h:1637). They are therefore held raw and never wrapped in
/// NmMessage, which would call notmuch_message_destroy on memory the thread
/// frees again, and the walk finishes before the caller drops the thread. This
/// is the same rule walkReplies follows, and getting it wrong is a double-free
/// rather than a leak.
QString recipientsOf(notmuch_thread_t *thread)
{
    // The first message with a usable To wins. A thread is one conversation,
    // and the alternative, folding every message's recipients together, is the
    // participants-list problem item 2 rejected: it produces a union that
    // misdescribes itself the moment a thread has replies going both ways.
    notmuch_messages_t *messages = notmuch_thread_get_messages(thread);
    for (; notmuch_messages_valid(messages);
         notmuch_messages_move_to_next(messages)) {
        notmuch_message_t *message = notmuch_messages_get(messages);
        if (!message)
            continue;

        // Returns "" for a missing header and NULL on error, and the two mean
        // different things only to notmuch: both are "nothing to show" here.
        const char *to = notmuch_message_get_header(message, "To");
        if (!to || !*to)
            continue;

        const QString summary = recipientSummary(QString::fromUtf8(to));
        if (!summary.isEmpty())
            return summary;
    }
    return QString();
}

/// Collects the message ids a query matches. Returns false if the query could
/// not be run at all, which is different from a query that matched nothing.
bool collectMessageIds(notmuch_database_t *db, const QString &query,
                       QStringList *ids)
{
    NmQuery nmQuery(notmuch_query_create(db, query.toUtf8().constData()));
    if (!nmQuery)
        return false;

    notmuch_messages_t *raw = nullptr;
    if (notmuch_query_search_messages(nmQuery.get(), &raw) != NOTMUCH_STATUS_SUCCESS)
        return false;

    NmMessages messages(raw);
    for (; notmuch_messages_valid(messages.get());
           notmuch_messages_move_to_next(messages.get())) {
        NmMessage message(notmuch_messages_get(messages.get()));
        if (message)
            ids->append(QString::fromUtf8(notmuch_message_get_message_id(message.get())));
    }
    return true;
}

/// Walks a thread's reply structure depth-first, appending each message with
/// its depth.
///
/// Takes RAW notmuch_message_t*, deliberately, against the rule that every
/// handle in this file is RAII-owned. Messages reached through a thread belong
/// to that thread and are freed with it (notmuch.h:1637), so wrapping one in
/// NmMessage would call notmuch_message_destroy on memory the thread frees
/// again. The NmThread in the caller is what keeps every pointer here alive,
/// and this must not outlive it.
///
/// No match-set argument, unlike loadThread. A row is drawn for every message
/// in the thread regardless of the query: the list is where the user goes to
/// SEE the thread's shape, and hiding replies that did not match would make the
/// reply count disagree with the rows beneath it.
void walkReplies(notmuch_messages_t *messages, int depth,
                 QVector<MessageNode> *out)
{
    for (; notmuch_messages_valid(messages);
           notmuch_messages_move_to_next(messages)) {

        notmuch_message_t *message = notmuch_messages_get(messages);
        if (!message)
            continue;

        MessageNode node;
        node.messageId =
            QString::fromUtf8(notmuch_message_get_message_id(message));
        node.threadId =
            QString::fromUtf8(notmuch_message_get_thread_id(message));
        node.filePath =
            QString::fromUtf8(notmuch_message_get_filename(message));
        node.from =
            QString::fromUtf8(notmuch_message_get_header(message, "from"));
        node.subject =
            QString::fromUtf8(notmuch_message_get_header(message, "subject"));
        node.date =
            QDateTime::fromSecsSinceEpoch(notmuch_message_get_date(message));
        node.tags = tagsOf(message);
        node.depth = depth;
        out->append(node);

        // NULL is a legitimate "no replies" here: notmuch_messages_valid
        // accepts it and returns FALSE (notmuch.h:1630), so a leaf needs no
        // guard of its own.
        walkReplies(notmuch_message_get_replies(message), depth + 1, out);
    }
}

/// The Maildir FOLDER a message file sits in, relative to the database root.
///
/// `<root>/acct/inbox/cur/12345` becomes `acct/inbox`: the `cur`/`new` segment
/// is stripped because it is Maildir's read-state bookkeeping rather than part
/// of the folder's name, and moveMessages() takes a folder without one. That
/// makes the value round-trip: what comes out here can be handed straight back
/// to move a message home.
///
/// Empty when the file is not under the root at all, which the caller treats as
/// "origin unknown" rather than guessing. A wrong folder here would send a
/// restored message somewhere the user never had it.
QString folderOfMessageFile(const QString &root, const QString &filePath)
{
    const QString rootPath = QDir(root).absolutePath();
    const QString dir = QFileInfo(filePath).absolutePath();

    const QString relative = QDir(rootPath).relativeFilePath(dir);
    // relativeFilePath happily walks upwards, so a path outside the root comes
    // back as `../something` rather than as a failure.
    if (relative.isEmpty() || relative == QStringLiteral(".")
        || relative.startsWith(QStringLiteral("../"))) {
        return QString();
    }

    QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!parts.isEmpty()
        && (parts.last() == QStringLiteral("cur")
            || parts.last() == QStringLiteral("new"))) {
        parts.removeLast();
    }
    return parts.join(QLatin1Char('/'));
}

} // namespace

/// Registers SortOrder for queued calls, once, before main() runs.
///
/// Q_ENUM alone is NOT enough for a queued Q_ARG: it gives the enum a
/// meta-object entry, not a metatype registered under the name invokeMethod
/// resolves, so MainWindow's queued runQuery would drop its sort argument at
/// runtime with a warning and every query would silently run newest-first.
///
/// Here rather than in MainWindow's constructor, because the registration
/// belongs to the type rather than to one consumer: a caller that never
/// constructs a MainWindow (a test, or a future headless mode) needs it too,
/// and that is exactly how the first attempt at this passed by accident and
/// failed under test.
static const int kSortOrderMetaType =
    qRegisterMetaType<NotmuchWorker::SortOrder>("NotmuchWorker::SortOrder");

NotmuchWorker::NotmuchWorker(const QString &notmuchConfigPath, QObject *parent)
    : QObject(parent), m_configPath(notmuchConfigPath)
{
    Q_UNUSED(kSortOrderMetaType);
}

NotmuchWorker::~NotmuchWorker()
{
    close();
}

/// An empty config path means "let notmuch resolve its own config", which
/// libnotmuch spells as NULL. The QByteArray is returned by value so callers
/// keep it alive for as long as they use constData().
QByteArray NotmuchWorker::configPathArg() const
{
    return m_configPath.isEmpty() ? QByteArray() : m_configPath.toLocal8Bit();
}

bool NotmuchWorker::openReadOnly()
{
    if (m_db)
        return true;

    const QByteArray configPath = configPathArg();
    char *error = nullptr;
    const notmuch_status_t status = notmuch_database_open_with_config(
        nullptr,                                        // let config decide path
        NOTMUCH_DATABASE_MODE_READ_ONLY,
        configPath.isEmpty() ? nullptr : configPath.constData(),
        nullptr,
        &m_db,
        &error);

    if (status != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Cannot open notmuch database: %1")
            .arg(QString::fromUtf8(error ? error : notmuch_status_to_string(status))));
        free(error);
        m_db = nullptr;
        return false;
    }
    return true;
}

void NotmuchWorker::close()
{
    if (m_db) {
        notmuch_database_destroy(m_db);
        m_db = nullptr;
    }
}

void NotmuchWorker::runQuery(const QString &query, quint64 generation,
                             SortOrder sort, bool withRecipients)
{
    if (!openReadOnly())
        return;

    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(QStringLiteral("Invalid query: %1").arg(query));
        return;
    }
    notmuch_query_set_sort(nmQuery.get(),
                           sort == OldestFirst ? NOTMUCH_SORT_OLDEST_FIRST
                                               : NOTMUCH_SORT_NEWEST_FIRST);

    notmuch_threads_t *rawThreads = nullptr;
    const notmuch_status_t status =
        notmuch_query_search_threads(nmQuery.get(), &rawThreads);
    if (status != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Query failed: %1")
            .arg(QString::fromUtf8(notmuch_status_to_string(status))));
        return;
    }
    NmThreads threads(rawThreads);

    // Message paths are reported RELATIVE to this. An absolute path would be
    // useless to the UI, which knows accounts only by their maildir, a
    // database-relative prefix: comparing the two never matched and left every
    // row resolving to no account at all.
    const QString dbRoot =
        QDir(mailRootOf(m_db)).absolutePath();

    QVector<ThreadSummary> batch;
    batch.reserve(kBatchSize);
    int total = 0;

    for (; notmuch_threads_valid(threads.get());
           notmuch_threads_move_to_next(threads.get())) {

        NmThread thread(notmuch_threads_get(threads.get()));
        if (!thread)
            continue;

        ThreadSummary summary;
        summary.threadId = QString::fromUtf8(notmuch_thread_get_thread_id(thread.get()));
        summary.subject = QString::fromUtf8(notmuch_thread_get_subject(thread.get()));
        summary.authors = QString::fromUtf8(notmuch_thread_get_authors(thread.get()));
        summary.date = QDateTime::fromSecsSinceEpoch(
            notmuch_thread_get_newest_date(thread.get()));
        summary.totalCount = notmuch_thread_get_total_messages(thread.get());
        summary.matchedCount = notmuch_thread_get_matched_messages(thread.get());
        summary.tags = tagsOf(thread.get());
        if (withRecipients)
            summary.recipients = recipientsOf(thread.get());

        // The message the row's card stands for. Raw pointers on purpose:
        // messages reached through a thread are owned by the THREAD and freed
        // with it (notmuch.h:1637), so an NmMessage wrapper here would destroy
        // memory the thread frees again. Everything must be read while
        // `thread` is alive, which it is for the rest of this iteration.
        //
        // Index-only, so it costs nothing measurable: see
        // ThreadSummary::firstMessageId.
        //
        // Two different questions, and the Sent view asks the second one. A
        // normal row stands for the thread's OPENING message. A Sent row
        // stands for what the USER sent, usually a reply and often not the
        // opening message at all, so it takes the first message the query
        // MATCHED. withRecipients is exactly the Sent query, which is why it
        // selects between them rather than carrying a second flag that could
        // disagree with it.
        //
        // Note notmuch_thread_get_matched_messages returns a COUNT, not an
        // iterator; there is no matched-messages list. The match state is a
        // per-message flag, so the Sent branch walks in oldest-first order and
        // stops at the first match. Measured at 0.146s against a 0.143s
        // baseline over 4,515 threads: the walk stops early and reads the
        // index, so it is as free as the toplevel call.
        if (withRecipients) {
            notmuch_messages_t *all = notmuch_thread_get_messages(thread.get());
            for (; all && notmuch_messages_valid(all);
                   notmuch_messages_move_to_next(all)) {
                notmuch_message_t *message = notmuch_messages_get(all);
                if (!message)
                    continue;
                notmuch_bool_t matched = FALSE;
                notmuch_message_get_flag_st(message,
                                            NOTMUCH_MESSAGE_FLAG_MATCH,
                                            &matched);
                if (matched) {
                    summary.firstMessageId = QString::fromUtf8(
                        notmuch_message_get_message_id(message));
                    // The card's own tags, beside the thread's union above.
                    // Same walk, same index read, no extra query.
                    summary.firstMessageTags = tagsOf(message);
                    // Which account this belongs to, for Delete's destination.
                    summary.firstMessagePath = QDir(dbRoot).relativeFilePath(
                        QString::fromUtf8(
                            notmuch_message_get_filename(message)));
                    break;
                }
            }
        } else if (notmuch_messages_t *top =
                       notmuch_thread_get_toplevel_messages(thread.get())) {
            if (notmuch_messages_valid(top)) {
                if (notmuch_message_t *first = notmuch_messages_get(top)) {
                    summary.firstMessageId = QString::fromUtf8(
                        notmuch_message_get_message_id(first));
                    // The card's own tags, beside the thread's union above.
                    // Same walk, same index read, no extra query.
                    summary.firstMessageTags = tagsOf(first);
                    // Which account this belongs to, for Delete's destination.
                    summary.firstMessagePath = QDir(dbRoot).relativeFilePath(
                        QString::fromUtf8(
                            notmuch_message_get_filename(first)));
                }
            }
        }

        batch.append(summary);
        ++total;

        if (batch.size() >= kBatchSize) {
            emit threadsReady(batch, generation);
            batch.clear();
            batch.reserve(kBatchSize);
        }
    }

    if (!batch.isEmpty())
        emit threadsReady(batch, generation);

    emit queryFinished(total, generation);
}

void NotmuchWorker::loadThread(const QString &threadId,
                               const QString &matchQuery,
                               quint64 generation, bool matchedOnly)
{
    if (!openReadOnly())
        return;

    // Which messages of the thread matched the user's query. Running the query
    // intersected with the thread is cheaper than testing each message.
    //
    // haveMatchSet distinguishes "no query was given, so everything counts as
    // matched" from "a query was given and matched nothing in this thread".
    // Collapsing those would render a whole thread expanded precisely when the
    // user filtered it down to nothing.
    QSet<QString> matchedIds;
    bool haveMatchSet = false;
    if (!matchQuery.trimmed().isEmpty()) {
        const QString intersect =
            QStringLiteral("thread:%1 and (%2)").arg(threadId, matchQuery);
        QStringList ids;
        if (collectMessageIds(m_db, intersect, &ids)) {
            matchedIds = QSet<QString>(ids.begin(), ids.end());
            haveMatchSet = true;
        }
    }

    const QString query = QStringLiteral("thread:%1").arg(threadId);
    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(QStringLiteral("Cannot load thread %1").arg(threadId));
        return;
    }
    notmuch_query_set_sort(nmQuery.get(), NOTMUCH_SORT_OLDEST_FIRST);

    notmuch_messages_t *rawMessages = nullptr;
    if (notmuch_query_search_messages(nmQuery.get(), &rawMessages)
            != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Cannot search thread %1").arg(threadId));
        return;
    }
    NmMessages messages(rawMessages);

    QVector<MessageRef> result;
    for (; notmuch_messages_valid(messages.get());
           notmuch_messages_move_to_next(messages.get())) {

        NmMessage message(notmuch_messages_get(messages.get()));
        if (!message)
            continue;

        MessageRef ref;
        ref.messageId = QString::fromUtf8(notmuch_message_get_message_id(message.get()));
        ref.filePath = QString::fromUtf8(notmuch_message_get_filename(message.get()));
        ref.tags = tagsOf(message.get());
        ref.matched = !haveMatchSet || matchedIds.contains(ref.messageId);

        // Dropped rather than rendered as a stub.
        //
        // haveMatchSet is redundant here and kept deliberately: ref.matched is
        // already true for every message when no query was given, so the two
        // conditions cannot disagree today. It states the invariant this
        // depends on at the point that depends on it, so a later change to how
        // ref.matched is computed cannot silently empty the pane.
        if (matchedOnly && haveMatchSet && !ref.matched)
            continue;

        result.append(ref);
    }

    emit threadLoaded(result, generation);
}

void NotmuchWorker::loadThreadTree(const QString &threadId,
                                   const QString &matchQuery,
                                   quint64 generation)
{
    // Accepted for signature symmetry with loadThread, and unused on purpose:
    // see walkReplies on why every message in the thread gets a row.
    Q_UNUSED(matchQuery);

    if (!openReadOnly())
        return;

    const QString query = QStringLiteral("thread:%1").arg(threadId);
    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(
            QStringLiteral("Cannot load thread %1").arg(threadId));
        return;
    }

    // search_threads, not search_messages. The messages have to come from a
    // notmuch_thread_t or notmuch_message_get_replies returns NULL for every
    // one of them and the walk below produces a flat list at depth 0.
    notmuch_threads_t *rawThreads = nullptr;
    if (notmuch_query_search_threads(nmQuery.get(), &rawThreads)
            != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(
            QStringLiteral("Cannot search thread %1").arg(threadId));
        return;
    }
    NmThreads threads(rawThreads);

    QVector<MessageNode> nodes;
    if (notmuch_threads_valid(threads.get())) {
        // Held for the whole walk: every message pointer inside belongs to this
        // thread and dies with it.
        NmThread thread(notmuch_threads_get(threads.get()));
        if (thread) {
            walkReplies(notmuch_thread_get_toplevel_messages(thread.get()), 0,
                        &nodes);
        }
    }

    emit threadTreeLoaded(nodes, generation);
}

void NotmuchWorker::loadMessage(const QString &messageId, quint64 generation)
{
    // Every failure below emits an EMPTY result as well as its error, and that
    // is a contract rather than tidiness. The bottom of this function already
    // said so ("emitted even when empty, so the UI's handler runs"), but the
    // three failure paths returned silently and broke it. A caller that arms
    // state on this request and disarms it on the reply then waits for ever:
    // MainWindow's compose path did exactly that, and a request left armed
    // hijacks a later pane load for the same message.
    if (!openReadOnly()) {
        emit messageLoaded({}, generation);
        return;
    }

    // id: is an exact-match prefix, and the id is quoted because a message id
    // can legitimately contain characters notmuch's parser would otherwise read
    // as query syntax.
    const QString query = QStringLiteral("id:\"%1\"").arg(messageId);
    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(
            QStringLiteral("Cannot load message %1").arg(messageId));
        emit messageLoaded({}, generation);
        return;
    }

    notmuch_messages_t *rawMessages = nullptr;
    if (notmuch_query_search_messages(nmQuery.get(), &rawMessages)
            != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(
            QStringLiteral("Cannot search message %1").arg(messageId));
        emit messageLoaded({}, generation);
        return;
    }
    NmMessages messages(rawMessages);

    QVector<MessageRef> result;
    if (notmuch_messages_valid(messages.get())) {
        NmMessage message(notmuch_messages_get(messages.get()));
        if (message) {
            MessageRef ref;
            ref.messageId = QString::fromUtf8(
                notmuch_message_get_message_id(message.get()));
            ref.filePath = QString::fromUtf8(
                notmuch_message_get_filename(message.get()));
            ref.tags = tagsOf(message.get());

            // Always matched: the user asked for this message by clicking its
            // row, so rendering it as a stub would answer the wrong question.
            ref.matched = true;
            result.append(ref);
        }
    }

    // Emitted even when empty, so the UI's handler runs and can decide what to
    // do rather than waiting for a reply that never comes.
    emit messageLoaded(result, generation);
}

void NotmuchWorker::applyTagsToThreads(const QStringList &threadIds,
                                       const QStringList &add,
                                       const QStringList &remove,
                                       const QString &description)
{
    if (threadIds.isEmpty())
        return;

    if (!openReadOnly())
        return;

    // Resolve every thread to its message ids in ONE query. Issuing a query per
    // thread would reopen the same Xapian cursor hundreds of times on a large
    // selection.
    QStringList terms;
    terms.reserve(threadIds.size());
    for (const QString &id : threadIds)
        terms.append(QStringLiteral("thread:%1").arg(id));

    QStringList messageIds;
    if (!collectMessageIds(m_db, terms.join(QStringLiteral(" or ")), &messageIds)) {
        emit errorOccurred(QStringLiteral("Cannot resolve selected threads"));
        return;
    }

    if (messageIds.isEmpty()) {
        emit errorOccurred(QStringLiteral("Selected threads contain no messages"));
        return;
    }

    applyTags(TagChange{ messageIds, add, remove, description });
}

void NotmuchWorker::applyTags(const TagChange &change)
{
    if (change.messageIds.isEmpty())
        return;

    // The read-only handle must be closed first: notmuch allows only one open
    // handle per process.
    close();

    const QByteArray configPath = configPathArg();
    notmuch_database_t *db = nullptr;
    char *error = nullptr;
    const notmuch_status_t status = notmuch_database_open_with_config(
        nullptr,
        NOTMUCH_DATABASE_MODE_READ_WRITE,
        configPath.isEmpty() ? nullptr : configPath.constData(),
        nullptr,
        &db,
        &error);

    if (status != NOTMUCH_STATUS_SUCCESS) {
        // NOT reached by lock contention, despite the wording. Measured
        // 2026-08-04: this call BLOCKS on a held write lock and then returns
        // SUCCESS (9.158s against a 12s hold), so a running sync never lands
        // here. What does land here is a genuinely broken open: bad
        // permissions, a corrupt index, a missing database. None of those are
        // helped by waiting, so the UI reverts rather than retrying.
        //
        // The stall a running sync DOES cause is avoided upstream, in
        // MainWindow, by not sending the write at all while the lock is held.
        emit errorOccurred(
            QStringLiteral("Cannot open database for writing: %1")
                .arg(QString::fromUtf8(error ? error
                                             : notmuch_status_to_string(status))));
        free(error);
        return;
    }

    for (const QString &id : change.messageIds) {
        notmuch_message_t *raw = nullptr;
        // find_message reports SUCCESS with a null message when the id is not
        // in the database, so both have to be checked. A stale id must not
        // abort the batch: the live ids alongside it still need tagging.
        if (notmuch_database_find_message(db, id.toUtf8().constData(), &raw)
                != NOTMUCH_STATUS_SUCCESS || !raw) {
            continue;
        }
        NmMessage message(raw);

        notmuch_message_freeze(message.get());
        for (const QString &tag : change.removed)
            notmuch_message_remove_tag(message.get(), tag.toUtf8().constData());
        for (const QString &tag : change.added)
            notmuch_message_add_tag(message.get(), tag.toUtf8().constData());
        notmuch_message_thaw(message.get());

        // Renames the file on disk when the seen/flagged tags changed, keeping
        // the Maildir and the index in agreement for the next `notmuch new`.
        notmuch_message_tags_to_maildir_flags(message.get());
    }

    notmuch_database_close(db);
    notmuch_database_destroy(db);

    emit tagsApplied(change);
}

void NotmuchWorker::moveMessages(const QStringList &messageIds,
                                 const QString &destFolder)
{
    if (messageIds.isEmpty() || destFolder.isEmpty())
        return;

    // The read-only handle must be closed first: notmuch allows only one open
    // handle per process. Same ordering as applyTags, for the same reason.
    close();

    const QByteArray configPath = configPathArg();
    notmuch_database_t *db = nullptr;
    char *error = nullptr;
    const notmuch_status_t status = notmuch_database_open_with_config(
        nullptr,
        NOTMUCH_DATABASE_MODE_READ_WRITE,
        configPath.isEmpty() ? nullptr : configPath.constData(),
        nullptr,
        &db,
        &error);

    if (status != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(
            QStringLiteral("Cannot open database for writing: %1")
                .arg(QString::fromUtf8(error ? error
                                             : notmuch_status_to_string(status))));
        free(error);
        return;
    }

    const QString root = mailRootOf(db);
    const QString destDir =
        root + QLatin1Char('/') + destFolder + QStringLiteral("/cur");

    QStringList moved;
    QMap<QString, QString> origins;
    for (const QString &id : messageIds) {
        notmuch_message_t *raw = nullptr;
        // find_message reports SUCCESS with a null message when the id is not
        // in the database, so both have to be checked. A stale id must not
        // abort the batch: the live ids alongside it still need moving.
        if (notmuch_database_find_message(db, id.toUtf8().constData(), &raw)
                != NOTMUCH_STATUS_SUCCESS || !raw) {
            continue;
        }
        NmMessage message(raw);

        const char *rawName = notmuch_message_get_filename(message.get());
        if (!rawName)
            continue;
        const QString from = QString::fromUtf8(rawName);
        // The handle is released before the file moves under it.
        message.reset();

        // Where it is coming FROM, captured here because this is the only
        // moment the old filename exists. See messagesMovedFrom().
        const QString origin = folderOfMessageFile(root, from);

        // cur/, never new/. A file dropped in new/ is re-announced as fresh
        // mail by every reader of the Maildir.
        if (!QDir().mkpath(destDir)) {
            emit errorOccurred(QStringLiteral("Cannot create folder %1")
                                   .arg(destDir));
            continue;
        }

        // Already where it was asked to go, compared on the DIRECTORY rather
        // than on the full path. It used to compare paths, which worked only
        // because the filename was carried across unchanged; with a fresh name
        // that test can never be true, so a message already in the destination
        // would be renamed on every move for no reason, and every rename is a
        // new filename mbsync has to reconcile.
        if (QFileInfo(from).absolutePath() == QFileInfo(destDir).absoluteFilePath()) {
            moved.append(id);
            origins.insert(id, origin);
            continue;
        }

        // A FRESH name, never the old one. See MaildirName::fresh(): carrying
        // the `,U=` infix across a folder boundary is what produced
        // `Maildir error: duplicate UID` on real mail.
        const QString to = destDir + QLatin1Char('/')
                           + MaildirName::fresh(QFileInfo(from).fileName());

        if (!QFile::rename(from, to)) {
            emit errorOccurred(QStringLiteral("Cannot move %1 to %2")
                                   .arg(QFileInfo(from).fileName(), destFolder));
            continue;
        }

        // Index the NEW path BEFORE dropping the old one. The reverse order
        // removes the last filename for this message id, which deletes the
        // database entry and every tag on it; the file then reindexes as a
        // brand new message with default tags, silently.
        notmuch_message_t *indexed = nullptr;
        const notmuch_status_t added = notmuch_database_index_file(
            db, to.toUtf8().constData(), nullptr, &indexed);
        if (indexed)
            notmuch_message_destroy(indexed);

        // DUPLICATE_MESSAGE_ID is success here: it means the id was already
        // known, which is exactly the case for a file this just moved.
        if (added != NOTMUCH_STATUS_SUCCESS
            && added != NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID) {
            QFile::rename(to, from);
            emit errorOccurred(QStringLiteral("Cannot index %1 at its new path: %2")
                                   .arg(id, QString::fromUtf8(
                                                notmuch_status_to_string(added))));
            continue;
        }

        notmuch_database_remove_message(db, from.toUtf8().constData());
        moved.append(id);
        origins.insert(id, origin);
    }

    notmuch_database_close(db);
    notmuch_database_destroy(db);

    emit messagesMoved(moved, destFolder);
    emit messagesMovedFrom(origins, destFolder);
}

void NotmuchWorker::resolveMessages(const QStringList &messageIds,
                                   const QString &requestTag)
{
    if (messageIds.isEmpty())
        return;

    QStringList terms;
    terms.reserve(messageIds.size());
    for (const QString &id : messageIds)
        terms.append(QStringLiteral("id:%1").arg(id));

    resolveQuery(terms.join(QStringLiteral(" or ")), requestTag);
}

void NotmuchWorker::resolveThreadMessages(const QStringList &threadIds,
                                          const QString &requestTag)
{
    if (threadIds.isEmpty())
        return;

    // One combined query, for the reason applyTagsToThreads() gives: a query
    // per thread reopens the same Xapian cursor once per selected row.
    QStringList terms;
    terms.reserve(threadIds.size());
    for (const QString &id : threadIds)
        terms.append(QStringLiteral("thread:%1").arg(id));

    resolveQuery(terms.join(QStringLiteral(" or ")), requestTag);
}

void NotmuchWorker::resolveQuery(const QString &query,
                                 const QString &requestTag)
{
    if (!openReadOnly())
        return;

    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(QStringLiteral("Cannot resolve selected threads"));
        return;
    }

    notmuch_messages_t *raw = nullptr;
    if (notmuch_query_search_messages(nmQuery.get(), &raw)
        != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Cannot resolve selected threads"));
        return;
    }

    // Paths are reported RELATIVE to the database root, matching
    // ThreadSummary::firstMessagePath: the UI knows accounts only by their
    // maildir, itself a database-relative prefix.
    const QString dbRoot =
        QDir(mailRootOf(m_db)).absolutePath();

    QStringList messageIds;
    QStringList paths;
    QStringList tags;
    NmMessages messages(raw);
    for (; notmuch_messages_valid(messages.get());
           notmuch_messages_move_to_next(messages.get())) {
        NmMessage message(notmuch_messages_get(messages.get()));
        if (!message)
            continue;
        const char *rawName = notmuch_message_get_filename(message.get());
        if (!rawName)
            continue;
        messageIds.append(
            QString::fromUtf8(notmuch_message_get_message_id(message.get())));
        paths.append(
            QDir(dbRoot).relativeFilePath(QString::fromUtf8(rawName)));
        // Joined by a TAB, not a space. A notmuch tag may absolutely contain
        // a space: a Maildir folder named "Inbox/SlackBuilds users" produces
        // `deleted-from:Inbox/SlackBuilds users`, and splitting that on spaces
        // truncated the folder to "Inbox/SlackBuilds". Restore then moved the
        // messages into a folder of that name, CREATING it, so four real
        // messages ended up in a directory mbsync does not sync and the user
        // could not find them. A tab cannot appear in a tag, because notmuch's
        // own dump/restore format is whitespace-delimited by line.
        tags.append(tagsOf(message.get()).join(QLatin1Char('\t')));
    }

    emit threadMessagesResolved(messageIds, paths, tags, requestTag);
}

void NotmuchWorker::requestAllTags(quint64 generation)
{
    if (!openReadOnly())
        return;

    NmTags tags(notmuch_database_get_all_tags(m_db));
    if (!tags) {
        emit errorOccurred(QStringLiteral("Cannot list tags"));
        return;
    }

    QStringList result;
    for (; notmuch_tags_valid(tags.get()); notmuch_tags_move_to_next(tags.get()))
        result.append(QString::fromUtf8(notmuch_tags_get(tags.get())));

    // Sorted once here so no consumer has to sort again. notmuch returns tags
    // in Xapian term order, which is byte order, not the user's locale order.
    result.sort();
    emit allTagsReady(result, generation);
}

void NotmuchWorker::requestDatabaseStats(quint64 generation)
{
    if (!openReadOnly())
        return;

    DatabaseStats stats;

    // "*" is notmuch's match-everything query. Counting messages and threads
    // needs two calls on it: the numbers differ by the reply depth of the
    // database and there is no single call that yields both.
    NmQuery all(notmuch_query_create(m_db, "*"));
    if (all) {
        unsigned int messages = 0;
        if (notmuch_query_count_messages(all.get(), &messages)
            == NOTMUCH_STATUS_SUCCESS) {
            stats.messages = static_cast<int>(messages);
        }
    }

    // A second query object rather than reusing the one above: notmuch caches
    // results on a query, and counting both ways from one has bitten people.
    NmQuery allThreads(notmuch_query_create(m_db, "*"));
    if (allThreads) {
        unsigned int threads = 0;
        if (notmuch_query_count_threads(allThreads.get(), &threads)
            == NOTMUCH_STATUS_SUCCESS) {
            stats.threads = static_cast<int>(threads);
        }
    }

    // Already enumerated for the completer, so this costs nothing extra.
    NmTags tags(notmuch_database_get_all_tags(m_db));
    if (tags) {
        int count = 0;
        for (; notmuch_tags_valid(tags.get()); notmuch_tags_move_to_next(tags.get()))
            ++count;
        stats.tags = count;
    }

    emit databaseStatsReady(stats, generation);
}

void NotmuchWorker::requestCounts(const QStringList &queries, quint64 generation)
{
    if (!openReadOnly())
        return;

    QVector<int> counts;
    counts.reserve(queries.size());

    for (const QString &query : queries) {
        NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));

        unsigned int count = 0;
        // -1 rather than a skipped entry: the caller pairs these with its own
        // labels positionally, so a dropped answer would put a real number
        // against the wrong name, which is worse than showing none.
        if (!nmQuery ||
            notmuch_query_count_threads(nmQuery.get(), &count)
                != NOTMUCH_STATUS_SUCCESS) {
            counts.append(-1);
            continue;
        }

        // Threads, matching what the thread list shows. A message count would
        // disagree with the number of rows a click on this line produces.
        counts.append(static_cast<int>(count));
    }

    emit countsReady(counts, generation);
}

void NotmuchWorker::requestMessageCounts(const QStringList &queries,
                                         quint64 generation)
{
    if (!openReadOnly())
        return;

    QVector<int> counts;
    counts.reserve(queries.size());

    for (const QString &query : queries) {
        NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));

        unsigned int count = 0;
        // -1 rather than a skipped entry, matching requestCounts: the caller
        // pairs these with its own rules positionally, so a dropped answer
        // would put a real number against the wrong rule.
        if (!nmQuery ||
            notmuch_query_count_messages(nmQuery.get(), &count)
                != NOTMUCH_STATUS_SUCCESS) {
            counts.append(-1);
            continue;
        }

        // Messages, not threads: a rule tags messages, so counting threads
        // would understate a rule matching part of a large thread.
        counts.append(static_cast<int>(count));
    }

    emit messageCountsReady(counts, generation);
}

void NotmuchWorker::requestMailRoot()
{
    if (!openReadOnly()) {
        // Answered anyway, with an empty root. A consumer waiting for this
        // signal to enable something would otherwise wait for ever on a
        // database that cannot be opened, which is the same silent stall
        // loadMessage() emits an empty result to avoid.
        emit mailRootReady(QString());
        return;
    }

    // mailRootOf(), never notmuch_database_get_path(). Item 124: under a split
    // config the latter names the INDEX directory, and a draft or a sent copy
    // composed from it is written into the Xapian tree.
    const QString root = mailRootOf(m_db);
    emit mailRootReady(root.isEmpty() ? QString()
                                      : QDir(root).absolutePath());
}

void NotmuchWorker::requestFolders()
{
    if (!openReadOnly())
        return;

    const QString root = mailRootOf(m_db);
    if (root.isEmpty()) {
        emit errorOccurred(
            QStringLiteral("notmuch reports no database path."));
        return;
    }

    // A Maildir folder is a directory holding cur/. Testing for that rather
    // than listing every directory keeps the plumbing (cur, new, tmp) and an
    // account's container directory out of the list; neither is somewhere mail
    // is filed. Hidden directories are skipped, which is what excludes
    // .notmuch itself.
    QStringList folders;
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    const QDir rootDir(root);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!QFileInfo::exists(path + QStringLiteral("/cur")))
            continue;
        folders.append(rootDir.relativeFilePath(path));
    }

    // Sorted, so the dropdown keeps one order across openings. QDirIterator
    // walks in filesystem order, which is neither stable nor alphabetical.
    folders.sort();
    emit foldersReady(folders);
}
