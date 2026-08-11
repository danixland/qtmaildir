/*
 * qtmaildir - a Qt6 GUI for a local notmuch-indexed Maildir
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef QTMAILDIR_MARKS_H
#define QTMAILDIR_MARKS_H

#include <QColor>
#include <QPixmap>
#include <QSize>

class QPainter;
class QRect;

/// The pane marks: the small state icons drawn on a card and in the message
/// pane's header.
///
/// These are qtmaildir's own, deliberately, and that is the point of item 70.
/// Toolbar and menu icons come from QIcon::fromTheme and follow the user's icon
/// theme; the panes must not, because a mark that changes shape under a theme
/// change is a mark whose meaning the application cannot state. They are also
/// no longer font glyphs: U+1F4CE and U+2605 render at the mercy of whatever
/// font the desktop supplies, and both fell back to a bare "*" on a font that
/// lacked them, which made a flagged thread and one with an attachment
/// indistinguishable.
///
/// The SVG payloads are compiled in as string literals rather than loaded from
/// a .qrc, and the reason is in src/CMakeLists.txt: a qrc compiled into the
/// static library registers itself from a global initialiser that the linker
/// drops, so resources belong to the executable. The tests link the library,
/// not the executable, so a resource-based mark would be absent exactly where
/// it needs asserting. The editable originals live in assets/icons/marks/ and
/// are the source these were taken from.
namespace Marks {

enum class Mark {
    Attachment,
    Flagged,
    Passed,
    Replied,
    ExpanderCollapsed,
    ExpanderExpanded,
};

/// The mark rendered at `size`, filled with `color`.
///
/// Recoloured rather than shipped in light and dark variants: every payload
/// paints with fill="currentColor", which QSvgRenderer does not resolve, so the
/// colour is composited in. One asset then serves both palettes and cannot fall
/// out of step with itself.
///
/// Cached by (mark, size, colour, devicePixelRatio). A delegate paints these on
/// every row of every repaint, and re-parsing six XML documents per frame is
/// the kind of cost that does not show up until a list is long.
QPixmap pixmap(Mark mark, const QSize &size, const QColor &color,
               qreal devicePixelRatio = 1.0);

/// Paints `mark` centred in `rect`, scaled to fit its shorter side.
void paint(QPainter *painter, const QRect &rect, Mark mark,
           const QColor &color);

/// The raw SVG payload, exposed for tests and for the message pane, which
/// embeds marks as data: URIs in the header label's rich text rather than
/// painting them.
QByteArray svg(Mark mark);

}   // namespace Marks

#endif   // QTMAILDIR_MARKS_H
