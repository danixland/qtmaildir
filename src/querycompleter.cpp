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

#include "querycompleter.h"

namespace {

/// Whether the cursor sits inside a double-quoted literal. Counts quotes from
/// the start: an odd count before the cursor means the quote is still open.
bool insideQuotes(const QString &text, int cursor)
{
    int quotes = 0;
    for (int i = 0; i < cursor; ++i) {
        if (text.at(i) == QLatin1Char('"'))
            ++quotes;
    }
    return (quotes % 2) != 0;
}

/// Start of the token the cursor sits in. The boundary is whitespace or '(',
/// so "tag:inbox and su" has its last token starting at 14, not at 0.
int tokenStart(const QString &text, int cursor)
{
    int start = cursor;
    while (start > 0) {
        const QChar c = text.at(start - 1);
        if (c.isSpace() || c == QLatin1Char('('))
            break;
        --start;
    }
    return start;
}

} // namespace

CompletionContext completionContext(const QString &text, int cursor)
{
    CompletionContext ctx;

    if (cursor < 0 || cursor > text.size())
        return ctx;

    if (insideQuotes(text, cursor))
        return ctx;   // kind stays None

    const int start = tokenStart(text, cursor);
    const QString token = text.mid(start, cursor - start);

    const int colon = token.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        ctx.kind = CompletionContext::Prefix;
        ctx.stem = token;
        ctx.replaceFrom = start;
        ctx.replaceLength = token.size();
        return ctx;
    }

    ctx.kind = CompletionContext::Value;
    ctx.prefix = token.left(colon).toLower();
    ctx.stem = token.mid(colon + 1);
    ctx.replaceFrom = start + colon + 1;
    ctx.replaceLength = ctx.stem.size();
    return ctx;
}
