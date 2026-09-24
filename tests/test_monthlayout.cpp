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

#include "monthlayout.h"

class TestMonthLayout : public QObject
{
    Q_OBJECT

private slots:
    void theGridStartsOnTheLocalesFirstDay();
    void cellsTileTheAreaWithNoGapOrOverlap();
    void chipsThatDoNotFitBecomeMore();
    void aPointFindsItsCell();
};

void TestMonthLayout::theGridStartsOnTheLocalesFirstDay()
{
    // 2026-09-01 is a Tuesday.
    QCOMPARE(MonthLayout::gridStart(2026, 9, Qt::Monday), QDate(2026, 8, 31));
    QCOMPARE(MonthLayout::gridStart(2026, 9, Qt::Sunday), QDate(2026, 8, 30));
    // A month starting ON the first weekday starts on its own first day.
    QCOMPARE(MonthLayout::gridStart(2026, 6, Qt::Monday), QDate(2026, 6, 1));
    const MonthLayout layout(QRect(0, 0, 700, 600), 2026, 9, Qt::Monday, 20, 16);
    QCOMPARE(layout.dateAt(0), QDate(2026, 8, 31));
    QCOMPARE(layout.dateAt(41), QDate(2026, 10, 11));
}

void TestMonthLayout::cellsTileTheAreaWithNoGapOrOverlap()
{
    // 703 does not divide by 7: the remainder must go somewhere, not vanish
    // as a gap down the right edge.
    const QRect area(0, 0, 703, 605);
    const MonthLayout layout(area, 2026, 9, Qt::Monday, 20, 16);
    QCOMPARE(layout.cellRect(0).left(), area.left());
    QCOMPARE(layout.cellRect(6).right(), area.right());
    QCOMPARE(layout.cellRect(41).bottom(), area.bottom());
    for (int i = 0; i < 6; ++i)
        QCOMPARE(layout.cellRect(i).right() + 1, layout.cellRect(i + 1).left());
    QCOMPARE(layout.cellRect(0).bottom() + 1, layout.cellRect(7).top());
}

void TestMonthLayout::chipsThatDoNotFitBecomeMore()
{
    // 600 / 6 rows = 100 px a cell; 20 px of day number; 16 px chips: 5 fit.
    const MonthLayout layout(QRect(0, 0, 700, 600), 2026, 9, Qt::Monday, 20, 16);
    QCOMPARE(layout.capacity(), 5);
    QCOMPARE(layout.visibleChips(3), 3);
    QCOMPARE(layout.visibleChips(5), 5);
    // Six do not fit: four chips and a "+2 more" line in the fifth slot.
    QCOMPARE(layout.visibleChips(6), 4);
    QCOMPARE(layout.chipRect(0, 0).top(), layout.cellRect(0).top() + 20);
    QVERIFY(layout.cellRect(0).contains(layout.chipRect(0, 4)));
}

void TestMonthLayout::aPointFindsItsCell()
{
    const MonthLayout layout(QRect(0, 0, 700, 600), 2026, 9, Qt::Monday, 20, 16);
    QCOMPARE(layout.cellAt(layout.cellRect(17).center()), 17);
    QCOMPARE(layout.cellAt(QPoint(-5, 10)), -1);
}

QTEST_MAIN(TestMonthLayout)
#include "test_monthlayout.moc"
