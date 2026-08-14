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

#include "searchterm.h"

/// The query grammar of the right-click search actions.
///
/// Asserted on the CONSTRUCTED STRING throughout, never on a query notmuch
/// refuses: notmuch's parser rejects almost nothing, so `from:((((` parses
/// cleanly and matches zero. A test expecting a failure would pass against
/// correct code and against broken code alike.
class TestSearchTerm : public QObject
{
    Q_OBJECT
private slots:
    void quotesAPlainValue();
    void escapesEmbeddedQuotes();
    void collapsesWhitespaceAndNewlines();
    void rejectsEmptyAndWhitespaceOnly();
    void capsAnOverlongSelection();
    void buildsAFieldTerm();
    void omitsAFieldWithNoValue();
    void buildsADateRangeForOneDay();
    void tagIsNotQuoted();
    void extendParenthesisesBothSides();
    void extendOntoAnEmptyQueryIsAReplace();
    void excludeParenthesisesBothSides();
    void excludeFromAnEmptyQueryIsEmpty();
    void excludeWithNothingToExcludeLeavesTheQuery();
};

void TestSearchTerm::quotesAPlainValue()
{
    QCOMPARE(SearchTerm::quote(QStringLiteral("Quarterly report")),
             QStringLiteral("\"Quarterly report\""));
}

void TestSearchTerm::escapesEmbeddedQuotes()
{
    // A selection is arbitrary prose and can carry a quote. Unescaped, it ends
    // the quoted string early and the rest becomes stray query syntax, which
    // notmuch accepts and matches nothing on.
    QCOMPARE(SearchTerm::quote(QStringLiteral("say \"hello\" now")),
             QStringLiteral("\"say \\\"hello\\\" now\""));
}

void TestSearchTerm::collapsesWhitespaceAndNewlines()
{
    QCOMPARE(SearchTerm::quote(QStringLiteral("  two\n\nlines\there  ")),
             QStringLiteral("\"two lines here\""));
}

void TestSearchTerm::rejectsEmptyAndWhitespaceOnly()
{
    // An empty term must yield an empty string, which is what every caller
    // tests to decide whether to offer a menu entry at all.
    QVERIFY(SearchTerm::quote(QString()).isEmpty());
    QVERIFY(SearchTerm::quote(QStringLiteral("   \n\t ")).isEmpty());
}

void TestSearchTerm::capsAnOverlongSelection()
{
    // A multi-kilobyte selection is a mis-drag, not a query.
    const QString huge(5000, QLatin1Char('x'));
    const QString term = SearchTerm::quote(huge);
    QVERIFY2(term.size() < 300,
             qPrintable(QStringLiteral("term was %1 chars").arg(term.size())));
    QVERIFY(term.startsWith(QStringLiteral("\"xxx")));
    QVERIFY(term.endsWith(QLatin1Char('"')));
}

void TestSearchTerm::buildsAFieldTerm()
{
    QCOMPARE(SearchTerm::field(QStringLiteral("from"),
                               QStringLiteral("Foo <foo@example.org>")),
             QStringLiteral("from:\"Foo <foo@example.org>\""));
}

void TestSearchTerm::omitsAFieldWithNoValue()
{
    // A message with no Cc must not offer cc:"" , which parses cleanly and
    // matches nothing, so the entry would look enabled and do nothing.
    QVERIFY(SearchTerm::field(QStringLiteral("cc"), QString()).isEmpty());
}

void TestSearchTerm::buildsADateRangeForOneDay()
{
    // notmuch's date: range is inclusive at both ends, so one day is the day
    // named twice rather than the day and its successor.
    const QDate day(2026, 8, 14);
    QCOMPARE(SearchTerm::onDate(day),
             QStringLiteral("date:2026-08-14..2026-08-14"));
    QVERIFY(SearchTerm::onDate(QDate()).isEmpty());
}

void TestSearchTerm::tagIsNotQuoted()
{
    // A tag name is a token from a known vocabulary, not prose. Quoting one
    // is not wrong but reads badly in the bar, and the user edits that text.
    QCOMPARE(SearchTerm::tag(QStringLiteral("inbox")),
             QStringLiteral("tag:inbox"));
    // A tag containing a space is the exception and does need quoting.
    QCOMPARE(SearchTerm::tag(QStringLiteral("to do")),
             QStringLiteral("tag:\"to do\""));
    QVERIFY(SearchTerm::tag(QString()).isEmpty());
}

void TestSearchTerm::extendParenthesisesBothSides()
{
    // THE case this exists for. The bar may hold a hand-written disjunction,
    // and `a or b AND c` binds as `a or (b AND c)`: the result WIDENS a search
    // the user asked to narrow. Both sides are wrapped so neither can rebind.
    QCOMPARE(SearchTerm::extend(QStringLiteral("tag:inbox or tag:flagged"),
                                QStringLiteral("from:foo@example.org")),
             QStringLiteral("(tag:inbox or tag:flagged) AND (from:foo@example.org)"));
}

void TestSearchTerm::extendOntoAnEmptyQueryIsAReplace()
{
    // Rather than "() AND (x)", which matches nothing.
    QCOMPARE(SearchTerm::extend(QString(), QStringLiteral("tag:inbox")),
             QStringLiteral("tag:inbox"));
    QCOMPARE(SearchTerm::extend(QStringLiteral("   "),
                                QStringLiteral("tag:inbox")),
             QStringLiteral("tag:inbox"));
    // And an empty new term leaves the existing query alone.
    QCOMPARE(SearchTerm::extend(QStringLiteral("tag:inbox"), QString()),
             QStringLiteral("tag:inbox"));
}

void TestSearchTerm::excludeParenthesisesBothSides()
{
    // The same trap as extendParenthesisesBothSides, and worse in this
    // direction. Unparenthesised, `a or b AND NOT c` binds as
    // `a or (b AND NOT c)`: the exclusion covers only the second term, so
    // every message matching `a` stays on screen INCLUDING the ones the user
    // asked to be rid of. notmuch reports no error for either form, so this
    // assertion is the only thing that fails.
    QCOMPARE(SearchTerm::exclude(QStringLiteral("tag:inbox or tag:flagged"),
                                 QStringLiteral("from:foo@example.org")),
             QStringLiteral(
                 "(tag:inbox or tag:flagged) AND NOT (from:foo@example.org)"));
}

void TestSearchTerm::excludeFromAnEmptyQueryIsEmpty()
{
    // Deliberately NOT extend()'s behaviour. extend() returns the addition
    // alone, because narrowing nothing by x sensibly means x. Excluding from
    // nothing would mean the whole Maildir minus one value: a legitimate
    // query, and an implausible thing to have meant by right-clicking a value
    // in a fresh window. The menus grey the entry out; this is the second
    // layer, against a caller that forgets the guard.
    QVERIFY(SearchTerm::exclude(QString(), QStringLiteral("tag:inbox"))
                .isEmpty());
    QVERIFY(SearchTerm::exclude(QStringLiteral("   "),
                                QStringLiteral("tag:inbox"))
                .isEmpty());
}

void TestSearchTerm::excludeWithNothingToExcludeLeavesTheQuery()
{
    QCOMPARE(SearchTerm::exclude(QStringLiteral("tag:inbox"), QString()),
             QStringLiteral("tag:inbox"));
    QVERIFY(SearchTerm::exclude(QString(), QString()).isEmpty());
}

QTEST_MAIN(TestSearchTerm)
#include "test_searchterm.moc"
