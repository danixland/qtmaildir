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

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include "tagcolors.h"
#include "threadlistmodel.h"

class TestThreadListModel : public QObject
{
    Q_OBJECT
private slots:
    void aRowLeavesTheViewWhenItLosesTheViewsTag();
    void rowsLosingTheTagAreRemovedInOneContiguousRun();
    void theTrashViewDrawsNoDoomedFill();
    void messageNodeHoldsDisplayFacts();
    void rootRowsSurviveTheTreeConversion();
    void repliesBecomeChildRowsUnderTheirThread();
    void messageRowsShowTheirOwnSenderAndSubject();
    void modelHasOneColumn();
    void aFlatThreadStillListsItsReplies();
    void theRootCardKnowsItsOwnMessage();
    void replyShowsOnlyItsOwnTags();
    void replySharingEveryThreadTagShowsNone();
    void reloadingAThreadReplacesItsRepliesRatherThanRepeatingThem();
    void anUnexpandedMultiMessageThreadOffersAnExpander();
    void scopeFollowsTheSelectedRowKind();
    void scopeCountsEveryMessageOfAnUnexpandedThread();
    void scopeHonoursAMixedSelectionWithoutEscalating();
    void startsEmpty();
    void accountKeysComeFromTheAccountTags();
    void accountKeysCoverAThreadSpanningTwoAccounts();
    void accountKeysAreEmptyForAnUnknownThread();
    void appendsBatches();
    void appendingEmptyBatchIsNoOp();
    void clearResetsModel();
    void reportsSubjectAndAuthors();
    void theReplyCountExcludesTheRootMessage();
    void unreadThreadsRenderBold();
    void readThreadsAreDimmedAndUnreadAreNot();
    void flaggedThreadsShowAStar();
    void pillTagsExcludeWhatTheRowAlreadyShows();
    void aTagDrawnAsAMarkIsNotAlsoAChip();
    void theUnreadCueDoesNotDependOnFontWeight();
    void aDoomedThreadKeepsItsContrastEvenWhenRead();
    void accountTagBecomesAChipLabel();
    void unreadStylingSurvivesAnAccountChip();
    void accountChipUsesTheConfiguredColour();
    void deletedThreadsAreRedAndStruckThrough();
    void attachmentIsMarkedOnlyOnTaggedThreads();
    void spamThreadsAreOrangeAndStruckThrough();
    void doomedStylingCoversTheWholeCard();
    void ordinaryThreadsCarryNoRowColour();
    void threadIdIsReachableFromAnIndex();
    void invalidIndexesReturnNothing();
    void threadAtOutOfRangeIsSafe();
    void threadForResolvesAReplyThroughItsParent();
    void applyMessageTagChangeRepaintsThatReplyAlone();
    void aDeletedReplyIsPaintedAsDoomed();
    void aDeletedReplyIsStruckThrough();
    void markingAReplyReadChangesItsForeground();
    void anUnreadReplyIsBoldAndStillSmallerThanItsThread();
    void aThreadTagChangeReachesItsLoadedReplies();
    void messageScopeResolvesAThreadRowToTheMessageItDisplays();
    void messageScopeSkipsAThreadRowItCannotNameAMessageFor();
    void aMessageTagChangeReachesTheRootCardsOwnMessage();
    void aMessageTagChangeOnOneOfManyLeavesTheThreadSummaryAlone();
    void aCardListsItsOwnTagsBeforeItsSiblings();
    void theSplitIsKnownBeforeTheRowIsEverOpened();
    void reconcileRefreshesASurvivorsOwnMessageTags();
    void updatesTagsForMessage();
    void tagChangeIsIdempotent();
    void tagChangeSignalsExactlyTheChangedRow();
    void tagChangeForUnknownThreadIsIgnored();
    void tagChangeRoundTripsForRevert();
    void modelPassesQtTester();
    void reconcileAddsNewThreadsInTheOrderGiven();
    void reconcileRemovesThreadsThatNoLongerMatch();
    void reconcileKeepsSurvivingRowsAndTheirExpansion();
    void reconcileUpdatesTagsOnASurvivingThread();
    void reconcileOnAnEmptyModelFillsIt();
    void reconcileWithAnIdenticalResultChangesNothing();
    void reconcileMovesAThreadBumpedByANewReply();
    void reconcileKeepsAMovedRowsPersistentIndex();
    void flatModeOffersNoExpanderAndNoReplyCount();
    void flatModeIsOffByDefaultAndReversible();
    void recipientsReplaceTheSenderWhenPresent();
    void aRowCarriesItsSenderAndAccountAddress();
    void aMessageRowCarriesItsOwnSenderAndAddress();
    void aFlatViewsAvatarFollowsTheRecipient();
    void aSummaryWithOneMessageIsAMessageRow();
    void aSummaryWithRepliesIsAConversationRow();
    void aLoadedThreadTrustsItsChildrenOverItsCount();
    void aMessageRowIsNeverAConversationRow();
};

static ThreadSummary makeThread(const QString &id, const QString &subject)
{
    ThreadSummary t;
    t.threadId = id;
    t.subject = subject;
    t.authors = QStringLiteral("Alice");
    t.date = QDateTime::fromSecsSinceEpoch(1750000000);
    t.totalCount = 2;
    t.matchedCount = 1;
    t.tags = QStringList{ QStringLiteral("inbox"), QStringLiteral("unread") };
    return t;
}

static MessageNode makeNode(const QString &id, int depth,
                            const QString &from = QStringLiteral("Alice"),
                            const QString &subject = QStringLiteral("Re: Hi"))
{
    MessageNode n;
    n.messageId = id;
    n.threadId = QStringLiteral("t1");
    n.from = from;
    n.subject = subject;
    n.date = QDateTime::fromSecsSinceEpoch(1750000000);
    n.depth = depth;
    return n;
}

void TestThreadListModel::repliesBecomeChildRowsUnderTheirThread()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("A subject")) });

    // Depth 0 is the thread's FIRST message and belongs on the root row, not in
    // the children: the user's model is "N replies", so a thread of three shows
    // one root and two children.
    model.setThreadMessages(QStringLiteral("t1"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              makeNode(QStringLiteral("m1@example.org"), 1),
                              makeNode(QStringLiteral("m2@example.org"), 2) });

    const QModelIndex root = model.index(0, 0, QModelIndex());
    QCOMPARE(model.rowCount(root), 2);

    const QModelIndex child =
        model.index(0, 0, root);
    QVERIFY(child.isValid());
    QCOMPARE(model.parent(child), model.index(0, 0, QModelIndex()));

    QVERIFY(model.data(child, ThreadListModel::IsMessageRole).toBool());
    QCOMPARE(model.data(child, ThreadListModel::MessageIdRole).toString(),
             QStringLiteral("m1@example.org"));

    // A message row still belongs to a thread, so a caller that only needs the
    // containing thread does not have to walk up itself.
    QCOMPARE(model.data(child, ThreadListModel::ThreadIdRole).toString(),
             QStringLiteral("t1"));

    // A thread root is not a message ROW, but it does carry a message id: the
    // root card is the thread's first message, and selecting it renders that
    // message alone. It used to answer nothing here, which is what made the
    // first message of every thread unreachable.
    QVERIFY(!model.data(root, ThreadListModel::IsMessageRole).toBool());
    QCOMPARE(model.data(root, ThreadListModel::MessageIdRole).toString(),
             QStringLiteral("m0@example.org"));

    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Warning);
    Q_UNUSED(tester);
}

void TestThreadListModel::messageRowsShowTheirOwnSenderAndSubject()
{
    // A reply's row shows the REPLY's sender, not the thread's author summary.
    // Reading the thread's fields for a child row is the obvious mistake and
    // would look almost right, since the first sender is usually in both.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("A subject")) });
    model.setThreadMessages(
        QStringLiteral("t1"),
        { makeNode(QStringLiteral("m0@example.org"), 0),
          makeNode(QStringLiteral("m1@example.org"), 1,
                   QStringLiteral("Bob <bob@example.org>"),
                   QStringLiteral("Re: A subject")) });

    const QModelIndex root = model.index(0, 0, QModelIndex());
    const QModelIndex reply = model.index(0, 0, root);

    QCOMPARE(model.data(reply, ThreadListModel::SendersRole).toString(),
             QStringLiteral("Bob <bob@example.org>"));
    QCOMPARE(model.data(reply, ThreadListModel::SubjectRole).toString(),
             QStringLiteral("Re: A subject"));

    // No tag strip under a child row. The strip is a row-wide band carrying the
    // THREAD's tags; one under every reply would stripe the list and repeat the
    // same tags down the whole expansion.
    QVERIFY(model.data(reply, ThreadListModel::PillTagsRole)
                .toStringList().isEmpty());
}

void TestThreadListModel::reloadingAThreadReplacesItsRepliesRatherThanRepeatingThem()
{
    // A thread reloaded after a sync must not end up listing its replies twice.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("A subject")) });

    const QVector<MessageNode> nodes{
        makeNode(QStringLiteral("m0@example.org"), 0),
        makeNode(QStringLiteral("m1@example.org"), 1)
    };

    model.setThreadMessages(QStringLiteral("t1"), nodes);
    const QModelIndex root = model.index(0, 0, QModelIndex());
    QCOMPARE(model.rowCount(root), 1);

    model.setThreadMessages(QStringLiteral("t1"), nodes);
    QCOMPARE(model.rowCount(root), 1);

    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Warning);
    Q_UNUSED(tester);
}

void TestThreadListModel::anUnexpandedMultiMessageThreadOffersAnExpander()
{
    // This is what makes lazy loading work at all. rowCount is 0 until the
    // worker has walked the thread, so a view inferring the expander from
    // rowCount alone draws none, the user can never expand, and the replies are
    // never requested. hasChildren answers from the summary's count instead.
    ThreadListModel model;
    ThreadSummary many = makeThread(QStringLiteral("t1"),
                                    QStringLiteral("Has replies"));
    many.totalCount = 4;
    ThreadSummary lone = makeThread(QStringLiteral("t2"),
                                    QStringLiteral("Single message"));
    lone.totalCount = 1;
    model.appendBatch({ many, lone });

    const QModelIndex withReplies = model.index(0, 0, QModelIndex());
    const QModelIndex single = model.index(1, 0, QModelIndex());

    // Guard: neither is expanded, so this really is the unloaded case.
    QCOMPARE(model.rowCount(withReplies), 0);
    QCOMPARE(model.rowCount(single), 0);

    QVERIFY(model.hasChildren(withReplies));
    QVERIFY(!model.hasChildren(single));

    // Once loaded the children are the truth, including "there are none": a
    // thread whose count included duplicates must stop offering an expander
    // that opens onto nothing.
    model.setThreadMessages(QStringLiteral("t1"),
                            { makeNode(QStringLiteral("m0@example.org"), 0) });
    QVERIFY(!model.hasChildren(withReplies));

    // A message row is always a leaf.
    model.setThreadMessages(QStringLiteral("t1"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              makeNode(QStringLiteral("m1@example.org"), 1) });
    QVERIFY(model.hasChildren(withReplies));
    QVERIFY(!model.hasChildren(model.index(0, 0, withReplies)));
}

void TestThreadListModel::scopeFollowsTheSelectedRowKind()
{
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"),
                                 QStringLiteral("A subject"));
    t.totalCount = 3;
    model.appendBatch({ t });
    model.setThreadMessages(QStringLiteral("t1"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              makeNode(QStringLiteral("m1@example.org"), 1) });

    const QModelIndex root = model.index(0, 0, QModelIndex());
    const QModelIndex child = model.index(0, 0, root);

    // A thread root acts on the whole thread, and reports every message it
    // stands for so the status bar can say so.
    const ActionScope threadScope = model.scopeFor({ root });
    QCOMPARE(threadScope.threadIds, QStringList{ QStringLiteral("t1") });
    QVERIFY(threadScope.messageIds.isEmpty());
    QCOMPARE(threadScope.messageCount, 3);
    QVERIFY(threadScope.wholeThread);

    // A message row acts on that message alone.
    const ActionScope messageScope = model.scopeFor({ child });
    QVERIFY(messageScope.threadIds.isEmpty());
    QCOMPARE(messageScope.messageIds,
             QStringList{ QStringLiteral("m1@example.org") });
    QCOMPARE(messageScope.messageCount, 1);
    QVERIFY(!messageScope.wholeThread);
}

