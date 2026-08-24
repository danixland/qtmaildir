# Signatures Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the composer a signature the user picks from a control on the editor bar, stored as one markdown file per signature.

**Architecture:** A new `Signatures` namespace of free functions reads a directory of `*.md` files and splices a signature into a plain-text buffer, finding any existing one by the RFC 3676 `-- ` delimiter and replacing it only when its text matches a known signature. `ComposeWindow` seeds from config on open and re-seeds on a From: change until the user touches the switch. `MessageBuilder` is not touched at all: it already derives `text/plain` verbatim and `text/html` through `MarkdownRenderer` from the same string, so a markdown signature in the buffer yields both forms.

**Tech Stack:** Qt 6 (QDir, QFile, QToolButton, QMenu), C++17, QtTest. No new dependency.

**Spec:** `docs/superpowers/specs/2026-08-24-signatures-design.md`. Backlog item 152.

**Read before starting:** `CLAUDE.md`, in particular the rules on `tr()` and `QT_TRANSLATE_NOOP`, on never running a test binary without `QT_QPA_PLATFORM=offscreen`, and on never launching the application unasked.

---

## File structure

| File | Responsibility |
|---|---|
| `src/signatures.h` (create) | The `Signatures` namespace: `Position`, `names()`, `text()`, `replace()` |
| `src/signatures.cpp` (create) | Its implementation. No widget, no Qt GUI type |
| `src/config.h` (modify) | `Account::signature`; `ComposeSettings::signature`, `::signaturePosition` |
| `src/config.cpp` (modify) | Reads all three keys, reporting a malformed position |
| `src/composewindow.h` (modify) | The switch's members and the seeding helpers |
| `src/composewindow.cpp` (modify) | Builds the switch, seeds on open, follows From: until used |
| `tests/test_signatures.cpp` (create) | The namespace, no widget |
| `tests/test_composewindow.cpp` (create) | The composer wiring. No such file exists yet |
| `src/CMakeLists.txt` (modify) | `signatures.cpp` in `qtmaildir_lib` |
| `tests/CMakeLists.txt` (modify) | `add_qtmaildir_test(signatures)`, `add_qtmaildir_test(composewindow)` |
| `README.md` (modify) | Document the directory and the three keys |
| `CHANGELOG.md` (modify) | An `Added` entry under `[Unreleased]` |
| `translations/qtmaildir_it_IT.ts` (modify) | Refreshed with `lupdate-qt6`, translated, 0 unfinished |

**Build and test commands** used throughout:

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R signatures
QT_QPA_PLATFORM=offscreen ./build/tests/test_signatures -functions
```

Reconfigure only when a CMakeLists changes:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

---

## Task 1: The `Signatures` namespace, reading a directory

**Files:**
- Create: `src/signatures.h`, `src/signatures.cpp`
- Modify: `src/CMakeLists.txt:1-39`
- Create: `tests/test_signatures.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_signatures.cpp`:

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
#include <QTemporaryDir>

#include "signatures.h"

class TestSignatures : public QObject
{
    Q_OBJECT

private slots:
    void namesAreTheFileStemsSorted();
    void namesIgnoreFilesThatAreNotMarkdown();
    void aMissingDirectoryHasNoNames();
    void textIsTheFileContent();
    void textOfAnUnknownNameIsEmpty();

private:
    /// Writes \p files as name -> content into a fresh temporary directory.
    static void write(const QTemporaryDir &dir,
                      const QList<QPair<QString, QString>> &files);
};

void TestSignatures::write(const QTemporaryDir &dir,
                           const QList<QPair<QString, QString>> &files)
{
    for (const auto &entry : files) {
        QFile file(dir.path() + QStringLiteral("/") + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(entry.second.toUtf8());
        file.close();
    }
}

void TestSignatures::namesAreTheFileStemsSorted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir, { { QStringLiteral("work.md"), QStringLiteral("Work") },
                 { QStringLiteral("brief.md"), QStringLiteral("Brief") } });

    QCOMPARE(Signatures::names(dir.path()),
             QStringList({ QStringLiteral("brief"), QStringLiteral("work") }));
}

void TestSignatures::namesIgnoreFilesThatAreNotMarkdown()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir, { { QStringLiteral("work.md"), QStringLiteral("Work") },
                 { QStringLiteral("notes.txt"), QStringLiteral("Not one") },
                 { QStringLiteral("README"), QStringLiteral("Nor this") } });

    QCOMPARE(Signatures::names(dir.path()),
             QStringList({ QStringLiteral("work") }));
}

void TestSignatures::aMissingDirectoryHasNoNames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.path() + QStringLiteral("/nothing-here");

    QVERIFY(Signatures::names(missing).isEmpty());
}

void TestSignatures::textIsTheFileContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir, { { QStringLiteral("work.md"),
                   QStringLiteral("Danilo\n**qtmaildir**\n") } });

    QCOMPARE(Signatures::text(dir.path(), QStringLiteral("work")),
             QStringLiteral("Danilo\n**qtmaildir**\n"));
}

void TestSignatures::textOfAnUnknownNameIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVERIFY(Signatures::text(dir.path(), QStringLiteral("absent")).isEmpty());
}

QTEST_MAIN(TestSignatures)
#include "test_signatures.moc"
```

- [ ] **Step 2: Register the test and the source, then run it to verify it fails**

Add to `src/CMakeLists.txt`, in the `qtmaildir_lib` source list after `searchterm.cpp`:

```cmake
    signatures.cpp
```

Add to `tests/CMakeLists.txt`, after `add_qtmaildir_test(searchterm)`:

```cmake
add_qtmaildir_test(signatures)
```

Run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

Expected: FAIL at configure or compile time, with `signatures.cpp` not found.

- [ ] **Step 3: Write the header**

Create `src/signatures.h`:

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

#include <QString>
#include <QStringList>

