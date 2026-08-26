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

#include "carddelegate.h"

#include "cardlayout.h"
#include "tagchip.h"
#include "tagcolors.h"
#include "threadlistmodel.h"

#include <QTest>

class TestCardDelegate : public QObject
{
    Q_OBJECT

private slots:
    void theAccentLiftsAMutedAccountColour();
    void theAccentKeepsEachAccountTellableApart();
    void anAccountWithNoColourFallsBackToTheNeutralLine();
    void aSiblingChipIsMutedButStaysLegibleAndRecognisable();
    void aSiblingChipFontIsSmallerThanItsOwnTier();
    void aSiblingChipsPaddingShrinksWithItsFont();
    void theFadeEndsAtSixtyPercentOfTheCard();
    void aReplyFadeStartsAtItsOwnSpine();
    void theDelegateAsksForAScaledSquircle();
    void aRowWithNoSenderFallsBackToTheAccount();
};

namespace {

/// The user's five configured account colours, as a realistic sample. They are
/// all mid-tone because they were chosen as CHIP fills, which is the whole
/// reason the bar has to lift them.
const QList<QColor> &sampleAccounts()
{
    static const QList<QColor> colours{
        QColor("#2f6fa8"), QColor("#3d7a4a"), QColor("#a83f2f"),
        QColor("#8a5cb8"), QColor("#b8862f"),
    };
    return colours;
}

float saturationOf(const QColor &c)
{
    float h = 0, s = 0, l = 0, a = 0;
    c.getHslF(&h, &s, &l, &a);
    return s;
}

float lightnessOf(const QColor &c)
{
    float h = 0, s = 0, l = 0, a = 0;
    c.getHslF(&h, &s, &l, &a);
    return l;
}

float hueOf(const QColor &c)
{
    float h = 0, s = 0, l = 0, a = 0;
    c.getHslF(&h, &s, &l, &a);
    return h;
}

}  // namespace

void TestCardDelegate::theAccentLiftsAMutedAccountColour()
{
    // An account colour is chosen to be a chip's fill with legible text on top,
    // so it is mid-tone by construction. Three pixels of a mid-tone colour
    // beside a card's own background barely register, which is what the user
    // reported: "the colours could be a little more vivid".
    //
    // The green is the weakest of the five (S 0.33, L 0.36) and is the one that
    // has to move most.
    const QColor muted("#3d7a4a");
    const QColor accent = CardDelegate::accentLineColour(muted);

    QVERIFY2(saturationOf(accent) > saturationOf(muted),
             "the accent is no more saturated than the chip colour it comes "
             "from, so a muted account stays muted as a 3px bar");
    QVERIFY2(lightnessOf(accent) > lightnessOf(muted),
             "the accent is no lighter than the chip colour, so it cannot "
             "carry on a dark theme");

    // The floor, stated as the numbers that were chosen by rendering all five
    // against both themes. Higher pushed the green toward a neon that no
    // longer matched its own chip.
    QVERIFY(saturationOf(accent) >= 0.65f - 0.01f);
    QVERIFY(lightnessOf(accent) >= 0.50f - 0.01f);

    // A colour already past the floor is left alone: the lift is a floor, not
    // a repaint, or a user who picked a vivid colour would have it changed.
    const QColor alreadyVivid = QColor::fromHslF(0.6f, 0.9f, 0.6f);
    const QColor untouched = CardDelegate::accentLineColour(alreadyVivid);
    QCOMPARE(saturationOf(untouched), saturationOf(alreadyVivid));
    QCOMPARE(lightnessOf(untouched), lightnessOf(alreadyVivid));
}

void TestCardDelegate::theAccentKeepsEachAccountTellableApart()
{
    // The bar's whole job is saying WHICH account, so the lift must not
    // converge two hues. Asserted across the real five rather than one pair:
    // a floor applied to saturation and lightness leaves hue untouched, and
    // this is what proves it stayed that way.
    for (const QColor &configured : sampleAccounts()) {
        const QColor accent = CardDelegate::accentLineColour(configured);
        QVERIFY2(qAbs(hueOf(accent) - hueOf(configured)) < 0.01f,
                 qPrintable(QStringLiteral("account %1 changed hue to %2, so "
                                           "it no longer matches its own chip")
                                .arg(configured.name(), accent.name())));
    }

    // And no two of them collapse onto each other.
    QSet<QRgb> seen;
    for (const QColor &configured : sampleAccounts())
        seen.insert(CardDelegate::accentLineColour(configured).rgb());
    QCOMPARE(seen.size(), sampleAccounts().size());
}