void TestThreadListModel::scopeCountsEveryMessageOfAnUnexpandedThread()
{
    // totalCount, not the loaded children. A thread that was never expanded
    // still has all of its messages, and counting only what happens to be on
    // screen would understate what the action is about to do.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"),
                                 QStringLiteral("A subject"));
    t.totalCount = 7;
    model.appendBatch({ t });

    const QModelIndex root = model.index(0, 0, QModelIndex());
    QCOMPARE(model.rowCount(root), 0);  // guard: nothing expanded

    const ActionScope scope = model.scopeFor({ root });
    QCOMPARE(scope.messageCount, 7);
}

void TestThreadListModel::scopeHonoursAMixedSelectionWithoutEscalating()
{
    // Selecting a thread root and an unrelated reply acts on that whole thread
    // AND that one message. Nothing is escalated to thread scope or narrowed to
    // message scope silently, which is the point of the scope being visible.
    ThreadListModel model;
    ThreadSummary t1 = makeThread(QStringLiteral("t1"), QStringLiteral("One"));
    t1.totalCount = 2;
    ThreadSummary t2 = makeThread(QStringLiteral("t2"), QStringLiteral("Two"));
    t2.totalCount = 5;
    model.appendBatch({ t1, t2 });

    MessageNode reply = makeNode(QStringLiteral("m1@example.org"), 1);
    reply.threadId = QStringLiteral("t2");
    model.setThreadMessages(QStringLiteral("t2"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              reply });

    const QModelIndex firstRoot = model.index(0, 0, QModelIndex());
    const QModelIndex secondRoot = model.index(1, 0, QModelIndex());
    const QModelIndex reply1 = model.index(0, 0, secondRoot);

    const ActionScope scope = model.scopeFor({ firstRoot, reply1 });
    QCOMPARE(scope.threadIds, QStringList{ QStringLiteral("t1") });
    QCOMPARE(scope.messageIds, QStringList{ QStringLiteral("m1@example.org") });

    // 2 from the whole thread plus 1 for the lone message.
    QCOMPARE(scope.messageCount, 3);
    QVERIFY(scope.wholeThread);
}

void TestThreadListModel::accountKeysComeFromTheAccountTags()
{
    // Item 49 reads this to decide which mbsync channels a sync needs. Only
    // account tags count: a functional tag names no mailbox.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"), QStringLiteral("Hi"));
    t.tags.append(TagColors::tagForAccountKey(QStringLiteral("work")));
    model.appendBatch({ t });

    QCOMPARE(model.accountKeysForThread(QStringLiteral("t1")),
             QStringList{ QStringLiteral("work") });
}

void TestThreadListModel::accountKeysCoverAThreadSpanningTwoAccounts()
{
    // The row shows one chip, but tagging this thread touches files under both
    // mailboxes. Returning only the first would strand the other's edits.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"), QStringLiteral("Hi"));
    t.tags.append(TagColors::tagForAccountKey(QStringLiteral("work")));
    t.tags.append(TagColors::tagForAccountKey(QStringLiteral("personal")));
    model.appendBatch({ t });

    QStringList keys = model.accountKeysForThread(QStringLiteral("t1"));
    keys.sort();
    QCOMPARE(keys, (QStringList{ QStringLiteral("personal"),
                                 QStringLiteral("work") }));
}

void TestThreadListModel::accountKeysAreEmptyForAnUnknownThread()
{
    // A thread the model no longer holds must yield nothing rather than
    // matching some other row.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("Hi")) });

    QVERIFY(model.accountKeysForThread(QStringLiteral("nope")).isEmpty());
}

void TestThreadListModel::messageNodeHoldsDisplayFacts()
{
    // A message ROW has to be drawn without opening the message, so the display
    // facts live on the node itself. MessageRef, which exists for rendering a
    // thread into the pane, carries none of them.
    MessageNode node;
    node.messageId = QStringLiteral("id@example.org");
    node.from = QStringLiteral("A Sender <sender@example.org>");
    node.subject = QStringLiteral("Re: a subject");
    node.date = QDateTime::fromSecsSinceEpoch(1000);
    node.depth = 2;
    node.tags = QStringList{ QStringLiteral("unread") };

    QCOMPARE(node.depth, 2);
    QVERIFY(node.isUnread());
    QCOMPARE(node.from, QStringLiteral("A Sender <sender@example.org>"));
    QCOMPARE(node.subject, QStringLiteral("Re: a subject"));

    // Depth 0 is the thread's first message, which the ROOT row stands for.
    // Defaulting to 0 rather than 1 keeps "is this the root" a plain check.
    const MessageNode fresh;
    QCOMPARE(fresh.depth, 0);
    QVERIFY(!fresh.isUnread());
}

void TestThreadListModel::rootRowsSurviveTheTreeConversion()
{
    // The point of this test is NOT the tree. It is that converting the base
    // class from QAbstractTableModel changed nothing a thread row does: a table
    // answers index() and parent() too, just trivially, and every existing test
    // in this file is the real regression net beside it.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("A subject")) });

    // A tree model reports its roots under an INVALID parent.
    QCOMPARE(model.rowCount(QModelIndex()), 1);
    QCOMPARE(model.columnCount(QModelIndex()), 1);

    const QModelIndex root =
        model.index(0, 0, QModelIndex());
    QVERIFY(root.isValid());
    QVERIFY(!model.parent(root).isValid());
    QCOMPARE(model.data(root, ThreadListModel::ThreadIdRole).toString(),
             QStringLiteral("t1"));

    // No children until a thread's messages are asked for. An expander drawn
    // over a thread whose replies were never loaded would open onto nothing.
    QCOMPARE(model.rowCount(root), 0);

    // Qt's own conformance check. It walks index/parent/rowCount for
    // consistency and catches the classic tree-model faults, such as a parent()
    // that does not round-trip, which a hand-written assertion misses.
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Warning);
    Q_UNUSED(tester);
}

void TestThreadListModel::startsEmpty()
{
    ThreadListModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 1);
}

void TestThreadListModel::appendsBatches()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });
    QCOMPARE(model.rowCount(), 1);

    model.appendBatch({ makeThread(QStringLiteral("t2"), QStringLiteral("two")),
                        makeThread(QStringLiteral("t3"), QStringLiteral("three")) });
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.threadAt(2).threadId, QStringLiteral("t3"));
}

void TestThreadListModel::appendingEmptyBatchIsNoOp()
{
    ThreadListModel model;
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

    model.appendBatch({});

    QCOMPARE(model.rowCount(), 0);
    // An empty beginInsertRows(first, first - 1) range is a Qt contract
    // violation, so the guard must come before the signal.
    QVERIFY(inserted.isEmpty());
}

void TestThreadListModel::clearResetsModel()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    QSignalSpy spy(&model, &QAbstractItemModel::modelReset);
    model.clear();

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(spy.count(), 1);
}

void TestThreadListModel::reportsSubjectAndAuthors()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("hello")) });

    // One index, every field, by role. The card draws them all at once, so
    // reading them through Qt::DisplayRole as five columns did is no longer
    // possible: DisplayRole answers the subject alone.
    const QModelIndex card = model.index(0, 0);
    QCOMPARE(model.data(card, ThreadListModel::SendersRole).toString(),
             QStringLiteral("Alice"));
    QVERIFY(model.data(card, ThreadListModel::DateRole).toDateTime().isValid());
    QCOMPARE(model.data(card, ThreadListModel::SubjectRole).toString(),
             QStringLiteral("hello"));

    QCOMPARE(model.data(card, ThreadListModel::TagsRole).toStringList(),
             QStringList({ QStringLiteral("inbox"), QStringLiteral("unread") }));
}

void TestThreadListModel::aRowCarriesItsSenderAndAccountAddress()
{
    // Task 8: the avatar needs the row's bare sender address to hash and to
    // match against the business-senders list, and the display name to take
    // initials from. Both come from the row itself, not from the load.
    ThreadListModel model;
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.subject = QStringLiteral("Subject");
    summary.authors = QStringLiteral("John Doe");
    summary.firstMessageId = QStringLiteral("m1");
    summary.firstMessageSender = QStringLiteral("john@example.org");
    model.appendBatch({ summary });

    const QModelIndex index = model.index(0, 0);
    QCOMPARE(index.data(ThreadListModel::SenderAddressRole).toString(),
             QStringLiteral("john@example.org"));
    // The display name comes from `authors`, which is all notmuch gives.
    QCOMPARE(index.data(ThreadListModel::SenderNameRole).toString(),
             QStringLiteral("John Doe"));
}

void TestThreadListModel::aMessageRowCarriesItsOwnSenderAndAddress()
{
    // Task 8 counterpart of aRowCarriesItsSenderAndAccountAddress: that test
    // covers the thread-row branch, and a role added to one branch and not the
    // other is silently absent with nothing to flag it. A selected reply's
    // avatar reads these, so the row that actually answers must carry them.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("A subject")) });

    MessageNode root = makeNode(QStringLiteral("m0@example.org"), 0);
    MessageNode reply = makeNode(QStringLiteral("m1@example.org"), 1,
                                 QStringLiteral("Bob <bob@example.org>"));
    reply.senderAddress = QStringLiteral("bob@example.org");
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex replyIndex =
        model.index(0, 0, model.index(0, 0, QModelIndex()));
    QVERIFY(model.isMessageRow(replyIndex));

    QCOMPARE(replyIndex.data(ThreadListModel::SenderAddressRole).toString(),
             QStringLiteral("bob@example.org"));
    QCOMPARE(replyIndex.data(ThreadListModel::SenderNameRole).toString(),
             QStringLiteral("Bob <bob@example.org>"));
}

void TestThreadListModel::aFlatViewsAvatarFollowsTheRecipient()
{
    // In a Sent or Drafts view firstMessageSender is the USER on every row, so
    // hashing it gives one pattern for the whole list. The recipient is what
    // the row is about, and SendersRole already follows the same rule.
    ThreadListModel model;
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.subject = QStringLiteral("Subject");
    summary.authors = QStringLiteral("Me");
    summary.firstMessageId = QStringLiteral("m1");
    summary.firstMessageSender = QStringLiteral("me@example.org");
    summary.recipients = QStringLiteral("John Doe");
    summary.firstMessageRecipient = QStringLiteral("john@example.org");
    model.appendBatch({ summary });

    QCOMPARE(model.index(0, 0).data(ThreadListModel::SenderAddressRole)
                 .toString(),
             QStringLiteral("john@example.org"));

    // No usable To: the sender is the fallback rather than a blank seed.
    ThreadListModel bare;
    summary.recipients.clear();
    summary.firstMessageRecipient.clear();
    bare.appendBatch({ summary });
    QCOMPARE(bare.index(0, 0).data(ThreadListModel::SenderAddressRole)
                 .toString(),
             QStringLiteral("me@example.org"));
}

void TestThreadListModel::theReplyCountExcludesTheRootMessage()
{
    ThreadListModel model;

    ThreadSummary single = makeThread(QStringLiteral("t1"), QStringLiteral("alone"));
    single.totalCount = 1;
    ThreadSummary multi = makeThread(QStringLiteral("t2"), QStringLiteral("group"));
    multi.totalCount = 4;
    model.appendBatch({ single, multi });

    // The count used to be a "(4)" suffix on the subject. It is the expander
    // on the card's second line now, and it counts REPLIES: totalCount
    // includes the root message, which is the card itself.
    QCOMPARE(model.data(model.index(0, 0),
                        ThreadListModel::ReplyCountRole).toInt(), 0);
    QCOMPARE(model.data(model.index(1, 0),
                        ThreadListModel::ReplyCountRole).toInt(), 3);

    // And the subject is bare, with no count spliced into it.
    QCOMPARE(model.data(model.index(1, 0),
                        ThreadListModel::SubjectRole).toString(),
             QStringLiteral("group"));
}

