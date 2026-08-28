// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 */
#include <QtTest>

#include "threaddashboard.h"
#include "threaddigest.h"

class TestThreadDashboard : public QObject
{
    Q_OBJECT

private slots:
    void itNamesTheUnreadCountItWasGiven();
    void itSaysAllCaughtUpWhenNothingIsUnread();
    void itOffersMoreWhenTheListIsCapped();
    void itOffersNoMoreLinkWhenTheListIsComplete();
};

static ThreadDigest digestWith(int unreadTotal, int shown)
{
    ThreadDigest d;
    d.threadId = QStringLiteral("t1");
    d.totalCount = 44;
    d.unreadTotal = unreadTotal;
    for (int i = 0; i < shown; ++i) {
        MessageNode ref;
        ref.messageId = QStringLiteral("m%1@example.org").arg(i);
        ref.subject = QStringLiteral("Re: something");
        // The avatar hashes the bare address, so a node without one exercises
        // only the no-sender fallback.
        ref.senderAddress = QStringLiteral("someone@example.org");
        d.unread.append(ref);
    }
    d.buckets = QVector<int>(ThreadDigest::kBuckets, 1);
    return d;
}

void TestThreadDashboard::itNamesTheUnreadCountItWasGiven()
{
    ThreadDashboard dashboard;
    dashboard.setDigest(digestWith(3, 3));
    QCOMPARE(dashboard.unreadCountShown(), 3);
    QVERIFY(!dashboard.showingAllCaughtUp());
}

void TestThreadDashboard::itSaysAllCaughtUpWhenNothingIsUnread()
{
    ThreadDashboard dashboard;
    dashboard.setDigest(digestWith(0, 0));
    QVERIFY2(dashboard.showingAllCaughtUp(),
             "a fully read thread shows an empty Waiting for you block rather "
             "than saying so");
}

void TestThreadDashboard::itOffersMoreWhenTheListIsCapped()
{
    // 20 unread, 5 shown: the link has to name the 15 the pane cannot list, or
    // the dashboard under-reports exactly when the number matters most.
    ThreadDashboard dashboard;
    dashboard.setDigest(digestWith(20, ThreadDigest::kUnreadShown));
    QCOMPARE(dashboard.hiddenUnreadCount(), 15);
}

void TestThreadDashboard::itOffersNoMoreLinkWhenTheListIsComplete()
{
    ThreadDashboard dashboard;
    dashboard.setDigest(digestWith(4, 4));
    QCOMPARE(dashboard.hiddenUnreadCount(), 0);
}

QTEST_MAIN(TestThreadDashboard)
#include "test_threaddashboard.moc"
