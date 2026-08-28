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

#include <QFontMetrics>
#include <QLocale>

QString CardLayout::formatDate(const QDateTime &date, const QString &format)
{
    // The system locale's own short format by default, not a hardcoded
    // pattern: an Italian desktop writes 10/08/2025, not 2025-08-10, and a mail
    // client that disagrees with every other application on screen is simply
    // wrong. [general] date_format overrides it for a user who wants one
    // specific shape regardless of the locale.
    if (format.isEmpty())
        return QLocale::system().toString(date, QLocale::ShortFormat);
    return QLocale::system().toString(date, format);
}

int CardLayout::markSide(const QFont &font)
{
    // Derived from the font's ascent rather than fixed, so the marks grow with
    // the user's text size. A pixel count settled on one desktop is wrong on
    // the next one, and qt6ct sets fonts in PIXELS, where pointSizeF() returns
    // -1 (see the note in this file's header) which is why the metric and not
    // the size is the thing measured.
    //
    // Ascent rather than height: height includes the descent, which no mark
    // occupies, and marks sized from it read noticeably larger than the text
    // beside them.
    const int side = QFontMetrics(font).ascent();

    // Floor of 8: below that the shapes stop being tellable apart, which is the
    // defect item 70 exists to fix rather than one to reintroduce at a small
    // font size.
    return qMax(8, side);
}

QString CardLayout::expanderLabel(int replyCount, bool expanded)
{
    // "3 replies", not a bare "3". The count alone reads as an unexplained
    // number beside the subject, and the word is what says the card opens.
    //
    // The triangle is NO LONGER part of this string. Item 70 made it a drawn
    // mark, so the pill reserves a rect for it and the delegate paints it; a
    // glyph left here would be a second triangle beside the drawn one. The
    // `expanded` parameter therefore no longer changes the label, and is kept
    // because both states are still measured: the pill must not change width
    // when the card opens, and a caller that stopped passing the state would
    // hide that requirement rather than satisfy it.
    //
    // Not translated through tr() here because CardLayout is a plain struct
    // rather than a QObject; the delegate is where a translated build would
    // wrap this, and the string is deliberately kept in one place so there is
    // exactly one thing to change.
    Q_UNUSED(expanded);
    const QString word = replyCount == 1 ? QStringLiteral("reply")
                                         : QStringLiteral("replies");
    return QStringLiteral("%1 %2").arg(replyCount).arg(word);
}

QString CardLayout::widestDateSample(const QString &format)
{
    // A real date run through the same formatter, with the wide digits and a
    // two-digit day and month, so the reserved width matches what is drawn
    // whatever the locale's pattern turns out to be. Guessing a pattern here
    // would reintroduce the clipping this exists to prevent.
    //
    // Not cached in a static any more: the sample depends on the format, and a
    // single static computed for whichever format arrived first would reserve
    // the system format's width for a custom pattern. The formatter is one
    // QLocale call per row, which is the same cost the date itself already pays.
    const QDateTime wide(QDate(2028, 12, 28), QTime(22, 58));
    return formatDate(wide, format);
}

QFont CardLayout::smallFont(const QFont &cardFont)
{
    QFont small = cardFont;
    // Derived from the card's font rather than fixed, so it follows the
    // desktop's font size instead of shrinking to nothing on a large one.
    //
    // pointSizeF() returns -1 for a font set in PIXELS, which qt6ct and some
    // styles do. Subtracting from -1 would ask for an invalid size and Qt
    // would silently keep the original, making the small font the same size as
    // the card's; the pixel branch avoids that.
    if (small.pointSizeF() > 0.0)
        small.setPointSizeF(qMax(6.0, cardFont.pointSizeF() - 1.0));
    else if (small.pixelSize() > 0)
        small.setPixelSize(qMax(8, cardFont.pixelSize() - 1));
    return small;
}

int CardLayout::heightFor(const QFont &font)
{
    const QFontMetrics metrics(font);
    const QFontMetrics smallMetrics(smallFont(font));
    // Two lines at the card's font, one at the small one, plus the padding
    // above the first and below the last.
    return kPaddingY * 2 + metrics.height() * 2 + smallMetrics.height();
}

