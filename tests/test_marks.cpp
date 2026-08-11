/*
 * qtmaildir - a Qt6 GUI for a local notmuch-indexed Maildir
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "marks.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

/// Counts pixels with any alpha at all.
///
/// "Rendering probes lie" in CLAUDE.md is about probes over widgets, where a
/// blank result is more likely a broken probe than broken code. This one is
/// safe for the opposite reason: the input is a fixed SVG payload and a
/// transparent pixmap this test creates itself, with no widget, no exposure and
/// no viewport to come out empty. Every assertion below still states the ink it
/// expects to find before drawing a conclusion from ink it does not.
static int inkPixels(const QImage &image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0)
                ++count;
        }
    }
    return count;
}

static QImage renderMark(Marks::Mark mark, int side = 64,
                         const QColor &color = Qt::black)
{
    return Marks::pixmap(mark, QSize(side, side), color).toImage();
}

class TestMarks : public QObject
{
    Q_OBJECT

private slots:
    void everyMarkHasAPayload();
    void everyMarkDrawsSomething();
    void marksAreRecolouredRatherThanShippedPerTheme();
    void theExpanderPairIsTheSameWeightInBothStates();
    void passedAndRepliedAreMirrorsOfEachOther();
    void aMarkIsDistinguishableFromEveryOther();
    void paintCentresTheMarkInItsRect();
    void anEmptySizeOrInvalidColourYieldsNothing();
};

void TestMarks::everyMarkHasAPayload()
{
    // A missing case in the switch returns an empty QByteArray, which
    // QSvgRenderer accepts and renders as nothing. That failure is silent
    // everywhere else, so it is caught here first.
    const QList<Marks::Mark> all = {
        Marks::Mark::Attachment,         Marks::Mark::Flagged,
        Marks::Mark::Passed,             Marks::Mark::Replied,
        Marks::Mark::ExpanderCollapsed,  Marks::Mark::ExpanderExpanded,
    };

    for (const Marks::Mark mark : all) {
        const QByteArray payload = Marks::svg(mark);
        QVERIFY2(!payload.isEmpty(),
                 qPrintable(QStringLiteral("mark %1 has no payload")
                                .arg(static_cast<int>(mark))));
        QVERIFY(payload.contains("<svg"));
        // The recolouring in pixmap() depends on this: a payload that named a
        // literal colour would ignore the palette and stay that colour on both
        // themes.
        QVERIFY2(payload.contains("currentColor"),
                 qPrintable(QStringLiteral("mark %1 does not paint with "
                                           "currentColor, so it cannot be "
                                           "recoloured")
                                .arg(static_cast<int>(mark))));
    }
}

void TestMarks::everyMarkDrawsSomething()
{
    // The guard the rest of this file needs: a probe that cannot find ink where
    // ink certainly exists is broken, and would pass every "differs from"
    // assertion below by finding nothing anywhere.
    const QList<QPair<Marks::Mark, QString>> all = {
        { Marks::Mark::Attachment, QStringLiteral("attachment") },
        { Marks::Mark::Flagged, QStringLiteral("flagged") },
        { Marks::Mark::Passed, QStringLiteral("passed") },
        { Marks::Mark::Replied, QStringLiteral("replied") },
        { Marks::Mark::ExpanderCollapsed, QStringLiteral("expander-collapsed") },
        { Marks::Mark::ExpanderExpanded, QStringLiteral("expander-expanded") },
    };

    for (const auto &[mark, name] : all) {
        const QImage image = renderMark(mark);
        QVERIFY2(!image.isNull(), qPrintable(name + QStringLiteral(" is null")));
        const int ink = inkPixels(image);
        QVERIFY2(ink > 100,
                 qPrintable(QStringLiteral("%1 drew %2 ink pixels at 64x64, "
                                           "which is a blank or near-blank "
                                           "render")
                                .arg(name)
                                .arg(ink)));
    }
}

void TestMarks::marksAreRecolouredRatherThanShippedPerTheme()
{
    // One asset serves a light and a dark palette. The payload paints with
    // currentColor, which QSvgRenderer renders BLACK rather than resolving, so
    // without the SourceIn composite every mark would be black on both themes
    // and invisible on a dark one.
    const QImage light = renderMark(Marks::Mark::Flagged, 64, QColor(Qt::white));
    const QImage dark = renderMark(Marks::Mark::Flagged, 64, QColor(Qt::black));

    QCOMPARE(inkPixels(light), inkPixels(dark));   // same shape

    // Find a pixel the shape actually covers and compare the colour there.
    // Sampling a fixed coordinate would risk landing outside the star.
    bool sampled = false;
    for (int y = 0; y < light.height() && !sampled; ++y) {
        for (int x = 0; x < light.width() && !sampled; ++x) {
            if (qAlpha(light.pixel(x, y)) != 255)
                continue;
            const QRgb lit = light.pixel(x, y);
            const QRgb unlit = dark.pixel(x, y);
            QVERIFY2(qRed(lit) > 200 && qGreen(lit) > 200 && qBlue(lit) > 200,
                     "the white request did not produce a white mark");
            QVERIFY2(qRed(unlit) < 50 && qGreen(unlit) < 50 && qBlue(unlit) < 50,
                     "the black request did not produce a black mark");
            sampled = true;
        }
    }
    QVERIFY2(sampled, "no fully opaque pixel found, so nothing was compared");
}

void TestMarks::theExpanderPairIsTheSameWeightInBothStates()
{
    // The expanded triangle is the collapsed one rotated 90 degrees about the
    // centre, so neither state can read as heavier than the other. Asserted as
    // equal ink rather than by eye, and it is the property most easily lost by
    // hand-editing one of the two paths.
    const int collapsed = inkPixels(renderMark(Marks::Mark::ExpanderCollapsed));
    const int expanded = inkPixels(renderMark(Marks::Mark::ExpanderExpanded));

    QVERIFY2(collapsed > 0 && expanded > 0, "an expander drew nothing");

    // Not exactly equal: antialiasing along a rotated edge differs by a few
    // pixels. 2% is far tighter than any real weight difference would be.
    const double ratio = double(qAbs(collapsed - expanded))
                         / double(qMax(collapsed, expanded));
    QVERIFY2(ratio < 0.02,
             qPrintable(QStringLiteral("expander states differ in weight: %1 "
                                       "against %2 ink pixels")
                            .arg(collapsed)
                            .arg(expanded)));
}

void TestMarks::passedAndRepliedAreMirrorsOfEachOther()
{
    // Item 69 wants these two to read as one pair. They are mirrors about
    // x = 8, so mirroring one must reproduce the other; a hand edit to one
    // alone would break the pairing while leaving both looking plausible.
    const QImage passed = renderMark(Marks::Mark::Passed);
    const QImage replied = renderMark(Marks::Mark::Replied);

    QVERIFY(inkPixels(passed) > 100);

    // Near-equal, not equal. These are mirrored CURVES, and the rasteriser
    // antialiases a curve and its mirror slightly differently: measured 1383
    // against 1397 at 64x64, a 1% difference that says nothing about the
    // shapes. The pixel-by-pixel comparison below is the assertion that would
    // actually catch a broken pair; this one only rejects a gross weight
    // difference.
    const int passedInk = inkPixels(passed);
    const int repliedInk = inkPixels(replied);
    const double weightRatio = double(qAbs(passedInk - repliedInk))
                               / double(qMax(passedInk, repliedInk));
    QVERIFY2(weightRatio < 0.02,
             qPrintable(QStringLiteral("passed and replied differ in weight: "
                                       "%1 against %2 ink pixels")
                            .arg(passedInk)
                            .arg(repliedInk)));

    const QImage mirrored = passed.mirrored(true, false);
    QCOMPARE(mirrored.size(), replied.size());

    // Compared on alpha rather than on exact pixels: mirroring resamples the
    // antialiased edges, so a strict image equality would fail on a correct
    // pair. A shape mismatch shows up as a large disagreeing area, not a few
    // edge pixels.
    int disagreeing = 0;
    for (int y = 0; y < replied.height(); ++y) {
        for (int x = 0; x < replied.width(); ++x) {
            const int a = qAlpha(mirrored.pixel(x, y)) > 127 ? 1 : 0;
            const int b = qAlpha(replied.pixel(x, y)) > 127 ? 1 : 0;
            if (a != b)
                ++disagreeing;
        }
    }
    const double fraction = double(disagreeing)
                            / double(replied.width() * replied.height());
    QVERIFY2(fraction < 0.02,
             qPrintable(QStringLiteral("passed mirrored does not match replied: "
                                       "%1% of pixels disagree")
                            .arg(fraction * 100, 0, 'f', 1)));
}

void TestMarks::aMarkIsDistinguishableFromEveryOther()
{
    // The defect the glyphs had: an unrenderable codepoint fell back to "*" for
    // BOTH the star and the paperclip, so a flagged thread and one carrying an
    // attachment looked identical. Whatever else changes about these marks, no
    // two may render the same.
    const QList<QPair<Marks::Mark, QString>> all = {
        { Marks::Mark::Attachment, QStringLiteral("attachment") },
        { Marks::Mark::Flagged, QStringLiteral("flagged") },
        { Marks::Mark::Passed, QStringLiteral("passed") },
        { Marks::Mark::Replied, QStringLiteral("replied") },
        { Marks::Mark::ExpanderCollapsed, QStringLiteral("expander-collapsed") },
        { Marks::Mark::ExpanderExpanded, QStringLiteral("expander-expanded") },
    };

    for (int i = 0; i < all.size(); ++i) {
        for (int j = i + 1; j < all.size(); ++j) {
            const QImage a = renderMark(all.at(i).first);
            const QImage b = renderMark(all.at(j).first);
            QVERIFY2(a != b,
                     qPrintable(QStringLiteral("%1 and %2 render identically")
                                    .arg(all.at(i).second, all.at(j).second)));
        }
    }
}

void TestMarks::paintCentresTheMarkInItsRect()
{
    // paint() is what the delegate calls, and it must not stretch a mark to a
    // non-square rect: the message pane's rects are not square.
    QImage canvas(80, 40, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    {
        QPainter painter(&canvas);
        Marks::paint(&painter, QRect(0, 0, 80, 40), Marks::Mark::Flagged,
                     QColor(Qt::black));
    }

    const int ink = inkPixels(canvas);
    QVERIFY2(ink > 50, "paint() drew nothing into the canvas");

    // Sized to the SHORTER side, so nothing is drawn outside a centred 40x40
    // square. Columns outside it must be empty.
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < 20; ++x) {
            QVERIFY2(qAlpha(canvas.pixel(x, y)) == 0,
                     "the mark was stretched past its square, so a non-square "
                     "rect distorts it");
        }
        for (int x = 60; x < canvas.width(); ++x)
            QVERIFY(qAlpha(canvas.pixel(x, y)) == 0);
    }
}

void TestMarks::anEmptySizeOrInvalidColourYieldsNothing()
{
    // Rather than asserting or painting at a garbage size.
    QVERIFY(Marks::pixmap(Marks::Mark::Flagged, QSize(0, 0), Qt::black).isNull());
    QVERIFY(Marks::pixmap(Marks::Mark::Flagged, QSize(16, 16), QColor()).isNull());
}

QTEST_MAIN(TestMarks)
#include "test_marks.moc"
