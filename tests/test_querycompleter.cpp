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

#include "querycompleter.h"

class TestQueryCompleter : public QObject
{
    Q_OBJECT

private slots:
    void emptyTextCompletesPrefix();
    void bareWordCompletesPrefix();
    void wordAfterOperatorCompletesPrefix();
    void prefixReplaceSpanCoversTheWord();
    void colonSwitchesToValue();
    void valueReplaceSpanExcludesThePrefix();
    void prefixIsLowercased();
    void emptyValueAfterColonStillCompletes();
    void insideQuotesCompletesNothing();
    void afterClosedQuotesCompletesAgain();
    void rangeUpperBoundCompletes();
    void rangeLowerBoundCompletes();
    void bareValueAllowsRelativeEntries();
    void rangeSuppressesRelativeEntries();
};

void TestQueryCompleter::emptyTextCompletesPrefix()
{
    const CompletionContext ctx = completionContext(QString(), 0);
    QCOMPARE(ctx.kind, CompletionContext::Prefix);
    QCOMPARE(ctx.stem, QString());
}

void TestQueryCompleter::bareWordCompletesPrefix()
{
    const CompletionContext ctx = completionContext(QStringLiteral("su"), 2);
    QCOMPARE(ctx.kind, CompletionContext::Prefix);
    QCOMPARE(ctx.stem, QStringLiteral("su"));
}

void TestQueryCompleter::wordAfterOperatorCompletesPrefix()
{
    // The token boundary is whitespace, not the start of the line.
    const QString text = QStringLiteral("tag:inbox and su");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Prefix);
    QCOMPARE(ctx.stem, QStringLiteral("su"));
}

void TestQueryCompleter::prefixReplaceSpanCoversTheWord()
{
    const QString text = QStringLiteral("tag:inbox and su");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.replaceFrom, 14);
    QCOMPARE(ctx.replaceLength, 2);
}

void TestQueryCompleter::colonSwitchesToValue()
{
    const QString text = QStringLiteral("tag:sho");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.prefix, QStringLiteral("tag"));
    QCOMPARE(ctx.stem, QStringLiteral("sho"));
}

void TestQueryCompleter::valueReplaceSpanExcludesThePrefix()
{
    // Accepting must overwrite "sho" only, never "tag:".
    const QString text = QStringLiteral("tag:sho");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.replaceFrom, 4);
    QCOMPARE(ctx.replaceLength, 3);
}

void TestQueryCompleter::prefixIsLowercased()
{
    const QString text = QStringLiteral("TAG:sho");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.prefix, QStringLiteral("tag"));
}

void TestQueryCompleter::emptyValueAfterColonStillCompletes()
{
    // "tag:" with the cursor at the end offers every tag.
    const QString text = QStringLiteral("tag:");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.prefix, QStringLiteral("tag"));
    QCOMPARE(ctx.stem, QString());
    QCOMPARE(ctx.replaceFrom, 4);
    QCOMPARE(ctx.replaceLength, 0);
}

void TestQueryCompleter::insideQuotesCompletesNothing()
{
    // subject:"foo bar| is a literal, not a keyword position.
    const QString text = QStringLiteral("subject:\"foo bar");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::None);
}

void TestQueryCompleter::afterClosedQuotesCompletesAgain()
{
    const QString text = QStringLiteral("subject:\"foo bar\" and ta");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Prefix);
    QCOMPARE(ctx.stem, QStringLiteral("ta"));
}

void TestQueryCompleter::rangeUpperBoundCompletes()
{
    const QString text = QStringLiteral("date:today..yes");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.prefix, QStringLiteral("date"));
    QCOMPARE(ctx.stem, QStringLiteral("yes"));
    // Overwrites "yes" only: "date:today.." must survive.
    QCOMPARE(ctx.replaceFrom, 12);
    QCOMPARE(ctx.replaceLength, 3);
}

void TestQueryCompleter::rangeLowerBoundCompletes()
{
    // Cursor sits at offset 8, before the "..".
    const QString text = QStringLiteral("date:las..today");
    const CompletionContext ctx = completionContext(text, 8);
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.stem, QStringLiteral("las"));
    QCOMPARE(ctx.replaceFrom, 5);
    QCOMPARE(ctx.replaceLength, 3);
}

void TestQueryCompleter::bareValueAllowsRelativeEntries()
{
    const QString text = QStringLiteral("date:1w");
    const CompletionContext ctx = completionContext(text, text.size());
    QVERIFY(ctx.allowRangeEntries);
}

void TestQueryCompleter::rangeSuppressesRelativeEntries()
{
    // "1week.." offered here would produce date:1week....today.
    const QString text = QStringLiteral("date:1w..today");
    const CompletionContext ctx = completionContext(text, 7);
    QVERIFY(!ctx.allowRangeEntries);
}

QTEST_MAIN(TestQueryCompleter)
#include "test_querycompleter.moc"
