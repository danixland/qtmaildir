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

    /// The colour the accent bar is painted in: the account's hue, lifted to a
    /// floor of saturation and lightness.
    ///
    /// Two earlier versions were wrong in opposite directions. Blending toward
    /// the palette's Base at threadLineColour()'s 0.35 weight produced an
    /// INVISIBLE bar on a dark theme, landing at (0.18, 0.22, 0.26) against a
    /// Base of (0.169, 0.169, 0.169): the weight is a fraction OF THE ACCOUNT
    /// COLOUR, so a low one keeps the background rather than the hue. Passing
    /// the raw colour through fixed that and was still too quiet, because an
    /// account colour is chosen as a CHIP's fill with legible text on top and
    /// is therefore mid-tone by construction.
    ///
    /// A floor, not a repaint: a colour already past it is returned untouched,
    /// so a deliberately vivid choice is preserved. Hue is never altered, since
    /// hue is the whole information the bar carries and a shifted one would
    /// stop matching the account's chip and its swatch in the dropdown.
    ///
    /// The SPINE takes its colour from this and mutes it again in paint(): it
    /// runs the full height of every reply in an expansion and has to be
    /// followable without competing with the senders, which is a different
    /// problem from a 3px edge marker.
    ///
    /// Falls back to threadLineColour() for a thread with no account tag.
    static QColor accentLineColour(const QColor &accountColour);

    /// A tag chip's colour, drained for the SIBLING tier (item 111).
    ///
    /// Saturation only: hue stays so the tag is still recognisable, and
    /// lightness stays so `TagColors::textColourOn()` keeps choosing the same
    /// text colour and the chip cannot become unreadable. Exposed for a test,
    /// since "muted" has to be asserted on rather than eyeballed.
    static QColor mutedChipColour(const QColor &chipColour);

    /// The size of one tag chip on a card, for either tier.
    ///
    /// The delegate's own arithmetic rather than a duplicate of it: a test
    /// calling `TagChip::sizeFor` directly proves what that function does and
    /// nothing about what the delegate ASKS for, which is where the padding
    /// scale is chosen. A mutation dropping the scale at the call site
    /// survived exactly that kind of test.
    static QSize chipSize(const QFontMetrics &metrics, const QString &text,
                          bool own);
};