/// Signatures, as markdown files spliced into the composer's buffer.
///
/// Free functions over values, with no widget anywhere, matching
/// MarkdownFormat, MessageBuilder and DraftStore. The splice is the part worth
/// testing and it is testable with no painter.
///
/// MARKDOWN, and that is what makes this small: MessageBuilder already builds
/// text/plain from the buffer verbatim and text/html from MarkdownRenderer
/// over the same string, so a signature in the buffer yields both forms with
/// no change there and no second code path. One choice by the user serves both
/// parts, which is what the feature was asked for.
namespace Signatures {

/// Where a newly inserted signature goes, from [compose] signature_position.
enum class Position {
    End,        ///< The end of the buffer. The default and the user's habit.
    AboveQuote  ///< Before the first quoted line, or the end when there is none.
};

/// The stems of every `*.md` in \p dir, sorted, without the extension.
///
/// A missing or unreadable directory yields an empty list. That is not a
/// misconfiguration: it means the user keeps no signatures, and the switch
/// then offers only "None".
QStringList names(const QString &dir);

/// The content of `<dir>/<name>.md`, or empty when it cannot be read.
///
/// \p name is a stem from names(), never a path. It is rejected if it contains
/// a path separator, so a value arriving from the config file cannot reach
/// outside \p dir.
QString text(const QString &dir, const QString &name);

/// Returns \p buffer with \p signature spliced in.
///
/// Any signature already present is replaced; \p signature empty removes it
/// and inserts nothing, which is what "None" selects.
///
/// \p known is the text of every signature in the directory, and it is what
/// makes this non-destructive. A `-- ` delimiter is NOT sufficient authority
/// to delete what follows it: the block is replaced only when its text matches
/// one of \p known, and otherwise the new signature is INSERTED with nothing
/// removed. `-- ` reaches a buffer without the user ever choosing a signature,
/// most plausibly pasted in with quoted text from another client, and the
/// unguarded rule would silently delete everything after it.
///
/// The failure is therefore directional, which is the whole point: a wrong
/// guess adds a visible second signature, one undo away, rather than losing
/// the user's own writing.
QString replace(const QString &buffer, const QString &signature,
                const QStringList &known, Position position);

}  // namespace Signatures
```

- [ ] **Step 4: Write the minimal implementation of `names()` and `text()`**

Create `src/signatures.cpp`. `replace()` is a stub here and is built in Task 2:

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

#include "signatures.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Signatures {

QStringList names(const QString &dir)
{
    QDir directory(dir);
    if (!directory.exists())
        return {};

    QStringList result;
    const QStringList files =
        directory.entryList({ QStringLiteral("*.md") }, QDir::Files, QDir::Name);
    result.reserve(files.size());
    for (const QString &file : files)
        result.append(QFileInfo(file).completeBaseName());
    return result;
}

QString text(const QString &dir, const QString &name)
{
    // A name arriving from the config file is untrusted input reaching a path.
    // Stems from names() never contain a separator, so rejecting one costs
    // nothing and stops `signature = ../../.ssh/id_rsa` from being read into a
    // message the user is about to send.
    if (name.isEmpty() || name.contains(QLatin1Char('/'))
        || name.contains(QLatin1Char('\\')))
        return {};

    QFile file(dir + QStringLiteral("/") + name + QStringLiteral(".md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString replace(const QString &buffer, const QString &signature,
                const QStringList &known, Position position)
{
    Q_UNUSED(signature);
    Q_UNUSED(known);
    Q_UNUSED(position);
    return buffer;
}

}  // namespace Signatures
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R signatures
```

Expected: PASS, 5 of 5.

- [ ] **Step 6: Commit**

```bash
git add src/signatures.h src/signatures.cpp tests/test_signatures.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -S -m "feat(signatures): read a directory of markdown signatures

One file per signature under a directory the caller names, the stem being
the name shown to the user. A name containing a path separator is refused:
it arrives from the config file, and it reaches a path that is read into a
message about to be sent.

Part of item 152."
```

---

## Task 2: The splice, inserting into a buffer with no signature

**Files:**
- Modify: `src/signatures.cpp` (the `replace()` stub)
- Modify: `tests/test_signatures.cpp`

- [ ] **Step 1: Write the failing tests**

Add to the `private slots:` block of `tests/test_signatures.cpp`:

```cpp
    void insertingAtTheEndAppendsAfterADelimiter();
    void insertingAboveTheQuotePutsItBeforeTheFirstQuotedLine();
    void insertingAboveTheQuoteWithNoQuoteIsTheSameAsEnd();
    void insertingNothingLeavesTheBufferAlone();
```

Add the implementations before `QTEST_MAIN`:

```cpp
void TestSignatures::insertingAtTheEndAppendsAfterADelimiter()
{
    const QString buffer = QStringLiteral("Hello.\n");

    const QString result = Signatures::replace(
        buffer, QStringLiteral("Danilo"), {}, Signatures::Position::End);

    QCOMPARE(result, QStringLiteral("Hello.\n\n-- \nDanilo"));
}

void TestSignatures::insertingAboveTheQuotePutsItBeforeTheFirstQuotedLine()
{
    const QString buffer = QStringLiteral(
        "My reply.\n"
        "\n"
        "On Mon, someone wrote:\n"
        "> the original\n"
        "> second line\n");

    const QString result = Signatures::replace(
        buffer, QStringLiteral("Danilo"), {},
        Signatures::Position::AboveQuote);

    // Before the QUOTED lines, and the attribution stays with the quote it
    // introduces: it is the line the quote hangs from, not part of the reply.
    QCOMPARE(result, QStringLiteral(
        "My reply.\n"
        "\n"
        "-- \n"
        "Danilo\n"
        "\n"
        "On Mon, someone wrote:\n"
        "> the original\n"
        "> second line\n"));
}

void TestSignatures::insertingAboveTheQuoteWithNoQuoteIsTheSameAsEnd()
{
    const QString buffer = QStringLiteral("A new message.\n");

    const QString above = Signatures::replace(
        buffer, QStringLiteral("Danilo"), {},
        Signatures::Position::AboveQuote);
    const QString end = Signatures::replace(
        buffer, QStringLiteral("Danilo"), {}, Signatures::Position::End);

    QCOMPARE(above, end);
}

void TestSignatures::insertingNothingLeavesTheBufferAlone()
{
    const QString buffer = QStringLiteral("Hello.\n");

    QCOMPARE(Signatures::replace(buffer, QString(), {},
                                 Signatures::Position::End),
             buffer);
}
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R signatures
```

Expected: FAIL, 4 failures, each comparing the unchanged buffer against the expected splice (the stub returns `buffer`).

- [ ] **Step 3: Implement the insertion**

Replace the `replace()` stub in `src/signatures.cpp` with this, and add the helpers above it inside the namespace:

```cpp
namespace {

/// The RFC 3676 signature separator: two hyphens, a space, end of line.
///
/// The trailing space is part of the standard and is what receiving clients
/// match on to fold or strip a signature. It is also why `--` typed by hand
/// does not collide: an editor does not add trailing whitespace on its own.
const QLatin1String kDelimiter("-- ");

bool isQuoted(const QString &line)
{
    return line.startsWith(QLatin1Char('>'));
}

/// The index of the first line of the quote, or -1 when the buffer has none.
///
/// The attribution line ("On Mon, someone wrote:") is deliberately NOT
/// included: it introduces the quote and belongs with it, so a signature
/// inserted above the quote goes above the attribution too. Returning the
/// quoted line itself would strand the signature between the attribution and
/// the text it introduces.
int quoteStart(const QStringList &lines)
{
    for (int i = 0; i < lines.size(); ++i) {
        if (!isQuoted(lines.at(i)))
            continue;
        // Walk back over the attribution and the blank line before it, so the
        // signature lands above the whole block rather than inside it.
        int start = i;
        while (start > 0 && !lines.at(start - 1).trimmed().isEmpty()
               && !isQuoted(lines.at(start - 1)))
            --start;
        return start;
    }
    return -1;
}

}  // namespace

QString replace(const QString &buffer, const QString &signature,
                const QStringList &known, Position position)
{
    Q_UNUSED(known);

    if (signature.isEmpty())
        return buffer;

    const QString block = QStringLiteral("\n") + kDelimiter
                          + QStringLiteral("\n") + signature;

    QStringList lines = buffer.split(QLatin1Char('\n'));
    const int quote =
        position == Position::AboveQuote ? quoteStart(lines) : -1;

    // No quote to sit above is not a special case: it is the End placement,
    // which is why a New message needs no branch of its own.
    if (quote < 0)
        return buffer + block;

    QStringList head = lines.mid(0, quote);
    const QStringList tail = lines.mid(quote);
    // The head already ends in the blank line that separated the reply from
    // the attribution, so the block's own leading newline would double it.
    while (!head.isEmpty() && head.last().trimmed().isEmpty())
        head.removeLast();

    return head.join(QLatin1Char('\n')) + block + QStringLiteral("\n\n")
           + tail.join(QLatin1Char('\n'));
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R signatures
```

Expected: PASS, 9 of 9.

- [ ] **Step 5: Commit**

```bash
git add src/signatures.cpp tests/test_signatures.cpp
git commit -S -m "feat(signatures): splice a signature into a buffer

Both placements over one implementation. above_quote inserts before the
attribution rather than before the first quoted line: the attribution
introduces the quote and belongs with it, and a signature between the two
would read as part of the quoted message.

A buffer with no quote makes above_quote identical to end, so a new
message needs no branch of its own.

Part of item 152."
```

---

## Task 3: Replacing an existing signature, and the guard that makes it safe

**Files:**
- Modify: `src/signatures.cpp` (`replace()`)
- Modify: `tests/test_signatures.cpp`

This is the task that carries the spec's data-loss guard. The last test here must not be dropped.

- [ ] **Step 1: Write the failing tests**

Add to the `private slots:` block:

```cpp
    void switchingReplacesAKnownSignature();
    void switchingReplacesAKnownSignatureAboveAQuote();
    void selectingNoneRemovesAKnownSignature();
    void aBlockMatchingNoKnownSignatureIsNotRemoved();
    void aDelimiterInsideTheQuoteIsNotTheSignature();
```

Add the implementations:

```cpp
void TestSignatures::switchingReplacesAKnownSignature()
{
    const QStringList known = { QStringLiteral("Danilo"),
                                QStringLiteral("Danilo M.\nqtmaildir") };
    const QString buffer = QStringLiteral("Hello.\n\n-- \nDanilo");

    const QString result = Signatures::replace(
        buffer, QStringLiteral("Danilo M.\nqtmaildir"), known,
        Signatures::Position::End);

    QCOMPARE(result,
             QStringLiteral("Hello.\n\n-- \nDanilo M.\nqtmaildir"));
}

void TestSignatures::switchingReplacesAKnownSignatureAboveAQuote()
{
    const QStringList known = { QStringLiteral("Danilo"),
                                QStringLiteral("Brief") };
    const QString buffer = QStringLiteral(
        "My reply.\n"
        "\n"
        "-- \n"
        "Danilo\n"
        "\n"
        "On Mon, someone wrote:\n"
        "> the original\n");

    const QString result = Signatures::replace(
        buffer, QStringLiteral("Brief"), known,
        Signatures::Position::AboveQuote);

    QCOMPARE(result, QStringLiteral(
        "My reply.\n"
        "\n"
        "-- \n"
        "Brief\n"
        "\n"
        "On Mon, someone wrote:\n"
        "> the original\n"));
}

void TestSignatures::selectingNoneRemovesAKnownSignature()
{
    const QStringList known = { QStringLiteral("Danilo") };
    const QString buffer = QStringLiteral("Hello.\n\n-- \nDanilo");

    const QString result = Signatures::replace(
        buffer, QString(), known, Signatures::Position::End);

    QCOMPARE(result, QStringLiteral("Hello.\n"));
}

void TestSignatures::aBlockMatchingNoKnownSignatureIsNotRemoved()
{
    // THE test for the data-loss guard, and it must not be dropped. A "-- "
    // reaches a buffer without the user ever choosing a signature, pasted in
    // with quoted text from another client. Replacing from there would delete
    // everything after it silently.
    const QStringList known = { QStringLiteral("Danilo") };
    const QString buffer = QStringLiteral(
        "Hello.\n"
        "\n"
        "-- \n"
        "text the user pasted and wants to keep");

    const QString result = Signatures::replace(
        buffer, QStringLiteral("Danilo"), known, Signatures::Position::End);

    // The user's text survives, and the signature is ADDED. A wrong guess
    // produces a visible duplicate, never a deletion.
    QVERIFY(result.contains(
        QStringLiteral("text the user pasted and wants to keep")));
    QVERIFY(result.endsWith(QStringLiteral("-- \nDanilo")));
}

void TestSignatures::aDelimiterInsideTheQuoteIsNotTheSignature()
{
    // The quoted original carries the sender's own signature, quoted. A tail
    // rule would find it, and under End it would append after it; the block
    // must not be treated as this message's signature whichever way it goes.
    const QStringList known = { QStringLiteral("Danilo") };
    const QString buffer = QStringLiteral(
        "My reply.\n"
        "\n"
        "On Mon, someone wrote:\n"
        "> the original\n"
        "> -- \n"
        "> Their Name\n");

    const QString result = Signatures::replace(
        buffer, QStringLiteral("Danilo"), known, Signatures::Position::End);

    QVERIFY(result.contains(QStringLiteral("> -- \n> Their Name")));
    QVERIFY(result.endsWith(QStringLiteral("-- \nDanilo")));
}
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R signatures
```

Expected: FAIL. `switchingReplacesAKnownSignature` shows two signatures in the result, because Task 2's `replace()` only ever appends.

- [ ] **Step 3: Implement the find-and-remove**

Add this helper to the anonymous namespace in `src/signatures.cpp`, after `quoteStart()`:

```cpp
/// The line index of the delimiter introducing an existing signature, or -1.
///
/// Two conditions, and both are load-bearing. The delimiter must not be
/// QUOTED, since the quoted original carries the other party's signature and
/// it is not this message's to replace. And the block after it must MATCH one
/// of \p known: finding a delimiter is not authority to delete what follows
/// it, because "-- " reaches a buffer pasted in with quoted text.
int existingSignature(const QStringList &lines, const QStringList &known)
{
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (lines.at(i) != kDelimiter)
            continue;

        // The block runs to the end, or to the quote when the signature sits
        // above one.
        int end = i + 1;
        while (end < lines.size() && !isQuoted(lines.at(end)))
            ++end;
        // A trailing blank line belongs to the separation, not to the text.
        int textEnd = end;
        while (textEnd > i + 1 && lines.at(textEnd - 1).trimmed().isEmpty())
            --textEnd;

        const QString block =
            lines.mid(i + 1, textEnd - (i + 1)).join(QLatin1Char('\n'));
        if (known.contains(block))
            return i;
    }
    return -1;
}

/// \p lines with the signature at \p delimiter removed, blank separator and
/// all. The caller has already established that the block is a known one.
QStringList withoutSignature(const QStringList &lines, int delimiter)
{
    int end = delimiter + 1;
    while (end < lines.size() && !isQuoted(lines.at(end)))
        ++end;

    QStringList head = lines.mid(0, delimiter);
    while (!head.isEmpty() && head.last().trimmed().isEmpty())
        head.removeLast();

    QStringList result = head;
    if (end < lines.size()) {
        // Something follows (the quote): restore the blank line that
        // separated it from the signature now being removed.
        result.append(QString());
        result.append(lines.mid(end));
    } else {
        // The signature ran to the end of the buffer, and the trailing
        // newline the head lost with its blank line goes back.
        result.append(QString());
    }
    return result;
}
```

Then change the body of `replace()` to remove before inserting. Replace the whole function with:

```cpp
QString replace(const QString &buffer, const QString &signature,
                const QStringList &known, Position position)
{
    QStringList lines = buffer.split(QLatin1Char('\n'));

    const int existing = existingSignature(lines, known);
    if (existing >= 0)
        lines = withoutSignature(lines, existing);

    const QString stripped = lines.join(QLatin1Char('\n'));

    // "None", or nothing to insert: the removal above is the whole operation.
    if (signature.isEmpty())
        return stripped;

    const QString block = QStringLiteral("\n") + kDelimiter
                          + QStringLiteral("\n") + signature;

    const int quote =
        position == Position::AboveQuote ? quoteStart(lines) : -1;

    if (quote < 0)
        return stripped + block;

    QStringList head = lines.mid(0, quote);
    const QStringList tail = lines.mid(quote);
    while (!head.isEmpty() && head.last().trimmed().isEmpty())
        head.removeLast();

    return head.join(QLatin1Char('\n')) + block + QStringLiteral("\n\n")
           + tail.join(QLatin1Char('\n'));
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R signatures
```

Expected: PASS, 14 of 14.

- [ ] **Step 5: Mutation-check the guard**

The guard is the one thing here that prevents data loss, so prove its test can fail. Temporarily change `existingSignature()` to skip the match:

```cpp
        if (true)  // was: if (known.contains(block))
            return i;
```

Run:

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R signatures
```

Expected: FAIL, and specifically `aBlockMatchingNoKnownSignatureIsNotRemoved` reporting that the pasted text is gone. **Revert the mutation** and re-run to confirm green before committing.

- [ ] **Step 6: Commit**

```bash
git add src/signatures.cpp tests/test_signatures.cpp
git commit -S -m "feat(signatures): replace an existing signature, guarded by a match

Finding a \"-- \" delimiter is not authority to delete what follows it. The
block is replaced only when its text matches one of the signatures on disk,
and otherwise the new one is inserted with nothing removed, so a wrong guess
adds a visible duplicate rather than destroying the user's writing.

A quoted delimiter is never the signature either: the quoted original
carries the other party's, and it is not this message's to replace.

The guard's test was mutation-checked by making the match unconditional,
which fails it.

Part of item 152."
```

---

## Task 4: The three config keys

**Files:**
- Modify: `src/config.h:31-60` (`Account`), `src/config.h:202-232` (`ComposeSettings`)
- Modify: `src/config.cpp:443-500` (the accounts loop), `src/config.cpp:528-548` (the `[compose]` group)
- Modify: `tests/test_config.cpp`

- [ ] **Step 1: Write the failing tests**

Add to the `private slots:` block of `tests/test_config.cpp`:

```cpp
    void theSignatureKeysAreRead();
    void anAccountSignatureOverridesTheComposeDefault();
    void aMalformedSignaturePositionIsReportedAndFallsBack();
```

Add the implementations before `QTEST_MAIN`. Follow the file's existing helper for writing a config; the pattern below matches the other tests in it:

```cpp
void TestConfig::theSignatureKeysAreRead()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[compose]\n"
            << "signature = work\n"
            << "signature_position = above_quote\n";
    }

    Config config;
    config.load(path);

    QCOMPARE(config.compose().signature, QStringLiteral("work"));
    QCOMPARE(config.compose().signaturePosition,
             Signatures::Position::AboveQuote);
}

void TestConfig::anAccountSignatureOverridesTheComposeDefault()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[compose]\n"
            << "signature = work\n"
            << "\n"
            << "[account.personal]\n"
            << "name = Someone\n"
            << "address = someone@example.org\n"
            << "maildir = personal\n"
            << "signature = brief\n"
            << "\n"
            << "[account.other]\n"
            << "name = Someone\n"
            << "address = other@example.org\n"
            << "maildir = other\n";
    }

    Config config;
    config.load(path);

    // The account SEEDS the choice; it does not own the signature. The key is
    // a starting value and the switch keeps every signature reachable.
    QCOMPARE(config.account(QStringLiteral("personal")).signature,
             QStringLiteral("brief"));
    // An account with no key of its own carries none, and the caller falls
    // through to the [compose] default rather than this being resolved here.
    QVERIFY(config.account(QStringLiteral("other")).signature.isEmpty());
    QCOMPARE(config.compose().signature, QStringLiteral("work"));
}

void TestConfig::aMalformedSignaturePositionIsReportedAndFallsBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[compose]\n"
            << "signature_position = abov\n";
    }

    Config config;
    config.load(path);

    // Present and malformed is REPORTED, matching quote_position. A silent
    // value(key, default) would accept "abov" as above_quote.
    QCOMPARE(config.compose().signaturePosition, Signatures::Position::End);
    bool reported = false;
    for (const QString &problem : config.problems()) {
        if (problem.contains(QStringLiteral("signature_position")))
            reported = true;
    }
    QVERIFY(reported);
}
```

Add the include at the top of `tests/test_config.cpp`, beside the existing ones:

```cpp
#include "signatures.h"
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R config
```

Expected: FAIL at compile time, `'struct ComposeSettings' has no member named 'signature'`.

- [ ] **Step 3: Add the fields**

In `src/config.h`, add the include beside the others at the top:

```cpp
#include "signatures.h"
```

In `struct Account`, after the `sent` member:

```cpp
    /// The signature seeded when composing from this account, by name.
    ///
    /// Optional, and it does not tie a signature to the account: the switch on
    /// the composer's editor bar keeps every signature reachable whichever
    /// account is selected. This is a STARTING value only, which is why the
    /// user's "not tied to an account" constraint survives it (item 152).
    QString signature;