void TestThreadListModel::unreadThreadsRenderBold()
{
    ThreadListModel model;
    ThreadSummary read = makeThread(QStringLiteral("t1"), QStringLiteral("read"));
    read.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ read, makeThread(QStringLiteral("t2"), QStringLiteral("unread")) });

    const QVariant readFont =
        model.data(model.index(0, 0), Qt::FontRole);
    QVERIFY(!readFont.isValid());

    const QVariant unreadFont =
        model.data(model.index(1, 0), Qt::FontRole);
    QVERIFY(unreadFont.isValid());
    QVERIFY(unreadFont.value<QFont>().bold());
}

void TestThreadListModel::readThreadsAreDimmedAndUnreadAreNot()
{
    // Bold was unread's ONLY cue, which leaves nothing to see when the
    // desktop's own font is configured bold: every row renders bold and
    // setBold() changes nothing. That is what the original report turned out
    // to be, a qt6ct setting rather than a defect here, but a cue with one
    // point of failure is worth reinforcing.
    //
    // Read rows are dimmed as well, which inverts the emphasis: unread sits at
    // full contrast and the bulk of a mostly-read list recedes. Bold still
    // applies on top.
    ThreadListModel model;
    ThreadSummary read = makeThread(QStringLiteral("t1"), QStringLiteral("read"));
    read.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch(
        { read, makeThread(QStringLiteral("t2"), QStringLiteral("unread")) });

    const QVariant readFg =
        model.data(model.index(0, 0),
                   Qt::ForegroundRole);
    const QVariant unreadFg =
        model.data(model.index(1, 0),
                   Qt::ForegroundRole);

    QVERIFY2(readFg.isValid(), "a read thread carries no dimming");
    QVERIFY2(!unreadFg.isValid(),
             "an unread thread must be left at the palette's own colour, so it "
             "is the one that stands out");
}

void TestThreadListModel::flaggedThreadsShowAStar()
{
    // "flagged" is an ordinary notmuch tag already carried in ThreadSummary,
    // so this needs no worker query, exactly as the paperclip did not.
    ThreadListModel model;
    ThreadSummary plain = makeThread(QStringLiteral("t1"), QStringLiteral("plain"));
    plain.tags = QStringList{ QStringLiteral("inbox") };
    ThreadSummary starred = makeThread(QStringLiteral("t2"),
                                       QStringLiteral("starred"));
    starred.tags = QStringList{ QStringLiteral("inbox"),
                                QStringLiteral("flagged") };
    model.appendBatch({ plain, starred });

    QVERIFY2(!model.data(model.index(0, 0),
                         ThreadListModel::IsFlaggedRole).toBool(),
             "an unflagged thread reports itself flagged");
    QVERIFY2(model.data(model.index(1, 0),
                        ThreadListModel::IsFlaggedRole).toBool(),
             "a flagged thread does not report itself flagged");

}

void TestThreadListModel::pillTagsExcludeWhatTheRowAlreadyShows()
{
    // The pills exist to say what the row does not already say. Repeating the
    // account, the flag, the attachment or the read state as text beside the
    // chip, the star, the paperclip and the dimming would spend the new space
    // on things already visible.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"),
                                      QStringLiteral("noisy"));
    thread.tags = QStringList{
        QStringLiteral("inbox"),      // structural, always true here
        QStringLiteral("unread"),     // shown by not being dimmed
        QStringLiteral("flagged"),    // shown by the star column
        QStringLiteral("attachment"), // shown by the paperclip column
        QStringLiteral("account-work"),  // shown as the chip
        QStringLiteral("SBo"),        // worth showing
        QStringLiteral("shopping/amazon"),
    };
    model.appendBatch({ thread });

    const QStringList pills =
        model.data(model.index(0, 0),
                   ThreadListModel::PillTagsRole).toStringList();

    QVERIFY2(pills.contains(QStringLiteral("SBo")), qPrintable(pills.join(',')));
    QVERIFY2(pills.contains(QStringLiteral("shopping/amazon")),
             qPrintable(pills.join(',')));

    for (const QString &hidden : { QStringLiteral("inbox"),
                                   QStringLiteral("unread"),
                                   QStringLiteral("flagged"),
                                   QStringLiteral("attachment") }) {
        QVERIFY2(!pills.contains(hidden),
                 qPrintable(QStringLiteral("'%1' is repeated as a pill")
                                .arg(hidden)));
    }

    // The account tag is matched by shape rather than by name, since the key
    // varies per user: whatever TagColors calls an account tag is excluded.
    for (const QString &tag : pills) {
        QVERIFY2(!TagColors::isAccountTag(tag),
                 qPrintable(QStringLiteral("account tag '%1' repeated as a pill")
                                .arg(tag)));
    }

    // Stable order, so a row does not reshuffle its own pills between repaints.
    QStringList sorted = pills;
    sorted.sort();
    QCOMPARE(pills, sorted);
}

void TestThreadListModel::theUnreadCueDoesNotDependOnFontWeight()
{
    // The property that matters, stated directly: strip every font from the
    // model's answer and the two states must still be distinguishable. A test
    // asserting only that bold is set passes on a system where bold paints
    // exactly like regular, which is precisely how this went unnoticed.
    ThreadListModel model;
    ThreadSummary read = makeThread(QStringLiteral("t1"), QStringLiteral("read"));
    read.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch(
        { read, makeThread(QStringLiteral("t2"), QStringLiteral("unread")) });

    const QVariant readFg =
        model.data(model.index(0, 0), Qt::ForegroundRole);
    const QVariant unreadFg =
        model.data(model.index(1, 0), Qt::ForegroundRole);

    QVERIFY2(readFg != unreadFg,
             "read and unread cards render identically once the font is "
             "ignored");
}

void TestThreadListModel::aDoomedThreadKeepsItsContrastEvenWhenRead()
{
    // Both cues write ForegroundRole, so they share one channel and the order
    // matters. A deleted row forces white text onto its crimson fill; dimming
    // it because it also happens to be read would drop that contrast to
    // unreadable.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"),
                                      QStringLiteral("doomed and read"));
    thread.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ thread });

    model.applyTagChange(QStringLiteral("t1"), { QStringLiteral("deleted") }, {});

    const QModelIndex subject = model.index(0, 0);
    QCOMPARE(model.data(subject, Qt::ForegroundRole).value<QBrush>().color(),
             QColor(Qt::white));
}

void TestThreadListModel::accountTagBecomesAChipLabel()
{
    // The account tag is a different taxonomy from a functional one: which
    // mailbox the thread arrived in. It renders as a chip in front of the
    // subject, so the model exposes its label and colour separately.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"), QStringLiteral("hello"));
    thread.tags = QStringList{ QStringLiteral("inbox"),
                               QStringLiteral("account-webmail-personal") };
    model.appendBatch({ thread });

    const QModelIndex subject = model.index(0, 0);
    QCOMPARE(model.data(subject, ThreadListModel::AccountLabelRole).toString(),
             QStringLiteral("webmail-personal"));
    QVERIFY(model.data(subject, ThreadListModel::AccountColourRole)
                .value<QColor>().isValid());

    // A thread with no account tag gets no chip rather than an empty one.
    ThreadListModel plain;
    ThreadSummary untagged = makeThread(QStringLiteral("t2"), QStringLiteral("hi"));
    untagged.tags = QStringList{ QStringLiteral("inbox") };
    plain.appendBatch({ untagged });
    QVERIFY(plain.data(plain.index(0, 0),
                       ThreadListModel::AccountLabelRole).toString().isEmpty());
}

void TestThreadListModel::unreadStylingSurvivesAnAccountChip()
{
    // The subject cell is drawn by a delegate when the thread has an account
    // chip. The delegate paints the text itself, so it has to keep honouring
    // the model's font: otherwise an unread thread stops rendering bold for
    // exactly those threads that carry an account tag, which is all of them.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"), QStringLiteral("hello"));
    thread.tags = QStringList{ QStringLiteral("inbox"), QStringLiteral("unread"),
                               QStringLiteral("account-webmail-personal") };
    model.appendBatch({ thread });

    const QModelIndex subject = model.index(0, 0);
    QVERIFY(!model.data(subject, ThreadListModel::AccountLabelRole)
                 .toString().isEmpty());

    const QVariant font = model.data(subject, Qt::FontRole);
    QVERIFY2(font.isValid(), "unread thread with an account tag has no font");
    QVERIFY2(font.value<QFont>().bold(), "unread thread is not bold");
}

void TestThreadListModel::accountChipUsesTheConfiguredColour()
{
    // The colour comes from the account's own stanza, so a configured one must
    // reach the chip rather than the generated fallback.
    TagColors colours;
    colours.setAccountColour(QStringLiteral("webmail-personal"),
                             QColor(QStringLiteral("#cc0000")));

    ThreadListModel model;
    model.setTagColors(&colours);
    ThreadSummary thread = makeThread(QStringLiteral("t1"), QStringLiteral("hello"));
    thread.tags = QStringList{ QStringLiteral("account-webmail-personal") };
    model.appendBatch({ thread });

    QCOMPARE(model.data(model.index(0, 0),
                        ThreadListModel::AccountColourRole).value<QColor>(),
             QColor(QStringLiteral("#cc0000")));
}

void TestThreadListModel::deletedThreadsAreRedAndStruckThrough()
{
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"), QStringLiteral("doomed"));
    thread.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ thread });

    const QModelIndex subject = model.index(0, 0);
    QVERIFY(!model.data(subject, Qt::BackgroundRole).isValid());

    model.applyTagChange(QStringLiteral("t1"), { QStringLiteral("deleted") }, {});

    const QVariant background = model.data(subject, Qt::BackgroundRole);
    QVERIFY(background.isValid());
    QCOMPARE(background.value<QBrush>().color(), ThreadListModel::deletedColour());

    // White text on the fill, and struck through so the state reads even in a
    // screenshot with the colours stripped.
    QCOMPARE(model.data(subject, Qt::ForegroundRole).value<QBrush>().color(),
             QColor(Qt::white));
    QVERIFY(model.data(subject, Qt::FontRole).value<QFont>().strikeOut());
}

void TestThreadListModel::spamThreadsAreOrangeAndStruckThrough()
{
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"), QStringLiteral("junk"));
    thread.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ thread });

    model.applyTagChange(QStringLiteral("t1"), { QStringLiteral("spam") }, {});

    const QModelIndex subject = model.index(0, 0);
    QCOMPARE(model.data(subject, Qt::BackgroundRole).value<QBrush>().color(),
             ThreadListModel::spamColour());
    QVERIFY(model.data(subject, Qt::FontRole).value<QFont>().strikeOut());

    // Spam and deleted must be distinguishable, not two shades of one colour.
    QVERIFY(ThreadListModel::spamColour() != ThreadListModel::deletedColour());
}

void TestThreadListModel::doomedStylingCoversTheWholeCard()
{
    // The cue is on the card itself. It used to be asserted per column,
    // because a cue on one column vanished the moment that column scrolled out
    // of view; one column cannot scroll away, but the roles still have to be
    // answered or a deleted card looks untouched.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"), QStringLiteral("doomed"));
    thread.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ thread });

    model.applyTagChange(QStringLiteral("t1"), { QStringLiteral("deleted") }, {});

    const QModelIndex index = model.index(0, 0);
    QVERIFY2(model.data(index, Qt::BackgroundRole).isValid(),
             "a deleted card has no background");
    QVERIFY2(model.data(index, Qt::FontRole).value<QFont>().strikeOut(),
             "a deleted card is not struck through");
}

