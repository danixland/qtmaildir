# Searching From The Message Pane Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Right-clicking a value in the message pane offers to search for it, either replacing the query bar or narrowing what is already there.

**Architecture:** A pure `SearchTerm` namespace builds notmuch query strings and combines them, with no Qt widget involved, so the query logic is tested without a painter or a web engine. `MessageView`, `TagStrip` and a new `MessageDetailsDialog` build context menus and emit a finished query plus the chosen operation; `MainWindow` sets the query bar and calls its existing runner. No pane touches the query bar directly.

**Tech Stack:** Qt 6.11 (Widgets, WebEngineWidgets, Test), C++17, CMake + Ninja, QtTest.

**Spec:** `docs/superpowers/specs/2026-08-14-search-from-message-design.md`

---

## Read before starting

These are project facts that will cost you a session each if rediscovered:

- **Never run a test binary without `QT_QPA_PLATFORM=offscreen`**, and never launch `./build/src/qtmaildir`. A direct run of `test_mainwindow` throws a hundred windows onto the user's desktop. Every command in this plan already sets it.
- **Rendering probes lie.** Do not assert this feature works by counting pixels. Every test here asserts on constructed strings, on `QAction` lists and on geometry from one function.
- **A mis-quoted notmuch query is not an error**, it matches zero. Never assert on a provoked parser failure; assert on the constructed string.
- **`test_mainwindow` hangs, rather than fails, when a fixture produces a config problem** (item 84), because `showWarnings()` raises a modal from the constructor. If a test in Task 8 hangs, kill the surviving process and rebuild before theorising: a stale binary otherwise reports a fixed failure as still failing.

## File Structure

**Create:**
- `src/searchterm.h` / `src/searchterm.cpp` — builds and combines notmuch query strings. No Qt widgets. The whole query grammar of this feature lives here.
- `src/messagedetailsdialog.h` / `src/messagedetailsdialog.cpp` — the details dialog, rebuilt as rows, moved out of `MessageView`.
- `tests/test_searchterm.cpp` — term construction and combining.
- `tests/test_messagedetailsdialog.cpp` — rows, values and the plain-text property.

**Modify:**
- `src/mimeparser.h` / `src/mimeparser.cpp` — expose the existing RFC 2822 date cleaning as `MimeParser::parseDate()`, currently trapped in a file-local function.
- `src/tagstrip.h` / `src/tagstrip.cpp` — `chipAt()` hit test and a context-menu signal.
- `src/messageview.h` / `src/messageview.cpp` — header context menu, web view context menu, details dialog delegation, one new signal.
- `src/mainwindow.h` / `src/mainwindow.cpp` — receive the signal, set the query bar, run.
- `src/CMakeLists.txt`, `tests/CMakeLists.txt` — register the new files.
- `CHANGELOG.md` — the user-visible entry.
- `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md` and its closed-items file — close item 85.

**Ordering.** Tasks 1 and 2 are pure logic with no UI. Tasks 3 to 6 add one surface each and are independent of one another. Task 7 wires the window, Task 8 covers it end to end.

---

### Task 1: `SearchTerm` builds and combines query strings

**Files:**
- Create: `src/searchterm.h`, `src/searchterm.cpp`
- Create: `tests/test_searchterm.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_searchterm.cpp`:

```cpp
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
    // and `a or b AND c` binds as `a or (b AND c)`, which WIDENS a search the
    // user asked to narrow. Both sides are wrapped so neither can rebind.
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

QTEST_MAIN(TestSearchTerm)
#include "test_searchterm.moc"
```

Add to `tests/CMakeLists.txt`, beside the other `add_qtmaildir_test` lines:

```cmake
add_qtmaildir_test(searchterm)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

Expected: the build FAILS with `searchterm.h: No such file or directory`. That is this step's pass condition.

- [ ] **Step 3: Write the implementation**

Create `src/searchterm.h`:

```cpp
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

#include <QDate>
#include <QString>

/// Builds the notmuch query strings behind the right-click search actions.
///
/// Free functions and no widget, so the whole query grammar is testable
/// without a painter, a web engine or a window. Every surface that offers a
/// search goes through here, which is what stops five surfaces growing five
/// slightly different quoting rules.
///
/// **notmuch rejects almost nothing.** `from:((((` parses cleanly and matches
/// zero, so a malformed query produces an empty result rather than an error
/// the user could act on. Correctness here cannot be checked by asking notmuch;
/// it is checked against the constructed string.
namespace SearchTerm {

/// Longest quoted value. A selection longer than this is a mis-drag rather
/// than a search, and the query bar is an editable line the user has to be
/// able to read.
inline constexpr int kMaxValueLength = 200;

/// Quotes an arbitrary value for use as a notmuch term.
///
/// Whitespace and newlines collapse to single spaces, embedded quotes are
/// escaped, the value is capped at kMaxValueLength, and an empty or
/// whitespace-only value yields an EMPTY STRING rather than `""`. Callers
/// test for empty to decide whether to offer a menu entry at all.
QString quote(const QString &value);

/// `field:"value"`, or empty when the value is empty.
///
/// The field name is a notmuch keyword and is never translated: it is wire
/// format, not user-facing text.
QString field(const QString &name, const QString &value);

/// `date:YYYY-MM-DD..YYYY-MM-DD` for a single day, empty for an invalid date.
///
/// The day twice rather than the day and its successor: notmuch's range is
/// inclusive at both ends, so the naive `..next-day` form silently includes a
/// second day of mail.
QString onDate(const QDate &day);

/// `tag:name`, quoted only when the name needs it.
QString tag(const QString &name);

/// Narrows `existing` by `addition`, as `(existing) AND (addition)`.
///
/// **Both sides are parenthesised and that is load-bearing.** The query bar
/// may hold a hand-written disjunction, and `a or b AND c` binds as
/// `a or (b AND c)`: the result WIDENS a search the user asked to narrow, and
/// nothing reports an error. The same trap is why the post-new hook
/// parenthesises a rule's query before scoping it with `tag:new`.
///
/// An empty `existing` yields `addition` alone rather than `() AND (x)`, which
/// matches nothing; an empty `addition` leaves `existing` untouched.
QString extend(const QString &existing, const QString &addition);

}  // namespace SearchTerm
```

Create `src/searchterm.cpp`:

```cpp
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

#include "searchterm.h"

