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

#include "formattoolbar.h"

class TestFormatToolbar : public QObject
{
    Q_OBJECT

private slots:
    void wrappingASelectionKeepsItSelected();
    void wrappingWithNoSelectionPutsTheCursorBetweenTheTokens();
    void wrappingAppliesTheTokenOnBothSides();
    void aBackwardsSelectionWrapsTheSameWordsAsAForwardOne();

    void wrappingTwiceNestsTheTokensAroundTheSameWords();

    void aLinkWithASelectionUsesItAsTheLabel();
    void aLinkWithNoSelectionLeavesTheCursorInTheLabel();
    void aBackwardsSelectionLinksTheSameWordsAsAForwardOne();
    void quotingPrefixesEveryLineTheSelectionTouches();
    void quotingAPartialLineStillQuotesTheWholeLine();
    void quotingASingleLineWithNoSelectionQuotesThatLine();
    void quotingWithTheCursorAtTheEndOfALineQuotesThatLineNotTheNext();
    void quotingSelectsTheQuotedLines();
    void quotingSelectsOnlyTheLineItQuoted();
    void quotingTheLastLineKeepsTheRestOfTheText();
    void quotingAnAlreadyQuotedLineNestsIt();
    void quotingAnEmptyLineLeavesTheMarkerWithoutTrailingSpace();
    void aSelectionPastTheEndIsClamped();
    void aSelectionSplittingASurrogatePairKeepsTheCharacterWhole();
};

void TestFormatToolbar::wrappingASelectionKeepsItSelected()
{
    // The selection is preserved so a second button press applies a second
    // token to the same words: bold then italic, without reselecting.
    const MarkdownFormat::Edit edit = MarkdownFormat::wrap(
        QStringLiteral("make this bold"), 5, 9, QStringLiteral("**"));

    QCOMPARE(edit.text, QStringLiteral("make **this** bold"));
    QCOMPARE(edit.text.mid(edit.selectionStart,
                           edit.selectionEnd - edit.selectionStart),
             QStringLiteral("this"));
}

void TestFormatToolbar::wrappingWithNoSelectionPutsTheCursorBetweenTheTokens()
{
    // The property a user notices immediately when it is wrong: press Bold,
    // start typing, and the words must appear INSIDE the asterisks. A text
    // comparison alone passes whether the cursor is inside or after.
    const MarkdownFormat::Edit edit = MarkdownFormat::wrap(
        QStringLiteral("ab"), 2, 2, QStringLiteral("**"));

    QCOMPARE(edit.text, QStringLiteral("ab****"));
    QCOMPARE(edit.selectionStart, edit.selectionEnd);
    QCOMPARE(edit.selectionStart, 4);

    // Stated as the behaviour rather than the index: typing "x" here must
    // produce "ab**x**".
    QString typed = edit.text;
    typed.insert(edit.selectionStart, QStringLiteral("x"));
    QCOMPARE(typed, QStringLiteral("ab**x**"));
}

void TestFormatToolbar::wrappingAppliesTheTokenOnBothSides()
{
    QCOMPARE(MarkdownFormat::wrap(QStringLiteral("x"), 0, 1,
                                  QStringLiteral("~~")).text,
             QStringLiteral("~~x~~"));
    QCOMPARE(MarkdownFormat::wrap(QStringLiteral("x"), 0, 1,
                                  QStringLiteral("`")).text,
             QStringLiteral("`x`"));
}

void TestFormatToolbar::aBackwardsSelectionWrapsTheSameWordsAsAForwardOne()
{
    // A drag from right to left reports the anchor after the cursor. Qt hands
    // that over as-is, so a transformation that trusts the order inserts the
    // closing token before the opening one and corrupts the buffer.
    const MarkdownFormat::Edit edit = MarkdownFormat::wrap(
        QStringLiteral("make this bold"), 9, 5, QStringLiteral("**"));

    QCOMPARE(edit.text, QStringLiteral("make **this** bold"));
    QCOMPARE(edit.text.mid(edit.selectionStart,
                           edit.selectionEnd - edit.selectionStart),
             QStringLiteral("this"));
}

void TestFormatToolbar::wrappingTwiceNestsTheTokensAroundTheSameWords()
{
    // The reason the selection is preserved at all: bold, then italic,
    // without touching the mouse. Asserting on the second result is what
    // makes the preserved selection load-bearing rather than decorative,
    // since a wrong selection here produces valid-looking but wrong markdown
    // ("make ***this** bold*" or similar).
    //
    // Stacking rather than toggling is the spec's behaviour, not an
    // omission: a second Bold press gives "****this****". A toggle is wanted
    // eventually and would make THIS gesture unreachable, which is the
    // unanswered design question recorded as backlog item 135.
    const MarkdownFormat::Edit first = MarkdownFormat::wrap(
        QStringLiteral("make this bold"), 5, 9, QStringLiteral("**"));
    const MarkdownFormat::Edit second = MarkdownFormat::wrap(
        first.text, first.selectionStart, first.selectionEnd,
        QStringLiteral("*"));

    QCOMPARE(second.text, QStringLiteral("make ***this*** bold"));
    QCOMPARE(second.text.mid(second.selectionStart,
                             second.selectionEnd - second.selectionStart),
             QStringLiteral("this"));
}

