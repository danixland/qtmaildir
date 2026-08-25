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
    void oldestFirstReversesTheOrder();
    void theSortOrderCrossesAQueuedCall();

    void loadThreadReturnsMessagesOldestFirst();
    void loadThreadMarksMatchedMessages();
    void loadThreadWithEmptyQueryMatchesEverything();
    void loadThreadWithNonMatchingQueryMatchesNothing();

    void applyTagsAddsAndRemoves();
    void applyTagsEmitsTheChange();
    void applyTagsIgnoresUnknownMessageIds();
    void applyTagsWithNoIdsDoesNothing();
    void queryStillWorksAfterWrite();
    void aThreadCarriesItsCardMessagesOwnTags();

    void applyTagsToThreadsTagsEveryMessage();
    void applyTagsToThreadsSpansMultipleThreads();
    void applyTagsToThreadsWithNoThreadsDoesNothing();

    void requestAllTagsReturnsSortedTags();
    void requestAllTagsOnUnreadableConfigEmitsError();

    void loadMessageReturnsOnlyThatMessage();
    void loadMessageOnAnUnknownIdReturnsNothing();
    void aQueryCarriesEachThreadsFirstMessageId();
    void aSentQueryCarriesTheMatchedMessageNotTheThreadsFirst();
    void loadThreadTreeReportsReplyDepth();
    void loadThreadTreeCarriesTheFactsARowNeeds();

    void loadThreadMatchedOnlyDropsTheRest();
    void loadThreadMatchedOnlyWithNoQueryKeepsEverything();

    void recipientsAreAbsentUnlessAskedFor();
    void recipientsAreFoldedWhenAskedFor();
    void recipientsCrossAQueuedCall();

    void requestCountsAnswersOneCountPerQuery();
    void requestCountsKeepsPositionOnAnInvalidQuery();
    void requestDatabaseStatsCountsMessagesNotThreads();
    void requestDatabaseStatsOnUnreadableConfigEmitsError();

    void messageCountsCountMessagesNotThreads();
    void messageCountsReportAnInvalidQueryAsMinusOne();

    void requestFoldersListsEveryMaildirFolder();
    void requestFoldersOnUnreadableConfigEmitsError();

    void moveMessagesRelocatesTheFile();
    void moveMessagesReindexesAtTheNewPath();
    void moveMessagesKeepsTheMessagesTags();
    void moveMessagesReportsOnlyWhatMoved();
    void moveMessagesGivesTheFileAFreshMaildirName();
    void moveMessagesKeepsTheMaildirFlags();
    void moveMessagesRecoversWhenASyncRenamedTheFile();
    void moveMessagesStillReportsAMessageThatIsReallyGone();

    void indexDraftFileMakesAFileFindable();
    void indexDraftFileRemovesThePreviousFile();
    void removeIndexedFileDropsTheEntry();

    void aSplitIndexStillResolvesTheMailRoot();
    void aSplitIndexMovesIntoTheMaildirNotTheIndex();
    void aSplitIndexListsTheMaildirsFolders();
    void twoMessagesMovedTogetherGetDistinctNames();

    void aQuerySeesMailIndexedAfterTheWorkerOpened();

private:
    /// Adds one read message in `folder` and reindexes, for the move tests.
    /// Each of those takes its own message, because a move is destructive and
    /// the fixture database is shared by every test in this class.
    bool addMovableMessage(const QString &folder, const QString &messageId);
    /// Writes a draft file into <folder>/cur with the "D" flag and returns its
    /// path, WITHOUT indexing it, so a test can index just that file.
    QString writeDraftFile(const QString &folder, const QString &messageId);
    /// The single file backing `messageId`, or an empty string when the
    /// database does not know the id.
    QString fileOf(const QString &messageId,
                   const QString &configPath = QString());

    /// Tags of one message, read back through a fresh worker query.
    QStringList tagsOf(const QString &messageId);
    QVector<MessageRef> messagesOfThread(const QString &threadId,
                                         const QString &matchQuery = QString(),
                                         bool matchedOnly = false);
    QVector<ThreadSummary> runQuery(
        const QString &query,
        NotmuchWorker::SortOrder sort = NotmuchWorker::NewestFirst,
        bool withRecipients = false);
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

    // Thread D: in a "sent" folder, with real recipients. The To header is the
    // only thing that distinguishes these from the threads above, and it is
    // what the recipient fold reads.
    QVERIFY(m_fixture.addMessage(QStringLiteral("sent"), QStringLiteral("d1@example.org"),
                                 QStringLiteral("Preventivo"),
                                 QStringLiteral("You <you@example.org>"),
                                 QStringLiteral("Fri, 5 Jun 2026 10:00:00 +0000"),
                                 QStringLiteral("fifth message"), false, QString(),
                                 QStringLiteral("Mario Rossi <mario@example.org>")));

    // Thread E: several recipients, one of them with a comma inside a quoted
    // display name, which is what defeats splitting on commas.
    QVERIFY(m_fixture.addMessage(QStringLiteral("sent"), QStringLiteral("e1@example.org"),
                                 QStringLiteral("Riunione"),
                                 QStringLiteral("You <you@example.org>"),
                                 QStringLiteral("Sat, 6 Jun 2026 10:00:00 +0000"),
                                 QStringLiteral("sixth message"), false, QString(),
                                 QStringLiteral("\"Rossi, Mario\" <mario@example.org>, "
                                                "info@example.net, "
                                                "third@example.org")));

    QVERIFY2(m_fixture.index(), qPrintable(m_fixture.error()));
}

void TestNotmuchWorker::aQuerySeesMailIndexedAfterTheWorkerOpened()
{
    // The defect this covers is item 104, and it is the reason mail arriving
    // while the window is open was invisible until the application restarted.
    //
    // A read-only notmuch handle is a Xapian SNAPSHOT taken when it is opened.
    // `notmuch new` runs in a separate process, so nothing it writes is visible
    // to a handle already open, however long it is held and however many
    // queries are run through it. The worker opens once and keeps that handle
    // for the process lifetime, so every query after the first sync answered
    // from a stale index: a refresh missed the mail, and so did a query the
    // user typed by hand, which is what ruled out the model and the generation
    // counter when this was diagnosed.
    //
    // ONE worker across both queries is the whole point. The runQuery() helper
    // builds a fresh worker per call, which opens a fresh handle and therefore
    // cannot reproduce this at all: a test written through it passes against
    // the bug.
    NotmuchWorker worker(m_fixture.configPath());

    const QString query = QStringLiteral("subject:\"Arrived mid-session\"");

    {
        QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);
        worker.runQuery(query, 1);
        QVector<ThreadSummary> before;
        for (const QList<QVariant> &args : ready)
            before += args.at(0).value<QVector<ThreadSummary>>();
        // Establishes that the handle is open and the query is well-formed,
        // rather than leaving "found nothing" to mean either.
        QCOMPARE(before.size(), 0);
    }

    // A second process writes to the index, exactly as the sync script's
    // `notmuch new` does while the window is open.
    QVERIFY(m_fixture.addMessage(QStringLiteral("inbox"),
                                 QStringLiteral("mid@example.org"),
                                 QStringLiteral("Arrived mid-session"),
                                 QStringLiteral("Carol <carol@example.org>"),
                                 QStringLiteral("Tue, 9 Jun 2026 10:00:00 +0000"),
                                 QStringLiteral("new mail")));
    QVERIFY2(m_fixture.index(), qPrintable(m_fixture.error()));

    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);
    worker.runQuery(query, 2);
    QVector<ThreadSummary> after;
    for (const QList<QVariant> &args : ready)
        after += args.at(0).value<QVector<ThreadSummary>>();

    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().subject, QStringLiteral("Arrived mid-session"));
}

