#include <QSignalSpy>
#include <QtTest>

#include "notmuchfixture.h"
#include "notmuchworker.h"
#include "types.h"

/// NotmuchWorker against a throwaway database. This is the only code in the
/// project that writes to a notmuch index, so applyTags gets the most
/// attention: a bug there corrupts real mail state.
class TestNotmuchWorker : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void queryReturnsAllThreads();
    void queryFiltersByTag();
    void queryReportsThreadMetadata();
    void malformedQueryYieldsNoThreads();
    void unreadableConfigEmitsError();
    void queryPassesGenerationThrough();

    void loadThreadReturnsMessagesOldestFirst();
    void loadThreadMarksMatchedMessages();
    void loadThreadWithEmptyQueryMatchesEverything();
    void loadThreadWithNonMatchingQueryMatchesNothing();

    void applyTagsAddsAndRemoves();
    void applyTagsEmitsTheChange();
    void applyTagsIgnoresUnknownMessageIds();
    void applyTagsWithNoIdsDoesNothing();
    void queryStillWorksAfterWrite();

    void applyTagsToThreadsTagsEveryMessage();
    void applyTagsToThreadsSpansMultipleThreads();
    void applyTagsToThreadsWithNoThreadsDoesNothing();

private:
    /// Tags of one message, read back through a fresh worker query.
    QStringList tagsOf(const QString &messageId);
    QVector<MessageRef> messagesOfThread(const QString &threadId,
                                         const QString &matchQuery = QString());
    QVector<ThreadSummary> runQuery(const QString &query);
    QString threadIdOf(const QString &subject);

    NotmuchFixture m_fixture;
};

void TestNotmuchWorker::initTestCase()
{
    QVERIFY(m_fixture.isValid());

    // Thread A: two messages, a reply. Both read.
    QVERIFY(m_fixture.addMessage(QStringLiteral("inbox"), QStringLiteral("a1@example.org"),
                                 QStringLiteral("Release notes"),
                                 QStringLiteral("Alice <alice@example.org>"),
                                 QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                                 QStringLiteral("first message"), false));
    QVERIFY(m_fixture.addMessage(QStringLiteral("inbox"), QStringLiteral("a2@example.org"),
                                 QStringLiteral("Re: Release notes"),
                                 QStringLiteral("Bob <bob@example.org>"),
                                 QStringLiteral("Tue, 2 Jun 2026 10:00:00 +0000"),
                                 QStringLiteral("second message with hamsterwheel"), false,
                                 QStringLiteral("a1@example.org")));

    // Thread B: one unread message.
    QVERIFY(m_fixture.addMessage(QStringLiteral("inbox"), QStringLiteral("b1@example.org"),
                                 QStringLiteral("Newsletter"),
                                 QStringLiteral("Carol <carol@example.org>"),
                                 QStringLiteral("Wed, 3 Jun 2026 10:00:00 +0000"),
                                 QStringLiteral("third message")));

    // Thread C: in a different folder, for path-scoped queries.
    QVERIFY(m_fixture.addMessage(QStringLiteral("archive"), QStringLiteral("c1@example.org"),
                                 QStringLiteral("Old thing"),
                                 QStringLiteral("Dave <dave@example.org>"),
                                 QStringLiteral("Thu, 4 Jun 2026 10:00:00 +0000"),
                                 QStringLiteral("fourth message"), false));

    QVERIFY2(m_fixture.index(), qPrintable(m_fixture.error()));
}

QVector<ThreadSummary> TestNotmuchWorker::runQuery(const QString &query)
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);
    QSignalSpy finished(&worker, &NotmuchWorker::queryFinished);

    worker.runQuery(query, 1);

    QVector<ThreadSummary> all;
    for (const QList<QVariant> &args : ready)
        all += args.at(0).value<QVector<ThreadSummary>>();
    return all;
}