void TestFormatToolbar::aLinkWithASelectionUsesItAsTheLabel()
{
    const MarkdownFormat::Edit edit = MarkdownFormat::link(
        QStringLiteral("see the docs"), 8, 12);

    QCOMPARE(edit.text, QStringLiteral("see the [docs]()"));

    // The cursor goes inside the parentheses: the label is written and the
    // URL is what the user still has to type.
    QCOMPARE(edit.selectionStart, edit.selectionEnd);
    QString typed = edit.text;
    typed.insert(edit.selectionStart, QStringLiteral("https://example.org"));
    QCOMPARE(typed, QStringLiteral("see the [docs](https://example.org)"));
}

void TestFormatToolbar::aLinkWithNoSelectionLeavesTheCursorInTheLabel()
{
    // With nothing selected there is no label yet, so the label is what the
    // user types first.
    const MarkdownFormat::Edit edit = MarkdownFormat::link(QString(), 0, 0);

    QCOMPARE(edit.text, QStringLiteral("[]()"));
    QCOMPARE(edit.selectionStart, edit.selectionEnd);
    QString typed = edit.text;
    typed.insert(edit.selectionStart, QStringLiteral("label"));
    QCOMPARE(typed, QStringLiteral("[label]()"));
}

void TestFormatToolbar::aBackwardsSelectionLinksTheSameWordsAsAForwardOne()
{
    const MarkdownFormat::Edit edit = MarkdownFormat::link(
        QStringLiteral("see the docs"), 12, 8);

    QCOMPARE(edit.text, QStringLiteral("see the [docs]()"));
    QString typed = edit.text;
    typed.insert(edit.selectionStart, QStringLiteral("https://example.org"));
    QCOMPARE(typed, QStringLiteral("see the [docs](https://example.org)"));
}

void TestFormatToolbar::quotingPrefixesEveryLineTheSelectionTouches()
{
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("one\ntwo\nthree"), 0, 7);

    QCOMPARE(edit.text, QStringLiteral("> one\n> two\nthree"));
}

void TestFormatToolbar::quotingAPartialLineStillQuotesTheWholeLine()
{
    // A selection from the middle of one line into the middle of the next
    // must quote both whole lines. Quoting half a line produces markdown that
    // means something else entirely.
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("one\ntwo\nthree"), 1, 5);

    QCOMPARE(edit.text, QStringLiteral("> one\n> two\nthree"));
}

void TestFormatToolbar::quotingASingleLineWithNoSelectionQuotesThatLine()
{
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("one\ntwo"), 5, 5);

    QCOMPARE(edit.text, QStringLiteral("one\n> two"));
}

void TestFormatToolbar::quotingWithTheCursorAtTheEndOfALineQuotesThatLineNotTheNext()
{
    // Position 3 is the end of "one", immediately BEFORE the newline, so the
    // cursor is on the first line. Searching backwards from the cursor itself
    // rather than from one before it finds that newline and quotes the SECOND
    // line, which is the line the user is not on. The off-by-one is invisible
    // in every other case because no newline sits at the search position.
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("one\ntwo"), 3, 3);

    QCOMPARE(edit.text, QStringLiteral("> one\ntwo"));
}

void TestFormatToolbar::quotingSelectsTheQuotedLines()
{
    // The quoted block stays selected, so pressing Quote again nests it and
    // a following transformation applies to the same lines.
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("one\ntwo\nthree"), 1, 5);

    QCOMPARE(edit.text.mid(edit.selectionStart,
                           edit.selectionEnd - edit.selectionStart),
             QStringLiteral("> one\n> two"));
}

void TestFormatToolbar::quotingTheLastLineKeepsTheRestOfTheText()
{
    // No trailing newline after the last line, so the end-of-text search
    // returns -1 and an unguarded implementation truncates everything from
    // the selection onwards.
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("one\ntwo\nthree"), 9, 9);

    QCOMPARE(edit.text, QStringLiteral("one\ntwo\n> three"));
}

void TestFormatToolbar::quotingSelectsOnlyTheLineItQuoted()
{
    // The line quoted here is the SECOND one, so a selection that wrongly
    // starts at 0 is distinguishable from a correct one. The existing
    // quotingSelectsTheQuotedLines fixture starts on the first line, where a
    // hardcoded 0 and the right answer coincide: that coincidence let a
    // mutation replacing firstLineStart with 0 pass the whole suite.
    //
    // The damage is not cosmetic. With the wrong selection a second Quote
    // press quotes a line the user never selected, and a following Bold
    // bolds the wrong text.
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("one\ntwo\nthree"), 5, 5);

    QCOMPARE(edit.text, QStringLiteral("one\n> two\nthree"));
    QCOMPARE(edit.text.mid(edit.selectionStart,
                           edit.selectionEnd - edit.selectionStart),
             QStringLiteral("> two"));
}