void TestThreadListModel::ordinaryThreadsCarryNoRowColour()
{
    // Undo has to restore the plain look, not merely drop the tag.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"), QStringLiteral("normal"));
    thread.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ thread });

    model.applyTagChange(QStringLiteral("t1"), { QStringLiteral("deleted") }, {});
    model.applyTagChange(QStringLiteral("t1"), {}, { QStringLiteral("deleted") });

    const QModelIndex subject = model.index(0, 0);
    QVERIFY(!model.data(subject, Qt::BackgroundRole).isValid());
    const QVariant font = model.data(subject, Qt::FontRole);
    QVERIFY(!font.isValid() || !font.value<QFont>().strikeOut());

    // The foreground goes back to the dimming a read thread carries, NOT to
    // nothing: this thread has no unread tag, so plain for it means dimmed.
    // What matters is that the doomed white is gone.
    const QVariant foreground = model.data(subject, Qt::ForegroundRole);
    if (foreground.isValid()) {
        QVERIFY2(foreground.value<QBrush>().color() != QColor(Qt::white),
                 "the doomed white text survived the undo");
    }
}

void TestThreadListModel::threadIdIsReachableFromAnIndex()
{
    // The view hands MainWindow a QModelIndex; the worker needs a thread id.
    // Without a role for it, every caller has to reach around the model.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")),
                        makeThread(QStringLiteral("t2"), QStringLiteral("two")) });

    const QModelIndex index = model.index(1, 0);
    QCOMPARE(model.data(index, ThreadListModel::ThreadIdRole).toString(),
             QStringLiteral("t2"));
}

void TestThreadListModel::invalidIndexesReturnNothing()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    QVERIFY(!model.data(QModelIndex(), Qt::DisplayRole).isValid());

    // Qt refuses to hand out an out-of-range index at all, so data() cannot be
    // reached with one through the public API. Verified empirically: index()
    // returns invalid for these, and a QPersistentModelIndex is invalidated by
    // the reset in clear() before data() ever sees it. data() still checks its
    // own bounds, but that guard is unreachable defence, not something these
    // assertions can falsify.
    QVERIFY(!model.index(0, 1).isValid());
    QVERIFY(!model.index(5, 0).isValid());
    QVERIFY(!model.index(-1, 0).isValid());

    // A child index must yield nothing: this is a table, not a tree.
    const QModelIndex child = model.index(0, 0, model.index(0, 0));
    QVERIFY(!child.isValid());
    QVERIFY(!model.data(child, Qt::DisplayRole).isValid());
}

void TestThreadListModel::threadAtOutOfRangeIsSafe()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    QVERIFY(model.threadAt(-1).threadId.isEmpty());
    QVERIFY(model.threadAt(99).threadId.isEmpty());
    QCOMPARE(model.threadAt(0).threadId, QStringLiteral("t1"));
}

void TestThreadListModel::threadForResolvesAReplyThroughItsParent()
{
    // Item 88. threadAt() takes a top-level row and a tree numbers rows per
    // parent, so the first reply of ANY thread has row() == 0 and threadAt(0)
    // answers "t1" for a reply of t2. threadFor() resolves through the parent
    // instead, which is what every caller holding an index needs.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")),
                        makeThread(QStringLiteral("t2"), QStringLiteral("two")) });
    model.setThreadMessages(QStringLiteral("t2"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              makeNode(QStringLiteral("m1@example.org"), 1) });

    const QModelIndex second = model.index(1, 0, QModelIndex());
    QCOMPARE(model.threadFor(second).threadId, QStringLiteral("t2"));

    const QModelIndex reply = model.index(0, 0, second);
    QVERIFY(reply.isValid());
    QVERIFY2(model.isMessageRow(reply),
             "the fixture did not produce a message row");
    QCOMPARE(reply.row(), 0);   // The trap: a plausible top-level row number.

    QCOMPARE(model.threadFor(reply).threadId, QStringLiteral("t2"));

    // And the row-taking overload still does the wrong thing for that index,
    // which is why it is documented as unsafe rather than merely deprecated.
    QCOMPARE(model.threadAt(reply.row()).threadId, QStringLiteral("t1"));

    // An invalid index gives an empty summary, which every caller treats as
    // "nothing to do" rather than acting on row 0.
    QVERIFY(model.threadFor(QModelIndex()).threadId.isEmpty());
}

void TestThreadListModel::applyMessageTagChangeRepaintsThatReplyAlone()
{
    // The user's report: hitting Delete or Ctrl+U on a reply moved the pending
    // count and changed nothing on screen. sendMessageTagChange made no
    // optimistic update at all, on the correct reasoning that repainting the
    // THREAD row would claim every message in it had changed. The row that
    // should have repainted is the reply's own.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    MessageNode root = makeNode(QStringLiteral("m0@example.org"), 0);
    MessageNode reply = makeNode(QStringLiteral("m1@example.org"), 1);
    reply.tags = QStringList{ QStringLiteral("unread") };
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex threadIndex = model.index(0, 0, QModelIndex());
    const QModelIndex replyIndex = model.index(0, 0, threadIndex);
    QVERIFY(model.isMessageRow(replyIndex));

    const QStringList threadTagsBefore =
        model.data(threadIndex, ThreadListModel::TagsRole).toStringList();

    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    model.applyMessageTagChange(QStringLiteral("m1@example.org"),
                                { QStringLiteral("deleted") },
                                { QStringLiteral("unread") });

    // The node carries the change, which is what every reply-row role reads.
    QCOMPARE(model.messageAt(replyIndex).isDeleted(), true);
    QCOMPARE(model.messageAt(replyIndex).isUnread(), false);

    // And the view was told, or the change is invisible until something else
    // happens to repaint the row.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toModelIndex(), replyIndex);

    // The THREAD is untouched. Claiming the whole thread changed is the lie
    // the missing update was avoiding, and it must stay avoided.
    QCOMPARE(model.data(threadIndex, ThreadListModel::TagsRole).toStringList(),
             threadTagsBefore);
    QCOMPARE(model.messageAt(model.index(0, 0, threadIndex)).messageId,
             QStringLiteral("m1@example.org"));

    // An unknown message is a no-op rather than a wrong row repainted.
    spy.clear();
    model.applyMessageTagChange(QStringLiteral("nobody@example.org"),
                                { QStringLiteral("deleted") }, {});
    QCOMPARE(spy.count(), 0);
}

void TestThreadListModel::aDeletedReplyIsPaintedAsDoomed()
{
    // Updating the node is not enough on its own: a reply row had no doomed
    // branch at all, so a deleted reply repainted identically to an undeleted
    // one and the user still saw nothing. The thread row has carried this cue
    // since item 13.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    MessageNode root = makeNode(QStringLiteral("m0@example.org"), 0);
    MessageNode reply = makeNode(QStringLiteral("m1@example.org"), 1);
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex threadIndex = model.index(0, 0, QModelIndex());
    const QModelIndex replyIndex = model.index(0, 0, threadIndex);

    const QVariant plainBackground =
        model.data(replyIndex, Qt::BackgroundRole);

    model.applyMessageTagChange(QStringLiteral("m1@example.org"),
                                { QStringLiteral("deleted") }, {});

    const QVariant doomedBackground =
        model.data(replyIndex, Qt::BackgroundRole);
    QVERIFY2(doomedBackground != plainBackground,
             "a deleted reply paints exactly like an undeleted one, so the "
             "user has no way to see that Delete did anything");
    QCOMPARE(doomedBackground.value<QBrush>().color(),
             ThreadListModel::deletedColour());

    // Spam is the other half of isDoomed() and gets its own colour, so the two
    // are told apart by hue rather than by shade.
    model.applyMessageTagChange(QStringLiteral("m1@example.org"),
                                { QStringLiteral("spam") },
                                { QStringLiteral("deleted") });
    QCOMPARE(model.data(replyIndex, Qt::BackgroundRole).value<QBrush>().color(),
             ThreadListModel::spamColour());
}

void TestThreadListModel::aDeletedReplyIsStruckThrough()
{
    // The fill is not the only cue, deliberately: a strike-out survives a
    // screenshot, a colourblind reader and a theme that overrides the
    // background. The thread row has carried both since item 13; a reply had
    // neither.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });
    model.setThreadMessages(QStringLiteral("t1"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              makeNode(QStringLiteral("m1@example.org"), 1) });

    const QModelIndex replyIndex =
        model.index(0, 0, model.index(0, 0, QModelIndex()));

    QVERIFY(!model.data(replyIndex, Qt::FontRole).value<QFont>().strikeOut());

    model.applyMessageTagChange(QStringLiteral("m1@example.org"),
                                { QStringLiteral("deleted") }, {});

    QVERIFY2(model.data(replyIndex, Qt::FontRole).value<QFont>().strikeOut(),
             "a deleted reply is not struck through, so the only cue it has "
             "is a background colour");

    // The reply's smaller font is not lost to the strike-out branch: a reply
    // reads as subordinate whatever its tags say.
    const QFont threadFont =
        model.data(model.index(0, 0, QModelIndex()), Qt::FontRole).value<QFont>();
    const QFont replyFont =
        model.data(replyIndex, Qt::FontRole).value<QFont>();
    if (threadFont.pointSize() > 0 && replyFont.pointSize() > 0)
        QVERIFY(replyFont.pointSize() < threadFont.pointSize());
}

void TestThreadListModel::markingAReplyReadChangesItsForeground()
{
    // The user's second report: marking a reply read or unread moved the
    // counter with no visible change. A reply is deliberately NEVER bold, so
    // unlike a thread row its only cue is the foreground dimming. That cue has
    // to at least exist and change, which is what this pins.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    MessageNode root = makeNode(QStringLiteral("m0@example.org"), 0);
    MessageNode reply = makeNode(QStringLiteral("m1@example.org"), 1);
    reply.tags = QStringList{ QStringLiteral("unread") };
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex replyIndex =
        model.index(0, 0, model.index(0, 0, QModelIndex()));

    const QVariant unreadForeground =
        model.data(replyIndex, Qt::ForegroundRole);

    model.applyMessageTagChange(QStringLiteral("m1@example.org"), {},
                                { QStringLiteral("unread") });

    const QVariant readForeground = model.data(replyIndex, Qt::ForegroundRole);
    QVERIFY2(readForeground != unreadForeground,
             "marking a reply read changed nothing about how its row paints");
    QCOMPARE(readForeground.value<QBrush>().color(),
             ThreadListModel::readColour());

    // And back, so the toggle is visible in both directions rather than only
    // on the way to read.
    model.applyMessageTagChange(QStringLiteral("m1@example.org"),
                                { QStringLiteral("unread") }, {});
    QCOMPARE(model.data(replyIndex, Qt::ForegroundRole), unreadForeground);
}

void TestThreadListModel::anUnreadReplyIsBoldAndStillSmallerThanItsThread()
{
    // Requested by the user on 2026-08-16: "I prefer the bold on replies
    // combined with the dimming." Replies were deliberately never bold before
    // that, so this pins the decision rather than describing the code.
    //
    // Both halves matter. Bold is the second cue, next to the dimming; the
    // smaller size is what still separates a reply from the thread heading
    // above it, and dropping it would make an unread reply indistinguishable
    // from a thread row.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    MessageNode root = makeNode(QStringLiteral("m0@example.org"), 0);
    MessageNode reply = makeNode(QStringLiteral("m1@example.org"), 1);
    reply.tags = QStringList{ QStringLiteral("unread") };
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex threadIndex = model.index(0, 0, QModelIndex());
    const QModelIndex replyIndex = model.index(0, 0, threadIndex);

    const QFont unreadFont =
        model.data(replyIndex, Qt::FontRole).value<QFont>();
    QVERIFY2(unreadFont.bold(), "an unread reply is not bold");

    const QFont threadFont =
        model.data(threadIndex, Qt::FontRole).value<QFont>();
    if (threadFont.pointSize() > 0 && unreadFont.pointSize() > 0) {
        QVERIFY2(unreadFont.pointSize() < threadFont.pointSize(),
                 "a bold reply is the same size as its thread row, so the two "
                 "kinds of row no longer read apart");
    }

    // Bold is the unread cue specifically, not decoration on every reply.
    model.applyMessageTagChange(QStringLiteral("m1@example.org"), {},
                                { QStringLiteral("unread") });
    QVERIFY2(!model.data(replyIndex, Qt::FontRole).value<QFont>().bold(),
             "a read reply is still bold, so bold says nothing");
}

