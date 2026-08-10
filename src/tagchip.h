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

#pragma once

#include <QColor>
#include <QFont>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStyledItemDelegate>

class QPainter;
class QFontMetrics;

/// Draws one rounded, filled tag chip. Shared so the account chip in the
/// thread list and the strip under the message pane cannot drift apart.
namespace TagChip {

/// Padding inside a chip and the gap between two of them.
///
/// kPaddingX allows for the rounded ends: the corner radius is half the chip's
/// height, so the leftmost and rightmost few pixels of the fill are curve
/// rather than usable width, and text set closer would touch it.
constexpr int kPaddingX = 9;
constexpr int kPaddingY = 1;
constexpr int kSpacing = 4;

QSize sizeFor(const QFontMetrics &metrics, const QString &text);

/// Paints the chip into `rect`, using `text` and `background`. The text colour
/// is derived from the fill so it stays legible.
void paint(QPainter *painter, const QRect &rect, const QString &text,
           const QColor &background);

}  // namespace TagChip

/// Makes the selection highlight outrank a model-supplied foreground colour.
///
/// Qt resolves Qt::ForegroundRole into the palette's Text roles and its
/// painting then prefers those over HighlightedText, so a model that supplies
/// a foreground wins even on a selected row. That is wrong for the read/unread
/// dimming, whose colour is blended against the UNSELECTED background: over
/// the selection highlight it lands as grey on the highlight colour, close to
/// unreadable.
///
/// Inherited by CardDelegate, which is the only delegate the thread list
/// installs.
class RowStyleDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option,
                         const QModelIndex &index) const override;
};

