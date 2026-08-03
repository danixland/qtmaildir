# Query Bar Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the query bar complete notmuch prefixes, tag names, dates, paths and mimetypes with inline descriptions, so the bar teaches the query language while it is typed.

**Architecture:** A new `QueryCompleter` class owns a `QCompleter` installed on the existing `QLineEdit`, swapping its model as the cursor moves between contexts. A pure `completionContext()` function decides the context from the text and cursor offset, so the parsing rules are testable without a widget or a database. `NotmuchWorker` gains an all-tags call following the existing generation-counter pattern.

**Tech Stack:** C++17, Qt 6.11 (Widgets), libnotmuch, QTest.

**Spec:** `docs/superpowers/specs/2026-08-03-query-completion-design.md`

---

## Background for the implementer

Read these before starting. They are project rules that this plan depends on.

- **Every code block in a plan document in this repo is a draft, not verified
  code.** Nine defects were found in the previous plan's code blocks. Run the
  tests; do not assume a block compiles.
- **`NmTags` already exists** in `src/nmraii.h`. The spec says it must be added;
  that is wrong. Do not re-add it.
- **`[general]` keys are read WITHOUT the `general/` prefix.** QSettings' INI
  backend treats a section literally named `[general]` as its own fallback
  section and strips it. `settings.value("completion_on_focus")` is correct;
  `settings.value("general/completion_on_focus")` silently matches nothing.
  Other sections DO take the prefix: `settings.value("sync/command")`.
- **No `notmuch_*` pointer crosses the thread boundary.** Data crosses as plain
  value types over queued signals.
- **gmime headers before Qt headers** in any translation unit including both.
  Not relevant to this plan's files, but it is why include order looks odd
  elsewhere.
- Build: `cmake --build build`. Test one binary: `ctest --test-dir build -R <name>`.
- Commits are GPG-signed: `git commit -S`. Never disable signing.

## File Structure

**Created:**
- `src/completionentry.h` — the `CompletionEntry` value type alone. It has its
  own header because both `Config` and `QueryCompleter` need it, and
  `QueryCompleter` needs `Config`: putting it in `querycompleter.h` would make
  the two headers include each other.
- `src/querycompleter.h` — the `CompletionContext` struct, the
  `completionContext()` free function declaration, and the `QueryCompleter`
  class.
- `src/querycompleter.cpp` — tokenizer, vocabulary tables, model construction,
  the description delegate, and the popup footer.
- `tests/test_querycompleter.cpp` — tokenizer rules and model selection.

**Modified:**
- `src/notmuchworker.h` / `.cpp` — `requestAllTags` slot, `allTagsReady` signal.
- `src/config.h` / `.cpp` — `completionOnFocus()` and `extraMimetypes()`.
- `src/keymap.cpp` — the `complete_query` default binding.
- `src/mainwindow.h` / `.cpp` — construct `QueryCompleter`, wire tag refresh.
- `src/CMakeLists.txt` — add `querycompleter.cpp` to `qtmaildir_lib`.
- `tests/CMakeLists.txt` — `add_qtmaildir_test(querycompleter)`.
- `tests/test_notmuchworker.cpp` — cover `requestAllTags`.
- `tests/test_config.cpp` — cover the two new accessors.

The tokenizer, the vocabulary and the widget wiring live in one class because
they change together: adding a prefix means adding its model and its trigger in
the same edit. Splitting them across files would spread one change over three.

---

## Task 1: The completion context struct and prefix tokenizing

**Files:**
- Create: `src/querycompleter.h`
- Create: `src/querycompleter.cpp`
- Create: `tests/test_querycompleter.cpp`
- Modify: `src/CMakeLists.txt:16`
- Modify: `tests/CMakeLists.txt:20`

- [ ] **Step 1: Write the failing test**

Create `tests/test_querycompleter.cpp`:

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
```

Add to `tests/CMakeLists.txt` after line 20 (`add_qtmaildir_test(messageview)`):

```cmake
add_qtmaildir_test(querycompleter)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build 2>&1 | tail -20`
Expected: FAIL, `querycompleter.h: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `src/querycompleter.h` (GPL header as above, then):

```cpp
#pragma once

#include <QString>

/// Where the cursor sits in a query, and therefore what should be offered.
///
/// A plain value type produced by a pure function so the parsing rules can be
/// tested without a widget or a database.
struct CompletionContext
{
    enum Kind {
        None,    ///< Complete nothing: inside a quoted literal, for instance.
        Prefix,  ///< Complete a query keyword: tag:, date:, and, or, not.
        Value,   ///< Complete a value for `prefix`.
    };

    Kind kind = None;

    /// For Value, the keyword left of ':', lowercased. Empty for Prefix.
    QString prefix;

    /// The text being matched against the candidates.
    QString stem;

    /// The exact span an accepted completion overwrites. Covers only the text
    /// being completed, so accepting never disturbs neighbouring text.
    int replaceFrom = 0;
    int replaceLength = 0;
};

/// Decides what the cursor position implies about completion.
///
/// `cursor` is an offset into `text`, as QLineEdit::cursorPosition() returns.
CompletionContext completionContext(const QString &text, int cursor);
```