void TestThreadListModel::aThreadTagChangeReachesItsLoadedReplies()
{
    // The user's report: "if I hit read/unread on the main thread message [...]
    // only the main message is repainted [...] the replies don't get
    // repainted."
    //
    // A thread-scoped write reaches every message in the thread IN THE
    // DATABASE. applyTagChange only ever updated the thread's summary, so an
    // expanded thread kept showing replies with their old tags: bold, undimmed
    // and unstruck, describing a state the database no longer held. The rows
    // corrected themselves on the next query, which is what made this look
    // like a repaint problem rather than a stale-model one.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    MessageNode root = makeNode(QStringLiteral("m0@example.org"), 0);
    root.tags = QStringList{ QStringLiteral("unread") };
    MessageNode reply = makeNode(QStringLiteral("m1@example.org"), 1);
    reply.tags = QStringList{ QStringLiteral("unread") };
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex threadIndex = model.index(0, 0, QModelIndex());
    const QModelIndex replyIndex = model.index(0, 0, threadIndex);
    QVERIFY(model.messageAt(replyIndex).isUnread());

    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    model.applyTagChange(QStringLiteral("t1"), {},
                         { QStringLiteral("unread") });

    QVERIFY2(!model.messageAt(replyIndex).isUnread(),
             "a thread marked read left its loaded replies carrying unread, so "
             "the rows describe a state the database does not hold");

    // The reply's row was told to repaint, not merely mutated behind the view.
    bool replyRepainted = false;
    for (const QList<QVariant> &call : spy) {
        const QModelIndex from = call.at(0).toModelIndex();
        const QModelIndex to = call.at(1).toModelIndex();
        if (from.parent() == threadIndex && replyIndex.row() >= from.row()
            && replyIndex.row() <= to.row()) {
            replyRepainted = true;
            break;
        }
    }
    QVERIFY2(replyRepainted,
             "no dataChanged covered the reply rows, so the view has no reason "
             "to redraw them");

    // Both directions, since a toggle is only fixed if it is visible each way.
    model.applyTagChange(QStringLiteral("t1"), { QStringLiteral("unread") }, {});
    QVERIFY(model.messageAt(replyIndex).isUnread());
}

void TestThreadListModel::messageScopeResolvesAThreadRowToTheMessageItDisplays()
{
    // Item 108. A thread root RENDERS one message since item 66, so acting on
    // it acts on that message. The thread's other messages are reached through
    // the explicit thread actions, which still resolve through scopeFor().
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"),
                                 QStringLiteral("A subject"));
    t.totalCount = 7;
    t.firstMessageId = QStringLiteral("m0@example.org");
    model.appendBatch({ t });

    const QModelIndex root = model.index(0, 0, QModelIndex());

    // Unexpanded, which is the case that matters: the id comes from the query,
    // so this needs no children loaded.
    QCOMPARE(model.rowCount(root), 0);

    const ActionScope scope = model.messageScopeFor({ root });
    QCOMPARE(scope.messageIds, QStringList{ QStringLiteral("m0@example.org") });
    QVERIFY2(scope.threadIds.isEmpty(),
             "a thread row still resolved to its whole thread, so every action "
             "on a root card would touch messages it does not display");
    QCOMPARE(scope.messageCount, 1);
    QVERIFY2(!scope.wholeThread,
             "the status bar would claim '(whole thread)' for a one-message "
             "action");

    // The old resolver is unchanged and is what the thread actions use.
    const ActionScope threadScope = model.scopeFor({ root });
    QCOMPARE(threadScope.threadIds, QStringList{ QStringLiteral("t1") });
    QCOMPARE(threadScope.messageCount, 7);
    QVERIFY(threadScope.wholeThread);

    // A reply row is unchanged in both: it always stood for one message.
    model.setThreadMessages(QStringLiteral("t1"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              makeNode(QStringLiteral("m1@example.org"), 1) });
    const QModelIndex reply = model.index(0, 0, root);
    QCOMPARE(model.messageScopeFor({ reply }).messageIds,
             QStringList{ QStringLiteral("m1@example.org") });

    // A root and one of its own replies is two DISTINCT messages, not one
    // deduplicated to the thread.
    const ActionScope both = model.messageScopeFor({ root, reply });
    QCOMPARE(both.messageIds,
             (QStringList{ QStringLiteral("m0@example.org"),
                           QStringLiteral("m1@example.org") }));
    QCOMPARE(both.messageCount, 2);
}

void TestThreadListModel::messageScopeSkipsAThreadRowItCannotNameAMessageFor()
{
    // firstMessageId is populated by the worker from the query. A summary that
    // arrived without one names no message, and the tempting fallback is to
    // act on the whole thread instead. That is exactly the silent escalation
    // item 108 exists to remove: the user would ask to act on one message and
    // hit the conversation.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"),
                                 QStringLiteral("A subject"));
    t.totalCount = 4;
    t.firstMessageId.clear();
    model.appendBatch({ t });

    const QModelIndex root = model.index(0, 0, QModelIndex());
    const ActionScope scope = model.messageScopeFor({ root });

    QVERIFY2(scope.isEmpty(),
             "a thread row with no message id was escalated to its whole "
             "thread rather than skipped");
    QCOMPARE(scope.messageCount, 0);
}

void TestThreadListModel::aMessageTagChangeReachesTheRootCardsOwnMessage()
{
    // The user, 2026-08-16: "delete single message on the root message of a
    // thread doesn't trigger the repaint, delete whole thread does".
    //
    // applyMessageTagChange only searched `children`, and the root message is
    // never there: setThreadMessages drops depth 0 because the root row stands
    // for it. So a write to the message a root card displays found nothing,
    // updated nothing and repainted nothing, while the same write on a reply
    // worked. Item 108 made this the ORDINARY case, so the every-day gesture
    // was the broken one.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"), QStringLiteral("one"));
    t.totalCount = 1;            // A single-message thread.
    t.firstMessageId = QStringLiteral("m0@example.org");
    t.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ t });

    const QModelIndex threadIndex = model.index(0, 0, QModelIndex());
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    model.applyMessageTagChange(QStringLiteral("m0@example.org"),
                                { QStringLiteral("deleted") }, {});

    // The card has to repaint, which is the whole report.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toModelIndex(), threadIndex);

    // And it has to LOOK deleted. For a one-message thread the thread's tags
    // ARE that message's tags: notmuch_thread_get_tags is a union over the
    // thread, and a union over one message is that message.
    QVERIFY2(model.threadAt(0).isDeleted(),
             "the root card does not show the state of the message it "
             "displays, so Delete on it looks like it did nothing");

    // Works before the thread has ever been expanded, which is the case the
    // user hits: nothing loads a root's node until then.
    QCOMPARE(model.rowCount(threadIndex), 0);
}

void TestThreadListModel::aMessageTagChangeOnOneOfManyLeavesTheThreadSummaryAlone()
{
    // The other half, and the reason the fix is not "write it to the summary".
    // A thread's tags are a UNION over its messages, so deleting one message of
    // seven does not make the conversation deleted, and painting the card
    // crimson would claim it did.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"), QStringLiteral("one"));
    t.totalCount = 7;
    t.firstMessageId = QStringLiteral("m0@example.org");
    t.tags = QStringList{ QStringLiteral("inbox"), QStringLiteral("unread") };
    model.appendBatch({ t });

    const QModelIndex threadIndex = model.index(0, 0, QModelIndex());
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    model.applyMessageTagChange(QStringLiteral("m0@example.org"),
                                { QStringLiteral("deleted") },
                                { QStringLiteral("unread") });

    // Still repaints: MessageIdRole and anything else keyed on the root's own
    // node has changed, and the row is what the user is looking at.
    QCOMPARE(spy.count(), 1);

    QVERIFY2(!model.threadAt(0).isDeleted(),
             "deleting one message of a seven-message thread painted the whole "
             "conversation as deleted");
    QVERIFY2(model.threadAt(0).isUnread(),
             "marking one message of a seven-message thread read claimed the "
             "whole conversation was read, though six messages still are not");
}

void TestThreadListModel::aCardListsItsOwnTagsBeforeItsSiblings()
{
    // The user, 2026-08-16, looking at a real four-message thread: the card
    // showed `mailing-list/SBo` and `signed`, and `signed` vanished the moment
    // the row was selected, because it belongs to a SIBLING and item 110 made
    // the card stop claiming it.
    //
    // Their answer, which is better than either extreme: show both, and let
    // size say whose is whose. Own tags first at full size, the thread's other
    // tags after, smaller and muted. Nothing disappears; a chip only shrinks
    // once the split becomes known.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"), QStringLiteral("one"));
    t.totalCount = 4;
    t.firstMessageId = QStringLiteral("m0@example.org");
    // The UNION, as notmuch reports it: `signed` is a sibling's.
    t.tags = QStringList{ QStringLiteral("inbox"),
                          QStringLiteral("mailing-list/SBo"),
                          QStringLiteral("signed"),
                          QStringLiteral("unread") };
    model.appendBatch({ t });

    const QModelIndex threadIndex = model.index(0, 0, QModelIndex());

    // Before the row is opened there is no per-message answer, so every chip
    // is in the own tier. This is what stops anything from appearing to vanish
    // later: the split narrows the tier, it does not remove a chip.
    const QStringList before =
        model.data(threadIndex, ThreadListModel::PillTagsRole).toStringList();
    QVERIFY(before.contains(QStringLiteral("mailing-list/SBo")));
    QVERIFY(before.contains(QStringLiteral("signed")));
    QCOMPARE(model.data(threadIndex, ThreadListModel::PillOwnCountRole).toInt(),
             before.size());

    // The message loads, carrying what it really has.
    model.setRootMessageTags(QStringLiteral("m0@example.org"),
                             { QStringLiteral("inbox"),
                               QStringLiteral("mailing-list/SBo"),
                               QStringLiteral("unread") });

    const QStringList after =
        model.data(threadIndex, ThreadListModel::PillTagsRole).toStringList();

    // Same chips, still all present. The user explicitly did not want the
    // sibling's tag dropped.
    QVERIFY2(after.contains(QStringLiteral("signed")),
             "the sibling's tag was dropped from the card rather than being "
             "shown smaller, which is what looked like a bug");
    QVERIFY2(after.contains(QStringLiteral("mailing-list/SBo")),
             "the card lost a tag the message really carries");

    // Own first, siblings after, and the count is where the delegate switches
    // fonts.
    const int own =
        model.data(threadIndex, ThreadListModel::PillOwnCountRole).toInt();
    QVERIFY2(own > 0 && own < after.size(),
             "the split did not happen: every chip is in one tier");
    QCOMPARE(after.mid(0, own),
             QStringList{ QStringLiteral("mailing-list/SBo") });
    QCOMPARE(after.mid(own), QStringList{ QStringLiteral("signed") });

    // Colours stay aligned with the tags, since the delegate walks them in
    // step and a shift would colour a chip with its neighbour's colour.
    QCOMPARE(model.data(threadIndex, ThreadListModel::PillColoursRole)
                 .toList()
                 .size(),
             after.size());
}

