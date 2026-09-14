# CLI Selectors and Single-Instance Launch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let another program launch `qtmaildir --account KEY --thread ID --message ID` and have the already-running window apply those selectors and raise itself, instead of a second process opening a second window.

**Architecture:** `QCommandLineParser` replaces the hand-rolled `strcmp` loop in `main.cpp`. A `QLocalServer` under the state directory makes the first process the server; a later launch connects, sends its selectors as one payload, and exits. Both the local startup path and the socket handler call one `MainWindow::applySelectors()`, which reuses the existing `recoverStaleThread()` for the thread and message cases. Resolving a Message-ID to a thread id is one new worker round trip.

**Tech Stack:** Qt 6.11 (Widgets, **Network** is new), libnotmuch 5, CMake 3.21+/Ninja, QtTest.

**Spec:** `docs/superpowers/specs/2026-09-13-cli-selectors-design.md`. Backlog item 200.

---

## Required reading before Task 1

Read these before writing any code. Each records a trap this plan walks past.

- `AGENTS.md`, the whole file. In particular **"Adding an action is FIVE places"** (this plan adds none, and the reason is stated in the spec), the `tr()` rules, and the test-writing rules under "Rendering probes lie".
- `docs/superpowers/specs/2026-09-13-cli-selectors-design.md`, the spec this implements.
- `src/searchterm.h:30-35` on why a malformed notmuch query cannot be detected by asking notmuch.

Three facts that will otherwise cost a session each:

1. **Never run a test binary without `QT_QPA_PLATFORM=offscreen`**, and never launch `./build/src/qtmaildir` yourself. `tests/CMakeLists.txt` sets that variable for ctest only. One direct run of `test_mainwindow` throws a hundred windows onto the user's screen.
2. **`recoverStaleThread()` is a PRIVATE SLOT** (`src/mainwindow.h:605`). Tests reach private slots by name through `QMetaObject::invokeMethod`, which is the established pattern in `tests/test_mainwindow.cpp` (see line 2060).
3. **`QTRY_VERIFY_WITH_TIMEOUT`, never `qWait(n)`.** A fixed sleep passes when the result never arrives.

## File Structure

**Created:**

- `src/singleinstance.h` / `src/singleinstance.cpp` — a `SingleInstance` QObject owning the `QLocalServer`. One responsibility: decide whether this process is the first, hand a later process's payload to whoever is listening, and emit what arrived. Knows nothing about queries or mail.
- `src/launchselectors.h` / `src/launchselectors.cpp` — a `LaunchSelectors` struct and the parse/serialise functions over it. Pure over values, no Qt GUI, no widget: this is what makes the parse and the payload testable without a window.
- `tests/test_launchselectors.cpp` — the parse and the payload round trip.
- `tests/test_singleinstance.cpp` — server/client behaviour against a `QTemporaryDir`.

**Modified:**

- `CMakeLists.txt:20` — add `Network` to `QTMAILDIR_QT_COMPONENTS`.
- `src/CMakeLists.txt:58` — link `Qt6::Network`, add the two new `.cpp` files to `qtmaildir_lib`.
- `src/main.cpp:38-66` — replace the `strcmp` loop; add the connect-or-listen step.
- `src/mainwindow.h` / `src/mainwindow.cpp` — `applySelectors()`, a state-path helper, and the Message-ID round trip.
- `src/notmuchworker.h` / `src/notmuchworker.cpp` — `resolveThreadForMessage()` slot and `threadForMessageResolved()` signal.
- `tests/CMakeLists.txt` — register the two new tests.
- `tests/test_mainwindow.cpp` — the applied-selector cases.
- `tests/test_notmuchworker.cpp` — the Message-ID lookup cases.
- `README.md` — a Usage section.
- `CHANGELOG.md` — an `[Unreleased]` entry.

**Why two new units rather than code in `main.cpp`:** `main.cpp` is not in `qtmaildir_lib` (only the executable compiles it, see `src/CMakeLists.txt:67`), so anything written there cannot be tested at all. The parse and the socket both need tests, so both live in the library.

---

## Task 1: The Qt6::Network component

**Files:**
- Modify: `CMakeLists.txt:20`
- Modify: `src/CMakeLists.txt:58-61`

- [ ] **Step 1: Add Network to the component list**

In `CMakeLists.txt`, change line 20 from:

```cmake
set(QTMAILDIR_QT_COMPONENTS Widgets Svg WebEngineWidgets)
```

to:

```cmake
# Network is for QLocalServer/QLocalSocket only, which is a unix domain socket
# between two copies of this program (item 200). It is NOT network protocol
# work: the rule in AGENTS.md is about IMAP and SMTP, and nothing here speaks
# either. Slackware ships it inside the monolithic qt6 package, so this adds no
# new build dependency.
set(QTMAILDIR_QT_COMPONENTS Widgets Svg WebEngineWidgets Network)
```

- [ ] **Step 2: Link it**

In `src/CMakeLists.txt`, change the `target_link_libraries(qtmaildir_lib ...)` call at line 58 so the `PUBLIC` list reads:

```cmake
target_link_libraries(qtmaildir_lib
    PUBLIC Qt6::Widgets Qt6::Svg Qt6::WebEngineWidgets Qt6::Network
           PkgConfig::GMIME
           ${NOTMUCH_LIBRARY} PkgConfig::CMARK_GFM
           ${CMARK_GFM_EXTENSIONS_LIBRARY})
```

- [ ] **Step 3: Reconfigure and build**

Run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

Expected: configures and builds clean. If `find_package` cannot find `Qt6Network`, stop and report it rather than working around it — it means the assumption that Slackware's `qt6` package carries it is wrong, and that changes the SlackBuild too.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt
git commit -S -m "build: link Qt6::Network for the single-instance socket