namespace SearchTerm {

QString quote(const QString &value)
{
    // simplified() collapses every run of whitespace, newlines and tabs
    // included, and trims the ends. A selection spanning paragraphs arrives
    // full of newlines, which would otherwise reach the query bar verbatim.
    QString cleaned = value.simplified();
    if (cleaned.isEmpty())
        return {};

    if (cleaned.size() > kMaxValueLength)
        cleaned.truncate(kMaxValueLength);

    // Backslashes first: escaping the quotes first would then escape the
    // backslashes this step adds, doubling them.
    cleaned.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    cleaned.replace(QLatin1Char('"'), QStringLiteral("\\\""));

    return QLatin1Char('"') + cleaned + QLatin1Char('"');
}

QString field(const QString &name, const QString &value)
{
    const QString quoted = quote(value);
    if (quoted.isEmpty())
        return {};
    return name + QLatin1Char(':') + quoted;
}

QString onDate(const QDate &day)
{
    if (!day.isValid())
        return {};
    const QString text = day.toString(QStringLiteral("yyyy-MM-dd"));
    return QStringLiteral("date:%1..%1").arg(text);
}

QString tag(const QString &name)
{
    const QString trimmed = name.simplified();
    if (trimmed.isEmpty())
        return {};

    // A tag is a token from a vocabulary the user chose, so it reads better
    // unquoted in the bar they are about to edit. Quoted only when it holds
    // something that would not survive.
    const bool needsQuoting =
        std::any_of(trimmed.cbegin(), trimmed.cend(), [](QChar ch) {
            return !(ch.isLetterOrNumber() || ch == QLatin1Char('-')
                     || ch == QLatin1Char('_') || ch == QLatin1Char('.')
                     || ch == QLatin1Char('/'));
        });

    return QStringLiteral("tag:") + (needsQuoting ? quote(trimmed) : trimmed);
}

QString extend(const QString &existing, const QString &addition)
{
    const QString left = existing.trimmed();
    const QString right = addition.trimmed();

    if (right.isEmpty())
        return left;
    if (left.isEmpty())
        return right;

    return QStringLiteral("(%1) AND (%2)").arg(left, right);
}

}  // namespace SearchTerm
```

Add `<algorithm>` to the includes in `src/searchterm.cpp`, above `#include "searchterm.h"` is wrong for this project's style; put it after, with the other standard headers:

```cpp
#include "searchterm.h"

#include <algorithm>
```

Add to the `qtmaildir_lib` source list in `src/CMakeLists.txt`, keeping the list's alphabetical order:

```cmake
    searchterm.cpp
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R searchterm --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 1`, 11 test functions passing.

- [ ] **Step 5: Commit**

```bash
git add src/searchterm.h src/searchterm.cpp src/CMakeLists.txt tests/test_searchterm.cpp tests/CMakeLists.txt
git commit -S -m "feat(search): build notmuch terms for the right-click actions

One place for the query grammar behind every search surface, with no widget
involved so it is tested without a painter or a web engine.

extend() parenthesises both sides. The query bar may hold a hand-written
disjunction, and 'a or b AND c' binds as 'a or (b AND c)', which widens a
search meant to narrow it and reports nothing."
```

---

### Task 2: Expose the RFC 2822 date parse from `MimeParser`

**Files:**
- Modify: `src/mimeparser.h`, `src/mimeparser.cpp:275-291`
- Test: `tests/test_mimeparser.cpp`

`ParsedMessage::date` is the raw `Date:` header text, so the date search must parse it. The parse already exists inside the file-local `attachmentFolderName()`, complete with the fix for a trap this feature would otherwise walk into: **`Qt::RFC2822Date` rejects the entire string when a trailing timezone comment is present** (`... +0200 (CEST)`), which is legal per RFC 5322 and common in the wild. Extract it rather than writing a second parser that lacks the fix.

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` list in `tests/test_mimeparser.cpp`:

```cpp
    void parsesADateWithATimezoneComment();