void TestThreadListModel::theSplitIsKnownBeforeTheRowIsEverOpened()
{
    // The user, 2026-08-16: "not selecting the thread shows the chips at 'main'
    // size, not smaller, not dimmed. After selecting the thread the unioned
    // chips repaint to the correct size/color."
    //
    // The first version derived the split from the message LOAD, so an unopened
    // row had no per-message answer and put every chip in the own tier. That is
    // honest and useless: the list is mostly unopened rows, so the feature was
    // invisible exactly where it was meant to be read, and selecting a row
    // still changed the card.
    //
    // The query knows. The worker already walks to the card's message to get
    // its id, so it reads that message's tags in the same pass and the split
    // arrives with the row.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"), QStringLiteral("one"));
    t.totalCount = 4;
    t.firstMessageId = QStringLiteral("m0@example.org");
    t.tags = QStringList{ QStringLiteral("inbox"),
                          QStringLiteral("mailing-list/SBo"),
                          QStringLiteral("signed"),
                          QStringLiteral("unread") };
    // What the worker now supplies: the CARD's message, not the thread.
    t.firstMessageTags = QStringList{ QStringLiteral("inbox"),
                                      QStringLiteral("mailing-list/SBo"),
                                      QStringLiteral("unread") };
    model.appendBatch({ t });

    const QModelIndex threadIndex = model.index(0, 0, QModelIndex());

    // Never opened, never expanded.
    QCOMPARE(model.rowCount(threadIndex), 0);

    const QStringList pills =
        model.data(threadIndex, ThreadListModel::PillTagsRole).toStringList();
    const int own =
        model.data(threadIndex, ThreadListModel::PillOwnCountRole).toInt();

    QVERIFY2(own < pills.size(),
             "an unopened row still puts every chip in the own tier, so the "
             "card renders them all at full size and only corrects itself "
             "when the row is selected");
    QCOMPARE(pills.mid(0, own), QStringList{ QStringLiteral("mailing-list/SBo") });
    QCOMPARE(pills.mid(own), QStringList{ QStringLiteral("signed") });

    // And a message-scoped write still lands, without a load having happened.
    model.applyMessageTagChange(QStringLiteral("m0@example.org"),
                                { QStringLiteral("deleted") }, {});
    QVERIFY(model.messageById(QStringLiteral("m0@example.org")).isDeleted());
    QVERIFY2(!model.threadAt(0).isDeleted(),
             "the thread summary was rewritten for a one-message edit on a "
             "four-message thread");
}

void TestThreadListModel::reconcileRefreshesASurvivorsOwnMessageTags()
{
    // reconcile() keeps a surviving row's NODE, deliberately: its children and
    // its loaded flag are the expansion state the method exists to preserve.
    // That means the per-message tags have to be refreshed explicitly, and the
    // change detector has to notice when only they moved.
    //
    // The case: a sync where the root message alone changed, which is exactly
    // what an external `notmuch tag` or another client does. The thread's union
    // can be identical while the card's own message is not.
    ThreadListModel model;
    ThreadSummary before = makeThread(QStringLiteral("t1"),
                                      QStringLiteral("one"));
    before.totalCount = 2;
    before.firstMessageId = QStringLiteral("m0@example.org");
    before.tags = QStringList{ QStringLiteral("inbox"),
                               QStringLiteral("unread") };
    before.firstMessageTags = QStringList{ QStringLiteral("inbox"),
                                           QStringLiteral("unread") };
    model.appendBatch({ before });

    QVERIFY(model.messageById(QStringLiteral("m0@example.org")).isUnread());

    // The root was read elsewhere. The THREAD is still unread, because its
    // reply is, so the union does not move at all.
    ThreadSummary after = before;
    after.firstMessageTags = QStringList{ QStringLiteral("inbox") };

    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
    model.reconcile({ after });

    QVERIFY2(!model.messageById(QStringLiteral("m0@example.org")).isUnread(),
             "a sync that changed only the card's own message left the row "
             "showing the old per-message tags");
    QVERIFY2(spy.count() >= 1,
             "the change was applied without telling the view, so the card "
             "keeps its old pixels until something else repaints it");

    // The expansion state is still what reconcile() exists to preserve.
    QCOMPARE(model.threadAt(0).threadId, QStringLiteral("t1"));
}

void TestThreadListModel::updatesTagsForMessage()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });
    QVERIFY(model.threadAt(0).isUnread());

    // Optimistic UI: the model changes before the worker confirms.
    model.applyTagChange(QStringLiteral("t1"), {}, { QStringLiteral("unread") });
    QVERIFY(!model.threadAt(0).isUnread());

    model.applyTagChange(QStringLiteral("t1"), { QStringLiteral("flagged") }, {});
    QVERIFY(model.threadAt(0).isFlagged());
}

void TestThreadListModel::tagChangeIsIdempotent()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    // Adding a tag twice must not duplicate it: the tag list is shown joined,
    // and "inbox inbox" is visible garbage.
    model.applyTagChange(QStringLiteral("t1"), { QStringLiteral("inbox") }, {});
    QCOMPARE(model.threadAt(0).tags.count(QStringLiteral("inbox")), 1);

    // Removing an absent tag is equally harmless.
    model.applyTagChange(QStringLiteral("t1"), {}, { QStringLiteral("nosuchtag") });
    QCOMPARE(model.threadAt(0).tags.count(QStringLiteral("inbox")), 1);
}

void TestThreadListModel::tagChangeSignalsExactlyTheChangedRow()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")),
                        makeThread(QStringLiteral("t2"), QStringLiteral("two")),
                        makeThread(QStringLiteral("t3"), QStringLiteral("three")) });

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.applyTagChange(QStringLiteral("t2"), {}, { QStringLiteral("unread") });

    QCOMPARE(changed.size(), 1);
    const QModelIndex topLeft = changed.first().at(0).value<QModelIndex>();
    const QModelIndex bottomRight = changed.first().at(1).value<QModelIndex>();
    QCOMPARE(topLeft.row(), 1);
    QCOMPARE(bottomRight.row(), 1);
    // One column, so the range is a single index: the card repaints whole.
    QCOMPARE(topLeft.column(), 0);
    QCOMPARE(bottomRight.column(), 0);
}

void TestThreadListModel::tagChangeForUnknownThreadIsIgnored()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.applyTagChange(QStringLiteral("nosuchthread"), { QStringLiteral("x") }, {});

    QVERIFY(changed.isEmpty());
    QCOMPARE(model.threadAt(0).tags, (QStringList{ QStringLiteral("inbox"),
                                                   QStringLiteral("unread") }));
}

void TestThreadListModel::tagChangeRoundTripsForRevert()
{
    // MainWindow reverts a failed write by re-applying the change with add and
    // remove swapped. That only restores the original state if the round trip
    // is exact.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });
    const QStringList before = model.threadAt(0).tags;

    const QStringList add{ QStringLiteral("flagged") };
    const QStringList remove{ QStringLiteral("inbox") };

    model.applyTagChange(QStringLiteral("t1"), add, remove);
    QVERIFY(model.threadAt(0).tags != before);

    model.applyTagChange(QStringLiteral("t1"), remove, add);
    const QStringList after = model.threadAt(0).tags;
    QCOMPARE(after.size(), before.size());
    for (const QString &tag : before)
        QVERIFY(after.contains(tag));
}

void TestThreadListModel::modelPassesQtTester()
{
    ThreadListModel model;
    // Catches signal/rowCount contract violations that hand-written tests miss.
    QAbstractItemModelTester tester(&model,
        QAbstractItemModelTester::FailureReportingMode::QtTest);

    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")),
                        makeThread(QStringLiteral("t2"), QStringLiteral("two")) });
    model.applyTagChange(QStringLiteral("t1"), {}, { QStringLiteral("unread") });
    model.clear();
}

void TestThreadListModel::attachmentIsMarkedOnlyOnTaggedThreads()
{
    // The mark is drawn on the card's second line by CardDelegate. What the
    // model owes it is the flag and the glyph, which is what this asserts:
    // the column that used to carry it is gone.
    ThreadSummary plain = makeThread(QStringLiteral("t1"),
                                     QStringLiteral("no attachment"));
    ThreadSummary withFile = makeThread(QStringLiteral("t2"),
                                        QStringLiteral("has one"));
    // notmuch applies this tag itself while indexing, so no MIME parsing and
    // no extra worker query are involved.
    withFile.tags.append(QStringLiteral("attachment"));

    ThreadListModel model;
    model.appendBatch({ plain, withFile });

    const QModelIndex plainCell =
        model.index(0, 0);
    const QModelIndex fileCell =
        model.index(1, 0);

    QVERIFY(!model.data(plainCell, ThreadListModel::HasAttachmentRole).toBool());
    QVERIFY(model.data(fileCell, ThreadListModel::HasAttachmentRole).toBool());

    // Only the marked thread gets a tooltip, or an empty cell would claim to
    // have an attachment on hover.
    QVERIFY(model.data(plainCell, Qt::ToolTipRole).toString().isEmpty());
    QVERIFY(!model.data(fileCell, Qt::ToolTipRole).toString().isEmpty());
}

void TestThreadListModel::aTagDrawnAsAMarkIsNotAlsoAChip()
{
    // Items 69 and 70. passed and replied are drawn marks on line two now, so a
    // chip repeating the word puts the same fact on the card twice. Caught by
    // rendering a real card rather than by any existing test, which is why this
    // one exists: every geometry assertion passed while the row said "passed"
    // as both an arrow and a green pill.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"),
                                      QStringLiteral("subject"));
    thread.tags = { QStringLiteral("inbox"),   QStringLiteral("flagged"),
                    QStringLiteral("attachment"), QStringLiteral("passed"),
                    QStringLiteral("replied"),    QStringLiteral("project") };
    model.appendBatch({ thread });

    const QStringList pills =
        model.data(model.index(0, 0), ThreadListModel::PillTagsRole)
            .toStringList();

    for (const QString &drawn : { QStringLiteral("flagged"),
                                  QStringLiteral("attachment"),
                                  QStringLiteral("passed"),
                                  QStringLiteral("replied") }) {
        QVERIFY2(!pills.contains(drawn),
                 qPrintable(QStringLiteral("'%1' is drawn as a mark and still "
                                           "appears as a chip")
                                .arg(drawn)));
    }

    // Guard: a tag with no mark still becomes a chip, or the assertions above
    // would pass against a model that dropped every pill.
    QVERIFY2(pills.contains(QStringLiteral("project")),
             "an ordinary tag lost its chip, so the filter is too broad");
}

void TestThreadListModel::modelHasOneColumn()
{
    ThreadListModel model;
    ThreadSummary thread;
    thread.threadId = QStringLiteral("T1");
    thread.subject = QStringLiteral("Build fails");
    thread.authors = QStringLiteral("alice@example.org");
    thread.date = QDateTime::currentDateTime();
    thread.totalCount = 1;
    model.appendBatch({ thread });

    QCOMPARE(model.columnCount(), 1);

    // Every field the five columns used to answer is still reachable, by role
    // rather than by column, because the card draws them all.
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(index.data(ThreadListModel::SubjectRole).toString(),
             QStringLiteral("Build fails"));
    QCOMPARE(index.data(ThreadListModel::SendersRole).toString(),
             QStringLiteral("alice@example.org"));
    QVERIFY(index.data(ThreadListModel::DateRole).toDateTime().isValid());

    // A single-message thread offers no expander: totalCount includes the root
    // message, which is the card itself.
    QCOMPARE(index.data(ThreadListModel::ReplyCountRole).toInt(), 0);
}

void TestThreadListModel::aFlatThreadStillListsItsReplies()
{
    // A thread whose messages carry no reply structure: notmuch returns them
    // all from get_toplevel_messages at depth 0, which is what happens when the
    // mail has no usable In-Reply-To. Measured in the user's own database:
    // of 396 inbox threads, three are like this, one of them nine messages
    // deep, and every one of them showed a reply count that expanded to
    // nothing because the model kept only nodes with depth > 0.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"),
                                      QStringLiteral("flat thread"));
    thread.totalCount = 3;
    model.appendBatch({ thread });

    model.setThreadMessages(QStringLiteral("t1"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              makeNode(QStringLiteral("m1@example.org"), 0),
                              makeNode(QStringLiteral("m2@example.org"), 0) });

    const QModelIndex root = model.index(0, 0);

    // Two children, not zero: the FIRST message is the root card itself, and
    // the rest are its replies however flat the thread is.
    QCOMPARE(model.rowCount(root), 2);
    QCOMPARE(model.index(0, 0, root).data(ThreadListModel::MessageIdRole)
                 .toString(),
             QStringLiteral("m1@example.org"));

    // And the count the card advertises must agree with the rows beneath it,
    // or the expander opens onto nothing.
    QCOMPARE(root.data(ThreadListModel::ReplyCountRole).toInt(),
             model.rowCount(root));
}

