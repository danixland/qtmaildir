#include "notmuchworker.h"

#include <notmuch.h>

#include <QSet>

#include <cstdlib>

#include "nmraii.h"

namespace {

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

} // namespace

NotmuchWorker::NotmuchWorker(const QString &notmuchConfigPath, QObject *parent)
    : QObject(parent), m_configPath(notmuchConfigPath)
{
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

void NotmuchWorker::runQuery(const QString &query, quint64 generation)
{
    if (!openReadOnly())
        return;

    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(QStringLiteral("Invalid query: %1").arg(query));
        return;
    }
    notmuch_query_set_sort(nmQuery.get(), NOTMUCH_SORT_NEWEST_FIRST);

    notmuch_threads_t *rawThreads = nullptr;
    const notmuch_status_t status =
        notmuch_query_search_threads(nmQuery.get(), &rawThreads);
    if (status != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Query failed: %1")
            .arg(QString::fromUtf8(notmuch_status_to_string(status))));
        return;
    }
    NmThreads threads(rawThreads);

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
                               quint64 generation)
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
        result.append(ref);
    }

    emit threadLoaded(result, generation);
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
        emit errorOccurred(
            QStringLiteral("Cannot open database for writing (is a sync running?): %1")
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