QString TestNotmuchWorker::threadIdOf(const QString &subject)
{
    const QVector<ThreadSummary> threads = runQuery(QStringLiteral("*"));
    for (const ThreadSummary &t : threads) {
        if (t.subject == subject)
            return t.threadId;
    }
    return QString();
}

QVector<MessageRef> TestNotmuchWorker::messagesOfThread(const QString &threadId,
                                                        const QString &matchQuery)
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy loaded(&worker, &NotmuchWorker::threadLoaded);
    worker.loadThread(threadId, matchQuery, 1);
    if (loaded.isEmpty())
        return {};
    return loaded.first().at(0).value<QVector<MessageRef>>();
}

QStringList TestNotmuchWorker::tagsOf(const QString &messageId)
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy loaded(&worker, &NotmuchWorker::threadLoaded);
    worker.loadThread(QStringLiteral("{id:%1}").arg(messageId), QString(), 1);
    if (loaded.isEmpty())
        return {};
    const auto messages = loaded.first().at(0).value<QVector<MessageRef>>();
    for (const MessageRef &m : messages) {
        if (m.messageId == messageId)
            return m.tags;
    }
    return {};
}

void TestNotmuchWorker::queryReturnsAllThreads()
{
    const QVector<ThreadSummary> threads = runQuery(QStringLiteral("*"));
    QCOMPARE(threads.size(), 3);
}

void TestNotmuchWorker::queryFiltersByTag()
{
    const QVector<ThreadSummary> unread = runQuery(QStringLiteral("tag:unread"));
    QCOMPARE(unread.size(), 1);
    QCOMPARE(unread.first().subject, QStringLiteral("Newsletter"));
    QVERIFY(unread.first().isUnread());
}

void TestNotmuchWorker::queryReportsThreadMetadata()
{
    const QVector<ThreadSummary> threads = runQuery(QStringLiteral("subject:\"Release notes\""));
    QCOMPARE(threads.size(), 1);

    const ThreadSummary &t = threads.first();
    QVERIFY(!t.threadId.isEmpty());
    QCOMPARE(t.subject, QStringLiteral("Release notes"));
    QVERIFY(t.authors.contains(QStringLiteral("Alice")));
    QCOMPARE(t.totalCount, 2);
    QVERIFY(t.date.isValid());
    QVERIFY(t.tags.contains(QStringLiteral("inbox")));
}

void TestNotmuchWorker::malformedQueryYieldsNoThreads()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);
    QSignalSpy finished(&worker, &NotmuchWorker::queryFinished);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    // notmuch's query parser is lenient: an unbalanced quote is accepted and
    // simply matches nothing, rather than failing. Verified against notmuch
    // 0.39, which exits 0 on this query. So the contract here is "no threads,
    // no error, one queryFinished with zero" — not an error path.
    worker.runQuery(QStringLiteral("subject:\"unterminated"), 1);

    QVERIFY(ready.isEmpty());
    QVERIFY(errors.isEmpty());
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(0).toInt(), 0);
}

void TestNotmuchWorker::unreadableConfigEmitsError()
{
    // Fails closed: a bad config path must report an error, never silently
    // fall through to the user's real database.
    NotmuchWorker worker(QStringLiteral("/nonexistent/qtmaildir-test/config"));
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.runQuery(QStringLiteral("*"), 1);

    QCOMPARE(errors.size(), 1);
    QVERIFY(ready.isEmpty());
}

void TestNotmuchWorker::queryPassesGenerationThrough()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);
    QSignalSpy finished(&worker, &NotmuchWorker::queryFinished);

    worker.runQuery(QStringLiteral("*"), 42);

    QCOMPARE(ready.size(), 1);
    QCOMPARE(ready.first().at(1).value<quint64>(), quint64(42));
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(0).toInt(), 3);
    QCOMPARE(finished.first().at(1).value<quint64>(), quint64(42));
}

