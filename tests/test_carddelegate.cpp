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

#include "threadlistmodel.h"

#include <QTest>

class TestCardDelegate : public QObject
{
    Q_OBJECT

private slots:
    void theAccentLiftsAMutedAccountColour();
    void theAccentKeepsEachAccountTellableApart();
    void anAccountWithNoColourFallsBackToTheNeutralLine();
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

QTEST_MAIN(TestCardDelegate)
#include "test_carddelegate.moc"
