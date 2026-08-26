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

#include <QTest>

#include "avatar.h"

class TestAvatar : public QObject
{
    Q_OBJECT

private slots:
    void twoWordNameTakesOneLetterFromEach();
    void oneWordNameTakesItsFirstTwoLetters();
    void bareAddressTakesLocalAndDomain();
    void nothingUsableFallsBackToTheAccountLabel();
    void initialsAreAlwaysTwoLetters();
    void aRawFromHeaderIsNotSplitOnItsBracket();
    void aCommaJoinedAuthorListTakesTheFirstAuthor();
    void aSeparatorIsNotAWord();
    void aDisplayNameMeansAPerson();
    void theListOverridesADisplayName();
    void aColourIsStablePerAddress();
    void aPixmapIsStableAndDiffersPerSeed();
    void bothTwoToneHuesReachTheFace();
};

void TestAvatar::twoWordNameTakesOneLetterFromEach()
{
    QCOMPARE(Avatar::initialsFor(QStringLiteral("John Doe"),
                                 QStringLiteral("john@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("JD"));
    // Three words still take the FIRST two, not the first and last.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("Maria Grazia Rossi"),
                                 QStringLiteral("maria@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("MG"));
}

void TestAvatar::oneWordNameTakesItsFirstTwoLetters()
{
    QCOMPARE(Avatar::initialsFor(QStringLiteral("Cofidis"),
                                 QStringLiteral("noreply@cofidis.it"),
                                 QStringLiteral("Work")),
             QStringLiteral("CO"));
}

void TestAvatar::bareAddressTakesLocalAndDomain()
{
    QCOMPARE(Avatar::initialsFor(QString(),
                                 QStringLiteral("noreply@cofidis.it"),
                                 QStringLiteral("Work")),
             QStringLiteral("NC"));
}

void TestAvatar::nothingUsableFallsBackToTheAccountLabel()
{
    // No name and no address at all: the account's label is the last resort,
    // so a card always carries a squircle rather than a hole.
    QCOMPARE(Avatar::initialsFor(QString(), QString(),
                                 QStringLiteral("Work")),
             QStringLiteral("WO"));
    // And with nothing whatsoever, still two characters rather than empty.
    QCOMPARE(Avatar::initialsFor(QString(), QString(), QString()).size(), 2);
}

void TestAvatar::initialsAreAlwaysTwoLetters()
{
    // The shape is the point: every squircle reads the same. An address with
    // no domain, a one-letter local part and a name of one letter all still
    // produce two characters.
    const QStringList names { QString(), QStringLiteral("X"),
                              QStringLiteral("A B") };
    const QStringList addresses { QStringLiteral("a@b.org"),
                                  QStringLiteral("malformed"),
                                  QString() };
    for (const QString &name : names) {
        for (const QString &address : addresses) {
            const QString initials =
                Avatar::initialsFor(name, address, QStringLiteral("Acct"));
            QCOMPARE(initials.size(), 2);
        }
    }
}

void TestAvatar::aRawFromHeaderIsNotSplitOnItsBracket()
{
    // A reply row's first line is the RAW header, so a naive space split gave
    // the name's first letter and a literal `<`.
    QCOMPARE(Avatar::initialsFor(
                 QStringLiteral("tsujan <notifications@github.com>"),
                 QStringLiteral("notifications@github.com"),
                 QStringLiteral("Work")),
             QStringLiteral("TS"));
    // A bare address in the name's place is not a name: the address branch
    // answers, rather than the local part's first two letters.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("info@moomhotel.com"),
                                 QStringLiteral("info@moomhotel.com"),
                                 QStringLiteral("Work")),
             QStringLiteral("IM"));
    // And the fill agrees: neither of those is a display name.
    QCOMPARE(Avatar::fillFor(QStringLiteral("info@moomhotel.com"), false),
             Avatar::Fill::TwoTone);
}

void TestAvatar::aCommaJoinedAuthorListTakesTheFirstAuthor()
{
    // notmuch's author summary joins participants with a comma, so one letter
    // from each gave initials belonging to two different people.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("Standreas, tsujan"),
                                 QStringLiteral("notifications@github.com"),
                                 QStringLiteral("Work")),
             QStringLiteral("ST"));
    // A QUOTED name may legally contain a comma and must survive whole.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("\"Rossi, Mario\""),
                                 QStringLiteral("m@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("RM"));
}

void TestAvatar::aSeparatorIsNotAWord()
{
    // `INE - Expert IT Training` took the dash as its second word and drew
    // `I-`. A word has to carry a letter or a digit.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("INE - Expert IT Training"),
                                 QStringLiteral("news@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("IE"));
    // Leading punctuation is trimmed rather than disqualifying the word.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("(Acme) Support"),
                                 QStringLiteral("s@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("AS"));
    // Punctuation ONLY is no name at all: the address answers.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("- ---"),
                                 QStringLiteral("news@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("NE"));
}

void TestAvatar::aDisplayNameMeansAPerson()
{
    // The case the user asked for by name: a corporate address that presents
    // itself as a person reads as a person.
    QCOMPARE(Avatar::fillFor(QStringLiteral("Ian Farrell"), false),
             Avatar::Fill::Identicon);
    QCOMPARE(Avatar::fillFor(QString(), false), Avatar::Fill::TwoTone);
}

void TestAvatar::theListOverridesADisplayName()
{
    // A listed address stays a business even when it sets a friendly name.
    QCOMPARE(Avatar::fillFor(QStringLiteral("Cofidis"), true),
             Avatar::Fill::TwoTone);
}

void TestAvatar::aColourIsStablePerAddress()
{
    const QColor first = Avatar::colourFor(QStringLiteral("a@example.org"));
    const QColor again = Avatar::colourFor(QStringLiteral("a@example.org"));
    QCOMPARE(first, again);
    QVERIFY(first.isValid());
    QVERIFY(Avatar::colourFor(QStringLiteral("b@example.org")) != first);
}

void TestAvatar::aPixmapIsStableAndDiffersPerSeed()
{
    const QFont font;
    const QPixmap first = Avatar::pixmapFor(QStringLiteral("a@example.org"),
                                            QStringLiteral("AE"),
                                            Avatar::Fill::Identicon, 44, font);
    QCOMPARE(first.size(), QSize(44, 44));
    QVERIFY(!first.isNull());

    const QPixmap again = Avatar::pixmapFor(QStringLiteral("a@example.org"),
                                            QStringLiteral("AE"),
                                            Avatar::Fill::Identicon, 44, font);
    // Same seed, same image, byte for byte: the identity must not drift
    // between repaints.
    QCOMPARE(first.toImage(), again.toImage());

    const QPixmap other = Avatar::pixmapFor(QStringLiteral("b@example.org"),
                                            QStringLiteral("AE"),
                                            Avatar::Fill::Identicon, 44, font);
    // Different sender, different image, even with identical initials.
    QVERIFY(first.toImage() != other.toImage());

    const QPixmap twoTone = Avatar::pixmapFor(QStringLiteral("a@example.org"),
                                              QStringLiteral("AE"),
                                              Avatar::Fill::TwoTone, 44, font);
    // The two fills are actually different renderings, not one with a flag.
    QVERIFY(first.toImage() != twoTone.toImage());
}

void TestAvatar::bothTwoToneHuesReachTheFace()
{
    // The split has to cross the squircle, not graze its edge. Building the
    // gradient axis as a RADIUS from the centre put the 0.5 stop on the
    // boundary, so one hue filled almost the whole face and the fill read as
    // flat: reported against noreply@cofidis.it. A colour count is what
    // distinguishes the two, since both versions paint every pixel.
    //
    // Several seeds, because one unlucky angle proves nothing either way.
    const QStringList seeds { QStringLiteral("noreply@cofidis.it"),
                              QStringLiteral("a@example.org"),
                              QStringLiteral("b@example.org"),
                              QStringLiteral("c@example.org") };
    for (const QString &seed : seeds) {
        const QImage face =
            Avatar::pixmapFor(seed, QStringLiteral("XX"),
                              Avatar::Fill::TwoTone, 64, QFont()).toImage();

        // The two hues, as painted. Sampled by counting pixels of each rather
        // than probing a corner: which corner gets which hue depends on the
        // hashed angle.
        const QColor base = Avatar::colourFor(seed);
        const QColor dark = base.darker(135);
        int light = 0, shade = 0;
        for (int y = 0; y < face.height(); ++y) {
            for (int x = 0; x < face.width(); ++x) {
                const QColor pixel = face.pixelColor(x, y);
                if (pixel.alpha() < 255)
                    continue;  // The squircle's antialiased corners.
                // Nearest of the two, not an exact match: the gradient
                // interpolates in premultiplied space and the pixel format
                // rounds, so an exact compare finds NEITHER hue and the probe
                // reports 0 against 0 whatever the code does.
                const int toBase = qAbs(pixel.red() - base.red())
                                   + qAbs(pixel.green() - base.green())
                                   + qAbs(pixel.blue() - base.blue());
                const int toDark = qAbs(pixel.red() - dark.red())
                                   + qAbs(pixel.green() - dark.green())
                                   + qAbs(pixel.blue() - dark.blue());
                if (toBase < toDark)
                    ++light;
                else
                    ++shade;
            }
        }

        // A fifth of the face each: enough that neither is a sliver, loose
        // enough that the hashed angle is free to put the split anywhere.
        const int fifth = face.width() * face.height() / 5;
        QVERIFY2(light > fifth && shade > fifth,
                 qPrintable(QStringLiteral("%1: one hue took the face, %2 "
                                           "light against %3 dark")
                                .arg(seed).arg(light).arg(shade)));
    }
}

QTEST_MAIN(TestAvatar)
#include "test_avatar.moc"
