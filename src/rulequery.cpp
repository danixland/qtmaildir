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

#include "rulequery.h"

namespace {

/// The notmuch prefix each field compiles to. Wire format, never translated.
QString prefixFor(RuleTerm::Field field)
{
    switch (field) {
    case RuleTerm::From:       return QStringLiteral("from");
    case RuleTerm::To:         return QStringLiteral("to");
    case RuleTerm::Cc:         return QStringLiteral("cc");
    case RuleTerm::Subject:    return QStringLiteral("subject");
    case RuleTerm::Tag:        return QStringLiteral("tag");
    case RuleTerm::Folder:     return QStringLiteral("path");
    case RuleTerm::Attachment: return QStringLiteral("attachment");
    case RuleTerm::Date:       return QStringLiteral("date");
    }
    return QString();
}

} // namespace

bool operator==(const RuleTerm &a, const RuleTerm &b)
{
    return a.field == b.field && a.op == b.op && a.value == b.value;
}

bool operator==(const RuleQuery &a, const RuleQuery &b)
{
    return a.parsed == b.parsed && a.join == b.join
           && a.terms == b.terms && a.exclusions == b.exclusions;
}

QString RuleQuery::compile() const
{
    if (terms.isEmpty())
        return QString();

    return prefixFor(terms.first().field) + QLatin1Char(':')
           + terms.first().value;
}

RuleQuery RuleQuery::parse(const QString &query)
{
    Q_UNUSED(query);
    return RuleQuery();
}
