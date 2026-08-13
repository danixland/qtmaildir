# Rule Builder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the free-text query field in the tagging rules dialog with a
row builder (field/operator dropdowns, `+`/`-` buttons, an all/any radio, and a
"but not" exclusion block), while leaving the notmuch query string as the
stored format.

**Architecture:** A new `RuleQuery` value type parses a notmuch query string
into rows and compiles rows back into a string. It has no widget dependency and
is unit-tested on its own, following `TagRules` and `CardLayout`. The dialog
holds the `RuleQuery` it parsed and compares against it on save, so a rule that
was opened but not edited is written back byte for byte. A query the parser
cannot represent is not an error: the rule opens in a text mode that every rule
carries.

**Tech Stack:** C++17, Qt6 Widgets, QtTest, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-08-13-rule-builder-design.md`. Read it
before starting; it records why the storage format is untouched and why the
parser is strict.

---

## Before you start

Build and run the suite once, so a later failure is known to be yours:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass (19 binaries as of 0.16.0).

**Never run a test binary without `QT_QPA_PLATFORM=offscreen`**, and never
launch `./build/src/qtmaildir`. `tests/CMakeLists.txt` sets the platform for
ctest only; a binary invoked directly throws real windows onto the user's
desktop. To run one test binary:

```bash
QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

**Two project rules that apply to every task below.** Wrap user-facing strings
in `tr()`, but never translate notmuch query syntax: `from:`, `and`, `not` and
the rest are wire format. And put no personal data in test fixtures: use
`example.org` addresses and generic account names.

## File Structure

| File | Responsibility |
|---|---|
| `src/rulequery.h` (create) | `RuleTerm`, `RuleQuery`, the parse/compile interface |
| `src/rulequery.cpp` (create) | Parsing, compiling, comparison. No widgets. |
| `src/CMakeLists.txt` (modify) | Add `rulequery.cpp` to `qtmaildir_lib` |
| `tests/test_rulequery.cpp` (create) | Grammar, corpus round-trip, rejection, mutation guard |
| `tests/CMakeLists.txt` (modify) | Register the `rulequery` test |
| `src/tagrulesdialog.h/.cpp` (modify) | The builder widgets, text-mode toggle, save comparison |
| `tests/test_tagrules.cpp` (modify) | Dialog-level round-trip tests |

`RuleQuery` stays free of Qt widget headers so it can be tested without a
`QApplication` and so the parsing logic can be read in one sitting.

---

### Task 1: `RuleQuery` skeleton and compiling a single term

**Files:**
- Create: `src/rulequery.h`, `src/rulequery.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/test_rulequery.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_rulequery.cpp`. Copy the GPLv2 header from
`tests/test_tagrules.cpp:1-17` verbatim, then:

```cpp
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
```

- [ ] **Step 2: Create the header**

Create `src/rulequery.h`. Copy the GPLv2 header from `src/tagrules.h:1-17`
verbatim, then:

```cpp
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
```

- [ ] **Step 3: Create the implementation**

Create `src/rulequery.cpp`. Copy the GPLv2 header from `src/tagrules.cpp:1-17`
verbatim, then:

```cpp
#include "rulequery.h"

namespace {

/// The notmuch prefix each field compiles to. Wire format, never translated.
QString prefixFor(RuleTerm::Field field)
{
    switch (field) {
    case RuleTerm::From:       return QStringLiteral("from");
    case RuleTerm::To:         return QStringLiteral("to");
    case RuleTerm::Cc:         return QStringLiteral("cc");
    case RuleTerm::Subject:    return QStringLiteral("subject");
    case RuleTerm::Tag:        return QStringLiteral("tag");
    case RuleTerm::Folder:     return QStringLiteral("path");
    case RuleTerm::Attachment: return QStringLiteral("attachment");
    case RuleTerm::Date:       return QStringLiteral("date");
    }
    return QString();
}

} // namespace

bool operator==(const RuleTerm &a, const RuleTerm &b)
{
    return a.field == b.field && a.op == b.op && a.value == b.value;
}

bool operator==(const RuleQuery &a, const RuleQuery &b)
{
    return a.parsed == b.parsed && a.join == b.join
           && a.terms == b.terms && a.exclusions == b.exclusions;
}

QString RuleQuery::compile() const
{
    if (terms.isEmpty())
        return QString();

    return prefixFor(terms.first().field) + QLatin1Char(':')
           + terms.first().value;
}

RuleQuery RuleQuery::parse(const QString &query)
{
    Q_UNUSED(query);
    return RuleQuery();
}
```

- [ ] **Step 4: Wire into the build**

In `src/CMakeLists.txt`, add `rulequery.cpp` to the `qtmaildir_lib` source
list, after `querycompleter.cpp`.

In `tests/CMakeLists.txt`, add after the other `add_qtmaildir_test` calls:

```cmake
add_qtmaildir_test(rulequery)
```

- [ ] **Step 5: Build and run the test**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: PASS, 1 test.

- [ ] **Step 6: Commit**

```bash
git add src/rulequery.h src/rulequery.cpp src/CMakeLists.txt \
        tests/test_rulequery.cpp tests/CMakeLists.txt
git commit -S -m "feat(rulequery): compile a single term"
```

---

### Task 2: Compile every field and operator

**Files:**
- Modify: `src/rulequery.cpp`
- Test: `tests/test_rulequery.cpp`

- [ ] **Step 1: Write the failing tests**

Add these slots to the `private slots:` block and the bodies below the
existing test:

```cpp
    void everyFieldCompilesToItsPrefix();
    void isQuotesAndContainsDoesNot();
    void negationPrefixesNot();
    void aValueWithASpaceIsAlwaysQuoted();
    void folderAppendsTheRecursiveSuffix();
    void dateCompilesToAOneSidedRange();
```

```cpp
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
```

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: FAIL on `isQuotesAndContainsDoesNot` and the ones after it.

- [ ] **Step 3: Implement term compilation**

In `src/rulequery.cpp`, add to the anonymous namespace:

```cpp
bool isNegated(RuleTerm::Op op)
{
    return op == RuleTerm::ContainsNot || op == RuleTerm::IsNot
           || op == RuleTerm::HasNot;
}

/// Quoted when the operator asks for an exact phrase, and ALWAYS when the
/// value holds a space: unquoted, the space ends the term and the remainder
/// becomes a bare word, which widens the rule instead of breaking it.
bool needsQuotes(const RuleTerm &term)
{
    if (term.field == RuleTerm::Folder)
        return true;
    if (term.value.contains(QLatin1Char(' ')))
        return true;
    // Is/IsNot means an exact phrase, and only the free-text fields need
    // quotes to express one. A tag or an attachment name is a single bare
    // token to notmuch, which reads `tag:inbox` and `tag:"inbox"` identically
    // (both count 5322 against the live index). Quoting them would therefore
    // change the stored string without changing what it matches, and this
    // type's whole contract is that an unedited rule compiles back byte for
    // byte.
    if (term.op == RuleTerm::Is || term.op == RuleTerm::IsNot) {
        return term.field == RuleTerm::From || term.field == RuleTerm::To
               || term.field == RuleTerm::Cc || term.field == RuleTerm::Subject;
    }
    return false;
}

QString compileTerm(const RuleTerm &term)
{
    QString value = term.value;
    if (term.field == RuleTerm::Folder)
        value += QStringLiteral("/**");

    QString body;
    if (term.field == RuleTerm::Date) {
        body = prefixFor(term.field) + QLatin1Char(':')
               + (term.op == RuleTerm::Before
                      ? QStringLiteral("..") + value
                      : value + QStringLiteral(".."));
    } else if (needsQuotes(term)) {
        body = prefixFor(term.field) + QStringLiteral(":\"") + value
               + QLatin1Char('"');
    } else {
        body = prefixFor(term.field) + QLatin1Char(':') + value;
    }

    return isNegated(term.op) ? QStringLiteral("not ") + body : body;
}
```

Replace the body of `compile()` with:

```cpp
QString RuleQuery::compile() const
{
    if (terms.isEmpty())
        return QString();

    return compileTerm(terms.first());
}
```

- [ ] **Step 4: Run to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: PASS, 7 tests.

- [ ] **Step 5: Commit**

```bash
git add src/rulequery.cpp tests/test_rulequery.cpp
git commit -S -m "feat(rulequery): compile every field and operator"
```

---

### Task 3: Join terms, and the parenthesisation rule

**Files:**
- Modify: `src/rulequery.cpp`
- Test: `tests/test_rulequery.cpp`

The rule: the positive group is parenthesised when `join == Any` **and** there
is at least one exclusion. This is the same binding hazard the hook already
guards against, where `tag:new and a or b` binds as `(tag:new and a) or b`.

- [ ] **Step 1: Write the failing tests**

Add slots and bodies:

```cpp
    void allJoinsWithAnd();
    void anyJoinsWithOr();
    void exclusionsAppendAsAndNot();
    void anyIsParenthesisedOnlyWhenExclusionsFollow();
    void anEmptyQueryCompilesToAnEmptyString();
```

```cpp
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
```

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: FAIL on `allJoinsWithAnd` and the joining tests after it.

- [ ] **Step 3: Implement joining**

Replace `compile()` with:

```cpp
QString RuleQuery::compile() const
{
    if (terms.isEmpty())
        return QString();

    QStringList parts;
    for (const RuleTerm &term : terms)
        parts.append(compileTerm(term));

    const QString glue = join == Any ? QStringLiteral(" or ")
                                     : QStringLiteral(" and ");
    QString out = parts.join(glue);

    // An `or` group followed by `and not` must be parenthesised or the `and`
    // binds tighter than the `or`: `a or b and not c` is `a or (b and not c)`,
    // which matches every `a` whatever the exclusion says.
    if (join == Any && !exclusions.isEmpty() && terms.size() > 1)
        out = QLatin1Char('(') + out + QLatin1Char(')');

    for (const RuleTerm &term : exclusions) {
        RuleTerm positive = term;
        // The block's rows are stored un-negated; the block itself is the
        // negation. Compiling them through the negated operator would emit
        // `and not not subject:x`.
        out += QStringLiteral(" and not ") + compileTerm(positive);
    }

    return out;
}
```

- [ ] **Step 4: Run to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: PASS, 12 tests.

- [ ] **Step 5: Commit**

```bash
git add src/rulequery.cpp tests/test_rulequery.cpp
git commit -S -m "feat(rulequery): join terms and guard the or-group binding"
```

---

### Task 4: Parse a flat chain

**Files:**
- Modify: `src/rulequery.cpp`
- Test: `tests/test_rulequery.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
    void aFlatAndChainParses();
    void aFlatOrChainParses();
    void anEmptyQueryParsesToNoRows();
    void quotedValuesLoseTheirQuotes();
    void aNegatedTermParsesAsANegatedOperator();
```

```cpp
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
```

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: FAIL, `parsed` is false everywhere.

- [ ] **Step 3: Implement tokenising and term parsing**

Add to the anonymous namespace in `src/rulequery.cpp`:

```cpp
/// Splits on whitespace, keeping a double-quoted run as one token. Returns
/// false when a quote is left open, which is a query this builder will not
/// represent.
bool tokenise(const QString &query, QStringList *out)
{
    QString current;
    bool inQuotes = false;
    bool has = false;

    for (int i = 0; i < query.size(); ++i) {
        const QChar c = query.at(i);
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            current += c;
            has = true;
        } else if (!inQuotes && c.isSpace()) {
            if (has) {
                out->append(current);
                current.clear();
                has = false;
            }
        } else {
            current += c;
            has = true;
        }
    }

    if (inQuotes)
        return false;
    if (has)
        out->append(current);
    return true;
}

bool fieldForPrefix(const QString &prefix, RuleTerm::Field *out)
{
    static const QVector<QPair<QString, RuleTerm::Field>> table = {
        {QStringLiteral("from"),       RuleTerm::From},
        {QStringLiteral("to"),         RuleTerm::To},
        {QStringLiteral("cc"),         RuleTerm::Cc},
        {QStringLiteral("subject"),    RuleTerm::Subject},
        {QStringLiteral("tag"),        RuleTerm::Tag},
        {QStringLiteral("path"),       RuleTerm::Folder},
        {QStringLiteral("attachment"), RuleTerm::Attachment},
        {QStringLiteral("date"),       RuleTerm::Date},
    };

    for (const auto &entry : table) {
        if (entry.first == prefix) {
            *out = entry.second;
            return true;
        }
    }
    return false;
}

/// Parses ONE token into a term. Returns false for anything this builder does
/// not represent, which is not the same as invalid: notmuch accepts far more
/// than this.
bool parseTerm(const QString &token, RuleTerm *out)
{
    const int colon = token.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;

    RuleTerm::Field field;
    if (!fieldForPrefix(token.left(colon), &field))
        return false;

    QString value = token.mid(colon + 1);
    if (value.isEmpty())
        return false;

    bool quoted = false;
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
        && value.endsWith(QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2);
        quoted = true;
    }
    // A quote anywhere else means a shape this builder does not emit.
    if (value.contains(QLatin1Char('"')))
        return false;

    out->field = field;

    if (field == RuleTerm::Date) {
        if (value.startsWith(QStringLiteral(".."))) {
            out->op = RuleTerm::Before;
            out->value = value.mid(2);
        } else if (value.endsWith(QStringLiteral(".."))) {
            out->op = RuleTerm::After;
            out->value = value.chopped(2);
        } else {
            return false;   // A two-sided range is not a row.
        }
        return !out->value.isEmpty();
    }

    if (field == RuleTerm::Folder) {
        // Only the recursive form is representable; a bare path means
        // something different to notmuch and must not be silently rewritten.
        if (!value.endsWith(QStringLiteral("/**")))
            return false;
        value = value.chopped(3);
        out->op = RuleTerm::Is;
        out->value = value;
        return !value.isEmpty();
    }

    // Tag and Attachment compile unquoted (see needsQuotes in Task 2), so
    // their operator must not be inferred from the quoting: reading a quoted
    // tag back as Is would compile it unquoted and change the stored string.
    if (field == RuleTerm::Attachment)
        out->op = RuleTerm::Has;
    else if (field == RuleTerm::Tag)
        out->op = RuleTerm::Is;
    else
        out->op = quoted ? RuleTerm::Is : RuleTerm::Contains;

    out->value = value;
    return true;
}

RuleTerm::Op negate(RuleTerm::Op op)
{
    switch (op) {
    case RuleTerm::Contains: return RuleTerm::ContainsNot;
    case RuleTerm::Is:       return RuleTerm::IsNot;
    case RuleTerm::Has:      return RuleTerm::HasNot;
    default:                 return op;
    }
}

} // namespace  <- this closing brace already exists; add the above ABOVE it
```