QVector<ThreadSummary> TestNotmuchWorker::runQuery(
    const QString &query, NotmuchWorker::SortOrder sort, bool withRecipients)
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);
    QSignalSpy finished(&worker, &NotmuchWorker::queryFinished);

    worker.runQuery(query, 1, sort, withRecipients);

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

bool TestNotmuchWorker::addMovableMessage(const QString &folder,
                                          const QString &messageId)
{
    if (!m_fixture.addMessage(folder, messageId,
                              QStringLiteral("Movable %1").arg(messageId),
                              QStringLiteral("Erin <erin@example.org>"),
                              QStringLiteral("Sun, 7 Jun 2026 10:00:00 +0000"),
                              QStringLiteral("movable body"), false)) {
        return false;
    }
    return m_fixture.index();
}

QString TestNotmuchWorker::writeDraftFile(const QString &folder,
                                          const QString &messageId)
{
    const QString dirPath = m_fixture.maildirPath() + QLatin1Char('/') + folder;
    QDir dir;
    if (!dir.mkpath(dirPath + QStringLiteral("/cur"))
        || !dir.mkpath(dirPath + QStringLiteral("/new"))
        || !dir.mkpath(dirPath + QStringLiteral("/tmp"))) {
        return {};
    }

    // The same filename recipe addMessage() uses, with the draft flag instead
    // of the seen flag, matching what DraftStore writes.
    QString base = messageId;
    base.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
    base.replace(QLatin1Char('@'), QLatin1Char('.'));
    base.replace(QLatin1Char('/'), QLatin1Char('.'));
    base += QStringLiteral(":2,D");

    const QString path = dirPath + QStringLiteral("/cur/") + base;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    QTextStream out(&file);
    out << "From: You <you@example.org>\n"
        << "To: someone@example.org\n"
        << "Subject: A draft\n"
        << "Message-ID: <" << messageId << ">\n"
        << "Date: Sun, 7 Jun 2026 10:00:00 +0000\n"
        << "\n"
        << "draft body\n";
    out.flush();
    file.close();
    return path;
}

QString TestNotmuchWorker::fileOf(const QString &messageId,
                                 const QString &configPath)
{
    NotmuchWorker worker(configPath.isEmpty() ? m_fixture.configPath() : configPath);
    QSignalSpy loaded(&worker, &NotmuchWorker::threadLoaded);
    worker.loadThread(QStringLiteral("{id:%1}").arg(messageId), QString(), 1);
    if (loaded.isEmpty())
        return {};
    const auto messages = loaded.first().at(0).value<QVector<MessageRef>>();
    for (const MessageRef &m : messages) {
        if (m.messageId == messageId)
            return m.filePath;
    }
    return {};
}

QVector<MessageRef> TestNotmuchWorker::messagesOfThread(const QString &threadId,
                                                        const QString &matchQuery,
                                                        bool matchedOnly)
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy loaded(&worker, &NotmuchWorker::threadLoaded);
    worker.loadThread(threadId, matchQuery, 1, matchedOnly);
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

void TestNotmuchWorker::loadMessageReturnsOnlyThatMessage()
{
    // a2 is a reply in a two-message thread. Selecting a reply row must render
    // that message alone; loadThread would hand back the whole thread and the
    // pane would show the conversation the user was trying to look inside.
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy loaded(&worker, &NotmuchWorker::messageLoaded);
    worker.loadMessage(QStringLiteral("a2@example.org"), 1);

    QCOMPARE(loaded.count(), 1);
    const auto messages = loaded.first().at(0).value<QVector<MessageRef>>();

    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.first().messageId, QStringLiteral("a2@example.org"));
    QVERIFY(!messages.first().filePath.isEmpty());

    // matched, so the pane renders it expanded rather than as a stub. The user
    // asked for this message by clicking it, which is as matched as it gets.
    QVERIFY(messages.first().matched);
}

void TestNotmuchWorker::loadMessageOnAnUnknownIdReturnsNothing()
{
    // Empty rather than an error: a stale row after a reindex is an ordinary
    // race, not a failure worth a message in the status bar.
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy loaded(&worker, &NotmuchWorker::messageLoaded);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.loadMessage(QStringLiteral("nonexistent@example.org"), 1);

    QCOMPARE(loaded.count(), 1);
    QVERIFY(loaded.first().at(0).value<QVector<MessageRef>>().isEmpty());
    QCOMPARE(errors.count(), 0);
}

void TestNotmuchWorker::aQueryCarriesEachThreadsFirstMessageId()
{
    // The root card IS the thread's first message, so selecting it must be
    // able to load that message. Before this the id was known only after the
    // thread had been EXPANDED, so a first click on an unexpanded root fell
    // back to rendering the whole conversation, and the same click behaved
    // differently once the thread had been opened. That inconsistency is what
    // the user reported as item 66.
    //
    // Free to collect: measured against a real 36,615-thread database, a walk
    // with this and a walk without are indistinguishable, because
    // notmuch_thread_get_toplevel_messages reads the index rather than the
    // message files. Contrast ThreadSummary::recipients, which reads every
    // file and is Sent-only for that reason.
    const QVector<ThreadSummary> threads = runQuery(QStringLiteral("*"));
    QVERIFY(!threads.isEmpty());

    bool sawTheThread = false;
    for (const ThreadSummary &t : threads) {
        QVERIFY2(!t.firstMessageId.isEmpty(),
                 qPrintable(QStringLiteral("thread %1 carries no first message")
                                .arg(t.subject)));
        if (t.subject == QStringLiteral("Release notes")) {
            // a1 is the root, a2 its reply. The FIRST message, not the newest.
            QCOMPARE(t.firstMessageId, QStringLiteral("a1@example.org"));
            sawTheThread = true;
        }
    }
    QVERIFY2(sawTheThread, "the two-message thread was not in the results");
}

