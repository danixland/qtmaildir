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

#include <QString>

/// The markdown transformations behind the composer's formatting toolbar.
///
/// Free functions over text and a selection, with no widget anywhere, so the
/// grammar is tested without a painter. Each one is a transformation over the
/// SOURCE: nothing about the buffer changes, it stays markdown the user can
/// also type by hand.
///
/// Every function takes the selection as the widget reports it, which means
/// the anchor may sit AFTER the cursor. Each one normalises with qMin/qMax
/// rather than requiring the caller to, since a backwards drag is an ordinary
/// gesture and a caller that forgets would corrupt the buffer silently.
/// Out-of-range positions are clamped to the text, so a stale selection
/// cannot index past the end, and a boundary landing INSIDE a surrogate pair
/// is nudged off it, so a position computed arithmetically cannot split a
/// character in half.
///
/// Neither wrap() nor quote() TOGGLES. A second press stacks another level:
/// `**this**` becomes `***this***` and `> one` becomes `> > one`. That is the
/// design, not an omission. The selection is preserved precisely so a second
/// press can apply a SECOND token to the same words, bold then italic without
/// reselecting, and a toggle would make that gesture unreachable. A toggle is
/// wanted eventually and is a spec change rather than a fix; see item 135 in
/// the backlog for the states it has to distinguish.
namespace MarkdownFormat {

/// The result of a transformation: the new text and where the selection
/// should end up.
struct Edit
{
    QString text;
    int selectionStart = 0;
    int selectionEnd = 0;
};

/// Wraps the selection in \p token, or inserts an empty pair with the cursor
/// BETWEEN the tokens when there is no selection.
///
/// The cursor landing between the tokens is the property a user notices
/// immediately when it is wrong, and it is invisible to a test that only
/// compares the resulting text.
Edit wrap(const QString &text, int start, int end, const QString &token);

/// `[text](url)`. With a selection the selected text becomes the label and
/// the cursor lands inside the empty parentheses, which is where the user has
/// to type next. With none the cursor lands inside the brackets, since the
/// label is then what gets typed first.
Edit link(const QString &text, int start, int end);

/// `> ` on every line the selection touches, including a line the selection
/// only starts or ends on. Line-based rather than a wrap, so it cannot be
/// expressed with wrap().
Edit quote(const QString &text, int start, int end);

}  // namespace MarkdownFormat