void TestNotmuchWorker::loadThreadReturnsMessagesOldestFirst()
{
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    QVERIFY(!threadId.isEmpty());

    const QVector<MessageRef> messages = messagesOfThread(threadId);
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).messageId, QStringLiteral("a1@example.org"));
    QCOMPARE(messages.at(1).messageId, QStringLiteral("a2@example.org"));
    QVERIFY(QFile::exists(messages.at(0).filePath));
    QVERIFY(messages.at(0).tags.contains(QStringLiteral("inbox")));
}

void TestNotmuchWorker::loadThreadMarksMatchedMessages()
{
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    QVERIFY(!threadId.isEmpty());

    // Only the second message contains this word.
    const QVector<MessageRef> messages =
        messagesOfThread(threadId, QStringLiteral("hamsterwheel"));
    QCOMPARE(messages.size(), 2);
    QVERIFY(!messages.at(0).matched);
    QVERIFY(messages.at(1).matched);
}

void TestNotmuchWorker::loadThreadWithEmptyQueryMatchesEverything()
{
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    const QVector<MessageRef> messages = messagesOfThread(threadId, QString());
    QCOMPARE(messages.size(), 2);
    for (const MessageRef &m : messages)
        QVERIFY(m.matched);
}

void TestNotmuchWorker::loadThreadWithNonMatchingQueryMatchesNothing()
{
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));

    // A real query that matches nothing in this thread must mark every message
    // unmatched. Treating "no matches" as "everything matched" would render a
    // whole thread expanded when the user filtered it down to nothing.
    const QVector<MessageRef> messages =
        messagesOfThread(threadId, QStringLiteral("tag:thistagdoesnotexist"));
    QCOMPARE(messages.size(), 2);
    for (const MessageRef &m : messages)
        QVERIFY(!m.matched);
}

void TestNotmuchWorker::applyTagsAddsAndRemoves()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    const TagChange change{ { QStringLiteral("a1@example.org") },
                            { QStringLiteral("flagged") },
                            { QStringLiteral("inbox") },
                            QStringLiteral("Flag and archive") };
    worker.applyTags(change);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    const QStringList tags = tagsOf(QStringLiteral("a1@example.org"));
    QVERIFY(tags.contains(QStringLiteral("flagged")));
    QVERIFY(!tags.contains(QStringLiteral("inbox")));

    // Put it back so later tests see the original state.
    NotmuchWorker restore(m_fixture.configPath());
    restore.applyTags(change.inverted());
    const QStringList back = tagsOf(QStringLiteral("a1@example.org"));
    QVERIFY(back.contains(QStringLiteral("inbox")));
    QVERIFY(!back.contains(QStringLiteral("flagged")));
}

void TestNotmuchWorker::applyTagsEmitsTheChange()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy applied(&worker, &NotmuchWorker::tagsApplied);

    const TagChange change{ { QStringLiteral("b1@example.org") },
                            { QStringLiteral("testtag") },
                            {},
                            QStringLiteral("Add testtag") };
    worker.applyTags(change);

    QCOMPARE(applied.size(), 1);
    const TagChange emitted = applied.first().at(0).value<TagChange>();
    QCOMPARE(emitted.messageIds, change.messageIds);
    QCOMPARE(emitted.added, change.added);
    QCOMPARE(emitted.description, change.description);

    NotmuchWorker restore(m_fixture.configPath());
    restore.applyTags(change.inverted());
}

void TestNotmuchWorker::applyTagsIgnoresUnknownMessageIds()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy applied(&worker, &NotmuchWorker::tagsApplied);

    // A stale id from a since-deleted message must not abort the whole batch:
    // the real ids alongside it still need tagging.
    const TagChange change{ { QStringLiteral("nosuchmessage@example.org"),
                              QStringLiteral("b1@example.org") },
                            { QStringLiteral("survivor") },
                            {},
                            QStringLiteral("Partially stale batch") };
    worker.applyTags(change);

    QCOMPARE(applied.size(), 1);
    QVERIFY(tagsOf(QStringLiteral("b1@example.org")).contains(QStringLiteral("survivor")));

    NotmuchWorker restore(m_fixture.configPath());
    restore.applyTags(change.inverted());
}