void TestNotmuchWorker::aThreadCarriesItsCardMessagesOwnTags()
{
    // Item 111. A card draws its own message's tags at full size and the rest
    // of the conversation's smaller, so it needs BOTH: `tags` is notmuch's
    // union over the thread and `firstMessageTags` is the one message the card
    // stands for.
    //
    // Derived from the message LOAD at first, which meant an unopened row had
    // no split and drew everything as its own, correcting itself only when the
    // user selected it. The user reported exactly that. The query knows, and
    // the walk that finds firstMessageId is already holding the message, so
    // this is the same index read rather than a second pass.

    // A tag on the REPLY only, which is the case that separates the two: a1 is
    // the card's message, a2 its reply.
    NotmuchWorker writer(m_fixture.configPath());
    writer.applyTags(TagChange{ { QStringLiteral("a2@example.org") },
                                { QStringLiteral("signed") },
                                {},
                                QStringLiteral("Sign the reply") });

    const QVector<ThreadSummary> threads = runQuery(QStringLiteral("*"));
    bool sawTheThread = false;
    for (const ThreadSummary &t : threads) {
        if (t.subject != QStringLiteral("Release notes"))
            continue;
        sawTheThread = true;

        QCOMPARE(t.firstMessageId, QStringLiteral("a1@example.org"));

        // The union carries the reply's tag, as notmuch reports it.
        QVERIFY2(t.tags.contains(QStringLiteral("signed")),
                 "the thread's own tags stopped being the union, which the "
                 "sibling tier is derived from");

        // The card's message does not, and this is the whole point: without it
        // the card claims a tag belonging to a message it does not display.
        QVERIFY2(!t.firstMessageTags.isEmpty(),
                 "the query carried no per-message tags, so an unopened row "
                 "has no split and draws every chip at full size");
        QVERIFY2(!t.firstMessageTags.contains(QStringLiteral("signed")),
                 "the card's message was given its reply's tag");

        // And it does carry its own.
        QVERIFY(t.firstMessageTags.contains(QStringLiteral("inbox")));
    }
    QVERIFY2(sawTheThread, "the two-message thread was not in the results");
}

void TestNotmuchWorker::aSentQueryCarriesTheMatchedMessageNotTheThreadsFirst()
{
    // THE case the Sent branch exists for, and the one hardest to get right.
    // In a Sent view a row stands for what the USER sent, which is normally a
    // reply. Taking the thread's opening message there would show whoever
    // started the conversation instead, under a heading that says Sent.
    //
    // "Release notes" is a1 (the root) plus a2 (its reply). A query matching
    // only the reply stands in for a Sent query: withRecipients is what the
    // Sent view sets, and it is what selects the matched-message branch.
    const QVector<ThreadSummary> asSent =
        runQuery(QStringLiteral("id:a2@example.org"),
                 NotmuchWorker::NewestFirst, /*withRecipients=*/true);

    bool sawIt = false;
    for (const ThreadSummary &t : asSent) {
        if (t.subject != QStringLiteral("Release notes"))
            continue;
        // The REPLY, because that is what matched. Not a1, the thread's first.
        QCOMPARE(t.firstMessageId, QStringLiteral("a2@example.org"));
        sawIt = true;
    }
    QVERIFY2(sawIt, "the thread was not in the results at all");

    // And the same thread under an ordinary query still reports its opening
    // message, so the branch is a Sent special case and not a change of
    // meaning for everything else.
    const QVector<ThreadSummary> asInbox =
        runQuery(QStringLiteral("id:a2@example.org"),
                 NotmuchWorker::NewestFirst, /*withRecipients=*/false);
    for (const ThreadSummary &t : asInbox) {
        if (t.subject == QStringLiteral("Release notes"))
            QCOMPARE(t.firstMessageId, QStringLiteral("a1@example.org"));
    }
}

void TestNotmuchWorker::loadThreadTreeReportsReplyDepth()
{
    // Thread A is a root plus one reply carrying In-Reply-To, which is what
    // notmuch threads on. Without that header the two would be separate threads
    // and this test would assert nothing about depth.
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    QVERIFY(!threadId.isEmpty());

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy loaded(&worker, &NotmuchWorker::threadTreeLoaded);
    worker.loadThreadTree(threadId, QString(), 1);

    QCOMPARE(loaded.count(), 1);
    const auto nodes = loaded.first().at(0).value<QVector<MessageNode>>();

    QCOMPARE(nodes.size(), 2);
    QCOMPARE(nodes.at(0).messageId, QStringLiteral("a1@example.org"));
    QCOMPARE(nodes.at(0).depth, 0);
    QCOMPARE(nodes.at(1).messageId, QStringLiteral("a2@example.org"));
    QCOMPARE(nodes.at(1).depth, 1);
}

void TestNotmuchWorker::loadThreadTreeCarriesTheFactsARowNeeds()
{
    // A row is drawn without opening the message, so the walk has to read the
    // headers. loadThread does not, which is why a separate signal exists.
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    QVERIFY(!threadId.isEmpty());

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy loaded(&worker, &NotmuchWorker::threadTreeLoaded);
    worker.loadThreadTree(threadId, QString(), 1);

    QCOMPARE(loaded.count(), 1);
    const auto nodes = loaded.first().at(0).value<QVector<MessageNode>>();
    QCOMPARE(nodes.size(), 2);

    const MessageNode &reply = nodes.at(1);
    QVERIFY(reply.from.contains(QStringLiteral("bob@example.org")));
    QCOMPARE(reply.subject, QStringLiteral("Re: Release notes"));
    QVERIFY(reply.date.isValid());
    QVERIFY(!reply.filePath.isEmpty());

    // Every node names its thread, so a batch does not need the caller to keep
    // track of which thread it asked about.
    QCOMPARE(reply.threadId, threadId);
    QCOMPARE(nodes.at(0).threadId, threadId);
}

void TestNotmuchWorker::queryReturnsAllThreads()
{
    const QVector<ThreadSummary> threads = runQuery(QStringLiteral("*"));
    QCOMPARE(threads.size(), 5);
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
    QCOMPARE(finished.first().at(0).toInt(), 5);
    QCOMPARE(finished.first().at(1).value<quint64>(), quint64(42));
}

void TestNotmuchWorker::oldestFirstReversesTheOrder()
{
    const QVector<ThreadSummary> newest = runQuery(QStringLiteral("*"));
    const QVector<ThreadSummary> oldest =
        runQuery(QStringLiteral("*"), NotmuchWorker::OldestFirst);

    QCOMPARE(oldest.size(), newest.size());

    // The guard: with fewer than two threads, or with every thread carrying
    // the same date, a reversal is indistinguishable from no sorting at all
    // and every assertion below would pass against a hardcoded order.
    QVERIFY(newest.size() >= 2);
    QVERIFY(newest.first().date != newest.last().date);

    QCOMPARE(oldest.first().threadId, newest.last().threadId);
    QCOMPARE(oldest.last().threadId, newest.first().threadId);
}

void TestNotmuchWorker::theSortOrderCrossesAQueuedCall()
{
    // MainWindow reaches the worker with invokeMethod(..., QueuedConnection)
    // across a thread boundary, and a Q_ARG whose type the meta-object system
    // does not know FAILS AT RUNTIME with a warning, not at compile time. So
    // the enum's registration is asserted here rather than assumed from Q_ENUM.
    QVERIFY2(QMetaType::fromName("NotmuchWorker::SortOrder").isValid(),
             "SortOrder is not a registered metatype, so the queued runQuery "
             "call will drop its sort argument at runtime");

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);

    // The real call shape, invoked by NAME exactly as MainWindow does.
    QVERIFY(QMetaObject::invokeMethod(
        &worker, "runQuery", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("*")), Q_ARG(quint64, 1),
        Q_ARG(NotmuchWorker::SortOrder, NotmuchWorker::OldestFirst)));
    QCOMPARE(ready.size(), 1);
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
    QCOMPARE(ready.at(1).at(0).value<QVector<ThreadSummary>>().size(), 5);

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