```

And the test body, at the end of the file before `QTEST_MAIN`:

```cpp
void TestMimeParser::parsesADateWithATimezoneComment()
{
    // Qt::RFC2822Date rejects the WHOLE string when a trailing comment is
    // present (verified on Qt 6.11), and "+0200 (CEST)" is both legal and
    // common. Without the comment stripped, every such message loses its date
    // silently: the attachment folder loses its prefix, and a date search
    // offers nothing with no indication why.
    const QDateTime withComment = MimeParser::parseDate(
        QStringLiteral("Thu, 14 Aug 2026 09:30:00 +0200 (CEST)"));
    QVERIFY(withComment.isValid());
    QCOMPARE(withComment.date(), QDate(2026, 8, 14));

    const QDateTime plain = MimeParser::parseDate(
        QStringLiteral("Thu, 14 Aug 2026 09:30:00 +0200"));
    QVERIFY(plain.isValid());
    QCOMPARE(plain.date(), QDate(2026, 8, 14));

    // Nothing usable is an invalid QDateTime, never a guess.
    QVERIFY(!MimeParser::parseDate(QStringLiteral("last Tuesday")).isValid());
    QVERIFY(!MimeParser::parseDate(QString()).isValid());
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build
```

Expected: build FAILS with `'parseDate' is not a member of 'MimeParser'`.

- [ ] **Step 3: Write the implementation**

Add to the `public:` section of `class MimeParser` in `src/mimeparser.h`, beside the other static helper:

```cpp
    /// Parses an RFC 2822 `Date:` header, returning an invalid QDateTime when
    /// nothing usable is there.
    ///
    /// **Strips comments before parsing**, because `Qt::RFC2822Date` rejects
    /// the entire string when a trailing timezone comment is present, and
    /// `... +0200 (CEST)` is legal per RFC 5322 and common in the wild
    /// (verified on Qt 6.11). A parser without this silently loses the date on
    /// a large share of real mail.
    static QDateTime parseDate(const QString &rfc822Date);
```

Add `#include <QDateTime>` to `src/mimeparser.h` if it is not already present.

In `src/mimeparser.cpp`, replace the parsing lines inside `attachmentFolderName()` with a call, and add the new definition. The function currently reads:

```cpp
    QString cleaned = rfc822Date;
    cleaned.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    cleaned = cleaned.trimmed();

    QString prefix;
    const QDateTime parsed = QDateTime::fromString(cleaned, Qt::RFC2822Date);
    if (parsed.isValid())
        prefix = parsed.toString(QStringLiteral("yyyy-MM-dd"));
```

Replace those lines with:

```cpp
    QString prefix;
    const QDateTime parsed = MimeParser::parseDate(rfc822Date);
    if (parsed.isValid())
        prefix = parsed.toString(QStringLiteral("yyyy-MM-dd"));
```

And add the definition, above `attachmentFolderName()` so it is declared before use:

```cpp
QDateTime MimeParser::parseDate(const QString &rfc822Date)
{
    // A trailing timezone comment, "... +0200 (CEST)", is legal per RFC 5322
    // and common in the wild, but Qt::RFC2822Date rejects the whole string
    // when one is present (verified on Qt 6.11). Strip comments before
    // parsing, or every such message silently loses its date.
    QString cleaned = rfc822Date;
    cleaned.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    cleaned = cleaned.trimmed();

    return QDateTime::fromString(cleaned, Qt::RFC2822Date);
}
```

Note that `attachmentFolderName()` is a file-local function in an anonymous namespace while this is a static member, so the definition must sit OUTSIDE that anonymous namespace. Place it after the namespace's closing brace.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R mimeparser --output-on-failure
```

Expected: PASS, including the existing attachment-folder tests, which now exercise the same code through the new entry point.

- [ ] **Step 5: Commit**

```bash
git add src/mimeparser.h src/mimeparser.cpp tests/test_mimeparser.cpp
git commit -S -m "refactor(mime): expose the Date: header parse as MimeParser::parseDate

The date search needs it and the logic already existed inside a file-local
function, including the fix for Qt::RFC2822Date rejecting a string that
carries a trailing timezone comment. Extracted rather than rewritten, so the
second caller cannot end up without that fix."
```

---

### Task 3: `TagStrip` hit-tests a chip and offers a menu

**Files:**
- Modify: `src/tagstrip.h`, `src/tagstrip.cpp`
- Test: `tests/test_tagstrip.cpp` (create)
- Modify: `tests/CMakeLists.txt`

The chip rects are computed in `relayout()` and again in `paintEvent()`. **One function must produce both**, or the drawn and clickable rects drift; `CardDelegate::expanderRectFor` exists for exactly this reason and is the pattern to copy.

- [ ] **Step 1: Write the failing test**

Create `tests/test_tagstrip.cpp`:

```cpp
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

#include "tagstrip.h"

/// The chip hit test.
///
/// Asserted on rects from chipRectAt(), which is the SAME function paintEvent
/// uses, so a drawn chip and a clickable chip cannot drift apart. Not asserted
/// by rendering: a pixel probe cannot tell a chip that is drawn from a chip
/// that is drawn and clickable, and CLAUDE.md records both halves of that
/// being broken independently in one session.
class TestTagStrip : public QObject
{
    Q_OBJECT
private slots:
    void chipAtFindsEachVisibleTag();
    void chipAtMissesTheGapAndTheEdges();
    void chipAtIgnoresTheOverflowChip();
};

void TestTagStrip::chipAtFindsEachVisibleTag()
{
    TagStrip strip;
    strip.resize(600, 30);
    strip.setTags({ QStringLiteral("inbox"), QStringLiteral("unread") });

    const QStringList visible = strip.visibleTags();
    QCOMPARE(visible.size(), 2);

    // The guard: the geometry this test depends on must exist before the test
    // can mean anything. A zero-width chip would make every lookup below miss
    // and the test would pass for the wrong reason.
    for (int i = 0; i < visible.size(); ++i) {
        const QRect rect = strip.chipRectAt(i);
        QVERIFY2(rect.width() > 0 && rect.height() > 0,
                 qPrintable(QStringLiteral("chip %1 has an empty rect").arg(i)));
        QCOMPARE(strip.chipAt(rect.center()), visible.at(i));
    }
}

void TestTagStrip::chipAtMissesTheGapAndTheEdges()
{
    TagStrip strip;
    strip.resize(600, 30);
    strip.setTags({ QStringLiteral("inbox"), QStringLiteral("unread") });
    QCOMPARE(strip.visibleTags().size(), 2);

    const QRect first = strip.chipRectAt(0);
    const QRect second = strip.chipRectAt(1);
    QVERIFY2(second.left() > first.right() + 1,
             "the two chips must not touch, or there is no gap to test");

    // Between the chips: no tag, so no menu entry rather than the nearest one.
    const QPoint gap((first.right() + second.left()) / 2, first.center().y());
    QVERIFY(strip.chipAt(gap).isEmpty());

    // Past the last chip, where the strip is empty space.
    QVERIFY(strip.chipAt(QPoint(strip.width() - 1, first.center().y())).isEmpty());
}

void TestTagStrip::chipAtIgnoresTheOverflowChip()
{
    // The +N chip stands for a LIST of tags, not for a tag, so there is no
    // single value a search could be built from.
    TagStrip strip;
    strip.resize(90, 30);
    strip.setTags({ QStringLiteral("inbox"), QStringLiteral("unread"),
                    QStringLiteral("flagged"), QStringLiteral("attachment"),
                    QStringLiteral("replied") });

    QVERIFY2(!strip.hiddenTags().isEmpty(),
             "the strip must actually overflow, or this asserts nothing");

    // Every point across the strip either finds a VISIBLE tag or nothing. The
    // overflow chip sits after the visible ones and must yield nothing.
    for (int x = 0; x < strip.width(); x += 3) {
        const QString found = strip.chipAt(QPoint(x, strip.height() / 2));
        if (!found.isEmpty())
            QVERIFY(strip.visibleTags().contains(found));
    }

    const QRect last = strip.chipRectAt(strip.visibleTags().size() - 1);
    QVERIFY(strip.chipAt(QPoint(last.right() + 5, strip.height() / 2)).isEmpty());
}

QTEST_MAIN(TestTagStrip)
#include "test_tagstrip.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
add_qtmaildir_test(tagstrip)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build
```

Expected: build FAILS with `'class TagStrip' has no member named 'chipRectAt'`.

- [ ] **Step 3: Write the implementation**

In `src/tagstrip.h`, add to the `public:` section after `hiddenTags()`:

```cpp
    /// The rect of the visible chip at `index`, empty when out of range.
    ///
    /// The SAME function paintEvent lays out from, so what is drawn and what
    /// is clickable cannot drift. `CardDelegate::expanderRectFor` exists for
    /// this reason and this follows it.
    QRect chipRectAt(int index) const;

    /// The tag under `point`, empty when the point is on no chip.
    ///
    /// The trailing "+N" chip yields an empty string: it stands for a list of
    /// tags rather than for one, so there is nothing a search could name.
    QString chipAt(const QPoint &point) const;

signals:
    /// A visible chip was right-clicked. `globalPos` is where to pop a menu.
    ///
    /// The strip does not build the menu itself: what a tag can do belongs to
    /// the window, which owns the query bar and the actions.
    void tagContextMenuRequested(const QString &tag, const QPoint &globalPos);
```

Add `#include <QRect>` to the header's includes.

In `src/tagstrip.cpp`, add the geometry function and the two new members. First, a shared layout helper placed just below the anonymous namespace's `overflowText()`:

```cpp
/// Left edge of the chip at `index`, given the metrics used to lay the row out.
///
/// Both paintEvent() and chipRectAt() walk the row with this, so a change to
/// the spacing moves the drawn chip and its hit area together.
int chipLeftEdge(const QFontMetrics &metrics, const QStringList &visible,
                 int index, int spacing, int startX)
{
    int x = startX;
    for (int i = 0; i < index && i < visible.size(); ++i)
        x += TagChip::sizeFor(metrics, visible.at(i)).width() + spacing;
    return x;
}
```

Then the public members, after `setTags()`:

```cpp
QRect TagStrip::chipRectAt(int index) const
{
    if (index < 0 || index >= m_visible.size())
        return {};

    const QFontMetrics metrics(font());
    const QSize size = TagChip::sizeFor(metrics, m_visible.at(index));
    const int x = chipLeftEdge(metrics, m_visible, index, kChipSpacing, 0);
    const int top = (height() - size.height()) / 2;
    return QRect(QPoint(x, top), size);
}

QString TagStrip::chipAt(const QPoint &point) const
{
    for (int i = 0; i < m_visible.size(); ++i) {
        if (chipRectAt(i).contains(point))
            return m_visible.at(i);
    }
    // Deliberately nothing for the overflow chip and for empty space.
    return {};
}

void TagStrip::contextMenuEvent(QContextMenuEvent *event)
{
    const QString tag = chipAt(event->pos());
    if (tag.isEmpty()) {
        // Not accepted, so the parent's own menu still has its chance.
        event->ignore();
        return;
    }

    event->accept();
    emit tagContextMenuRequested(tag, event->globalPos());
}
```

Declare the event handler in the header's `protected:` section, beside `paintEvent`:

```cpp
    void contextMenuEvent(QContextMenuEvent *event) override;
```

Add `#include <QContextMenuEvent>` to `src/tagstrip.cpp`.

**`kChipSpacing` and the starting x must match what `paintEvent` and `relayout` already use.** Read the existing `paintEvent` loop (`src/tagstrip.cpp:112-130`) and `relayout()` (`src/tagstrip.cpp:65-96`) and reuse the same constants: if the spacing is a literal in those loops, promote it to a named constant in the anonymous namespace and use it in all three places. Then rewrite the `paintEvent` loop to take each rect from `chipRectAt(i)` so there is exactly one source of geometry.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R tagstrip --output-on-failure
```

Expected: PASS, 3 test functions.

Then confirm nothing else moved:

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Expected: every test passing. The message pane's strip is rendered in other tests; a spacing constant changed in one place and not another shows up here.

- [ ] **Step 5: Commit**

```bash
git add src/tagstrip.h src/tagstrip.cpp tests/test_tagstrip.cpp tests/CMakeLists.txt
git commit -S -m "feat(tags): hit-test a chip in the tag strip

chipRectAt() is the single source of a chip's geometry, used by paintEvent
and by the hit test, so the drawn chip and the clickable chip cannot drift.

The +N chip yields nothing: it stands for a list of tags rather than one, so
there is no single value a search could be built from."
```

---

### Task 4: The header offers its fields

**Files:**
- Modify: `src/messageview.h`, `src/messageview.cpp:429-497`
- Test: `tests/test_messageview.cpp`

The header is one rich-text `QLabel` holding up to four rendered lines. **The menu lists the available fields rather than hit-testing which line was clicked**: mapping a point through laid-out rich text is fiddly and breaks the moment the label wraps. The searchable values are kept beside the label by the same pass that renders it, never parsed back out of the markup.

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` list in `tests/test_messageview.cpp`:

```cpp
    void headerOffersSubjectDateAndSenderForOneMessage();
    void headerOffersNoSenderForARealThread();
    void headerOffersNothingForAnAbsentField();
```

And the bodies, before `QTEST_MAIN`:

```cpp
void TestMessageView::headerOffersSubjectDateAndSenderForOneMessage()
{
    MessageView view;
    view.showThread({ oneMessage() });

    const QList<SearchOffer> offers = view.headerSearchOffers();

    QStringList queries;
    for (const SearchOffer &offer : offers)
        queries << offer.query;

    QVERIFY2(queries.contains(QStringLiteral("subject:\"Quarterly report\"")),
             qPrintable(queries.join(QStringLiteral(" | "))));
    QVERIFY2(queries.contains(
                 QStringLiteral("from:\"Sender <sender@example.org>\"")),
             qPrintable(queries.join(QStringLiteral(" | "))));
    QVERIFY2(queries.contains(
                 QStringLiteral("to:\"Recipient <recipient@example.org>\"")),
             qPrintable(queries.join(QStringLiteral(" | "))));
    QVERIFY2(queries.contains(QStringLiteral("cc:\"Copied <copied@example.org>\"")),
             qPrintable(queries.join(QStringLiteral(" | "))));

    // Every offer carries a label the menu shows, and it names the value so
    // the user can see what they are about to search for.
    for (const SearchOffer &offer : offers)
        QVERIFY(!offer.label.isEmpty());
}

void TestMessageView::headerOffersNoSenderForARealThread()
{
    // The header shows From/To/Cc only for a single-message thread, because a
    // thread's recipient differs message to message. The menu shares that
    // condition rather than restating it: it must never offer a value the
    // header is not stating.
    ThreadRenderItem first = oneMessage();
    ThreadRenderItem second = oneMessage();
    second.message.from = QStringLiteral("Recipient <recipient@example.org>");
    second.message.to = QStringLiteral("Sender <sender@example.org>");

    MessageView view;
    view.showThread({ first, second });

    QStringList queries;
    for (const SearchOffer &offer : view.headerSearchOffers())
        queries << offer.query;

    // THE GUARD. A test asserting only that something is absent passes against
    // no implementation whatever, which item 82 records having shipped once.
    // Subject and date must still be offered, proving the list was built.
    QVERIFY2(!queries.isEmpty(), "no offers at all: the list was never built");
    QVERIFY(queries.contains(QStringLiteral("subject:\"Quarterly report\"")));

    for (const QString &query : queries) {
        QVERIFY2(!query.startsWith(QStringLiteral("from:")),
                 qPrintable(QStringLiteral("thread offered %1").arg(query)));
        QVERIFY2(!query.startsWith(QStringLiteral("to:")),
                 qPrintable(QStringLiteral("thread offered %1").arg(query)));
        QVERIFY2(!query.startsWith(QStringLiteral("cc:")),
                 qPrintable(QStringLiteral("thread offered %1").arg(query)));
    }
}

void TestMessageView::headerOffersNothingForAnAbsentField()
{
    // cc:"" parses cleanly and matches nothing, so an entry built from an
    // empty header would look enabled and silently do nothing.
    ThreadRenderItem item = oneMessage();
    item.message.cc.clear();

    MessageView view;
    view.showThread({ item });

    for (const SearchOffer &offer : view.headerSearchOffers())
        QVERIFY(!offer.query.startsWith(QStringLiteral("cc:")));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build
```

Expected: build FAILS with `'SearchOffer' was not declared in this scope`.

- [ ] **Step 3: Write the implementation**

Create the shared value type. Add to `src/searchterm.h`, after the namespace:

```cpp
/// One entry a context menu can offer: a finished query and the text naming it.
///
/// Carried rather than rebuilt at menu-construction time, so the value a menu
/// entry searches for is the value the pane extracted, with no second parse of
/// anything already rendered.
struct SearchOffer
{
    /// Shown in the menu. Already translated, and elides a long value: the
    /// query keeps the full one.
    QString label;

    /// The finished notmuch query. Never empty in a constructed offer.
    QString query;
};
```

Add `#include <QList>` to `src/searchterm.h`.

In `src/messageview.h`, add `#include "searchterm.h"`, then to the `public:` section:

```cpp
    /// What the header can be searched for, given what it is currently showing.
    ///
    /// From, To and Cc appear only for a single-message thread, which is
    /// exactly when the header displays them: for a real thread the recipient
    /// differs message to message and the header says only the subject and the
    /// count. The menu must never offer a value the header is not stating.
    ///
    /// Public for the tests and for the window; the values come from the same
    /// pass that renders the label, never from parsing it back.
    QList<SearchOffer> headerSearchOffers() const { return m_headerOffers; }
```

Add to the `signals:` section:

```cpp
    /// The user chose a search from one of the pane's context menus.
    ///
    /// `extend` narrows the current query rather than replacing it. The view
    /// does not know what the query bar holds and must not: the window owns
    /// that field and does the combining.
    void searchRequested(const QString &query, bool extend);
```

Add to the `private:` section:

```cpp
    /// Populated by updateHeader(), consumed by the header's context menu.
    QList<SearchOffer> m_headerOffers;

    /// Builds and pops the header's menu at `pos`, in the label's coordinates.
    void showHeaderContextMenu(const QPoint &pos);

    /// Appends "Search for this" and "Add to search" for every offer.
    ///
    /// Shared by the header and the web view so the two menus cannot grow
    /// different wording or a different pair of operations.
    void addSearchEntries(QMenu *menu, const QList<SearchOffer> &offers);
```

In `src/messageview.cpp`, add `#include <QMenu>` and `#include "searchterm.h"`.

In the constructor, after the header label is created (around line 164), enable its menu:

```cpp
    m_headerLabel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_headerLabel, &QWidget::customContextMenuRequested,
            this, &MessageView::showHeaderContextMenu);
```

In `updateHeader()`, populate the offers alongside the rendered text. Add near the top of the function, after the empty check:

```cpp
    m_headerOffers.clear();
```

After `const QString subject = m_items.first().message.subject;`, add:

```cpp
    // Built here rather than at menu time, from the same values the label is
    // about to render: a second parse of the rendered markup is what this
    // avoids, and rich text does not survive one.
    auto offer = [this](const QString &label, const QString &query) {
        if (query.isEmpty())
            return;
        m_headerOffers.append({ label, query });
    };

    auto elided = [](const QString &value) {
        constexpr int kMaxLabel = 40;
        return value.size() > kMaxLabel
                   ? value.left(kMaxLabel) + QStringLiteral("...")
                   : value;
    };

    offer(tr("subject \"%1\"").arg(elided(subject)),
          SearchTerm::field(QStringLiteral("subject"), subject));

    const QDateTime sent = MimeParser::parseDate(m_items.first().message.date);
    if (sent.isValid()) {
        offer(tr("mail from %1").arg(sent.date().toString(Qt::ISODate)),
              SearchTerm::onDate(sent.date()));
    }
```

Inside the existing `if (m_items.size() == 1)` branch, after the three `row(...)` calls, add:

```cpp
        // Only here, sharing the condition with the header's own display: for
        // a real thread these differ message to message and the details dialog
        // is where they are unambiguous.
        offer(tr("sender %1").arg(elided(message.from)),
              SearchTerm::field(QStringLiteral("from"), message.from));
        offer(tr("recipient %1").arg(elided(message.to)),
              SearchTerm::field(QStringLiteral("to"), message.to));
        offer(tr("copied to %1").arg(elided(message.cc)),
              SearchTerm::field(QStringLiteral("cc"), message.cc));
```

Add `#include "mimeparser.h"` to `src/messageview.cpp` if it is not already there.

Then the two new functions, placed after `updateHeader()`:

```cpp
void MessageView::addSearchEntries(QMenu *menu, const QList<SearchOffer> &offers)
{
    if (offers.isEmpty())
        return;

    for (const SearchOffer &entry : offers) {
        auto *sub = menu->addMenu(tr("Search for %1").arg(entry.label));

        auto *replace = sub->addAction(tr("Search for this"));
        connect(replace, &QAction::triggered, this,
                [this, entry]() { emit searchRequested(entry.query, false); });

        auto *extend = sub->addAction(tr("Add to search"));
        connect(extend, &QAction::triggered, this,
                [this, entry]() { emit searchRequested(entry.query, true); });
    }
}

void MessageView::showHeaderContextMenu(const QPoint &pos)
{
    if (m_headerOffers.isEmpty())
        return;

    QMenu menu(this);
    addSearchEntries(&menu, m_headerOffers);
    menu.exec(m_headerLabel->mapToGlobal(pos));
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R messageview --output-on-failure
```

Expected: PASS, including the three new functions and every existing header test.

- [ ] **Step 5: Commit**

```bash
git add src/messageview.h src/messageview.cpp src/searchterm.h tests/test_messageview.cpp
git commit -S -m "feat(search): offer the header's fields for searching

The menu lists what is searchable rather than hit-testing which line of a
rich-text label was clicked, which breaks as soon as the label wraps. The
values are collected by the pass that renders the header, so nothing parses
the markup back into structure.

From, To and Cc appear only for a single-message thread, sharing the
condition with the header's own display: a thread's recipient differs message
to message, and the menu must not offer what the header is not stating."
```

---

### Task 5: The body selection is searchable

**Files:**
- Modify: `src/messageview.h`, `src/messageview.cpp`
- Test: `tests/test_messageview.cpp`

`QWebEnginePage::selectedText()` reads the selection with **no script injection**; JavaScript stays disabled in the profile and this task must not change that.

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` list in `tests/test_messageview.cpp`:

```cpp
    void bodySelectionBecomesAQuotedSearch();
```

And the body:

```cpp
void TestMessageView::bodySelectionBecomesAQuotedSearch()
{
    // The selection reaches the query as ONE quoted term. Asserted on the
    // constructed string: a query that lost its quoting is not an error to
    // notmuch, it simply matches nothing, so nothing downstream would report
    // this being wrong.
    MessageView view;

    QCOMPARE(view.selectionSearchOffer(QStringLiteral("invoice 4471")).query,
             QStringLiteral("\"invoice 4471\""));

    // A selection spanning paragraphs arrives full of newlines.
    QCOMPARE(view.selectionSearchOffer(
                 QStringLiteral("first line\n\nsecond line")).query,
             QStringLiteral("\"first line second line\""));

    // Nothing selected means no entry, rather than an entry searching for "".
    QVERIFY(view.selectionSearchOffer(QString()).query.isEmpty());
    QVERIFY(view.selectionSearchOffer(QStringLiteral("  \n ")).query.isEmpty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build
```

Expected: build FAILS with `'class MessageView' has no member named 'selectionSearchOffer'`.

- [ ] **Step 3: Write the implementation**

In `src/messageview.h`, add to `public:`:

```cpp
    /// The offer for a body selection, empty-queried when there is nothing
    /// usable selected.
    ///
    /// Takes the text rather than reading the page, so the quoting is testable
    /// without a live web engine and a rendered document.
    SearchOffer selectionSearchOffer(const QString &selectedText) const;
```

And to `private:`:

```cpp
    /// Builds and pops the web view's menu, keeping its standard entries.
    void showBodyContextMenu(const QPoint &pos);
```

In `src/messageview.cpp`, add the implementation after `showHeaderContextMenu()`:

```cpp
SearchOffer MessageView::selectionSearchOffer(const QString &selectedText) const
{
    const QString query = SearchTerm::quote(selectedText);
    if (query.isEmpty())
        return {};

    constexpr int kMaxLabel = 40;
    const QString shown = selectedText.simplified();
    return { tr("\"%1\"").arg(shown.size() > kMaxLabel
                                  ? shown.left(kMaxLabel) + QStringLiteral("...")
                                  : shown),
             query };
}

void MessageView::showBodyContextMenu(const QPoint &pos)
{
    // The page's own menu first: copy, select all and the rest stay exactly as
    // they were. This adds to it rather than replacing it.
    QMenu *menu = m_view->createStandardContextMenu();
    if (!menu)
        menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // selectedText() reads the selection out of the render process with no
    // script injection. JavaScript is disabled in this profile and stays so.
    const SearchOffer offer = selectionSearchOffer(m_view->page()->selectedText());
    if (!offer.query.isEmpty()) {
        menu->addSeparator();
        addSearchEntries(menu, { offer });
    }

    menu->popup(m_view->mapToGlobal(pos));
}
```

In the constructor, after `m_view` is created (around line 135):

```cpp
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested,
            this, &MessageView::showBodyContextMenu);
```

Add `#include <QWebEnginePage>` to `src/messageview.cpp` if it is not already present.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R messageview --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/messageview.h src/messageview.cpp tests/test_messageview.cpp
git commit -S -m "feat(search): search for the selected body text

selectedText() reads the selection with no script injection; JavaScript stays
disabled in the profile. The page's standard menu is kept and the entry added
to it.

The quoting is tested through a function taking the text, so it needs no live
web engine: a selection is arbitrary prose and can carry quotes, newlines and
query syntax, none of which notmuch reports as an error."
```

---

### Task 6: The details dialog becomes rows

**Files:**
- Create: `src/messagedetailsdialog.h`, `src/messagedetailsdialog.cpp`
- Create: `tests/test_messagedetailsdialog.cpp`
- Modify: `src/messageview.h`, `src/messageview.cpp:500-552`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

The current dialog is a `QPlainTextEdit` built inline. **The reason it is plain text is security, not style**: header values come from strangers, and plain text cannot interpret markup, so there is nothing to escape and nothing that can render. Row widgets bring that risk back, because a `QLabel` interprets rich text under `Qt::AutoText`. Every value label is therefore explicitly `Qt::PlainText`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_messagedetailsdialog.cpp`:

```cpp
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

#include <QLabel>
#include <QSignalSpy>
#include <QtTest>

#include "htmlbuilder.h"
#include "messagedetailsdialog.h"

class TestMessageDetailsDialog : public QObject
{
    Q_OBJECT
private slots:
    void showsEveryHeaderOfEveryMessage();
    void valueLabelsCannotRenderMarkup();
    void offersASearchForEachValue();
    void omitsAnEmptyHeader();

private:
    ThreadRenderItem oneMessage() const
    {
        ThreadRenderItem item;
        item.message.ok = true;
        item.message.subject = QStringLiteral("Quarterly report");
        item.message.from = QStringLiteral("Sender <sender@example.org>");
        item.message.to = QStringLiteral("Recipient <recipient@example.org>");
        item.message.cc = QStringLiteral("Copied <copied@example.org>");
        item.message.date = QStringLiteral("Thu, 14 Aug 2026 09:30:00 +0200");
        item.message.messageId = QStringLiteral("<abc123@example.org>");
        return item;
    }
};

void TestMessageDetailsDialog::showsEveryHeaderOfEveryMessage()
{
    ThreadRenderItem second = oneMessage();
    second.message.subject = QStringLiteral("Re: Quarterly report");

    MessageDetailsDialog dialog({ oneMessage(), second });

    const QList<HeaderRow> rows = dialog.rows();
    QVERIFY2(!rows.isEmpty(), "no rows: the dialog was never populated");

    // Both messages are represented, each row knowing which one it belongs to.
    QVERIFY(std::any_of(rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
        return row.messageIndex == 0;
    }));
    QVERIFY(std::any_of(rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
        return row.messageIndex == 1;
    }));

    QStringList values;
    for (const HeaderRow &row : rows)
        values << row.value;
    QVERIFY(values.contains(QStringLiteral("Sender <sender@example.org>")));
    QVERIFY(values.contains(QStringLiteral("Re: Quarterly report")));
    QVERIFY(values.contains(QStringLiteral("<abc123@example.org>")));
}

void TestMessageDetailsDialog::valueLabelsCannotRenderMarkup()
{
    // The dialog it replaced used a QPlainTextEdit deliberately: header values
    // are attacker-controlled and plain text cannot interpret markup. A QLabel
    // guesses under Qt::AutoText, so every value label states PlainText.
    ThreadRenderItem hostile = oneMessage();
    hostile.message.subject =
        QStringLiteral("<b>bold</b><img src=x onerror=1>");

    MessageDetailsDialog dialog({ hostile });

    const QList<QLabel *> labels = dialog.findChildren<QLabel *>();
    QVERIFY2(!labels.isEmpty(), "no labels: the dialog was never populated");

    bool sawTheSubject = false;
    for (const QLabel *label : labels) {
        QCOMPARE(label->textFormat(), Qt::PlainText);
        if (label->text().contains(QStringLiteral("<b>bold</b>")))
            sawTheSubject = true;
    }

    // The markup survives as TEXT, which is the proof it was not interpreted.
    QVERIFY2(sawTheSubject, "the hostile subject never reached a label");
}

void TestMessageDetailsDialog::offersASearchForEachValue()
{
    MessageDetailsDialog dialog({ oneMessage() });

    QSignalSpy spy(&dialog, &MessageDetailsDialog::searchRequested);
    QVERIFY(spy.isValid());

    const QList<HeaderRow> rows = dialog.rows();
    const auto from = std::find_if(
        rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
            return row.field == QStringLiteral("from");
        });
    QVERIFY2(from != rows.cend(), "no From row to search from");
    QCOMPARE(from->query, QStringLiteral("from:\"Sender <sender@example.org>\""));

    // Replacing and narrowing are both offered, and the flag distinguishes them.
    dialog.requestSearch(*from, false);
    dialog.requestSearch(*from, true);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), from->query);
    QCOMPARE(spy.at(0).at(1).toBool(), false);
    QCOMPARE(spy.at(1).at(1).toBool(), true);
}

void TestMessageDetailsDialog::omitsAnEmptyHeader()
{
    ThreadRenderItem noCc = oneMessage();
    noCc.message.cc.clear();

    MessageDetailsDialog dialog({ noCc });

    for (const HeaderRow &row : dialog.rows())
        QVERIFY(row.field != QStringLiteral("cc"));
}

QTEST_MAIN(TestMessageDetailsDialog)
#include "test_messagedetailsdialog.moc"
```

Add to `tests/CMakeLists.txt`:

```cmake
add_qtmaildir_test(messagedetailsdialog)
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build
```

Expected: build FAILS with `messagedetailsdialog.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `src/messagedetailsdialog.h`:

```cpp
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

#include <QDialog>
#include <QList>
#include <QString>

#include "htmlbuilder.h"

/// One header of one message, as shown and as searched for.
///
/// The query is built when the row is, from the parsed value, so the menu
/// never parses displayed text back into structure. That is the whole reason
/// this dialog stopped being a text box.
struct HeaderRow
{
    /// notmuch's field name, or empty for a header with no searchable form
    /// (Message-Id is shown but is not offered as a search).
    QString field;

    /// Translated label shown at the start of the row, e.g. "From:".
    QString label;

    /// The header's value, verbatim and untrusted.
    QString value;

    /// The finished query, empty when the header has no searchable form.
    QString query;

    /// Which message of the thread this row belongs to, zero-based.
    int messageIndex = 0;
};

/// The full headers of every message in a thread, read-only.
///
/// Rows rather than one text box, so a value can carry its own context menu
/// without anything parsing rendered text back into structure.
///
/// **Every value label is explicitly `Qt::PlainText`.** This replaced a
/// `QPlainTextEdit` whose plain-textness was a security property rather than a
/// style: header values come from strangers, and plain text cannot interpret
/// markup, so there is nothing to escape and nothing that can render. A QLabel
/// guesses under `Qt::AutoText`, so stating the format is what preserves that.
class MessageDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MessageDetailsDialog(const QList<ThreadRenderItem> &items,
                                  QWidget *parent = nullptr);

    /// The rows on display, in order. Exposed for testing without rendering.
    QList<HeaderRow> rows() const { return m_rows; }

    /// Emits searchRequested for `row`. The menu entries call this; a test can
    /// too, without popping a menu.
    void requestSearch(const HeaderRow &row, bool extend);

signals:
    /// The user chose a search from a row's menu. `extend` narrows the current
    /// query rather than replacing it.
    void searchRequested(const QString &query, bool extend);

private:
    /// Builds the rows from the thread, one group per message.
    void buildRows(const QList<ThreadRenderItem> &items);

    QList<HeaderRow> m_rows;
};
```

Create `src/messagedetailsdialog.cpp`:

```cpp
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

#include "messagedetailsdialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QVBoxLayout>

#include "searchterm.h"

MessageDetailsDialog::MessageDetailsDialog(const QList<ThreadRenderItem> &items,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Message details"));
    setObjectName(QStringLiteral("messageDetailsDialog"));

    buildRows(items);

    auto *layout = new QVBoxLayout(this);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *grid = new QGridLayout(content);

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    int gridRow = 0;
    int lastMessage = -1;
    for (const HeaderRow &row : std::as_const(m_rows)) {
        if (row.messageIndex != lastMessage) {
            lastMessage = row.messageIndex;
            if (items.size() > 1) {
                auto *heading = new QLabel(
                    tr("--- Message %1 of %2 ---")
                        .arg(row.messageIndex + 1).arg(items.size()),
                    content);
                heading->setTextFormat(Qt::PlainText);
                grid->addWidget(heading, gridRow++, 0, 1, 2);
            }
        }

        auto *label = new QLabel(row.label, content);
        label->setTextFormat(Qt::PlainText);
        label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        grid->addWidget(label, gridRow, 0);

        // PlainText stated, not inferred. A QLabel guesses under AutoText, and
        // this value came from a stranger.
        auto *value = new QLabel(row.value, content);
        value->setTextFormat(Qt::PlainText);
        value->setFont(fixed);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);

        if (!row.query.isEmpty()) {
            value->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(value, &QWidget::customContextMenuRequested, this,
                    [this, value, row](const QPoint &pos) {
                        QMenu menu(this);
                        auto *replace = menu.addAction(tr("Search for this"));
                        connect(replace, &QAction::triggered, this,
                                [this, row]() { requestSearch(row, false); });
                        auto *extend = menu.addAction(tr("Add to search"));
                        connect(extend, &QAction::triggered, this,
                                [this, row]() { requestSearch(row, true); });
                        menu.exec(value->mapToGlobal(pos));
                    });
        }

        grid->addWidget(value, gridRow, 1);
        ++gridRow;
    }

    grid->setColumnStretch(1, 1);
    grid->setRowStretch(gridRow, 1);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(700, 400);
}

