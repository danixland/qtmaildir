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

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollBar>

void ThreadListView::mousePressEvent(QMouseEvent *event)
{
    const QModelIndex index = indexAt(event->pos());

    // Only a thread row, only the subject column, only the strip the delegate
    // reserved for the glyph. Anything wider would swallow clicks meant to
    // select the row, which is what the rest of the subject cell is for.
    if (event->button() == Qt::LeftButton && index.isValid()
        && !index.parent().isValid()
        && index.column() == ThreadListModel::SubjectColumn
        && index.data(ThreadListModel::HasRepliesRole).toBool()) {

        const QRect rect = visualRect(index);
        if (event->pos().x() >= rect.left()
            && event->pos().x() < rect.left() + SubjectDelegate::kExpanderWidth) {
            // Column 0, not the clicked index. Expansion state belongs to the
            // ROW, and QTreeView keys it on the first column: asking
            // isExpanded() about the subject-column index always answers false,
            // so every click expanded again instead of toggling.
            const QModelIndex row = index.siblingAtColumn(0);
            setExpanded(row, !isExpanded(row));

            // Swallowed, so the click that opened a thread does not also load
            // it into the message pane: expanding is a request to see the
            // thread's shape, not to read it.
            event->accept();
            return;
        }
    }

    QTreeView::mousePressEvent(event);
}

void ThreadListView::paintEvent(QPaintEvent *event)
{
    QTreeView::paintEvent(event);

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

    // Only the rows actually on screen, walked by INDEX rather than by row
    // number. A tree numbers rows per parent, so row 0 exists once per expanded
    // thread and the old flat 0..N walk would paint the first thread's strip
    // over every one of them.
    QModelIndex walk = indexAt(QPoint(0, 0));

    // Counts the rows actually painted, for the alternating colour. In a tree
    // that has to follow VISUAL position: row 0 under three different threads
    // is three different stripes, and using index.row() would give all three
    // the same one.
    int visualRow = 0;

    // The spine's extent, collected across the reply rows and drawn once after
    // the loop. Per-row segments leave a gap at every row boundary and read as
    // a column of dashes rather than as one line.
    int spineX = -1;
    int spineTop = std::numeric_limits<int>::max();
    int spineBottom = std::numeric_limits<int>::min();

    for (; walk.isValid(); walk = indexBelow(walk), ++visualRow) {
        const QRect rowRect = visualRect(walk);
        if (rowRect.top() > viewport()->height())
            break;

        // A message row: no tag strip, but it does get the band filled to its
        // own tint and a thread line down its left.
        //
        // The band has to be filled here for the same reason a thread row's is.
        // The cells paint the tint per cell, so nothing covers the width to the
        // right of the last column or the lower band the strip normally
        // occupies, and an untouched reply row comes out tinted across its text
        // and bare underneath it.
        if (walk.parent().isValid()) {
            // Only the band BELOW the text, never the whole row. paintEvent
            // runs after the cells, so filling the row's full height paints
            // over the sender and subject the delegate just drew: measured at
            // zero surviving text pixels, a block of blank tinted rows.
            const int bandTop = rowRect.top() + SubjectDelegate::kRowPadding
                              + rowMetrics.height();
            const QRect band(columnViewportPosition(ThreadListModel::DateColumn),
                             bandTop,
                             viewport()->width()
                                 - columnViewportPosition(
                                     ThreadListModel::DateColumn),
                             rowRect.bottom() - bandTop + 1);

            if (selectionModel()
                && selectionModel()->isSelected(
                    walk.siblingAtColumn(ThreadListModel::SubjectColumn))) {
                painter.fillRect(band, palette().brush(QPalette::Highlight));
            } else {
                painter.fillRect(band, ThreadListModel::replyBackground());
            }

            // The spine is NOT drawn here. Drawing it per row leaves a gap
            // wherever consecutive rows do not abut exactly, which is every row
            // boundary once the rows carry padding: the result reads as a
            // column of dashes rather than as one line. It is drawn as a single
            // continuous run after this loop, from the collected extents below.
            const int subjectLeft =
                columnViewportPosition(ThreadListModel::SubjectColumn);
            const int lineX = subjectLeft + SubjectDelegate::kExpanderWidth / 2;

            if (spineX < 0)
                spineX = lineX;
            spineTop = qMin(spineTop, rowRect.top());
            spineBottom = qMax(spineBottom, rowRect.bottom() + 1);

            // A stub out to the row, so each reply is visibly attached to the
            // spine rather than merely beside it. Drawn in the LOWER band, not
            // at the row's midpoint: the midpoint crosses the sender text, and
            // this paints after the cells.
            const int stubY = bandTop + (rowRect.bottom() - bandTop) / 2;
            painter.setPen(QPen(ThreadListModel::threadLineColour(), 2));
            painter.drawLine(lineX, stubY,
                             subjectLeft + SubjectDelegate::kReplyIndent
                                 - TagChip::kSpacing * 2,
                             stubY);
            continue;
        }

        const QModelIndex index = walk.siblingAtColumn(
            ThreadListModel::SubjectColumn);

        const int rowTop = rowRect.top();
        const int height = rowRect.height();
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
        // isSelected on the index, not isRowSelected(int): a QTreeView has no
        // such overload, and a row number alone cannot name a row in a tree
        // anyway since it is only unique under one parent.
        else if (selectionModel() && selectionModel()->isSelected(index))
            painter.fillRect(band, palette().brush(QPalette::Highlight));
        else if (alternatingRowColors() && (visualRow % 2))
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

    // One continuous spine over every visible reply row, drawn last so no
    // cell fill can break it. Segments drawn per row left a dash at every row
    // boundary, which read as a dotted decoration rather than as the structure
    // holding the block together.
    if (spineX >= 0 && spineBottom > spineTop) {
        painter.setPen(QPen(ThreadListModel::threadLineColour(), 2));
        painter.drawLine(spineX, spineTop, spineX, spineBottom);
    }
}