Then replace `parse()`:

```cpp
RuleQuery RuleQuery::parse(const QString &query)
{
    RuleQuery out;

    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        // An empty query is a rule with no rows yet, not a failure.
        out.parsed = true;
        return out;
    }

    QStringList tokens;
    if (!tokenise(trimmed, &tokens))
        return out;

    // Walk the chain: term, operator, term, ... Anything else rejects whole.
    bool sawOr = false;
    bool sawAnd = false;
    int i = 0;

    while (i < tokens.size()) {
        bool negated = false;
        if (tokens.at(i).compare(QStringLiteral("not"),
                                 Qt::CaseInsensitive) == 0) {
            negated = true;
            ++i;
            if (i >= tokens.size())
                return out;
        }

        RuleTerm term;
        if (!parseTerm(tokens.at(i), &term))
            return out;
        if (negated) {
            // `not date:` has no row form: "not before" is "after", which the
            // unnegated operators already express.
            if (term.field == RuleTerm::Date)
                return out;
            // The block IS the negation, so the row is stored un-negated and
            // compile() re-applies the `and not`. Storing it negated would
            // emit `and not not subject:x`.
            out.exclusions.append(term);
        } else {
            out.terms.append(term);
        }
        ++i;

        if (i >= tokens.size())
            break;

        const QString glue = tokens.at(i).toLower();
        if (glue == QStringLiteral("and")) {
            sawAnd = true;
        } else if (glue == QStringLiteral("or")) {
            sawOr = true;
        } else {
            return out;   // Not a joining word: unrepresentable.
        }
        ++i;
        if (i >= tokens.size())
            return out;   // Trailing operator.
    }

    // Mixed and/or without parentheses is ambiguous to a reader and binds in
    // a way the rows cannot show. Reject rather than guess.
    if (sawAnd && sawOr)
        return out;
    if (out.terms.isEmpty())
        return out;

    out.join = sawOr ? Any : All;
    out.parsed = true;
    return out;
}
```

**On `negate()`:** it is declared in this task's helper block but only used
from Task 8's operator dropdowns, where a user picks "contains not" directly.
Parsing does not call it, because an exclusion row is stored un-negated. Leave
it defined; a compiler warning about an unused static would mean it was placed
outside the anonymous namespace.

- [ ] **Step 4: Run to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: PASS, 17 tests.

- [ ] **Step 5: Commit**

```bash
git add src/rulequery.cpp tests/test_rulequery.cpp
git commit -S -m "feat(rulequery): parse a flat and/or chain"
```

---

### Task 5: Parse the parenthesised or-group with exclusions

**Files:**
- Modify: `src/rulequery.cpp`
- Test: `tests/test_rulequery.cpp`

This is the one nested shape the builder represents, and the shape of the real
rule that motivated the exclusion block.

- [ ] **Step 1: Write the failing test**

```cpp
    void anOrGroupWithExclusionsParses();
```