CardLayout CardLayout::compute(const Input &input, const QRect &rect,
                               const QFont &font)
{
    CardLayout out;
    const QFontMetrics metrics(font);
    const QFontMetrics smallMetrics(smallFont(font));

    out.totalHeight = rect.height();

    // The accent bar sits flush against the card's left edge, on thread cards
    // only, and everything else starts after it so no text sits on the colour.
    if (!input.isMessage) {
        out.accentRect =
            QRect(rect.left(), rect.top(), kAccentWidth, rect.height());
    }
    const int textLeft = rect.left() + kAccentWidth;

    // Indent, capped. qMin rather than a branch so depth 5 and depth 50 land
    // in exactly the same place.
    //
    // A MESSAGE row is nested at least one level whatever depth it reports.
    // notmuch numbers every message of a thread with no usable In-Reply-To as
    // depth 0, so a flat thread's replies arrived here claiming no nesting and
    // drew flush against their own thread with no spine, while a nested
    // thread's replies indented normally: two different shapes on screen for
    // the same relationship. Being a child row IS the nesting; the depth only
    // says how much further to go.
    const int effectiveDepth =
        input.isMessage ? qMax(1, input.depth) : input.depth;
    const int depth = qMin(effectiveDepth, kMaxDepth);
    const int indent = depth * kIndentStep;
    out.contentLeft = textLeft + kPaddingX + indent;

    // The avatar, square, in the gutter between the indent and the text.
    // Sized from the card's HEIGHT rather than from a pixel constant, so it
    // follows the desktop's font exactly as markSide() does.
    const int avatarSide = qMax(0, rect.height() - kPaddingY * 2);
    out.avatarRect = QRect(out.contentLeft, rect.top() + kPaddingY,
                           avatarSide, avatarSide);
    // Everything after it starts past the squircle. This is what the item's
    // cost is: a deep reply loses the gutter on top of its indent.
    out.contentLeft = out.avatarRect.right() + 1 + kAvatarGap;

    // One spine per level actually indented, each running the card's full
    // height so an expansion reads as one continuous block.
    for (int level = 0; level < depth; ++level) {
        const int x = textLeft + kPaddingX + level * kIndentStep
                      + kIndentStep / 2;
        out.spines.append(QRect(x, rect.top(), 2, rect.height()));
    }

    // The EXCLUSIVE right edge: one past the last pixel a card may draw on.
    // QRect::right() is inclusive (left + width - 1), so building widths from
    // it directly lands everything one pixel short of the intended padding.
    const int right = rect.right() + 1 - kPaddingX;
    const int lineOneTop = rect.top() + kPaddingY;
    const int lineTwoTop = lineOneTop + metrics.height();
    const int lineThreeTop = lineTwoTop + metrics.height();

    // The date is measured first and the sender gets what is left, so a long
    // sender is elided rather than painting over the date.
    //
    // Measured BOLD whatever font this is handed. An unread card draws bold and
    // the delegate computes its layout from the view's regular font, so a rect
    // sized regular clips a bold date: 154px reserved against 170px needed, one
    // digit of the year gone from every unread card. Reserving the wider of the
    // two costs a few pixels on a read card and cannot disagree with what is
    // painted.
    QFont dateFont = font;
    dateFont.setBold(true);
    const int dateWidth =
        QFontMetrics(dateFont).horizontalAdvance(
            widestDateSample(input.dateFormat));
    out.dateRect = QRect(right - dateWidth, lineOneTop, dateWidth,
                         metrics.height());
    out.senderRect = QRect(out.contentLeft, lineOneTop,
                           qMax(0, out.dateRect.left() - out.contentLeft
                                       - kPaddingX),
                           metrics.height());

    // The expander is the reply count as a PILL, on line two and on the right.
    //
    // Sized from the label actually drawn rather than from a fixed sample, so
    // the background and the text inside it cannot disagree. Both states of the
    // glyph are measured because the rect must not change width when the card
    // is expanded: a pill that resized on click would shift the subject's
    // elision under the pointer.
    if (input.replyCount > 0) {
        const int collapsed = smallMetrics.horizontalAdvance(
            expanderLabel(input.replyCount, false));
        const int expanded = smallMetrics.horizontalAdvance(
            expanderLabel(input.replyCount, true));
        // The triangle is a drawn mark since item 70, so the pill has to
        // reserve its width explicitly. It came free from the text metrics
        // while it was a glyph in the label, which is exactly the kind of
        // width that disappears silently when the glyph does.
        const int triangle = smallMetrics.ascent() + kMarkGap;
        const int countWidth =
            qMax(collapsed, expanded) + triangle + kPillPaddingX * 2;
        out.expanderRect = QRect(right - countWidth, lineTwoTop, countWidth,
                                 metrics.height());
    }

    // Item 70's marks. Until it they were glyphs inside the subject string, so
    // the text metrics reserved their width without anyone arranging it; as
    // icons they need rects, and the subject needs to end before them or it
    // runs underneath.
    //
    // Laid out from the RIGHT, inwards: the expander is already placed, and
    // each mark present takes the next slot to its left. The subject then gets
    // whatever is left, which is what keeps a card with four marks from eliding
    // its subject to nothing on a narrow window: the marks are small and fixed,
    // the subject is the elastic part.
    const int side = markSide(font);
    const int markTop = lineTwoTop + (metrics.height() - side) / 2;
    int markRight = out.expanderRect.isEmpty()
                        ? right
                        : out.expanderRect.left() - kPaddingX;

    // Order matters and is the drawing order reversed: placing right to left
    // here puts attachment nearest the subject and replied furthest right,
    // which is the order the delegate then paints them in.
    const auto placeMark = [&](bool present, QRect &target) {
        if (!present)
            return;
        target = QRect(markRight - side, markTop, side, side);
        markRight = target.left() - kMarkGap;
    };
    placeMark(input.receivedForward, out.receivedForwardRect);
    placeMark(input.replied, out.repliedRect);
    placeMark(input.passed, out.passedRect);
    placeMark(input.hasAttachment, out.attachmentRect);

    // The flag sits at the START of line two, where the glyph did, so a flagged
    // card still reads flagged from the left edge. It indents the subject
    // rather than overlapping it.
    int subjectLeft = out.contentLeft;
    if (input.flagged) {
        out.flagRect = QRect(out.contentLeft, markTop, side, side);
        subjectLeft = out.flagRect.right() + 1 + kMarkGap;
    }

    const int subjectRight = markRight;
    out.subjectRect = QRect(subjectLeft, lineTwoTop,
                            qMax(0, subjectRight - subjectLeft),
                            metrics.height());

    out.tagRect = QRect(out.contentLeft, lineThreeTop,
                        qMax(0, right - out.contentLeft),
                        smallMetrics.height());

    return out;
}