Create `src/querycompleter.cpp` (GPL header, then):

```cpp
#include "querycompleter.h"

namespace {

/// Start of the token the cursor sits in. The boundary is whitespace or '(',
/// so "tag:inbox and su" has its last token starting at 14, not at 0.
int tokenStart(const QString &text, int cursor)
{
    int start = cursor;
    while (start > 0) {
        const QChar c = text.at(start - 1);
        if (c.isSpace() || c == QLatin1Char('('))
            break;
        --start;
    }
    return start;
}

} // namespace

CompletionContext completionContext(const QString &text, int cursor)
{
    CompletionContext ctx;

    if (cursor < 0 || cursor > text.size())
        return ctx;

    const int start = tokenStart(text, cursor);
    const QString token = text.mid(start, cursor - start);

    ctx.kind = CompletionContext::Prefix;
    ctx.stem = token;
    ctx.replaceFrom = start;
    ctx.replaceLength = token.size();
    return ctx;
}
```

Add `querycompleter.cpp` to the `qtmaildir_lib` list in `src/CMakeLists.txt`,
after `mainwindow.cpp` on line 16.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R querycompleter --output-on-failure`
Expected: PASS, 4 tests.

- [ ] **Step 5: Commit**

```bash
git add src/querycompleter.h src/querycompleter.cpp tests/test_querycompleter.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -S -m "feat(completion): add the query cursor-context tokenizer

Prefix completion only so far: the token under the cursor, bounded by
whitespace or an opening parenthesis rather than by the start of the line."
```

---

## Task 2: Value context after a prefix

**Files:**
- Modify: `src/querycompleter.cpp`
- Test: `tests/test_querycompleter.cpp`

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` block and the file body:

```cpp
    void colonSwitchesToValue();
    void valueReplaceSpanExcludesThePrefix();
    void prefixIsLowercased();
    void emptyValueAfterColonStillCompletes();
```

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ctest --test-dir build -R querycompleter --output-on-failure`
Expected: FAIL, `colonSwitchesToValue` compares `Prefix` to `Value`.

- [ ] **Step 3: Write minimal implementation**

Replace the body of `completionContext()` after the `token` line:

```cpp
    const int colon = token.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        ctx.kind = CompletionContext::Prefix;
        ctx.stem = token;
        ctx.replaceFrom = start;
        ctx.replaceLength = token.size();
        return ctx;
    }

    ctx.kind = CompletionContext::Value;
    ctx.prefix = token.left(colon).toLower();
    ctx.stem = token.mid(colon + 1);
    ctx.replaceFrom = start + colon + 1;
    ctx.replaceLength = ctx.stem.size();
    return ctx;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R querycompleter --output-on-failure`
Expected: PASS, 8 tests.

- [ ] **Step 5: Commit**

```bash
git add src/querycompleter.cpp tests/test_querycompleter.cpp
git commit -S -m "feat(completion): recognise value context after a prefix

The replace span covers the value only, so accepting a completion never
overwrites the prefix that selected it."
```

---

## Task 3: Quoted literals complete nothing

**Files:**
- Modify: `src/querycompleter.cpp`
- Test: `tests/test_querycompleter.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void insideQuotesCompletesNothing();
    void afterClosedQuotesCompletesAgain();
```

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ctest --test-dir build -R querycompleter --output-on-failure`
Expected: FAIL. `insideQuotesCompletesNothing` gets `Value`, because the space
inside the quotes is treated as a token boundary.

- [ ] **Step 3: Write minimal implementation**

Add above `tokenStart()` in the anonymous namespace:

```cpp
/// Whether the cursor sits inside a double-quoted literal. Counts quotes from
/// the start: an odd count before the cursor means the quote is still open.
bool insideQuotes(const QString &text, int cursor)
{
    int quotes = 0;
    for (int i = 0; i < cursor; ++i) {
        if (text.at(i) == QLatin1Char('"'))
            ++quotes;
    }
    return (quotes % 2) != 0;
}
```

Add to `completionContext()` immediately after the bounds check:

```cpp
    if (insideQuotes(text, cursor))
        return ctx;   // kind stays None
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R querycompleter --output-on-failure`
Expected: PASS, 10 tests.

- [ ] **Step 5: Commit**

```bash
git add src/querycompleter.cpp tests/test_querycompleter.cpp
git commit -S -m "feat(completion): complete nothing inside a quoted literal

subject:\"foo bar has the cursor in free text, where offering keywords
would be wrong."
```

---

## Task 4: Date ranges complete on both bounds

**Files:**
- Modify: `src/querycompleter.h`
- Modify: `src/querycompleter.cpp`
- Test: `tests/test_querycompleter.cpp`

This is the subtlest rule in the plan. `date:today..yes` must complete
`yesterday` as the upper bound without disturbing `today`.

- [ ] **Step 1: Write the failing test**

