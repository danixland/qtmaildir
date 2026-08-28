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

/// The thread list.
///
/// It exists for ONE reason now: the expander is drawn by CardDelegate, and a
/// delegate gets no click of its own without an editor, so the view has to own
/// the hit-test. Everything else it used to do is gone.
///
/// Until item 53 this class also painted a row-wide strip of tag chips after
/// the cells, because a delegate cannot paint outside its column and the strip
/// spanned all five. With one column and one delegate painting the whole card,
/// that reason is gone and so is the paintEvent, along with the two faults it
/// kept producing: a deleted row cut in half, and every other row showing a
/// bare stripe, both from the view having to re-honour alternating colours,
/// the selection and BackgroundRole across cells it did not own.
class ThreadListView : public QTreeView
{
    Q_OBJECT
public:
    using QTreeView::QTreeView;

protected:
    /// Toggles a thread when its message count is clicked.
    ///
    /// Being VISIBLE and being CLICKABLE are separate properties:
    /// setRootIsDecorated(false), needed to stop the style drawing its own
    /// indicator underneath, also removed the style's hit area, so an expander
    /// once painted correctly and did nothing at all.
    void mousePressEvent(QMouseEvent *event) override;
};
