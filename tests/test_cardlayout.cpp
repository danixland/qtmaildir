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
#include <QLocale>
#include <QTest>

class TestCardLayout : public QObject
{
    Q_OBJECT

private slots:
    void everyCardIsTheSameHeight();
    void threeLinesStackWithoutOverlapping();
    void replyIndentsByDepth();
    void aDepthZeroReplyStillIndents();
    void indentStopsAtTheCap();
    void expanderSitsOnTheSecondLine();
    void expanderIsEmptyWithoutReplies();
    void theExpanderReadsAsAPillWithAWord();
    void dateIsFlushRight();
    void threadCardCarriesAnAccentBar();
    void replyCardCarriesNoAccentBar();
    void theDateFitsWhenTheCardIsBold();
    void theDateFollowsTheSystemLocale();
    void aConfiguredDateFormatIsUsedAndReservedFor();
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

void TestCardLayout::aDepthZeroReplyStillIndents()
{
    // A reply in a FLAT thread carries depth 0, because notmuch reports every
    // message of a thread with no usable In-Reply-To as a top-level message.
    // It is still a reply: it is a child row under the root card, and it has
    // to read as one.
    //
    // Treating depth 0 as "no nesting" left those replies flush against their
    // thread with no spine, while a nested thread's replies indented normally,
    // so the list showed two different shapes for the same relationship.
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const QRect rect(0, 0, 400, h);

    CardLayout::Input flatReply;
    flatReply.isMessage = true;
    flatReply.depth = 0;

    const CardLayout root = CardLayout::compute(threadInput(), rect, font);
    const CardLayout reply = CardLayout::compute(flatReply, rect, font);

    QVERIFY2(reply.contentLeft > root.contentLeft,
             "a depth-0 reply sits flush with its thread, so a flat thread's "
             "replies look like more threads");
    QVERIFY2(!reply.spines.isEmpty(),
             "a depth-0 reply has no spine, so nothing joins it to the thread "
             "above it");

    // And it lands at the same place a depth-1 reply does: the two are the
    // same relationship and notmuch's numbering is the only difference.
    const CardLayout nested = CardLayout::compute(replyInput(1), rect, font);
    QCOMPARE(reply.contentLeft, nested.contentLeft);
    QCOMPARE(reply.spines.size(), nested.spines.size());
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

void TestCardLayout::theExpanderReadsAsAPillWithAWord()
{
    // A bare "3" beside the subject reads as an unexplained number and gives
    // no hint that it can be clicked. The label carries the word, and the rect
    // carries padding for the pill drawn behind it.
    QCOMPARE(CardLayout::expanderLabel(3, false),
             QStringLiteral("\u25b8 3 replies"));
    QCOMPARE(CardLayout::expanderLabel(3, true),
             QStringLiteral("\u25be 3 replies"));

    // Singular, because "1 replies" is the kind of detail that makes an
    // interface look unfinished.
    QCOMPARE(CardLayout::expanderLabel(1, false),
             QStringLiteral("\u25b8 1 reply"));

    const QFont font;
    const int h = CardLayout::heightFor(font);
    const CardLayout card =
        CardLayout::compute(threadInput(), QRect(0, 0, 400, h), font);
    const QFontMetrics small(CardLayout::smallFont(font));

    // The rect must hold the label AND its padding, or the pill's background
    // is narrower than the text sitting on it.
    QVERIFY2(card.expanderRect.width()
                 >= small.horizontalAdvance(CardLayout::expanderLabel(3, false))
                        + CardLayout::kPillPaddingX * 2,
             "the expander rect is too narrow for its own label and padding");

    // And it must NOT change width when the card opens: a pill that resized on
    // click would shift the subject's elision under the pointer.
    CardLayout::Input open = threadInput();
    const CardLayout expanded =
        CardLayout::compute(open, QRect(0, 0, 400, h), font);
    QCOMPARE(expanded.expanderRect.width(), card.expanderRect.width());
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

void TestCardLayout::theDateFollowsTheSystemLocale()
{
    const QDateTime when(QDate(2025, 8, 10), QTime(6, 26));

    // The system locale's own rendering, whatever it is. Asserting a specific
    // string would only restate the hardcoded pattern this replaced, and would
    // fail on any machine but the one that wrote it.
    QCOMPARE(CardLayout::formatDate(when),
             QLocale::system().toString(when, QLocale::ShortFormat));

    // The specific fault: an ISO-looking pattern on a desktop that does not
    // use one. Guarded so this test says nothing on a locale that genuinely
    // formats that way.
    if (QLocale::system().toString(when, QLocale::ShortFormat)
            != QStringLiteral("2025-08-10 06:26")) {
        QVERIFY2(CardLayout::formatDate(when)
                     != QStringLiteral("2025-08-10 06:26"),
                 "the date is hardcoded to yyyy-MM-dd hh:mm rather than "
                 "following the desktop's locale");
    }

    // And the reserved width has to follow the same formatter, or a locale
    // whose dates are longer clips them exactly as the bold font did.
    QFont font;
    const int h = CardLayout::heightFor(font);
    const CardLayout card =
        CardLayout::compute(threadInput(), QRect(0, 0, 400, h), font);
    QFont bold = font;
    bold.setBold(true);
    QVERIFY2(card.dateRect.width()
                 >= QFontMetrics(bold).horizontalAdvance(
                        CardLayout::formatDate(when)),
             "the reserved date width is narrower than this locale's own "
             "formatting of a date");
}

void TestCardLayout::aConfiguredDateFormatIsUsedAndReservedFor()
{
    const QDateTime when(QDate(2025, 8, 10), QTime(6, 26));

    // A pattern deliberately much longer than any locale's short format, so
    // the width claim below cannot pass by accident on a locale whose own
    // dates happen to be wide enough already.
    const QString format = QStringLiteral("dddd d MMMM yyyy 'at' hh:mm:ss");

    QCOMPARE(CardLayout::formatDate(when, format),
             QLocale::system().toString(when, format));
    QVERIFY2(CardLayout::formatDate(when, format)
                 != CardLayout::formatDate(when),
             "a configured format produced the same string as the system one, "
             "so the parameter is being ignored");

    // An empty format is what an absent or rejected config key gives, and it
    // must mean the system format rather than an empty date.
    QCOMPARE(CardLayout::formatDate(when, QString()),
             CardLayout::formatDate(when));

    // The load-bearing half: the reserved width has to follow the SAME format,
    // or a long pattern is elided into a rect sized for a short one. This is
    // the fault that a static, format-independent widest-date sample produces.
    QFont font;
    const int h = CardLayout::heightFor(font);
    CardLayout::Input in = threadInput();
    in.dateFormat = format;
    const CardLayout card = CardLayout::compute(in, QRect(0, 0, 900, h), font);
    QFont bold = font;
    bold.setBold(true);
    QVERIFY2(card.dateRect.width()
                 >= QFontMetrics(bold).horizontalAdvance(
                        CardLayout::formatDate(when, format)),
             "the reserved date width is narrower than the configured format's "
             "own output");

    // And it is genuinely wider than the default's, which proves the width
    // moved with the format rather than a generous constant covering both.
    const CardLayout plain =
        CardLayout::compute(threadInput(), QRect(0, 0, 900, h), font);
    QVERIFY2(card.dateRect.width() > plain.dateRect.width(),
             "a longer date format reserved no more width than the default");
}

void TestCardLayout::theDateFitsWhenTheCardIsBold()
{
    // An UNREAD card draws BOLD, and bold is wider. The layout is computed from
    // option.font, which is the view's regular font, while the text is painted
    // with the font initStyleOption resolved from the model's Qt::FontRole. So
    // a date measured regular and drawn bold overflows its rect: measured at
    // 154px reserved against 170px needed, which clipped the leading digit of
    // the year off every unread card.
    //
    // The fix is in the layout rather than in the delegate: it reserves the
    // BOLD width whatever font it is handed, so the two can never disagree.
    QFont regular;
    regular.setBold(false);
    QFont bold = regular;
    bold.setBold(true);

    const QString sample = QStringLiteral("2025-08-10 06:26");
    const int boldWidth = QFontMetrics(bold).horizontalAdvance(sample);

    // Guard: bold must actually be wider here, or this asserts nothing.
    QVERIFY2(boldWidth > QFontMetrics(regular).horizontalAdvance(sample),
             "bold is not wider than regular in this environment, so this test "
             "cannot detect the overflow it exists for");

    const int h = CardLayout::heightFor(regular);
    const CardLayout card =
        CardLayout::compute(threadInput(), QRect(0, 0, 400, h), regular);

    QVERIFY2(card.dateRect.width() >= boldWidth,
             qPrintable(QStringLiteral("a layout computed from the REGULAR "
                                       "font reserves %1px, and the date needs "
                                       "%2px when the card draws bold")
                            .arg(card.dateRect.width())
                            .arg(boldWidth)));
}

QTEST_MAIN(TestCardLayout)
#include "test_cardlayout.moc"