A unix domain socket between two copies of this program, for item 200. Not
network protocol work: the rule in AGENTS.md is about IMAP and SMTP.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01P3HQXLauwQgzxR4YfJBB3x"
```

---

## Task 2: LaunchSelectors, the value type and its parse

**Files:**
- Create: `src/launchselectors.h`
- Create: `src/launchselectors.cpp`
- Create: `tests/test_launchselectors.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_launchselectors.cpp`:

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

#include "launchselectors.h"

/// The command line and the socket payload, over values. No window, no
/// QApplication: this is the half of item 200 that can be asserted exactly,
/// which is why it is its own unit rather than code inside main.cpp.
class TestLaunchSelectors : public QObject
{
    Q_OBJECT

private slots:
    void anEmptyCommandLineSelectsNothing();
    void eachSelectorIsParsed();
    void theThreeSelectorsCompose();
    void anUnknownOptionIsReportedNotFatal();
    void aPayloadRoundTrips();
    void anEmptyPayloadRoundTripsToNothing();
    void aTruncatedPayloadIsRejected();
    void anOversizedPayloadIsRejected();
};

void TestLaunchSelectors::anEmptyCommandLineSelectsNothing()
{
    QString error;
    const LaunchSelectors selectors =
        LaunchSelectors::parse({ QStringLiteral("qtmaildir") }, &error);

    QVERIFY(error.isEmpty());
    QVERIFY(selectors.isEmpty());
    QVERIFY(selectors.account.isEmpty());
    QVERIFY(selectors.threadId.isEmpty());
    QVERIFY(selectors.messageId.isEmpty());
}

void TestLaunchSelectors::eachSelectorIsParsed()
{
    QString error;

    const LaunchSelectors account = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"), QStringLiteral("--account"),
          QStringLiteral("work") }, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(account.account, QStringLiteral("work"));
    QVERIFY(!account.isEmpty());

    const LaunchSelectors thread = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"), QStringLiteral("--thread"),
          QStringLiteral("0000000000001a2b") }, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(thread.threadId, QStringLiteral("0000000000001a2b"));

    const LaunchSelectors message = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"), QStringLiteral("--message"),
          QStringLiteral("<abc@example.org>") }, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(message.messageId, QStringLiteral("<abc@example.org>"));
}

void TestLaunchSelectors::theThreeSelectorsCompose()
{
    // They are not exclusive: "open this message, in this account's view" is
    // one sensible request, and the spec says they compose.
    QString error;
    const LaunchSelectors selectors = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"),
          QStringLiteral("--account"), QStringLiteral("work"),
          QStringLiteral("--thread"), QStringLiteral("00001a2b"),
          QStringLiteral("--message"), QStringLiteral("<abc@example.org>") },
        &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(selectors.account, QStringLiteral("work"));
    QCOMPARE(selectors.threadId, QStringLiteral("00001a2b"));
    QCOMPARE(selectors.messageId, QStringLiteral("<abc@example.org>"));
}

void TestLaunchSelectors::anUnknownOptionIsReportedNotFatal()
{
    // Reported so main() can print it, and NOT a crash or a silent ignore.
    // Today's strcmp loop ignores everything it does not know, which is how a
    // typo currently produces a normal window and no clue.
    QString error;
    const LaunchSelectors selectors = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"), QStringLiteral("--nonsense") }, &error);

    QVERIFY(!error.isEmpty());
    QVERIFY(selectors.isEmpty());
}

void TestLaunchSelectors::aPayloadRoundTrips()
{
    // What crosses the socket. A round trip is the whole contract: the values
    // that go in are the values that come out, including one with an embedded
    // newline, which is what defeats a line-based format.
    LaunchSelectors original;
    original.account = QStringLiteral("work");
    original.threadId = QStringLiteral("00001a2b");
    original.messageId = QStringLiteral("<a\nb@example.org>");

    QString error;
    const LaunchSelectors parsed =
        LaunchSelectors::fromPayload(original.toPayload(), &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(parsed.account, original.account);
    QCOMPARE(parsed.threadId, original.threadId);
    QCOMPARE(parsed.messageId, original.messageId);
}

void TestLaunchSelectors::anEmptyPayloadRoundTripsToNothing()
{
    // A bare `qtmaildir` with a running instance still sends a payload: it
    // means "raise yourself", which is a real request and not an error.
    QString error;
    const LaunchSelectors parsed =
        LaunchSelectors::fromPayload(LaunchSelectors().toPayload(), &error);

    QVERIFY(error.isEmpty());
    QVERIFY(parsed.isEmpty());
}

void TestLaunchSelectors::aTruncatedPayloadIsRejected()
{
    // The socket hands over whatever it is given. A half-written payload must
    // be refused rather than half-applied.
    LaunchSelectors original;
    original.account = QStringLiteral("work");
    const QByteArray payload = original.toPayload();
    QVERIFY(payload.size() > 4);

    QString error;
    const LaunchSelectors parsed =
        LaunchSelectors::fromPayload(payload.left(payload.size() - 2), &error);

    QVERIFY(!error.isEmpty());
    QVERIFY(parsed.isEmpty());
}

void TestLaunchSelectors::anOversizedPayloadIsRejected()
{
    // A cap, because a local socket will hand over as much as the peer sends.
    // The peer is the user's own process, so this is not a hostile-input
    // defence; it is what stops a confused writer from being read as a
    // gigabyte of selector.
    QString error;
    const LaunchSelectors parsed = LaunchSelectors::fromPayload(
        QByteArray(LaunchSelectors::kMaxPayloadBytes + 1, 'x'), &error);

    QVERIFY(!error.isEmpty());
    QVERIFY(parsed.isEmpty());
}

QTEST_MAIN(TestLaunchSelectors)
#include "test_launchselectors.moc"
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add to `tests/CMakeLists.txt`, beside the other `add_qtmaildir_test` lines:

```cmake
add_qtmaildir_test(launchselectors)
```

Run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

Expected: FAILS to compile, with `launchselectors.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/launchselectors.h`:

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

#include <QByteArray>
#include <QString>
#include <QStringList>

/// What a launch asked the window to show (item 200).
///
/// A value type with no Qt GUI dependency, deliberately: it is parsed before
/// QApplication exists, it crosses a socket, and both halves need tests that
/// no window has to be built for.
///
/// The three selectors COMPOSE rather than excluding each other. "Open this
/// message, in this account's view" is one request, and nothing about it is
/// contradictory.
struct LaunchSelectors
{
    /// An account key as written in the config, e.g. `work` from
    /// `[account.work]`. Validated against the configured accounts by the
    /// window, not here: this unit knows nothing about a Config.
    QString account;

    /// A notmuch thread id.
    QString threadId;

    /// A Message-ID, with or without the angle brackets.
    QString messageId;

    bool isEmpty() const
    {
        return account.isEmpty() && threadId.isEmpty() && messageId.isEmpty();
    }

    /// Longest payload accepted off the socket.
    ///
    /// A local socket hands over whatever the peer sends, and the peer here is
    /// another copy of this program running as the same user, so this is not a
    /// defence against an attacker. It is what stops a confused or truncated
    /// writer from being read as an unbounded selector. Three ids and an
    /// account key are a few hundred bytes; 64 KiB is room to spare.
    static constexpr int kMaxPayloadBytes = 64 * 1024;

    /// Parses a command line, `arguments[0]` being the program name.
    ///
    /// Takes a QStringList rather than argc/argv so it can be called before
    /// QCoreApplication exists, which is what lets --version keep answering on
    /// a machine where the GUI cannot open.
    ///
    /// On an unknown option, returns an empty result and sets \p error. The
    /// caller prints it; it is NOT fatal to the window, since a typo should
    /// not cost the user their mail client.
    static LaunchSelectors parse(const QStringList &arguments, QString *error);

    /// The help text, for `--help`. Translatable prose; the option NAMES are
    /// wire format and are never translated.
    static QString helpText(const QString &versionDisplay);

    /// Serialises for the socket. The inverse of fromPayload().
    QByteArray toPayload() const;

    /// Parses a socket payload. On anything malformed, oversized or truncated,
    /// returns an empty result and sets \p error.
    static LaunchSelectors fromPayload(const QByteArray &payload,
                                       QString *error);
};

/// Declared in the header that DEFINES the type, as types.h and threaddigest.h
/// do for theirs. A consumer declaring it instead would leave any other
/// consumer without it, and QSignalSpy needs it to carry the type.
Q_DECLARE_METATYPE(LaunchSelectors)
```

- [ ] **Step 4: Write the implementation**

Create `src/launchselectors.cpp`:

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

#include "launchselectors.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>

namespace {

/// Bumped only if the payload's shape changes incompatibly. Both ends of the
/// socket are the same binary in the ordinary case, but an upgrade can leave an
/// old instance running while a new one is launched, and a version the reader
/// does not know is refused rather than misread.
constexpr quint16 kPayloadVersion = 1;

}  // namespace

LaunchSelectors LaunchSelectors::parse(const QStringList &arguments,
                                       QString *error)
{
    if (error)
        error->clear();

    QCommandLineParser parser;
    // No addHelpOption()/addVersionOption(): those are handled in main() before
    // QApplication exists, and Qt's own versions call exit() through
    // QCoreApplication, which is not constructed at that point.
    QCommandLineOption accountOption(
        QStringLiteral("account"),
        QCoreApplication::translate(
            "LaunchSelectors", "Open this account's view."),
        QStringLiteral("key"));
    QCommandLineOption threadOption(
        QStringLiteral("thread"),
        QCoreApplication::translate(
            "LaunchSelectors", "Open this thread."),
        QStringLiteral("id"));
    QCommandLineOption messageOption(
        QStringLiteral("message"),
        QCoreApplication::translate(
            "LaunchSelectors", "Open this message, inside its thread."),
        QStringLiteral("id"));

    parser.addOption(accountOption);
    parser.addOption(threadOption);
    parser.addOption(messageOption);

    // parse(), not process(): process() prints to stderr and calls exit() on an
    // error, which would take the window down over a typo. The error is
    // returned instead and main() decides.
    if (!parser.parse(arguments)) {
        if (error)
            *error = parser.errorText();
        return {};
    }

    // --version and --help are consumed in main() before this runs, so they
    // never reach the parser. An unknown option does, and is an error rather
    // than something to ignore: today's strcmp loop ignores everything it does
    // not recognise, so a typo silently produces an ordinary window.
    const QStringList unknown = parser.unknownOptionNames();
    if (!unknown.isEmpty()) {
        if (error) {
            *error = QCoreApplication::translate(
                         "LaunchSelectors", "Unknown option: %1")
                         .arg(unknown.join(QStringLiteral(", ")));
        }
        return {};
    }

    LaunchSelectors selectors;
    selectors.account = parser.value(accountOption);
    selectors.threadId = parser.value(threadOption);
    selectors.messageId = parser.value(messageOption);
    return selectors;
}

