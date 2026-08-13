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
    void everyFieldCompilesToItsPrefix();
    void isQuotesAndContainsDoesNot();
    void negationPrefixesNot();
    void aValueWithASpaceIsAlwaysQuoted();
    void folderAppendsTheRecursiveSuffix();
    void dateCompilesToAOneSidedRange();
};

void TestRuleQuery::aSingleContainsTermCompiles()
{
    RuleQuery q;
    q.terms.append({RuleTerm::From, RuleTerm::Contains,
                    QStringLiteral("sender@example.org")});

    QCOMPARE(q.compile(), QStringLiteral("from:sender@example.org"));
}

void TestRuleQuery::everyFieldCompilesToItsPrefix()
{
    const QVector<QPair<RuleTerm::Field, QString>> cases = {
        {RuleTerm::From,    QStringLiteral("from:x")},
        {RuleTerm::To,      QStringLiteral("to:x")},
        {RuleTerm::Cc,      QStringLiteral("cc:x")},
        {RuleTerm::Subject, QStringLiteral("subject:x")},
    };

    for (const auto &c : cases) {
        RuleQuery q;
        q.terms.append({c.first, RuleTerm::Contains, QStringLiteral("x")});
        QCOMPARE(q.compile(), c.second);
    }
}

void TestRuleQuery::isQuotesAndContainsDoesNot()
{
    RuleQuery contains;
    contains.terms.append({RuleTerm::Subject, RuleTerm::Contains,
                           QStringLiteral("receipt")});
    QCOMPARE(contains.compile(), QStringLiteral("subject:receipt"));

    RuleQuery is;
    is.terms.append({RuleTerm::Subject, RuleTerm::Is,
                     QStringLiteral("receipt")});
    QCOMPARE(is.compile(), QStringLiteral("subject:\"receipt\""));
}

void TestRuleQuery::negationPrefixesNot()
{
    RuleQuery q;
    q.terms.append({RuleTerm::Subject, RuleTerm::ContainsNot,
                    QStringLiteral("receipt")});
    QCOMPARE(q.compile(), QStringLiteral("not subject:receipt"));

    RuleQuery tag;
    tag.terms.append({RuleTerm::Tag, RuleTerm::IsNot,
                      QStringLiteral("inbox")});
    QCOMPARE(tag.compile(), QStringLiteral("not tag:inbox"));

    RuleQuery att;
    att.terms.append({RuleTerm::Attachment, RuleTerm::HasNot,
                      QStringLiteral("pdf")});
    QCOMPARE(att.compile(), QStringLiteral("not attachment:pdf"));
}

void TestRuleQuery::aValueWithASpaceIsAlwaysQuoted()
{
    // Unquoted, a space would end the term and the rest would become a
    // separate bare word, silently widening the rule.
    RuleQuery q;
    q.terms.append({RuleTerm::Subject, RuleTerm::Contains,
                    QStringLiteral("your receipt")});
    QCOMPARE(q.compile(), QStringLiteral("subject:\"your receipt\""));
}

void TestRuleQuery::folderAppendsTheRecursiveSuffix()
{
    // A path: without the suffix matches nothing, and notmuch reports no
    // error when it happens.
    RuleQuery q;
    q.terms.append({RuleTerm::Folder, RuleTerm::Is,
                    QStringLiteral("account-one")});
    QCOMPARE(q.compile(), QStringLiteral("path:\"account-one/**\""));
}

void TestRuleQuery::dateCompilesToAOneSidedRange()
{
    RuleQuery before;
    before.terms.append({RuleTerm::Date, RuleTerm::Before,
                         QStringLiteral("2026-01-01")});
    QCOMPARE(before.compile(), QStringLiteral("date:..2026-01-01"));

    RuleQuery after;
    after.terms.append({RuleTerm::Date, RuleTerm::After,
                        QStringLiteral("2026-01-01")});
    QCOMPARE(after.compile(), QStringLiteral("date:2026-01-01.."));
}

QTEST_MAIN(TestRuleQuery)
#include "test_rulequery.moc"