void TestNotmuchWorker::requestAllTagsReturnsSortedTags()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy spy(&worker, &NotmuchWorker::allTagsReady);

    worker.requestAllTags(7);

    QCOMPARE(spy.count(), 1);
    const QStringList tags = spy.at(0).at(0).toStringList();
    const quint64 generation = spy.at(0).at(1).value<quint64>();

    QCOMPARE(generation, quint64(7));
    QVERIFY(tags.contains(QStringLiteral("inbox")));
    QVERIFY(tags.contains(QStringLiteral("unread")));

    // Completion offers these in order, so the worker sorts once rather than
    // every consumer sorting again.
    QStringList sorted = tags;
    sorted.sort();
    QCOMPARE(tags, sorted);
}

void TestNotmuchWorker::requestAllTagsOnUnreadableConfigEmitsError()
{
    // Fails closed like every other entry point: never silently fall through to
    // the user's real database.
    NotmuchWorker worker(QStringLiteral("/nonexistent/qtmaildir-test/config"));
    QSignalSpy ready(&worker, &NotmuchWorker::allTagsReady);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.requestAllTags(1);

    QCOMPARE(errors.size(), 1);
    QVERIFY(ready.isEmpty());
}

void TestNotmuchWorker::loadThreadMatchedOnlyDropsTheRest()
{
    // Thread A is two messages, and only the reply carries "hamsterwheel".
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    QVERIFY(!threadId.isEmpty());

    // Without the flag: both messages, the non-matching one marked as a stub.
    // This is the reading pane's normal behaviour and must not change.
    const QVector<MessageRef> whole =
        messagesOfThread(threadId, QStringLiteral("hamsterwheel"));
    QCOMPARE(whole.size(), 2);

    // With it: only the message that matched. The pane in a Sent view shows
    // what the user sent, not the conversation their message started.
    const QVector<MessageRef> matched =
        messagesOfThread(threadId, QStringLiteral("hamsterwheel"), true);
    QCOMPARE(matched.size(), 1);
    QVERIFY(matched.at(0).matched);
    QCOMPARE(matched.at(0).messageId, QStringLiteral("a2@example.org"));
}

void TestNotmuchWorker::loadThreadMatchedOnlyWithNoQueryKeepsEverything()
{
    // No query means nothing was filtered, so every message counts as matched
    // and the flag has nothing to drop.
    //
    // This does NOT prove the haveMatchSet guard in loadThread: ref.matched is
    // already true for every message in this case, so removing that guard
    // leaves this passing, confirmed by mutation. It pins the BEHAVIOUR, which
    // is what a caller depends on, and the guard is a stated invariant rather
    // than a branch a test can reach.
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    QVERIFY(!threadId.isEmpty());

    const QVector<MessageRef> all = messagesOfThread(threadId, QString(), true);
    QCOMPARE(all.size(), 2);
}

void TestNotmuchWorker::recipientsAreAbsentUnlessAskedFor()
{
    // Opt-in, and this is a PERFORMANCE contract rather than a preference.
    // notmuch_message_get_header(m, "To") is not served from the index, it
    // reads the message file: measured 2026-08-11 against a real database,
    // folding every thread of a 4411-thread inbox took 38.2 seconds, 8.7 ms
    // per thread, against 1.1 ms per thread over the 601-thread sent view.
    //
    // A version that always folds is correct in every other respect, which is
    // exactly why it needs a test: nothing else here would notice.
    const QVector<ThreadSummary> threads =
        runQuery(QStringLiteral("subject:Preventivo"));

    QCOMPARE(threads.size(), 1);
    QVERIFY2(threads.at(0).recipients.isEmpty(),
             "the To header was read for a query that never asked for it");
}

void TestNotmuchWorker::recipientsAreFoldedWhenAskedFor()
{
    const QVector<ThreadSummary> one =
        runQuery(QStringLiteral("subject:Preventivo"),
                 NotmuchWorker::NewestFirst, true);
    QCOMPARE(one.size(), 1);
    QCOMPARE(one.at(0).recipients, QStringLiteral("Mario Rossi"));

    // The comma-inside-a-display-name case, end to end through the worker
    // rather than only against recipientSummary(): the header survives being
    // written to a real maildir, indexed, and read back out of notmuch.
    const QVector<ThreadSummary> many =
        runQuery(QStringLiteral("subject:Riunione"),
                 NotmuchWorker::NewestFirst, true);
    QCOMPARE(many.size(), 1);

    const QString summary = many.at(0).recipients;
    QVERIFY2(summary.startsWith(QStringLiteral("Rossi, Mario")),
             qPrintable(QStringLiteral("lost the quoted display name: %1")
                            .arg(summary)));
    QVERIFY2(summary.endsWith(QStringLiteral("+1")),
             qPrintable(QStringLiteral("three recipients did not collapse to "
                                       "two plus one: %1").arg(summary)));
}

void TestNotmuchWorker::recipientsCrossAQueuedCall()
{
    // The trap CLAUDE.md records for SortOrder, in the shape it takes for this
    // argument. A bool is a registered metatype already, so this cannot fail
    // the way an unregistered enum would, and the test exists to prove that
    // rather than to assume it: the flag arriving as a default-constructed
    // false would silently give an empty recipients column and nothing else.
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);

    QVERIFY(QMetaObject::invokeMethod(
        &worker, "runQuery", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("subject:Preventivo")),
        Q_ARG(quint64, 1),
        Q_ARG(NotmuchWorker::SortOrder, NotmuchWorker::NewestFirst),
        Q_ARG(bool, true)));

    QVector<ThreadSummary> all;
    for (const QList<QVariant> &args : ready)
        all += args.at(0).value<QVector<ThreadSummary>>();

    QCOMPARE(all.size(), 1);
    QVERIFY2(!all.at(0).recipients.isEmpty(),
             "the recipients flag was dropped crossing invokeMethod");
}

void TestNotmuchWorker::requestCountsAnswersOneCountPerQuery()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy spy(&worker, &NotmuchWorker::countsReady);

    worker.requestCounts({ QStringLiteral("tag:unread"),
                           QStringLiteral("tag:inbox"),
                           QStringLiteral("tag:flagged") }, 9);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).value<quint64>(), quint64(9));

    // Threads, not messages: thread A holds two messages and must count once,
    // which is the number the pane's "N in inbox" line claims to be showing.
    const QVector<int> counts = spy.at(0).at(0).value<QVector<int>>();
    QCOMPARE(counts, QVector<int>({ 1, 5, 0 }));
}