QString LaunchSelectors::helpText(const QString &versionDisplay)
{
    // The option NAMES are wire format and are never translated; the prose
    // beside them is. Kept as one block rather than assembled from pieces so a
    // translator sees the layout they are translating.
    return QCoreApplication::translate(
               "LaunchSelectors",
               "qtmaildir %1 - a Qt6 mail client for notmuch-indexed Maildirs\n"
               "\n"
               "Usage: qtmaildir [options]\n"
               "\n"
               "  -h, --help         Show this help and exit\n"
               "  -v, --version      Show the version and exit\n"
               "  --account <key>    Open this account's view\n"
               "  --thread <id>      Open this thread\n"
               "  --message <id>     Open this message, inside its thread\n"
               "\n"
               "The three selectors combine. When qtmaildir is already "
               "running,\n"
               "a second launch hands its selectors to that window and exits "
               "rather\n"
               "than opening a second one.\n"
               "\n"
               "Configuration: ~/.config/qtmaildir/qtmaildir.conf\n"
               "qtmaildir reads a notmuch-indexed Maildir. It does no network\n"
               "protocol work: fetching and sending are external commands.\n")
        .arg(versionDisplay);
}

QByteArray LaunchSelectors::toPayload() const
{
    // QDataStream rather than a line-based format: a Message-ID can contain
    // almost anything, a newline included, and a length-prefixed encoding does
    // not care. The round-trip test carries an embedded newline for exactly
    // this reason.
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kPayloadVersion << account << threadId << messageId;
    return payload;
}

LaunchSelectors LaunchSelectors::fromPayload(const QByteArray &payload,
                                             QString *error)
{
    if (error)
        error->clear();

    if (payload.size() > kMaxPayloadBytes) {
        if (error) {
            *error = QCoreApplication::translate(
                "LaunchSelectors", "Launch payload too large");
        }
        return {};
    }

    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_6_0);

    quint16 version = 0;
    stream >> version;
    if (stream.status() != QDataStream::Ok || version != kPayloadVersion) {
        if (error) {
            *error = QCoreApplication::translate(
                "LaunchSelectors", "Unrecognised launch payload");
        }
        return {};
    }

    LaunchSelectors selectors;
    stream >> selectors.account >> selectors.threadId >> selectors.messageId;

    // Checked AFTER every read, which is what catches a truncated payload: a
    // short read leaves the stream in ReadPastEnd and the fields
    // default-constructed, so without this a half-written message id would be
    // applied as an empty one.
    if (stream.status() != QDataStream::Ok) {
        if (error) {
            *error = QCoreApplication::translate(
                "LaunchSelectors", "Truncated launch payload");
        }
        return {};
    }

    return selectors;
}
```

- [ ] **Step 5: Add the source to the library**

In `src/CMakeLists.txt`, add `launchselectors.cpp` to the `qtmaildir_lib` source list, keeping the list's existing order convention.

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build build && ctest --test-dir build -R launchselectors --output-on-failure
```

Expected: PASS, 8 tests.

- [ ] **Step 7: Commit**

```bash
git add src/launchselectors.h src/launchselectors.cpp \
        tests/test_launchselectors.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -S -m "feat: parse the launch selectors as a value type

--account, --thread and --message, plus the payload that crosses the socket.
A value type with no GUI dependency: it is parsed before QApplication exists
and both halves need tests no window has to be built for.

QDataStream rather than a line-based payload, because a Message-ID may contain
a newline. Every read is status-checked, which is what catches a truncated
payload: a short read otherwise leaves the fields default-constructed and a
half-written id would be applied as an empty one.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01P3HQXLauwQgzxR4YfJBB3x"
```

---

## Task 3: SingleInstance, the socket

**Files:**
- Create: `src/singleinstance.h`
- Create: `src/singleinstance.cpp`
- Create: `tests/test_singleinstance.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_singleinstance.cpp`:

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

#include <QLocalServer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "launchselectors.h"
#include "singleinstance.h"

/// The socket half of item 200. Every case runs against a socket name of its
/// own inside a QTemporaryDir, so nothing here can reach a real running
/// qtmaildir, and two cases cannot collide.
class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void theFirstInstanceBecomesTheServer();
    void aSecondInstanceHandsOverItsSelectors();
    void aSecondInstanceWithNoSelectorsStillArrives();
    void aStaleSocketFileIsReclaimed();
    void anUncreatableSocketDoesNotStopStartup();

private:
    QTemporaryDir m_dir;
    QString m_socketPath;
};

void TestSingleInstance::init()
{
    QVERIFY(m_dir.isValid());
    // A name per test function, so a socket left behind by one case cannot
    // decide the next one's result.
    m_socketPath = m_dir.filePath(
        QStringLiteral("sock-%1").arg(QTest::currentTestFunction()));
}

void TestSingleInstance::theFirstInstanceBecomesTheServer()
{
    SingleInstance first(m_socketPath);
    QVERIFY(first.tryBecomeServer());
    QVERIFY(first.isServer());
}

void TestSingleInstance::aSecondInstanceHandsOverItsSelectors()
{
    // The whole point of the feature: the second process does not open a
    // window, it hands its request to the first and exits.
    SingleInstance first(m_socketPath);
    QVERIFY(first.tryBecomeServer());

    QSignalSpy arrived(&first, &SingleInstance::selectorsReceived);

    LaunchSelectors selectors;
    selectors.account = QStringLiteral("work");
    selectors.messageId = QStringLiteral("<abc@example.org>");

    SingleInstance second(m_socketPath);
    QVERIFY(!second.tryBecomeServer());
    QVERIFY(second.sendToRunningInstance(selectors));

    QTRY_VERIFY_WITH_TIMEOUT(arrived.count() == 1, 5000);
    const auto received =
        arrived.first().at(0).value<LaunchSelectors>();
    QCOMPARE(received.account, QStringLiteral("work"));
    QCOMPARE(received.messageId, QStringLiteral("<abc@example.org>"));
}

void TestSingleInstance::aSecondInstanceWithNoSelectorsStillArrives()
{
    // A bare `qtmaildir` against a running instance means "raise yourself".
    // That is a real request, so it must arrive rather than being dropped as
    // an empty message.
    SingleInstance first(m_socketPath);
    QVERIFY(first.tryBecomeServer());

    QSignalSpy arrived(&first, &SingleInstance::selectorsReceived);

    SingleInstance second(m_socketPath);
    QVERIFY(!second.tryBecomeServer());
    QVERIFY(second.sendToRunningInstance(LaunchSelectors()));

    QTRY_VERIFY_WITH_TIMEOUT(arrived.count() == 1, 5000);
    QVERIFY(arrived.first().at(0).value<LaunchSelectors>().isEmpty());
}

void TestSingleInstance::aStaleSocketFileIsReclaimed()
{
    // A crash or a kill leaves the socket file behind, and listen() then fails
    // with AddressInUse on a file nothing is serving. Without recovery the
    // application would never start again until someone deleted it by hand.
    //
    // The file is created by a server that is then destroyed WITHOUT removing
    // it, which QLocalServer does on an abrupt exit.
    // A plain file at the socket path, which is what a killed process leaves
    // behind: a filesystem entry with no process serving it. listen() then
    // fails with AddressInUse, and nothing answers a connection.
    QFile stale(m_socketPath);
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.close();
    QVERIFY(QFile::exists(m_socketPath));

    SingleInstance fresh(m_socketPath);
    QVERIFY2(fresh.tryBecomeServer(),
             "a stale socket file must not stop the application starting");
    QVERIFY(fresh.isServer());
}

