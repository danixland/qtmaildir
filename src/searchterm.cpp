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

#include "searchterm.h"

#include <algorithm>

namespace SearchTerm {

QString quote(const QString &value)
{
    // simplified() collapses every run of whitespace, newlines and tabs
    // included, and trims the ends. A selection spanning paragraphs arrives
    // full of newlines, which would otherwise reach the query bar verbatim.
    QString cleaned = value.simplified();
    if (cleaned.isEmpty())
        return {};

    if (cleaned.size() > kMaxValueLength)
        cleaned.truncate(kMaxValueLength);

    // Backslashes first: escaping the quotes first would then escape the
    // backslashes this step adds, doubling them.
    cleaned.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    cleaned.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    return QLatin1Char('"') + cleaned + QLatin1Char('"');
}

QString field(const QString &name, const QString &value)
{
    const QString quoted = quote(value);
    if (quoted.isEmpty())
        return {};
    return name + QLatin1Char(':') + quoted;
}

QString onDate(const QDate &day)
{
    if (!day.isValid())
        return {};
    const QString text = day.toString(QStringLiteral("yyyy-MM-dd"));
    return QStringLiteral("date:%1..%1").arg(text);
}

QString tag(const QString &name)
{
    const QString trimmed = name.simplified();
    if (trimmed.isEmpty())
        return {};

    // A tag is a token from a vocabulary the user chose, so it reads better
    // unquoted in the bar they are about to edit. Quoted only when it holds
    // something that would not survive.
    const bool needsQuoting =
        std::any_of(trimmed.cbegin(), trimmed.cend(), [](QChar ch) {
            return !(ch.isLetterOrNumber() || ch == QLatin1Char('-')
                     || ch == QLatin1Char('_') || ch == QLatin1Char('.')
                     || ch == QLatin1Char('/'));
        });

    return QStringLiteral("tag:") + (needsQuoting ? quote(trimmed) : trimmed);
}

QString extend(const QString &existing, const QString &addition)
{
    const QString left = existing.trimmed();
    const QString right = addition.trimmed();

    if (right.isEmpty())
        return left;
    if (left.isEmpty())
        return right;

    return QStringLiteral("(%1) AND (%2)").arg(left, right);
}

}  // namespace SearchTerm