void TestThreadListModel::theRootCardKnowsItsOwnMessage()
{
    // The root card IS the thread's first message, so it has to be able to say
    // which message that is. Without this the pane renders the whole thread
    // when the root is selected, and the first message is unreachable: the
    // only rows offering it are the replies, and it is not one of them.
    ThreadListModel model;
    ThreadSummary thread = makeThread(QStringLiteral("t1"),
                                      QStringLiteral("a subject"));
    thread.totalCount = 2;
    model.appendBatch({ thread });

    const QModelIndex root = model.index(0, 0);

    // Before the replies are loaded there is nothing to report, and the caller
    // must fall back to loading the whole thread rather than a wrong message.
    QVERIFY(root.data(ThreadListModel::MessageIdRole).toString().isEmpty());

    model.setThreadMessages(QStringLiteral("t1"),
                            { makeNode(QStringLiteral("m0@example.org"), 0),
                              makeNode(QStringLiteral("m1@example.org"), 1) });

    QCOMPARE(root.data(ThreadListModel::MessageIdRole).toString(),
             QStringLiteral("m0@example.org"));

    // And it is the FIRST message, not just any of them: the reply must still
    // report its own.
    QCOMPARE(model.index(0, 0, root).data(ThreadListModel::MessageIdRole)
                 .toString(),
             QStringLiteral("m1@example.org"));
}

void TestThreadListModel::replyShowsOnlyItsOwnTags()
{
    ThreadListModel model;
    ThreadSummary thread;
    thread.threadId = QStringLiteral("T1");
    thread.subject = QStringLiteral("Build fails");
    thread.totalCount = 2;
    thread.tags = { QStringLiteral("inbox"), QStringLiteral("work") };
    model.appendBatch({ thread });

    MessageNode reply;
    reply.messageId = QStringLiteral("M2");
    reply.threadId = QStringLiteral("T1");
    reply.from = QStringLiteral("bob@example.org");
    reply.depth = 1;
    // Two the thread already has, one it does not.
    reply.tags = { QStringLiteral("inbox"), QStringLiteral("work"),
                   QStringLiteral("todo") };

    // Led by the thread's FIRST message, which is what the worker sends and
    // what the root card draws. setThreadMessages drops it by position.
    MessageNode root;
    root.messageId = QStringLiteral("M1");
    root.threadId = QStringLiteral("T1");
    root.depth = 0;
    model.setThreadMessages(QStringLiteral("T1"), { root, reply });

    const QModelIndex threadIndex = model.index(0, 0);
    QVERIFY(model.hasChildren(threadIndex));
    const QModelIndex replyIndex = model.index(0, 0, threadIndex);
    QVERIFY(replyIndex.isValid());

    const QStringList own =
        replyIndex.data(ThreadListModel::MessageOwnTagsRole).toStringList();
    QCOMPARE(own, QStringList{ QStringLiteral("todo") });

    // The colours must line up with the names one for one, or the delegate
    // walks the two lists together and paints a chip in another tag's colour.
    const QVariantList colours =
        replyIndex.data(ThreadListModel::MessageOwnColoursRole).toList();
    QCOMPARE(colours.size(), own.size());
    QVERIFY(colours.first().value<QColor>().isValid());
}

void TestThreadListModel::replySharingEveryThreadTagShowsNone()
{
    ThreadListModel model;
    ThreadSummary thread;
    thread.threadId = QStringLiteral("T1");
    thread.totalCount = 2;
    thread.tags = { QStringLiteral("inbox"), QStringLiteral("work") };
    model.appendBatch({ thread });

    MessageNode reply;
    reply.messageId = QStringLiteral("M2");
    reply.threadId = QStringLiteral("T1");
    reply.depth = 1;
    reply.tags = { QStringLiteral("inbox"), QStringLiteral("work") };

    MessageNode root;
    root.messageId = QStringLiteral("M1");
    root.threadId = QStringLiteral("T1");
    root.depth = 0;
    model.setThreadMessages(QStringLiteral("T1"), { root, reply });

    const QModelIndex replyIndex = model.index(0, 0, model.index(0, 0));
    QVERIFY(replyIndex.isValid());
    QVERIFY(replyIndex.data(ThreadListModel::MessageOwnTagsRole)
                .toStringList()
                .isEmpty());

    // A thread row has no thread to differ from, so it never answers these:
    // its own chips come from PillTagsRole.
    QVERIFY(model.index(0, 0)
                .data(ThreadListModel::MessageOwnTagsRole)
                .toStringList()
                .isEmpty());
}

void TestThreadListModel::reconcileAddsNewThreadsInTheOrderGiven()
{
    // Item 35b. The auto-refresh hands the model a fresh result set and the
    // model works out the difference, rather than being cleared and refilled.
    //
    // Position comes from the result, never from a rule of this model's own:
    // the query is sorted by the worker, so with newest-first a new thread
    // arrives at the front and with oldest-first at the back. A model that
    // forced new rows to the top would contradict the sort the user chose.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("One")),
                        makeThread(QStringLiteral("t2"),
                                   QStringLiteral("Two")) });

    model.reconcile({ makeThread(QStringLiteral("t3"),
                                 QStringLiteral("Newest")),
                      makeThread(QStringLiteral("t1"),
                                 QStringLiteral("One")),
                      makeThread(QStringLiteral("t2"),
                                 QStringLiteral("Two")) });

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.threadAt(0).threadId, QStringLiteral("t3"));
    QCOMPARE(model.threadAt(1).threadId, QStringLiteral("t1"));
    QCOMPARE(model.threadAt(2).threadId, QStringLiteral("t2"));
}

void TestThreadListModel::reconcileRemovesThreadsThatNoLongerMatch()
{
    // A thread read out of an Unread view stops matching, and the list has to
    // say so. Leaving it would make the list disagree with its own query, and
    // every view-wide action (Mark all read) acts on what the list holds.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("One")),
                        makeThread(QStringLiteral("t2"),
                                   QStringLiteral("Two")) });

    model.reconcile({ makeThread(QStringLiteral("t2"),
                                 QStringLiteral("Two")) });

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.threadAt(0).threadId, QStringLiteral("t2"));
}

void TestThreadListModel::reconcileKeepsSurvivingRowsAndTheirExpansion()
{
    // The whole point of reconciling rather than clearing. A surviving thread
    // must keep the SAME row identity, because the view's selection, its
    // expanded state and the open message all hang off persistent indexes: a
    // beginResetModel drops every one of them, which is what made the old
    // refresh close the thread being read.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("One")),
                        makeThread(QStringLiteral("t2"),
                                   QStringLiteral("Two")) });

    MessageNode root = makeNode(QStringLiteral("m1"), 0);
    MessageNode reply = makeNode(QStringLiteral("m2"), 1);
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });
    QCOMPARE(model.rowCount(model.index(0, 0)), 1);

    const QPersistentModelIndex survivor(model.index(0, 0));
    QVERIFY(survivor.isValid());

    // t2 leaves, t3 arrives, t1 stays put.
    model.reconcile({ makeThread(QStringLiteral("t1"),
                                 QStringLiteral("One")),
                      makeThread(QStringLiteral("t3"),
                                 QStringLiteral("Three")) });

    QVERIFY2(survivor.isValid(),
             "reconciling invalidated a surviving row, so the selection and "
             "the open thread would be lost exactly as a reset loses them");
    QCOMPARE(model.threadAt(survivor.row()).threadId, QStringLiteral("t1"));

    // Its loaded replies survive too, or the thread collapses under the reader.
    QCOMPARE(model.rowCount(model.index(survivor.row(), 0)), 1);
}

void TestThreadListModel::reconcileUpdatesTagsOnASurvivingThread()
{
    // A thread that stays but changed state: read elsewhere, tagged by a
    // filter, flagged on the phone. The row has to repaint, or the list shows
    // stale state while claiming to be current.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("One")) });
    QVERIFY(model.threadAt(0).tags.contains(QStringLiteral("unread")));

    ThreadSummary readNow = makeThread(QStringLiteral("t1"),
                                       QStringLiteral("One"));
    readNow.tags = QStringList{ QStringLiteral("inbox") };

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.reconcile({ readNow });

    QCOMPARE(model.rowCount(), 1);
    QVERIFY2(!model.threadAt(0).tags.contains(QStringLiteral("unread")),
             "a surviving thread kept its stale tags");
    QVERIFY2(!changed.isEmpty(),
             "the row's new state was stored without repainting it");
}

void TestThreadListModel::reconcileOnAnEmptyModelFillsIt()
{
    // Item 35a's case, now reached through the same path as every other
    // refresh rather than through a special one: read the view empty, cron
    // indexes new mail, it appears.
    ThreadListModel model;
    QCOMPARE(model.rowCount(), 0);

    model.reconcile({ makeThread(QStringLiteral("t1"),
                                 QStringLiteral("New")) });

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.threadAt(0).threadId, QStringLiteral("t1"));
}

void TestThreadListModel::reconcileWithAnIdenticalResultChangesNothing()
{
    // The common case: the sync brought nothing this query cares about. It
    // runs every ten minutes under a reader, so it must not churn rows, and
    // must not emit a reset that would collapse an expanded thread.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("One")),
                        makeThread(QStringLiteral("t2"),
                                   QStringLiteral("Two")) });

    const QPersistentModelIndex kept(model.index(1, 0));
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);

    model.reconcile({ makeThread(QStringLiteral("t1"),
                                 QStringLiteral("One")),
                      makeThread(QStringLiteral("t2"),
                                 QStringLiteral("Two")) });

    QCOMPARE(model.rowCount(), 2);
    QVERIFY2(reset.isEmpty(), "an unchanged result reset the model");
    QVERIFY2(inserted.isEmpty(), "an unchanged result inserted rows");
    QVERIFY2(removed.isEmpty(), "an unchanged result removed rows");
    QVERIFY(kept.isValid());
    QCOMPARE(kept.row(), 1);
}

void TestThreadListModel::reconcileMovesAThreadBumpedByANewReply()
{
    // The case the other reconcile tests all miss, and the commonest reordering
    // there is: an old thread gets a new reply, so under newest-first the
    // worker returns it at the FRONT although it was already on screen. It is
    // neither an arrival nor a departure, and a reconcile that only handles
    // those two leaves it where it was, showing an order the query does not
    // agree with.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("One")),
                        makeThread(QStringLiteral("t2"),
                                   QStringLiteral("Two")),
                        makeThread(QStringLiteral("t3"),
                                   QStringLiteral("Three")) });

    // t3 was replied to and now sorts first.
    model.reconcile({ makeThread(QStringLiteral("t3"),
                                 QStringLiteral("Three")),
                      makeThread(QStringLiteral("t1"),
                                 QStringLiteral("One")),
                      makeThread(QStringLiteral("t2"),
                                 QStringLiteral("Two")) });

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.threadAt(0).threadId, QStringLiteral("t3"));
    QCOMPARE(model.threadAt(1).threadId, QStringLiteral("t1"));
    QCOMPARE(model.threadAt(2).threadId, QStringLiteral("t2"));
}