void TestSingleInstance::anUncreatableSocketDoesNotStopStartup()
{
    // A read-only state directory must degrade to today's behaviour, a window
    // that opens and works, rather than to no mail client at all. The caller
    // reads isServer() as false and carries on.
    const QString impossible =
        m_dir.filePath(QStringLiteral("no/such/directory/sock"));

    SingleInstance instance(impossible);
    QVERIFY(!instance.tryBecomeServer());
    QVERIFY(!instance.isServer());
    // And it cannot reach a running instance either, since there is none.
    QVERIFY(!instance.sendToRunningInstance(LaunchSelectors()));
}

QTEST_MAIN(TestSingleInstance)
#include "test_singleinstance.moc"
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add to `tests/CMakeLists.txt`:

```cmake
add_qtmaildir_test(singleinstance)
```

Run:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

Expected: FAILS to compile, `singleinstance.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `src/singleinstance.h`:

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

#include <QObject>
#include <QString>

#include "launchselectors.h"

class QLocalServer;

/// Makes a launch either the running instance or a messenger to it (item 200).
///
/// **This is not network protocol work.** A QLocalServer is a unix domain
/// socket between two copies of this program, owned by the user, in the user's
/// own state directory. The rule in AGENTS.md is about IMAP and SMTP.
///
/// Knows nothing about queries, accounts or mail: it carries a LaunchSelectors
/// from one process to another and emits what arrived.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    /// \p socketPath is a filesystem path, so a test can point it inside a
    /// QTemporaryDir and never touch the user's real one.
    explicit SingleInstance(const QString &socketPath,
                            QObject *parent = nullptr);
    ~SingleInstance() override;

    /// Tries to become the instance others talk to.
    ///
    /// Returns true when this process is now listening, false when another
    /// instance already is OR when no socket could be created at all. The
    /// caller treats both falses the same way for the second case: **a socket
    /// that cannot be created must not stop the window opening**, or a
    /// read-only state directory costs the user their mail client.
    ///
    /// Handles the stale socket file, which is the ordinary aftermath of a
    /// crash: it attempts a CONNECTION first, and a refused connection on an
    /// existing file proves nothing is serving it, so the file is removed and
    /// the listen retried. Connecting first is what stops a live instance
    /// being removed out from under itself.
    bool tryBecomeServer();

    /// True when tryBecomeServer() succeeded and this process is listening.
    bool isServer() const;

    /// Sends \p selectors to the running instance. Returns false when there is
    /// none, or when the write could not be completed.
    ///
    /// An EMPTY selector set is still sent: a bare `qtmaildir` against a
    /// running window means "raise yourself", which is a request and not a
    /// no-op.
    bool sendToRunningInstance(const LaunchSelectors &selectors);

signals:
    /// A later launch handed these over. Emitted on the server side only.
    void selectorsReceived(const LaunchSelectors &selectors);

private:
    QString m_socketPath;
    QLocalServer *m_server = nullptr;
};
```

`Q_DECLARE_METATYPE(LaunchSelectors)` is **not** repeated here: it belongs in
`launchselectors.h`, which defines the type, matching `types.h` and
`threaddigest.h`. Declaring it in a consumer instead would leave every other
consumer without it.

- [ ] **Step 4: Write the implementation**

Create `src/singleinstance.cpp`:

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

#include "singleinstance.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>

namespace {

/// How long a client waits for the running instance, and how long the server
/// waits for a client's payload. Short: both processes are on this machine and
/// the peer is either there or it is not. A long wait would stall a launch
/// behind a wedged instance, which is worse than opening a window.
constexpr int kTimeoutMs = 2000;

}  // namespace

SingleInstance::SingleInstance(const QString &socketPath, QObject *parent)
    : QObject(parent), m_socketPath(socketPath)
{
    // Registered here rather than at a call site so any connection carrying
    // this type works, including a queued one a future caller might add. A
    // Q_DECLARE_METATYPE alone gives the type a metatype but does not register
    // it under the name a queued invoke resolves, which is the trap AGENTS.md
    // records for Q_ENUM.
    qRegisterMetaType<LaunchSelectors>("LaunchSelectors");
}

SingleInstance::~SingleInstance()
{
    if (m_server) {
        m_server->close();
        // Removes the filesystem entry, so an orderly exit leaves nothing for
        // the next launch to reclaim.
        QLocalServer::removeServer(m_socketPath);
    }
}

bool SingleInstance::isServer() const
{
    return m_server != nullptr && m_server->isListening();
}

bool SingleInstance::tryBecomeServer()
{
    if (isServer())
        return true;

    // CONNECT FIRST, and the order is the design. A successful connection means
    // a live instance owns this socket and this process is the messenger. A
    // refused connection on an EXISTING file means the file is stale, left by a
    // crash, and can be removed; doing it in this order is what stops a live
    // instance being removed out from under itself.
    {
        QLocalSocket probe;
        probe.connectToServer(m_socketPath);
        if (probe.waitForConnected(kTimeoutMs)) {
            probe.disconnectFromServer();
            return false;
        }
    }

    auto *server = new QLocalServer(this);
    // The socket is the user's own, in their own state directory. Nothing else
    // has any business connecting to it.
    server->setSocketOptions(QLocalServer::UserAccessOption);

    if (!server->listen(m_socketPath)) {
        if (server->serverError() == QAbstractSocket::AddressInUseError
            && QFileInfo::exists(m_socketPath)) {
            // Nothing answered the probe above, so this file is stale.
            QLocalServer::removeServer(m_socketPath);
            server->listen(m_socketPath);
        }
    }

    if (!server->isListening()) {
        // A read-only state directory, a filesystem that has no unix sockets,
        // or a path whose parent does not exist. Report and carry on: the
        // window must still open. Losing single-instance behaviour is a
        // degradation; losing the mail client is not acceptable.
        qWarning() << "qtmaildir: cannot create the single-instance socket at"
                   << m_socketPath << ":" << server->errorString()
                   << "- continuing without it";
        delete server;
        return false;
    }

    m_server = server;
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            // Deleted when the peer goes away, which it does immediately after
            // writing: the client's whole life is one payload.
            connect(socket, &QLocalSocket::disconnected,
                    socket, &QLocalSocket::deleteLater);

            if (!socket->waitForReadyRead(kTimeoutMs)) {
                socket->disconnectFromServer();
                continue;
            }

            // readAll() rather than a sized read: the payload is one short
            // write and the cap inside fromPayload() is what bounds it.
            const QByteArray payload = socket->readAll();
            QString error;
            const LaunchSelectors selectors =
                LaunchSelectors::fromPayload(payload, &error);
            if (!error.isEmpty()) {
                qWarning() << "qtmaildir: ignoring a launch payload:" << error;
                socket->disconnectFromServer();
                continue;
            }

            // Emitted even when empty: a bare launch means "raise yourself".
            emit selectorsReceived(selectors);
            socket->disconnectFromServer();
        }
    });

    return true;
}

bool SingleInstance::sendToRunningInstance(const LaunchSelectors &selectors)
{
    QLocalSocket socket;
    socket.connectToServer(m_socketPath);
    if (!socket.waitForConnected(kTimeoutMs))
        return false;

    socket.write(selectors.toPayload());
    // Flushed before returning, because the caller exits immediately
    // afterwards and an unflushed write would be lost with the process.
    if (!socket.waitForBytesWritten(kTimeoutMs))
        return false;

    socket.disconnectFromServer();
    return true;
}
```

- [ ] **Step 5: Add the source to the library**

In `src/CMakeLists.txt`, add `singleinstance.cpp` to the `qtmaildir_lib` source list.

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build build && ctest --test-dir build -R singleinstance --output-on-failure
```

Expected: PASS, 5 tests.

- [ ] **Step 7: Commit**

