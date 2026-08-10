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

#include "tagchip.h"

/// Paints a whole card: three lines, all of it, including the tag chips.
///
/// It replaces both SubjectDelegate and ThreadListView::paintEvent. The view
/// used to paint the tag strip because a delegate cannot paint outside its
/// column and the strip spanned all five; with one column there is nothing to
/// span, so the strip comes home to the delegate and the view stops painting
/// entirely. That removes the two failure modes CLAUDE.md records for the
/// strip, a deleted row cut in half and every other row showing a bare stripe,
/// both of which existed because the view had to re-honour alternating
/// colours, the selection and BackgroundRole across cells it did not own.
///
/// Inherits RowStyleDelegate for its one job, which still matters: Qt resolves
/// Qt::ForegroundRole into the palette's Text roles and then prefers those over
/// HighlightedText, so the read/unread dimming would otherwise win on a
/// selected row and land as grey on the highlight colour.
class CardDelegate : public RowStyleDelegate
{
    Q_OBJECT
public:
    using RowStyleDelegate::RowStyleDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    /// The expander's rect for a row, so the VIEW can hit-test a click without
    /// duplicating the layout. The delegate draws it and the view owns the
    /// click, because a delegate gets no click of its own without an editor.
    static QRect expanderRectFor(const QStyleOptionViewItem &option,
                                 const QModelIndex &index);

    /// An account's colour as a thin LINE rather than as a chip's fill.
    ///
    /// Never use the raw account colour for the accent bar or the spine. That
    /// colour is chosen to be a background with legible text drawn on top
    /// (TagColors::textColourOn picks black or white against it). The same
    /// colour as a few pixels of line on the pane's own background is a
    /// different problem: it has to be followable down a long expansion
    /// WITHOUT competing with the senders beside it, which is the constraint
    /// threadLineColour() states and meets by blending 0.35 toward the
    /// palette's text. This blends the account colour toward the palette's
    /// Base by the same weight, keeping the hue that identifies the account
    /// and dropping the saturation that would shout.
    static QColor accentLineColour(const QColor &accountColour);
};