void TestCardDelegate::anAccountWithNoColourFallsBackToTheNeutralLine()
{
    // A thread with no account tag has no colour to lift, and must not end up
    // with a saturated bar invented out of an invalid QColor.
    QCOMPARE(CardDelegate::accentLineColour(QColor()),
             ThreadListModel::threadLineColour());
}

void TestCardDelegate::aSiblingChipIsMutedButStaysLegibleAndRecognisable()
{
    // Item 111: a card shows its own tags at full size and the rest of the
    // conversation's smaller and muted. "Muted" has two hard requirements that
    // a look at the screen will not catch, so they are asserted here.
    for (const QColor &colour : sampleAccounts()) {
        const QColor muted = CardDelegate::mutedChipColour(colour);

        // Actually muted, or the tier is not distinguishable at all.
        QVERIFY2(saturationOf(muted) < saturationOf(colour),
                 qPrintable(QStringLiteral("%1 was not drained at all")
                                .arg(colour.name())));

        // Same HUE. A sibling's `signed` has to stay recognisably the same
        // colour as a full-size `signed` elsewhere in the list, or the muting
        // reads as a different tag rather than a quieter one.
        float h1 = 0, h2 = 0, s = 0, l = 0, a = 0;
        colour.getHslF(&h1, &s, &l, &a);
        muted.getHslF(&h2, &s, &l, &a);
        QVERIFY2(qAbs(h1 - h2) < 0.001f,
                 qPrintable(QStringLiteral("%1 changed hue when muted")
                                .arg(colour.name())));

        // Same LIGHTNESS, which is what keeps the text legible: TagColors
        // picks the text colour from the fill, and a fill that drifted toward
        // black or white could flip that choice or land mid-grey where neither
        // works. Blending toward the background would do exactly that, which
        // is the mistake accentLineColour() records.
        QCOMPARE(lightnessOf(muted), lightnessOf(colour));
        QCOMPARE(TagColors::textColourOn(muted),
                 TagColors::textColourOn(colour));
    }

    // An invalid colour stays invalid rather than becoming a real one.
    QVERIFY(!CardDelegate::mutedChipColour(QColor()).isValid());
}

void TestCardDelegate::aSiblingChipFontIsSmallerThanItsOwnTier()
{
    // Size is what says whose tag a chip is, so the two tiers must differ, and
    // by enough to SEE. The first version subtracted a point from smallFont(),
    // and the user reported the tiers as indistinguishable: on their 14pt
    // desktop that gave 13 and 12, a 7% step.
    //
    // The step is now a fraction of the card font, so it does not shrink as
    // the desktop's font grows. Asserted as a ratio rather than as a size, to
    // keep this about the DISTINCTION rather than about the constant.
    QFont card;
    card.setPointSizeF(14.0);   // The user's own desktop size.
    const qreal own = CardLayout::smallFont(card).pointSizeF();
    const qreal sibling = CardLayout::siblingFont(card).pointSizeF();

    QVERIFY(sibling < own);
    QVERIFY2(sibling < own * 0.85,
             qPrintable(QStringLiteral("sibling %1pt against own %2pt is under "
                                       "a 15%% step, which reads as the same "
                                       "size")
                            .arg(sibling)
                            .arg(own)));

    // Proportional, not a fixed subtraction: the step must survive a larger
    // desktop font rather than becoming proportionally smaller.
    QFont big;
    big.setPointSizeF(28.0);
    QVERIFY(CardLayout::siblingFont(big).pointSizeF()
            < CardLayout::smallFont(big).pointSizeF() * 0.85);

    // The pixel branch too: qt6ct sets fonts in PIXELS, and pointSizeF() is -1
    // for those, so a point-only implementation silently returns the original
    // size and both tiers render identically. CLAUDE.md records this trap.
    QFont pixels;
    pixels.setPixelSize(14);
    QVERIFY(pixels.pointSizeF() < 0);
    QVERIFY2(CardLayout::siblingFont(pixels).pixelSize()
                 < CardLayout::smallFont(pixels).pixelSize(),
             "a pixel-sized desktop font gives both tiers the same size, so "
             "the distinction disappears entirely");

    // Floored rather than shrinking without limit.
    QFont tiny;
    tiny.setPointSizeF(6.0);
    QVERIFY(CardLayout::siblingFont(tiny).pointSizeF() >= 6.0);
}