```bash
git add src/singleinstance.h src/singleinstance.cpp \
        tests/test_singleinstance.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -S -m "feat: add the single-instance socket

A QLocalServer under the state directory. The first launch listens; a later one
connects, hands over its selectors and exits.

Connect-first ordering, which is also how a stale socket file is detected: a
refused connection on an existing file proves nothing is serving it. Doing it
the other way round would remove a live instance's socket out from under it.

A socket that cannot be created does NOT stop the window opening. A read-only
state directory costs single-instance behaviour, which is a degradation; it
must not cost the user their mail client.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01P3HQXLauwQgzxR4YfJBB3x"
```

---

## Task 4: Resolving a Message-ID to its thread

**Files:**
- Modify: `src/notmuchworker.h:130-141`
- Modify: `src/notmuchworker.cpp` (beside `threadIdForTesting`, around line 764)
- Test: `tests/test_notmuchworker.cpp`

**Context you need:** `NotmuchWorker::threadIdForTesting(const QString &query)` already exists at `src/notmuchworker.cpp:764` and does exactly this lookup, but it is a plain method named for tests and explicitly documented as "Not a slot, so it cannot be reached across the thread boundary by accident". This task adds a real slot beside it rather than promoting it, because the slot must be asynchronous (it answers by signal) while the existing helper returns a value synchronously, and four tests already depend on the synchronous form.

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` list in `tests/test_notmuchworker.cpp`, after `loadMessageOnAnUnknownIdReturnsNothing()`:

```cpp
    void resolvingAMessageIdAnswersItsThreadId();
    void resolvingAnUnknownMessageIdAnswersEmpty();
    void resolvingAMessageIdQuotesTheId();
```

Add the bodies, after `TestNotmuchWorker::loadMessageOnAnUnknownIdReturnsNothing()`:

```cpp
void TestNotmuchWorker::resolvingAMessageIdAnswersItsThreadId()
{
    // What --message needs (item 200): the CLI knows a Message-ID and the
    // window needs the thread id, because opening the message means opening
    // its conversation with that message selected.
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy resolved(&worker, &NotmuchWorker::threadForMessageResolved);

    worker.resolveThreadForMessage(QStringLiteral("a2@example.org"));

    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.first().at(0).toString(), QStringLiteral("a2@example.org"));

    // a2 is a REPLY, so its thread id is the thread's, not its own. Asserted
    // against the thread a1 resolves to: the two must agree, which is the
    // whole point of resolving through the thread rather than the message.
    const QString threadId = resolved.first().at(1).toString();
    QVERIFY(!threadId.isEmpty());
    QCOMPARE(threadId,
             worker.threadIdForTesting(QStringLiteral("id:a1@example.org")));
}

void TestNotmuchWorker::resolvingAnUnknownMessageIdAnswersEmpty()
{
    // Answers rather than staying silent: the window shows the miss in the
    // status bar, and a slot that never replies would leave it waiting.
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy resolved(&worker, &NotmuchWorker::threadForMessageResolved);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.resolveThreadForMessage(QStringLiteral("nonexistent@example.org"));

    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.first().at(0).toString(),
             QStringLiteral("nonexistent@example.org"));
    QVERIFY(resolved.first().at(1).toString().isEmpty());

    // Not an error: a stale id from another program is an ordinary miss, the
    // same class as a stale row after a reindex.
    QCOMPARE(errors.count(), 0);
}

void TestNotmuchWorker::resolvingAMessageIdQuotesTheId()
{
    // The security-relevant case. This id comes from argv, not from notmuch,
    // and notmuch's parser rejects almost nothing: an unquoted id carrying
    // query syntax would be PARSED as syntax, matching something else or
    // nothing, with no error anywhere (searchterm.h:30-35).
    //
    // Asserted as a miss that stays a miss: the id cannot match, and it must
    // not blow up, error, or resolve to some unrelated thread.
    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy resolved(&worker, &NotmuchWorker::threadForMessageResolved);
    QSignalSpy errors(&worker, &NotmuchWorker::errorOccurred);

    worker.resolveThreadForMessage(
        QStringLiteral("a2@example.org\" or from:alice or \"x"));

    QCOMPARE(resolved.count(), 1);
    QVERIFY2(resolved.first().at(1).toString().isEmpty(),
             "an id carrying query syntax resolved to a thread: it was not quoted");
    QCOMPARE(errors.count(), 0);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAILS to compile, `'threadForMessageResolved' is not a member of 'NotmuchWorker'`.

- [ ] **Step 3: Declare the slot and the signal**

In `src/notmuchworker.h`, add to the `public slots:` section that begins at line 141, after the `loadMessage` declaration:

```cpp
    /// Answers which thread a Message-ID belongs to (item 200).
    ///
    /// For `--message`, which knows an id and needs the conversation: opening
    /// a message means opening its thread with that message selected, never an
    /// `id:` query showing one card out of a conversation (item 91).
    ///
    /// Answers with an EMPTY thread id when the message is unknown rather than
    /// staying silent, since the window reports the miss and a slot that never
    /// replies would leave it waiting forever.
    ///
    /// **The id is quoted before it reaches notmuch.** Unlike every other id in
    /// this class, this one came from argv rather than from notmuch itself, and
    /// notmuch parses garbage happily while matching nothing.
    void resolveThreadForMessage(const QString &messageId);
```

And to the `signals:` section, after `messageLoaded`:

```cpp
    /// The answer to resolveThreadForMessage(). The message id is echoed back
    /// so a caller can tell which request this answers; the thread id is empty
    /// when nothing matched.
    void threadForMessageResolved(const QString &messageId,
                                  const QString &threadId);
```

- [ ] **Step 4: Write the implementation**

In `src/notmuchworker.cpp`, add after `threadIdForTesting()` (which ends around line 786):

```cpp
void NotmuchWorker::resolveThreadForMessage(const QString &messageId)
{
    if (messageId.isEmpty()) {
        emit threadForMessageResolved(messageId, QString());
        return;
    }

    // SearchTerm::quote(), not the QStringLiteral("id:\"%1\"") this file uses
    // elsewhere. Every other id here came out of notmuch; this one came off the
    // command line of another program, so it is untrusted in the ordinary
    // sense. quote() escapes backslashes before quotes, which is the order that
    // matters, and caps the length.
    const QString term = SearchTerm::field(QStringLiteral("id"), messageId);
    if (term.isEmpty()) {
        emit threadForMessageResolved(messageId, QString());
        return;
    }

    // threadIdForTesting() is the same lookup and is deliberately NOT reused
    // by name: it is documented as not being a slot so it cannot cross the
    // thread boundary by accident, and renaming it would rewrite four existing
    // tests for no gain. The shared part is one query, which is small enough
    // that a helper would be more indirection than it saves.
    emit threadForMessageResolved(messageId, firstThreadIdMatching(term));
}
```

Rename the body of `threadIdForTesting` to a private helper both call. Replace the existing definition at line 764 with:

```cpp
QString NotmuchWorker::firstThreadIdMatching(const QString &query)
{
    if (!openReadOnly())
        return QString();

    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery)
        return QString();

    notmuch_threads_t *rawThreads = nullptr;
    if (notmuch_query_search_threads(nmQuery.get(), &rawThreads)
            != NOTMUCH_STATUS_SUCCESS) {
        return QString();
    }
    NmThreads threads(rawThreads);
    if (!notmuch_threads_valid(threads.get()))
        return QString();

    NmThread thread(notmuch_threads_get(threads.get()));
    if (!thread)
        return QString();
    return QString::fromUtf8(notmuch_thread_get_thread_id(thread.get()));
}

QString NotmuchWorker::threadIdForTesting(const QString &query)
{
    return firstThreadIdMatching(query);
}
```

Declare the helper in `src/notmuchworker.h`, in the `private:` section:

```cpp
    /// The first thread id matching \p query, or empty.
    ///
    /// Shared by threadIdForTesting() and resolveThreadForMessage(). The
    /// callers differ in what they do with it and in whether they are slots;
    /// the lookup is the same.
    QString firstThreadIdMatching(const QString &query);
```

Add the include at the top of `src/notmuchworker.cpp`, with the other project includes:

```cpp
#include "searchterm.h"
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build -R notmuchworker --output-on-failure
```

Expected: PASS. The whole `notmuchworker` suite, not only the three new cases — the refactor of `threadIdForTesting` touches four existing tests.

- [ ] **Step 6: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp tests/test_notmuchworker.cpp
git commit -S -m "feat: resolve a Message-ID to its thread id

For --message (item 200), which knows an id and needs the conversation:
opening a message means opening its thread with that message selected, never
an id: query showing one card out of a conversation (item 91).

The id is quoted through SearchTerm, unlike every other id in this class.
Those came out of notmuch; this one comes off another program's command line,
and notmuch parses garbage happily while matching nothing, so an unquoted id
carrying query syntax would be read AS syntax with no error anywhere.

threadIdForTesting() keeps its name and gains a shared helper rather than being
promoted: it is documented as not being a slot, and the new entry point has to
answer asynchronously.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01P3HQXLauwQgzxR4YfJBB3x"
```

---

## Task 5: MainWindow::applySelectors()

**Files:**
- Modify: `src/mainwindow.h` (public section around line 100, private slots around line 605)
- Modify: `src/mainwindow.cpp` (near `uiStatePath()` at line 92, the worker wiring at line 2681)
- Test: `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` list in `tests/test_mainwindow.cpp`, after `aGeneratedStartupQueryActuallyRuns()`:

```cpp
    void theSocketPathIsUnderTheStateDirectory();
    void anAccountSelectorMovesTheDropdown();
    void anUnknownAccountSelectorLeavesTheDropdownAlone();
    void aThreadSelectorOpensThatThread();
    void anEmptySelectorSetChangesNothing();
```

Add the bodies at the end of the file, before the `QTEST_MAIN` line:

```cpp
void TestMainWindow::theSocketPathIsUnderTheStateDirectory()
{
    // Beside uistate.conf, and built the same way: GenericStateLocation, not
    // StateLocation, because the latter appends both the organization and the
    // application name and both are "qtmaildir".
    const QString socket = MainWindow::singleInstanceSocketPath();
    const QString state = MainWindow::uiStatePath();

    QVERIFY(!socket.isEmpty());
    QCOMPARE(QFileInfo(socket).absolutePath(),
             QFileInfo(state).absolutePath());
    QVERIFY2(!socket.endsWith(QStringLiteral("/qtmaildir/qtmaildir")),
             "StateLocation was used: the path doubles the application name");
}

void TestMainWindow::anAccountSelectorMovesTheDropdown()
{
    // --account work, with the config's own startup account being something
    // else. The selector wins, which is what "open this account's view" means.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        // [general] keys are read WITHOUT the prefix: QSettings' INI backend
        // treats a section literally named [general] as its own fallback.
        s.setValue(QStringLiteral("startup_query"), QStringLiteral("Inbox"));
        s.setValue(QStringLiteral("startup_account"),
                   QStringLiteral("personal"));
        s.beginGroup(QStringLiteral("account.work"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("work"));
        s.endGroup();
        s.beginGroup(QStringLiteral("account.personal"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("personal"));
        s.endGroup();
        s.sync();
    }
    Config config;
    config.load(path);

    MainWindow window(config);
    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("personal"));

    LaunchSelectors selectors;
    selectors.account = QStringLiteral("work");
    window.applySelectors(selectors);

    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("work"));
}

