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
/// Applied to the columns that have no delegate of their own; SubjectDelegate
/// inherits it for the subject column.
class RowStyleDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option,
                         const QModelIndex &index) const override;
};

/// Item delegate for the subject column: draws the account chip in front of
/// the subject text, so which mailbox a thread came from reads at a glance
/// without a tags column spelling it out.
/// **Install on the subject column only.** It reads AccountLabelRole, which is
/// a property of the row rather than of a cell, so as a view-wide delegate it
/// draws the account chip into every column.
class SubjectDelegate : public RowStyleDelegate
{
    Q_OBJECT
public:
    using RowStyleDelegate::RowStyleDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    /// Vertical breathing room above the subject and below the pill row.
    static constexpr int kRowPadding = 4;

    /// The font the pill strip is drawn in: a size down from the row's own.
    ///
    /// At the same size the pills read as a second row of content competing
    /// with the subject, rather than as annotation beneath it. Derived from
    /// the row font rather than fixed, so it follows the desktop's font size.
    static QFont pillFont(const QFont &rowFont);

    /// The height every row gets, tall enough for the subject and a pill strip
    /// beneath it. The view applies this itself: a QTableView takes one height
    /// for the whole row, so leaving it to a single column's sizeHint would
    /// let whichever column the view happens to ask decide.
    static int rowHeightFor(const QFont &rowFont);

protected:
    /// The height of the band the subject text occupies. Everything below it
    /// belongs to ThreadListView's row-wide pill strip.
    static int subjectBandHeight(const QStyleOptionViewItem &option);
};