void TestNotmuchWorker::applyTagsWithNoIdsDoesNothing()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy applied(&worker, &NotmuchWorker::tagsApplied);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.applyTags(TagChange{ {}, { QStringLiteral("x") }, {}, QStringLiteral("Nothing") });

    QVERIFY(applied.isEmpty());
    QVERIFY(errors.isEmpty());
}

void TestNotmuchWorker::queryStillWorksAfterWrite()
{
    // applyTags closes the read-only handle to take the write lock. The same
    // worker must be able to query again afterwards.
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);

    worker.runQuery(QStringLiteral("*"), 1);
    QCOMPARE(ready.size(), 1);

    const TagChange change{ { QStringLiteral("b1@example.org") },
                            { QStringLiteral("roundtrip") },
                            {},
                            QStringLiteral("Round trip") };
    worker.applyTags(change);

    worker.runQuery(QStringLiteral("*"), 2);
    QCOMPARE(ready.size(), 2);
    QCOMPARE(ready.at(1).at(0).value<QVector<ThreadSummary>>().size(), 3);

    worker.applyTags(change.inverted());
}

void TestNotmuchWorker::applyTagsToThreadsTagsEveryMessage()
{
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    QVERIFY(!threadId.isEmpty());

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy applied(&worker, &NotmuchWorker::tagsApplied);

    worker.applyTagsToThreads({ threadId }, { QStringLiteral("batched") }, {},
                              QStringLiteral("Batch tag"));

    QCOMPARE(applied.size(), 1);
    // Both messages of the thread, resolved by the worker, not by the caller.
    const TagChange emitted = applied.first().at(0).value<TagChange>();
    QCOMPARE(emitted.messageIds.size(), 2);
    QVERIFY(tagsOf(QStringLiteral("a1@example.org")).contains(QStringLiteral("batched")));
    QVERIFY(tagsOf(QStringLiteral("a2@example.org")).contains(QStringLiteral("batched")));

    NotmuchWorker restore(m_fixture.configPath());
    restore.applyTags(emitted.inverted());
}

void TestNotmuchWorker::applyTagsToThreadsSpansMultipleThreads()
{
    const QString threadA = threadIdOf(QStringLiteral("Release notes"));
    const QString threadB = threadIdOf(QStringLiteral("Newsletter"));
    QVERIFY(!threadA.isEmpty());
    QVERIFY(!threadB.isEmpty());

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy applied(&worker, &NotmuchWorker::tagsApplied);

    worker.applyTagsToThreads({ threadA, threadB }, { QStringLiteral("multi") }, {},
                              QStringLiteral("Multi-thread tag"));

    QCOMPARE(applied.size(), 1);
    const TagChange emitted = applied.first().at(0).value<TagChange>();
    QCOMPARE(emitted.messageIds.size(), 3);
    QVERIFY(tagsOf(QStringLiteral("a1@example.org")).contains(QStringLiteral("multi")));
    QVERIFY(tagsOf(QStringLiteral("b1@example.org")).contains(QStringLiteral("multi")));

    NotmuchWorker restore(m_fixture.configPath());
    restore.applyTags(emitted.inverted());
}

void TestNotmuchWorker::applyTagsToThreadsWithNoThreadsDoesNothing()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy applied(&worker, &NotmuchWorker::tagsApplied);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.applyTagsToThreads({}, { QStringLiteral("x") }, {}, QStringLiteral("Nothing"));

    QVERIFY(applied.isEmpty());
    QVERIFY(errors.isEmpty());
}

QTEST_MAIN(TestNotmuchWorker)
#include "test_notmuchworker.moc"