```

In `struct ComposeSettings`, after `defaultAccount`:

```cpp
    /// The signature seeded when the account carries none, by name. Empty
    /// means no signature is seeded at all.
    QString signature;

    /// Where a newly inserted signature goes. End by default, which is the
    /// user's own habit; above_quote exists because other clients offer the
    /// choice, and the splice's quote-aware scan is needed for the guard
    /// either way.
    Signatures::Position signaturePosition = Signatures::Position::End;
```

- [ ] **Step 4: Read the keys**

In `src/config.cpp`, inside the accounts loop after the `sent` key is read:

```cpp
        // Trimmed for the same reason every other name-ish key is: a trailing
        // space would be carried into a filename lookup and match nothing,
        // which is invisible in a config file.
        account.signature =
            settings.value(QStringLiteral("signature")).toString().trimmed();
```

In the `[compose]` group, after `send_html` is read:

```cpp
    m_compose.signature =
        settings.value(QStringLiteral("signature")).toString().trimmed();

    // The same shape as quote_position directly above: an absent key is
    // silent and the struct default holds, but a PRESENT and malformed value
    // is reported rather than silently accepted. value(key, default) alone
    // would read "signature_position = abov" as above_quote.
    const QString signaturePosition =
        settings.value(QStringLiteral("signature_position"),
                       QStringLiteral("end"))
            .toString().trimmed();
    if (signaturePosition.compare(QStringLiteral("above_quote"),
                                  Qt::CaseInsensitive) == 0) {
        m_compose.signaturePosition = Signatures::Position::AboveQuote;
    } else if (signaturePosition.compare(QStringLiteral("end"),
                                         Qt::CaseInsensitive) == 0) {
        m_compose.signaturePosition = Signatures::Position::End;
    } else {
        addProblem(tr("[compose] signature_position '%1' is not recognised; "
                      "expected end or above_quote. Using end.")
                       .arg(signaturePosition));
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R config
```

Expected: PASS, the whole `config` suite including the three new cases.

- [ ] **Step 6: Commit**

```bash
git add src/config.h src/config.cpp tests/test_config.cpp
git commit -S -m "feat(config): read the three signature keys

[compose] signature and signature_position, and a per-account signature
that OVERRIDES the former. The account seeds the choice rather than owning
it: the composer's switch keeps every signature reachable whichever account
is selected, which is what keeps the note's \"not tied to an account\"
constraint intact.

signature_position follows quote_position's shape exactly, reporting a
present-but-malformed value rather than accepting it silently.

Part of item 152."
```

---

## Task 5: The switch on the editor bar, and seeding

**Files:**
- Modify: `src/composewindow.h:190-240`
- Modify: `src/composewindow.cpp:127-147` (the constructor), `:409-540` (`buildFormatToolbar`), `:625-666` (`seedBody`), `:893-918` (`setSendControlsEnabled`)
- Create: `tests/test_composewindow.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_composewindow.cpp` with the GPL header used by every other file in `tests/`, then:

```cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QMenu>

#include "composewindow.h"
#include "composecontext.h"
#include "config.h"
#include "signatures.h"

class TestComposeWindow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aNewMessageSeedsTheComposeSignature();
    void anAccountSignatureOverridesTheComposeOne();
    void aResumedDraftSeedsNoSignature();
    void anUnknownSignatureNameSeedsNothing();
    void theSwitchListsEveryFileAndNone();

private:
    /// A config pointing at a signatures directory holding \p files, with one
    /// account that can send.
    Config makeConfig(const QList<QPair<QString, QString>> &files,
                      const QString &composeSignature,
                      const QString &accountSignature = {});

    QTemporaryDir *m_dir = nullptr;
    QString m_signatureDir;
};

void TestComposeWindow::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_signatureDir = m_dir->path() + QStringLiteral("/signatures");
    QVERIFY(QDir().mkpath(m_signatureDir));
}

void TestComposeWindow::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

Config TestComposeWindow::makeConfig(
    const QList<QPair<QString, QString>> &files,
    const QString &composeSignature, const QString &accountSignature)
{
    for (const auto &entry : files) {
        QFile file(m_signatureDir + QStringLiteral("/") + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(entry.second.toUtf8());
        file.close();
    }

    const QString path = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[compose]\n"
            << "signature = " << composeSignature << "\n"
            << "\n"
            << "[account.work]\n"
            << "name = Someone\n"
            << "address = someone@example.org\n"
            << "maildir = work\n"
            << "send_command = /bin/cat\n";
        if (!accountSignature.isEmpty())
            out << "signature = " << accountSignature << "\n";
    }

    Config config;
    config.load(path);
    return config;
}

void TestComposeWindow::aNewMessageSeedsTheComposeSignature()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Danilo") } },
        QStringLiteral("work"));

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    QVERIFY(body->toPlainText().endsWith(QStringLiteral("-- \nDanilo")));
}

void TestComposeWindow::anAccountSignatureOverridesTheComposeOne()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Long one") },
          { QStringLiteral("brief.md"), QStringLiteral("Brief") } },
        QStringLiteral("work"), QStringLiteral("brief"));

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    QVERIFY(body->toPlainText().endsWith(QStringLiteral("-- \nBrief")));
}

void TestComposeWindow::aResumedDraftSeedsNoSignature()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Danilo") } },
        QStringLiteral("work"));

    // The saved body already carries whatever signature it was written with.
    // Seeding again would put a SECOND one on a message written once.
    ComposeContext context;
    context.kind = ComposeContext::Kind::Draft;
    context.accountKey = QStringLiteral("work");
    context.body = QStringLiteral("Half a thought.\n\n-- \nDanilo");
    context.draftPath = m_dir->path() + QStringLiteral("/draft");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    QCOMPARE(body->toPlainText().count(QStringLiteral("-- \nDanilo")), 1);
}

void TestComposeWindow::anUnknownSignatureNameSeedsNothing()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Danilo") } },
        QStringLiteral("absent"));

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    // No signature, and the composer still opened rather than refusing.
    QVERIFY(!body->toPlainText().contains(QStringLiteral("-- ")));
}

void TestComposeWindow::theSwitchListsEveryFileAndNone()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Danilo") },
          { QStringLiteral("brief.md"), QStringLiteral("Brief") } },
        QString());

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *button =
        window.findChild<QToolButton *>(QStringLiteral("signatureSwitch"));
    QVERIFY(button);
    QVERIFY(button->menu());
    // "None" plus one per file.
    QCOMPARE(button->menu()->actions().size(), 3);
}

QTEST_MAIN(TestComposeWindow)
#include "test_composewindow.moc"
```

Register it in `tests/CMakeLists.txt`, after `add_qtmaildir_test(composecontext)`:

```cmake
add_qtmaildir_test(composewindow)
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

Expected: FAIL at compile time, `'class ComposeWindow' has no member named 'setSignatureDir'`.

- [ ] **Step 3: Add the members and the seam**

