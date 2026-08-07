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

#include <QTableView>

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
class ThreadListView : public QTableView
{
    Q_OBJECT
public:
    using QTableView::QTableView;

protected:
    void paintEvent(QPaintEvent *event) override;
};