```cpp
    void rangeUpperBoundCompletes();
    void rangeLowerBoundCompletes();
    void bareValueAllowsRelativeEntries();
    void rangeSuppressesRelativeEntries();
```

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build 2>&1 | tail -20`
Expected: FAIL to compile, `'allowRangeEntries' has no member`.

- [ ] **Step 3: Write minimal implementation**

Add to `CompletionContext` in `src/querycompleter.h`, after `replaceLength`:

```cpp
    /// Whether candidates that are themselves ranges may be offered.
    ///
    /// The relative date entries ("1week..") are complete open-ended ranges.
    /// Offering one inside an existing range yields date:1week....today, which
    /// is malformed, so they are withheld once a range is underway.
    bool allowRangeEntries = true;
```

In `completionContext()`, replace the Value block:

```cpp
    ctx.kind = CompletionContext::Value;
    ctx.prefix = token.left(colon).toLower();

    const QString value = token.mid(colon + 1);
    const int valueStart = start + colon + 1;

    // A range is two independent values. Complete whichever side the cursor
    // is in, leaving the other untouched.
    const int separator = value.indexOf(QStringLiteral(".."));
    if (separator < 0) {
        ctx.stem = value;
        ctx.replaceFrom = valueStart;
        ctx.replaceLength = value.size();
        return ctx;
    }

    ctx.allowRangeEntries = false;

    const int cursorInValue = cursor - valueStart;
    if (cursorInValue <= separator) {
        ctx.stem = value.left(cursorInValue);
        ctx.replaceFrom = valueStart;
        ctx.replaceLength = separator;
    } else {
        const int upperStart = separator + 2;
        ctx.stem = value.mid(upperStart, cursorInValue - upperStart);
        ctx.replaceFrom = valueStart + upperStart;
        ctx.replaceLength = value.size() - upperStart;
    }
    return ctx;
```

Note the `stem` uses the cursor offset while `replaceLength` uses the whole
side. That is deliberate: matching uses what has been typed so far, but
accepting replaces the entire bound, so completing mid-word does not leave a
tail behind.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R querycompleter --output-on-failure`
Expected: PASS, 14 tests.

- [ ] **Step 5: Commit**

```bash
git add src/querycompleter.h src/querycompleter.cpp tests/test_querycompleter.cpp
git commit -S -m "feat(completion): complete both bounds of a date range

Each side of '..' is an independent value against the same model. Entries
that are themselves ranges are withheld once a range exists, since
date:1week....today is malformed."
```

---

## Task 5: The vocabulary tables

**Files:**
- Create: `src/completionentry.h`
- Modify: `src/querycompleter.h`
- Modify: `src/querycompleter.cpp`
- Test: `tests/test_querycompleter.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void prefixVocabularyCoversNotmuchKeywords();
    void dateVocabularySeparatesRelativeEntries();
```

```cpp
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
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build 2>&1 | tail -20`
Expected: FAIL, `'CompletionEntry' was not declared`.

- [ ] **Step 3: Write minimal implementation**

Create `src/completionentry.h` (GPL header, then):

```cpp
#pragma once

#include <QString>

/// One completion candidate and the prose describing it.
///
/// This lives in its own header because both Config and QueryCompleter need
/// it, and QueryCompleter needs Config. Declaring it in querycompleter.h
/// would make the two headers include each other.
struct CompletionEntry
{
    QString value;        ///< Inserted verbatim. Query syntax, never translated.
    QString description;  ///< Shown beside it. Prose, always translated.
};
```

Add `#include <QList>` and `#include "completionentry.h"` to
`src/querycompleter.h`, then after `completionContext()`:

```cpp
/// The notmuch query keywords, with descriptions. Hardcoded: notmuch exposes
/// no way to enumerate its own prefixes, so this list must track releases by
/// hand. See the spec's Consequences section.
QList<CompletionEntry> prefixVocabulary();

/// Symbolic and relative date values. Absolute dates are not enumerable and
/// are covered by the free-form hint in the popup footer instead.
QList<CompletionEntry> dateVocabulary();

/// The built-in mimetypes, before the user's extra_mimetypes are appended.
QList<CompletionEntry> mimetypeVocabulary();
```

Add to `src/querycompleter.cpp`, after the anonymous namespace. Note `QObject::tr()`
requires `#include <QObject>`:

```cpp
QList<CompletionEntry> prefixVocabulary()
{
    return {
        { QStringLiteral("tag:"),        QObject::tr("messages with a tag") },
        { QStringLiteral("is:"),         QObject::tr("same as tag:") },
        { QStringLiteral("from:"),       QObject::tr("sender address or name") },
        { QStringLiteral("to:"),         QObject::tr("recipient, including Cc") },
        { QStringLiteral("subject:"),    QObject::tr("words in the subject") },
        { QStringLiteral("date:"),       QObject::tr("a date or a range") },
        { QStringLiteral("attachment:"), QObject::tr("attachment filename") },
        { QStringLiteral("mimetype:"),   QObject::tr("attachment content type") },
        { QStringLiteral("folder:"),     QObject::tr("Maildir folder name") },
        { QStringLiteral("path:"),       QObject::tr("directory below the Maildir root") },
        { QStringLiteral("thread:"),     QObject::tr("a thread id") },
        { QStringLiteral("id:"),         QObject::tr("a single message id") },
        { QStringLiteral("and"),         QObject::tr("both conditions") },
        { QStringLiteral("or"),          QObject::tr("either condition") },
        { QStringLiteral("not"),         QObject::tr("exclude what follows") },
    };
}

QList<CompletionEntry> dateVocabulary()
{
    return {
        { QStringLiteral("today"),      QObject::tr("since midnight") },
        { QStringLiteral("yesterday"),  QObject::tr("the previous day") },
        { QStringLiteral("this_week"),  QObject::tr("the current week") },
        { QStringLiteral("last_week"),  QObject::tr("the week before this one") },
        { QStringLiteral("this_month"), QObject::tr("the current month") },
        { QStringLiteral("last_month"), QObject::tr("the month before this one") },
        { QStringLiteral("this_year"),  QObject::tr("the current year") },
        { QStringLiteral("1week.."),    QObject::tr("the last seven days") },
        { QStringLiteral("1month.."),   QObject::tr("the last month") },
    };
}

QList<CompletionEntry> mimetypeVocabulary()
{
    return {
        { QStringLiteral("application/pdf"), QObject::tr("PDF document") },
        { QStringLiteral("image/jpeg"),      QObject::tr("JPEG image") },
        { QStringLiteral("image/png"),       QObject::tr("PNG image") },
        { QStringLiteral("text/html"),       QObject::tr("HTML document") },
        { QStringLiteral("application/zip"), QObject::tr("ZIP archive") },
    };
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R querycompleter --output-on-failure`
Expected: PASS, 16 tests.

- [ ] **Step 5: Commit**

```bash
git add src/completionentry.h src/querycompleter.h src/querycompleter.cpp \
        tests/test_querycompleter.cpp
git commit -S -m "feat(completion): add the prefix, date and mimetype vocabularies

Keywords are syntax and stay untranslated; the descriptions beside them are
prose and go through tr()."
```

---

## Task 6: Config accessors

**Files:**
- Modify: `src/config.h:95` (after `messageZoom()`)
- Modify: `src/config.cpp:77-88` (after the `message_zoom` block)
- Test: `tests/test_config.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_config.cpp`. Follow the existing fixture style in that file
for writing a temporary config; the assertions are:

```cpp
    void completionOnFocusDefaultsToFalse();
    void extraMimetypesAppendToBuiltins();
    void extraMimetypeDescriptionMayContainComma();
    void malformedExtraMimetypeIsSkipped();
```

```cpp
void TestConfig::completionOnFocusDefaultsToFalse()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n")));
    QCOMPARE(config.completionOnFocus(), false);
}

void TestConfig::extraMimetypesAppendToBuiltins()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[completion]\n"
        "extra_mimetypes = application/epub+zip|EPUB book, message/rfc822\n")));

    const QList<CompletionEntry> extra = config.extraMimetypes();
    QCOMPARE(extra.size(), 2);
    QCOMPARE(extra.at(0).value, QStringLiteral("application/epub+zip"));
    QCOMPARE(extra.at(0).description, QStringLiteral("EPUB book"));
    QCOMPARE(extra.at(1).value, QStringLiteral("message/rfc822"));
    QVERIFY(extra.at(1).description.isEmpty());
}

void TestConfig::extraMimetypeDescriptionMayContainComma()
{
    // '|' separates value from description precisely so a description can
    // contain a comma without QSettings tearing the entry in two.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[completion]\n"
        "extra_mimetypes = \"application/epub+zip|EPUB, an ebook format\"\n")));

    const QList<CompletionEntry> extra = config.extraMimetypes();
    QCOMPARE(extra.size(), 1);
    QCOMPARE(extra.at(0).description, QStringLiteral("EPUB, an ebook format"));
}

void TestConfig::malformedExtraMimetypeIsSkipped()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[completion]\n"
        "extra_mimetypes = |no value here, message/rfc822\n")));

    const QList<CompletionEntry> extra = config.extraMimetypes();
    QCOMPARE(extra.size(), 1);
    QCOMPARE(extra.at(0).value, QStringLiteral("message/rfc822"));
    QVERIFY(!config.problems().isEmpty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build 2>&1 | tail -20`
Expected: FAIL, `'class Config' has no member named 'completionOnFocus'`.

- [ ] **Step 3: Write minimal implementation**

`src/config.h`: add `#include "completionentry.h"` and `#include <QList>`, then
after `messageZoom()`:

```cpp
    /// Whether focusing an empty query bar opens the completion popup. Off by
    /// default: it is helpful when learning the query language and intrusive
    /// once it is known. The manual trigger works regardless.
    bool completionOnFocus() const { return m_completionOnFocus; }

    /// User-supplied mimetype completions, APPENDED to the built-in list.
    /// Appending rather than replacing means a typo cannot leave completion
    /// worse off than the defaults.
    QList<CompletionEntry> extraMimetypes() const { return m_extraMimetypes; }
```

and the members:

```cpp
    bool m_completionOnFocus = false;
    QList<CompletionEntry> m_extraMimetypes;
```

`src/config.cpp`, after the `message_zoom` block. **`[general]` keys take no
prefix; `[completion]` keys do:**

```cpp
    m_completionOnFocus =
        settings.value(QStringLiteral("completion_on_focus"), false).toBool();

    const QStringList rawMimetypes =
        settings.value(QStringLiteral("completion/extra_mimetypes")).toStringList();
    for (const QString &raw : rawMimetypes) {
        const QString entry = raw.trimmed();
        if (entry.isEmpty())
            continue;

        const int bar = entry.indexOf(QLatin1Char('|'));
        const QString value =
            (bar < 0 ? entry : entry.left(bar)).trimmed();
        const QString description =
            (bar < 0 ? QString() : entry.mid(bar + 1)).trimmed();

        if (value.isEmpty()) {
            addProblem(QStringLiteral(
                "completion/extra_mimetypes: entry with no mimetype: %1").arg(entry));
            continue;
        }
        m_extraMimetypes.append({ value, description });
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R config --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/config.h src/config.cpp tests/test_config.cpp
git commit -S -m "feat(config): add completion_on_focus and extra_mimetypes

Mimetypes are the one completion list with no enumerator, so the user can
extend it. Entries append to the built-ins and a malformed one is skipped
with a problem recorded rather than dropping the whole list."
```

---

## Task 7: The all-tags worker call

**Files:**
- Modify: `src/notmuchworker.h:66` (slots) and `:76` (signals)
- Modify: `src/notmuchworker.cpp`
- Test: `tests/test_notmuchworker.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_notmuchworker.cpp`, following the existing fixture that
builds a throwaway Maildir and runs `notmuch new`:

```cpp
    void requestAllTagsReturnsSortedTags();
```

```cpp
void TestNotmuchWorker::requestAllTagsReturnsSortedTags()
{
    NotmuchWorker worker(m_configPath);
    QSignalSpy spy(&worker, &NotmuchWorker::allTagsReady);

    worker.requestAllTags(7);

    QCOMPARE(spy.count(), 1);
    const QStringList tags = spy.at(0).at(0).toStringList();
    const quint64 generation = spy.at(0).at(1).value<quint64>();

    QCOMPARE(generation, quint64(7));
    QVERIFY(tags.contains(QStringLiteral("inbox")));

    QStringList sorted = tags;
    sorted.sort();
    QCOMPARE(tags, sorted);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build 2>&1 | tail -20`
Expected: FAIL, `'class NotmuchWorker' has no member named 'requestAllTags'`.

- [ ] **Step 3: Write minimal implementation**

`src/notmuchworker.h`, in `public slots:`:

```cpp
    /// Every tag in the database, sorted. Feeds query bar completion, which
    /// cannot offer tag names it has no way to enumerate. Called at startup,
    /// after a sync, and after a tag mutation introduces an unknown tag.
    void requestAllTags(quint64 generation);
```

in `signals:`:

```cpp
    void allTagsReady(const QStringList &tags, quint64 generation);
```

`src/notmuchworker.cpp`. `NmTags` already exists in `nmraii.h`:

```cpp
void NotmuchWorker::requestAllTags(quint64 generation)
{
    if (!openReadOnly())
        return;

    NmTags tags(notmuch_database_get_all_tags(m_db));
    if (!tags) {
        emit errorOccurred(QStringLiteral("Cannot list tags"));
        return;
    }

    QStringList result;
    for (; notmuch_tags_valid(tags.get()); notmuch_tags_move_to_next(tags.get()))
        result.append(QString::fromUtf8(notmuch_tags_get(tags.get())));

    result.sort();
    emit allTagsReady(result, generation);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R notmuchworker --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp tests/test_notmuchworker.cpp
git commit -S -m "feat(worker): list every tag in the database

Query bar completion cannot offer tag names without a way to enumerate
them, and libnotmuch had no call wired up for it. Follows the existing
generation-counter pattern; the result crosses the thread boundary as a
QStringList."
```

---

## Task 8: Model selection

**Files:**
- Modify: `src/querycompleter.h`
- Modify: `src/querycompleter.cpp`
- Test: `tests/test_querycompleter.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
    void tagAndIsShareTheTagModel();
    void pathOffersAccountMaildirsBothForms();
    void folderOffersNothing();
    void mimetypeAppendsConfiguredEntries();
    void rangeContextDropsRelativeDates();
```

```cpp
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
```

`writeIni()` is the helper already used in `tests/test_config.cpp:44`. Copy it
verbatim into this file rather than sharing it, so the two tests stay
independent:

```cpp
static QString writeIni(const QTemporaryDir &dir, const QString &body)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(body.toUtf8());
    f.close();
    return path;
}
```

It needs `#include <QTemporaryDir>` and `#include "config.h"`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build 2>&1 | tail -20`
Expected: FAIL, `'QueryCompleter' was not declared in this scope`.

- [ ] **Step 3: Write minimal implementation**

Add to `src/querycompleter.h`:

```cpp
#include <QObject>
#include <QStringList>