In `src/composewindow.h`, in the public section after `lastSaveFailed()`:

```cpp
    /// Where the signature files live. Defaults to
    /// <config>/qtmaildir/signatures; a test points it at its own directory.
    ///
    /// A setter rather than a config key: nothing yet suggests the user wants
    /// a second location, and the tests need to not read the real one.
    void setSignatureDir(const QString &dir);

    /// Seeds the signature from config and fills the switch.
    ///
    /// Public and called by the constructor rather than private, so a test can
    /// drive it after pointing setSignatureDir() somewhere safe. A resumed
    /// draft seeds nothing: its body already carries the signature it was
    /// written with.
    void seedSignature();
```

In the private section, beside `m_sendHtml`:

```cpp
    QToolButton *m_signatureSwitch = nullptr;
    QString m_signatureDir;
    QString m_signatureName;   ///< The selected signature, empty for None.

    /// True once the user has used the switch. From then on a From: change
    /// stops re-seeding, so a deliberate choice is never overwritten. Matches
    /// how send_html seeds from context and is then left alone.
    bool m_signatureChosen = false;
```

Add a private helper declaration beside `seedBody()`:

```cpp
    /// Applies \p name to the buffer, replacing whatever is there.
    void applySignature(const QString &name);

    /// The text of every signature on disk, for replace()'s guard.
    QStringList knownSignatures() const;

    /// The signature name this account seeds, falling through to [compose].
    QString seededSignatureName() const;
```

- [ ] **Step 4: Implement them**

In `src/composewindow.cpp`, add the includes at the top beside the others:

```cpp
#include <QMenu>
#include <QStandardPaths>

#include "signatures.h"
```

Add the switch to `buildFormatToolbar()`, immediately after the `m_sendHtml` block and before the `m_sendAction` comment:

```cpp
    // The signature switch rides at the right end with Attach and the HTML
    // toggle: item 142 put the controls OF THE EDITOR on this side, as against
    // the formatting buttons on the left, and choosing a signature is one of
    // those.
    //
    // A QToolButton with a menu rather than a QComboBox, matching the bar's
    // other controls; a combo would read as a different class of thing. Not
    // registered in KeyMap: it is parented to this window, exactly as the
    // formatting actions are, so its scope is the composer and item 132's
    // reachability rule does not apply.
    m_signatureSwitch = new QToolButton(m_formatToolbar);
    m_signatureSwitch->setObjectName(QStringLiteral("signatureSwitch"));
    m_signatureSwitch->setText(tr("Signature"));
    m_signatureSwitch->setToolTip(
        tr("Chooses the signature added to this message."));
    m_signatureSwitch->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_signatureSwitch->setPopupMode(QToolButton::InstantPopup);
    const QIcon signatureIcon = QIcon::fromTheme(QStringLiteral("insert-text"));
    if (!signatureIcon.isNull())
        m_signatureSwitch->setIcon(signatureIcon);
    m_signatureSwitch->setMenu(new QMenu(m_signatureSwitch));
    m_formatToolbar->addWidget(m_signatureSwitch);
```

Add the new methods, after `seedBody()`:

```cpp
void ComposeWindow::setSignatureDir(const QString &dir)
{
    m_signatureDir = dir;
}

QStringList ComposeWindow::knownSignatures() const
{
    QStringList known;
    const QStringList names = Signatures::names(m_signatureDir);
    known.reserve(names.size());
    for (const QString &name : names)
        known.append(Signatures::text(m_signatureDir, name));
    return known;
}

QString ComposeWindow::seededSignatureName() const
{
    // The account SEEDS, it does not bind: this is a starting value, and the
    // switch keeps every signature reachable whichever account is selected.
    const Account account = m_config.account(m_context.accountKey);
    if (!account.signature.isEmpty())
        return account.signature;
    return m_config.compose().signature;
}

void ComposeWindow::applySignature(const QString &name)
{
    const QString text =
        name.isEmpty() ? QString() : Signatures::text(m_signatureDir, name);

    // A QTextCursor replacement rather than setPlainText(), for the reason
    // recorded at applyEdit(): setPlainText() destroys the document's undo
    // stack, so a switch would make everything typed before it unrecoverable.
    const QString replaced = Signatures::replace(
        m_body->toPlainText(), text, knownSignatures(),
        m_config.compose().signaturePosition);

    QTextCursor cursor(m_body->document());
    cursor.select(QTextCursor::Document);
    cursor.insertText(replaced);

    m_signatureName = name;

    for (QAction *action : m_signatureSwitch->menu()->actions())
        action->setChecked(action->data().toString() == name);
}

void ComposeWindow::seedSignature()
{
    if (m_signatureDir.isEmpty()) {
        const QString base =
            QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        m_signatureDir = base + QStringLiteral("/qtmaildir/signatures");
    }

    QMenu *menu = m_signatureSwitch->menu();
    menu->clear();

    auto *none = menu->addAction(tr("None"));
    none->setCheckable(true);
    none->setData(QString());
    connect(none, &QAction::triggered, this, [this]() {
        m_signatureChosen = true;
        applySignature(QString());
        markDirty();
    });

    const QStringList names = Signatures::names(m_signatureDir);
    for (const QString &name : names) {
        auto *action = menu->addAction(name);
        action->setCheckable(true);
        action->setData(name);
        connect(action, &QAction::triggered, this, [this, name]() {
            m_signatureChosen = true;
            applySignature(name);
            markDirty();
        });
    }

    // A resumed draft is the message ITSELF and already carries whatever
    // signature it was saved with, exactly as seedBody() takes its body
    // verbatim. Seeding again would append a second one.
    if (m_context.kind == ComposeContext::Kind::Draft) {
        none->setChecked(true);
        return;
    }

    const QString seeded = seededSignatureName();
    if (seeded.isEmpty()) {
        none->setChecked(true);
        return;
    }
    if (!names.contains(seeded)) {
        // Reported by Config as a problem; the composer still opens, with no
        // signature, and the switch still works.
        none->setChecked(true);
        return;
    }

    applySignature(seeded);

    // The seeded signature is not an edit the user made, so it must not
    // survive as an undo step: one Ctrl+Z on a fresh composer would otherwise
    // wipe content they never typed. Same reason seedBody() clears after the
    // quote.
    m_body->document()->clearUndoRedoStacks();
}
```

Call it from the constructor, after `seedBody()` on line 130:

```cpp
    seedSignature();
```

Add the switch to `setSendControlsEnabled()`, beside `m_sendHtml`:

```cpp
    m_signatureSwitch->setEnabled(enabled);
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R composewindow
```

Expected: PASS, 5 of 5.

- [ ] **Step 6: Commit**

