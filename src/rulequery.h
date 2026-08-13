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

#include <QList>
#include <QString>

/// One row in the builder: a field, an operator, and a value.
struct RuleTerm
{
    enum Field { From, To, Cc, Subject, Tag, Folder, Attachment, Date };

    enum Op {
        Contains, ContainsNot,   ///< Unquoted term, optionally negated.
        Is, IsNot,               ///< Quoted phrase, optionally negated.
        Has, HasNot,             ///< attachment: only.
        Before, After            ///< date: only. "not before" is "after", so
                                 ///< these carry no negated twin.
    };

    Field field = From;
    Op op = Contains;
    QString value;
};

bool operator==(const RuleTerm &a, const RuleTerm &b);

/// A tagging rule's query, as rows.
///
/// The STRING is the stored format, shared with mailctl and executed by the
/// notmuch post-new hook. This type is a view over it, never the store: a
/// query it cannot represent must still open, save and run unchanged.
struct RuleQuery
{
    enum Join { All, Any };   ///< and / or, over the positive terms only.

    Join join = All;
    QList<RuleTerm> terms;        ///< Positive section.
    QList<RuleTerm> exclusions;   ///< The "but not" block, joined `and not`.

    /// False when the query cannot be shown as rows. NOT an error and NOT a
    /// claim that the query is invalid: the rule opens in text mode.
    bool parsed = false;

    static RuleQuery parse(const QString &query);
    QString compile() const;
};

bool operator==(const RuleQuery &a, const RuleQuery &b);
