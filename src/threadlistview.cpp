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

#include "threadlistview.h"

#include "tagchip.h"
#include "threadlistmodel.h"

#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>

void ThreadListView::paintEvent(QPaintEvent *event)
{
    QTableView::paintEvent(event);

    if (!model())
        return;

    QPainter painter(viewport());

    // Two fonts, deliberately. The row's own font fixes where the text band
    // ends, and the pills are drawn a size smaller: at the same size they read
    // as a second row of content competing with the subject, rather than as
    // annotation beneath it.
    const QFontMetrics rowMetrics(font());
    const QFont pillFont = SubjectDelegate::pillFont(font());
    const QFontMetrics metrics(pillFont);
    painter.setFont(pillFont);

    // Only the rows actually on screen. Walking the whole model would paint
    // thousands of strips outside the viewport on a large query.
    const int first = rowAt(0);
    const int last = rowAt(viewport()->height() - 1);
    const int lastRow = last >= 0 ? last : model()->rowCount() - 1;

    for (int row = qMax(0, first); row <= lastRow; ++row) {
        const QModelIndex index =
            model()->index(row, ThreadListModel::SubjectColumn);

        const int rowTop = rowViewportPosition(row);
        const int height = rowHeight(row);
        if (height <= 0)
            continue;

        // The strip's band, filled to match the row before anything is drawn
        // on it.
        //
        // A QTableView paints alternating colours and the selection PER CELL,
        // so nothing paints the width to the right of the last column, and
        // nothing paints the band at all where a column does not reach. Left
        // unfilled, an alternate-coloured or selected row shows the viewport
        // background in a strip across its lower half. Filled for every
        // visible row, not only tagged ones, since an untagged row has the
        // same band to account for.
        // Starting at the date column, NOT at the viewport edge. The two
        // leading columns hold the attachment and flag glyphs, centred in the
        // full row height, so a band drawn over them cuts those glyphs in half.
        const int bandLeft =
            columnViewportPosition(ThreadListModel::DateColumn);
        const QRect band(bandLeft, rowTop + SubjectDelegate::kRowPadding
                                       + rowMetrics.height(),
                         viewport()->width() - bandLeft,
                         height - SubjectDelegate::kRowPadding
                                - rowMetrics.height());

        // The model's own row colour wins where it has one: a deleted or spam
        // thread fills its cells with crimson or orange, and painting the base
        // colour across the band beneath them would cut the row in half.
        const QVariant background = index.data(Qt::BackgroundRole);

        if (background.isValid())
            painter.fillRect(band, background.value<QBrush>());
        else if (selectionModel() && selectionModel()->isRowSelected(row))
            painter.fillRect(band, palette().brush(QPalette::Highlight));
        else if (alternatingRowColors() && (row % 2))
            painter.fillRect(band, palette().brush(QPalette::AlternateBase));
        else
            painter.fillRect(band, palette().brush(QPalette::Base));

        const QStringList tags =
            index.data(ThreadListModel::PillTagsRole).toStringList();
        if (tags.isEmpty())
            continue;

        const QVariantList colours =
            index.data(ThreadListModel::PillColoursRole).toList();

        // The band the cells leave free, below the text they draw in the
        // upper one. Measured from SubjectDelegate by both sides, so neither
        // can drift into the other's half. The row's own font metrics set the
        // text band; the strip's smaller font must not be used for it, or the
        // pills ride up over the date and sender.
        const int top = rowTop + SubjectDelegate::kRowPadding
                      + rowMetrics.height() + TagChip::kSpacing;

        // Aligned with the first text column rather than the viewport edge:
        // the two leading columns are narrow markers for the attachment and
        // flag glyphs, and a strip starting at x=0 paints straight over them.
        // Indented past the date column's own left edge rather than flush with
        // it: a chip starting exactly where the column does reads as part of
        // the column rather than as a strip laid under the row.
        int x = columnViewportPosition(ThreadListModel::DateColumn)
              + TagChip::kSpacing * 2;
        const int available = viewport()->width() - TagChip::kSpacing;

        for (int i = 0; i < tags.size(); ++i) {
            const QSize size = TagChip::sizeFor(metrics, tags.at(i));

            // Stop rather than wrap or elide. A row that grew to fit its tags
            // would break the uniform height the list depends on, and half a
            // chip reads as a rendering fault.
            if (x + size.width() > available)
                break;

            const QColor colour = i < colours.size()
                ? colours.at(i).value<QColor>()
                : QColor(0x55, 0x55, 0x5f);

            TagChip::paint(&painter, QRect(x, top, size.width(), size.height()),
                           tags.at(i), colour);
            x += size.width() + TagChip::kSpacing;
        }
    }
}