```bash
git add src/composewindow.h src/composewindow.cpp tests/test_composewindow.cpp \
        tests/CMakeLists.txt
git commit -S -m "feat(compose): a signature switch on the editor bar

A QToolButton with a checkable menu at the right end of the editor bar,
where item 142 put the controls of the editor. Not registered in KeyMap:
parented to the composer like the formatting actions, so its scope is this
window.

The signature is applied through a QTextCursor rather than setPlainText(),
which destroys the undo stack, and the seeded one is cleared from that
stack for the reason the seeded quote already is: one Ctrl+Z must not wipe
content the user never typed.

A resumed draft seeds nothing. Its body already carries the signature it
was written with, and seeding again would put a second one on a message
written once.

Part of item 152."
```

---

## Task 6: Following the From: account until the switch is used

**Files:**
- Modify: `src/composewindow.cpp` (the `m_from` connection at `:406-408`)
- Modify: `tests/test_composewindow.cpp`

- [ ] **Step 1: Write the failing tests**

Add to the `private slots:` block of `tests/test_composewindow.cpp`:

```cpp
    void changingTheAccountFollowsItsSignature();
    void changingTheAccountStopsFollowingOnceTheSwitchIsUsed();
```

Add the implementations. Note the config here needs TWO sending accounts with different signatures, so a wrong answer is distinguishable from a right one:

```cpp
void TestComposeWindow::changingTheAccountFollowsItsSignature()
{
    for (const auto &entry :
         QList<QPair<QString, QString>>{
             { QStringLiteral("work.md"), QStringLiteral("Work sig") },
             { QStringLiteral("home.md"), QStringLiteral("Home sig") } }) {
        QFile file(m_signatureDir + QStringLiteral("/") + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(entry.second.toUtf8());
        file.close();
    }

    const QString path = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.work]\n"
            << "name = Someone\naddress = someone@example.org\n"
            << "maildir = work\nsend_command = /bin/cat\n"
            << "signature = work\n"
            << "\n[account.home]\n"
            << "name = Someone\naddress = other@example.org\n"
            << "maildir = home\nsend_command = /bin/cat\n"
            << "signature = home\n";
    }
    Config config;
    config.load(path);

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *from = window.findChild<QComboBox *>(QStringLiteral("from"));
    QVERIFY(body);
    QVERIFY(from);
    QVERIFY(body->toPlainText().contains(QStringLiteral("Work sig")));

    // Select the other account by its key, never by index: the order of the
    // combo is the config's and an index assertion would pass on the wrong one.
    const int home = from->findData(QStringLiteral("home"));
    QVERIFY(home >= 0);
    from->setCurrentIndex(home);

    QVERIFY(body->toPlainText().contains(QStringLiteral("Home sig")));
    QVERIFY(!body->toPlainText().contains(QStringLiteral("Work sig")));
}

void TestComposeWindow::changingTheAccountStopsFollowingOnceTheSwitchIsUsed()
{
    for (const auto &entry :
         QList<QPair<QString, QString>>{
             { QStringLiteral("work.md"), QStringLiteral("Work sig") },
             { QStringLiteral("home.md"), QStringLiteral("Home sig") },
             { QStringLiteral("chosen.md"), QStringLiteral("Chosen sig") } }) {
        QFile file(m_signatureDir + QStringLiteral("/") + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(entry.second.toUtf8());
        file.close();
    }

    const QString path = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.work]\n"
            << "name = Someone\naddress = someone@example.org\n"
            << "maildir = work\nsend_command = /bin/cat\n"
            << "signature = work\n"
            << "\n[account.home]\n"
            << "name = Someone\naddress = other@example.org\n"
            << "maildir = home\nsend_command = /bin/cat\n"
            << "signature = home\n";
    }
    Config config;
    config.load(path);

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *from = window.findChild<QComboBox *>(QStringLiteral("from"));
    auto *button =
        window.findChild<QToolButton *>(QStringLiteral("signatureSwitch"));
    QVERIFY(body);
    QVERIFY(from);
    QVERIFY(button);

    // The user picks one deliberately.
    for (QAction *action : button->menu()->actions()) {
        if (action->data().toString() == QStringLiteral("chosen"))
            action->trigger();
    }
    QVERIFY(body->toPlainText().contains(QStringLiteral("Chosen sig")));

    const int home = from->findData(QStringLiteral("home"));
    QVERIFY(home >= 0);
    from->setCurrentIndex(home);

    // The deliberate choice survives the account change. Overwriting it is
    // the one behaviour that can silently discard something the user just did.
    QVERIFY(body->toPlainText().contains(QStringLiteral("Chosen sig")));
    QVERIFY(!body->toPlainText().contains(QStringLiteral("Home sig")));
}
```

Add the include at the top of the test file:

```cpp
#include <QComboBox>
```

- [ ] **Step 2: Run them to verify they fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R composewindow
```

Expected: FAIL. `changingTheAccountFollowsItsSignature` still shows "Work sig" after the account change, because nothing re-seeds.

- [ ] **Step 3: Confirm the From: combo is already addressable (no change expected)**

The tests find the combo by `objectName` and its accounts by `findData`. Both
already hold on master and this step is a check, not work:
`composewindow.cpp:259` sets `setObjectName(QStringLiteral("from"))` and
`:582` adds each item as `addItem(label, account.key)`.

```bash
grep -n 'm_from->setObjectName\|m_from->addItem' src/composewindow.cpp
```

Expected: two hits, matching the lines above. If either is missing the tests
cannot address the combo, and it is added before continuing.

- [ ] **Step 4: Re-seed on an account change**

In `src/composewindow.cpp`, replace the existing `m_from` connection:

```cpp
    connect(m_from, &QComboBox::currentIndexChanged, this,
            &ComposeWindow::markDirty);
```

with:

```cpp
    connect(m_from, &QComboBox::currentIndexChanged, this, [this]() {
        markDirty();
        // The account SEEDS the signature, so a change to it re-seeds. It
        // stops the moment the user picks one: re-seeding unconditionally is
        // the one behaviour that can silently discard a deliberate choice
        // made a moment earlier. Same shape as send_html, which seeds from
        // context and is then left alone.
        if (m_signatureChosen)
            return;
        const QString seeded = seededSignatureName();
        if (!Signatures::names(m_signatureDir).contains(seeded)) {
            applySignature(QString());
            return;
        }
        applySignature(seeded);
    });