void TestMainWindow::anUnknownAccountSelectorLeavesTheDropdownAlone()
{
    // The miss path. The window opens on its configured view and says so in
    // the status bar; it does not clear the dropdown, and it does not refuse
    // to start.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue(QStringLiteral("startup_account"), QStringLiteral("work"));
        s.beginGroup(QStringLiteral("account.work"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("work"));
        s.endGroup();
        s.sync();
    }
    Config config;
    config.load(path);

    MainWindow window(config);

    LaunchSelectors selectors;
    selectors.account = QStringLiteral("nosuchaccount");
    window.applySelectors(selectors);

    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("work"));

    // "statusMessage", which is the object name buildUi() sets at
    // mainwindow.cpp:674. There is no widget named "statusLabel", and
    // findChild would return null and assert nothing.
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);
    QVERIFY2(status->text().contains(QStringLiteral("nosuchaccount")),
             "the miss must name the value, so a stale caller can be debugged");
}

void TestMainWindow::aThreadSelectorOpensThatThread()
{
    // --thread, against a real database. The row the selector names is the row
    // that ends up current.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("one@example.org"),
        QStringLiteral("First subject"), QStringLiteral("a@example.org"),
        // Friday, verified with `date -d 2026-08-14 +%A`. Qt::RFC2822Date
        // validates the weekday against the date.
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body one.")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("two@example.org"),
        QStringLiteral("Second subject"), QStringLiteral("b@example.org"),
        // Saturday, verified with `date -d 2026-08-15 +%A`.
        QStringLiteral("Sat, 15 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body two.")));
    QVERIFY2(backed.build(), qPrintable(backed.error()));

    // The thread id is not knowable in advance, so it is read back from the
    // index the same way the CLI's caller would have obtained it.
    NotmuchWorker probe(backed.config().notmuchConfig());
    const QString threadId =
        probe.threadIdForTesting(QStringLiteral("id:two@example.org"));
    QVERIFY(!threadId.isEmpty());

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    LaunchSelectors selectors;
    selectors.threadId = threadId;
    window.applySelectors(selectors);

    // QTRY, never qWait: the worker is on another thread and a fixed sleep
    // passes when the result never arrives at all.
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    // Asserted on the thread the row STANDS FOR, through threadFor(), never
    // threadAt(index.row()): a tree numbers rows per parent.
    QTRY_VERIFY_WITH_TIMEOUT(view->currentIndex().isValid(), 15000);
    QCOMPARE(model->threadFor(view->currentIndex()).threadId, threadId);
}

void TestMainWindow::anEmptySelectorSetChangesNothing()
{
    // A bare `qtmaildir` against a running window means "raise yourself". It
    // must not re-run a query or move the selection: the user is looking at
    // something, and a raise is not a navigation.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue(QStringLiteral("startup_account"), QStringLiteral("work"));
        s.beginGroup(QStringLiteral("account.work"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("work"));
        s.endGroup();
        s.sync();
    }
    Config config;
    config.load(path);

    MainWindow window(config);
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:flagged"));

    window.applySelectors(LaunchSelectors());

    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("work"));
    QCOMPARE(queryEdit->text(), QStringLiteral("tag:flagged"));
}
```

Add the include at the top of `tests/test_mainwindow.cpp`, with the other project includes:

```cpp
#include "launchselectors.h"
```

Nothing else needs adding: `QLabel`, `QFileInfo` (via `QDir`/`QtTest`), `threadlistmodel.h` and `threadlistview.h` are already included in that file.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAILS to compile, `'applySelectors' is not a member of 'MainWindow'`.

- [ ] **Step 3: Declare the new members**

In `src/mainwindow.h`, add to the `public:` section, right after the `uiStatePath()` declaration (around line 165):

```cpp
    /// Path of the single-instance socket (item 200).
    ///
    /// Beside uiStatePath() and built the same way, so the two cannot drift.
    /// GenericStateLocation, not StateLocation: the latter appends both the
    /// organization and the application name, and both are "qtmaildir".
    static QString singleInstanceSocketPath();
```

And to the `public:` section around line 100, after `pendingChangeSnapshot()`:

```cpp
    /// Applies what a launch asked for (item 200).
    ///
    /// ONE entry point, called both by main() at startup and by the socket
    /// handler when a later launch arrives. Two paths through separate code
    /// would drift, which is the lesson this file has already learned from
    /// every other pair.
    ///
    /// An EMPTY selector set deliberately changes nothing: a bare launch
    /// against a running window means "raise yourself", and a raise is not a
    /// navigation. The user is looking at something.
    ///
    /// A selector that matches nothing leaves the window on its configured
    /// view and names the miss in the status bar. Not an empty result, which
    /// makes a stale link look like a broken client; not a refusal, which is
    /// right for a script and wrong for a desktop launch.
    void applySelectors(const LaunchSelectors &selectors);
