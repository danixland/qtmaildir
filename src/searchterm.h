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

#include <QDate>
#include <QList>
#include <QString>

/// Builds the notmuch query strings behind the right-click search actions.
///
/// Free functions and no widget, so the whole query grammar is testable
/// without a painter, a web engine or a window. Every surface that offers a
/// search goes through here, which is what stops five surfaces growing five
/// slightly different quoting rules.
///
/// **notmuch rejects almost nothing.** `from:((((` parses cleanly and matches
/// zero, so a malformed query produces an empty result rather than an error
/// the user could act on. Correctness here cannot be checked by asking notmuch;
/// it is checked against the constructed string.
namespace SearchTerm {

/// Longest quoted value. A selection longer than this is a mis-drag rather
/// than a search, and the query bar is an editable line the user has to be
/// able to read.
inline constexpr int kMaxValueLength = 200;

/// Quotes an arbitrary value for use as a notmuch term.
///
/// Whitespace and newlines collapse to single spaces, embedded quotes are
/// escaped, the value is capped at kMaxValueLength, and an empty or
/// whitespace-only value yields an EMPTY STRING rather than `""`. Callers
/// test for empty to decide whether to offer a menu entry at all.
QString quote(const QString &value);

/// `field:"value"`, or empty when the value is empty.
///
/// The field name is a notmuch keyword and is never translated: it is wire
/// format, not user-facing text.
QString field(const QString &name, const QString &value);

/// `date:YYYY-MM-DD..YYYY-MM-DD` for a single day, empty for an invalid date.
///
/// The day twice rather than the day and its successor: notmuch's range is
/// inclusive at both ends, so the naive `..next-day` form silently includes a
/// second day of mail.
QString onDate(const QDate &day);

/// `tag:name`, quoted only when the name needs it.
QString tag(const QString &name);

/// Narrows `existing` by `addition`, as `(existing) AND (addition)`.
///
/// **Both sides are parenthesised and that is load-bearing.** The query bar
/// may hold a hand-written disjunction, and `a or b AND c` binds as
/// `a or (b AND c)`: the result WIDENS a search the user asked to narrow, and
/// nothing reports an error. The same trap is why the post-new hook
/// parenthesises a rule's query before scoping it with `tag:new`.
///
/// An empty `existing` yields `addition` alone rather than `() AND (x)`, which
/// matches nothing; an empty `addition` leaves `existing` untouched.
QString extend(const QString &existing, const QString &addition);

}  // namespace SearchTerm

/// One entry a context menu can offer: a finished query and the text naming it.
///
/// Carried rather than rebuilt at menu-construction time, so the value a menu
/// entry searches for is the value the pane extracted, with no second parse of
/// anything already rendered.
struct SearchOffer
{
    /// Shown in the menu. Already translated, and elides a long value: the
    /// query keeps the full one.
    QString label;

    /// The finished notmuch query. Never empty in a constructed offer.
    QString query;
};
