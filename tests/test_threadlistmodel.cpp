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

#include "threadlistmodel.h"

class TestThreadListModel : public QObject
{
    Q_OBJECT
private slots:
    void startsEmpty();
    void appendsBatches();
    void appendingEmptyBatchIsNoOp();
    void clearResetsModel();
    void reportsSubjectAndAuthors();
    void subjectShowsMessageCountOnlyForRealThreads();
    void unreadThreadsRenderBold();
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

void TestThreadListModel::startsEmpty()
{
    ThreadListModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), ThreadListModel::ColumnCount);
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

    const QModelIndex authors = model.index(0, ThreadListModel::AuthorsColumn);
    QCOMPARE(model.data(authors, Qt::DisplayRole).toString(),
             QStringLiteral("Alice"));

    const QModelIndex date = model.index(0, ThreadListModel::DateColumn);
    QVERIFY(!model.data(date, Qt::DisplayRole).toString().isEmpty());

    const QModelIndex tags = model.index(0, ThreadListModel::TagsColumn);
    QCOMPARE(model.data(tags, Qt::DisplayRole).toString(),
             QStringLiteral("inbox unread"));
}

void TestThreadListModel::subjectShowsMessageCountOnlyForRealThreads()
{
    ThreadListModel model;

    ThreadSummary single = makeThread(QStringLiteral("t1"), QStringLiteral("alone"));
    single.totalCount = 1;
    ThreadSummary multi = makeThread(QStringLiteral("t2"), QStringLiteral("group"));
    multi.totalCount = 4;
    model.appendBatch({ single, multi });

    QCOMPARE(model.data(model.index(0, ThreadListModel::SubjectColumn),
                        Qt::DisplayRole).toString(),
             QStringLiteral("alone"));
    QCOMPARE(model.data(model.index(1, ThreadListModel::SubjectColumn),
                        Qt::DisplayRole).toString(),
             QStringLiteral("group (4)"));
}

void TestThreadListModel::unreadThreadsRenderBold()
{
    ThreadListModel model;
    ThreadSummary read = makeThread(QStringLiteral("t1"), QStringLiteral("read"));
    read.tags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ read, makeThread(QStringLiteral("t2"), QStringLiteral("unread")) });

    const QVariant readFont =
        model.data(model.index(0, ThreadListModel::SubjectColumn), Qt::FontRole);
    QVERIFY(!readFont.isValid());

    const QVariant unreadFont =
        model.data(model.index(1, ThreadListModel::SubjectColumn), Qt::FontRole);
    QVERIFY(unreadFont.isValid());
    QVERIFY(unreadFont.value<QFont>().bold());
}

void TestThreadListModel::threadIdIsReachableFromAnIndex()
{
    // The view hands MainWindow a QModelIndex; the worker needs a thread id.
    // Without a role for it, every caller has to reach around the model.
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")),
                        makeThread(QStringLiteral("t2"), QStringLiteral("two")) });

    const QModelIndex index = model.index(1, ThreadListModel::SubjectColumn);
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
    QVERIFY(!model.index(0, ThreadListModel::ColumnCount).isValid());
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
    QCOMPARE(topLeft.column(), 0);
    // The whole row repaints: unread state changes the font of every column.
    QCOMPARE(bottomRight.column(), ThreadListModel::ColumnCount - 1);
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

QTEST_MAIN(TestThreadListModel)
#include "test_threadlistmodel.moc"
