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

#include "cardlayout.h"

#include <QFont>
#include <QTest>

class TestCardLayout : public QObject
{
    Q_OBJECT

private slots:
    void everyCardIsTheSameHeight();
    void threeLinesStackWithoutOverlapping();
    void replyIndentsByDepth();
    void indentStopsAtTheCap();
    void expanderSitsOnTheSecondLine();
    void expanderIsEmptyWithoutReplies();
    void dateIsFlushRight();
    void threadCardCarriesAnAccentBar();
    void replyCardCarriesNoAccentBar();
};

namespace {

CardLayout::Input threadInput()
{
    CardLayout::Input in;
    in.isMessage = false;
    in.depth = 0;
    in.replyCount = 3;
    return in;
}

CardLayout::Input replyInput(int depth)
{
    CardLayout::Input in;
    in.isMessage = true;
    in.depth = depth;
    in.replyCount = 0;
    return in;
}

}  // namespace

void TestCardLayout::everyCardIsTheSameHeight()
{
    const QFont font;
    const int thread = CardLayout::heightFor(font);

    // The uniform height is the whole reason setUniformRowHeights(true)
    // survives this design, so it is asserted directly rather than inferred
    // from two cards happening to look alike.
    //
    // Note what is NOT varied here: the tag list. CardLayout reserves line 3
    // unconditionally and never sees the tags, which is exactly the property
    // being asserted. A version of this test that passed a tag list in would
    // be testing a parameter that does not exist.
    const CardLayout threadCard =
        CardLayout::compute(threadInput(), QRect(0, 0, 400, thread), font);
    const CardLayout deepReply =
        CardLayout::compute(replyInput(3), QRect(0, 0, 400, thread), font);
    CardLayout::Input noRepliesIn = threadInput();
    noRepliesIn.replyCount = 0;
    const CardLayout noReplies =
        CardLayout::compute(noRepliesIn, QRect(0, 0, 400, thread), font);

    QCOMPARE(threadCard.totalHeight, thread);
    QCOMPARE(deepReply.totalHeight, thread);
    QCOMPARE(noReplies.totalHeight, thread);

    // The third line exists on every card, including one with nothing to put
    // there. That blank band is the cost the uniform height was bought with.
    QCOMPARE(noReplies.tagRect.height(), threadCard.tagRect.height());
}

void TestCardLayout::threeLinesStackWithoutOverlapping()
{
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const CardLayout card =
        CardLayout::compute(threadInput(), QRect(0, 0, 400, h), font);

    QVERIFY(card.senderRect.height() > 0);
    QVERIFY(card.subjectRect.height() > 0);
    QVERIFY(card.tagRect.height() > 0);

    // Guard: these must actually be three stacked bands. A layout that
    // collapsed them all to the same rect would satisfy any assertion that
    // only checked they exist.
    QVERIFY(card.senderRect.bottom() <= card.subjectRect.top());
    QVERIFY(card.subjectRect.bottom() <= card.tagRect.top());
    QVERIFY(card.tagRect.bottom() <= h);
}

void TestCardLayout::replyIndentsByDepth()
{
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const QRect rect(0, 0, 400, h);

    const CardLayout root = CardLayout::compute(threadInput(), rect, font);
    const CardLayout d1 = CardLayout::compute(replyInput(1), rect, font);
    const CardLayout d2 = CardLayout::compute(replyInput(2), rect, font);

    QVERIFY(d1.contentLeft > root.contentLeft);
    QVERIFY(d2.contentLeft > d1.contentLeft);

    // One spine per depth level, so the count is the depth itself.
    QCOMPARE(root.spines.size(), 0);
    QCOMPARE(d1.spines.size(), 1);
    QCOMPARE(d2.spines.size(), 2);

    // Each spine runs the full height of the card, which is what makes an
    // expansion read as one continuous block rather than as dashes.
    for (const QRect &spine : d2.spines) {
        QCOMPARE(spine.top(), rect.top());
        QCOMPARE(spine.bottom(), rect.bottom());
    }
}