void TestCardDelegate::aSiblingChipsPaddingShrinksWithItsFont()
{
    // Half of "smaller" is the padding, and leaving it fixed is why the first
    // version still looked the same size. kPaddingX is 9 a side: on a sibling
    // chip that is 18px of padding around roughly 30px of text, so the chip
    // stayed wide while its letters shrank, which reads as "same chip, smaller
    // text" rather than as a smaller chip.
    QFont card;
    card.setPointSizeF(14.0);
    const QFontMetrics ownMetrics(CardLayout::smallFont(card));
    const QFontMetrics siblingMetrics(CardLayout::siblingFont(card));

    const QString tag = QStringLiteral("signed");
    // Through CardDelegate::chipSize(), which is what the paint loop calls.
    // Calling TagChip::sizeFor() directly here proved what THAT function does
    // and nothing about whether the delegate asks it for a scaled padding: a
    // mutation dropping the scale at the call site survived that version of
    // this test.
    const QSize own = CardDelegate::chipSize(ownMetrics, tag, true);
    const QSize scaled = CardDelegate::chipSize(siblingMetrics, tag, false);
    const QSize unscaled = TagChip::sizeFor(siblingMetrics, tag);

    // The font alone is not enough: scaling the padding as well takes off
    // measurably more width.
    QVERIFY2(scaled.width() < unscaled.width(),
             "the padding did not scale, so the chip keeps full-size margins "
             "around smaller letters");
    QVERIFY(scaled.width() < own.width());
    QVERIFY(scaled.height() < own.height());

    // Floored rather than collapsing to nothing: the corner radius is half the
    // height, so a chip with no horizontal padding has its text on the curve.
    const QSize tiny = TagChip::sizeFor(siblingMetrics, tag, 0.0);
    QVERIFY2(tiny.width() > siblingMetrics.horizontalAdvance(tag),
             "a zero scale left no horizontal padding at all, so the text sits "
             "on the chip's rounded end");
}

void TestCardDelegate::theFadeEndsAtSixtyPercentOfTheCard()
{
    const QRect card(0, 0, 500, 60);
    const QRect root = CardDelegate::fadeRectFor(card, QRect());
    // Anchored at the card's RIGHT edge: the hard stop belongs where the card
    // ends, not 60% across it, which read as a slab.
    QCOMPARE(root.right(), card.right());
    QCOMPARE(root.width(), 300);
}

void TestCardDelegate::aReplyFadeStartsAtItsOwnSpine()
{
    const QRect card(0, 0, 500, 60);
    // The innermost spine of a nested reply, which is its own coloured border.
    const QRect spine(80, 0, 2, 60);
    // A spine deep enough to cut into the wash, which starts at 40% here.
    const QRect deep(300, 0, 2, 60);
    const QRect reply = CardDelegate::fadeRectFor(card, deep);

    // Clamped at the spine, so the wash never runs under a reply's own border.
    QCOMPARE(reply.left(), deep.left());
    // Still anchored at the card's right edge, so a deeper reply's wash is
    // shorter rather than displaced.
    QCOMPARE(reply.right(), CardDelegate::fadeRectFor(card, QRect()).right());
    QVERIFY(reply.width() < CardDelegate::fadeRectFor(card, QRect()).width());

    // A shallow spine sits left of where the wash begins and changes nothing.
    const QRect shallow(80, 0, 2, 60);
    QCOMPARE(CardDelegate::fadeRectFor(card, shallow),
             CardDelegate::fadeRectFor(card, QRect()));
}

void TestCardDelegate::theDelegateAsksForAScaledSquircle()
{
    // Asserted through the function the PRODUCTION path calls, not through
    // Avatar::pixmapFor() directly: a test pointed at the function being
    // called into proves what that function does and nothing about whether the
    // delegate asks it for the right thing. CLAUDE.md records a mutation that
    // survived exactly that mistake.
    const QRect card(0, 0, 500, 60);
    const QFont font;
    const CardLayout layout =
        CardLayout::compute(CardLayout::Input(), card, font);

    const QPixmap pixmap = CardDelegate::avatarFor(
        QStringLiteral("john@example.org"), QStringLiteral("John Doe"),
        QStringLiteral("me@example.org"), QStringLiteral("Work"), false,
        layout.avatarRect.width(), font);

    QCOMPARE(pixmap.size(),
             QSize(layout.avatarRect.width(), layout.avatarRect.width()));
}

void TestCardDelegate::aRowWithNoSenderFallsBackToTheAccount()
{
    const QFont font;
    // No sender address at all: the squircle is still drawn, seeded from the
    // account, so a card never shows a hole.
    const QPixmap fallback = CardDelegate::avatarFor(
        QString(), QString(), QStringLiteral("me@example.org"),
        QStringLiteral("Work"), false, 44, font);
    QVERIFY(!fallback.isNull());

    // And it is the ACCOUNT's identity, not an arbitrary one: seeding from the
    // same account twice agrees.
    const QPixmap again = CardDelegate::avatarFor(
        QString(), QString(), QStringLiteral("me@example.org"),
        QStringLiteral("Work"), false, 44, font);
    QCOMPARE(fallback.toImage(), again.toImage());
}

QTEST_MAIN(TestCardDelegate)
#include "test_carddelegate.moc"
