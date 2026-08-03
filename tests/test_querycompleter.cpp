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

QTEST_MAIN(TestQueryCompleter)
#include "test_querycompleter.moc"
