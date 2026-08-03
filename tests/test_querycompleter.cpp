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
#include <QTemporaryDir>

#include <QLineEdit>

#include "config.h"
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
    void prefixVocabularyCoversNotmuchKeywords();
    void dateVocabularySeparatesRelativeEntries();
    void tagAndIsShareTheTagModel();
    void pathOffersAccountMaildirsBothForms();
    void folderOffersNothing();
    void mimetypeAppendsConfiguredEntries();
    void rangeContextDropsRelativeDates();
    void acceptReplacesOnlyThePrefixToken();
    void acceptReplacesOnlyTheValueAfterThePrefix();
    void acceptReplacesOnlyTheEditedRangeBound();
    void acceptReplacesTheWholeBoundWhenCompletingMidWord();
};

// Copied from tests/test_config.cpp rather than shared, so the two test files
// stay independent.
static QString writeIni(const QTemporaryDir &dir, const QString &body)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(body.toUtf8());
    f.close();
    return path;
}

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
    // Cursor sits at offset 8, mid-way through the lower bound rather than at
    // its end. End-of-bound would not discriminate: there the whole-bound span
    // and the typed-so-far span happen to be the same length.
    const QString text = QStringLiteral("date:lastweek..today");
    const CompletionContext ctx = completionContext(text, 8);
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.stem, QStringLiteral("las"));
    QCOMPARE(ctx.replaceFrom, 5);
    // Covers the whole lower bound, so accepting leaves no "tweek" tail.
    QCOMPARE(ctx.replaceLength, 8);
    QVERIFY(!ctx.allowRangeEntries);
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

void TestQueryCompleter::prefixVocabularyCoversNotmuchKeywords()
{
    const QList<CompletionEntry> entries = prefixVocabulary();

    QStringList values;
    for (const CompletionEntry &entry : entries)
        values.append(entry.value);

    QVERIFY(values.contains(QStringLiteral("tag:")));
    QVERIFY(values.contains(QStringLiteral("date:")));
    QVERIFY(values.contains(QStringLiteral("and")));

    // Every entry carries a description; a blank column teaches nothing.
    for (const CompletionEntry &entry : entries)
        QVERIFY(!entry.description.isEmpty());
}

void TestQueryCompleter::dateVocabularySeparatesRelativeEntries()
{
    const QList<CompletionEntry> entries = dateVocabulary();

    bool sawSymbolic = false;
    bool sawRelative = false;
    for (const CompletionEntry &entry : entries) {
        if (entry.value == QStringLiteral("today"))
            sawSymbolic = true;
        if (entry.value.contains(QStringLiteral("..")))
            sawRelative = true;
    }
    QVERIFY(sawSymbolic);
    QVERIFY(sawRelative);
}

void TestQueryCompleter::tagAndIsShareTheTagModel()
{
    Config config;
    QueryCompleter completer(nullptr, config);
    completer.setTags({ QStringLiteral("inbox"), QStringLiteral("shopping/amazon") });

    const QStringList forTag = completer.candidatesFor(
        completionContext(QStringLiteral("tag:"), 4));
    const QStringList forIs = completer.candidatesFor(
        completionContext(QStringLiteral("is:"), 3));

    QVERIFY(forTag.contains(QStringLiteral("shopping/amazon")));
    QCOMPARE(forTag, forIs);
}

void TestQueryCompleter::pathOffersAccountMaildirsBothForms()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "maildir = work\n"
        "address = you@example.org\n")));

    QueryCompleter completer(nullptr, config);
    const QStringList candidates = completer.candidatesFor(
        completionContext(QStringLiteral("path:"), 5));

    QVERIFY(candidates.contains(QStringLiteral("work")));
    // The recursive form is what scopedQuery() itself builds and is not
    // guessable, so it is offered directly.
    QVERIFY(candidates.contains(QStringLiteral("work/**")));
}

void TestQueryCompleter::folderOffersNothing()
{
    // folder: matches a Maildir folder name, not a path, and its values are
    // not enumerable from config. Prefix-only, like from: and to:.
    Config config;
    QueryCompleter completer(nullptr, config);
    const QStringList candidates = completer.candidatesFor(
        completionContext(QStringLiteral("folder:"), 7));
    QVERIFY(candidates.isEmpty());
}

void TestQueryCompleter::mimetypeAppendsConfiguredEntries()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[completion]\n"
        "extra_mimetypes = application/epub+zip|EPUB book\n")));

    QueryCompleter completer(nullptr, config);
    const QStringList candidates = completer.candidatesFor(
        completionContext(QStringLiteral("mimetype:"), 9));

    QVERIFY(candidates.contains(QStringLiteral("application/epub+zip")));
    QVERIFY(candidates.contains(QStringLiteral("application/pdf")));
}

void TestQueryCompleter::rangeContextDropsRelativeDates()
{
    Config config;
    QueryCompleter completer(nullptr, config);

    const QStringList bare = completer.candidatesFor(
        completionContext(QStringLiteral("date:"), 5));
    QVERIFY(bare.contains(QStringLiteral("1week..")));

    const QString ranged = QStringLiteral("date:today..");
    const QStringList inRange = completer.candidatesFor(
        completionContext(ranged, ranged.size()));
    QVERIFY(inRange.contains(QStringLiteral("yesterday")));
    QVERIFY(!inRange.contains(QStringLiteral("1week..")));
}

// The accept path is driven directly rather than through synthetic key
// events: whether a key needs Shift is a keyboard-layout property, so
// QTest::keyClick could never decide whether this logic is right.
static QString acceptInto(const QString &text, int cursor, const QString &value)
{
    Config config;
    QLineEdit edit;
    QueryCompleter completer(&edit, config);

    edit.setText(text);
    edit.setCursorPosition(cursor);
    completer.updateContext();
    completer.acceptCompletion(value);

    return edit.text();
}

void TestQueryCompleter::acceptReplacesOnlyThePrefixToken()
{
    // The neighbouring token must survive untouched.
    QCOMPARE(acceptInto(QStringLiteral("tag:inbox su"), 12,
                        QStringLiteral("subject:")),
             QStringLiteral("tag:inbox subject:"));
}

void TestQueryCompleter::acceptReplacesOnlyTheValueAfterThePrefix()
{
    // QCompleter's own insertion would overwrite "date:tod" whole, because
    // that is the token it matched on. Only "tod" may be replaced.
    QCOMPARE(acceptInto(QStringLiteral("date:tod"), 8, QStringLiteral("today")),
             QStringLiteral("date:today"));
}

void TestQueryCompleter::acceptReplacesOnlyTheEditedRangeBound()
{
    const QString text = QStringLiteral("date:yesterday..to");
    QCOMPARE(acceptInto(text, text.size(), QStringLiteral("today")),
             QStringLiteral("date:yesterday..today"));
}

void TestQueryCompleter::acceptReplacesTheWholeBoundWhenCompletingMidWord()
{
    // Caret sits after "yest" but the bound runs to the "..", so accepting
    // must leave no "erday" tail behind.
    QCOMPARE(acceptInto(QStringLiteral("date:yesterday..today"), 9,
                        QStringLiteral("this_week")),
             QStringLiteral("date:this_week..today"));
}

QTEST_MAIN(TestQueryCompleter)
#include "test_querycompleter.moc"