void TestCardLayout::indentStopsAtTheCap()
{
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const QRect rect(0, 0, 400, h);

    const CardLayout d4 = CardLayout::compute(replyInput(4), rect, font);
    const CardLayout d5 = CardLayout::compute(replyInput(5), rect, font);
    const CardLayout d9 = CardLayout::compute(replyInput(9), rect, font);

    QCOMPARE(d5.contentLeft, d4.contentLeft);
    QCOMPARE(d9.contentLeft, d4.contentLeft);
    QCOMPARE(d5.spines.size(), d4.spines.size());
    QCOMPARE(d9.spines.size(), d4.spines.size());

    // Guard: the cap must not be so low that it has already bitten at depth 3,
    // which would make the three assertions above true for the wrong reason.
    const CardLayout d3 = CardLayout::compute(replyInput(3), rect, font);
    QVERIFY(d3.contentLeft < d4.contentLeft);
}

void TestCardLayout::expanderSitsOnTheSecondLine()
{
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const CardLayout card =
        CardLayout::compute(threadInput(), QRect(0, 0, 400, h), font);

    QVERIFY(!card.expanderRect.isEmpty());
    // It is the reply count, so it belongs on the line the reply count is on.
    QVERIFY(card.expanderRect.top() >= card.subjectRect.top());
    QVERIFY(card.expanderRect.bottom() <= card.subjectRect.bottom());
    // And it is on the right, where the count is drawn, not in a left gutter.
    QVERIFY(card.expanderRect.left() > 400 / 2);
}

void TestCardLayout::expanderIsEmptyWithoutReplies()
{
    const QFont font;
    const int h = CardLayout::heightFor(font);
    CardLayout::Input in = threadInput();
    in.replyCount = 0;

    const CardLayout card = CardLayout::compute(in, QRect(0, 0, 400, h), font);
    QVERIFY(card.expanderRect.isEmpty());
}

void TestCardLayout::dateIsFlushRight()
{
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const QRect rect(0, 0, 400, h);
    const CardLayout card = CardLayout::compute(threadInput(), rect, font);

    // Compared as exclusive edges. QRect::right() is inclusive (left + width -
    // 1), so asserting card.dateRect.right() == rect.right() - kPaddingX
    // demands a gap of kPaddingX - 1 pixels and is off by one against the
    // padding the constant names.
    QCOMPARE(card.dateRect.right() + 1, rect.right() + 1 - CardLayout::kPaddingX);
    // The sender must stop before the date starts, or a long sender overwrites
    // it. This is the assertion that fails if the two are laid out
    // independently.
    QVERIFY(card.senderRect.right() <= card.dateRect.left());
}

void TestCardLayout::threadCardCarriesAnAccentBar()
{
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const QRect rect(0, 0, 400, h);
    const CardLayout card = CardLayout::compute(threadInput(), rect, font);

    QCOMPARE(card.accentRect.left(), rect.left());
    QCOMPARE(card.accentRect.width(), CardLayout::kAccentWidth);
    // Full height, so a run of cards from one account reads as a continuous
    // edge rather than as dashes.
    QCOMPARE(card.accentRect.top(), rect.top());
    QCOMPARE(card.accentRect.bottom(), rect.bottom());

    // Nothing may be drawn on top of the colour.
    QVERIFY(card.contentLeft >= card.accentRect.right());
}

void TestCardLayout::replyCardCarriesNoAccentBar()
{
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const CardLayout reply =
        CardLayout::compute(replyInput(1), QRect(0, 0, 400, h), font);

    // A reply's account is its thread's, stated once at the head. The spine
    // carries the accent instead, so the gutter never holds two lines.
    QVERIFY(reply.accentRect.isEmpty());
    QCOMPARE(reply.spines.size(), 1);
}

QTEST_MAIN(TestCardLayout)
#include "test_cardlayout.moc"
