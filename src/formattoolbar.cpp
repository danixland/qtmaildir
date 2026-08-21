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

#include "formattoolbar.h"

#include <QStringList>


namespace {

/// Normalises the selection a widget reports into an ordered, in-range pair
/// that does not split a character.
///
/// Three hazards, handled once rather than per function. A backwards drag
/// reports the anchor AFTER the cursor; a selection can outlive the edit that
/// shortened the buffer under it; and a boundary can land in the middle of a
/// surrogate pair, where inserting a token splits one character into two
/// halves and the result is not valid UTF-16 at all.
///
/// The surrogate case is not reachable with an arrow key or the mouse, which
/// move in whole clusters, but QTextCursor::setPosition accepts such a
/// position, so any caller computing one arithmetically can produce it: a
/// draft restore, a find/replace, a template insertion. A boundary sitting on
/// a LOW surrogate is inside a pair, and moving it back by one puts it before
/// the whole character.
///
/// A COLLAPSED cursor moves back, not outward: nudging the two ends in
/// opposite directions would turn an empty selection into a two-unit one and
/// wrap a character the user never selected. A real selection widens, so that
/// touching any part of a character covers the whole of it.
void normalise(const QString &text, int &from, int &to)
{
    from = qBound(0, from, int(text.size()));
    to = qBound(0, to, int(text.size()));
    if (from > to)
        qSwap(from, to);

    const auto insidePair = [&text](int at) {
        return at < text.size() && text.at(at).isLowSurrogate();
    };

    if (from == to) {
        if (insidePair(from)) {
            --from;
            to = from;
        }
        return;
    }

    if (insidePair(from))
        --from;
    if (insidePair(to))
        ++to;
}

}  // namespace

MarkdownFormat::Edit MarkdownFormat::wrap(const QString &text, int start,
                                          int end, const QString &token)
{
    Edit edit;
    int from = start;
    int to = end;
    normalise(text, from, to);

    edit.text = text;
    // The closing token first: inserting at `from` would shift `to`.
    edit.text.insert(to, token);
    edit.text.insert(from, token);

    if (from == to) {
        // No selection: the cursor goes BETWEEN the two tokens so typing
        // continues inside them. Landing after the closing token instead is
        // the mistake a user notices on the first keystroke.
        edit.selectionStart = from + token.size();
        edit.selectionEnd = edit.selectionStart;
    } else {
        // The selection is preserved so a second press applies a second token
        // to the same words without reselecting: bold then italic.
        edit.selectionStart = from + token.size();
        edit.selectionEnd = to + token.size();
    }

    return edit;
}

MarkdownFormat::Edit MarkdownFormat::link(const QString &text, int start, int end)
{
    Edit edit;
    int from = start;
    int to = end;
    normalise(text, from, to);

    const QString label = text.mid(from, to - from);

    edit.text = text;
    edit.text.replace(from, to - from, QStringLiteral("[%1]()").arg(label));

    if (label.isEmpty()) {
        // Nothing selected: the label is what gets typed first, so the cursor
        // goes inside the brackets, one past the '['.
        edit.selectionStart = from + 1;
    } else {
        // The label is written; the URL is what remains, so the cursor goes
        // inside the parentheses: past '[', the label, ']' and '('.
        edit.selectionStart = from + label.size() + 3;
    }
    edit.selectionEnd = edit.selectionStart;

    return edit;
}

MarkdownFormat::Edit MarkdownFormat::quote(const QString &text, int start, int end)
{
    Edit edit;
    int from = start;
    int to = end;
    normalise(text, from, to);

    // Line-based, not a wrap. The selection is widened to whole lines first:
    // quoting half a line produces markdown that means something else.
    //
    // The backwards search starts at `from - 1`, not at `from`. QString's
    // lastIndexOf INCLUDES the position it is given, so a cursor sitting at
    // the end of a line, immediately before its newline, would find that
    // newline and quote the FOLLOWING line instead of the one the cursor is
    // on. The guard against a negative position matters too, since -1 means
    // "search from the end" and would find the last newline in the buffer.
    const int firstLineStart =
        from > 0 ? text.lastIndexOf(QLatin1Char('\n'), from - 1) + 1 : 0;

    // No newline after the last line, so the end of the text is the end of
    // the block. Without this the whole tail would be dropped.
    int lastLineEnd = text.indexOf(QLatin1Char('\n'), to);
    if (lastLineEnd < 0)
        lastLineEnd = text.size();

    const QString before = text.left(firstLineStart);
    const QString middle = text.mid(firstLineStart, lastLineEnd - firstLineStart);
    const QString after = text.mid(lastLineEnd);

    const QStringList lines = middle.split(QLatin1Char('\n'));

    // Nesting rather than toggling, per the spec: a second press deepens the
    // quote. There is deliberately no live toggle here, because tracking "my
    // text" and "the quote" as separate pieces to make one reversible is
    // machinery for a case the user answers by closing the composer.
    QStringList result;
    result.reserve(lines.size());
    for (const QString &line : lines) {
        // A blank line keeps the marker, since that is what continues a quote
        // block in markdown, but WITHOUT the trailing space: several editors
        // and mail clients strip trailing whitespace, and stripping it from
        // "> " leaves ">" anyway, so writing it bare is the same result
        // reached deliberately.
        result.append(line.isEmpty() ? QStringLiteral(">")
                                     : QStringLiteral("> ") + line);
    }

    const QString replacement = result.join(QLatin1Char('\n'));
    edit.text = before + replacement + after;
    // The quoted block stays selected, so a second press nests it.
    edit.selectionStart = firstLineStart;
    edit.selectionEnd = firstLineStart + replacement.size();

    return edit;
}