void MessageDetailsDialog::buildRows(const QList<ThreadRenderItem> &items)
{
    for (int i = 0; i < items.size(); ++i) {
        const ParsedMessage &message = items.at(i).message;

        auto add = [this, i](const QString &field, const QString &label,
                             const QString &value, const QString &query) {
            if (value.isEmpty())
                return;   // An empty row reads as a rendering fault.
            m_rows.append({ field, label, value, query, i });
        };

        add(QStringLiteral("subject"), tr("Subject:"), message.subject,
            SearchTerm::field(QStringLiteral("subject"), message.subject));
        add(QStringLiteral("from"), tr("From:"), message.from,
            SearchTerm::field(QStringLiteral("from"), message.from));
        add(QStringLiteral("to"), tr("To:"), message.to,
            SearchTerm::field(QStringLiteral("to"), message.to));
        add(QStringLiteral("cc"), tr("Cc:"), message.cc,
            SearchTerm::field(QStringLiteral("cc"), message.cc));

        const QDateTime sent = MimeParser::parseDate(message.date);
        add(QStringLiteral("date"), tr("Date:"), message.date,
            sent.isValid() ? SearchTerm::onDate(sent.date()) : QString());

        // Shown but not searchable: a message id names one message, and the
        // thread it belongs to is already on screen.
        add(QString(), tr("Message-Id:"), message.messageId, QString());
    }
}

