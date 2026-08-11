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

#include <QDateTime>
#include <QFont>
#include <QRect>
#include <QVector>

/// Where everything on a card goes, with no painting and no widget.
///
/// Split out from CardDelegate on purpose. A delegate needs a live QPainter and
/// an exposed view before it draws anything, which is what makes delegate tests
/// fragile: CLAUDE.md records that viewport()->render() returns a blank image in
/// several ordinary situations, and that a probe reporting "no ink anywhere" is
/// far more likely broken than the code it is testing. Every geometric claim
/// about a card is therefore made here, where a test is a function call.
///
/// The card is three lines, always:
///
///     sender ................................ date      <- senderRect/dateRect
///     * subject                    @   v 3 replies      <- subjectRect/expanderRect
///     [tag] [tag]                                       <- tagRect
struct CardLayout
{
    /// What the model says about the row. Deliberately plain data: the layout
    /// must be computable in a test without a model or a view.
    struct Input
    {
        bool isMessage = false;
        int depth = 0;       ///< 0 for a thread root, 1 for a direct reply.
        int replyCount = 0;  ///< 0 means no expander.

        /// A QDateTime::toString() pattern from [general] date_format, or empty
        /// for the system's short format.
        ///
        /// It lives on the INPUT rather than being read where the date is
        /// drawn, because the width reserved for the date is computed from the
        /// same format inside compute(). A pattern reaching the painter but not
        /// the geometry is exactly how a longer date gets elided into a rect
        /// sized for a shorter one.
        QString dateFormat;
    };

    /// Width of the account accent bar down a thread card's left edge.
    ///
    /// A starting value, not a settled one. Five accounts is enough that two
    /// colours distinct as chips can read alike as thin stripes, and that can
    /// only be judged against real cards on the user's own screen and theme
    /// (Task 10). Widen it there if the accounts are not tellable apart.
    static constexpr int kAccentWidth = 3;

    /// Horizontal breathing room at the card's edges, measured from the accent
    /// bar rather than from the card, so text does not sit on the colour.
    static constexpr int kPaddingX = 8;

    /// Vertical breathing room above the first line and below the last.
    static constexpr int kPaddingY = 4;

    /// How far one level of reply nesting indents.
    static constexpr int kIndentStep = 18;

    /// The depth past which nothing indents further.
    ///
    /// A mailing-list chain can nest a dozen deep, and without a cap the
    /// sender is eventually pushed off the right edge. Item 20 accepted that
    /// deep chains must be capped in the VIEW rather than flattened in the
    /// model, and this is that cap. Rows past it draw at this depth's indent
    /// with no marker saying so.
    static constexpr int kMaxDepth = 4;

    QRect senderRect;
    QRect dateRect;
    QRect subjectRect;
    QRect tagRect;

    /// The reply count's rect, and the click target that toggles the thread.
    /// Empty when the row has no replies.
    QRect expanderRect;

    /// The account accent bar down the card's left edge.
    ///
    /// Thread cards only. A reply's account is its thread's, stated once at the
    /// head of the conversation, and a second vertical line in a reply's gutter
    /// would sit a few pixels from the spine and compete with it. The spine
    /// carries the accent instead, so an expansion is bounded by one colour
    /// without ever drawing two lines. Empty on a reply.
    QRect accentRect;

    /// One full-height vertical line per depth level, outermost first.
    QVector<QRect> spines;

    /// Where the card's text starts, after any indent.
    int contentLeft = 0;

    int totalHeight = 0;

    /// The height EVERY row gets, thread and reply alike.
    ///
    /// Uniform by design: it keeps setUniformRowHeights(true), which is the
    /// single cheapest property of this layout, since no scrolling or
    /// hit-testing arithmetic has to account for rows of differing size. The
    /// cost is a blank third line on a card with no tags, which was accepted
    /// explicitly.
    static int heightFor(const QFont &font);

    /// The font the tag chips and the reply count are drawn in: a size down
    /// from the card's own, so they read as annotation rather than as a third
    /// column of content.
    static QFont smallFont(const QFont &cardFont);

    static CardLayout compute(const Input &input, const QRect &rect,
                              const QFont &font);

    /// How a card writes a date, in the user's own locale.
    ///
    /// Never a hardcoded pattern. "yyyy-MM-dd hh:mm" is a US-looking format
    /// that an Italian desktop does not use, and the whole point of asking the
    /// system locale is that the user reads dates the way their desktop writes
    /// them everywhere else.
    ///
    /// Shared with the layout so the width reserved for the date and the text
    /// drawn into it come from one place: a locale whose short format is
    /// longer than the reserved rect would clip, which is exactly the fault
    /// bold text produced.
    /// `format` is a QDateTime::toString() pattern, or empty for the system's
    /// short format. Config validates it, so an unusable pattern never gets
    /// this far.
    static QString formatDate(const QDateTime &date,
                              const QString &format = QString());

    /// The widest string formatDate() can return, for reserving space.
    static QString widestDateSample(const QString &format = QString());

    /// The expander's label: the reply count with its glyph, as drawn.
    ///
    /// Shared with the layout for the same reason as formatDate: the rect
    /// reserved for the pill and the text put inside it must come from one
    /// place, or a count wider than the sample the layout guessed at spills
    /// out of its own background.
    ///
    /// `expanded` chooses which way the triangle points.
    static QString expanderLabel(int replyCount, bool expanded);

    /// Padding inside the expander pill, matching a tag chip's, so the two read
    /// as the same kind of object on the card.
    static constexpr int kPillPaddingX = 8;
};
