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
    void allJoinsWithAnd();
    void anyJoinsWithOr();
    void exclusionsAppendAsAndNot();
    void anyIsParenthesisedOnlyWhenExclusionsFollow();
    void anEmptyQueryCompilesToAnEmptyString();
    void aFlatAndChainParses();
    void aFlatOrChainParses();
    void anEmptyQueryParsesToNoRows();
    void quotedValuesLoseTheirQuotes();
    void aNegatedTermParsesAsANegatedOperator();
    void whatParsesCompilesBackUnchanged();
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

void TestRuleQuery::allJoinsWithAnd()
{
    RuleQuery q;
    q.join = RuleQuery::All;
    q.terms.append({RuleTerm::From, RuleTerm::Contains,
                    QStringLiteral("vendor.example.org")});
    q.terms.append({RuleTerm::Subject, RuleTerm::Contains,
                    QStringLiteral("receipt")});

    QCOMPARE(q.compile(),
             QStringLiteral("from:vendor.example.org and subject:receipt"));
}

void TestRuleQuery::anyJoinsWithOr()
{
    RuleQuery q;
    q.join = RuleQuery::Any;
    q.terms.append({RuleTerm::From, RuleTerm::Contains,
                    QStringLiteral("one.example.org")});
    q.terms.append({RuleTerm::From, RuleTerm::Contains,
                    QStringLiteral("two.example.org")});

    QCOMPARE(q.compile(),
             QStringLiteral("from:one.example.org or from:two.example.org"));
}

void TestRuleQuery::exclusionsAppendAsAndNot()
{
    RuleQuery q;
    q.join = RuleQuery::All;
    q.terms.append({RuleTerm::From, RuleTerm::Contains,
                    QStringLiteral("vendor.example.org")});
    q.exclusions.append({RuleTerm::Subject, RuleTerm::Contains,
                         QStringLiteral("receipt")});

    QCOMPARE(q.compile(),
             QStringLiteral("from:vendor.example.org "
                            "and not subject:receipt"));
}

void TestRuleQuery::anyIsParenthesisedOnlyWhenExclusionsFollow()
{
    // Without the parens this binds as (a or (b and not c)), which matches
    // everything from the first sender regardless of the exclusion.
    RuleQuery guarded;
    guarded.join = RuleQuery::Any;
    guarded.terms.append({RuleTerm::From, RuleTerm::Contains,
                          QStringLiteral("one.example.org")});
    guarded.terms.append({RuleTerm::From, RuleTerm::Contains,
                          QStringLiteral("two.example.org")});
    guarded.exclusions.append({RuleTerm::Subject, RuleTerm::Contains,
                               QStringLiteral("receipt")});

    QCOMPARE(guarded.compile(),
             QStringLiteral("(from:one.example.org or from:two.example.org) "
                            "and not subject:receipt"));

    // No exclusion, no parens: they would be noise in the stored file.
    RuleQuery bare;
    bare.join = RuleQuery::Any;
    bare.terms.append({RuleTerm::From, RuleTerm::Contains,
                       QStringLiteral("one.example.org")});
    bare.terms.append({RuleTerm::From, RuleTerm::Contains,
                       QStringLiteral("two.example.org")});

    QCOMPARE(bare.compile(),
             QStringLiteral("from:one.example.org or from:two.example.org"));
}

void TestRuleQuery::anEmptyQueryCompilesToAnEmptyString()
{
    RuleQuery q;
    QCOMPARE(q.compile(), QString());
}

void TestRuleQuery::aFlatAndChainParses()
{
    const RuleQuery q = RuleQuery::parse(
        QStringLiteral("from:vendor.example.org and subject:receipt"));

    QVERIFY(q.parsed);
    QCOMPARE(q.join, RuleQuery::All);
    QCOMPARE(q.terms.size(), 2);
    QCOMPARE(q.terms.at(0).field, RuleTerm::From);
    QCOMPARE(q.terms.at(0).op, RuleTerm::Contains);
    QCOMPARE(q.terms.at(0).value, QStringLiteral("vendor.example.org"));
    QCOMPARE(q.terms.at(1).field, RuleTerm::Subject);
    QVERIFY(q.exclusions.isEmpty());
}

void TestRuleQuery::aFlatOrChainParses()
{
    const RuleQuery q = RuleQuery::parse(
        QStringLiteral("from:one.example.org or from:two.example.org"));

    QVERIFY(q.parsed);
    QCOMPARE(q.join, RuleQuery::Any);
    QCOMPARE(q.terms.size(), 2);
}

void TestRuleQuery::anEmptyQueryParsesToNoRows()
{
    // One shipped rule has an empty query. It must open in the builder ready
    // to receive a row, not fall back to text mode.
    const RuleQuery q = RuleQuery::parse(QString());

    QVERIFY(q.parsed);
    QVERIFY(q.terms.isEmpty());
}

void TestRuleQuery::quotedValuesLoseTheirQuotes()
{
    const RuleQuery q = RuleQuery::parse(
        QStringLiteral("subject:\"your receipt\""));

    QVERIFY(q.parsed);
    QCOMPARE(q.terms.size(), 1);
    QCOMPARE(q.terms.at(0).value, QStringLiteral("your receipt"));
    QCOMPARE(q.terms.at(0).op, RuleTerm::Is);
}

void TestRuleQuery::aNegatedTermParsesAsANegatedOperator()
{
    const RuleQuery q = RuleQuery::parse(
        QStringLiteral("from:vendor.example.org and not tag:inbox"));

    QVERIFY(q.parsed);
    // A trailing negation on an `and` chain becomes an exclusion: that is how
    // the user describes these rules, and the design records the preference.
    QCOMPARE(q.terms.size(), 1);
    QCOMPARE(q.exclusions.size(), 1);
    QCOMPARE(q.exclusions.at(0).field, RuleTerm::Tag);
    QCOMPARE(q.exclusions.at(0).op, RuleTerm::Is);
}

void TestRuleQuery::whatParsesCompilesBackUnchanged()
{
    // The dialog decides whether to rewrite the stored string by comparing
    // against what it parsed, so a compile that differs by so much as a quote
    // would churn a file a second tool reads.
    const QStringList queries = {
        QStringLiteral("from:vendor.example.org"),
        QStringLiteral("from:vendor.example.org and subject:receipt"),
        QStringLiteral("from:one.example.org or from:two.example.org"),
        QStringLiteral("subject:\"your receipt\""),
        QStringLiteral("path:\"account-one/**\""),
        QStringLiteral("tag:inbox"),
        QStringLiteral("attachment:pdf"),
        QStringLiteral("date:..2026-01-01"),
        QStringLiteral("date:2026-01-01.."),
        QStringLiteral("from:vendor.example.org and not tag:inbox"),
        QStringLiteral("from:vendor.example.org and not subject:receipt "
                       "and not subject:refund"),
    };

    for (const QString &query : queries) {
        const RuleQuery parsed = RuleQuery::parse(query);
        QVERIFY2(parsed.parsed, qPrintable(query));
        QCOMPARE(parsed.compile(), query);
    }
}

QTEST_MAIN(TestRuleQuery)
#include "test_rulequery.moc"