class Config;
class QCompleter;
class QLineEdit;
class QStandardItemModel;

/// Completion for the notmuch query bar.
///
/// Installs a QCompleter on a QLineEdit and swaps its model as the cursor
/// moves between contexts, so `tag:` offers tag names while `date:` offers
/// dates. The tokenizer above decides which; this class owns the candidates.
class QueryCompleter : public QObject
{
    Q_OBJECT
public:
    /// `edit` may be null in tests that exercise candidate selection only.
    QueryCompleter(QLineEdit *edit, const Config &config,
                   QObject *parent = nullptr);

    /// Replaces the tag candidates. Called with the worker's allTagsReady.
    void setTags(const QStringList &tags);

    /// The candidate values for a context, in the order they are offered.
    /// Exposed for testing; the widget path goes through the models directly.
    QStringList candidatesFor(const CompletionContext &context) const;

private:
    QList<CompletionEntry> entriesFor(const CompletionContext &context) const;

    QLineEdit *m_edit = nullptr;
    const Config &m_config;
    QStringList m_tags;
};
```

Add to `src/querycompleter.cpp`:

```cpp
QueryCompleter::QueryCompleter(QLineEdit *edit, const Config &config,
                               QObject *parent)
    : QObject(parent), m_edit(edit), m_config(config)
{
}

void QueryCompleter::setTags(const QStringList &tags)
{
    m_tags = tags;
}

QList<CompletionEntry> QueryCompleter::entriesFor(
    const CompletionContext &context) const
{
    if (context.kind == CompletionContext::None)
        return {};

    if (context.kind == CompletionContext::Prefix)
        return prefixVocabulary();

    // notmuch treats is:x as a synonym for tag:x, so both take the tag list.
    if (context.prefix == QStringLiteral("tag")
        || context.prefix == QStringLiteral("is")) {
        QList<CompletionEntry> entries;
        for (const QString &tag : m_tags)
            entries.append({ tag, QString() });
        return entries;
    }

    if (context.prefix == QStringLiteral("date")) {
        QList<CompletionEntry> entries;
        for (const CompletionEntry &entry : dateVocabulary()) {
            // Entries that are themselves ranges cannot go inside a range.
            if (!context.allowRangeEntries
                && entry.value.contains(QStringLiteral("..")))
                continue;
            entries.append(entry);
        }
        return entries;
    }

    if (context.prefix == QStringLiteral("mimetype")) {
        QList<CompletionEntry> entries = mimetypeVocabulary();
        entries.append(m_config.extraMimetypes());
        return entries;
    }

    if (context.prefix == QStringLiteral("path")) {
        QList<CompletionEntry> entries;
        for (const Account &account : m_config.accounts()) {
            if (account.maildir.isEmpty())
                continue;
            entries.append({ account.maildir, QObject::tr("account directory") });
            entries.append({ account.maildir + QStringLiteral("/**"),
                             QObject::tr("and everything below it") });
        }
        return entries;
    }

    // from:, to:, folder:, subject:, attachment:, thread:, id: complete no
    // values. Addresses need an enumerator libnotmuch does not expose; the
    // rest are free text.
    return {};
}

QStringList QueryCompleter::candidatesFor(const CompletionContext &context) const
{
    QStringList values;
    for (const CompletionEntry &entry : entriesFor(context))
        values.append(entry.value);
    return values;
}
```

`src/querycompleter.cpp` needs `#include "config.h"`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R querycompleter --output-on-failure`
Expected: PASS, 21 tests.

- [ ] **Step 5: Commit**

```bash
git add src/querycompleter.h src/querycompleter.cpp tests/test_querycompleter.cpp
git commit -S -m "feat(completion): select candidates per context

tag: and is: share the tag list, since notmuch aliases them. path: offers
each account maildir in both bare and recursive forms, the latter being
what Account::scopedQuery builds and not something a user would guess."
```

---

## Task 9: The popup, with descriptions and the free-form date hint

**Files:**
- Modify: `src/querycompleter.h`
- Modify: `src/querycompleter.cpp`

No new unit test: this is widget rendering, verified by running the app. The
selection logic underneath is already covered by Task 8.

- [ ] **Step 1: Build the model and the completer**

Add to `QueryCompleter`'s private section in the header:

```cpp
    void rebuildModel(const CompletionContext &context);
    void updateContext();

    QCompleter *m_completer = nullptr;
    QStandardItemModel *m_model = nullptr;
    QLabel *m_hint = nullptr;
    CompletionContext m_context;
```

Forward-declare `class QLabel;` alongside the others.

- [ ] **Step 2: Implement**

In the constructor, after the member init:

```cpp
    if (!m_edit)
        return;

    m_model = new QStandardItemModel(this);

    m_completer = new QCompleter(m_model, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionColumn(0);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    // Tag hierarchies are the reason completion exists here, and a user who
    // types "amazon" means shopping/amazon.
    m_completer->setFilterMode(Qt::MatchContains);
    m_edit->setCompleter(m_completer);

    auto *view = new QListView;
    view->setItemDelegate(new CompletionDelegate(view));
    m_completer->setPopup(view);

    connect(m_edit, &QLineEdit::textEdited,
            this, &QueryCompleter::updateContext);
    connect(m_edit, &QLineEdit::cursorPositionChanged,
            this, [this]() { updateContext(); });

    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, [this](const QModelIndex &index) {
        // Replace exactly the span the tokenizer identified. QCompleter's own
        // insertion replaces the whole "completion prefix", which is not the
        // same span once a prefix or a range bound is involved.
        const QString value = index.data(Qt::DisplayRole).toString();
        QString text = m_edit->text();
        text.replace(m_context.replaceFrom, m_context.replaceLength, value);
        m_edit->setText(text);
        m_edit->setCursorPosition(m_context.replaceFrom + value.size());
    });
```

`updateContext()` and `rebuildModel()`:

```cpp
void QueryCompleter::updateContext()
{
    if (!m_edit)
        return;
    m_context = completionContext(m_edit->text(), m_edit->cursorPosition());
    rebuildModel(m_context);
}

void QueryCompleter::rebuildModel(const CompletionContext &context)
{
    m_model->clear();

    for (const CompletionEntry &entry : entriesFor(context)) {
        auto *value = new QStandardItem(entry.value);
        auto *description = new QStandardItem(entry.description);
        m_model->appendRow({ value, description });
    }

    // The free-form hint is a footer, not a row: a row would be filtered away
    // by the first keystroke that did not match it, and could be selected and
    // inserted, producing a broken query.
    if (m_hint) {
        m_hint->setVisible(context.kind == CompletionContext::Value
                           && context.prefix == QStringLiteral("date"));
    }
}
```

The delegate, in the anonymous namespace:

```cpp
/// Draws the description greyed and right-aligned beside the value.
class CompletionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);

        const QModelIndex sibling = index.sibling(index.row(), 1);
        const QString description = sibling.data(Qt::DisplayRole).toString();
        if (description.isEmpty())
            return;

        painter->save();
        QColor grey = option.palette.color(QPalette::Disabled, QPalette::Text);
        painter->setPen(grey);
        painter->drawText(option.rect.adjusted(0, 0, -6, 0),
                          Qt::AlignRight | Qt::AlignVCenter, description);
        painter->restore();
    }
};
```

The hint label, created after the popup view:

```cpp
    m_hint = new QLabel(
        tr("also accepts free-form dates, e.g. 2026-01-15 or 15/01/2026..today"),
        view);
    QFont hintFont = m_hint->font();
    hintFont.setItalic(true);
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.9);
    m_hint->setFont(hintFont);
    m_hint->hide();
```

**Verify empirically rather than from memory:** the hint must sit below the
list and must not be selectable. If parenting it to the view does not place it
correctly, wrap the view and the label in a `QWidget` with a `QVBoxLayout` and
set that as the popup instead. Do not assume either approach works without
running it.

- [ ] **Step 3: Build and run the app**

Run: `cmake --build build && ./build/src/qtmaildir`
Verify by hand: typing `tag:` shows tag names with no descriptions; typing
`date:` shows dates with descriptions and the italic footer; typing
`subject:"foo ` shows nothing.

- [ ] **Step 4: Commit**

```bash
git add src/querycompleter.h src/querycompleter.cpp
git commit -S -m "feat(completion): render the popup with descriptions

The free-form date hint is a footer label rather than a model row: a row
would be filtered away by the first non-matching keystroke and could be
selected and inserted, producing a query that errors."
```

---

## Task 10: The manual trigger and the focus toggle

**Files:**
- Modify: `src/keymap.cpp:38` (known actions) and `:75` (default bindings)
- Modify: `src/querycompleter.h` / `.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_keymap.cpp`:

```cpp
void TestKeyMap::completeQueryIsBoundByDefault()
{
    QVERIFY(KeyMap::knownActions().contains(QStringLiteral("complete_query")));
    QCOMPARE(KeyMap::defaultSequenceFor(QStringLiteral("complete_query")),
             QKeySequence(QStringLiteral("Ctrl+Space")));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ctest --test-dir build -R keymap --output-on-failure`
Expected: FAIL, `knownActions()` does not contain it.

- [ ] **Step 3: Write minimal implementation**

`src/keymap.cpp`: add `QStringLiteral("complete_query"),` to the
`knownActions()` list, and to `defaultBindings()`:

```cpp
        { QStringLiteral("Ctrl+Space"), QStringLiteral("complete_query") },
```

Going through `KeyMap` rather than hardcoding means it appears in the shortcut
reference automatically and is rebindable in `[keys]`.

Add the public slot to `QueryCompleter`:

```cpp
public slots:
    /// Opens the popup regardless of what has been typed. Bound to
    /// complete_query, and the only trigger when completion_on_focus is off.
    void triggerCompletion();
```

```cpp
void QueryCompleter::triggerCompletion()
{
    if (!m_edit || !m_completer)
        return;
    updateContext();
    m_completer->setCompletionPrefix(m_context.stem);
    m_completer->complete();
}
```