void TestThreadListModel::reconcileKeepsAMovedRowsPersistentIndex()
{
    // A reordering seen from the VIEW's side rather than the data's.
    //
    // reconcile() places rows with beginMoveRows, and the assertions on
    // threadAt() cannot tell a correct move from a broken one: QVector::move
    // reorders the storage whatever Qt was told, so the data lands right even
    // if the signal is wrong and only a persistent index reports the
    // difference. A real view's selection rides on exactly that.
    //
    // Every move reconcile() makes is upwards, which is a property of the walk
    // and not of this data: the result is walked front to back, so rows ahead
    // of the target are already final and a misplaced survivor is always
    // pulled forward. The model asserts that invariant.
    ThreadListModel model;

    // Fatal, and attached BEFORE the move. The tester is what actually checks
    // the beginMoveRows arguments against the rows that end up moving; the
    // assertions below read m_threads, which QVector::move reorders correctly
    // whatever destination Qt was told. Without this a wrong destination
    // corrupts only what the VIEW is told, and every assertion here still
    // passes while a real view's selection lands on the wrong row.
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    Q_UNUSED(tester);

    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("One")),
                        makeThread(QStringLiteral("t2"),
                                   QStringLiteral("Two")),
                        makeThread(QStringLiteral("t3"),
                                   QStringLiteral("Three")) });

    const QPersistentModelIndex moved(model.index(0, 0));
    QVERIFY(moved.isValid());

    // t1 ends last. Reached by t2 and t3 each being pulled forward past it,
    // which is what makes t1's persistent index the thing under test: it is
    // displaced twice without ever being the row that moves.
    model.reconcile({ makeThread(QStringLiteral("t2"),
                                 QStringLiteral("Two")),
                      makeThread(QStringLiteral("t3"),
                                 QStringLiteral("Three")),
                      makeThread(QStringLiteral("t1"),
                                 QStringLiteral("One")) });

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.threadAt(0).threadId, QStringLiteral("t2"));
    QCOMPARE(model.threadAt(1).threadId, QStringLiteral("t3"));
    QCOMPARE(model.threadAt(2).threadId, QStringLiteral("t1"));

    // The persistent index followed the row rather than being invalidated,
    // which is what keeps a selection on a thread that reordered under it.
    // This is the assertion the destination adjustment is answerable to: with
    // the wrong destination the DATA still lands correctly (QVector::move does
    // not care what Qt was told) and only this reports the difference.
    QVERIFY2(moved.isValid(), "a moved row lost its persistent index");
    QCOMPARE(moved.row(), 2);
}

void TestThreadListModel::flatModeOffersNoExpanderAndNoReplyCount()
{
    // A sent message lives on its own: the user's mental model of "what I sent"
    // is a list, not a set of conversations, and a thread pulled in whole shows
    // the replies they received under a view that claims to be their outbox.
    //
    // Deliberately not a second model or a filtered query. The expander is
    // driven by hasChildren() and the card's count by ReplyCountRole, both
    // already here, so flat mode is those two answering differently.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("Subject")) });

    const QModelIndex thread = model.index(0, 0);
    QVERIFY(thread.isValid());

    // The tree shape, before anything is turned off. Guards the assertions
    // below: a test whose subject was already flat would pass either way.
    QVERIFY2(model.hasChildren(thread),
             "the fixture thread is not expandable, so this proves nothing");
    QCOMPARE(model.data(thread, ThreadListModel::ReplyCountRole).toInt(), 1);

    model.setFlatMode(true);

    QVERIFY2(!model.hasChildren(thread),
             "a flat list still offered an expander");
    QCOMPARE(model.data(thread, ThreadListModel::ReplyCountRole).toInt(), 0);

    // rowCount has to agree, or the view draws an expander it cannot open, or
    // opens onto rows the card said were not there.
    QCOMPARE(model.rowCount(thread), 0);

    // The thread itself is still a row. Flat means one row per thread, not
    // fewer threads.
    QCOMPARE(model.rowCount(), 1);
}

void TestThreadListModel::flatModeIsOffByDefaultAndReversible()
{
    // The whole condition the user set for this feature: it must not leak into
    // any other view. Off by default is what guarantees that, and returning to
    // false has to restore the tree rather than leaving the model flattened
    // for the next query.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"),
                                   QStringLiteral("Subject")) });

    const QModelIndex thread = model.index(0, 0);
    QVERIFY2(model.hasChildren(thread),
             "a fresh model is flat, so every ordinary view lost its replies");

    model.setFlatMode(true);
    QVERIFY(!model.hasChildren(thread));

    model.setFlatMode(false);
    QVERIFY2(model.hasChildren(thread),
             "leaving flat mode did not restore the tree");
    QCOMPARE(model.data(thread, ThreadListModel::ReplyCountRole).toInt(), 1);
}

void TestThreadListModel::recipientsReplaceTheSenderWhenPresent()
{
    // In a Sent view the sender is the user on every row, so the card shows
    // who it went TO instead. One role, so the delegate needs no branch and
    // cannot disagree with the model about which name a row is showing.
    ThreadListModel model;

    ThreadSummary sent = makeThread(QStringLiteral("t1"),
                                    QStringLiteral("Preventivo"));
    sent.authors = QStringLiteral("You");
    sent.recipients = QStringLiteral("Mario Rossi");

    // No recipients: an ordinary view, where authors is the answer. Same
    // fixture otherwise, so the difference is the field and nothing else.
    ThreadSummary received = makeThread(QStringLiteral("t2"),
                                        QStringLiteral("Newsletter"));
    received.authors = QStringLiteral("Carol");

    model.appendBatch({ sent, received });

    QCOMPARE(model.data(model.index(0, 0),
                        ThreadListModel::SendersRole).toString(),
             QStringLiteral("Mario Rossi"));
    QCOMPARE(model.data(model.index(1, 0),
                        ThreadListModel::SendersRole).toString(),
             QStringLiteral("Carol"));

    // A thread whose To could not be parsed falls back rather than showing an
    // empty name. The fold returns an empty string for a malformed or absent
    // header, and a blank where a sender belongs reads as a rendering fault.
    ThreadSummary unparseable = makeThread(QStringLiteral("t3"),
                                           QStringLiteral("Broken"));
    unparseable.authors = QStringLiteral("You");
    unparseable.recipients = QString();
    model.appendBatch({ unparseable });

    QCOMPARE(model.data(model.index(2, 0),
                        ThreadListModel::SendersRole).toString(),
             QStringLiteral("You"));
}

void TestThreadListModel::aRowLeavesTheViewWhenItLosesTheViewsTag()
{
    ThreadListModel model;
    ThreadSummary a = makeThread(QStringLiteral("t1"), QStringLiteral("Keep"));
    a.firstMessageId = QStringLiteral("m1");
    ThreadSummary b = makeThread(QStringLiteral("t2"), QStringLiteral("Drop"));
    b.firstMessageId = QStringLiteral("m2");
    ThreadSummary c = makeThread(QStringLiteral("t3"), QStringLiteral("Keep2"));
    c.firstMessageId = QStringLiteral("m3");
    model.appendBatch({ a, b, c });
    QCOMPARE(model.rowCount(), 3);

    // The middle row loses `inbox`, as Delete strips it. Middle deliberately:
    // a removal at either end can be right by accident while the index
    // arithmetic is wrong.
    model.applyMessageTagChange(QStringLiteral("m2"), {},
                                { QStringLiteral("inbox") });
    model.removeThreadsWithoutTag(QStringLiteral("inbox"));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, 0, QModelIndex())
                 .data(ThreadListModel::SubjectRole).toString(),
             QStringLiteral("Keep"));
    QCOMPARE(model.index(1, 0, QModelIndex())
                 .data(ThreadListModel::SubjectRole).toString(),
             QStringLiteral("Keep2"));
}

void TestThreadListModel::rowsLosingTheTagAreRemovedInOneContiguousRun()
{
    ThreadListModel model;
    QList<ThreadSummary> batch;
    for (int i = 1; i <= 5; ++i) {
        ThreadSummary t = makeThread(QStringLiteral("t%1").arg(i),
                                     QStringLiteral("S%1").arg(i));
        t.firstMessageId = QStringLiteral("m%1").arg(i);
        batch.append(t);
    }
    model.appendBatch(batch);

    // Three adjacent rows go at once, which is the case a backwards walk in
    // runs handles and a naive forward loop gets wrong by renumbering.
    for (const QString &id : { QStringLiteral("m2"), QStringLiteral("m3"),
                               QStringLiteral("m4") }) {
        model.applyMessageTagChange(id, {}, { QStringLiteral("inbox") });
    }
    model.removeThreadsWithoutTag(QStringLiteral("inbox"));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.index(0, 0, QModelIndex())
                 .data(ThreadListModel::SubjectRole).toString(),
             QStringLiteral("S1"));
    QCOMPARE(model.index(1, 0, QModelIndex())
                 .data(ThreadListModel::SubjectRole).toString(),
             QStringLiteral("S5"));
}

void TestThreadListModel::theTrashViewDrawsNoDoomedFill()
{
    ThreadListModel model;
    ThreadSummary deleted = makeThread(QStringLiteral("t1"),
                                       QStringLiteral("Thrown away"));
    deleted.tags = QStringList{ QStringLiteral("deleted") };
    ThreadSummary spam = makeThread(QStringLiteral("t2"),
                                    QStringLiteral("Junk"));
    spam.tags = QStringList{ QStringLiteral("deleted"), QStringLiteral("spam") };
    model.appendBatch({ deleted, spam });

    const QModelIndex first = model.index(0, 0, QModelIndex());
    const QModelIndex second = model.index(1, 0, QModelIndex());

    // Outside the trash both are filled, which is the guard proving the
    // assertion below can fail.
    QVERIFY(first.data(Qt::BackgroundRole).isValid());
    QVERIFY(second.data(Qt::BackgroundRole).isValid());

    model.setTrashView(true);

    // A plainly deleted row loses the fill AND the white text that only reads
    // against it; the strike-out is what still says deleted, and is asserted
    // by the font role rather than by colour.
    QVERIFY(!first.data(Qt::BackgroundRole).isValid());
    QVERIFY(first.data(Qt::FontRole).value<QFont>().strikeOut());

    // A spam row keeps its tint: the trash promises "thrown away", not
    // "harmless".
    QVERIFY(second.data(Qt::BackgroundRole).isValid());

    // And the flag does not stick: leaving the trash restores the fill, which
    // is the leak the setter's comment warns about.
    model.setTrashView(false);
    QVERIFY(first.data(Qt::BackgroundRole).isValid());
}

void TestThreadListModel::aSummaryWithOneMessageIsAMessageRow()
{
    ThreadListModel model;
    ThreadSummary one = makeThread(QStringLiteral("t1"), QStringLiteral("Alone"));
    one.totalCount = 1;
    one.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ one });

    const QModelIndex row = model.index(0, 0, QModelIndex());
    QVERIFY2(!model.isConversationRow(row),
             "a thread of one message is not a conversation: it has no replies "
             "to stand for, and must open its message on one click");
}

void TestThreadListModel::aSummaryWithRepliesIsAConversationRow()
{
    ThreadListModel model;
    ThreadSummary many = makeThread(QStringLiteral("t1"), QStringLiteral("Talk"));
    many.totalCount = 4;
    many.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ many });

    const QModelIndex row = model.index(0, 0, QModelIndex());
    QVERIFY(model.isConversationRow(row));
}

void TestThreadListModel::aLoadedThreadTrustsItsChildrenOverItsCount()
{
    // notmuch's totalCount counts duplicates, so a "thread of 2" can load with
    // no replies at all. Once loaded the children are the truth, exactly as
    // hasChildren() already decides.
    ThreadListModel model;
    ThreadSummary many = makeThread(QStringLiteral("t1"), QStringLiteral("Dupe"));
    many.totalCount = 2;
    many.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ many });

    MessageNode root;
    root.messageId = QStringLiteral("m1");
    root.threadId = QStringLiteral("t1");
    root.depth = 0;
    model.setThreadMessages(QStringLiteral("t1"), { root });

    const QModelIndex row = model.index(0, 0, QModelIndex());
    QVERIFY2(!model.isConversationRow(row),
             "a thread whose count came from duplicates still claims to be a "
             "conversation after loading no replies at all");
}

void TestThreadListModel::aMessageRowIsNeverAConversationRow()
{
    ThreadListModel model;
    ThreadSummary many = makeThread(QStringLiteral("t1"), QStringLiteral("Talk"));
    many.totalCount = 2;
    many.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ many });

    MessageNode root;
    root.messageId = QStringLiteral("m1");
    root.threadId = QStringLiteral("t1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m2");
    reply.threadId = QStringLiteral("t1");
    reply.depth = 1;
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex thread = model.index(0, 0, QModelIndex());
    const QModelIndex replyRow = model.index(0, 0, thread);
    QVERIFY(model.isMessageRow(replyRow));
    QVERIFY2(!model.isConversationRow(replyRow),
             "a reply row answered yes, so an action on it would scope to the "
             "whole conversation");
}

QTEST_MAIN(TestThreadListModel)
#include "test_threadlistmodel.moc"