void MessageDetailsDialog::requestSearch(const HeaderRow &row, bool extend)
{
    if (row.query.isEmpty())
        return;
    emit searchRequested(row.query, extend);
}
```

Add `#include "mimeparser.h"` to `src/messagedetailsdialog.cpp`.

Add to the `qtmaildir_lib` list in `src/CMakeLists.txt`, in alphabetical order:

```cmake
    messagedetailsdialog.cpp
```

Now replace `MessageView::showDetailsDialog()` in `src/messageview.cpp` with a delegation:

```cpp
void MessageView::showDetailsDialog()
{
    if (m_items.isEmpty())
        return;

    MessageDetailsDialog dialog(m_items, this);
    // The dialog's searches are the pane's searches: one signal reaches the
    // window whichever surface the user used.
    connect(&dialog, &MessageDetailsDialog::searchRequested,
            this, &MessageView::searchRequested);
    dialog.exec();
}
```

Add `#include "messagedetailsdialog.h"` to `src/messageview.cpp`, and remove the now-unused `#include <QPlainTextEdit>` if nothing else in the file uses it.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R "messagedetailsdialog|messageview" --output-on-failure
```

Expected: both binaries pass. `detailsDialogIsOfferedForEveryThread` in `test_messageview.cpp` must still pass; if it asserted on a `QPlainTextEdit`, update it to assert the dialog appears rather than reverting this change.

- [ ] **Step 5: Commit**

```bash
git add src/messagedetailsdialog.h src/messagedetailsdialog.cpp src/messageview.h src/messageview.cpp src/CMakeLists.txt tests/test_messagedetailsdialog.cpp tests/CMakeLists.txt
git commit -S -m "feat(details): rebuild the message details dialog as rows

