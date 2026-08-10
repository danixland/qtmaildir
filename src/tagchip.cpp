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

#include "tagchip.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>

#include "tagcolors.h"
#include "threadlistmodel.h"

namespace TagChip {

QSize sizeFor(const QFontMetrics &metrics, const QString &text)
{
    return QSize(metrics.horizontalAdvance(text) + kPaddingX * 2,
                 metrics.height() + kPaddingY * 2);
}

void paint(QPainter *painter, const QRect &rect, const QString &text,
           const QColor &background)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    // Radius from the chip's own height rather than the fixed kRadius: a 3px
    // corner on a 17px chip reads as a slightly-softened rectangle, which is
    // hard to tell from the square cells of the columns behind it. Half the
    // height gives fully rounded ends, so a chip reads as an object sitting on
    // the row instead of as another compartment of it.
    const qreal radius = rect.height() / 2.0;
    painter->drawRoundedRect(rect, radius, radius);

    painter->setPen(TagColors::textColourOn(background));
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
}

}  // namespace TagChip

void RowStyleDelegate::initStyleOption(QStyleOptionViewItem *option,
                                       const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);

    // Qt resolves Qt::ForegroundRole into the palette's Text roles, and its
    // own painting then prefers those over HighlightedText: a model that
    // supplies a foreground wins even on a selected row.
    //
    // That is wrong for the read/unread dimming. A read thread's colour is
    // blended against the UNSELECTED background, so over the selection
    // highlight it lands as grey on purple, near unreadable. The highlight
    // already says "this row", so the dimming can yield to it while selected.
    //
    // Doomed threads are unaffected in practice: their fill is drawn beneath
    // the selection and their white is a contrast requirement, which
    // HighlightedText also satisfies.
    if (option->state & QStyle::State_Selected) {
        const QColor highlighted =
            option->palette.color(QPalette::HighlightedText);
        option->palette.setColor(QPalette::Text, highlighted);
        option->palette.setColor(QPalette::WindowText, highlighted);
    }

    // One line, elided. A card draws its own text through CardDelegate, but
    // this still governs whatever Qt draws for the item itself, and a wrapped
    // string would run past the card's own three lines.
    option->features &= ~QStyleOptionViewItem::WrapText;
    option->textElideMode = Qt::ElideRight;
}