void TestNotmuchWorker::requestCountsKeepsPositionOnAnInvalidQuery()
{
    // The caller pairs answers with its own labels by index, so every query
    // must produce exactly one entry at its own position. Dropping one would
    // shift every later count onto the wrong label, and the pane would show a
    // real number against the wrong name rather than visibly breaking.
    //
    // **notmuch's query parser rejects almost nothing.** malformedQuery...
    // above records the same finding: an unbalanced quote parses and matches
    // nothing. `((((` behaves the same way and counts 0 rather than failing,
    // which is why this asserts the positional contract rather than a -1 that
    // no query string can actually provoke. The -1 branch remains for a
    // notmuch_query_create allocation failure, which a test cannot reach.
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy spy(&worker, &NotmuchWorker::countsReady);

    worker.requestCounts({ QStringLiteral("tag:unread"),
                           QStringLiteral("(((("),
                           QStringLiteral("tag:inbox") }, 1);

    QCOMPARE(spy.count(), 1);
    const QVector<int> counts = spy.at(0).at(0).value<QVector<int>>();
    QCOMPARE(counts.size(), 3);

    // The queries either side keep their own answers, which is the property
    // the pane depends on.
    QCOMPARE(counts.at(0), 1);
    QCOMPARE(counts.at(2), 5);
}

void TestNotmuchWorker::messageCountsCountMessagesNotThreads()
{
    // The fixture is 6 messages in 5 threads: thread A carries a reply, every
    // other thread is a single message. That difference is the whole reason
    // this slot exists beside requestCounts, and it is what the numbers below
    // assert. A rule that matched one reply of a 30-message thread would be
    // reported as 1 by a thread count, understating it by 29.
    NotmuchWorker worker(m_fixture.configPath());

    QSignalSpy messages(&worker, &NotmuchWorker::messageCountsReady);
    QSignalSpy threads(&worker, &NotmuchWorker::countsReady);

    worker.requestMessageCounts({ QStringLiteral("*") }, 1);
    worker.requestCounts({ QStringLiteral("*") }, 1);

    QCOMPARE(messages.count(), 1);
    QCOMPARE(threads.count(), 1);

    const QVector<int> messageCounts =
        messages.first().at(0).value<QVector<int>>();
    const QVector<int> threadCounts =
        threads.first().at(0).value<QVector<int>>();

    QCOMPARE(messageCounts, (QVector<int>{ 6 }));
    // The guard that makes this test mean something: if requestMessageCounts
    // were implemented with count_threads it would return 5 here and match
    // the thread count, and the assertion above would be the only thing that
    // caught it.
    QCOMPARE(threadCounts, (QVector<int>{ 5 }));
}

void TestNotmuchWorker::messageCountsReportAnInvalidQueryAsMinusOne()
{
    // Paired positionally with the caller's rules, so a dropped answer would
    // put a real number against the wrong rule.
    //
    // **notmuch's query parser rejects almost nothing**, exactly as
    // requestCountsKeepsPositionOnAnInvalidQuery records for the thread count:
    // `from:((((` parses and matches nothing rather than failing, measured at
    // 0 against a throwaway database rather than assumed. So this asserts the
    // positional contract, which is the property a dry run depends on, and not
    // a -1 that no query string can provoke. The -1 branch remains for a
    // notmuch_query_create allocation failure, which a test cannot reach.
    NotmuchWorker worker(m_fixture.configPath());

    QSignalSpy spy(&worker, &NotmuchWorker::messageCountsReady);
    worker.requestMessageCounts({ QStringLiteral("from:(((("),
                                  QStringLiteral("*") }, 1);

    QCOMPARE(spy.count(), 1);
    const QVector<int> counts = spy.first().at(0).value<QVector<int>>();
    QCOMPARE(counts.size(), 2);
    QCOMPARE(counts.at(0), 0);
    // The query beside it keeps its own answer at its own position, which is
    // what pairs a count with the rule that produced it.
    QCOMPARE(counts.at(1), 6);
}

void TestNotmuchWorker::requestDatabaseStatsCountsMessagesNotThreads()
{
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy spy(&worker, &NotmuchWorker::databaseStatsReady);

    worker.requestDatabaseStats(11);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).value<quint64>(), quint64(11));
    const auto stats = spy.at(0).at(0).value<DatabaseStats>();

    // The fixture holds four messages in three threads: thread A is a message
    // and its reply. **That difference is the whole point of this call.**
    // requestCounts() counts threads, to match the row count of a query; this
    // one counts messages, which is what a user means by "how much mail". A
    // reimplementation that reused the thread count would report 3 here and be
    // confidently wrong under the label "messages".
    QCOMPARE(stats.messages, 6);
    QCOMPARE(stats.threads, 5);
    QVERIFY2(stats.messages != stats.threads,
             "messages and threads are equal, so this fixture cannot prove the "
             "two counts are distinct: add a reply to it");

    // Every tag the fixture creates, plus notmuch's own.
    QVERIFY(stats.tags > 0);
}

void TestNotmuchWorker::requestDatabaseStatsOnUnreadableConfigEmitsError()
{
    // Fails closed like every other entry point. The dialog then shows its
    // fields as unknown rather than as zero, since "no mail at all" is the
    // wrong thing to tell someone whose index failed to open.
    NotmuchWorker worker(QStringLiteral("/nonexistent/qtmaildir-test/config"));
    QSignalSpy ready(&worker, &NotmuchWorker::databaseStatsReady);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.requestDatabaseStats(1);

    QCOMPARE(errors.size(), 1);
    QVERIFY(ready.isEmpty());
}

