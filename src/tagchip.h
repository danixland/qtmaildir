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
constexpr int kPaddingX = 6;
constexpr int kPaddingY = 1;
constexpr int kSpacing = 4;
constexpr int kRadius = 3;

QSize sizeFor(const QFontMetrics &metrics, const QString &text);

/// Paints the chip into `rect`, using `text` and `background`. The text colour
/// is derived from the fill so it stays legible.
void paint(QPainter *painter, const QRect &rect, const QString &text,
           const QColor &background);

}  // namespace TagChip

/// Item delegate for the subject column: draws the account chip in front of
/// the subject text, so which mailbox a thread came from reads at a glance
/// without a tags column spelling it out.
class SubjectDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

protected:
    /// Makes the selection highlight outrank a model-supplied foreground.
    ///
    /// Qt's own resolution does the opposite, which leaves a dimmed read
    /// thread painting grey over the selection colour.
    void initStyleOption(QStyleOptionViewItem *option,
                         const QModelIndex &index) const override;
};
