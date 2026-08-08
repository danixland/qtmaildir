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

#include <QTreeView>

/// The thread list, with a row-wide strip of tag chips under each row's cells.
///
/// The strip is painted by the VIEW rather than by a delegate, and that is the
/// whole reason this class exists. A delegate is handed one cell's rectangle
/// and cannot paint outside its column, so pills drawn from the subject
/// column's delegate stop at that column's edge, losing the last tags of a
/// well-tagged thread, and start at that column's left edge, which puts them
/// under the subject instead of under the row. Painting after the cells lets
/// the strip run the full width, which is what the layout asks for:
///
///     [ date ][ from ][ subject ...................... ]
///       [ pill ][ pill ][ pill ]
///
/// The cells confine themselves to the upper band so the lower one is free;
/// SubjectDelegate::kRowPadding and rowHeightFor() are the shared measurements
/// that keep the two halves agreeing.
///
/// A QTreeView rather than a QTableView since item 20: a thread's replies are
/// child rows, and a table can neither indent nor expand. The strip survived
/// the port because every geometry call it needs (visualRect,
/// columnViewportPosition, indexAt, indexBelow) exists on both. What did NOT
/// survive is anything keyed on a row NUMBER: a tree numbers rows per parent,
/// so row 0 exists once per expanded thread and a flat 0..N walk paints the
/// first thread's strip over every one of them. The walk below goes by index.
///
/// The strip is painted for THREAD rows only. It carries the thread's tags, so
/// one under each reply would stripe the list and repeat identical tags down
/// the whole expansion.
class ThreadListView : public QTreeView
{
    Q_OBJECT
public:
    using QTreeView::QTreeView;

protected:
    void paintEvent(QPaintEvent *event) override;

    /// Toggles a thread when its expander glyph is clicked.
    ///
    /// The view owns this because the glyph is drawn by SubjectDelegate and a
    /// delegate gets no click of its own without an editor. Being VISIBLE and
    /// being CLICKABLE are separate properties: setRootIsDecorated(false),
    /// needed to stop the style drawing its own indicator underneath ours, also
    /// removed the style's hit area, so the expander painted correctly and did
    /// nothing at all.
    void mousePressEvent(QMouseEvent *event) override;

};
