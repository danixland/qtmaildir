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

#include <QPair>
#include <QStringList>
#include <QVector>

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
    // Is/IsNot means an exact phrase, and only the free-text fields need
    // quotes to express one. A tag or an attachment name is a single bare
    // token to notmuch, which reads `tag:inbox` and `tag:"inbox"` identically
    // (both count 5322 against the live index). Quoting them would therefore
    // change the stored string without changing what it matches, and this
    // type's whole contract is that an unedited rule compiles back byte for
    // byte.
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

/// Splits on whitespace, keeping a double-quoted run as one token. Returns
/// false when a quote is left open, which is a query this builder will not
/// represent.
bool tokenise(const QString &query, QStringList *out)
{
    QString current;
    bool inQuotes = false;
    bool has = false;

    for (int i = 0; i < query.size(); ++i) {
        const QChar c = query.at(i);
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            current += c;
            has = true;
        } else if (!inQuotes && c.isSpace()) {
            if (has) {
                out->append(current);
                current.clear();
                has = false;
            }
        } else {
            current += c;
            has = true;
        }
    }

    if (inQuotes)
        return false;
    if (has)
        out->append(current);
    return true;
}

bool fieldForPrefix(const QString &prefix, RuleTerm::Field *out)
{
    static const QVector<QPair<QString, RuleTerm::Field>> table = {
        {QStringLiteral("from"),       RuleTerm::From},
        {QStringLiteral("to"),         RuleTerm::To},
        {QStringLiteral("cc"),         RuleTerm::Cc},
        {QStringLiteral("subject"),    RuleTerm::Subject},
        {QStringLiteral("tag"),        RuleTerm::Tag},
        {QStringLiteral("path"),       RuleTerm::Folder},
        {QStringLiteral("attachment"), RuleTerm::Attachment},
        {QStringLiteral("date"),       RuleTerm::Date},
    };

    for (const auto &entry : table) {
        if (entry.first == prefix) {
            *out = entry.second;
            return true;
        }
    }
    return false;
}

/// Parses ONE token into a term. Returns false for anything this builder does
/// not represent, which is not the same as invalid: notmuch accepts far more
/// than this.
bool parseTerm(const QString &token, RuleTerm *out)
{
    const int colon = token.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;

    RuleTerm::Field field;
    if (!fieldForPrefix(token.left(colon), &field))
        return false;

    QString value = token.mid(colon + 1);
    if (value.isEmpty())
        return false;

    bool quoted = false;
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
        && value.endsWith(QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2);
        quoted = true;
    }
    // A quote anywhere else means a shape this builder does not emit.
    if (value.contains(QLatin1Char('"')))
        return false;

    out->field = field;

    if (field == RuleTerm::Date) {
        if (value.startsWith(QStringLiteral(".."))) {
            out->op = RuleTerm::Before;
            out->value = value.mid(2);
        } else if (value.endsWith(QStringLiteral(".."))) {
            out->op = RuleTerm::After;
            out->value = value.chopped(2);
        } else {
            return false;   // A two-sided range is not a row.
        }
        return !out->value.isEmpty();
    }

    if (field == RuleTerm::Folder) {
        // Only the recursive form is representable; a bare path means
        // something different to notmuch and must not be silently rewritten.
        if (!value.endsWith(QStringLiteral("/**")))
            return false;
        value = value.chopped(3);
        out->op = RuleTerm::Is;
        out->value = value;
        return !value.isEmpty();
    }

    // Tag and Attachment compile unquoted (see needsQuotes), so their
    // operator must not be inferred from the quoting: reading a quoted tag
    // back as a quoting operator would compile it unquoted and change the
    // stored string.
    if (field == RuleTerm::Attachment)
        out->op = RuleTerm::Has;
    else if (field == RuleTerm::Tag)
        out->op = RuleTerm::Is;
    else
        out->op = quoted ? RuleTerm::Is : RuleTerm::Contains;

    out->value = value;
    return true;
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

    QStringList parts;
    for (const RuleTerm &term : terms)
        parts.append(compileTerm(term));

    const QString glue = join == Any ? QStringLiteral(" or ")
                                     : QStringLiteral(" and ");
    QString out = parts.join(glue);

    // An `or` group followed by `and not` must be parenthesised or the `and`
    // binds tighter than the `or`: `a or b and not c` is `a or (b and not c)`,
    // which matches every `a` whatever the exclusion says.
    if (join == Any && !exclusions.isEmpty() && terms.size() > 1)
        out = QLatin1Char('(') + out + QLatin1Char(')');

    for (const RuleTerm &term : exclusions) {
        // The block IS the negation, so its rows are stored un-negated and
        // the `and not` is applied here. A row stored negated would compile
        // to `and not not subject:x`.
        out += QStringLiteral(" and not ") + compileTerm(term);
    }

    return out;
}

RuleQuery RuleQuery::parse(const QString &query)
{
    RuleQuery out;

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        // An empty query is a rule with no rows yet, not a failure.
        out.parsed = true;
        return out;
    }

    QStringList tokens;
    if (!tokenise(trimmed, &tokens))
        return RuleQuery();

    // Walk the chain: term, operator, term, ... Anything else rejects whole.
    bool sawOr = false;
    bool sawAnd = false;
    int i = 0;

    while (i < tokens.size()) {
        bool negated = false;
        if (tokens.at(i).compare(QStringLiteral("not"),
                                 Qt::CaseInsensitive) == 0) {
            negated = true;
            ++i;
            if (i >= tokens.size())
                return RuleQuery();
        }

        RuleTerm term;
        if (!parseTerm(tokens.at(i), &term))
            return RuleQuery();

        if (negated) {
            // `not date:` has no row form: "not before" is "after", which the
            // unnegated operators already express.
            if (term.field == RuleTerm::Date)
                return RuleQuery();
            // The block IS the negation, so the row is stored un-negated and
            // compile() re-applies the `and not`.
            out.exclusions.append(term);
        } else {
            out.terms.append(term);
        }
        ++i;

        if (i >= tokens.size())
            break;

        const QString glue = tokens.at(i).toLower();
        if (glue == QStringLiteral("and")) {
            sawAnd = true;
        } else if (glue == QStringLiteral("or")) {
            sawOr = true;
        } else {
            return RuleQuery();   // Not a joining word: unrepresentable.
        }
        ++i;
        if (i >= tokens.size())
            return RuleQuery();   // Trailing operator.
    }

    // Mixed and/or without parentheses is ambiguous to a reader and binds in
    // a way the rows cannot show. Reject rather than guess.
    if (sawAnd && sawOr)
        return RuleQuery();
    if (out.terms.isEmpty())
        return RuleQuery();

    out.join = sawOr ? Any : All;
    out.parsed = true;
    return out;
}
