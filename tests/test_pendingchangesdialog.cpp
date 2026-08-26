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

#include <QtTest>

#include "pendingchangesdialog.h"

/// The grouping the user asked for, asserted on the ROWS rather than on a
/// render. A pixel probe cannot tell a correct layout from a plausible one,
/// which is why MessageDetailsDialog exposes its rows too.
class TestPendingChangesDialog : public QObject
{
    Q_OBJECT
private slots:
    void aMessageIsDrawnOnceWithItsActionsBeneath();
    void everyChangeKeepsARowOfItsOwn();
    void aThreadRowCarriesItsMessageCount();
    void anUnresolvedIdStillOpensItsRun();
};

void TestPendingChangesDialog::aMessageIsDrawnOnceWithItsActionsBeneath()
{
    // The layout: subject once, actions under it.
    //
    //   Build fails      Delete
    //                    Mark read
    //   August digest    Delete
    const QVector<PendingChange> changes {
        { QStringLiteral("a@example.org"), false, QStringLiteral("Delete"),
          QStringLiteral("Build fails"), -1 },
        { QStringLiteral("a@example.org"), false, QStringLiteral("Mark read"),
          QStringLiteral("Build fails"), -1 },
        { QStringLiteral("b@example.org"), false, QStringLiteral("Delete"),
          QStringLiteral("August digest"), -1 },
    };

    const QVector<PendingChangeRow> rows =
        PendingChangesDialog::rowsFor(changes);
    QCOMPARE(rows.size(), 3);

    // First row of the run carries the subject.
    QVERIFY(rows.at(0).startsMessage);
    QCOMPARE(rows.at(0).subject, QStringLiteral("Build fails"));
    QCOMPARE(rows.at(0).action, QStringLiteral("Delete"));

    // The second action of the same message carries NO subject, which is what
    // puts it under the message rather than beside a repeated one.
    QVERIFY(!rows.at(1).startsMessage);
    QVERIFY2(rows.at(1).subject.isEmpty(),
             "the subject was repeated instead of grouping the actions");
    QCOMPARE(rows.at(1).action, QStringLiteral("Mark read"));

    // A different message opens a new run.
    QVERIFY(rows.at(2).startsMessage);
    QCOMPARE(rows.at(2).subject, QStringLiteral("August digest"));
}

void TestPendingChangesDialog::everyChangeKeepsARowOfItsOwn()
{
    // Grouping must not COLLAPSE anything: the count the user clicked has to
    // equal the number of rows they are shown, so two actions on one message
    // are two rows however they are drawn.
    const QVector<PendingChange> changes {
        { QStringLiteral("a@example.org"), false, QStringLiteral("Delete"),
          QStringLiteral("One"), -1 },
        { QStringLiteral("a@example.org"), false, QStringLiteral("Mark read"),
          QStringLiteral("One"), -1 },
        { QStringLiteral("a@example.org"), false, QStringLiteral("Mark spam"),
          QStringLiteral("One"), -1 },
    };

    const QVector<PendingChangeRow> rows =
        PendingChangesDialog::rowsFor(changes);
    QCOMPARE(rows.size(), changes.size());

    // Exactly one of them opens the run, and every action survives.
    int starts = 0;
    QStringList actions;
    for (const PendingChangeRow &row : rows) {
        if (row.startsMessage)
            ++starts;
        actions.append(row.action);
    }
    QCOMPARE(starts, 1);
    QCOMPARE(actions, QStringList({ QStringLiteral("Delete"),
                                    QStringLiteral("Mark read"),
                                    QStringLiteral("Mark spam") }));
}

void TestPendingChangesDialog::aThreadRowCarriesItsMessageCount()
{
    // A thread action reports how many messages it covered. The count belongs
    // to the row that opens the run, since that is where the subject is drawn.
    const QVector<PendingChange> changes {
        { QStringLiteral("t1"), true, QStringLiteral("Delete thread"),
          QStringLiteral("A conversation"), 4 },
        { QStringLiteral("m1@example.org"), false, QStringLiteral("Delete"),
          QStringLiteral("A message"), -1 },
    };

    const QVector<PendingChangeRow> rows =
        PendingChangesDialog::rowsFor(changes);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).messageCount, 4);
    // A message row claims no count: it stands for one message and saying "1"
    // would read as a thread of one.
    QCOMPARE(rows.at(1).messageCount, -1);
}

void TestPendingChangesDialog::anUnresolvedIdStillOpensItsRun()
{
    // A stale id resolves to an empty subject. The row must still OPEN a run,
    // or its actions would be drawn as though they belonged to the message
    // above them, which is worse than saying the subject is unknown.
    //
    // This is why startsMessage is carried rather than inferred from a
    // non-empty subject.
    const QVector<PendingChange> changes {
        { QStringLiteral("a@example.org"), false, QStringLiteral("Delete"),
          QStringLiteral("Known"), -1 },
        { QStringLiteral("gone@example.org"), false, QStringLiteral("Delete"),
          QString(), -1 },
    };

    const QVector<PendingChangeRow> rows =
        PendingChangesDialog::rowsFor(changes);
    QCOMPARE(rows.size(), 2);
    QVERIFY2(rows.at(1).startsMessage,
             "an unresolved id was folded into the message above it");
    QVERIFY(rows.at(1).subject.isEmpty());
}

QTEST_MAIN(TestPendingChangesDialog)
#include "test_pendingchangesdialog.moc"
