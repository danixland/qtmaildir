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

signals:
    void threadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void queryFinished(int totalThreads, quint64 generation);
    void threadLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void tagsApplied(const TagChange &change);
    void errorOccurred(const QString &message);

private:
    bool openReadOnly();
    void close();

    QByteArray configPathArg() const;

    QString m_configPath;
    notmuch_database_t *m_db = nullptr;
};
