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

#include <QtTest>

#include "rulequery.h"

/// RuleQuery is a view over a string the notmuch post-new hook executes, so
/// the risk is a query that compiles to something subtly wider than the rows
/// say. These tests are about exact strings, not about whether notmuch would
/// accept the result: notmuch accepts almost anything, including `from:((((`.
class TestRuleQuery : public QObject
{
    Q_OBJECT

private slots:
    void aSingleContainsTermCompiles();
};

void TestRuleQuery::aSingleContainsTermCompiles()
{
    RuleQuery q;
    q.terms.append({RuleTerm::From, RuleTerm::Contains,
                    QStringLiteral("sender@example.org")});

    QCOMPARE(q.compile(), QStringLiteral("from:sender@example.org"));
}

QTEST_MAIN(TestRuleQuery)
#include "test_rulequery.moc"
