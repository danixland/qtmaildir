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

QString CardLayout::formatDate(const QDateTime &date)
{
    // The system locale's own short format, not a hardcoded pattern: an
    // Italian desktop writes 10/08/2025, not 2025-08-10, and a mail client
    // that disagrees with every other application on screen is simply wrong.
    return QLocale::system().toString(date, QLocale::ShortFormat);
}

QString CardLayout::expanderLabel(int replyCount, bool expanded)
{
    // "3 replies", not a bare "3". The count alone reads as an unexplained
    // number beside the subject, and the word is what says the card opens.
    //
    // Not translated through tr() here because CardLayout is a plain struct
    // rather than a QObject; the delegate is where a translated build would
    // wrap this, and the string is deliberately kept in one place so there is
    // exactly one thing to change.
    const QString glyph = expanded ? QStringLiteral("\u25be")
                                   : QStringLiteral("\u25b8");
    const QString word = replyCount == 1 ? QStringLiteral("reply")
                                         : QStringLiteral("replies");
    return QStringLiteral("%1 %2 %3").arg(glyph).arg(replyCount).arg(word);
}

QString CardLayout::widestDateSample()
{
    // A real date run through the same formatter, with the wide digits and a
    // two-digit day and month, so the reserved width matches what is drawn
    // whatever the locale's pattern turns out to be. Guessing a pattern here
    // would reintroduce the clipping this exists to prevent.
    static const QString sample = [] {
        const QDateTime wide(QDate(2028, 12, 28), QTime(22, 58));
        return formatDate(wide);
    }();
    return sample;
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
    const int depth = qMin(input.depth, kMaxDepth);
    const int indent = depth * kIndentStep;
    out.contentLeft = textLeft + kPaddingX + indent;

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
        QFontMetrics(dateFont).horizontalAdvance(widestDateSample());
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
        const int countWidth =
            qMax(collapsed, expanded) + kPillPaddingX * 2;
        out.expanderRect = QRect(right - countWidth, lineTwoTop, countWidth,
                                 metrics.height());
    }

    const int subjectRight = out.expanderRect.isEmpty()
                                 ? right
                                 : out.expanderRect.left() - kPaddingX;
    out.subjectRect = QRect(out.contentLeft, lineTwoTop,
                            qMax(0, subjectRight - out.contentLeft),
                            metrics.height());

    out.tagRect = QRect(out.contentLeft, lineThreeTop,
                        qMax(0, right - out.contentLeft),
                        smallMetrics.height());

    return out;
}