A text box could not carry a per-value context menu without parsing displayed
text back into structure, and the user did not want a text box. Each row now
holds its own value, its message index and its query, built from the parsed
message.

Every value label states Qt::PlainText. The QPlainTextEdit this replaced was
plain by design rather than by style: header values come from strangers, and
a QLabel guesses the format under AutoText."
```

---

### Task 7: `MainWindow` runs the search

**Files:**
- Modify: `src/mainwindow.h`, `src/mainwindow.cpp`
- Test: `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` list in `tests/test_mainwindow.cpp`:

```cpp
    void aSearchFromThePaneReplacesTheQuery();
    void aSearchFromThePaneCanNarrowTheQuery();
```

And the bodies. Follow the file's existing pattern for building a window; the fixture and helper names below match what the rest of the file uses.

```cpp
void TestMainWindow::aSearchFromThePaneReplacesTheQuery()
{
    MainWindow window;
    QLineEdit *queryEdit = window.findChild<QLineEdit *>(
        QStringLiteral("queryEdit"));
    QVERIFY2(queryEdit, "no query bar: the window was never built");

    queryEdit->setText(QStringLiteral("tag:inbox"));

    MessageView *view = window.findChild<MessageView *>();
    QVERIFY2(view, "no message view");

    emit view->searchRequested(QStringLiteral("from:\"foo@example.org\""), false);

    QCOMPARE(queryEdit->text(), QStringLiteral("from:\"foo@example.org\""));
}