void TestNotmuchWorker::requestFoldersListsEveryMaildirFolder()
{
    // Its own fixture rather than the shared one: this needs a NESTED folder,
    // which is the shape a real account has (<account>/Drafts, not a flat
    // "drafts"), and adding a message to the shared fixture would move seven
    // count assertions in other tests for nothing.
    NotmuchFixture fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(fixture.addMessage(QStringLiteral("work/INBOX"),
                               QStringLiteral("g1@example.org"),
                               QStringLiteral("Something"),
                               QStringLiteral("Alice <alice@example.org>"),
                               QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), false));
    QVERIFY(fixture.addMessage(QStringLiteral("work/Drafts"),
                               QStringLiteral("g2@example.org"),
                               QStringLiteral("Half written"),
                               QStringLiteral("You <you@example.org>"),
                               QStringLiteral("Tue, 2 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), false));
    QVERIFY2(fixture.index(), qPrintable(fixture.error()));

    // An EMPTY folder, created but never written to. mbsync makes these, and a
    // list derived from indexed messages would not offer it. A rule may
    // legitimately target a folder that has nothing in it yet.
    QDir dir;
    const QString empty = fixture.maildirPath() + QStringLiteral("/work/Archive");
    QVERIFY(dir.mkpath(empty + QStringLiteral("/cur")));
    QVERIFY(dir.mkpath(empty + QStringLiteral("/new")));
    QVERIFY(dir.mkpath(empty + QStringLiteral("/tmp")));

    NotmuchWorker worker(fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::foldersReady);

    worker.requestFolders();

    QCOMPARE(ready.count(), 1);
    const QStringList folders = ready.first().at(0).toStringList();

    // Paths relative to the database root, which is what a Folder term
    // compiles a path: against. Drafts is the whole point of the item: the
    // dialog used to offer one entry per account and nothing below it.
    QVERIFY(folders.contains(QStringLiteral("work/INBOX")));
    QVERIFY(folders.contains(QStringLiteral("work/Drafts")));
    QVERIFY(folders.contains(QStringLiteral("work/Archive")));

    // Not the maildir plumbing, which is not a folder anyone files mail into,
    // and not the account directory itself, which holds no cur/.
    QVERIFY(!folders.contains(QStringLiteral("work/INBOX/cur")));
    QVERIFY(!folders.contains(QStringLiteral("work/INBOX/new")));
    QVERIFY(!folders.contains(QStringLiteral("work")));

    // Sorted, so the dropdown does not reorder itself between openings with
    // the same tree on disk. QDir's own order is filesystem order.
    QStringList sorted = folders;
    sorted.sort();
    QCOMPARE(folders, sorted);
}

void TestNotmuchWorker::requestFoldersOnUnreadableConfigEmitsError()
{
    // Fails closed like every other entry point. The dialog then leaves the
    // dropdown as it was rather than emptying it, since an empty list reads as
    // "this account has no folders".
    NotmuchWorker worker(QStringLiteral("/nonexistent/qtmaildir-test/config"));
    QSignalSpy ready(&worker, &NotmuchWorker::foldersReady);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.requestFolders();

    QCOMPARE(errors.size(), 1);
    QVERIFY(ready.isEmpty());
}

void TestNotmuchWorker::moveMessagesRelocatesTheFile()
{
    const QString id = QStringLiteral("move1@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), id),
             qPrintable(m_fixture.error()));

    const QString before = fileOf(id);
    QVERIFY(!before.isEmpty());
    QVERIFY(QFile::exists(before));

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy moved(&worker, &NotmuchWorker::messagesMoved);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.moveMessages({ id }, QStringLiteral("trash"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().at(0).toStringList(), QStringList{ id });
    QCOMPARE(moved.first().at(1).toString(), QStringLiteral("trash"));

    // cur/, never new/: a file in new/ is re-announced as fresh mail by every
    // reader of the Maildir.
    //
    // Asserted on the DIRECTORY, not on the full path. The filename is
    // deliberately regenerated by the move (see
    // moveMessagesGivesTheFileAFreshMaildirName), so an assertion naming the
    // old filename here encoded the very bug that item fixes: it required the
    // name to be carried across, which is what produced duplicate mbsync UIDs
    // on real mail.
    const QString expectedDir =
        m_fixture.maildirPath() + QStringLiteral("/trash/cur");
    const QString after = fileOf(id);
    QVERIFY2(!after.isEmpty(), "the message is not in the database after the move");
    QCOMPARE(QFileInfo(after).absolutePath(), expectedDir);
    QVERIFY2(QFile::exists(after), qPrintable(after));
    QVERIFY(!QFile::exists(before));
}

void TestNotmuchWorker::moveMessagesReindexesAtTheNewPath()
{
    // The half a filesystem check cannot see. A moved file with a stale index
    // entry sits correctly on disk and is invisible to every query.
    const QString id = QStringLiteral("move2@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), id),
             qPrintable(m_fixture.error()));

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);
    worker.moveMessages({ id }, QStringLiteral("trash"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    const QVector<ThreadSummary> inTrash =
        runQuery(QStringLiteral("path:\"trash/**\" and id:%1").arg(id));
    QCOMPARE(inTrash.size(), 1);

    const QVector<ThreadSummary> inInbox =
        runQuery(QStringLiteral("path:\"inbox/**\" and id:%1").arg(id));
    QCOMPARE(inInbox.size(), 0);
}

void TestNotmuchWorker::moveMessagesKeepsTheMessagesTags()
{
    // The ordering test. notmuch_database_remove_message() removes the LAST
    // filename for a message id by deleting the whole database entry, tags
    // included, so the new path must be indexed before the old one is dropped.
    // The reverse order leaves the file correctly placed, findable by query,
    // and stripped of every tag the user ever put on it.
    const QString id = QStringLiteral("move3@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), id),
             qPrintable(m_fixture.error()));

    NotmuchWorker tagger(m_fixture.configPath());
    tagger.applyTags(TagChange{ { id },
                                { QStringLiteral("keepme") },
                                {},
                                QStringLiteral("Tag before moving") });
    QVERIFY(tagsOf(id).contains(QStringLiteral("keepme")));

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);
    worker.moveMessages({ id }, QStringLiteral("trash"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    const QStringList after = tagsOf(id);
    QVERIFY2(after.contains(QStringLiteral("keepme")),
             qPrintable(QStringLiteral("tags after the move: %1")
                            .arg(after.join(QLatin1Char(' ')))));
}

void TestNotmuchWorker::moveMessagesGivesTheFileAFreshMaildirName()
{
    // mbsync's manual, under "the more efficient default UID mapping scheme":
    // "it is important that the MUA renames files when moving them between
    // Maildir folders", and "the general expectation is that a completely new
    // filename is generated as if the message was new".
    //
    // qtmaildir is that MUA and did not rename. The `,U=<n>` infix is mbsync's
    // per-folder IMAP UID, so carrying it into another folder makes it a claim
    // about a folder the file is no longer in. Moving a message out and back
    // then reinserts a UID the server has since reassigned, and mbsync reports
    // `Maildir error: duplicate UID`.
    //
    // Measured on the user's real Maildir on 2026-08-19: four collisions in one
    // folder, eight distinct messages, from a move-out and restore made with
    // 0.26.0.
    const QString id = QStringLiteral("move5@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), id),
             qPrintable(m_fixture.error()));

    // A realistic mbsync name, which the fixture does not produce on its own:
    // <unique>,U=<n>:2,<flags>.
    const QString before = fileOf(id);
    QVERIFY(!before.isEmpty());
    const QString uidName = QFileInfo(before).absolutePath()
                            + QStringLiteral("/move5.example.org,U=42:2,S");
    QVERIFY2(QFile::rename(before, uidName), "could not stage a ,U= filename");
    QVERIFY(m_fixture.index());

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);
    worker.moveMessages({ id }, QStringLiteral("trash"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    const QString after = fileOf(id);
    QVERIFY2(!after.isEmpty(), "the message is not in the database after the move");
    QVERIFY(QFile::exists(after));

    const QString name = QFileInfo(after).fileName();
    // The point of the item: no UID infix survives the move.
    QVERIFY2(!name.contains(QStringLiteral(",U=")),
             qPrintable(QStringLiteral("the moved file kept a UID infix: %1")
                            .arg(name)));
    // And it is a genuinely new name rather than the old one with the infix
    // cut out, which is what mbsync calls the expectation.
    QVERIFY2(name != QStringLiteral("move5.example.org:2,S"),
             qPrintable(QStringLiteral("the name was only stripped, not "
                                       "regenerated: %1").arg(name)));
}

void TestNotmuchWorker::moveMessagesKeepsTheMaildirFlags()
{
    // A fresh name must NOT mean fresh state. The `:2,<flags>` suffix carries
    // seen, flagged and replied, and notmuch's maildir.synchronize_flags is
    // true on the user's setup, so dropping it would mark read mail unread and
    // lose Important on every message the user deletes.
    //
    // Asserted on the FLAGS rather than on the whole name, since the unique
    // part is expected to change and the flags are expected not to.
    const QString id = QStringLiteral("move6@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), id),
             qPrintable(m_fixture.error()));

    const QString before = fileOf(id);
    QVERIFY(!before.isEmpty());
    // Seen and Flagged, so a suffix that is dropped or truncated shows up.
    const QString staged = QFileInfo(before).absolutePath()
                           + QStringLiteral("/move6.example.org,U=7:2,FS");
    QVERIFY2(QFile::rename(before, staged), "could not stage a flagged filename");
    QVERIFY(m_fixture.index());

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);
    worker.moveMessages({ id }, QStringLiteral("trash"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    const QString after = fileOf(id);
    QVERIFY2(!after.isEmpty(), "the message is not in the database after the move");
    const QString name = QFileInfo(after).fileName();

    QVERIFY2(name.endsWith(QStringLiteral(":2,FS")),
             qPrintable(QStringLiteral("the move lost the maildir flags: %1")
                            .arg(name)));
    QVERIFY(!name.contains(QStringLiteral(",U=")));
}

void TestNotmuchWorker::moveMessagesRecoversWhenASyncRenamedTheFile()
{
    // Item 162. mbsync uploads a file and RENAMES it to record the server UID,
    // and notmuch keeps the pre-`U=` name until that sync's `notmuch new`
    // runs. moveMessages() then renames a path that no longer exists, reports
    // "Cannot move <file> to <folder>", and silently does nothing.
    //
    // The ordinary fixture layout cannot see this: nothing renames a file
    // underneath the index. Driving it means renaming the file WITHOUT
    // reindexing, which is exactly the window mbsync opens.
    const QString id = QStringLiteral("move-stale@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), id),
             qPrintable(m_fixture.error()));

    const QString indexed = fileOf(id);
    QVERIFY(!indexed.isEmpty());

    // mbsync's rename, and deliberately NO m_fixture.index() afterwards: the
    // database must still name the old path, which is the whole precondition.
    const QString renamed = QFileInfo(indexed).absolutePath()
                            + QStringLiteral("/move-stale.example.org,U=7:2,D");
    QVERIFY2(QFile::rename(indexed, renamed), "could not stage the sync rename");

    // The guard that proves this test can fail: without it, a fixture that
    // quietly reindexed would make the assertions below pass against the bug.
    QCOMPARE(fileOf(id), indexed);
    QVERIFY2(!QFile::exists(indexed), "the stale path should no longer exist");

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy moved(&worker, &NotmuchWorker::messagesMoved);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.moveMessages({ id }, QStringLiteral("trash"));

    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().at(0).toStringList(), QStringList{ id });

    // The file really moved, and the database followed it.
    const QString after = fileOf(id);
    QVERIFY2(!after.isEmpty(), "the message is not in the database after the move");
    QCOMPARE(QFileInfo(after).absolutePath(),
             m_fixture.maildirPath() + QStringLiteral("/trash/cur"));
    QVERIFY2(QFile::exists(after), qPrintable(after));
    QVERIFY(!QFile::exists(renamed));

    // The `,U=` infix must not be carried across the folder boundary: that is
    // what produced `Maildir error: duplicate UID` on real mail.
    QVERIFY(!QFileInfo(after).fileName().contains(QStringLiteral(",U=")));
}

void TestNotmuchWorker::moveMessagesStillReportsAMessageThatIsReallyGone()
{
    // The bounded half of the recovery above. A file that is genuinely absent,
    // rather than merely renamed, must still be REPORTED: recovering silently
    // from every missing path would turn a real defect into a move that
    // claims success and does nothing.
    const QString id = QStringLiteral("move-gone@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), id),
             qPrintable(m_fixture.error()));

    const QString indexed = fileOf(id);
    QVERIFY(!indexed.isEmpty());
    QVERIFY2(QFile::remove(indexed), "could not remove the file");

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy moved(&worker, &NotmuchWorker::messagesMoved);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.moveMessages({ id }, QStringLiteral("trash"));

    QCOMPARE(errors.size(), 1);
    // Nothing is claimed to have moved.
    QVERIFY(moved.isEmpty() || moved.first().at(0).toStringList().isEmpty());
}

void TestNotmuchWorker::twoMessagesMovedTogetherGetDistinctNames()
{
    // The generated name must be unique, since a collision is the entire class
    // of bug this change exists to remove: two files landing on one name means
    // one message silently overwrites the other.
    //
    // Two messages in ONE batch, which is the case a timestamp alone does not
    // cover: both are moved in the same second, so only the per-process counter
    // separates them. A generator using time and pid alone passes every other
    // test here and fails this one.
    const QString first = QStringLiteral("move7@example.org");
    const QString second = QStringLiteral("move8@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), first),
             qPrintable(m_fixture.error()));
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), second),
             qPrintable(m_fixture.error()));

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);
    worker.moveMessages({ first, second }, QStringLiteral("trash"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    const QString a = fileOf(first);
    const QString b = fileOf(second);
    QVERIFY2(!a.isEmpty() && !b.isEmpty(),
             "a message is missing from the database after the move");

    // Distinct names...
    QVERIFY2(QFileInfo(a).fileName() != QFileInfo(b).fileName(),
             qPrintable(QStringLiteral("both messages were named %1")
                            .arg(QFileInfo(a).fileName())));
    // ...and both files really are on disk, which is what a collision would
    // have destroyed. The name check alone would pass against one file that
    // overwrote the other if the database still named two paths.
    QVERIFY(QFile::exists(a));
    QVERIFY(QFile::exists(b));
}

void TestNotmuchWorker::moveMessagesReportsOnlyWhatMoved()
{
    // A stale id must not abort the batch, and must not be reported as moved
    // either: a caller that assumed the request succeeded would show a delete
    // that never happened.
    const QString id = QStringLiteral("move4@example.org");
    QVERIFY2(addMovableMessage(QStringLiteral("inbox"), id),
             qPrintable(m_fixture.error()));

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy moved(&worker, &NotmuchWorker::messagesMoved);

    worker.moveMessages({ QStringLiteral("nosuchmessage@example.org"), id },
                        QStringLiteral("trash"));

    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().at(0).toStringList(), QStringList{ id });

    const QVector<ThreadSummary> inTrash =
        runQuery(QStringLiteral("path:\"trash/**\" and id:%1").arg(id));
    QCOMPARE(inTrash.size(), 1);
}

void TestNotmuchWorker::indexDraftFileMakesAFileFindable()
{
    const QString id = QStringLiteral("draft1@example.org");
    const QString path = writeDraftFile(QStringLiteral("drafts"), id);
    QVERIFY(!path.isEmpty());

    // On disk but not indexed: no query sees it, which is item 158's defect.
    QCOMPARE(runQuery(QStringLiteral("id:%1").arg(id)).size(), 0);

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);
    worker.indexDraftFile(path);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    QCOMPARE(runQuery(QStringLiteral("id:%1").arg(id)).size(), 1);
}

void TestNotmuchWorker::indexDraftFileRemovesThePreviousFile()
{
    const QString first = QStringLiteral("draft2@example.org");
    const QString second = QStringLiteral("draft3@example.org");
    const QString firstPath = writeDraftFile(QStringLiteral("drafts"), first);
    QVERIFY(!firstPath.isEmpty());

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);
    worker.indexDraftFile(firstPath);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));
    QCOMPARE(runQuery(QStringLiteral("id:%1").arg(first)).size(), 1);

    // A rewrite: a new file (a fresh Message-ID) and the old one unlinked, as
    // DraftStore does on every autosave. The old entry must not linger.
    const QString secondPath = writeDraftFile(QStringLiteral("drafts"), second);
    QVERIFY(!secondPath.isEmpty());
    QVERIFY(QFile::remove(firstPath));

    worker.indexDraftFile(secondPath, firstPath);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));

    QCOMPARE(runQuery(QStringLiteral("id:%1").arg(second)).size(), 1);
    QCOMPARE(runQuery(QStringLiteral("id:%1").arg(first)).size(), 0);
}

