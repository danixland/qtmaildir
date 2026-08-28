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
    void marksReserveTheirOwnSpaceRatherThanOverlappingTheSubject();
    void anAbsentMarkReservesNothing();
    void theFlagIndentsTheSubjectRatherThanSittingOnIt();
    void marksDoNotCollideWithEachOtherOrTheExpander();
    void dateIsFlushRight();
    void threadCardCarriesAnAccentBar();
    void replyCardCarriesNoAccentBar();
    void theDateFitsWhenTheCardIsBold();
    void theDateFollowsTheSystemLocale();
    void aConfiguredDateFormatIsUsedAndReservedFor();
    void everyRowCarriesAnAvatar();
    void theAvatarPushesTheContentRight();
    void theAvatarFollowsTheIndent();
    void theAvatarIsSquareAndFitsTheCard();
};

namespace {

CardLayout::Input threadInput()
{
    CardLayout::Input in;
    in.isMessage = false;
    in.depth = 0;
    in.messageCount = 3;
    return in;
}

CardLayout::Input replyInput(int depth)
{
    CardLayout::Input in;
    in.isMessage = true;
    in.depth = depth;
    in.messageCount = 0;
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
    noRepliesIn.messageCount = 0;
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
    in.messageCount = 0;

    const CardLayout card = CardLayout::compute(in, QRect(0, 0, 400, h), font);
    QVERIFY(card.expanderRect.isEmpty());
}

void TestCardLayout::theExpanderReadsAsAPillWithAWord()
{
    // A bare "3" beside the subject reads as an unexplained number and gives
    // no hint that it can be clicked. The label carries the word, and the rect
    // carries padding for the pill drawn behind it.
    //
    // NO triangle in the label since item 70: it is a drawn mark now, and a
    // glyph left here would be a second triangle beside the drawn one. The
    // label is the words alone, and the state no longer changes it.
    QCOMPARE(CardLayout::expanderLabel(3, false), QStringLiteral("3 messages"));
    QCOMPARE(CardLayout::expanderLabel(3, true), QStringLiteral("3 messages"));

    // Singular, because "1 messages" is the kind of detail that makes an
    // interface look unfinished.
    QCOMPARE(CardLayout::expanderLabel(1, false), QStringLiteral("1 message"));

    // The glyphs are gone from the label entirely. Asserted rather than assumed,
    // because a stray one would draw underneath the mark and look like a
    // rendering fault rather than like a stale string.
    QVERIFY(!CardLayout::expanderLabel(3, false).contains(QChar(0x25b8)));
    QVERIFY(!CardLayout::expanderLabel(3, true).contains(QChar(0x25be)));

    const QFont font;
    const int h = CardLayout::heightFor(font);
    const CardLayout card =
        CardLayout::compute(threadInput(), QRect(0, 0, 400, h), font);
    const QFontMetrics small(CardLayout::smallFont(font));

    // The rect must hold the label, the drawn triangle, the gap between them
    // AND the padding, or the pill's background is narrower than what sits on
    // it. The triangle's width came free from the text metrics while it was a
    // glyph in the label; since item 70 it is reserved explicitly, and this is
    // what would catch it being forgotten.
    QVERIFY2(card.expanderRect.width()
                 >= small.horizontalAdvance(CardLayout::expanderLabel(3, false))
                        + small.ascent() + CardLayout::kMarkGap
                        + CardLayout::kPillPaddingX * 2,
             "the expander rect is too narrow for its label, its triangle and "
             "its padding");

    // And it must NOT change width when the card opens: a pill that resized on
    // click would shift the subject's elision under the pointer.
    CardLayout::Input open = threadInput();
    const CardLayout expanded =
        CardLayout::compute(open, QRect(0, 0, 400, h), font);
    QCOMPARE(expanded.expanderRect.width(), card.expanderRect.width());
}

void TestCardLayout::marksReserveTheirOwnSpaceRatherThanOverlappingTheSubject()
{
    // Item 70. The marks were glyphs INSIDE the subject string until then, so
    // their width came free from the text metrics and no arrangement was
    // needed. As drawn icons they occupy rects, and a subject sized as though
    // they were absent runs underneath them. This is the assertion that would
    // catch that, and it cannot be made anywhere else: a rendering probe over
    // the delegate would show overlapping ink as a plausible-looking card.
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const QRect rect(0, 0, 400, h);

    CardLayout::Input bare = threadInput();
    CardLayout::Input marked = threadInput();
    marked.hasAttachment = true;
    marked.passed = true;
    marked.replied = true;

    const CardLayout without = CardLayout::compute(bare, rect, font);
    const CardLayout with = CardLayout::compute(marked, rect, font);

    QVERIFY(!with.attachmentRect.isEmpty());
    QVERIFY(!with.passedRect.isEmpty());
    QVERIFY(!with.repliedRect.isEmpty());

    // The subject gives up exactly the room the marks take.
    QVERIFY2(with.subjectRect.width() < without.subjectRect.width(),
             "the marks reserved no space, so the subject is sized as though "
             "they were not there and its text runs underneath them");

    // And every mark begins after the subject ends. Compared as exclusive
    // edges: QRect::right() is inclusive, which is the trap this file already
    // documents for the date.
    const int subjectEnd = with.subjectRect.left() + with.subjectRect.width();
    QVERIFY2(with.attachmentRect.left() >= subjectEnd,
             "the attachment mark overlaps the subject");
    QVERIFY2(with.passedRect.left() >= subjectEnd, "passed overlaps the subject");
    QVERIFY2(with.repliedRect.left() >= subjectEnd,
             "replied overlaps the subject");

    // Square, so nothing is drawn stretched.
    QCOMPARE(with.attachmentRect.width(), with.attachmentRect.height());
}

void TestCardLayout::anAbsentMarkReservesNothing()
{
    // A card with no attachment must not leave a hole where the mark would be:
    // the subject is the elastic part of line two and every reserved-but-unused
    // pixel comes out of it.
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const QRect rect(0, 0, 400, h);

    const CardLayout card = CardLayout::compute(threadInput(), rect, font);

    QVERIFY(card.flagRect.isEmpty());
    QVERIFY(card.attachmentRect.isEmpty());
    QVERIFY(card.passedRect.isEmpty());
    QVERIFY(card.repliedRect.isEmpty());

    // Guard: the same input WITH a mark must produce one, or the assertions
    // above pass against a layout that never draws marks at all.
    CardLayout::Input marked = threadInput();
    marked.hasAttachment = true;
    QVERIFY(!CardLayout::compute(marked, rect, font).attachmentRect.isEmpty());
}

void TestCardLayout::theFlagIndentsTheSubjectRatherThanSittingOnIt()
{
    // The flag is the one mark on the LEFT, where its glyph was, so a flagged
    // card still reads flagged from the left edge.
    const QFont font;
    const int h = CardLayout::heightFor(font);
    const QRect rect(0, 0, 400, h);

    CardLayout::Input flagged = threadInput();
    flagged.flagged = true;

    const CardLayout plain = CardLayout::compute(threadInput(), rect, font);
    const CardLayout marked = CardLayout::compute(flagged, rect, font);

    QVERIFY(!marked.flagRect.isEmpty());
    QCOMPARE(marked.flagRect.left(), marked.contentLeft);

    // The subject starts after the flag, rather than at contentLeft with the
    // flag drawn over it.
    QVERIFY2(marked.subjectRect.left() > plain.subjectRect.left(),
             "the flag did not move the subject, so it is drawn on top of it");
    QVERIFY(marked.subjectRect.left()
            >= marked.flagRect.left() + marked.flagRect.width());
}

void TestCardLayout::marksDoNotCollideWithEachOtherOrTheExpander()
{
    // All four marks at once on a card that also has an expander, which is the
    // densest line two can get. Nothing may overlap anything.
    const QFont font;
    const int h = CardLayout::heightFor(font);
    // 460 rather than 400: the pill reads "3 messages" now, a character wider
    // than "3 replies" was, and 400 left the elastic subject narrower than the
    // avatar gutter with all four marks out. The width is a stress value, not
    // a spec.
    const QRect rect(0, 0, 460, h);

    CardLayout::Input in = threadInput();
    in.flagged = true;
    in.hasAttachment = true;
    in.passed = true;
    in.replied = true;

    const CardLayout card = CardLayout::compute(in, rect, font);

    QVERIFY(!card.expanderRect.isEmpty());

    // Left to right: flag, subject, attachment, passed, replied, expander.
    const QList<QRect> ordered = { card.flagRect,   card.subjectRect,
                                   card.attachmentRect, card.passedRect,
                                   card.repliedRect,  card.expanderRect };
    for (int i = 0; i + 1 < ordered.size(); ++i) {
        const QRect &left = ordered.at(i);
        const QRect &right = ordered.at(i + 1);
        QVERIFY2(left.left() + left.width() <= right.left(),
                 qPrintable(QStringLiteral("rect %1 (x %2 w %3) overlaps rect "
                                           "%4 (x %5)")
                                .arg(i)
                                .arg(left.left())
                                .arg(left.width())
                                .arg(i + 1)
                                .arg(right.left())));
    }

    // And the whole line stays inside the card.
    QVERIFY(card.repliedRect.left() + card.repliedRect.width()
            <= card.expanderRect.left());
    QVERIFY(card.expanderRect.left() + card.expanderRect.width()
            <= rect.right() + 1);

    // The subject survives at a usable width rather than being squeezed to
    // nothing by four marks: they are small and fixed, it is the elastic part.
    // Relative rather than absolute: the avatar gutter shifts every text rect
    // right, so a fixed pixel floor like 100 fails on a card that gained a
    // gutter and would pass on one that had not. Comparing against another
    // fixed element of the same card keeps the real invariant: the marks must
    // not leave the subject narrower than the avatar gutter beside it.
    QVERIFY2(card.subjectRect.width() > card.avatarRect.width(),
             "four marks left the subject narrower than the avatar gutter on a "
             "460px card");
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

void TestCardLayout::everyRowCarriesAnAvatar()
{
    const QFont font;
    const QRect rect(0, 0, 600, CardLayout::heightFor(font));

    CardLayout::Input thread;
    const CardLayout rootCard = CardLayout::compute(thread, rect, font);
    QVERIFY(!rootCard.avatarRect.isEmpty());

    // A reply gets one too: it is the row where the sender actually changes.
    CardLayout::Input reply;
    reply.isMessage = true;
    reply.depth = 1;
    const CardLayout replyCard = CardLayout::compute(reply, rect, font);
    QVERIFY(!replyCard.avatarRect.isEmpty());
}

void TestCardLayout::theAvatarPushesTheContentRight()
{
    const QFont font;
    const QRect rect(0, 0, 600, CardLayout::heightFor(font));
    const CardLayout card = CardLayout::compute(CardLayout::Input(), rect, font);

    // The text starts after the squircle, never on it.
    QVERIFY(card.contentLeft >= card.avatarRect.right() + 1);
}

void TestCardLayout::theAvatarFollowsTheIndent()
{
    const QFont font;
    const QRect rect(0, 0, 600, CardLayout::heightFor(font));

    CardLayout::Input shallow;
    shallow.isMessage = true;
    shallow.depth = 1;
    CardLayout::Input deep;
    deep.isMessage = true;
    deep.depth = 3;

    const CardLayout shallowCard = CardLayout::compute(shallow, rect, font);
    const CardLayout deepCard = CardLayout::compute(deep, rect, font);

    // The squircle sits inside the card's own rect and moves with the nesting,
    // which is the same reason contentLeft does. Asserting on the RECT here is
    // safe precisely because it is CardLayout's own output, not a visualRect.
    QVERIFY(deepCard.avatarRect.left() > shallowCard.avatarRect.left());
}

void TestCardLayout::theAvatarIsSquareAndFitsTheCard()
{
    const QFont font;
    const QRect rect(0, 0, 600, CardLayout::heightFor(font));
    const CardLayout card = CardLayout::compute(CardLayout::Input(), rect, font);

    QCOMPARE(card.avatarRect.width(), card.avatarRect.height());
    QVERIFY(card.avatarRect.top() >= rect.top());
    QVERIFY(card.avatarRect.bottom() <= rect.bottom());
}

QTEST_MAIN(TestCardLayout)
#include "test_cardlayout.moc"