void TestMainWindow::aSearchFromThePaneCanNarrowTheQuery()
{
    // The case the feature exists for: a query returning a thousand threads is
    // narrowed by adding a condition. Both sides are parenthesised, because
    // 'a or b AND c' binds as 'a or (b AND c)' and would WIDEN the search.
    MainWindow window;
    QLineEdit *queryEdit = window.findChild<QLineEdit *>(
        QStringLiteral("queryEdit"));
    QVERIFY2(queryEdit, "no query bar: the window was never built");

    queryEdit->setText(QStringLiteral("tag:inbox or tag:flagged"));

    MessageView *view = window.findChild<MessageView *>();
    QVERIFY2(view, "no message view");

    emit view->searchRequested(QStringLiteral("from:\"foo@example.org\""), true);

    QCOMPARE(queryEdit->text(),
             QStringLiteral("(tag:inbox or tag:flagged) AND (from:\"foo@example.org\")"));
}
```

**Check the query bar's real object name** before running: grep for `setObjectName` near `m_queryEdit` in `src/mainwindow.cpp` and use whatever is there. If it has none, add `m_queryEdit->setObjectName(QStringLiteral("queryEdit"));` as part of this task.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R mainwindow --output-on-failure
```

Expected: FAIL. Either a compile error on the missing signal, or the query bar keeps its old text.