For the focus toggle, install an event filter in the constructor when
`m_config.completionOnFocus()` is true:

```cpp
    if (m_config.completionOnFocus())
        m_edit->installEventFilter(this);
```

```cpp
bool QueryCompleter::eventFilter(QObject *watched, QEvent *event)
{
    // Only the empty-bar case: once there is text, ordinary typing has
    // already driven completion.
    if (watched == m_edit && event->type() == QEvent::FocusIn
        && m_edit->text().isEmpty()) {
        triggerCompletion();
    }
    return QObject::eventFilter(watched, event);
}
```

Declare `bool eventFilter(QObject *watched, QEvent *event) override;` in the
header.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R keymap --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/keymap.cpp src/querycompleter.h src/querycompleter.cpp \
        tests/test_keymap.cpp
git commit -S -m "feat(completion): add Ctrl+Space and the focus toggle

The manual trigger goes through KeyMap so it reaches the shortcut reference
and stays rebindable. completion_on_focus governs only the empty-bar case."
```

---

## Task 11: Wire it into MainWindow

**Files:**
- Modify: `src/mainwindow.h` (member, slot)
- Modify: `src/mainwindow.cpp:220-223` (construction), `:825` (sync finished),
  and the `applyTags` result path.

- [ ] **Step 1: Construct the completer**

After the `m_queryEdit` block at `mainwindow.cpp:220`:

```cpp
    m_queryCompleter = new QueryCompleter(m_queryEdit, m_config, this);
```

Add to `mainwindow.h`: `class QueryCompleter;` and
`QueryCompleter *m_queryCompleter = nullptr;`.

- [ ] **Step 2: Wire the worker signal**

In `wireWorker()`, alongside the existing connects:

```cpp
    connect(m_worker, &NotmuchWorker::allTagsReady,
            this, &MainWindow::onAllTagsReady);
```

```cpp
void MainWindow::onAllTagsReady(const QStringList &tags, quint64 generation)
{
    Q_UNUSED(generation);   // Not a query result; no stale generation to discard.
    m_knownTags = tags;
    m_queryCompleter->setTags(tags);
}
```

Add `void onAllTagsReady(const QStringList &tags, quint64 generation);` and
`QStringList m_knownTags;` to the header.

- [ ] **Step 3: Request tags at startup and after sync**

After the worker thread starts, and inside `onSyncFinished()`:

```cpp
    QMetaObject::invokeMethod(m_worker, "requestAllTags", Qt::QueuedConnection,
                              Q_ARG(quint64, 0));
```

- [ ] **Step 4: Refresh after a tag mutation introduces an unknown tag**

Wherever `tagsApplied` is handled, before the existing body:

```cpp
    // A tag the user has just created is the one they are most likely to type
    // again, so do not wait for the next sync to offer it. A set membership
    // test, not a query.
    for (const QString &tag : change.add) {
        if (!m_knownTags.contains(tag)) {
            QMetaObject::invokeMethod(m_worker, "requestAllTags",
                                      Qt::QueuedConnection, Q_ARG(quint64, 0));
            break;
        }
    }
```

- [ ] **Step 5: Connect the shortcut**

Where the other `KeyMap` actions are dispatched, add the `complete_query` case
calling `m_queryCompleter->triggerCompletion()`.

- [ ] **Step 6: Build, test, run**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS, 13 test binaries.

Then `./build/src/qtmaildir` and verify by hand: `tag:` offers real tags from
the database; Ctrl+Space opens the popup on an empty bar; creating a new tag
makes it available without a sync.

- [ ] **Step 7: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp
git commit -S -m "feat(completion): wire query completion into the main window

Tags refresh at startup, after a sync, and after a mutation introduces a tag
not already known, since that is the tag most likely to be typed again."
```

---

## Task 12: Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md:58`

- [ ] **Step 1: Document the config keys**

Add `completion_on_focus` to the `[general]` documentation and a `[completion]`
section documenting `extra_mimetypes`, including the `|` separator and why it
differs from the `,` entry separator.

- [ ] **Step 2: Document the shortcut**

Add `Ctrl+Space` to the README's key binding table.

- [ ] **Step 3: Mark the backlog item done**

Change item 17's status from `open` to `done` in the status table.

- [ ] **Step 4: Commit**

```bash
git add README.md docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md
git commit -S -m "docs: document query completion config and shortcut"
```

---

## Verification

Before calling this complete:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

All 13 test binaries must pass. Then run the app and confirm by hand, since
none of this is reachable from a unit test:

1. `tag:` offers real tag names, hierarchies included.
2. `is:` offers the same list.
3. `date:` offers dates with descriptions and the italic free-form footer.
4. `date:today..` offers `yesterday` but not `1week..`.
5. `path:` offers each account maildir in both forms.
6. `subject:"foo ` offers nothing.
7. Ctrl+Space opens the popup on an empty bar.
8. Accepting a completion mid-query leaves the surrounding text intact.

Item 8 is the one most likely to be wrong, and it is the reason the tokenizer
reports an explicit replace span rather than relying on `QCompleter`'s own
insertion.
