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
    void messageNodeHoldsDisplayFacts();
    void rootRowsSurviveTheTreeConversion();
    void repliesBecomeChildRowsUnderTheirThread();
    void messageRowsShowTheirOwnSenderAndSubject();
    void modelHasOneColumn();
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
    void updatesTagsForMessage();
    void tagChangeIsIdempotent();
    void tagChangeSignalsExactlyTheChangedRow();
    void tagChangeForUnknownThreadIsIgnored();
    void tagChangeRoundTripsForRevert();
    void modelPassesQtTester();
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

    // A thread root is not a message row and carries no message id.
    QVERIFY(!model.data(root, ThreadListModel::IsMessageRole).toBool());
    QVERIFY(model.data(root, ThreadListModel::MessageIdRole)
                .toString().isEmpty());

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

    // The glyph the delegate draws from that flag must be something a font can
    // render: an unrenderable codepoint shows as tofu, which reads as
    // breakage rather than as a mark.
    QVERIFY(!ThreadListModel::flagGlyph().isEmpty());
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

    // The glyph must be something a font can draw. An unrenderable codepoint
    // shows as a tofu box, which reads as breakage rather than as a marker.
    QVERIFY(!ThreadListModel::attachmentGlyph().isEmpty());

    // Only the marked thread gets a tooltip, or an empty cell would claim to
    // have an attachment on hover.
    QVERIFY(model.data(plainCell, Qt::ToolTipRole).toString().isEmpty());
    QVERIFY(!model.data(fileCell, Qt::ToolTipRole).toString().isEmpty());
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
    model.setThreadMessages(QStringLiteral("T1"), { reply });

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
    model.setThreadMessages(QStringLiteral("T1"), { reply });

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

QTEST_MAIN(TestThreadListModel)
#include "test_threadlistmodel.moc"
