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

bool isNegated(RuleTerm::Op op)
{
    return op == RuleTerm::ContainsNot || op == RuleTerm::IsNot
           || op == RuleTerm::HasNot;
}

/// Quoted when the operator asks for an exact phrase, and ALWAYS when the
/// value holds a space: unquoted, the space ends the term and the remainder
/// becomes a bare word, which widens the rule instead of breaking it.
bool needsQuotes(const RuleTerm &term)
{
    if (term.field == RuleTerm::Folder)
        return true;
    if (term.value.contains(QLatin1Char(' ')))
        return true;
    // Is/IsNot asks for an exact phrase, which only free-text fields need
    // quoting for: tag: and attachment: values are already bare words, and
    // quoting them changes nothing notmuch cares about but breaks the test's
    // documented expectation of an unquoted tag.
    if (term.op == RuleTerm::Is || term.op == RuleTerm::IsNot) {
        return term.field == RuleTerm::From || term.field == RuleTerm::To
               || term.field == RuleTerm::Cc || term.field == RuleTerm::Subject;
    }
    return false;
}

QString compileTerm(const RuleTerm &term)
{
    QString value = term.value;
    if (term.field == RuleTerm::Folder)
        value += QStringLiteral("/**");

    QString body;
    if (term.field == RuleTerm::Date) {
        body = prefixFor(term.field) + QLatin1Char(':')
               + (term.op == RuleTerm::Before
                      ? QStringLiteral("..") + value
                      : value + QStringLiteral(".."));
    } else if (needsQuotes(term)) {
        body = prefixFor(term.field) + QStringLiteral(":\"") + value
               + QLatin1Char('"');
    } else {
        body = prefixFor(term.field) + QLatin1Char(':') + value;
    }

    return isNegated(term.op) ? QStringLiteral("not ") + body : body;
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

    return compileTerm(terms.first());
}

RuleQuery RuleQuery::parse(const QString &query)
{
    Q_UNUSED(query);
    return RuleQuery();
}