```

`seededSignatureName()` reads `m_context.accountKey`, which does not follow the combo, so update it to read the combo instead. Replace its body with:

```cpp
QString ComposeWindow::seededSignatureName() const
{
    // The COMBO, not m_context: the context records where the composer opened
    // and does not follow a From: change, so reading it would seed the
    // original account's signature for ever.
    const QString key = m_from->currentData().toString();
    const Account account =
        m_config.account(key.isEmpty() ? m_context.accountKey : key);
    if (!account.signature.isEmpty())
        return account.signature;
    return m_config.compose().signature;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R composewindow
```

Expected: PASS, 7 of 7.

- [ ] **Step 6: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: everything passes except `undoMovesTheMessageBack`, which is backlog item 136 and fails on master already. Confirm that is the only failure, and that its message is item 136's, not something this work caused.

- [ ] **Step 7: Commit**

```bash
git add src/composewindow.cpp tests/test_composewindow.cpp
git commit -S -m "feat(compose): the signature follows the account until it is chosen

A From: change re-seeds the signature from the newly selected account, and
stops doing so the moment the user picks one from the switch. Re-seeding
unconditionally is the one behaviour that can silently discard a deliberate
choice made a moment earlier; this is the shape send_html already uses.

seededSignatureName() reads the combo rather than the context, which
records where the composer opened and does not follow a change to it.

Part of item 152."
```

---

## Task 7: Documentation, changelog and translation

**Files:**
- Modify: `README.md`
- Modify: `CHANGELOG.md`
- Modify: `translations/qtmaildir_it_IT.ts`

- [ ] **Step 1: Document the feature in the README**

Find the Composing section (added by item 122) and add, after the existing `[compose]` key table:

```markdown
### Signatures

Signatures are markdown files, one per signature, in
`~/.config/qtmaildir/signatures/`:

```
~/.config/qtmaildir/signatures/
├── work.md
├── personal.md
└── brief.md
```

The filename without its extension is the name shown in the composer. There
is no editor for them inside qtmaildir: the directory is yours to manage with
your own editor.

Markdown, like the body of a message. `qtmaildir` builds the plain-text part
of a message from what you typed and the HTML part by rendering the same
text, so one signature serves both and you never choose a format.

The composer's editor bar carries a **Signature** control listing every file
plus *None*. Choosing one replaces whichever is already in the message, so
switching is safe to do repeatedly.

| Key | Default | Meaning |
|---|---|---|
| `[compose] signature` | none | The signature seeded on a new message, by name |
| `[compose] signature_position` | `end` | `end`, or `above_quote` to put it before the quoted text in a reply |
| `[account.<key>] signature` | none | Seeds this account's messages instead of the `[compose]` value |

A signature is not tied to an account. `[account.<key>] signature` only
chooses which one a message *starts* with; every signature stays reachable
from the control whichever account you are sending from, and changing the
From: account re-seeds it only until you pick one yourself.
```

- [ ] **Step 2: Add the changelog entry**

Under `## [Unreleased]`, in the `### Added` section (create it if the section is not there):

```markdown
- Signatures. Markdown files in `~/.config/qtmaildir/signatures/`, one per
  signature, chosen from a control on the composer's editor bar. One
  signature serves both the plain-text and the HTML part of a message, since
  the HTML is rendered from the same markdown. `[compose] signature` seeds a
  new message, `[account.<key>] signature` overrides it per account, and
  `[compose] signature_position` puts a new one at the end of the message
  (the default) or above the quoted text in a reply.
```

- [ ] **Step 3: Refresh the translation**

```bash
lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete -locations none
```

Expected: zero context warnings. If `lupdate` prints "tr() cannot be called without context", a literal needs `QT_TRANSLATE_NOOP("TheClass", "Text")` — see `CLAUDE.md`.

- [ ] **Step 4: Translate the new strings**

The new strings are `Signature`, `None`, `Chooses the signature added to this message.` and the `signature_position` warning. Translate each in the `.ts` and remove its `type="unfinished"`:

```xml
<message>
    <source>Signature</source>
    <translation>Firma</translation>
</message>
<message>
    <source>None</source>
    <translation>Nessuna</translation>
</message>
<message>
    <source>Chooses the signature added to this message.</source>
    <translation>Sceglie la firma aggiunta a questo messaggio.</translation>
</message>
<message>
    <source>[compose] signature_position &apos;%1&apos; is not recognised; expected end or above_quote. Using end.</source>
    <translation>[compose] signature_position &apos;%1&apos; non è riconosciuto; atteso end o above_quote. Uso end.</translation>
</message>
```

- [ ] **Step 5: Verify the translation is complete**

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R translations
```

Expected: PASS. `lrelease` must report **0 unfinished**: it silently drops an unfinished string and ships it as English inside an otherwise Italian UI.

- [ ] **Step 6: Commit**

```bash
git add README.md CHANGELOG.md translations/qtmaildir_it_IT.ts
git commit -S -m "docs(signatures): document the directory and the three keys

Part of item 152."
```

---

## Task 8: Close the backlog item

**Files:**
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md` (the item 152 row)
- Move: item 152's section to `docs/superpowers/plans/2026-08-03-post-0.1.0-usability-closed.md`

- [ ] **Step 1: Hand the work to the user for a hand test**

Do NOT mark the item done first. The rule in `CLAUDE.md` and in memory is that a green suite is not evidence the design is right, and this feature is judged by looking at it: where the control sits on the bar, whether the seeded signature reads correctly under the quote, whether switching feels safe.

Tell the user what to look at:

- the **Signature** control at the right of the editor bar, with Attach and Send as HTML
- a new message seeded with the `[compose] signature`
- switching between two signatures repeatedly, confirming nothing accumulates
- a reply under `signature_position = above_quote`
- a resumed draft, confirming it does not gain a second signature

- [ ] **Step 2: After the user reports green, update the status table**

Change item 152's row from `**specified**` to `**done** 2026-08-24, unreleased`, keeping the existing prose about why the design is what it is, and noting anything the hand test found.

- [ ] **Step 3: Move the section to the closed file**

Item 152's `## 152.` section moves from the open backlog to
`2026-08-03-post-0.1.0-usability-closed.md`, on the same commit. `CLAUDE.md`
requires this rather than leaving it for a later cleanup: that is how the file
reached five thousand lines the first time.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md \
        docs/superpowers/plans/2026-08-03-post-0.1.0-usability-closed.md
git commit -S -m "docs(backlog): close item 152, signatures"
```

---

## Notes for the implementer

**Never run a test binary without `QT_QPA_PLATFORM=offscreen`, and never launch
`./build/src/qtmaildir`.** `tests/CMakeLists.txt` sets the variable for ctest
only, so a binary invoked directly inherits the desktop's setting and throws
real windows onto the user's screen. Running the application is a hand test and
belongs to the user. This is in `CLAUDE.md` and the user has asked for it
specifically.

**`MessageBuilder` is not modified by any task in this plan.** If a task seems
to need it, the design has been misread: the signature lives in the composer's
buffer, and both MIME parts are already derived from that buffer.

**The guard in Task 3 is the one piece that prevents data loss.** Its test is
marked as not-to-be-dropped and is mutation-checked in that task. Do not
weaken `existingSignature()` to make an unrelated test pass.

**Two tests need two accounts with different signatures**, in Task 6. One
account, or two with the same signature, answers identically whichever way the
code resolves it, which is the trap `CLAUDE.md` records for the item 87 fix
that was mutation-checked and green while corrupting real mail.