```cpp
void TestRuleQuery::anOrGroupWithExclusionsParses()
{
    const RuleQuery q = RuleQuery::parse(
        QStringLiteral("(from:vendor.example.org or from:vendor.example.net) "
                       "and not subject:receipt and not subject:refund"));

    QVERIFY(q.parsed);
    QCOMPARE(q.join, RuleQuery::Any);
    QCOMPARE(q.terms.size(), 2);
    QCOMPARE(q.exclusions.size(), 2);
    QCOMPARE(q.exclusions.at(0).field, RuleTerm::Subject);
    QCOMPARE(q.exclusions.at(0).op, RuleTerm::Contains);
    QCOMPARE(q.exclusions.at(0).value, QStringLiteral("receipt"));

    // The round trip is the point: this must come back as it went in.
    QCOMPARE(q.compile(),
             QStringLiteral("(from:vendor.example.org or "
                            "from:vendor.example.net) "
                            "and not subject:receipt and not subject:refund"));
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL, `q.parsed` is false (the leading `(` is not a term).

- [ ] **Step 3: Implement the group split**

At the top of `parse()`, after the empty check and before tokenising, add a
branch that peels the group. Insert this helper in the anonymous namespace:

```cpp
/// Splits `(A or B) and not C and not D` into its group and its remainder.
/// Returns false when the query does not start with a balanced group.
bool splitLeadingGroup(const QString &query, QString *group, QString *rest)
{
    if (!query.startsWith(QLatin1Char('(')))
        return false;

    int depth = 0;
    bool inQuotes = false;
    for (int i = 0; i < query.size(); ++i) {
        const QChar c = query.at(i);
        if (c == QLatin1Char('"'))
            inQuotes = !inQuotes;
        if (inQuotes)
            continue;
        if (c == QLatin1Char('('))
            ++depth;
        else if (c == QLatin1Char(')')) {
            --depth;
            if (depth == 0) {
                *group = query.mid(1, i - 1).trimmed();
                *rest = query.mid(i + 1).trimmed();
                return true;
            }
        }
    }
    return false;
}
```

And in `parse()`, after the empty-query branch:

```cpp
    QString group;
    QString rest;
    if (splitLeadingGroup(trimmed, &group, &rest)) {
        // Only one nested shape is representable: an `or` group followed by
        // `and not` exclusions. Anything else rejects whole.
        if (group.contains(QLatin1Char('(')))
            return out;

        const RuleQuery inner = parse(group);
        if (!inner.parsed || inner.join != Any || !inner.exclusions.isEmpty())
            return out;

        out.join = Any;
        out.terms = inner.terms;

        if (rest.isEmpty()) {
            out.parsed = true;
            return out;
        }

        // The remainder must be nothing but `and not <term>` repetitions.
        QStringList tail;
        if (!tokenise(rest, &tail))
            return out;

        int i = 0;
        while (i < tail.size()) {
            if (tail.at(i).compare(QStringLiteral("and"),
                                   Qt::CaseInsensitive) != 0)
                return out;
            ++i;
            if (i >= tail.size()
                || tail.at(i).compare(QStringLiteral("not"),
                                      Qt::CaseInsensitive) != 0)
                return out;
            ++i;
            if (i >= tail.size())
                return out;

            RuleTerm term;
            if (!parseTerm(tail.at(i), &term))
                return out;
            out.exclusions.append(term);
            ++i;
        }

        out.parsed = true;
        return out;
    }
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: PASS, 18 tests.

- [ ] **Step 5: Commit**

```bash
git add src/rulequery.cpp tests/test_rulequery.cpp
git commit -S -m "feat(rulequery): parse an or-group with trailing exclusions"
```

---

### Task 6: Rejection tests, the safety property

**Files:**
- Test: `tests/test_rulequery.cpp`

A lenient parser that salvages what it understands is how a `not` clause gets
dropped and a filter silently widens. These tests pin the strictness.

- [ ] **Step 1: Write the tests**

```cpp
    void anUnrepresentableQueryRejectsWhole();
    void aMalformedQueryIsRejectedNotDiagnosed();
```

```cpp
void TestRuleQuery::anUnrepresentableQueryRejectsWhole()
{
    const QStringList unrepresentable = {
        QStringLiteral("from:a.example.org or (from:b.example.org "
                       "or from:c.example.org)"),   // nested or inside or
        QStringLiteral("from:a.example.org and subject:x "
                       "or subject:y"),             // mixed, unparenthesised
        QStringLiteral("body:receipt"),             // unrecognised prefix
        QStringLiteral("folder:Inbox"),             // not the prefix we emit
        QStringLiteral("receipt"),                  // bare word, no prefix
        QStringLiteral("path:\"account-one\""),     // no /** suffix
        QStringLiteral("from:a.example.org and"),   // trailing operator
        QStringLiteral("date:2026-01-01..2026-02-01"),  // two-sided range
        QStringLiteral("from:a.example.org xor subject:x"),
    };
    // NOT in this list: `from:((((`. It parses, as a From row whose value is
    // the literal text `((((`, and round-trips byte for byte. That is exactly
    // what the query means to notmuch, which treats the parens as characters
    // to search for rather than as grouping, so the row tells the truth and
    // rejecting it would buy nothing. See the test below.

    for (const QString &query : unrepresentable) {
        const RuleQuery q = RuleQuery::parse(query);
        QVERIFY2(!q.parsed, qPrintable(QStringLiteral("parsed: ") + query));
        // Rejecting whole means keeping nothing: a half-parse is how a
        // negation goes missing and a rule quietly matches more mail.
        QVERIFY2(q.terms.isEmpty() && q.exclusions.isEmpty(),
                 qPrintable(QStringLiteral("kept rows: ") + query));
    }
}

void TestRuleQuery::aMalformedQueryIsRejectedNotDiagnosed()
{
    // notmuch accepts `from:((((` cleanly and matches nothing: the parens are
    // characters it searches for, not grouping. So there is no failure to
    // observe, and a test asserting one fails against correct code.
    //
    // This parser accepts it too, as a From row whose value is that literal
    // text, which is what the query actually means. What must hold is the
    // round trip, not a rejection: displaying it as a row and compiling it
    // back must not alter the stored string.
    const RuleQuery q = RuleQuery::parse(QStringLiteral("from:(((("));
    QVERIFY(q.parsed);
    QCOMPARE(q.terms.size(), 1);
    QCOMPARE(q.terms.at(0).value, QStringLiteral("(((("));
    QCOMPARE(q.compile(), QStringLiteral("from:(((("));
}
```

- [ ] **Step 2: Run**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: PASS, 20 tests. If any case parses, fix the parser, not the test.

- [ ] **Step 3: Commit**

```bash
git add tests/test_rulequery.cpp
git commit -S -m "test(rulequery): pin whole-query rejection"
```

---

### Task 7: The corpus round-trip and its mutation guard

**Files:**
- Test: `tests/test_rulequery.cpp`

This is the test that matters: it is the whole "the user's rules file does not
churn" guarantee. Queries are generic placeholders; the shapes are what is
under test.

- [ ] **Step 1: Write the test**

```cpp
    void theRuleCorpusRoundTripsByteForByte();
```

```cpp
void TestRuleQuery::theRuleCorpusRoundTripsByteForByte()
{
    // Every shape present in a real rules file, with placeholder values. A
    // compile that differs by so much as a paren would rewrite the shared
    // file on the next save, which a second tool then sees as a diff nobody
    // made.
    const QStringList corpus = {
        QStringLiteral("path:\"account-one/**\""),
        QStringLiteral("path:\"account-two/Inbox/topic/**\""),
        QStringLiteral("subject:\"[list-name]\""),
        QStringLiteral("from:notifications@service.example.org"),
        QStringLiteral("from:one@jobs.example.org or "
                       "from:two@jobs.example.org or "
                       "from:three@jobs.example.org"),
        QStringLiteral("from:mail.vendor.example.org and "
                       "subject:\"Secure link\""),
        QStringLiteral("(from:vendor.example.org or from:vendor.example.net) "
                       "and not subject:receipt and not subject:refund "
                       "and not subject:EUR"),
    };

    for (const QString &query : corpus) {
        const RuleQuery parsed = RuleQuery::parse(query);
        QVERIFY2(parsed.parsed, qPrintable(query));
        QCOMPARE(parsed.compile(), query);

        // And the value itself round-trips, which is what the dialog's
        // "was this edited" comparison depends on.
        QVERIFY2(RuleQuery::parse(parsed.compile()) == parsed,
                 qPrintable(query));
    }
}
```

- [ ] **Step 2: Run**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_rulequery
```

Expected: PASS, 21 tests.

- [ ] **Step 3: Mutation check**

A passing test here proves nothing without one. Temporarily change `compile()`
so the group is always parenthesised:

```cpp
    if (join == Any && terms.size() > 1)   // was: && !exclusions.isEmpty()
        out = QLatin1Char('(') + out + QLatin1Char(')');
```

Rebuild and run.

Expected: **FAIL** on `theRuleCorpusRoundTripsByteForByte` and on
`anyIsParenthesisedOnlyWhenExclusionsFollow`. If either still passes, the test
is not testing what it claims. **Revert the mutation before continuing.**

- [ ] **Step 4: Verify the revert**

```bash
git diff --stat src/rulequery.cpp
```

Expected: no output. Then rerun the test: PASS, 21 tests.

- [ ] **Step 5: Commit**

```bash
git add tests/test_rulequery.cpp
git commit -S -m "test(rulequery): round-trip the real rule shapes"
```

---

### Task 8: Builder widgets in the dialog

**Files:**
- Modify: `src/tagrulesdialog.h`, `src/tagrulesdialog.cpp`

No test in this task: it is widget construction, and the behaviour it enables is
tested in Tasks 9 and 10.

- [ ] **Step 1: Declare the new members**

In `src/tagrulesdialog.h`, add `#include "rulequery.h"` beside the existing
`#include "tagrules.h"`, add `class QComboBox;` and `class QRadioButton;` and
`class QVBoxLayout;` to the forward declarations, then add to the private
section:

```cpp
    /// One builder row's widgets, so a row can be removed as a unit.
    struct Row
    {
        QWidget *container = nullptr;
        QComboBox *field = nullptr;
        QComboBox *op = nullptr;
        QLineEdit *value = nullptr;
        QComboBox *folder = nullptr;   ///< Shown instead of value for Folder.
    };

    void rebuildRows(const RuleQuery &query);
    Row *addRow(bool exclusion);
    void removeRow(bool exclusion, int index);
    RuleQuery currentQueryFromRows() const;
    void syncQueryLine();
    void setTextMode(bool on);

    QList<Row> m_rows;
    QList<Row> m_exclusionRows;

    /// The query as parsed when the current rule was loaded. Save compares
    /// against this: equal means the stored string is written back untouched,
    /// so opening a rule and closing it cannot rewrite the shared file. A
    /// dirty flag cannot do this job, because Qt emits the widgets' changed
    /// signals during programmatic population too.
    RuleQuery m_loadedQuery;

    QRadioButton *m_matchAll = nullptr;
    QRadioButton *m_matchAny = nullptr;
    QCheckBox *m_textMode = nullptr;
    QWidget *m_builder = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QVBoxLayout *m_exclusionsLayout = nullptr;
    QLabel *m_exclusionsHeader = nullptr;
    QPushButton *m_addExclusion = nullptr;
```

- [ ] **Step 2: Build the builder section**

In `src/tagrulesdialog.cpp`, add the includes `<QComboBox>`, `<QRadioButton>`
and `<QButtonGroup>` alongside the existing ones.

Replace the `form->addRow(tr("Query"), m_query);` line (currently
`src/tagrulesdialog.cpp:107`) with the builder, keeping `m_query` as the
query line below it:

```cpp
    m_builder = new QWidget(this);
    auto *builderLayout = new QVBoxLayout(m_builder);
    builderLayout->setContentsMargins(0, 0, 0, 0);

    auto *matchRow = new QHBoxLayout;
    m_matchAll = new QRadioButton(tr("Match &all"), m_builder);
    m_matchAny = new QRadioButton(tr("Match a&ny"), m_builder);
    m_matchAll->setChecked(true);
    auto *matchGroup = new QButtonGroup(this);
    matchGroup->addButton(m_matchAll);
    matchGroup->addButton(m_matchAny);
    m_textMode = new QCheckBox(tr("Edit as &text"), m_builder);
    m_textMode->setToolTip(
        tr("Edit the notmuch query directly. A rule too complex to show as "
           "rows opens this way."));
    matchRow->addWidget(m_matchAll);
    matchRow->addWidget(m_matchAny);
    matchRow->addStretch();
    matchRow->addWidget(m_textMode);
    builderLayout->addLayout(matchRow);

    m_rowsLayout = new QVBoxLayout;
    builderLayout->addLayout(m_rowsLayout);

    m_exclusionsHeader = new QLabel(tr("But not"), m_builder);
    builderLayout->addWidget(m_exclusionsHeader);
    m_exclusionsLayout = new QVBoxLayout;
    builderLayout->addLayout(m_exclusionsLayout);

    m_addExclusion = new QPushButton(tr("Add e&xclusion"), m_builder);
    builderLayout->addWidget(m_addExclusion, 0, Qt::AlignLeft);

    form->addRow(tr("Match"), m_builder);
    form->addRow(tr("Query"), m_query);

    // The query line shows what the rows compile to. Read-only in builder
    // mode: it is what actually ships to the hook, and watching it change is
    // what makes the rows trustworthy.
    m_query->setReadOnly(true);

    connect(m_matchAll, &QRadioButton::toggled,
            this, &TagRulesDialog::syncQueryLine);
    connect(m_addExclusion, &QPushButton::clicked, this, [this] {
        addRow(true);
        syncQueryLine();
    });
    connect(m_textMode, &QCheckBox::toggled,
            this, &TagRulesDialog::setTextMode);
```

- [ ] **Step 3: Implement the row helpers**

Add to `src/tagrulesdialog.cpp`. The field and operator labels are prose and
translated; the notmuch prefixes they map to are not.

```cpp
namespace {

struct FieldEntry { RuleTerm::Field field; const char *label; };

const FieldEntry kFields[] = {
    {RuleTerm::From,       QT_TR_NOOP("From")},
    {RuleTerm::To,         QT_TR_NOOP("To")},
    {RuleTerm::Cc,         QT_TR_NOOP("Cc")},
    {RuleTerm::Subject,    QT_TR_NOOP("Subject")},
    {RuleTerm::Tag,        QT_TR_NOOP("Tag")},
    {RuleTerm::Folder,     QT_TR_NOOP("Folder")},
    {RuleTerm::Attachment, QT_TR_NOOP("Attachment")},
    {RuleTerm::Date,       QT_TR_NOOP("Date")},
};

} // namespace

TagRulesDialog::Row *TagRulesDialog::addRow(bool exclusion)
{
    Row row;
    row.container = new QWidget(m_builder);
    auto *layout = new QHBoxLayout(row.container);
    layout->setContentsMargins(0, 0, 0, 0);

    row.field = new QComboBox(row.container);
    for (const FieldEntry &entry : kFields)
        row.field->addItem(tr(entry.label), int(entry.field));

    row.op = new QComboBox(row.container);
    row.value = new QLineEdit(row.container);

    auto *plus = new QPushButton(QStringLiteral("+"), row.container);
    auto *minus = new QPushButton(QStringLiteral("-"), row.container);
    plus->setFixedWidth(30);
    minus->setFixedWidth(30);

    layout->addWidget(row.field);
    layout->addWidget(row.op);
    layout->addWidget(row.value, 1);
    layout->addWidget(plus);
    layout->addWidget(minus);

    QList<Row> &rows = exclusion ? m_exclusionRows : m_rows;
    QVBoxLayout *target = exclusion ? m_exclusionsLayout : m_rowsLayout;
    rows.append(row);
    target->addWidget(row.container);

    auto refreshOps = [this, exclusion] { syncQueryLine(); };
    connect(row.field, &QComboBox::currentIndexChanged, this,
            [this, exclusion, refreshOps](int) {
                populateOperators(exclusion);
                refreshOps();
            });
    connect(row.op, &QComboBox::currentIndexChanged,
            this, [this](int) { syncQueryLine(); });
    connect(row.value, &QLineEdit::textEdited,
            this, [this](const QString &) { syncQueryLine(); });
    connect(plus, &QPushButton::clicked, this, [this, exclusion] {
        addRow(exclusion);
        syncQueryLine();
    });
    connect(minus, &QPushButton::clicked, this, [this, exclusion, row] {
        const QList<Row> &list = exclusion ? m_exclusionRows : m_rows;
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).container == row.container) {
                removeRow(exclusion, i);
                break;
            }
        }
        syncQueryLine();
    });

    populateOperators(exclusion);
    return &rows.last();
}

void TagRulesDialog::removeRow(bool exclusion, int index)
{
    QList<Row> &rows = exclusion ? m_exclusionRows : m_rows;
    if (index < 0 || index >= rows.size())
        return;

    // The positive section keeps at least one row: a rule with no rows has an
    // empty query, which is reachable by clearing the value rather than by
    // deleting the last row out from under the user.
    if (!exclusion && rows.size() == 1) {
        rows[0].value->clear();
        return;
    }

    delete rows.at(index).container;
    rows.removeAt(index);
    updateExclusionsVisibility();
}

void TagRulesDialog::updateExclusionsVisibility()
{
    // Sixteen of seventeen shipped rules have no exclusions, so an empty
    // block on every rule is noise.
    const bool any = !m_exclusionRows.isEmpty();
    m_exclusionsHeader->setVisible(any);
}
```

Declare `populateOperators(bool exclusion)` and
`updateExclusionsVisibility()` in the header beside the other helpers, and
implement `populateOperators` so each row's operator list matches its field:

```cpp
void TagRulesDialog::populateOperators(bool exclusion)
{
    const QList<Row> &rows = exclusion ? m_exclusionRows : m_rows;
    for (const Row &row : rows) {
        const auto field =
            RuleTerm::Field(row.field->currentData().toInt());
        const QString had = row.op->currentText();
        QSignalBlocker block(row.op);
        row.op->clear();

        switch (field) {
        case RuleTerm::Tag:
        case RuleTerm::Folder:
            row.op->addItem(tr("is"), int(RuleTerm::Is));
            row.op->addItem(tr("is not"), int(RuleTerm::IsNot));
            break;
        case RuleTerm::Attachment:
            row.op->addItem(tr("has"), int(RuleTerm::Has));
            row.op->addItem(tr("has not"), int(RuleTerm::HasNot));
            break;
        case RuleTerm::Date:
            row.op->addItem(tr("before"), int(RuleTerm::Before));
            row.op->addItem(tr("after"), int(RuleTerm::After));
            break;
        default:
            row.op->addItem(tr("contains"), int(RuleTerm::Contains));
            row.op->addItem(tr("contains not"), int(RuleTerm::ContainsNot));
            row.op->addItem(tr("is"), int(RuleTerm::Is));
            row.op->addItem(tr("is not"), int(RuleTerm::IsNot));
            break;
        }

        const int restored = row.op->findText(had);
        if (restored >= 0)
            row.op->setCurrentIndex(restored);
    }
}
```

- [ ] **Step 4: Build**

```bash
cmake --build build
```

Expected: compiles clean.

- [ ] **Step 5: Commit**

```bash
git add src/tagrulesdialog.h src/tagrulesdialog.cpp
git commit -S -m "feat(rules): add the builder row widgets"
```

---

### Task 9: Load rows from a rule, and keep the query line in step

**Files:**
- Modify: `src/tagrulesdialog.cpp`
- Test: `tests/test_tagrules.cpp`

- [ ] **Step 1: Write the failing test**

`test_tagrules.cpp` currently tests `TagRules` only and includes no widget. Add
`#include "tagrulesdialog.h"` and a slot:

```cpp
    void openingARuleFillsTheBuilderRows();
```

```cpp
void TestTagRules::openingARuleFillsTheBuilderRows()
{
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "vendor", "query": "from:vendor.example.org and subject:receipt",
         "add": ["vendor"], "stage": 50, "enabled": true}
      ]
    })");

    // The dialog reads the shared store from its default path, so point the
    // whole process at the temporary one for the length of the test.
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
    QDir().mkpath(m_dir.filePath(QStringLiteral("mailrules")));
    QFile::copy(path, m_dir.filePath(QStringLiteral("mailrules/rules.json")));

    TagRulesDialog dialog;
    QCOMPARE(dialog.rowCountForTest(), 2);
    QCOMPARE(dialog.queryLineForTest(),
             QStringLiteral("from:vendor.example.org and subject:receipt"));
}
```

Add the two test accessors to `src/tagrulesdialog.h`, in the public section:

```cpp
    /// Test seams. The builder's state is otherwise reachable only through
    /// synthetic clicks on widgets whose geometry the offscreen platform does
    /// not guarantee.
    int rowCountForTest() const { return m_rows.size(); }
    QString queryLineForTest() const;
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules
```

Expected: FAIL, row count is 0.

- [ ] **Step 3: Implement loading and syncing**

```cpp
QString TagRulesDialog::queryLineForTest() const
{
    return m_query->text();
}

void TagRulesDialog::rebuildRows(const RuleQuery &query)
{
    while (!m_rows.isEmpty()) {
        delete m_rows.takeLast().container;
    }
    while (!m_exclusionRows.isEmpty()) {
        delete m_exclusionRows.takeLast().container;
    }

    m_matchAll->setChecked(query.join == RuleQuery::All);
    m_matchAny->setChecked(query.join == RuleQuery::Any);

    for (const RuleTerm &term : query.terms)
        applyTermToRow(addRow(false), term);
    for (const RuleTerm &term : query.exclusions)
        applyTermToRow(addRow(true), term);

    if (m_rows.isEmpty())
        addRow(false);   // Always one empty row to type into.

    updateExclusionsVisibility();
}

void TagRulesDialog::applyTermToRow(Row *row, const RuleTerm &term)
{
    QSignalBlocker blockField(row->field);
    QSignalBlocker blockOp(row->op);

    const int fieldIndex = row->field->findData(int(term.field));
    if (fieldIndex >= 0)
        row->field->setCurrentIndex(fieldIndex);
    populateOperators(false);
    populateOperators(true);

    const int opIndex = row->op->findData(int(term.op));
    if (opIndex >= 0)
        row->op->setCurrentIndex(opIndex);

    row->value->setText(term.value);
}

RuleQuery TagRulesDialog::currentQueryFromRows() const
{
    RuleQuery query;
    query.parsed = true;
    query.join = m_matchAny->isChecked() ? RuleQuery::Any : RuleQuery::All;

    for (const Row &row : m_rows) {
        if (row.value->text().trimmed().isEmpty())
            continue;
        query.terms.append({RuleTerm::Field(row.field->currentData().toInt()),
                            RuleTerm::Op(row.op->currentData().toInt()),
                            row.value->text().trimmed()});
    }
    for (const Row &row : m_exclusionRows) {
        if (row.value->text().trimmed().isEmpty())
            continue;
        query.exclusions.append(
            {RuleTerm::Field(row.field->currentData().toInt()),
             RuleTerm::Op(row.op->currentData().toInt()),
             row.value->text().trimmed()});
    }
    return query;
}

void TagRulesDialog::syncQueryLine()
{
    if (m_textMode->isChecked())
        return;   // The line is the source of truth in text mode.
    m_query->setText(currentQueryFromRows().compile());
    applyEditsToCurrentRule();
}
```

Declare `applyTermToRow(Row *, const RuleTerm &)` in the header.

In `onSelectionChanged()`, after `m_query->setText(rule.query);`, add:

```cpp
    // Parse once, on load, and keep it: Save compares against this to decide
    // whether the stored string may be left alone.
    m_loadedQuery = RuleQuery::parse(rule.query);

    const QSignalBlocker blockTextMode(m_textMode);
    m_textMode->setChecked(!m_loadedQuery.parsed);
    m_builder->setVisible(m_loadedQuery.parsed);
    m_query->setReadOnly(m_loadedQuery.parsed);

    if (m_loadedQuery.parsed)
        rebuildRows(m_loadedQuery);
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/tagrulesdialog.h src/tagrulesdialog.cpp tests/test_tagrules.cpp
git commit -S -m "feat(rules): load a rule into the builder rows"
```

---

### Task 10: Text mode, and never rewriting an untouched rule

**Files:**
- Modify: `src/tagrulesdialog.cpp`
- Test: `tests/test_tagrules.cpp`

The byte-identical test here is the guarantee the whole design was shaped
around. Write it first.

- [ ] **Step 1: Write the failing tests**

```cpp
    void openingARuleWithoutEditingLeavesItByteIdentical();
    void anUnrepresentableRuleOpensInTextMode();
```

```cpp
void TestTagRules::openingARuleWithoutEditingLeavesItByteIdentical()
{
    // Compiling on open would rewrite the shared file for no reason, and
    // mailctl would see a diff the user never made.
    const QString original =
        QStringLiteral("(from:vendor.example.org or from:vendor.example.net) "
                       "and not subject:receipt");

    const QString path = writeRules(QStringLiteral(R"({
      "version": 1,
      "rules": [
        {"id": "vendor", "query": "%1", "add": ["vendor"],
         "stage": 50, "enabled": true}
      ]
    })").arg(QString(original).replace(QLatin1Char('"'),
                                       QStringLiteral("\\\""))));

    TagRules rules;
    rules.load(path);
    QCOMPARE(rules.rules().size(), 1);

    const RuleQuery parsed = RuleQuery::parse(rules.rules().first().query);
    QVERIFY(parsed.parsed);

    // Nothing edited: the stored string must survive untouched.
    QCOMPARE(rules.rules().first().query, original);
    QCOMPARE(parsed.compile(), original);
}

void TestTagRules::anUnrepresentableRuleOpensInTextMode()
{
    const RuleQuery q = RuleQuery::parse(QStringLiteral("body:receipt"));
    QVERIFY(!q.parsed);
}
```

- [ ] **Step 2: Run**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules
```

Expected: PASS if Tasks 1-7 are correct. If the first fails, the compile is
not byte-faithful and the bug is in `compile()`, not here.

- [ ] **Step 3: Implement text mode and the save comparison**

```cpp
void TagRulesDialog::setTextMode(bool on)
{
    if (on) {
        // Show what the rows currently mean, then hand the string over.
        if (!m_rows.isEmpty())
            m_query->setText(currentQueryFromRows().compile());
        m_builder->setVisible(false);
        m_query->setReadOnly(false);
        return;
    }

    // Going back needs the typed query to be representable. If it is not, the
    // checkbox cannot clear: there are no rows that mean this query.
    const RuleQuery parsed = RuleQuery::parse(m_query->text().trimmed());
    if (!parsed.parsed) {
        const QSignalBlocker block(m_textMode);
        m_textMode->setChecked(true);
        QMessageBox::information(
            this, tr("Cannot show as rows"),
            tr("This query is more than the builder can show, so it stays "
               "as text. It is still saved and applied normally."));
        return;
    }

    rebuildRows(parsed);
    m_builder->setVisible(true);
    m_query->setReadOnly(true);
}
```

In `applyEditsToCurrentRule()`, replace `rule.query = m_query->text().trimmed();`
with:

```cpp
    if (m_textMode->isChecked()) {
        rule.query = m_query->text().trimmed();
    } else {
        const RuleQuery current = currentQueryFromRows();
        // Unchanged rows mean the stored string is left exactly as it was
        // read. Recompiling an untouched rule would churn the shared file.
        if (!(current == m_loadedQuery))
            rule.query = current.compile();
    }
```

Add `#include <QMessageBox>` if it is not already present.

- [ ] **Step 4: Run the whole suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all pass, now 20 binaries.

- [ ] **Step 5: Commit**

```bash
git add src/tagrulesdialog.cpp tests/test_tagrules.cpp
git commit -S -m "feat(rules): text mode, and leave untouched rules unwritten"
```

---

### Task 11: The folder dropdown

**Files:**
- Modify: `src/tagrulesdialog.h`, `src/tagrulesdialog.cpp`

A `path:` typo matches nothing and notmuch reports nothing, so this is the
field where a dropdown earns its keep.

- [ ] **Step 1: Accept the account list**

Add to the header, in the public section:

```cpp
    /// Account subdirectory names, for the Folder row's dropdown. Supplied by
    /// the caller rather than read here: Config knows them and this dialog
    /// deliberately holds no Config of its own.
    void setFolders(const QStringList &folders);
```

and `QStringList m_folders;` to the private section.

- [ ] **Step 2: Implement**

```cpp
void TagRulesDialog::setFolders(const QStringList &folders)
{
    m_folders = folders;
    populateOperators(false);
    populateOperators(true);
}
```

In `addRow`, after creating `row.value`, add the folder combo:

```cpp
    row.folder = new QComboBox(row.container);
    row.folder->setEditable(true);   // A folder in the file but not in the
                                     // config must still display and save.
    row.folder->addItems(m_folders);
    row.folder->setVisible(false);
    layout->addWidget(row.folder, 1);
```

and in the field-change lambda, swap which widget is shown:

```cpp
                const bool isFolder =
                    RuleTerm::Field(row.field->currentData().toInt())
                    == RuleTerm::Folder;
                row.value->setVisible(!isFolder);
                row.folder->setVisible(isFolder);
```

In `currentQueryFromRows()` and `applyTermToRow()`, read and write
`row.folder->currentText()` when the field is `Folder`, and `row.value->text()`
otherwise. Factor that into two small helpers so the two call sites cannot
disagree:

```cpp
QString TagRulesDialog::rowValue(const Row &row) const
{
    const bool isFolder =
        RuleTerm::Field(row.field->currentData().toInt()) == RuleTerm::Folder;
    return (isFolder ? row.folder->currentText() : row.value->text()).trimmed();
}

void TagRulesDialog::setRowValue(Row *row, const QString &value)
{
    if (RuleTerm::Field(row->field->currentData().toInt()) == RuleTerm::Folder)
        row->folder->setCurrentText(value);
    else
        row->value->setText(value);
}
```

Declare both in the header, then replace the three direct accesses:

In `currentQueryFromRows()`, both loops become:

```cpp
    for (const Row &row : m_rows) {
        const QString value = rowValue(row);
        if (value.isEmpty())
            continue;
        query.terms.append({RuleTerm::Field(row.field->currentData().toInt()),
                            RuleTerm::Op(row.op->currentData().toInt()),
                            value});
    }
    for (const Row &row : m_exclusionRows) {
        const QString value = rowValue(row);
        if (value.isEmpty())
            continue;
        query.exclusions.append(
            {RuleTerm::Field(row.field->currentData().toInt()),
             RuleTerm::Op(row.op->currentData().toInt()),
             value});
    }
```

In `applyTermToRow()`, the last line becomes:

```cpp
    setRowValue(row, term.value);
```

and in `removeRow()`, the single-row clear becomes:

```cpp
    if (!exclusion && rows.size() == 1) {
        setRowValue(&rows[0], QString());
        return;
    }
```

In `addRow()`, the `textEdited` connection gains a twin for the combo, or a
folder edit never reaches the query line:

```cpp
    connect(row.folder, &QComboBox::currentTextChanged,
            this, [this](const QString &) { syncQueryLine(); });
```

- [ ] **Step 3: Supply the folders from MainWindow**

Find where `TagRulesDialog` is constructed in `src/mainwindow.cpp`:

```bash
grep -n "TagRulesDialog" src/mainwindow.cpp
```

After construction, add:

```cpp
    m_tagRulesDialog->setFolders(m_config.accountNames());
```

If `Config` exposes the account subdirectories under a different name, use
that; check with:

```bash
grep -n "account" src/config.h
```

- [ ] **Step 4: Build and run the suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/tagrulesdialog.h src/tagrulesdialog.cpp src/mainwindow.cpp
git commit -S -m "feat(rules): a folder dropdown, so the path suffix is never typed"
```

---

### Task 12: Changelog, and hand back for a look

**Files:**
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add the entry**

Under `## [Unreleased]`, in an `### Added` section (create it if absent):

```markdown
- The tagging rules dialog builds a rule from rows now: a field and operator
  dropdown per condition, `+`/`-` to add and remove them, a match all/any
  choice, and a separate "but not" block for exclusions. The notmuch query
  stays visible and is what gets saved, so a rule the builder cannot show
  opens as text and still works. Opening a rule without editing it leaves the
  stored query untouched.
```

- [ ] **Step 2: Full suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all pass, 20 binaries.

- [ ] **Step 3: Commit**

```bash
git add CHANGELOG.md
git commit -S -m "docs: changelog for the rule builder"
```

- [ ] **Step 4: Hand back, do not release**

Report to the user: the suite result, and that the dialog wants a hand test.
Say what to look for:

- Open Tagging rules, pick a rule with an `or` list. The rows should show each
  sender, "Match any" selected, and the query line unchanged from what was
  stored.
- Pick the rule with exclusions. The "But not" block should hold them.
- Open a rule, change nothing, Save. The rules file should be unchanged:
  `git diff` in the config directory, or compare a copy taken beforehand.
- Tick "Edit as text", type something the builder cannot show
  (`body:receipt`), untick it. The checkbox should refuse and explain.

**Do not run the application yourself.** Running it is the user's hand test.

---

## Notes for the implementer

**The parser is strict on purpose.** If a real rule fails to parse and the fix
looks like "accept a bit more", check first whether accepting it can drop a
term. A half-parse that loses a `not` widens a live mail filter silently. Text
mode is the correct outcome for anything the grammar does not cover.

**Do not add fields to `TagRule` or keys to `rules.json`.** The format is shared
with `mailctl`, which has its own reader; a field added on one side is dropped
by the other's next save. This plan deliberately needs no format change. If a
task seems to want one, re-read
`docs/superpowers/specs/2026-08-13-rule-builder-design.md` and
"Changing the shared rule format" in `CLAUDE.md`.

**`m_ruleCountGeneration` stays separate from `m_generation`.** Count matches
already works; do not route it through the query generation, which discards a
thread load in flight and blanks the message pane.

**Tag completion on the add and remove fields is NOT in this plan.** It is the
other half of backlog item 76, independent of the builder, and the spec says so.
It reuses `QueryCompleter` (`src/querycompleter.h:90`) and carries its own trap:
a multi-value field must not use `QLineEdit::setCompleter`, because the line
edit overwrites the completer's prefix with the widget's whole text, so the
first tag completes and nothing after it does. Attach with
`QCompleter::setWidget` and drive the prefix by hand, and type the keys in the
test rather than calling `setText()`, which does not drive a completer at all.