If the run HANGS rather than failing, that is item 84: kill the process, rebuild, and check the fixture's config before theorising.

```bash
pkill -f test_mainwindow
```

- [ ] **Step 3: Write the implementation**

In `src/mainwindow.h`, add to the `private:` section:

```cpp
    /// Runs a search asked for from the message pane.
    ///
    /// `extend` narrows the current query rather than replacing it. The panes
    /// carry a finished query and no knowledge of the bar; the combining
    /// happens here, because only the window can see what the bar holds.
    void runSearchFromPane(const QString &query, bool extend);
```

In `src/mainwindow.cpp`, add `#include "searchterm.h"`, and connect where the message view's other signals are connected (search for `&MessageView::queryRequested` to find the block):

```cpp
    connect(m_messageView, &MessageView::searchRequested,
            this, &MainWindow::runSearchFromPane);
```

Add the tag strip's connection in the same block. The strip is `MessageView`'s child, so route it through the view rather than reaching into it: in `MessageView`'s constructor, after the strip is created, add

```cpp
    connect(m_tagStrip, &TagStrip::tagContextMenuRequested, this,
            [this](const QString &tag, const QPoint &globalPos) {
                const QString query = SearchTerm::tag(tag);
                if (query.isEmpty())
                    return;

                QMenu menu(this);
                addSearchEntries(&menu, { { tr("tag %1").arg(tag), query } });
                menu.exec(globalPos);
            });
```

Then the implementation in `src/mainwindow.cpp`, placed near `runQuery()`:

```cpp
void MainWindow::runSearchFromPane(const QString &query, bool extend)
{
    if (query.isEmpty())
        return;

    const QString next =
        extend ? SearchTerm::extend(m_queryEdit->text(), query) : query;

    m_queryEdit->setText(next);

    // The existing runner, so the account scope, the generation counter and
    // the flat-mode reset all keep working exactly as they do for a typed
    // query. Nothing here builds a second query path.
    runCurrentQuery();
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R mainwindow --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp src/messageview.cpp tests/test_mainwindow.cpp
git commit -S -m "feat(search): run a search asked for from the message pane

The panes carry a finished query and know nothing of the query bar; the
window sets the field and calls the existing runner, so the account scope and
the generation counter keep working as they do for a typed query.

Narrowing combines here rather than in a pane, because only the window can
see what the bar currently holds."
```

---

### Task 8: Whole-suite check, changelog, close the item

**Files:**
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md`
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability-closed.md`

- [ ] **Step 1: Run the whole suite**

```bash
pkill -f test_mainwindow; cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Expected: every test passing. The `pkill` is not superstition: item 84 records that a hung `test_mainwindow` leaves a stale binary that a later `ctest` then runs, so a fixed failure appears to persist.

- [ ] **Step 2: Hand the build to the user for a hand test**

This feature is menus and a web view selection, and CLAUDE.md is explicit that running the application belongs to the user. Do not launch it. Report what to look at:

- Right-click the message pane header on a single-message thread: subject, date, sender, recipient and copied-to are all offered, each with **Search for this** and **Add to search**.
- The same on a thread of several messages: subject and date only.
- Right-click a tag chip under the message: that tag is offered. Right-click between chips: no search entry.
- Select a phrase in the body and right-click it: the standard copy entries are still there, with the search entries below a separator.
- The details button opens rows rather than a text box, and each value has its own menu.
- **Add to search** on a bar already holding `tag:inbox or tag:flagged` produces the parenthesised form.

- [ ] **Step 3: Write the changelog entry**

Add under `## [Unreleased]` in `CHANGELOG.md`:

```markdown
### Added

- Right-clicking a value in the message pane offers to search for it. The
  header's subject and date, its sender and recipients on a single-message
  thread, a tag chip, a selection in the body, and every header of every
  message in the details dialog. Each offers **Search for this**, which
  replaces the query, and **Add to search**, which narrows it.

### Changed

- The message details dialog shows labelled rows rather than one block of
  text, so each value can be searched for on its own.
```

- [ ] **Step 4: Close item 85 in the backlog**

In `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md`, change item 85's status cell to:

```
| 85 | Nothing on screen can be searched for by right-clicking it | workflow | M | **done** 2026-08-14, unreleased; see `specs/2026-08-14-search-from-message-design.md` |
```

Then **move item 85's whole section** into `2026-08-03-post-0.1.0-usability-closed.md`, leaving only the table row behind. CLAUDE.md is explicit that this happens on the commit that closes the item, not in a later cleanup: that is how the backlog reached five thousand lines the first time.

- [ ] **Step 5: Commit**

```bash
git add CHANGELOG.md docs/superpowers/plans/
git commit -S -m "docs: close item 85, searching from the message pane

Five surfaces in the message pane offer a search built from what they are
showing, replacing the query or narrowing it. The details dialog became rows
along the way, which the user wanted independently.

Item 78 stays open carrying the rule shortcut alone: the road from a search
to a rule already exists as save the query, then build a rule from it."
```

---

## Notes for the implementer

**Where this is likely to go wrong, in order of probability:**

1. **The tag strip's spacing constant.** Task 3 requires `paintEvent`, `relayout` and `chipRectAt` to agree. If the whole-suite run in Task 3 shows a message-pane test failing, the constants disagree; fix by making all three call `chipRectAt`.
2. **The query bar's object name.** Task 7's tests find it by name. Confirm the real one before assuming.
3. **`createStandardContextMenu()` can return null** on a page with nothing to offer. Task 5 handles it; do not simplify that away.
4. **`MessageDetailsDialog` is constructed on the stack in `showDetailsDialog()`** and connected before `exec()`. Connecting after `exec()` returns would connect to a dialog that has already closed.

**What must not change:**

- JavaScript stays disabled in the web engine profile. `selectedText()` needs no script.
- The interceptor's fail-closed default and the exact-URL document exemption.
- Value labels in the details dialog stay `Qt::PlainText`.
- No confirmation dialogs, no dry runs. This feature mutates nothing.
