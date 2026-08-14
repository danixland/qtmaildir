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

#include "tagstrip.h"

/// The chip hit test.
///
/// Asserted on rects from chipRectAt(), which is the SAME function paintEvent
/// lays out from, so a drawn chip and a clickable chip cannot drift apart. Not
/// asserted by rendering: a pixel probe cannot tell a chip that is drawn from a
/// chip that is drawn and clickable, and both halves have been broken
/// independently in this project before.
class TestTagStrip : public QObject
{
    Q_OBJECT
private slots:
    void chipAtFindsEachVisibleTag();
    void chipAtMissesTheGapAndTheEdges();
    void chipAtIgnoresTheOverflowChip();
};

void TestTagStrip::chipAtFindsEachVisibleTag()
{
    TagStrip strip;
    strip.resize(600, 30);
    strip.setTags({ QStringLiteral("inbox"), QStringLiteral("unread") });

    const QStringList visible = strip.visibleTags();
    QCOMPARE(visible.size(), 2);

    // The guard: the geometry this test depends on must exist before the test
    // can mean anything. A zero-width chip would make every lookup below miss
    // and the test would pass for the wrong reason.
    for (int i = 0; i < visible.size(); ++i) {
        const QRect rect = strip.chipRectAt(i);
        QVERIFY2(rect.width() > 0 && rect.height() > 0,
                 qPrintable(QStringLiteral("chip %1 has an empty rect").arg(i)));
        QCOMPARE(strip.chipAt(rect.center()), visible.at(i));
    }
}

void TestTagStrip::chipAtMissesTheGapAndTheEdges()
{
    TagStrip strip;
    strip.resize(600, 30);
    strip.setTags({ QStringLiteral("inbox"), QStringLiteral("unread") });
    QCOMPARE(strip.visibleTags().size(), 2);

    const QRect first = strip.chipRectAt(0);
    const QRect second = strip.chipRectAt(1);
    QVERIFY2(second.left() > first.right() + 1,
             "the two chips must not touch, or there is no gap to test");

    // Between the chips: no tag, so no menu entry rather than the nearest one.
    const QPoint gap((first.right() + second.left()) / 2, first.center().y());
    QVERIFY(strip.chipAt(gap).isEmpty());

    // Past the last chip, where the strip is empty space.
    QVERIFY(strip.chipAt(QPoint(strip.width() - 1, first.center().y())).isEmpty());
}

void TestTagStrip::chipAtIgnoresTheOverflowChip()
{
    // The +N chip stands for a LIST of tags, not for a tag, so there is no
    // single value a search could be built from.
    TagStrip strip;
    strip.resize(90, 30);
    strip.setTags({ QStringLiteral("inbox"), QStringLiteral("unread"),
                    QStringLiteral("flagged"), QStringLiteral("attachment"),
                    QStringLiteral("replied") });

    QVERIFY2(!strip.hiddenTags().isEmpty(),
             "the strip must actually overflow, or this asserts nothing");
    QVERIFY2(!strip.visibleTags().isEmpty(),
             "the strip must show at least one chip to test against");

    // Every point across the strip either finds a VISIBLE tag or nothing. The
    // overflow chip sits after the visible ones and must yield nothing.
    for (int x = 0; x < strip.width(); x += 3) {
        const QString found = strip.chipAt(QPoint(x, strip.height() / 2));
        if (!found.isEmpty())
            QVERIFY(strip.visibleTags().contains(found));
    }

    const QRect last = strip.chipRectAt(strip.visibleTags().size() - 1);
    QVERIFY(strip.chipAt(QPoint(last.right() + 5, strip.height() / 2)).isEmpty());
}

QTEST_MAIN(TestTagStrip)
#include "test_tagstrip.moc"