```

Add the include at the top of `src/mainwindow.h`:

```cpp
#include "launchselectors.h"
```

And in the `private slots:` section, beside `recoverStaleThread`:

```cpp
    /// The worker's answer to a --message selector.
    void onThreadForMessageResolved(const QString &messageId,
                                    const QString &threadId);
```

- [ ] **Step 4: Write the implementation**

In `src/mainwindow.cpp`, add after `uiStatePath()` (which ends at line 101):

```cpp
QString MainWindow::singleInstanceSocketPath()
{
    // Built exactly like uiStatePath(), including the GenericStateLocation
    // choice and the reason for it. A socket is machine-written state, so it
    // belongs beside the UI state and never in the hand-edited config
    // directory.
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    return base + QStringLiteral("/qtmaildir/qtmaildir.sock");
}
```

Add the method body, near the other public methods:

```cpp
void MainWindow::applySelectors(const LaunchSelectors &selectors)
{
    // Nothing asked for. A bare launch against a running window means "raise
    // yourself", which main() and the socket handler do around this call; from
    // here there is nothing to change, and re-running a query would take the
    // user off whatever they were reading.
    if (selectors.isEmpty())
        return;

    // The account FIRST, and the order matters: a built-in filter composes
    // with the dropdown, so a query run before the account moved would carry
    // the old scope. This is the same ordering the startup path uses.
    if (!selectors.account.isEmpty()) {
        const int index = m_accountBox->findData(selectors.account);
        if (index >= 0) {
            m_accountBox->setCurrentIndex(index);
        } else {
            // Named, so a caller passing a stale key can be debugged from the
            // client rather than from the caller.
            showTransientStatus(
                tr("No account named '%1'.").arg(selectors.account));
        }
    }

    // A message id names a message INSIDE a conversation, so it has to be
    // resolved to its thread before anything can be opened. Asked of the
    // worker, which owns the only database handle; the answer arrives in
    // onThreadForMessageResolved().
    if (!selectors.messageId.isEmpty()) {
        if (m_worker) {
            QMetaObject::invokeMethod(
                m_worker, "resolveThreadForMessage", Qt::QueuedConnection,
                Q_ARG(QString, selectors.messageId));
        }
        // The thread selector, if any, is deliberately NOT also applied here:
        // the message's own thread is what will open, and running a second
        // query underneath it would race the one the resolve is about to
        // start.
        return;
    }

    if (!selectors.threadId.isEmpty()) {
        // recoverStaleThread() is reused whole. It runs thread:<id>, remembers
        // the target across the two queued round trips the load takes, expands
        // the thread when its row arrives and selects the message once the
        // replies land. Item 91's double-click already reuses it; this is the
        // third caller.
        //
        // The empty message id is meaningful to it: land on the ROOT row,
        // which is the thread's first message.
        recoverStaleThread(selectors.threadId, QString());
    }
}

void MainWindow::onThreadForMessageResolved(const QString &messageId,
                                            const QString &threadId)
{
    if (threadId.isEmpty()) {
        // The miss path, and the window stays where it is. A message id from
        // another program can be stale for every ordinary reason: the mail was
        // deleted, moved by another client, or never indexed here.
        showTransientStatus(tr("No message matched '%1'.").arg(messageId));
        return;
    }

    // The THREAD, with that message selected. An id: query on the message
    // alone would show one card out of its conversation, which item 91 settled
    // is the wrong reading of "open this message".
    recoverStaleThread(threadId, messageId);
}
```

In `wireWorker()`, add beside the other worker connections (after the `messageLoaded` connect at line 2691):

```cpp
    connect(m_worker, &NotmuchWorker::threadForMessageResolved,
            this, &MainWindow::onThreadForMessageResolved);
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build -R mainwindow --output-on-failure
```

Expected: PASS. The whole `mainwindow` suite, not only the five new cases.

- [ ] **Step 6: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp tests/test_mainwindow.cpp
git commit -S -m "feat: apply the launch selectors to the window

One entry point, called both at startup and by the socket handler when a later
launch arrives. Two paths would drift, which is the lesson this file has
already learned from every other pair.

The account moves first, because a built-in filter composes with the dropdown
and a query run before it would carry the old scope. The thread case reuses
recoverStaleThread() whole, as item 91's double-click already does. A message
id resolves to its thread first: opening a message means opening its
conversation with that message selected.

An empty selector set changes nothing. A bare launch against a running window
means raise yourself, and a raise is not a navigation.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01P3HQXLauwQgzxR4YfJBB3x"
```

---

## Task 6: Wiring it into main()

**Files:**
- Modify: `src/main.cpp:38-66` and `src/main.cpp:138-152`

**No test for this task.** `main.cpp` is not compiled into `qtmaildir_lib` (see `src/CMakeLists.txt:67`, only the executable compiles it), so no test binary can reach it. That is why Tasks 2, 3 and 5 put every decidable thing in the library: what is left here is wiring, and it is verified by the hand test in Task 8.

- [ ] **Step 1: Replace the argument loop**

In `src/main.cpp`, replace the whole `for (int i = 1; i < argc; ++i)` block (lines 44-66) with:

```cpp
    // Answered before anything heavier starts: registering web engine schemes
    // and constructing a QApplication to print one line would be absurd, and
    // --version has to work on a machine where the GUI cannot open at all.
    //
    // Still hand-checked rather than left to QCommandLineParser: Qt's own
    // addVersionOption()/addHelpOption() exit through QCoreApplication, which
    // does not exist yet at this point.
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0
            || std::strcmp(argv[i], "-v") == 0) {
            std::printf("qtmaildir %s\n", QTMAILDIR_VERSION_DISPLAY);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0
            || std::strcmp(argv[i], "-h") == 0) {
            // The text lives with the parser, so the options and their
            // descriptions cannot drift apart.
            std::printf("%s",
                        LaunchSelectors::helpText(
                            QStringLiteral(QTMAILDIR_VERSION_DISPLAY))
                            .toLocal8Bit()
                            .constData());
            return 0;
        }
    }

    QString selectorError;
    const LaunchSelectors selectors =
        LaunchSelectors::parse(arguments, &selectorError);
    if (!selectorError.isEmpty()) {
        std::fprintf(stderr, "qtmaildir: %s\n",
                     selectorError.toLocal8Bit().constData());
        return 2;
    }
```

Add the includes at the top of `src/main.cpp`, with the other project includes:

```cpp
#include "launchselectors.h"
#include "singleinstance.h"
```

- [ ] **Step 2: Add the connect-or-listen step**

In `src/main.cpp`, immediately after the `QApplication app(argc, argv);` block and its `setApplicationName`/`setOrganizationName`/`setApplicationVersion` calls (which end around line 83), insert:

```cpp
    // Connect first, become the server only if that fails. A live instance is
    // handed the selectors and this process exits without ever opening a
    // database: notmuch permits one handle per process, so two windows are two
    // handles, which this avoids as a side effect of the feature.
    //
    // On its own stack frame in main(), like the QTranslator below: it owns the
    // socket for the life of the process and must outlive exec().
    SingleInstance instance(MainWindow::singleInstanceSocketPath());
    if (!instance.tryBecomeServer()) {
        if (instance.sendToRunningInstance(selectors))
            return 0;
        // No running instance answered and no socket could be created either.
        // Carry on and open a window: losing single-instance behaviour is a
        // degradation, and losing the mail client is not acceptable.
    }
```

- [ ] **Step 3: Apply the selectors and handle later launches**

In `src/main.cpp`, replace the `MainWindow window(config); window.show();` pair and what follows (lines 138-152) with:

```cpp
    MainWindow window(config);
    window.show();

    // What this launch asked for. After show(), so the window is up before a
    // query starts running against it.
    window.applySelectors(selectors);

    // A later launch. The selectors arrive on the socket and go through the
    // same applySelectors() this startup path just used.
    QObject::connect(&instance, &SingleInstance::selectorsReceived, &window,
                     [&window](const LaunchSelectors &arrived) {
                         // Raised whatever the selectors say, an empty set
                         // included: a bare launch against a running window
                         // means "show me the window".
                         //
                         // Under Wayland this is a REQUEST, not a command. The
                         // compositor may honour it as a focus hint or ignore
                         // it by policy, which is its decision and not a defect
                         // to work around: the selectors still apply and the
                         // window still shows the right thing.
                         window.setWindowState(window.windowState()
                                               & ~Qt::WindowMinimized);
                         window.show();
                         window.raise();
                         window.activateWindow();
                         window.applySelectors(arrived);
                     });

    // After show(), and out here rather than inside the constructor. A modal
    // raised from the constructor cannot be dismissed under the offscreen
    // platform, so it hung the test suite with no output (item 84). Showing it
    // here also gives the dialog a visible parent to sit on.
    const QStringList problems = window.configProblems();
    if (!problems.isEmpty()) {
        QMessageBox::warning(&window, QObject::tr("Configuration problems"),
                             problems.join(QLatin1Char('\n')));
    }

    return app.exec();
```

- [ ] **Step 4: Build and run the whole suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: every test passes. A failure in a suite this task did not touch means the new library sources broke something; fix it before committing.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -S -m "feat: accept the launch selectors on the command line

Connect first, become the server only if that fails. A live instance is handed
the selectors and this process exits without ever opening a database, so two
launches no longer mean two notmuch handles.

--version and --help stay hand-checked rather than going through
QCommandLineParser: Qt's own versions exit through QCoreApplication, which does
not exist at that point, and --version has to work where the GUI cannot open.

Raising under Wayland is a request rather than a command. The compositor may
honour it as a focus hint or ignore it by policy; the selectors apply either
way, which is the half that has to work.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01P3HQXLauwQgzxR4YfJBB3x"
```

---

## Task 7: Translations and documentation

**Files:**
- Modify: `translations/qtmaildir_it_IT.ts`
- Modify: `README.md` (a new section after `## Requirements`, line 30)
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Refresh the translation source**

```bash
lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete -locations none
```

Expected: a clean run reporting **zero context warnings**. A warning saying `tr() cannot be called without context` means a literal needs `QT_TRANSLATE_NOOP("TheClass", "Text")` rather than a bare `tr()`; the strings added by this plan are all inside classes or use `QCoreApplication::translate` with an explicit context, so a warning here is a real defect to fix.

- [ ] **Step 2: Translate the new strings**

Fill in every `<translation type="unfinished">` that this change introduced. The new strings and their Italian:

| Source | Italian |
|---|---|
| `Open this account's view.` | `Apre la vista di questo account.` |
| `Open this thread.` | `Apre questa conversazione.` |
| `Open this message, inside its thread.` | `Apre questo messaggio, nella sua conversazione.` |
| `Unknown option: %1` | `Opzione sconosciuta: %1` |
| `Launch payload too large` | `Dati di avvio troppo grandi` |
| `Unrecognised launch payload` | `Dati di avvio non riconosciuti` |
| `Truncated launch payload` | `Dati di avvio troncati` |
| `No account named '%1'.` | `Nessun account chiamato '%1'.` |
| `No message matched '%1'.` | `Nessun messaggio corrisponde a '%1'.` |

The `helpText()` block is one long string; translate the prose lines and **leave the option names (`--account`, `--thread`, `--message`, `-h`, `-v`) exactly as they are**. They are wire format: a translated option name is an option the user cannot type.

- [ ] **Step 3: Verify the translations compile and the suite agrees**

```bash
cmake --build build && ctest --test-dir build -R translations --output-on-failure
```

Expected: PASS. `lrelease` must report **0 unfinished**: it silently DROPS an unfinished string and ships it as English inside an otherwise Italian UI.

- [ ] **Step 4: Add the README section**

In `README.md`, insert a new section after `## Requirements` (which begins at line 30) and before `## Building`:

```markdown
## Usage

```
qtmaildir [options]

  -h, --help         Show this help and exit
  -v, --version      Show the version and exit
  --account <key>    Open this account's view
  --thread <id>      Open this thread
  --message <id>     Open this message, inside its thread
```

The three selectors combine: `--account work --message '<abc@example.org>'`
opens that message in the work account's view.

**A second launch does not open a second window.** When qtmaildir is already
running, a launch hands its selectors to the running window, asks it to raise
itself, and exits. This is what lets another program, a notification or a
script open a particular message in the client the user already has open. It
also means one process, and so one notmuch database handle.

Under a Wayland compositor, raising a window is a request rather than a
command: the compositor may honour it, or apply its own focus policy. The
selectors are applied either way.

A selector that matches nothing, a stale thread id or an account key that is
not configured, leaves the window on its normal startup view and says what
missed in the status bar. It is never a reason to refuse to start.
```

- [ ] **Step 5: Add the changelog entry**

In `CHANGELOG.md`, under `## [Unreleased]`, add:

```markdown
### Added

- `--account`, `--thread` and `--message` on the command line, so another
  program can open qtmaildir at a particular account's view, conversation or
  message. The three combine.
- A second launch now hands its selectors to the already-running window and
  asks it to raise itself, rather than opening a second window. One process,
  and so one notmuch database handle.
```

- [ ] **Step 6: Commit**

```bash
git add translations/qtmaildir_it_IT.ts README.md CHANGELOG.md
git commit -S -m "docs: document the launch selectors and translate them

The option names stay untranslated in the Italian help text: they are wire
format, and a translated option name is an option the user cannot type.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01P3HQXLauwQgzxR4YfJBB3x"
```

---

## Task 8: Hand over for the hand test

**Files:** none.

Three properties of this feature cannot be tested here, for reasons `AGENTS.md` records, and they are handed to the user rather than covered by a test that would pass whatever the code does.

- [ ] **Step 1: Run the full suite one more time**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. Report the actual count rather than asserting success.

- [ ] **Step 2: Verify the clean version string**

```bash
cmake -S . -B /tmp/qtmaildir-clean -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DQTMAILDIR_BUILD_NUMBER=OFF && cmake --build /tmp/qtmaildir-clean
/tmp/qtmaildir-clean/src/qtmaildir --version
```

Expected: a bare `qtmaildir X.Y.Z`, with no build number. This confirms `--version` still answers before `QApplication` exists, which is the one property the parser rewrite could quietly have broken.

Note this is `--version` only, which prints and exits. **Do not launch the GUI.**

- [ ] **Step 3: Hand it over**

Tell the user the branch is ready and what to look at. Do not launch the application; running it is theirs.

What to ask them to check:

1. **With qtmaildir closed**, run `qtmaildir --account <one of their keys>` and confirm the window opens on that account's view.
2. **With qtmaildir already open**, run the same command from another terminal and confirm **no second window appears**, the existing window switches account, and it comes to the front. Whether it takes focus is Hyprland's decision; the switch is the part that must work.
3. **A stale selector**: `qtmaildir --thread 0000000000000000` against the running window. Expect the normal view and a status-bar line naming the id.
4. **`qtmaildir --message '<some real Message-ID>'`**, taken from a message they can see, and confirm the conversation opens with that message selected rather than one card on its own.
5. **`qtmaildir --nonsense`**, and confirm it prints the error and exits 2 rather than opening a window.

---

## Self-review notes

**Spec coverage.** Every section of `2026-09-13-cli-selectors-design.md` maps to a task: the command line to Task 2, single instance to Task 3, the Message-ID round trip to Task 4, applying the selectors and the miss path to Task 5, raising and the startup order to Task 6, translations/docs to Task 7, and the hand test to Task 8. The `Qt6::Network` constraint is Task 1.

**One deviation from the spec, deliberately.** The spec says `QCommandLineParser` "replaces the `strcmp` loop". It replaces it for the three selectors; `--version` and `--help` keep their hand-check, because Qt's `addVersionOption()`/`addHelpOption()` exit through `QCoreApplication`, which does not exist at that point in `main()`. The spec's real constraint, that those two must answer without a `QApplication`, is met.

**One thing the spec did not know.** `NotmuchWorker::threadIdForTesting()` already performs the Message-ID lookup synchronously and has four existing callers. Task 4 adds a slot beside it over a shared private helper rather than promoting it, because the new entry point must answer by signal while the existing one returns a value.