void TestFormatToolbar::quotingAnAlreadyQuotedLineNestsIt()
{
    // Nests rather than toggling, per the spec, which states there is
    // deliberately no live toggle that inserts and removes the quote while
    // editing. A second press deepens the quote. Backlog item 135 holds the
    // toggle design if that is ever revisited.
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("> one"), 0, 5);

    QCOMPARE(edit.text, QStringLiteral("> > one"));
}

void TestFormatToolbar::quotingAnEmptyLineLeavesTheMarkerWithoutTrailingSpace()
{
    // A blank line inside a quoted block is what continues the block in
    // markdown, so it gets the marker. "> " with nothing after it is trailing
    // whitespace that several editors and mail clients strip, which would
    // break the block; the marker is written bare.
    const MarkdownFormat::Edit edit = MarkdownFormat::quote(
        QStringLiteral("one\n\ntwo"), 0, 8);

    QCOMPARE(edit.text, QStringLiteral("> one\n>\n> two"));
}

void TestFormatToolbar::aSelectionPastTheEndIsClamped()
{
    // A stale selection outliving an edit to the buffer would otherwise index
    // past the end. QString tolerates that in some calls and not in others,
    // so it is clamped once at the entry rather than relied on per call.
    QCOMPARE(MarkdownFormat::wrap(QStringLiteral("ab"), 0, 99,
                                  QStringLiteral("**")).text,
             QStringLiteral("**ab**"));
    QCOMPARE(MarkdownFormat::link(QStringLiteral("ab"), -5, 99).text,
             QStringLiteral("[ab]()"));
    QCOMPARE(MarkdownFormat::quote(QStringLiteral("ab"), -5, 99).text,
             QStringLiteral("> ab"));
}

void TestFormatToolbar::aSelectionSplittingASurrogatePairKeepsTheCharacterWhole()
{
    // An emoji is two UTF-16 code units, so a boundary at 4 lands BETWEEN
    // them. Inserting there splits the character: the result is invalid
    // UTF-16 and the emoji is destroyed, not merely moved.
    //
    // Not reachable by arrow key or mouse, which both move in whole clusters,
    // but QTextCursor::setPosition accepts it, so anything computing a
    // position arithmetically gets there: a draft restore, a find/replace, a
    // template insertion.
    const QString emoji = QString::fromUcs4(U"\U0001F600");
    const QString text = QStringLiteral("hi ") + emoji + QStringLiteral(" there");
    QCOMPARE(text.size(), 11);
    QVERIFY(text.at(3).isHighSurrogate());
    QVERIFY(text.at(4).isLowSurrogate());

    // Boundary inside the pair on the closing side.
    const MarkdownFormat::Edit a =
        MarkdownFormat::wrap(text, 3, 4, QStringLiteral("**"));
    QVERIFY2(a.text.isValidUtf16(), "wrap split the surrogate pair");
    QVERIFY2(a.text.contains(emoji), "wrap destroyed the character");

    // Boundary inside the pair on the opening side.
    const MarkdownFormat::Edit b =
        MarkdownFormat::wrap(text, 4, 5, QStringLiteral("**"));
    QVERIFY2(b.text.isValidUtf16(), "wrap split the surrogate pair");
    QVERIFY2(b.text.contains(emoji), "wrap destroyed the character");

    const MarkdownFormat::Edit c = MarkdownFormat::link(text, 3, 4);
    QVERIFY2(c.text.isValidUtf16(), "link split the surrogate pair");
    QVERIFY2(c.text.contains(emoji), "link destroyed the character");

    // A COLLAPSED cursor inside the pair must stay collapsed. Nudging its two
    // ends in opposite directions would keep the character whole while
    // turning "insert an empty pair here" into "wrap the emoji", which is a
    // character the user never selected.
    const MarkdownFormat::Edit e =
        MarkdownFormat::wrap(text, 4, 4, QStringLiteral("**"));
    QVERIFY2(e.text.isValidUtf16(), "wrap split the surrogate pair");
    QCOMPARE(e.text, QStringLiteral("hi ****") + emoji + QStringLiteral(" there"));
    QCOMPARE(e.selectionStart, e.selectionEnd);
    QString typedInto = e.text;
    typedInto.insert(e.selectionStart, QStringLiteral("x"));
    QCOMPARE(typedInto,
             QStringLiteral("hi **x**") + emoji + QStringLiteral(" there"));

    // quote() snaps to line boundaries, so it is immune by construction.
    // Asserted rather than assumed, so a later change to how it widens the
    // selection cannot quietly lose that.
    const MarkdownFormat::Edit d = MarkdownFormat::quote(text, 3, 4);
    QVERIFY2(d.text.isValidUtf16(), "quote split the surrogate pair");
    QCOMPARE(d.text, QStringLiteral("> ") + text);
}

QTEST_APPLESS_MAIN(TestFormatToolbar)
#include "test_formattoolbar.moc"