void TestNotmuchWorker::removeIndexedFileDropsTheEntry()
{
    const QString id = QStringLiteral("draft4@example.org");
    const QString path = writeDraftFile(QStringLiteral("drafts"), id);
    QVERIFY(!path.isEmpty());

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);
    worker.indexDraftFile(path);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));
    QCOMPARE(runQuery(QStringLiteral("id:%1").arg(id)).size(), 1);

    // The send path unlinks the draft and drops its entry, so it does not
    // linger as a ghost until the next sync.
    QVERIFY(QFile::remove(path));
    worker.removeIndexedFile(path);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));
    QCOMPARE(runQuery(QStringLiteral("id:%1").arg(id)).size(), 0);
}


// Item 124. notmuch can put the Xapian index outside the mail root
// (`mail_root` + `path`), which is how the index moves to faster storage while
// the mail stays put. Under that layout `notmuch_database_get_path()` returns
// the INDEX directory, so any code treating it as the mail root composes paths
// into the wrong tree entirely.
//
// These three need `splitIndex()`, and that is the whole point: in the
// ordinary layout the index lives inside the mail root and both accessors
// return the same string, so a test written against it passes whichever one
// the code uses and a mutation stays green.

void TestNotmuchWorker::aSplitIndexStillResolvesTheMailRoot()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.isValid());
    fixture.splitIndex();
    QVERIFY(fixture.addMessage(QStringLiteral("work/INBOX"),
                               QStringLiteral("split1@example.org"),
                               QStringLiteral("Something"),
                               QStringLiteral("Alice <alice@example.org>"),
                               QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), false));
    QVERIFY2(fixture.index(), qPrintable(fixture.error()));

    // The fixture really did split them, or the rest proves nothing.
    QVERIFY2(!fixture.indexPath().startsWith(fixture.maildirPath()),
             "the fixture did not put the index outside the mail root");
    QVERIFY(QDir(fixture.indexPath() + QStringLiteral("/xapian")).exists());

    NotmuchWorker worker(fixture.configPath());
    QSignalSpy ready(&worker, &NotmuchWorker::threadsReady);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.runQuery(QStringLiteral("*"), 1, NotmuchWorker::NewestFirst, false);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));
    QCOMPARE(ready.size(), 1);

    const auto threads = ready.first().at(0).value<QVector<ThreadSummary>>();
    QCOMPARE(threads.size(), 1);

    // The path is stored relative to the MAIL ROOT. Resolved against the index
    // directory it comes back as a "../.." escape, which matches no account
    // prefix and leaves every row belonging to no account at all.
    const QString path = threads.first().firstMessagePath;
    QVERIFY2(!path.startsWith(QStringLiteral("..")),
             qPrintable(QStringLiteral("path escaped the mail root: %1").arg(path)));
    QVERIFY2(path.startsWith(QStringLiteral("work/INBOX/")),
             qPrintable(QStringLiteral("expected a work/INBOX path, got: %1").arg(path)));
}

void TestNotmuchWorker::aSplitIndexMovesIntoTheMaildirNotTheIndex()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.isValid());
    fixture.splitIndex();
    QVERIFY(fixture.addMessage(QStringLiteral("work/INBOX"),
                               QStringLiteral("split2@example.org"),
                               QStringLiteral("Doomed"),
                               QStringLiteral("Alice <alice@example.org>"),
                               QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), false));
    QVERIFY2(fixture.index(), qPrintable(fixture.error()));

    NotmuchWorker worker(fixture.configPath());
    QSignalSpy moved(&worker, &NotmuchWorker::messagesMoved);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.moveMessages({ QStringLiteral("split2@example.org") },
                        QStringLiteral("work/Trash"));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));
    QCOMPARE(moved.size(), 1);

    // The file must land in the MAILDIR's trash. Composed against the index
    // directory it lands inside the Xapian tree instead: outside the Maildir,
    // invisible to mbsync, and gone from every other client. That is item
    // 103's stranded mail with a new cause, which is why this assertion names
    // the index directory explicitly rather than only checking the good path.
    const QString expected =
        fixture.maildirPath() + QStringLiteral("/work/Trash/cur");
    QVERIFY2(!QDir(fixture.indexPath() + QStringLiteral("/work")).exists(),
             "the move created a folder inside the INDEX directory");

    const QString after =
        fileOf(QStringLiteral("split2@example.org"), fixture.configPath());
    QVERIFY2(!after.isEmpty(), "the message is not in the database after the move");
    QCOMPARE(QFileInfo(after).absolutePath(), expected);
    QVERIFY2(QFile::exists(after), qPrintable(after));
}

void TestNotmuchWorker::aSplitIndexListsTheMaildirsFolders()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.isValid());
    fixture.splitIndex();
    QVERIFY(fixture.addMessage(QStringLiteral("work/INBOX"),
                               QStringLiteral("split3@example.org"),
                               QStringLiteral("Something"),
                               QStringLiteral("Alice <alice@example.org>"),
                               QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), false));
    QVERIFY2(fixture.index(), qPrintable(fixture.error()));

    NotmuchWorker worker(fixture.configPath());
    QSignalSpy folders(&worker, &NotmuchWorker::foldersReady);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.requestFolders();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()));
    QCOMPARE(folders.size(), 1);

    // Scanned from the mail root. Scanned from the index directory the list is
    // empty, or worse, names Xapian's own subdirectories as mail folders.
    const QStringList found = folders.first().at(0).toStringList();
    QVERIFY2(found.contains(QStringLiteral("work/INBOX")),
             qPrintable(QStringLiteral("expected work/INBOX, got: %1")
                            .arg(found.join(QStringLiteral(", ")))));
    QVERIFY2(!found.contains(QStringLiteral("xapian")),
             "the index's own directory was listed as a mail folder");
}

QTEST_MAIN(TestNotmuchWorker)
#include "test_notmuchworker.moc"
