# qtmaildir v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Qt6 desktop mail client that reads and organizes a local notmuch-indexed Maildir, with correct HTML mail rendering and no network protocol code.

**Architecture:** One process, two threads. A `NotmuchWorker` on a dedicated thread owns the only `notmuch_database_t*` and is the only translation unit that includes `notmuch.h`; it communicates with the UI exclusively through queued signals carrying plain value structs. The UI thread runs Qt Widgets with a `QAbstractTableModel` fed in batches, and renders message bodies through a locked-down `QWebEngineView` whose request interceptor denies every request by default.

**Tech Stack:** C++17, Qt6 (Widgets, WebEngineWidgets, Test), libnotmuch 0.39, GMime 3.0, CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-08-02-qtmaildir-design.md`

---

## Environment notes (verified 2026-08-02)

Read these before Task 1; they explain build choices that are otherwise surprising.

- **Qt 6.11.1**, including WebEngine, ships inside Slackware's monolithic `qt6` package. There is no separate `qt6-webengine` package to install.
- **notmuch installs no `notmuch.pc`.** Verified absent on disk, absent from the package file list, and `pkg-config --exists notmuch` fails. This is upstream behaviour. CMake must use `find_path`/`find_library`, never `pkg_check_modules`, for notmuch.
- **GMime 3.2.15** does ship `gmime-3.0.pc`, so it uses `pkg_check_modules`.
- **CMake is 4.3.4**, which rejects `cmake_minimum_required(VERSION <3.5)`. Use 3.21.
- Toolchain: GCC 15.3.0, Ninja 1.13.2.

Build and test commands used throughout:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run a single test binary directly for a tighter loop, e.g. `./build/tests/test_keymap`.

**Commits must be GPG-signed** (`git commit -S`). Never disable signing. If pinentry times out because the machine was unattended, simply re-run the same command.

---

## File Structure

Created over the course of the plan. Each file has one responsibility.

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Top-level build: dependency discovery, options. |
| `src/CMakeLists.txt` | Application target. |
| `tests/CMakeLists.txt` | Test targets. |
| `src/types.h` | Plain value structs crossing the thread boundary. No logic. |
| `src/keymap.{h,cpp}` | Key sequence to action-name mapping; defaults plus INI overrides. |
| `src/config.{h,cpp}` | INI load/save: accounts, saved queries, sync command, keys. |
| `src/mimeparser.{h,cpp}` | Message file to parts, bodies, attachments (GMime). Includes safe attachment-name resolution. |
| `src/nmraii.h` | RAII wrappers for libnotmuch C handles. Header-only. |
| `src/notmuchworker.{h,cpp}` | The only file including `notmuch.h`. Queries and tag mutations. |
| `src/threadlistmodel.{h,cpp}` | `QAbstractTableModel` over `ThreadSummary`, batch append. |
| `src/requestinterceptor.{h,cpp}` | `QWebEngineUrlRequestInterceptor`: deny-by-default policy. |
| `src/cidschemehandler.{h,cpp}` | Serves `cid:` parts of the current message only. |
| `src/htmlbuilder.{h,cpp}` | Turns a parsed message into the HTML string the web view loads. |
| `src/messageview.{h,cpp}` | Message pane widget: headers, web view, attachment bar. |
| `src/mailsync.{h,cpp}` | `QProcess` wrapper around the configured sync command. |
| `src/mainwindow.{h,cpp}` | Wiring only: layout, signal connections, action registration. |
| `src/main.cpp` | Entry point, profile setup, startup checks. |
| `tests/test_keymap.cpp` | Keymap defaults, overrides, chords, unknown actions. |
| `tests/test_config.cpp` | INI parsing, account round-trip, missing-field handling. |
| `tests/test_mimeparser.cpp` | Part selection, decoding, attachments, filename safety. |
| `tests/test_interceptor.cpp` | The security-critical deny-by-default assertions. |
| `tests/fixtures/*.eml` | Hand-written message fixtures. |

Build order is dependency order: pure-logic units (keymap, config, mimeparser, interceptor) come first and are fully tested, then the notmuch layer, then the UI that wires them together.

---

## Task 1: Project skeleton and build system

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `src/main.cpp`

- [ ] **Step 1: Write the top-level CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.21)
project(qtmaildir VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets WebEngineWidgets Test)

# notmuch ships no pkg-config file; locate it by hand.
find_path(NOTMUCH_INCLUDE_DIR notmuch.h)
find_library(NOTMUCH_LIBRARY NAMES notmuch)
if(NOT NOTMUCH_INCLUDE_DIR OR NOT NOTMUCH_LIBRARY)
    message(FATAL_ERROR
        "libnotmuch not found. Need notmuch.h and libnotmuch on the system.")
endif()
message(STATUS "Found notmuch: ${NOTMUCH_LIBRARY}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GMIME REQUIRED IMPORTED_TARGET gmime-3.0)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
```

- [ ] **Step 2: Write src/CMakeLists.txt**

The application logic lives in a static library so tests can link it without
duplicating source lists. Only `main.cpp` is in the executable.

```cmake
add_library(qtmaildir_lib STATIC
    main_placeholder.cpp
)

target_include_directories(qtmaildir_lib
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${NOTMUCH_INCLUDE_DIR})

target_link_libraries(qtmaildir_lib
    PUBLIC Qt6::Widgets Qt6::WebEngineWidgets PkgConfig::GMIME ${NOTMUCH_LIBRARY})

add_executable(qtmaildir main.cpp)
target_link_libraries(qtmaildir PRIVATE qtmaildir_lib)

install(TARGETS qtmaildir RUNTIME DESTINATION bin)
```

- [ ] **Step 3: Create the placeholder translation unit**

`add_library` needs at least one source. Create `src/main_placeholder.cpp`
containing exactly this; Task 2 replaces it with the first real source.

```cpp
// Placeholder so the library target has a source file before real code lands.
// Removed in Task 2.
namespace { int qtmaildir_placeholder = 0; }
```

- [ ] **Step 4: Write a minimal src/main.cpp**

```cpp
#include <QApplication>
#include <QLabel>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QLabel label(QStringLiteral("qtmaildir"));
    label.show();
    return app.exec();
}
```

- [ ] **Step 5: Write tests/CMakeLists.txt**

Empty for now except the helper function later tasks call.

```cmake
# add_qtmaildir_test(<name>) builds tests/test_<name>.cpp and registers it.
function(add_qtmaildir_test name)
    add_executable(test_${name} test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE qtmaildir_lib Qt6::Test)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()
```

- [ ] **Step 6: Configure and build**

Run:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```
Expected: configure prints `Found notmuch: /usr/lib64/libnotmuch.so`, build
succeeds, `./build/src/qtmaildir` exists.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/ tests/
git commit -S -m "build: add CMake skeleton and dependency discovery"
```

---

## Task 2: KeyMap — defaults, INI overrides, chords

Start here because it is pure logic with no dependencies, so it proves the
test harness works before anything harder lands.

**Files:**
- Create: `src/keymap.h`, `src/keymap.cpp`
- Create: `tests/test_keymap.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`
- Delete: `src/main_placeholder.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/test_keymap.cpp`:

```cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include "keymap.h"

class TestKeyMap : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAreLoaded();
    void iniOverridesDefault();
    void iniAddsNewBinding();
    void chordSequenceParses();
    void unknownActionIsReported();
    void invalidSequenceIsReported();
};

void TestKeyMap::defaultsAreLoaded()
{
    KeyMap map;
    map.loadDefaults();
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("j"))),
             QStringLiteral("next_thread"));
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("a"))),
             QStringLiteral("archive"));
}

void TestKeyMap::iniOverridesDefault()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("j"), QStringLiteral("archive"));
        s.endGroup();
    }

    KeyMap map;
    map.loadDefaults();
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("j"))),
             QStringLiteral("archive"));
    // An untouched default survives.
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("k"))),
             QStringLiteral("prev_thread"));
}

void TestKeyMap::iniAddsNewBinding()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("Ctrl+Shift+A"), QStringLiteral("archive"));
        s.endGroup();
    }

    KeyMap map;
    map.loadDefaults();
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("Ctrl+Shift+A"))),
             QStringLiteral("archive"));
}

void TestKeyMap::chordSequenceParses()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("g,i"), QStringLiteral("focus_query"));
        s.endGroup();
    }

    KeyMap map;
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    const QKeySequence chord = QKeySequence::fromString(QStringLiteral("g,i"));
    QCOMPARE(chord.count(), 2);
    QCOMPARE(map.actionFor(chord), QStringLiteral("focus_query"));
}

void TestKeyMap::unknownActionIsReported()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("z"), QStringLiteral("no_such_action"));
        s.endGroup();
    }

    KeyMap map;
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    // Reported, not fatal, and not bound.
    QCOMPARE(map.warnings().size(), 1);
    QVERIFY(map.warnings().first().contains(QStringLiteral("no_such_action")));
    QVERIFY(map.actionFor(QKeySequence(QStringLiteral("z"))).isEmpty());
}

void TestKeyMap::invalidSequenceIsReported()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("NotAKey++"), QStringLiteral("archive"));
        s.endGroup();
    }

    KeyMap map;
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    QCOMPARE(map.warnings().size(), 1);
}

QTEST_MAIN(TestKeyMap)
#include "test_keymap.moc"
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Append to `tests/CMakeLists.txt`:

```cmake
add_qtmaildir_test(keymap)
```

Run:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```
Expected: FAIL at compile time, `keymap.h: No such file or directory`.

- [ ] **Step 3: Write src/keymap.h**

```cpp
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QStringList>

class QSettings;

/// Maps key sequences to action names. Action names are plain strings so this
/// class has no dependency on the widgets that implement the actions.
class KeyMap
{
public:
    /// Every action name the application understands. loadOverrides() rejects
    /// anything not in this set, so a typo in the config cannot bind silently.
    static QStringList knownActions();

    void loadDefaults();

    /// Reads the [keys] group. Invalid sequences and unknown action names are
    /// collected into warnings() rather than throwing or aborting.
    void loadOverrides(QSettings &settings);

    /// Empty string when nothing is bound.
    QString actionFor(const QKeySequence &sequence) const;

    QStringList warnings() const { return m_warnings; }

private:
    QHash<QKeySequence, QString> m_bindings;
    QStringList m_warnings;
};
```

- [ ] **Step 4: Write src/keymap.cpp**

```cpp
#include "keymap.h"

#include <QSettings>

QStringList KeyMap::knownActions()
{
    // Keep in sync with the actions MainWindow registers.
    return {
        QStringLiteral("next_thread"),
        QStringLiteral("prev_thread"),
        QStringLiteral("open_thread"),
        QStringLiteral("archive"),
        QStringLiteral("delete"),
        QStringLiteral("spam"),
        QStringLiteral("toggle_unread"),
        QStringLiteral("flag"),
        QStringLiteral("focus_query"),
        QStringLiteral("toggle_html"),
        QStringLiteral("load_remote"),
        QStringLiteral("undo"),
        QStringLiteral("sync"),
        QStringLiteral("quit"),
    };
}

void KeyMap::loadDefaults()
{
    const QHash<QString, QString> defaults = {
        { QStringLiteral("j"),      QStringLiteral("next_thread") },
        { QStringLiteral("k"),      QStringLiteral("prev_thread") },
        { QStringLiteral("Return"), QStringLiteral("open_thread") },
        { QStringLiteral("a"),      QStringLiteral("archive") },
        { QStringLiteral("d"),      QStringLiteral("delete") },
        { QStringLiteral("N"),      QStringLiteral("toggle_unread") },
        { QStringLiteral("F"),      QStringLiteral("flag") },
        { QStringLiteral("/"),      QStringLiteral("focus_query") },
        { QStringLiteral("h"),      QStringLiteral("toggle_html") },
        { QStringLiteral("u"),      QStringLiteral("undo") },
        { QStringLiteral("G"),      QStringLiteral("sync") },
        { QStringLiteral("Ctrl+Q"), QStringLiteral("quit") },
    };

    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
        m_bindings.insert(QKeySequence::fromString(it.key()), it.value());
}

void KeyMap::loadOverrides(QSettings &settings)
{
    const QStringList known = knownActions();

    settings.beginGroup(QStringLiteral("keys"));
    const QStringList keys = settings.childKeys();
    for (const QString &key : keys) {
        const QString action = settings.value(key).toString();

        const QKeySequence sequence = QKeySequence::fromString(key);
        if (sequence.isEmpty()) {
            m_warnings.append(
                QStringLiteral("Unparseable key sequence '%1' in [keys]").arg(key));
            continue;
        }

        if (!known.contains(action)) {
            m_warnings.append(
                QStringLiteral("Unknown action '%1' bound to '%2' in [keys]")
                    .arg(action, key));
            continue;
        }

        m_bindings.insert(sequence, action);
    }
    settings.endGroup();
}

QString KeyMap::actionFor(const QKeySequence &sequence) const
{
    return m_bindings.value(sequence);
}
```

- [ ] **Step 5: Add sources to the library and drop the placeholder**

Edit `src/CMakeLists.txt`, replacing the `add_library` call:

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
)
```

Then delete the placeholder:
```bash
rm src/main_placeholder.cpp
```

- [ ] **Step 6: Run tests to verify they pass**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: `test_keymap` PASSes, 6 test functions, 0 failed.

- [ ] **Step 7: Commit**

```bash
git add src/keymap.h src/keymap.cpp src/CMakeLists.txt tests/
git rm --cached src/main_placeholder.cpp 2>/dev/null || true
git commit -S -m "feat: add KeyMap with defaults and INI overrides"
```

---

## Task 3: Config — accounts, queries, sync command

**Files:**
- Create: `src/config.h`, `src/config.cpp`
- Create: `tests/test_config.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_config.cpp`:

```cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include "config.h"

class TestConfig : public QObject
{
    Q_OBJECT
private slots:
    void parsesAccounts();
    void parsesSavedQueries();
    void missingSyncCommandIsEmpty();
    void accountWithoutMaildirIsRejected();
    void scopedQueryWrapsCorrectly();
};

static QString writeIni(const QTemporaryDir &dir, const QString &body)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(body.toUtf8());
    f.close();
    return path;
}

void TestConfig::parsesAccounts()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
        "drafts=Drafts\n"
        "\n"
        "[account.personal]\n"
        "name=Test User\n"
        "address=me@example.net\n"
        "maildir=personal\n"
    ));

    Config config;
    config.load(path);

    QCOMPARE(config.accounts().size(), 2);

    const Account work = config.account(QStringLiteral("work"));
    QCOMPARE(work.key, QStringLiteral("work"));
    QCOMPARE(work.name, QStringLiteral("Test User"));
    QCOMPARE(work.address, QStringLiteral("user@example.org"));
    QCOMPARE(work.maildir, QStringLiteral("work-mail"));
    QCOMPARE(work.drafts, QStringLiteral("Drafts"));

    // drafts is optional in v1 (send is v2).
    const Account personal = config.account(QStringLiteral("personal"));
    QVERIFY(personal.drafts.isEmpty());
    QVERIFY(personal.isValid());
}

void TestConfig::parsesSavedQueries()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[queries]\n"
        "Inbox=tag:inbox\n"
        "Unread=tag:unread\n"
    ));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 2);
    // Order follows the file, so the UI button order is predictable.
    QCOMPARE(queries.at(0).name, QStringLiteral("Inbox"));
    QCOMPARE(queries.at(0).query, QStringLiteral("tag:inbox"));
}

void TestConfig::missingSyncCommandIsEmpty()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[general]\n"));

    Config config;
    config.load(path);

    QVERIFY(config.syncCommand().isEmpty());
    // The UI uses this to disable the Sync button with a tooltip.
    QVERIFY(!config.warnings().isEmpty());
}

void TestConfig::accountWithoutMaildirIsRejected()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.broken]\n"
        "name=No Maildir\n"
        "address=x@example.org\n"
    ));

    Config config;
    config.load(path);

    // Rejected, reported, and not offered to the user as a scope.
    QCOMPARE(config.accounts().size(), 0);
    QCOMPARE(config.warnings().size(), 1);
    QVERIFY(config.warnings().first().contains(QStringLiteral("broken")));
}

void TestConfig::scopedQueryWrapsCorrectly()
{
    Account account;
    account.key = QStringLiteral("work");
    account.maildir = QStringLiteral("work-mail");

    QCOMPARE(account.scopedQuery(QStringLiteral("tag:inbox")),
             QStringLiteral("path:\"work-mail/**\" and (tag:inbox)"));

    // An empty query still scopes to the account rather than matching nothing.
    QCOMPARE(account.scopedQuery(QString()),
             QStringLiteral("path:\"work-mail/**\""));
}

QTEST_MAIN(TestConfig)
#include "test_config.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Append to `tests/CMakeLists.txt`:
```cmake
add_qtmaildir_test(config)
```

Run: `cmake -S . -B build -G Ninja && cmake --build build`
Expected: FAIL, `config.h: No such file or directory`.

- [ ] **Step 3: Write src/config.h**

```cpp
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/// One mail account. notmuch has no concept of accounts; it sees a single flat
/// tree. An account is therefore a path prefix within that tree plus an
/// identity.
struct Account
{
    QString key;      ///< INI group suffix, e.g. "work" from [account.work].
    QString name;
    QString address;
    QString maildir;  ///< Relative to notmuch's database.path.
    QString drafts;   ///< Unused in v1; send is v2.

    bool isValid() const { return !key.isEmpty() && !maildir.isEmpty(); }

    /// Restricts a notmuch query to this account's subtree.
    QString scopedQuery(const QString &query) const;
};

struct SavedQuery
{
    QString name;
    QString query;
};

/// Reads ~/.config/qtmaildir/qtmaildir.conf.
///
/// The Maildir path is deliberately NOT configurable here: notmuch already
/// stores it as database.path and libnotmuch reads it. Duplicating it would
/// allow the GUI to index a different tree than the CLI.
class Config
{
public:
    /// Path used when load() is called with no argument.
    static QString defaultPath();

    void load(const QString &path);

    QList<Account> accounts() const { return m_accounts; }
    Account account(const QString &key) const;
    QList<SavedQuery> savedQueries() const { return m_savedQueries; }

    /// Empty when unset; the caller disables the Sync button in that case.
    QString syncCommand() const { return m_syncCommand; }

    /// Optional alternate notmuch config file. Empty means "let notmuch decide".
    QString notmuchConfig() const { return m_notmuchConfig; }

    /// Non-fatal problems, shown once in a startup banner.
    QStringList warnings() const { return m_warnings; }

private:
    QList<Account> m_accounts;
    QList<SavedQuery> m_savedQueries;
    QString m_syncCommand;
    QString m_notmuchConfig;
    QStringList m_warnings;
};
```

- [ ] **Step 4: Write src/config.cpp**

```cpp
#include "config.h"

#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString Account::scopedQuery(const QString &query) const
{
    const QString prefix = QStringLiteral("path:\"%1/**\"").arg(maildir);
    if (query.trimmed().isEmpty())
        return prefix;
    return QStringLiteral("%1 and (%2)").arg(prefix, query);
}

QString Config::defaultPath()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return base + QStringLiteral("/qtmaildir/qtmaildir.conf");
}

void Config::load(const QString &path)
{
    QSettings settings(path, QSettings::IniFormat);

    m_notmuchConfig =
        settings.value(QStringLiteral("general/notmuch_config")).toString();

    m_syncCommand = settings.value(QStringLiteral("sync/command")).toString();
    if (m_syncCommand.isEmpty()) {
        m_warnings.append(QStringLiteral(
            "No sync command configured ([sync] command); syncing is disabled."));
    } else if (!QFileInfo::exists(m_syncCommand.split(QLatin1Char(' ')).first())) {
        m_warnings.append(
            QStringLiteral("Sync command '%1' does not exist; syncing is disabled.")
                .arg(m_syncCommand));
        m_syncCommand.clear();
    }

    for (const QString &group : settings.childGroups()) {
        if (!group.startsWith(QStringLiteral("account.")))
            continue;

        Account account;
        account.key = group.mid(QStringLiteral("account.").size());

        settings.beginGroup(group);
        account.name = settings.value(QStringLiteral("name")).toString();
        account.address = settings.value(QStringLiteral("address")).toString();
        account.maildir = settings.value(QStringLiteral("maildir")).toString();
        account.drafts = settings.value(QStringLiteral("drafts")).toString();
        settings.endGroup();

        if (!account.isValid()) {
            m_warnings.append(
                QStringLiteral("Account '%1' has no maildir; ignoring it.")
                    .arg(account.key));
            continue;
        }
        m_accounts.append(account);
    }

    settings.beginGroup(QStringLiteral("queries"));
    for (const QString &name : settings.childKeys())
        m_savedQueries.append({ name, settings.value(name).toString() });
    settings.endGroup();
}

Account Config::account(const QString &key) const
{
    for (const Account &a : m_accounts) {
        if (a.key == key)
            return a;
    }
    return {};
}
```

- [ ] **Step 5: Add to the library**

In `src/CMakeLists.txt`, add `config.cpp` to `add_library(qtmaildir_lib STATIC ...)`:

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
)
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `keymap` and `config` both PASS.

Note: `QSettings::childKeys()` returns keys sorted, so the `parsesSavedQueries`
ordering assertion holds for `Inbox` before `Unread` alphabetically. If a future
config needs file order, that requires a hand-rolled parser; not needed in v1.

- [ ] **Step 7: Commit**

```bash
git add src/config.h src/config.cpp src/CMakeLists.txt tests/
git commit -S -m "feat: add Config with account, query, and sync parsing"
```

---

## Task 4: MimeParser — part selection and decoding

**Files:**
- Create: `src/mimeparser.h`, `src/mimeparser.cpp`
- Create: `tests/test_mimeparser.cpp`
- Create: `tests/fixtures/plain.eml`, `alternative.eml`, `inline_image.eml`, `attachment.eml`, `encoded_subject.eml`, `truncated.eml`, `hostile_filename.eml`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the fixtures**

Create `tests/fixtures/plain.eml`:

```
From: Alice <alice@example.org>
To: Bob <bob@example.net>
Subject: Plain hello
Date: Sat, 01 Aug 2026 10:00:00 +0000
Message-ID: <plain-1@example.org>
Content-Type: text/plain; charset=utf-8

Hello Bob.

> quoted line
Regards,
Alice
```

Create `tests/fixtures/alternative.eml`:

```
From: Alice <alice@example.org>
Subject: Both parts
Date: Sat, 01 Aug 2026 10:00:00 +0000
Message-ID: <alt-1@example.org>
MIME-Version: 1.0
Content-Type: multipart/alternative; boundary="BOUND"

--BOUND
Content-Type: text/plain; charset=utf-8

plain version
--BOUND
Content-Type: text/html; charset=utf-8

<html><body><p>html version</p></body></html>
--BOUND--
```

Create `tests/fixtures/inline_image.eml`:

```
From: Alice <alice@example.org>
Subject: Inline image
Date: Sat, 01 Aug 2026 10:00:00 +0000
Message-ID: <cid-1@example.org>
MIME-Version: 1.0
Content-Type: multipart/related; boundary="REL"

--REL
Content-Type: text/html; charset=utf-8

<html><body><img src="cid:logo@example.org"></body></html>
--REL
Content-Type: image/png
Content-Transfer-Encoding: base64
Content-ID: <logo@example.org>

iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9
awAAAABJRU5ErkJggg==
--REL--
```

Create `tests/fixtures/attachment.eml`:

```
From: Alice <alice@example.org>
Subject: With attachment
Date: Sat, 01 Aug 2026 10:00:00 +0000
Message-ID: <att-1@example.org>
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="MIX"

--MIX
Content-Type: text/plain; charset=utf-8

see attached
--MIX
Content-Type: text/plain; charset=utf-8; name="notes.txt"
Content-Disposition: attachment; filename="notes.txt"
Content-Transfer-Encoding: quoted-printable

caf=C3=A9 notes
--MIX--
```

Create `tests/fixtures/encoded_subject.eml`:

```
From: =?utf-8?B?w4RsaWNl?= <alice@example.org>
Subject: =?utf-8?Q?Caf=C3=A9_meeting?=
Date: Sat, 01 Aug 2026 10:00:00 +0000
Message-ID: <enc-1@example.org>
Content-Type: text/plain; charset=utf-8

body
```

Create `tests/fixtures/truncated.eml` (deliberately cut off mid-part):

```
From: Alice <alice@example.org>
Subject: Truncated
Date: Sat, 01 Aug 2026 10:00:00 +0000
Message-ID: <trunc-1@example.org>
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="CUT"

--CUT
Content-Type: text/plain; charset=utf-8

this part never closes
```

Create `tests/fixtures/hostile_filename.eml`:

```
From: Attacker <bad@example.org>
Subject: Hostile attachment name
Date: Sat, 01 Aug 2026 10:00:00 +0000
Message-ID: <evil-1@example.org>
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary="EVIL"

--EVIL
Content-Type: text/plain; charset=utf-8

body
--EVIL
Content-Type: text/plain; name="../../../../tmp/pwned.txt"
Content-Disposition: attachment; filename="../../../../tmp/pwned.txt"

owned
--EVIL--
```

- [ ] **Step 2: Write the failing test**

Create `tests/test_mimeparser.cpp`:

```cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include "mimeparser.h"

class TestMimeParser : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void parsesPlainText();
    void prefersHtmlWhenAvailable();
    void fallsBackToPlainWhenHtmlDisabled();
    void collectsInlineCidParts();
    void decodesQuotedPrintableAttachment();
    void decodesEncodedHeaders();
    void malformedMessageDoesNotCrash();
    void missingFileIsReported();
    void hostileFilenameIsSanitised();
    void savedAttachmentMatchesBytes();
    void safeFilenameStripsPathComponents();

private:
    QString fixture(const QString &name) const
    { return m_fixtureDir + QLatin1Char('/') + name; }

    QString m_fixtureDir;
};

void TestMimeParser::initTestCase()
{
    // FIXTURE_DIR is defined by CMake so the test can run from any cwd.
    m_fixtureDir = QStringLiteral(FIXTURE_DIR);
    QVERIFY2(QDir(m_fixtureDir).exists(), "fixture directory missing");
}

void TestMimeParser::parsesPlainText()
{
    MimeParser parser;
    const ParsedMessage msg = parser.parse(fixture(QStringLiteral("plain.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.subject, QStringLiteral("Plain hello"));
    QCOMPARE(msg.from, QStringLiteral("Alice <alice@example.org>"));
    QVERIFY(msg.plainBody.contains(QStringLiteral("Hello Bob.")));
    QVERIFY(msg.htmlBody.isEmpty());
    QVERIFY(msg.attachments.isEmpty());
}

void TestMimeParser::prefersHtmlWhenAvailable()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("alternative.eml")));

    QVERIFY(msg.ok);
    QVERIFY(msg.htmlBody.contains(QStringLiteral("html version")));
    // The plain alternative is kept so the user can toggle to it.
    QVERIFY(msg.plainBody.contains(QStringLiteral("plain version")));
    QVERIFY(msg.hasHtml());
}

void TestMimeParser::fallsBackToPlainWhenHtmlDisabled()
{
    MimeParser parser;
    const ParsedMessage msg = parser.parse(fixture(QStringLiteral("plain.eml")));

    QVERIFY(!msg.hasHtml());
    QVERIFY(!msg.plainBody.isEmpty());
}

void TestMimeParser::collectsInlineCidParts()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("inline_image.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.inlineParts.size(), 1);
    // Content-ID angle brackets are stripped so it matches the cid: URL body.
    QVERIFY(msg.inlineParts.contains(QStringLiteral("logo@example.org")));

    const InlinePart part = msg.inlineParts.value(QStringLiteral("logo@example.org"));
    QCOMPARE(part.mimeType, QStringLiteral("image/png"));
    // Decoded 1x1 PNG starts with the PNG magic bytes.
    QVERIFY(part.data.startsWith(QByteArray("\x89PNG", 4)));
}

void TestMimeParser::decodesQuotedPrintableAttachment()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("attachment.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.attachments.size(), 1);
    QCOMPARE(msg.attachments.first().filename, QStringLiteral("notes.txt"));
    QCOMPARE(QString::fromUtf8(msg.attachments.first().data),
             QStringLiteral("café notes"));
}

void TestMimeParser::decodesEncodedHeaders()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("encoded_subject.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.subject, QStringLiteral("Café meeting"));
    QVERIFY(msg.from.contains(QStringLiteral("Älice")));
}

void TestMimeParser::malformedMessageDoesNotCrash()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("truncated.eml")));

    // GMime is tolerant: it recovers the headers and whatever body it found.
    // The requirement is only that parsing terminates and reports something.
    QCOMPARE(msg.subject, QStringLiteral("Truncated"));
}

void TestMimeParser::missingFileIsReported()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("does_not_exist.eml")));

    QVERIFY(!msg.ok);
    QVERIFY(!msg.error.isEmpty());
}

void TestMimeParser::hostileFilenameIsSanitised()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("hostile_filename.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.attachments.size(), 1);

    // The raw header value is preserved for display...
    QVERIFY(msg.attachments.first().filename.contains(QStringLiteral("..")));
    // ...but the name used on disk is reduced to a basename.
    QCOMPARE(msg.attachments.first().safeFilename(), QStringLiteral("pwned.txt"));
}

void TestMimeParser::savedAttachmentMatchesBytes()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("attachment.eml")));
    QVERIFY(msg.ok);

    QTemporaryDir dir;
    QString error;
    const QString written =
        msg.attachments.first().saveTo(dir.path(), &error);

    QVERIFY2(!written.isEmpty(), qPrintable(error));
    // Never escapes the target directory.
    QVERIFY(written.startsWith(dir.path()));

    QFile f(written);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), msg.attachments.first().data);
}

QTEST_MAIN(TestMimeParser)
#include "test_mimeparser.moc"
```

- [ ] **Step 3: Register the test and run to verify it fails**

Append to `tests/CMakeLists.txt`:

```cmake
add_qtmaildir_test(mimeparser)
target_compile_definitions(test_mimeparser PRIVATE
    FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
```

Run: `cmake -S . -B build -G Ninja && cmake --build build`
Expected: FAIL, `mimeparser.h: No such file or directory`.

- [ ] **Step 4: Write src/mimeparser.h**

```cpp
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

/// An inline part referenced by a cid: URL from the HTML body.
struct InlinePart
{
    QString mimeType;
    QByteArray data;
};

struct Attachment
{
    QString filename;   ///< As it appeared in the message. Untrusted.
    QString mimeType;
    QByteArray data;

    /// filename reduced to a basename safe to join onto a directory.
    /// Attacker-controlled input: a filename may contain path separators or
    /// "..", so anything that could escape the target directory is stripped.
    /// Returns a generated name when nothing usable remains.
    QString safeFilename() const;

    /// Writes the attachment into directory. Returns the full path written, or
    /// an empty string on failure with *error set.
    QString saveTo(const QString &directory, QString *error) const;
};

struct ParsedMessage
{
    bool ok = false;
    QString error;

    QString subject;
    QString from;
    QString to;
    QString cc;
    QString date;
    QString messageId;

    QString plainBody;
    QString htmlBody;

    QHash<QString, InlinePart> inlineParts;  ///< Keyed by Content-ID, no <>.
    QList<Attachment> attachments;

    bool hasHtml() const { return !htmlBody.isEmpty(); }
};

/// Parses a single message file using GMime.
///
/// Hand-rolling this would mean reimplementing RFC 2047 encoded words, RFC 2231
/// parameter continuations, transfer encodings, and charset conversion, plus
/// tolerance for malformed real-world mail.
class MimeParser
{
public:
    MimeParser();

    ParsedMessage parse(const QString &filePath) const;
};
```

- [ ] **Step 5: Write src/mimeparser.cpp**

```cpp
#include "mimeparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

#include <gmime/gmime.h>

namespace {

/// GMime must be initialised exactly once per process.
void ensureGMimeInit()
{
    static bool initialised = false;
    if (!initialised) {
        g_mime_init();
        initialised = true;
    }
}

QString fromGChar(char *owned)
{
    if (!owned)
        return {};
    const QString result = QString::fromUtf8(owned);
    g_free(owned);
    return result;
}

QString headerText(GMimeMessage *message, const char *name)
{
    GMimeHeaderList *headers = g_mime_object_get_header_list(
        GMIME_OBJECT(message));
    if (!headers)
        return {};
    GMimeHeader *header = g_mime_header_list_get_header(headers, name);
    if (!header)
        return {};
    // get_value() returns the RFC 2047-decoded value.
    return QString::fromUtf8(g_mime_header_get_value(header));
}

QByteArray decodePart(GMimePart *part)
{
    GMimeDataWrapper *content = g_mime_part_get_content(part);
    if (!content)
        return {};

    GMimeStream *memStream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(content, memStream);
    g_mime_stream_flush(memStream);

    GByteArray *bytes = g_mime_stream_mem_get_byte_array(
        GMIME_STREAM_MEM(memStream));
    QByteArray result(reinterpret_cast<const char *>(bytes->data), bytes->len);

    g_object_unref(memStream);
    return result;
}

/// Walks the MIME tree, filling the parsed message.
void collectParts(GMimeObject *object, ParsedMessage &out)
{
    if (GMIME_IS_MULTIPART(object)) {
        GMimeMultipart *multipart = GMIME_MULTIPART(object);
        const int count = g_mime_multipart_get_count(multipart);
        for (int i = 0; i < count; ++i)
            collectParts(g_mime_multipart_get_part(multipart, i), out);
        return;
    }

    if (GMIME_IS_MESSAGE_PART(object)) {
        GMimeMessage *sub = g_mime_message_part_get_message(
            GMIME_MESSAGE_PART(object));
        if (sub)
            collectParts(g_mime_message_get_mime_part(sub), out);
        return;
    }

    if (!GMIME_IS_PART(object))
        return;

    GMimePart *part = GMIME_PART(object);
    GMimeContentType *contentType = g_mime_object_get_content_type(object);
    const QString mimeType = contentType
        ? fromGChar(g_mime_content_type_get_mime_type(contentType))
        : QStringLiteral("application/octet-stream");

    const char *disposition = g_mime_object_get_disposition(object);
    const bool isAttachment =
        disposition && g_ascii_strcasecmp(disposition, "attachment") == 0;

    const char *contentId = g_mime_part_get_content_id(part);

    if (isAttachment) {
        Attachment attachment;
        attachment.mimeType = mimeType;
        attachment.data = decodePart(part);
        const char *filename = g_mime_part_get_filename(part);
        attachment.filename = filename
            ? QString::fromUtf8(filename)
            : QStringLiteral("attachment");
        out.attachments.append(attachment);
        return;
    }

    if (contentId) {
        // Strip the angle brackets so the key matches a cid: URL body.
        QString id = QString::fromUtf8(contentId);
        if (id.startsWith(QLatin1Char('<')) && id.endsWith(QLatin1Char('>')))
            id = id.mid(1, id.size() - 2);
        out.inlineParts.insert(id, InlinePart{ mimeType, decodePart(part) });
        return;
    }

    if (mimeType == QLatin1String("text/plain") && out.plainBody.isEmpty()) {
        out.plainBody = QString::fromUtf8(decodePart(part));
    } else if (mimeType == QLatin1String("text/html") && out.htmlBody.isEmpty()) {
        out.htmlBody = QString::fromUtf8(decodePart(part));
    }
}

} // namespace

QString Attachment::safeFilename() const
{
    // Reduce to a basename: QFileInfo handles '/', and backslashes are stripped
    // explicitly because a Windows-authored name can carry them.
    QString name = filename;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    name = QFileInfo(name).fileName();

    // A name of "..", "." or empty leaves nothing usable.
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return QStringLiteral("attachment-%1").arg(
            QUuid::createUuid().toString(QUuid::Id128).left(8));

    return name;
}

QString Attachment::saveTo(const QString &directory, QString *error) const
{
    const QDir dir(directory);
    const QString target = dir.absoluteFilePath(safeFilename());

    // Belt and braces, and currently UNREACHABLE through this function:
    // safeFilename() above already reduces any name to a basename, so no
    // caller-supplied filename can produce a target outside `directory`.
    // The guard exists so that a future change which stops sanitising, or
    // which lets a caller pass a subpath, still cannot escape. Do not write
    // a test that drives saveTo() expecting a refusal: it cannot happen
    // while safeFilename() runs first. Test safeFilename() instead, which
    // is the control that actually stops traversal today.
    //
    // The comparison must be separator-aware. A bare startsWith() on the
    // strings would accept "/tmp/safe-evil/x" as being inside "/tmp/safe",
    // since one is a string prefix of the other with no path boundary
    // between them. cleanPath() also resolves ".." before comparison rather
    // than leaving it to be compared textually.
    const QString cleanDir = QDir::cleanPath(QDir(directory).absolutePath());
    const QString cleanTarget = QDir::cleanPath(target);
    if (cleanTarget != cleanDir
        && !cleanTarget.startsWith(cleanDir + QLatin1Char('/'))) {
        if (error)
            *error = QStringLiteral("Refusing to write outside %1").arg(cleanDir);
        return {};
    }

    QFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    file.write(data);
    file.close();
    return target;
}

MimeParser::MimeParser()
{
    ensureGMimeInit();
}

ParsedMessage MimeParser::parse(const QString &filePath) const
{
    ParsedMessage out;

    FILE *fp = fopen(filePath.toLocal8Bit().constData(), "r");
    if (!fp) {
        out.error = QStringLiteral("Cannot open %1").arg(filePath);
        return out;
    }

    GMimeStream *stream = g_mime_stream_file_new(fp);
    GMimeParser *parser = g_mime_parser_new_with_stream(stream);
    GMimeMessage *message = g_mime_parser_construct_message(parser, nullptr);

    g_object_unref(parser);
    g_object_unref(stream);

    if (!message) {
        out.error = QStringLiteral("Cannot parse %1").arg(filePath);
        return out;
    }

    out.subject = QString::fromUtf8(
        g_mime_message_get_subject(message) ?: "");
    out.from = headerText(message, "From");
    out.to = headerText(message, "To");
    out.cc = headerText(message, "Cc");
    out.date = headerText(message, "Date");
    out.messageId = QString::fromUtf8(
        g_mime_message_get_message_id(message) ?: "");

    GMimeObject *body = g_mime_message_get_mime_part(message);
    if (body)
        collectParts(body, out);

    g_object_unref(message);

    out.ok = true;
    return out;
}
```

- [ ] **Step 6: Add to the library**

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
)
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: `keymap`, `config`, `mimeparser` all PASS.

If `decodesEncodedHeaders` fails on the From value, check that GMime's
`g_mime_header_get_value` is returning the decoded form; older GMime needs
`g_mime_utils_header_decode_text` applied to the raw value instead.

- [ ] **Step 8: Commit**

```bash
git add src/mimeparser.h src/mimeparser.cpp src/CMakeLists.txt tests/
git commit -S -m "feat: add MimeParser with GMime and safe attachment naming"
```

---

## Task 5: Request interceptor — deny by default

This is the security-critical component. It gets the most careful test in the
project.

**Files:**
- Create: `src/requestinterceptor.h`, `src/requestinterceptor.cpp`
- Create: `tests/test_interceptor.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_interceptor.cpp`:

```cpp
#include <QtTest>
#include "requestinterceptor.h"

class TestInterceptor : public QObject
{
    Q_OBJECT
private slots:
    void blocksRemoteHttpByDefault();
    void blocksRemoteHttpsByDefault();
    void blocksFileUrlsAlways();
    void allowsCidForCurrentMessage();
    void blocksCidForForeignMessage();
    void allowRemoteFlagPermitsHttpButNotFile();
    void recordsThatSomethingWasBlocked();
    void resetClearsBlockedFlag();
};

void TestInterceptor::blocksRemoteHttpByDefault()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("http://tracker.example/pixel.gif"))));
}

void TestInterceptor::blocksRemoteHttpsByDefault()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("https://cdn.example/style.css"))));
}

void TestInterceptor::blocksFileUrlsAlways()
{
    RequestInterceptor interceptor;
    interceptor.setAllowRemote(true);
    // Even with remote content explicitly allowed, local files stay blocked:
    // a message must never read the filesystem.
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("file:///etc/passwd"))));
}

void TestInterceptor::allowsCidForCurrentMessage()
{
    RequestInterceptor interceptor;
    interceptor.setAllowedCids({ QStringLiteral("logo@example.org") });
    QVERIFY(interceptor.shouldAllow(QUrl(QStringLiteral("cid:logo@example.org"))));
}

void TestInterceptor::blocksCidForForeignMessage()
{
    RequestInterceptor interceptor;
    interceptor.setAllowedCids({ QStringLiteral("logo@example.org") });
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("cid:other@example.org"))));
}

void TestInterceptor::allowRemoteFlagPermitsHttpButNotFile()
{
    RequestInterceptor interceptor;
    interceptor.setAllowRemote(true);
    QVERIFY(interceptor.shouldAllow(QUrl(QStringLiteral("https://cdn.example/img.png"))));
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("file:///etc/passwd"))));
}

void TestInterceptor::recordsThatSomethingWasBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.blockedAnything());
    interceptor.shouldAllow(QUrl(QStringLiteral("http://tracker.example/p.gif")));
    // Drives the "Remote content blocked" banner in the message header.
    QVERIFY(interceptor.blockedAnything());
}

void TestInterceptor::resetClearsBlockedFlag()
{
    RequestInterceptor interceptor;
    interceptor.shouldAllow(QUrl(QStringLiteral("http://tracker.example/p.gif")));
    QVERIFY(interceptor.blockedAnything());

    interceptor.resetForNewMessage();
    QVERIFY(!interceptor.blockedAnything());
    // Remote permission never carries over to the next message.
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("https://cdn.example/x.png"))));
}

QTEST_MAIN(TestInterceptor)
#include "test_interceptor.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Append to `tests/CMakeLists.txt`:
```cmake
add_qtmaildir_test(interceptor)
```

Run: `cmake -S . -B build -G Ninja && cmake --build build`
Expected: FAIL, `requestinterceptor.h: No such file or directory`.

- [ ] **Step 3: Write src/requestinterceptor.h**

The policy lives in `shouldAllow()`, a pure function of URL and state, so it is
testable without constructing a web engine profile. `interceptRequest()` is a
thin adapter over it.

```cpp
#pragma once

#include <QSet>
#include <QUrl>
#include <QWebEngineUrlRequestInterceptor>

/// Deny-by-default request policy for the message view.
///
/// A message body is untrusted input from a stranger. Everything is blocked
/// unless explicitly permitted: remote loads leak the fact that a message was
/// read (tracking pixels) and file: loads would expose the local filesystem.
class RequestInterceptor : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    explicit RequestInterceptor(QObject *parent = nullptr);

    /// The whole policy, as a pure function so it can be tested directly.
    bool shouldAllow(const QUrl &url);

    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

    /// Content-IDs belonging to the currently displayed message.
    void setAllowedCids(const QSet<QString> &cids) { m_allowedCids = cids; }

    /// The exact base URL passed to setHtml(). REQUIRED: without it every
    /// qtmaildir: URL is denied and nothing renders. Only this exact URL is
    /// trusted on that scheme; the scheme alone is not sufficient.
    void setDocumentUrl(const QUrl &url) { m_documentUrl = url; }

    /// Per-message opt-in, triggered by the user clicking "Load remote content".
    /// Never persisted, never carried to the next message.
    void setAllowRemote(bool allow) { m_allowRemote = allow; }
    bool allowRemote() const { return m_allowRemote; }

    /// True once any request has been denied, so the UI can offer the button.
    bool blockedAnything() const { return m_blockedAnything; }

    /// Called before rendering a new message: clears both the remote grant and
    /// the blocked flag.
    void resetForNewMessage();

private:
    QSet<QString> m_allowedCids;
    QUrl m_documentUrl;
    bool m_allowRemote = false;
    bool m_blockedAnything = false;
};
```

- [ ] **Step 4: Write src/requestinterceptor.cpp**

```cpp
#include "requestinterceptor.h"

#include <QWebEngineUrlRequestInfo>

RequestInterceptor::RequestInterceptor(QObject *parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
}

bool RequestInterceptor::shouldAllow(const QUrl &url)
{
    const QString scheme = url.scheme();

    // The document itself is loaded via setHtml() with a qtmaildir: base URL,
    // so that exact URL must pass or nothing renders at all. Allowing the
    // whole SCHEME would be a hole: a hostile body could reference
    // qtmaildir://anything and be trusted, which would make this object's
    // correctness depend on the scheme handler's. Fails closed when no
    // document URL has been set.
    if (scheme == QLatin1String("qtmaildir")) {
        if (!m_documentUrl.isEmpty() && url == m_documentUrl)
            return true;
        m_blockedAnything = true;
        return false;
    }

    // Inline parts of the current message only.
    if (scheme == QLatin1String("cid")) {
        // QUrl keeps a cid: body in path(), not host().
        const QString id = url.path();
        if (m_allowedCids.contains(id))
            return true;
        m_blockedAnything = true;
        return false;
    }

    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        if (m_allowRemote)
            return true;
        m_blockedAnything = true;
        return false;
    }

    // Everything else, file: above all, is denied unconditionally. There is no
    // flag that enables it.
    m_blockedAnything = true;
    return false;
}

void RequestInterceptor::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    if (!shouldAllow(info.requestUrl()))
        info.block(true);
}

void RequestInterceptor::resetForNewMessage()
{
    m_allowRemote = false;
    m_blockedAnything = false;
}
```

- [ ] **Step 5: Add to the library**

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
)
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: all four test binaries PASS, `interceptor` with 8 functions.

- [ ] **Step 7: Commit**

```bash
git add src/requestinterceptor.h src/requestinterceptor.cpp src/CMakeLists.txt tests/
git commit -S -m "feat: add deny-by-default web request interceptor"
```

---

## Task 6: CID scheme handler and HTML builder

**Files:**
- Create: `src/cidschemehandler.h`, `src/cidschemehandler.cpp`
- Create: `src/htmlbuilder.h`, `src/htmlbuilder.cpp`
- Modify: `src/CMakeLists.txt`

`HtmlBuilder` gets its own test binary rather than extending
`tests/test_mimeparser.cpp`, since escaping is a separate unit from parsing.
`CidSchemeHandler` has no unit test: it needs a live `QWebEngineUrlRequestJob`,
and the access rule it enforces is already asserted in Task 5.

- [ ] **Step 1: Write the failing test**

Create `tests/test_htmlbuilder.cpp`:

```cpp
#include <QtTest>
#include "htmlbuilder.h"

class TestHtmlBuilder : public QObject
{
    Q_OBJECT
private slots:
    void escapesPlainText();
    void preservesHtmlBodyWhenHtmlRequested();
    void marksQuotedLines();
    void plainTextScriptTagIsNeutralised();
    void buildsThreadWithAllMessages();
    void collapsedMessageShowsStubOnly();
    void threadNamespacesCidUrls();
};

void TestHtmlBuilder::escapesPlainText()
{
    ParsedMessage msg;
    msg.ok = true;
    msg.plainBody = QStringLiteral("a < b & c > d");

    const QString html = HtmlBuilder::build(msg, HtmlBuilder::ForcePlain);
    QVERIFY(html.contains(QStringLiteral("a &lt; b &amp; c &gt; d")));
}

void TestHtmlBuilder::preservesHtmlBodyWhenHtmlRequested()
{
    ParsedMessage msg;
    msg.ok = true;
    msg.htmlBody = QStringLiteral("<p>hello</p>");

    const QString html = HtmlBuilder::build(msg, HtmlBuilder::PreferHtml);
    QVERIFY(html.contains(QStringLiteral("<p>hello</p>")));
}

void TestHtmlBuilder::marksQuotedLines()
{
    ParsedMessage msg;
    msg.ok = true;
    msg.plainBody = QStringLiteral("reply\n> quoted\nend");

    const QString html = HtmlBuilder::build(msg, HtmlBuilder::ForcePlain);
    QVERIFY(html.contains(QStringLiteral("class=\"quote\"")));
}

void TestHtmlBuilder::plainTextScriptTagIsNeutralised()
{
    ParsedMessage msg;
    msg.ok = true;
    msg.plainBody = QStringLiteral("<script>alert(1)</script>");

    const QString html = HtmlBuilder::build(msg, HtmlBuilder::ForcePlain);
    // Escaped, not embedded. (JavaScript is also disabled at the profile level,
    // so this is the second of two independent defences.)
    QVERIFY(!html.contains(QStringLiteral("<script>")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
}

void TestHtmlBuilder::buildsThreadWithAllMessages()
{
    ThreadRenderItem first;
    first.message.ok = true;
    first.message.subject = QStringLiteral("First");
    first.message.from = QStringLiteral("Alice");
    first.message.plainBody = QStringLiteral("first body");
    first.expanded = true;

    ThreadRenderItem second;
    second.message.ok = true;
    second.message.subject = QStringLiteral("Second");
    second.message.from = QStringLiteral("Bob");
    second.message.plainBody = QStringLiteral("second body");
    second.expanded = true;

    const QString html =
        HtmlBuilder::buildThread({ first, second }, HtmlBuilder::ForcePlain);

    QVERIFY(html.contains(QStringLiteral("first body")));
    QVERIFY(html.contains(QStringLiteral("second body")));
    // Each message is its own section, so per-message CSS and anchors work.
    QCOMPARE(html.count(QStringLiteral("class=\"message\"")), 2);
}

void TestHtmlBuilder::collapsedMessageShowsStubOnly()
{
    ThreadRenderItem item;
    item.message.ok = true;
    item.message.from = QStringLiteral("Carol");
    item.message.subject = QStringLiteral("Old news");
    item.message.plainBody = QStringLiteral("secret body text");
    item.expanded = false;

    const QString html =
        HtmlBuilder::buildThread({ item }, HtmlBuilder::ForcePlain);

    // Unmatched messages collapse to a one-line stub; the body is not emitted.
    QVERIFY(html.contains(QStringLiteral("Carol")));
    QVERIFY(!html.contains(QStringLiteral("secret body text")));
    QVERIFY(html.contains(QStringLiteral("class=\"stub\"")));
}

void TestHtmlBuilder::threadNamespacesCidUrls()
{
    // Two messages in one document may both reference cid:logo@x. Without
    // namespacing, the second would show the first's image.
    ThreadRenderItem first;
    first.message.ok = true;
    first.message.htmlBody =
        QStringLiteral("<img src=\"cid:logo@example.org\">");
    first.expanded = true;
    first.cidPrefix = QStringLiteral("m0");

    ThreadRenderItem second;
    second.message.ok = true;
    second.message.htmlBody =
        QStringLiteral("<img src=\"cid:logo@example.org\">");
    second.expanded = true;
    second.cidPrefix = QStringLiteral("m1");

    const QString html =
        HtmlBuilder::buildThread({ first, second }, HtmlBuilder::PreferHtml);

    QVERIFY(html.contains(QStringLiteral("cid:m0!logo@example.org")));
    QVERIFY(html.contains(QStringLiteral("cid:m1!logo@example.org")));
    // The bare form must not survive, or it would resolve ambiguously.
    QVERIFY(!html.contains(QStringLiteral("\"cid:logo@example.org\"")));
}

QTEST_MAIN(TestHtmlBuilder)
#include "test_htmlbuilder.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Append to `tests/CMakeLists.txt`:
```cmake
add_qtmaildir_test(htmlbuilder)
```

Run: `cmake -S . -B build -G Ninja && cmake --build build`
Expected: FAIL, `htmlbuilder.h: No such file or directory`.

- [ ] **Step 3: Write src/htmlbuilder.h**

```cpp
#pragma once

#include <QString>

#include <QList>

#include "mimeparser.h"

/// One message's place in a rendered thread.
struct ThreadRenderItem
{
    ParsedMessage message;

    /// Matched messages render in full; unmatched collapse to a one-line stub.
    bool expanded = true;

    /// Disambiguates cid: references. Two newsletters in one thread commonly
    /// use the same Content-ID (cid:logo@example.org), which would collide in
    /// a single document, so every reference is rewritten to
    /// cid:<prefix>!<id>.
    QString cidPrefix;
};

/// Turns parsed messages into the HTML string handed to the web view.
///
/// Plain text goes through the same path as HTML so the view has one render
/// path rather than two. A whole thread renders as ONE document rather than one
/// view per message: a thread of newsletters can hold dozens of messages, and a
/// QWebEngineView each would spawn a Chromium render process each.
class HtmlBuilder
{
public:
    enum Mode {
        PreferHtml,  ///< Use the HTML part when the message has one.
        ForcePlain,  ///< Always render the plain part, escaped.
    };

    /// Single message, used for the error card and for tests.
    static QString build(const ParsedMessage &message, Mode mode);

    /// The whole thread, oldest first.
    static QString buildThread(const QList<ThreadRenderItem> &items, Mode mode);

    /// Rewrites cid: URLs in an HTML body to their namespaced form.
    static QString namespaceCids(const QString &html, const QString &prefix);

private:
    static QString renderPlain(const QString &text);
    static QString renderBody(const ThreadRenderItem &item, Mode mode);
    static QString renderStub(const ParsedMessage &message);
    static QString document(const QString &bodyHtml);
};
```

- [ ] **Step 4: Write src/htmlbuilder.cpp**

```cpp
#include "htmlbuilder.h"

#include <QRegularExpression>

namespace {

const char *kStyle = R"CSS(
body { font-family: sans-serif; font-size: 10pt; margin: 12px; }
pre.plain { white-space: pre-wrap; word-wrap: break-word;
            font-family: monospace; margin: 0; }
span.quote { color: #4a6f8a; }
.message { border-top: 1px solid #bbb; padding: 10px 0; }
.message:first-child { border-top: none; }
.msg-header { font-size: 9pt; color: #555; margin-bottom: 8px; }
.msg-header .who { font-weight: bold; color: #000; }
.stub { font-size: 9pt; color: #666; padding: 4px 0;
        border-top: 1px solid #ddd; }
)CSS";

} // namespace

QString HtmlBuilder::renderPlain(const QString &text)
{
    QString out;
    out += QStringLiteral("<pre class=\"plain\">");

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const bool quoted = line.startsWith(QLatin1Char('>'));

        if (quoted)
            out += QStringLiteral("<span class=\"quote\">");
        out += line.toHtmlEscaped();
        if (quoted)
            out += QStringLiteral("</span>");

        if (i + 1 < lines.size())
            out += QLatin1Char('\n');
    }

    out += QStringLiteral("</pre>");
    return out;
}

QString HtmlBuilder::document(const QString &bodyHtml)
{
    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>%1</style></head><body>%2</body></html>")
        .arg(QString::fromUtf8(kStyle), bodyHtml);
}

QString HtmlBuilder::namespaceCids(const QString &html, const QString &prefix)
{
    if (prefix.isEmpty())
        return html;

    // Matches cid: in src/href attribute values, quoted either way.
    static const QRegularExpression re(
        QStringLiteral("(?<attr>src|href)\\s*=\\s*(?<q>[\"'])cid:(?<id>[^\"']+)\\k<q>"),
        QRegularExpression::CaseInsensitiveOption);

    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += html.mid(last, match.capturedStart() - last);
        out += QStringLiteral("%1=%2cid:%3!%4%2")
                   .arg(match.captured(QStringLiteral("attr")),
                        match.captured(QStringLiteral("q")),
                        prefix,
                        match.captured(QStringLiteral("id")));
        last = match.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

QString HtmlBuilder::renderBody(const ThreadRenderItem &item, Mode mode)
{
    if (mode == PreferHtml && item.message.hasHtml())
        return namespaceCids(item.message.htmlBody, item.cidPrefix);
    return renderPlain(item.message.plainBody);
}

QString HtmlBuilder::renderStub(const ParsedMessage &message)
{
    return QStringLiteral("<div class=\"stub\">%1 &mdash; %2</div>")
        .arg(message.from.toHtmlEscaped(), message.subject.toHtmlEscaped());
}

QString HtmlBuilder::build(const ParsedMessage &message, Mode mode)
{
    ThreadRenderItem item;
    item.message = message;
    item.expanded = true;
    return document(renderBody(item, mode));
}

QString HtmlBuilder::buildThread(const QList<ThreadRenderItem> &items, Mode mode)
{
    QString body;

    for (int i = 0; i < items.size(); ++i) {
        const ThreadRenderItem &item = items.at(i);

        if (!item.expanded) {
            body += renderStub(item.message);
            continue;
        }

        body += QStringLiteral(
            "<div class=\"message\" id=\"msg-%1\">"
            "<div class=\"msg-header\"><span class=\"who\">%2</span><br>%3</div>"
            "%4</div>")
            .arg(QString::number(i),
                 item.message.from.toHtmlEscaped(),
                 item.message.date.toHtmlEscaped(),
                 renderBody(item, mode));
    }

    return document(body);
}
```

- [ ] **Step 5: Write src/cidschemehandler.h**

```cpp
#pragma once

#include <QHash>
#include <QWebEngineUrlSchemeHandler>

#include "mimeparser.h"

/// Serves cid: URLs from the currently displayed thread only.
///
/// Keys are the namespaced form "<prefix>!<content-id>" produced by
/// HtmlBuilder, so two messages in one thread that share a Content-ID do not
/// collide. The map is replaced wholesale on every thread change, so a thread
/// can never reference another thread's parts.
class CidSchemeHandler : public QWebEngineUrlSchemeHandler
{
    Q_OBJECT
public:
    explicit CidSchemeHandler(QObject *parent = nullptr);

    void setParts(const QHash<QString, InlinePart> &parts) { m_parts = parts; }

    /// Builds the namespaced key HtmlBuilder's rewritten URLs will request.
    static QString namespacedKey(const QString &prefix, const QString &contentId)
    { return prefix + QLatin1Char('!') + contentId; }

    void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
    QHash<QString, InlinePart> m_parts;
};
```

- [ ] **Step 6: Write src/cidschemehandler.cpp**

```cpp
#include "cidschemehandler.h"

#include <QBuffer>
#include <QWebEngineUrlRequestJob>

CidSchemeHandler::CidSchemeHandler(QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
{
}

void CidSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    const QString id = job->requestUrl().path();

    if (!m_parts.contains(id)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    const InlinePart part = m_parts.value(id);

    // The buffer is parented to the job so it lives exactly as long as needed.
    auto *buffer = new QBuffer(job);
    buffer->setData(part.data);
    buffer->open(QIODevice::ReadOnly);

    job->reply(part.mimeType.toUtf8(), buffer);
}
```

- [ ] **Step 7: Add to the library and run tests**

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
)
```

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: five test binaries PASS.

- [ ] **Step 8: Commit**

```bash
git add src/htmlbuilder.h src/htmlbuilder.cpp src/cidschemehandler.h \
        src/cidschemehandler.cpp src/CMakeLists.txt tests/
git commit -S -m "feat: add HTML builder and cid: scheme handler"
```

---

## Task 7: Value types and notmuch RAII wrappers

**Files:**
- Create: `src/types.h`
- Create: `src/nmraii.h`
- Modify: `src/CMakeLists.txt` (headers only, no new .cpp)

- [ ] **Step 1: Write src/types.h**

These are the only types that cross the thread boundary. They are plain values
with no pointers into notmuch, so a queued signal can copy them safely.

```cpp
#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

struct ThreadSummary
{
    QString threadId;
    QString subject;
    QString authors;
    QDateTime date;
    int totalCount = 0;
    int matchedCount = 0;
    QStringList tags;

    bool isUnread() const { return tags.contains(QStringLiteral("unread")); }
    bool isFlagged() const { return tags.contains(QStringLiteral("flagged")); }
};

struct MessageRef
{
    QString messageId;
    QString filePath;
    QStringList tags;

    /// True when the message itself matched the user's query, as opposed to
    /// being pulled in only because a sibling in its thread matched. Drives
    /// whether it renders expanded or as a stub.
    bool matched = true;
};

/// One tag mutation, kept so it can be inverted for undo.
struct TagChange
{
    QStringList messageIds;
    QStringList added;
    QStringList removed;
    QString description;  ///< Shown in the undo action's text.

    TagChange inverted() const
    {
        return TagChange{ messageIds, removed, added,
                          QStringLiteral("Undo %1").arg(description) };
    }
};

Q_DECLARE_METATYPE(ThreadSummary)
Q_DECLARE_METATYPE(MessageRef)
Q_DECLARE_METATYPE(TagChange)
```

- [ ] **Step 2: Write src/nmraii.h**

libnotmuch hands out raw C pointers with manual destroy calls. These wrappers
make leaks impossible on early return, which matters because the query paths
have several.

```cpp
#pragma once

#include <notmuch.h>

#include <utility>

/// Generic owner for a notmuch handle with a destroy function.
template <typename T, void (*Destroy)(T *)>
class NmHandle
{
public:
    NmHandle() = default;
    explicit NmHandle(T *handle) : m_handle(handle) {}

    ~NmHandle() { reset(); }

    NmHandle(const NmHandle &) = delete;
    NmHandle &operator=(const NmHandle &) = delete;

    NmHandle(NmHandle &&other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr)) {}

    NmHandle &operator=(NmHandle &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_handle = std::exchange(other.m_handle, nullptr);
        }
        return *this;
    }

    void reset(T *handle = nullptr)
    {
        if (m_handle)
            Destroy(m_handle);
        m_handle = handle;
    }

    T *get() const { return m_handle; }
    explicit operator bool() const { return m_handle != nullptr; }

private:
    T *m_handle = nullptr;
};

using NmQuery    = NmHandle<notmuch_query_t, notmuch_query_destroy>;
using NmThreads  = NmHandle<notmuch_threads_t, notmuch_threads_destroy>;
using NmMessages = NmHandle<notmuch_messages_t, notmuch_messages_destroy>;
using NmThread   = NmHandle<notmuch_thread_t, notmuch_thread_destroy>;
using NmMessage  = NmHandle<notmuch_message_t, notmuch_message_destroy>;
using NmTags     = NmHandle<notmuch_tags_t, notmuch_tags_destroy>;
```

- [ ] **Step 3: Verify it compiles**

Both are headers, so add a trivial compile check by including them from
`src/notmuchworker.cpp` in the next task. For now confirm the include path
resolves:

Run:
```bash
echo '#include "src/nmraii.h"
#include "src/types.h"
int main() { return 0; }' > /tmp/nmcheck.cpp && \
g++ -fsyntax-only -std=c++17 -I/usr/include \
    $(pkg-config --cflags Qt6Core) /tmp/nmcheck.cpp && echo OK
```
Expected: `OK`. If `Qt6Core` is not a valid pkg-config name on this system, use
`-I/usr/include/qt6 -I/usr/include/qt6/QtCore` instead.

- [ ] **Step 4: Commit**

```bash
git add src/types.h src/nmraii.h
git commit -S -m "feat: add cross-thread value types and notmuch RAII wrappers"
```

---

## Task 8: NotmuchWorker — batched queries

**Files:**
- Create: `src/notmuchworker.h`, `src/notmuchworker.cpp`
- Modify: `src/CMakeLists.txt`

**Changed 2026-08-02, superseding the spec's "no unit test" position.**
`NotmuchWorker` IS unit-tested, against a throwaway notmuch database built in
a temporary directory. The spec deferred this to manual verification on the
grounds that testing needs a real database; the answer is to build a fake one
rather than to skip the tests. This matters more than for any other class,
because `applyTags` is the only code in the project that WRITES to a notmuch
index, and a bug there corrupts real mail state.

The fixture creates a Maildir tree with a handful of messages, runs
`notmuch new` against a generated config pointing at it, and sets
`NOTMUCH_CONFIG` for the test process. Nothing touches the developer's own
`~/Mail` or `~/.notmuch-config`.

Task 13's manual checklist remains, but as confirmation against real data
rather than as the only coverage.

- [ ] **Step 1: Write src/notmuchworker.h**

```cpp
#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

#include "types.h"

struct _notmuch_database;
typedef struct _notmuch_database notmuch_database_t;

/// Owns the only notmuch database handle in the process.
///
/// libnotmuch is not thread-safe and queries over a large database block, so
/// this object lives on its own thread and the UI reaches it only through
/// queued signals. No notmuch pointer ever leaves this class.
class NotmuchWorker : public QObject
{
    Q_OBJECT
public:
    /// notmuchConfigPath may be empty, in which case notmuch resolves its own
    /// config and therefore its own database.path.
    explicit NotmuchWorker(const QString &notmuchConfigPath, QObject *parent = nullptr);
    ~NotmuchWorker() override;

    /// Threads emitted per threadsReady() signal.
    static constexpr int kBatchSize = 200;

public slots:
    /// Runs a query. generation lets the UI discard results from a superseded
    /// query without the worker needing to know about cancellation.
    void runQuery(const QString &query, quint64 generation);

    /// Loads the messages of one thread, oldest first. matchQuery is the
    /// user's current query; messages matching it render expanded, the rest
    /// as stubs.
    void loadThread(const QString &threadId, const QString &matchQuery,
                    quint64 generation);

    /// Applies tag changes. Opens the database read-write, applies, and closes
    /// immediately: notmuch's write lock is exclusive process-wide, so holding
    /// it would block the user's cron `notmuch new`.
    void applyTags(const TagChange &change);

    /// Batch tagging over whole threads. The UI holds thread ids, not message
    /// ids, for rows it has not opened, so the resolution happens here where
    /// the database handle lives. This is the path the archive/flag/delete
    /// actions use on a multi-row selection.
    void applyTagsToThreads(const QStringList &threadIds,
                            const QStringList &add,
                            const QStringList &remove,
                            const QString &description);

signals:
    void threadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void queryFinished(int totalThreads, quint64 generation);
    void threadLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void tagsApplied(const TagChange &change);
    void errorOccurred(const QString &message);

private:
    bool openReadOnly();
    void close();

    QString m_configPath;
    notmuch_database_t *m_db = nullptr;
};
```

- [ ] **Step 2: Write src/notmuchworker.cpp**

```cpp
#include "notmuchworker.h"

#include <notmuch.h>

#include <QSet>

#include "nmraii.h"

namespace {

QStringList tagsOf(notmuch_message_t *message)
{
    QStringList result;
    NmTags tags(notmuch_message_get_tags(message));
    for (; notmuch_tags_valid(tags.get()); notmuch_tags_move_to_next(tags.get()))
        result.append(QString::fromUtf8(notmuch_tags_get(tags.get())));
    return result;
}

QStringList tagsOf(notmuch_thread_t *thread)
{
    QStringList result;
    NmTags tags(notmuch_thread_get_tags(thread));
    for (; notmuch_tags_valid(tags.get()); notmuch_tags_move_to_next(tags.get()))
        result.append(QString::fromUtf8(notmuch_tags_get(tags.get())));
    return result;
}

} // namespace

NotmuchWorker::NotmuchWorker(const QString &notmuchConfigPath, QObject *parent)
    : QObject(parent), m_configPath(notmuchConfigPath)
{
}

NotmuchWorker::~NotmuchWorker()
{
    close();
}

bool NotmuchWorker::openReadOnly()
{
    if (m_db)
        return true;

    char *error = nullptr;
    const notmuch_status_t status = notmuch_database_open_with_config(
        nullptr,                                        // let config decide path
        NOTMUCH_DATABASE_MODE_READ_ONLY,
        m_configPath.isEmpty() ? nullptr
                               : m_configPath.toLocal8Bit().constData(),
        nullptr,
        &m_db,
        &error);

    if (status != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Cannot open notmuch database: %1")
            .arg(QString::fromUtf8(error ? error : notmuch_status_to_string(status))));
        free(error);
        m_db = nullptr;
        return false;
    }
    return true;
}

void NotmuchWorker::close()
{
    if (m_db) {
        notmuch_database_destroy(m_db);
        m_db = nullptr;
    }
}

void NotmuchWorker::runQuery(const QString &query, quint64 generation)
{
    if (!openReadOnly())
        return;

    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(QStringLiteral("Invalid query: %1").arg(query));
        return;
    }
    notmuch_query_set_sort(nmQuery.get(), NOTMUCH_SORT_NEWEST_FIRST);

    notmuch_threads_t *rawThreads = nullptr;
    const notmuch_status_t status =
        notmuch_query_search_threads(nmQuery.get(), &rawThreads);
    if (status != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Query failed: %1")
            .arg(QString::fromUtf8(notmuch_status_to_string(status))));
        return;
    }
    NmThreads threads(rawThreads);

    QVector<ThreadSummary> batch;
    batch.reserve(kBatchSize);
    int total = 0;

    for (; notmuch_threads_valid(threads.get());
           notmuch_threads_move_to_next(threads.get())) {

        NmThread thread(notmuch_threads_get(threads.get()));
        if (!thread)
            continue;

        ThreadSummary summary;
        summary.threadId = QString::fromUtf8(notmuch_thread_get_thread_id(thread.get()));
        summary.subject = QString::fromUtf8(notmuch_thread_get_subject(thread.get()));
        summary.authors = QString::fromUtf8(notmuch_thread_get_authors(thread.get()));
        summary.date = QDateTime::fromSecsSinceEpoch(
            notmuch_thread_get_newest_date(thread.get()));
        summary.totalCount = notmuch_thread_get_total_messages(thread.get());
        summary.matchedCount = notmuch_thread_get_matched_messages(thread.get());
        summary.tags = tagsOf(thread.get());

        batch.append(summary);
        ++total;

        if (batch.size() >= kBatchSize) {
            emit threadsReady(batch, generation);
            batch.clear();
            batch.reserve(kBatchSize);
        }
    }

    if (!batch.isEmpty())
        emit threadsReady(batch, generation);

    emit queryFinished(total, generation);
}

void NotmuchWorker::loadThread(const QString &threadId,
                               const QString &matchQuery,
                               quint64 generation)
{
    if (!openReadOnly())
        return;

    // Which messages of the thread matched the user's query. Running the query
    // intersected with the thread is cheaper than testing each message.
    QSet<QString> matchedIds;
    if (!matchQuery.trimmed().isEmpty()) {
        const QString intersect =
            QStringLiteral("thread:%1 and (%2)").arg(threadId, matchQuery);
        NmQuery matchQ(
            notmuch_query_create(m_db, intersect.toUtf8().constData()));
        if (matchQ) {
            notmuch_messages_t *rawMatched = nullptr;
            if (notmuch_query_search_messages(matchQ.get(), &rawMatched)
                    == NOTMUCH_STATUS_SUCCESS) {
                NmMessages matched(rawMatched);
                for (; notmuch_messages_valid(matched.get());
                       notmuch_messages_move_to_next(matched.get())) {
                    NmMessage message(notmuch_messages_get(matched.get()));
                    if (message) {
                        matchedIds.insert(QString::fromUtf8(
                            notmuch_message_get_message_id(message.get())));
                    }
                }
            }
        }
    }

    const QString query = QStringLiteral("thread:%1").arg(threadId);
    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(QStringLiteral("Cannot load thread %1").arg(threadId));
        return;
    }
    notmuch_query_set_sort(nmQuery.get(), NOTMUCH_SORT_OLDEST_FIRST);

    notmuch_messages_t *rawMessages = nullptr;
    if (notmuch_query_search_messages(nmQuery.get(), &rawMessages)
            != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Cannot search thread %1").arg(threadId));
        return;
    }
    NmMessages messages(rawMessages);

    QVector<MessageRef> result;
    for (; notmuch_messages_valid(messages.get());
           notmuch_messages_move_to_next(messages.get())) {

        NmMessage message(notmuch_messages_get(messages.get()));
        if (!message)
            continue;

        MessageRef ref;
        ref.messageId = QString::fromUtf8(notmuch_message_get_message_id(message.get()));
        ref.filePath = QString::fromUtf8(notmuch_message_get_filename(message.get()));
        ref.tags = tagsOf(message.get());
        // With no query to intersect, everything counts as matched.
        ref.matched = matchedIds.isEmpty() || matchedIds.contains(ref.messageId);
        result.append(ref);
    }

    emit threadLoaded(result, generation);
}

void NotmuchWorker::applyTagsToThreads(const QStringList &threadIds,
                                       const QStringList &add,
                                       const QStringList &remove,
                                       const QString &description)
{
    if (threadIds.isEmpty())
        return;

    if (!openReadOnly())
        return;

    // Resolve every thread to its message ids in ONE query. Issuing a query per
    // thread would reopen the same Xapian cursor hundreds of times on a large
    // selection.
    QStringList terms;
    terms.reserve(threadIds.size());
    for (const QString &id : threadIds)
        terms.append(QStringLiteral("thread:%1").arg(id));

    const QString query = terms.join(QStringLiteral(" or "));

    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(QStringLiteral("Cannot resolve selected threads"));
        return;
    }

    notmuch_messages_t *rawMessages = nullptr;
    if (notmuch_query_search_messages(nmQuery.get(), &rawMessages)
            != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(QStringLiteral("Cannot resolve selected threads"));
        return;
    }
    NmMessages messages(rawMessages);

    QStringList messageIds;
    for (; notmuch_messages_valid(messages.get());
           notmuch_messages_move_to_next(messages.get())) {
        NmMessage message(notmuch_messages_get(messages.get()));
        if (message) {
            messageIds.append(
                QString::fromUtf8(notmuch_message_get_message_id(message.get())));
        }
    }

    if (messageIds.isEmpty()) {
        emit errorOccurred(QStringLiteral("Selected threads contain no messages"));
        return;
    }

    applyTags(TagChange{ messageIds, add, remove, description });
}

void NotmuchWorker::applyTags(const TagChange &change)
{
    if (change.messageIds.isEmpty())
        return;

    // The read-only handle must be closed first: notmuch allows only one open
    // handle per process.
    close();

    notmuch_database_t *db = nullptr;
    char *error = nullptr;
    const notmuch_status_t status = notmuch_database_open_with_config(
        nullptr,
        NOTMUCH_DATABASE_MODE_READ_WRITE,
        m_configPath.isEmpty() ? nullptr
                               : m_configPath.toLocal8Bit().constData(),
        nullptr,
        &db,
        &error);

    if (status != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(
            QStringLiteral("Cannot open database for writing (is a sync running?): %1")
                .arg(QString::fromUtf8(error ? error
                                             : notmuch_status_to_string(status))));
        free(error);
        return;
    }

    for (const QString &id : change.messageIds) {
        notmuch_message_t *raw = nullptr;
        if (notmuch_database_find_message(db, id.toUtf8().constData(), &raw)
                != NOTMUCH_STATUS_SUCCESS || !raw) {
            continue;
        }
        NmMessage message(raw);

        notmuch_message_freeze(message.get());
        for (const QString &tag : change.removed)
            notmuch_message_remove_tag(message.get(), tag.toUtf8().constData());
        for (const QString &tag : change.added)
            notmuch_message_add_tag(message.get(), tag.toUtf8().constData());
        notmuch_message_thaw(message.get());

        notmuch_message_tags_to_maildir_flags(message.get());
    }

    notmuch_database_close(db);
    notmuch_database_destroy(db);

    emit tagsApplied(change);
}
```

- [ ] **Step 3: Add to the library**

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
    notmuchworker.cpp
)
```

- [ ] **Step 4: Build and verify existing tests still pass**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: builds clean, five test binaries still PASS.

If `notmuch_database_open_with_config` is not declared, the installed notmuch is
older than 0.32. Check with `grep open_with_config /usr/include/notmuch.h`; the
verified system has 0.39, which has it.

- [ ] **Step 5: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp src/CMakeLists.txt
git commit -S -m "feat: add NotmuchWorker with batched queries and tag mutation"
```

---

## Task 9: ThreadListModel

**Files:**
- Create: `src/threadlistmodel.h`, `src/threadlistmodel.cpp`
- Create: `tests/test_threadlistmodel.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_threadlistmodel.cpp`:

```cpp
#include <QtTest>
#include <QAbstractItemModelTester>
#include "threadlistmodel.h"

class TestThreadListModel : public QObject
{
    Q_OBJECT
private slots:
    void startsEmpty();
    void appendsBatches();
    void clearResetsModel();
    void reportsSubjectAndAuthors();
    void updatesTagsForMessage();
    void modelPassesQtTester();
};

static ThreadSummary makeThread(const QString &id, const QString &subject)
{
    ThreadSummary t;
    t.threadId = id;
    t.subject = subject;
    t.authors = QStringLiteral("Alice");
    t.date = QDateTime::fromSecsSinceEpoch(1750000000);
    t.totalCount = 2;
    t.matchedCount = 1;
    t.tags = QStringList{ QStringLiteral("inbox"), QStringLiteral("unread") };
    return t;
}

void TestThreadListModel::startsEmpty()
{
    ThreadListModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), ThreadListModel::ColumnCount);
}

void TestThreadListModel::appendsBatches()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });
    QCOMPARE(model.rowCount(), 1);

    model.appendBatch({ makeThread(QStringLiteral("t2"), QStringLiteral("two")),
                        makeThread(QStringLiteral("t3"), QStringLiteral("three")) });
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.threadAt(2).threadId, QStringLiteral("t3"));
}

void TestThreadListModel::clearResetsModel()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });

    QSignalSpy spy(&model, &QAbstractItemModel::modelReset);
    model.clear();

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(spy.count(), 1);
}

void TestThreadListModel::reportsSubjectAndAuthors()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("hello")) });

    const QModelIndex subject = model.index(0, ThreadListModel::SubjectColumn);
    QCOMPARE(model.data(subject, Qt::DisplayRole).toString(),
             QStringLiteral("hello"));

    const QModelIndex authors = model.index(0, ThreadListModel::AuthorsColumn);
    QCOMPARE(model.data(authors, Qt::DisplayRole).toString(),
             QStringLiteral("Alice"));
}

void TestThreadListModel::updatesTagsForMessage()
{
    ThreadListModel model;
    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")) });
    QVERIFY(model.threadAt(0).isUnread());

    // Optimistic UI: the model changes before the worker confirms.
    model.applyTagChange(QStringLiteral("t1"), {}, { QStringLiteral("unread") });
    QVERIFY(!model.threadAt(0).isUnread());
}

void TestThreadListModel::modelPassesQtTester()
{
    ThreadListModel model;
    // Catches signal/rowCount contract violations that hand-written tests miss.
    QAbstractItemModelTester tester(&model,
        QAbstractItemModelTester::FailureReportingMode::QtTest);

    model.appendBatch({ makeThread(QStringLiteral("t1"), QStringLiteral("one")),
                        makeThread(QStringLiteral("t2"), QStringLiteral("two")) });
    model.clear();
}

QTEST_MAIN(TestThreadListModel)
#include "test_threadlistmodel.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Append to `tests/CMakeLists.txt`:
```cmake
add_qtmaildir_test(threadlistmodel)
```

Run: `cmake -S . -B build -G Ninja && cmake --build build`
Expected: FAIL, `threadlistmodel.h: No such file or directory`.

- [ ] **Step 3: Write src/threadlistmodel.h**

```cpp
#pragma once

#include <QAbstractTableModel>
#include <QVector>

#include "types.h"

/// Table model over query results, filled in batches so a large query paints
/// its first screenful immediately.
class ThreadListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        DateColumn = 0,
        AuthorsColumn,
        SubjectColumn,
        TagsColumn,
        ColumnCount,
    };

    explicit ThreadListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    void appendBatch(const QVector<ThreadSummary> &batch);
    void clear();

    ThreadSummary threadAt(int row) const;

    /// Applies a tag change locally so the UI updates before the worker
    /// confirms. Reverted by the caller if the worker reports failure.
    void applyTagChange(const QString &threadId, const QStringList &added,
                        const QStringList &removed);

private:
    QVector<ThreadSummary> m_threads;
};
```

- [ ] **Step 4: Write src/threadlistmodel.cpp**

```cpp
#include "threadlistmodel.h"

#include <QFont>

ThreadListModel::ThreadListModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int ThreadListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_threads.size();
}

int ThreadListModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ThreadListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_threads.size())
        return {};

    const ThreadSummary &thread = m_threads.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case DateColumn:
            return thread.date.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        case AuthorsColumn:
            return thread.authors;
        case SubjectColumn:
            return thread.totalCount > 1
                ? QStringLiteral("%1 (%2)").arg(thread.subject)
                      .arg(thread.totalCount)
                : thread.subject;
        case TagsColumn:
            return thread.tags.join(QLatin1Char(' '));
        default:
            return {};
        }
    }

    if (role == Qt::FontRole && thread.isUnread()) {
        QFont font;
        font.setBold(true);
        return font;
    }

    return {};
}

QVariant ThreadListModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case DateColumn:    return QStringLiteral("Date");
    case AuthorsColumn: return QStringLiteral("From");
    case SubjectColumn: return QStringLiteral("Subject");
    case TagsColumn:    return QStringLiteral("Tags");
    default:            return {};
    }
}

void ThreadListModel::appendBatch(const QVector<ThreadSummary> &batch)
{
    if (batch.isEmpty())
        return;

    const int first = m_threads.size();
    beginInsertRows({}, first, first + batch.size() - 1);
    m_threads.append(batch);
    endInsertRows();
}

void ThreadListModel::clear()
{
    beginResetModel();
    m_threads.clear();
    endResetModel();
}

ThreadSummary ThreadListModel::threadAt(int row) const
{
    if (row < 0 || row >= m_threads.size())
        return {};
    return m_threads.at(row);
}

void ThreadListModel::applyTagChange(const QString &threadId,
                                     const QStringList &added,
                                     const QStringList &removed)
{
    for (int row = 0; row < m_threads.size(); ++row) {
        if (m_threads.at(row).threadId != threadId)
            continue;

        QStringList &tags = m_threads[row].tags;
        for (const QString &tag : removed)
            tags.removeAll(tag);
        for (const QString &tag : added) {
            if (!tags.contains(tag))
                tags.append(tag);
        }

        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        return;
    }
}
```

- [ ] **Step 5: Add to the library and run tests**

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
    notmuchworker.cpp
    threadlistmodel.cpp
)
```

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: six test binaries PASS.

- [ ] **Step 6: Commit**

```bash
git add src/threadlistmodel.h src/threadlistmodel.cpp src/CMakeLists.txt tests/
git commit -S -m "feat: add ThreadListModel with batch append"
```

---

## Task 10: MailSync — QProcess wrapper

**Files:**
- Create: `src/mailsync.h`, `src/mailsync.cpp`
- Create: `tests/test_mailsync.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_mailsync.cpp`:

```cpp
#include <QtTest>
#include <QSignalSpy>
#include "mailsync.h"

class TestMailSync : public QObject
{
    Q_OBJECT
private slots:
    void unavailableWhenCommandEmpty();
    void successfulRunEmitsFinished();
    void failedRunReportsExitCode();
    void capturesOutput();
    void refusesConcurrentRuns();
};

void TestMailSync::unavailableWhenCommandEmpty()
{
    MailSync sync(QString());
    QVERIFY(!sync.isAvailable());
    QVERIFY(!sync.start());
}

void TestMailSync::successfulRunEmitsFinished()
{
    MailSync sync(QStringLiteral("/bin/true"));
    QVERIFY(sync.isAvailable());

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toBool(), true);
}

void TestMailSync::failedRunReportsExitCode()
{
    MailSync sync(QStringLiteral("/bin/false"));

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QCOMPARE(spy.first().at(0).toBool(), false);
}

void TestMailSync::capturesOutput()
{
    MailSync sync(QStringLiteral("/bin/echo syncing"));

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QVERIFY(sync.log().contains(QStringLiteral("syncing")));
}

void TestMailSync::refusesConcurrentRuns()
{
    MailSync sync(QStringLiteral("/bin/sleep 2"));
    QVERIFY(sync.start());
    // The cron sync and this one share a flock; starting twice from the GUI is
    // still refused locally so the button cannot queue runs.
    QVERIFY(!sync.start());
    QVERIFY(sync.isRunning());
}

QTEST_MAIN(TestMailSync)
#include "test_mailsync.moc"
```

- [ ] **Step 2: Register and run to verify it fails**

Append to `tests/CMakeLists.txt`:
```cmake
add_qtmaildir_test(mailsync)
```

Run: `cmake -S . -B build -G Ninja && cmake --build build`
Expected: FAIL, `mailsync.h: No such file or directory`.

- [ ] **Step 3: Write src/mailsync.h**

```cpp
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

/// Runs the configured external sync command.
///
/// qtmaildir deliberately does not implement sync itself. The existing script
/// holds a flock that is the shared mutex between the user's cron sync, which
/// runs every 10 minutes, and any manual sync; running the script joins that
/// mutex, whereas a built-in implementation would sit outside it and could run
/// mbsync concurrently with cron, corrupting Maildir UID state.
class MailSync : public QObject
{
    Q_OBJECT
public:
    explicit MailSync(const QString &command, QObject *parent = nullptr);

    /// False when no command is configured; the UI disables its Sync button.
    bool isAvailable() const { return !m_command.isEmpty(); }
    bool isRunning() const;

    /// Returns false if unavailable or already running.
    bool start();

    QString log() const { return m_log; }

signals:
    void started();
    void outputReceived(const QString &chunk);
    void finished(bool success, int exitCode);

private:
    void handleReadyRead();
    void handleFinished(int exitCode, QProcess::ExitStatus status);

    QString m_command;
    QProcess m_process;
    QString m_log;
};
```

- [ ] **Step 4: Write src/mailsync.cpp**

```cpp
#include "mailsync.h"

MailSync::MailSync(const QString &command, QObject *parent)
    : QObject(parent), m_command(command)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_process, &QProcess::readyRead,
            this, &MailSync::handleReadyRead);
    connect(&m_process, &QProcess::finished,
            this, &MailSync::handleFinished);
}

bool MailSync::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

bool MailSync::start()
{
    if (!isAvailable() || isRunning())
        return false;

    m_log.clear();

    // splitCommand handles quoted arguments; running through a shell would make
    // a config value into an injection point.
    const QStringList parts = QProcess::splitCommand(m_command);
    if (parts.isEmpty())
        return false;

    m_process.setProgram(parts.first());
    m_process.setArguments(parts.mid(1));
    m_process.start();

    if (!m_process.waitForStarted(5000))
        return false;

    emit started();
    return true;
}

void MailSync::handleReadyRead()
{
    const QString chunk = QString::fromUtf8(m_process.readAll());
    m_log += chunk;
    emit outputReceived(chunk);
}

void MailSync::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    // Drain anything buffered at exit.
    handleReadyRead();

    const bool success =
        status == QProcess::NormalExit && exitCode == 0;
    emit finished(success, exitCode);
}
```

- [ ] **Step 5: Add to the library and run tests**

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
    notmuchworker.cpp
    threadlistmodel.cpp
    mailsync.cpp
)
```

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: seven test binaries PASS.

- [ ] **Step 6: Commit**

```bash
git add src/mailsync.h src/mailsync.cpp src/CMakeLists.txt tests/
git commit -S -m "feat: add MailSync process wrapper"
```

---

## Task 11: MessageView — the locked-down web view

**Files:**
- Create: `src/messageview.h`, `src/messageview.cpp`
- Modify: `src/CMakeLists.txt`

No unit test: this is a widget that needs a live `QWebEngineProfile`. Its policy
logic is already tested in Task 5; this task only wires it. Verified manually in
Task 13.

- [ ] **Step 1: Write src/messageview.h**

```cpp
#pragma once

#include <QList>
#include <QWidget>

#include "htmlbuilder.h"
#include "mimeparser.h"

class QLabel;
class QPushButton;
class QWebEngineView;
class QWebEngineProfile;
class CidSchemeHandler;
class RequestInterceptor;

/// The message pane: thread header, body, attachment bar.
///
/// A whole thread renders into one web view. A newsletter thread can hold
/// dozens of messages, and one view per message would spawn one Chromium
/// render process per message.
class MessageView : public QWidget
{
    Q_OBJECT
public:
    explicit MessageView(QWidget *parent = nullptr);
    ~MessageView() override;

    /// Renders a whole thread, oldest first. Items whose expanded flag is
    /// false collapse to a one-line stub.
    void showThread(const QList<ThreadRenderItem> &items);

    void showError(const QString &text, const QString &filePath);
    void clear();

public slots:
    void toggleHtml();
    void loadRemoteContent();

signals:
    void statusMessage(const QString &text);

private:
    void render();
    void updateHeader();

    QList<ThreadRenderItem> m_items;
    bool m_preferHtml = true;

    QWebEngineProfile *m_profile = nullptr;
    QWebEngineView *m_view = nullptr;
    RequestInterceptor *m_interceptor = nullptr;
    CidSchemeHandler *m_cidHandler = nullptr;

    QLabel *m_headerLabel = nullptr;
    QLabel *m_blockedLabel = nullptr;
    QPushButton *m_loadRemoteButton = nullptr;
    QWidget *m_attachmentBar = nullptr;
};
```

- [ ] **Step 2: Write src/messageview.cpp**

```cpp
#include "messageview.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebEnginePage>

#include <algorithm>

#include "cidschemehandler.h"
#include "htmlbuilder.h"
#include "requestinterceptor.h"

namespace {

/// Intercepts link clicks so a message can never navigate the pane.
class MessagePage : public QWebEnginePage
{
public:
    MessagePage(QWebEngineProfile *profile, QObject *parent)
        : QWebEnginePage(profile, parent) {}

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type,
                                 bool isMainFrame) override
    {
        if (type == NavigationTypeTyped || url.scheme() == QLatin1String("qtmaildir"))
            return true;

        if (type == NavigationTypeLinkClicked) {
            QDesktopServices::openUrl(url);
            return false;
        }
        return isMainFrame ? false : true;
    }
};

} // namespace

MessageView::MessageView(QWidget *parent)
    : QWidget(parent)
{
    // Off-the-record profile: no cookies, no cache, nothing persisted.
    m_profile = new QWebEngineProfile(this);
    m_profile->setHttpCacheType(QWebEngineProfile::NoCache);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

    m_interceptor = new RequestInterceptor(this);
    m_profile->setUrlRequestInterceptor(m_interceptor);

    m_cidHandler = new CidSchemeHandler(this);
    m_profile->installUrlSchemeHandler(QByteArrayLiteral("cid"), m_cidHandler);

    m_view = new QWebEngineView(this);
    m_view->setPage(new MessagePage(m_profile, m_view));

    QWebEngineSettings *settings = m_view->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setTextFormat(Qt::RichText);
    m_headerLabel->setWordWrap(true);
    m_headerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_blockedLabel = new QLabel(tr("Remote content blocked"), this);
    m_loadRemoteButton = new QPushButton(tr("Load remote content"), this);
    connect(m_loadRemoteButton, &QPushButton::clicked,
            this, &MessageView::loadRemoteContent);

    auto *blockedRow = new QHBoxLayout;
    blockedRow->addWidget(m_blockedLabel);
    blockedRow->addWidget(m_loadRemoteButton);
    blockedRow->addStretch();

    m_attachmentBar = new QWidget(this);
    new QHBoxLayout(m_attachmentBar);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_headerLabel);
    layout->addLayout(blockedRow);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_attachmentBar);

    clear();
}

MessageView::~MessageView() = default;

void MessageView::clear()
{
    m_items.clear();
    m_view->setHtml(QString());
    m_headerLabel->clear();
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();
}

void MessageView::showThread(const QList<ThreadRenderItem> &items)
{
    m_items = items;
    m_preferHtml = true;

    // Every thread starts from a clean policy: no remote grant carries over.
    m_interceptor->resetForNewMessage();

    // Flatten every message's inline parts into one namespaced map, so two
    // messages sharing a Content-ID resolve to different parts.
    QHash<QString, InlinePart> allParts;
    QSet<QString> cids;

    for (const ThreadRenderItem &item : m_items) {
        for (auto it = item.message.inlineParts.cbegin();
             it != item.message.inlineParts.cend(); ++it) {
            const QString key =
                CidSchemeHandler::namespacedKey(item.cidPrefix, it.key());
            allParts.insert(key, it.value());
            cids.insert(key);
        }
    }

    m_interceptor->setAllowedCids(cids);
    m_cidHandler->setParts(allParts);

    // REQUIRED by RequestInterceptor: it trusts only this exact URL on the
    // qtmaildir: scheme and fails closed otherwise, so this must match the
    // base URL passed to setHtml() in render() or nothing renders at all.
    m_interceptor->setDocumentUrl(QUrl(QStringLiteral("qtmaildir://message")));

    updateHeader();
    render();
}

void MessageView::showError(const QString &text, const QString &filePath)
{
    m_items.clear();
    m_headerLabel->setText(tr("<b>Cannot display message</b>"));
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();

    const QString html = QStringLiteral(
        "<html><body><p>%1</p><p><code>%2</code></p></body></html>")
        .arg(text.toHtmlEscaped(), filePath.toHtmlEscaped());
    m_view->setHtml(html, QUrl(QStringLiteral("qtmaildir://message")));
}

void MessageView::updateHeader()
{
    if (m_items.isEmpty()) {
        m_headerLabel->clear();
        return;
    }

    // The thread's subject comes from its first message; later replies carry
    // Re: prefixes that add nothing.
    const QString subject = m_items.first().message.subject;

    m_headerLabel->setText(
        QStringLiteral("<b>%1</b><br><small>%2</small>")
            .arg(subject.toHtmlEscaped(),
                 tr("%n message(s) in thread", "", m_items.size())));
}

void MessageView::render()
{
    const HtmlBuilder::Mode mode =
        m_preferHtml ? HtmlBuilder::PreferHtml : HtmlBuilder::ForcePlain;

    // The base URL uses a scheme the interceptor recognises, so the document
    // itself loads while everything it references is still filtered.
    m_view->setHtml(HtmlBuilder::buildThread(m_items, mode),
                    QUrl(QStringLiteral("qtmaildir://message")));

    // Blocking is discovered during load, so check shortly afterwards.
    QTimer::singleShot(300, this, [this]() {
        const bool blocked = m_interceptor->blockedAnything()
                             && !m_interceptor->allowRemote();
        m_blockedLabel->setVisible(blocked);
        m_loadRemoteButton->setVisible(blocked);
    });
}

void MessageView::toggleHtml()
{
    const bool anyHtml = std::any_of(
        m_items.cbegin(), m_items.cend(),
        [](const ThreadRenderItem &item) { return item.message.hasHtml(); });

    if (!anyHtml) {
        emit statusMessage(tr("No message in this thread has an HTML part"));
        return;
    }
    m_preferHtml = !m_preferHtml;
    render();
}

void MessageView::loadRemoteContent()
{
    // Applies to this thread only and is cleared by the next showThread().
    m_interceptor->setAllowRemote(true);
    m_blockedLabel->hide();
    m_loadRemoteButton->hide();
    render();
}
```

- [ ] **Step 3: Add to the library and build**

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
    notmuchworker.cpp
    threadlistmodel.cpp
    mailsync.cpp
    messageview.cpp
)
```

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: builds clean, seven test binaries still PASS.

- [ ] **Step 4: Commit**

```bash
git add src/messageview.h src/messageview.cpp src/CMakeLists.txt
git commit -S -m "feat: add MessageView with locked-down web engine profile"
```

---

## Task 12: MainWindow and application entry point

**Files:**
- Create: `src/mainwindow.h`, `src/mainwindow.cpp`
- Modify: `src/main.cpp`, `src/CMakeLists.txt`

- [ ] **Step 1: Write src/mainwindow.h**

```cpp
#pragma once

#include <QHash>
#include <QMainWindow>
#include <QThread>
#include <QUndoCommand>
#include <QUndoStack>

#include <functional>

#include "config.h"
#include "keymap.h"
#include "types.h"

class QLineEdit;
class QTableView;
class QLabel;
class QPushButton;
class QComboBox;
class QPlainTextEdit;

class ThreadListModel;
class MessageView;
class MailSync;
class NotmuchWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const Config &config, QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void runCurrentQuery();
    void onThreadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void onQueryFinished(int total, quint64 generation);
    void onThreadSelected(const QModelIndex &current, const QModelIndex &previous);
    void onThreadLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void onWorkerError(const QString &message);
    void onSyncFinished(bool success, int exitCode);

private:
    void buildUi();
    void registerActions();
    void wireWorker();
    void showWarnings();

    void tagSelected(const QStringList &add, const QStringList &remove,
                     const QString &description);

    /// Sends a tag change for a set of threads without touching the undo stack.
    /// Both tagSelected() and ThreadTagCommand route through this.
    void sendThreadTagChange(const QStringList &threadIds,
                             const QStringList &add,
                             const QStringList &remove,
                             const QString &description);

    friend class ThreadTagCommand;

    Config m_config;
    KeyMap m_keyMap;

    QThread m_workerThread;
    NotmuchWorker *m_worker = nullptr;

    ThreadListModel *m_model = nullptr;
    MessageView *m_messageView = nullptr;
    MailSync *m_sync = nullptr;
    QUndoStack m_undoStack;

    QLineEdit *m_queryEdit = nullptr;
    QTableView *m_threadView = nullptr;
    QComboBox *m_accountBox = nullptr;
    QPushButton *m_syncButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_syncLog = nullptr;

    QHash<QString, std::function<void()>> m_actions;
    quint64 m_generation = 0;
    QString m_lastQuery;
    QString m_currentThreadId;
    QVector<MessageRef> m_currentMessages;
};

/// Undo entry for a tag change over a set of threads.
///
/// Stores thread ids rather than message ids, so undo re-resolves them on the
/// worker and stays correct even if the selection has moved on.
class ThreadTagCommand : public QUndoCommand
{
public:
    ThreadTagCommand(MainWindow *window, const QStringList &threadIds,
                     const QStringList &add, const QStringList &remove,
                     const QString &description)
        : QUndoCommand(description), m_window(window), m_threadIds(threadIds),
          m_add(add), m_remove(remove), m_description(description) {}

    /// The stack calls redo() when the command is pushed. The change has
    /// already been sent by that point, so the first call is skipped.
    void redo() override
    {
        if (m_firstRedo) {
            m_firstRedo = false;
            return;
        }
        m_window->sendThreadTagChange(m_threadIds, m_add, m_remove,
                                      m_description);
    }

    void undo() override
    {
        // Inverted: what was added is removed and vice versa.
        m_window->sendThreadTagChange(m_threadIds, m_remove, m_add,
                                      QStringLiteral("Undo %1").arg(m_description));
    }

private:
    MainWindow *m_window;
    QStringList m_threadIds;
    QStringList m_add;
    QStringList m_remove;
    QString m_description;
    bool m_firstRedo = true;
};
```

- [ ] **Step 2: Write src/mainwindow.cpp**

```cpp
#include "mainwindow.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>

#include "mailsync.h"
#include "messageview.h"
#include "mimeparser.h"
#include "notmuchworker.h"
#include "threadlistmodel.h"

MainWindow::MainWindow(const Config &config, QWidget *parent)
    : QMainWindow(parent), m_config(config)
{
    qRegisterMetaType<ThreadSummary>();
    qRegisterMetaType<MessageRef>();
    qRegisterMetaType<TagChange>();
    qRegisterMetaType<QVector<ThreadSummary>>();
    qRegisterMetaType<QVector<MessageRef>>();

    m_keyMap.loadDefaults();
    {
        QSettings settings(Config::defaultPath(), QSettings::IniFormat);
        m_keyMap.loadOverrides(settings);
    }

    buildUi();
    registerActions();
    wireWorker();
    showWarnings();

    installEventFilter(this);

    if (!m_config.savedQueries().isEmpty()) {
        m_queryEdit->setText(m_config.savedQueries().first().query);
        runCurrentQuery();
    }
}

MainWindow::~MainWindow()
{
    m_workerThread.quit();
    m_workerThread.wait();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // Query row.
    auto *queryRow = new QHBoxLayout;
    m_accountBox = new QComboBox(central);
    m_accountBox->addItem(tr("All accounts"), QString());
    for (const Account &account : m_config.accounts())
        m_accountBox->addItem(account.key, account.key);

    m_queryEdit = new QLineEdit(central);
    m_queryEdit->setPlaceholderText(tr("notmuch query, e.g. tag:inbox"));
    connect(m_queryEdit, &QLineEdit::returnPressed,
            this, &MainWindow::runCurrentQuery);

    m_syncButton = new QPushButton(tr("Sync"), central);
    m_sync = new MailSync(m_config.syncCommand(), this);
    m_syncButton->setEnabled(m_sync->isAvailable());
    if (!m_sync->isAvailable()) {
        m_syncButton->setToolTip(
            tr("No sync command configured ([sync] command in qtmaildir.conf)"));
    }
    connect(m_syncButton, &QPushButton::clicked, this, [this]() {
        if (!m_sync->start())
            m_statusLabel->setText(tr("Sync already running"));
    });
    connect(m_sync, &MailSync::finished, this, &MainWindow::onSyncFinished);
    connect(m_sync, &MailSync::outputReceived, this, [this](const QString &chunk) {
        m_syncLog->appendPlainText(chunk.trimmed());
    });

    queryRow->addWidget(m_accountBox);
    queryRow->addWidget(m_queryEdit, 1);
    queryRow->addWidget(m_syncButton);
    layout->addLayout(queryRow);

    // Saved query buttons.
    auto *savedRow = new QHBoxLayout;
    for (const SavedQuery &saved : m_config.savedQueries()) {
        auto *button = new QPushButton(saved.name, central);
        connect(button, &QPushButton::clicked, this, [this, saved]() {
            m_queryEdit->setText(saved.query);
            runCurrentQuery();
        });
        savedRow->addWidget(button);
    }
    savedRow->addStretch();
    layout->addLayout(savedRow);

    // Thread list and message pane.
    m_model = new ThreadListModel(this);
    m_threadView = new QTableView(central);
    m_threadView->setModel(m_model);
    m_threadView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_threadView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_threadView->verticalHeader()->hide();
    m_threadView->horizontalHeader()->setStretchLastSection(false);
    m_threadView->horizontalHeader()->setSectionResizeMode(
        ThreadListModel::SubjectColumn, QHeaderView::Stretch);

    connect(m_threadView->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onThreadSelected);

    m_messageView = new MessageView(central);
    connect(m_messageView, &MessageView::statusMessage,
            this, [this](const QString &text) { m_statusLabel->setText(text); });

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(m_threadView);
    splitter->addWidget(m_messageView);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    m_syncLog = new QPlainTextEdit(central);
    m_syncLog->setReadOnly(true);
    m_syncLog->setMaximumHeight(120);
    m_syncLog->hide();
    layout->addWidget(m_syncLog);

    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);

    resize(1200, 800);
    setWindowTitle(tr("qtmaildir"));
}

void MainWindow::registerActions()
{
    m_actions[QStringLiteral("focus_query")] = [this]() {
        m_queryEdit->setFocus();
        m_queryEdit->selectAll();
    };
    m_actions[QStringLiteral("next_thread")] = [this]() {
        const QModelIndex current = m_threadView->currentIndex();
        const int row = current.isValid() ? current.row() + 1 : 0;
        if (row < m_model->rowCount())
            m_threadView->selectRow(row);
    };
    m_actions[QStringLiteral("prev_thread")] = [this]() {
        const QModelIndex current = m_threadView->currentIndex();
        if (current.isValid() && current.row() > 0)
            m_threadView->selectRow(current.row() - 1);
    };
    m_actions[QStringLiteral("open_thread")] = [this]() {
        m_threadView->setFocus();
    };
    m_actions[QStringLiteral("archive")] = [this]() {
        tagSelected({}, { QStringLiteral("inbox") }, tr("Archive"));
    };
    m_actions[QStringLiteral("delete")] = [this]() {
        tagSelected({ QStringLiteral("deleted") }, {}, tr("Delete"));
    };
    m_actions[QStringLiteral("spam")] = [this]() {
        tagSelected({ QStringLiteral("spam") }, { QStringLiteral("inbox") },
                    tr("Mark spam"));
    };
    m_actions[QStringLiteral("flag")] = [this]() {
        tagSelected({ QStringLiteral("flagged") }, {}, tr("Flag"));
    };
    m_actions[QStringLiteral("toggle_unread")] = [this]() {
        const QModelIndex current = m_threadView->currentIndex();
        if (!current.isValid())
            return;
        const ThreadSummary thread = m_model->threadAt(current.row());
        if (thread.isUnread())
            tagSelected({}, { QStringLiteral("unread") }, tr("Mark read"));
        else
            tagSelected({ QStringLiteral("unread") }, {}, tr("Mark unread"));
    };
    m_actions[QStringLiteral("toggle_html")] = [this]() {
        m_messageView->toggleHtml();
    };
    m_actions[QStringLiteral("load_remote")] = [this]() {
        m_messageView->loadRemoteContent();
    };
    m_actions[QStringLiteral("undo")] = [this]() {
        if (m_undoStack.canUndo())
            m_undoStack.undo();
        else
            m_statusLabel->setText(tr("Nothing to undo"));
    };
    m_actions[QStringLiteral("sync")] = [this]() {
        if (m_sync->isAvailable())
            m_sync->start();
    };
    m_actions[QStringLiteral("quit")] = [this]() { close(); };
}

void MainWindow::wireWorker()
{
    m_worker = new NotmuchWorker(m_config.notmuchConfig());
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &NotmuchWorker::threadsReady,
            this, &MainWindow::onThreadsReady);
    connect(m_worker, &NotmuchWorker::queryFinished,
            this, &MainWindow::onQueryFinished);
    connect(m_worker, &NotmuchWorker::threadLoaded,
            this, &MainWindow::onThreadLoaded);
    connect(m_worker, &NotmuchWorker::errorOccurred,
            this, &MainWindow::onWorkerError);

    m_workerThread.start();
}

void MainWindow::showWarnings()
{
    QStringList warnings = m_config.warnings() + m_keyMap.warnings();
    if (warnings.isEmpty())
        return;

    // Non-fatal: the app runs degraded rather than refusing to start.
    m_statusLabel->setText(
        tr("%1 configuration warning(s); see Help").arg(warnings.size()));
    QMessageBox::warning(this, tr("Configuration warnings"),
                         warnings.join(QLatin1Char('\n')));
}

void MainWindow::runCurrentQuery()
{
    QString query = m_queryEdit->text().trimmed();

    const QString accountKey = m_accountBox->currentData().toString();
    if (!accountKey.isEmpty())
        query = m_config.account(accountKey).scopedQuery(query);

    if (query.isEmpty())
        return;

    // Kept so loadThread() can work out which messages of a thread matched.
    m_lastQuery = query;

    ++m_generation;
    m_model->clear();
    m_messageView->clear();
    m_statusLabel->setText(tr("Searching..."));

    QMetaObject::invokeMethod(m_worker, "runQuery", Qt::QueuedConnection,
                              Q_ARG(QString, query),
                              Q_ARG(quint64, m_generation));
}

void MainWindow::onThreadsReady(const QVector<ThreadSummary> &threads,
                                quint64 generation)
{
    if (generation != m_generation)
        return;  // Superseded by a newer query.
    m_model->appendBatch(threads);
}

void MainWindow::onQueryFinished(int total, quint64 generation)
{
    if (generation != m_generation)
        return;
    m_statusLabel->setText(tr("%n thread(s)", "", total));
}

void MainWindow::onThreadSelected(const QModelIndex &current,
                                  const QModelIndex &)
{
    if (!current.isValid())
        return;

    m_currentThreadId = m_model->threadAt(current.row()).threadId;
    QMetaObject::invokeMethod(m_worker, "loadThread", Qt::QueuedConnection,
                              Q_ARG(QString, m_currentThreadId),
                              Q_ARG(QString, m_lastQuery),
                              Q_ARG(quint64, m_generation));
}

void MainWindow::onThreadLoaded(const QVector<MessageRef> &messages,
                                quint64 generation)
{
    if (generation != m_generation || messages.isEmpty())
        return;

    m_currentMessages = messages;

    MimeParser parser;
    QList<ThreadRenderItem> items;
    items.reserve(messages.size());

    for (int i = 0; i < messages.size(); ++i) {
        const MessageRef &ref = messages.at(i);

        ThreadRenderItem item;
        item.message = parser.parse(ref.filePath);

        if (!item.message.ok) {
            // One unreadable message must not lose the rest of the thread, so
            // it becomes an inline note rather than replacing the whole pane.
            item.message = {};
            item.message.ok = true;
            item.message.from = tr("(unreadable message)");
            item.message.subject = ref.filePath;
            item.message.plainBody =
                tr("This message could not be parsed.\n%1").arg(ref.filePath);
        }

        // Namespace prefix keeps cid: references distinct across the thread.
        item.cidPrefix = QStringLiteral("m%1").arg(i);

        // Matched messages open; the rest collapse to a stub. The last message
        // always opens, so a thread never renders as nothing but stubs.
        item.expanded = ref.matched || i == messages.size() - 1;

        items.append(item);
    }

    m_messageView->showThread(items);
}

void MainWindow::onWorkerError(const QString &message)
{
    m_statusLabel->setText(message);
}

void MainWindow::onSyncFinished(bool success, int exitCode)
{
    if (success) {
        m_statusLabel->setText(tr("Sync complete"));
        runCurrentQuery();
    } else {
        m_statusLabel->setText(tr("Sync failed (exit %1)").arg(exitCode));
        m_syncLog->show();
    }
}

void MainWindow::tagSelected(const QStringList &add, const QStringList &remove,
                             const QString &description)
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

    QStringList threadIds;
    threadIds.reserve(rows.size());
    for (const QModelIndex &index : rows)
        threadIds.append(m_model->threadAt(index.row()).threadId);

    sendThreadTagChange(threadIds, add, remove, description);

    // Pushed for undo. The inverse re-resolves the same threads, so it works
    // whether or not those rows are still selected.
    m_undoStack.push(new ThreadTagCommand(this, threadIds, add, remove,
                                          description));

    m_statusLabel->setText(
        tr("%1: %n thread(s)", "", threadIds.size()).arg(description));
}

void MainWindow::sendThreadTagChange(const QStringList &threadIds,
                                     const QStringList &add,
                                     const QStringList &remove,
                                     const QString &description)
{
    // Optimistic: the rows change now, so a bulk archive of hundreds of threads
    // feels instant. onWorkerError() reverts if the write fails.
    for (const QString &threadId : threadIds)
        m_model->applyTagChange(threadId, add, remove);

    // The worker resolves thread ids to message ids: the UI does not hold
    // message ids for rows it never opened.
    QMetaObject::invokeMethod(m_worker, "applyTagsToThreads",
                              Qt::QueuedConnection,
                              Q_ARG(QStringList, threadIds),
                              Q_ARG(QStringList, add),
                              Q_ARG(QStringList, remove),
                              Q_ARG(QString, description));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(watched, event);

    // The query bar must receive ordinary typing, so single-key bindings are
    // suppressed while it has focus.
    if (m_queryEdit->hasFocus())
        return QMainWindow::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const QKeySequence sequence(keyEvent->keyCombination());

    const QString action = m_keyMap.actionFor(sequence);
    if (action.isEmpty() || !m_actions.contains(action))
        return QMainWindow::eventFilter(watched, event);

    m_actions.value(action)();
    return true;
}
```

- [ ] **Step 3: Rewrite src/main.cpp**

```cpp
#include <QApplication>
#include <QMessageBox>
#include <QWebEngineUrlScheme>

#include <notmuch.h>

#include "config.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // Custom schemes must be registered before QApplication is constructed.
    {
        QWebEngineUrlScheme scheme(QByteArrayLiteral("cid"));
        scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                        | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
        QWebEngineUrlScheme::registerScheme(scheme);
    }
    {
        QWebEngineUrlScheme scheme(QByteArrayLiteral("qtmaildir"));
        scheme.setFlags(QWebEngineUrlScheme::SecureScheme);
        QWebEngineUrlScheme::registerScheme(scheme);
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qtmaildir"));
    app.setOrganizationName(QStringLiteral("qtmaildir"));

    // Fail loudly on an ABI mismatch rather than crashing later.
    if (LIBNOTMUCH_MAJOR_VERSION < 5) {
        QMessageBox::critical(nullptr, QObject::tr("qtmaildir"),
            QObject::tr("libnotmuch 5 or newer is required."));
        return 1;
    }

    Config config;
    config.load(Config::defaultPath());

    MainWindow window(config);
    window.show();

    return app.exec();
}
```

- [ ] **Step 4: Update src/CMakeLists.txt and build**

```cmake
add_library(qtmaildir_lib STATIC
    keymap.cpp
    config.cpp
    mimeparser.cpp
    requestinterceptor.cpp
    htmlbuilder.cpp
    cidschemehandler.cpp
    notmuchworker.cpp
    threadlistmodel.cpp
    mailsync.cpp
    messageview.cpp
    mainwindow.cpp
)
```

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: builds clean, seven test binaries PASS.

- [ ] **Step 5: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp src/main.cpp src/CMakeLists.txt
git commit -S -m "feat: add MainWindow wiring query, list, message, and sync"
```

---

## Task 13: Manual verification against a real database

The notmuch layer has no automated test by design. This task is the compensating
control. Do not skip it.

**Files:**
- Create: `docs/manual-verification.md`

- [ ] **Step 1: Write a starter config**

```bash
mkdir -p ~/.config/qtmaildir
```

Create `~/.config/qtmaildir/qtmaildir.conf` with the real account keys and
maildir subdirectories from the user's `~/Mail`. Confirm each maildir exists:

```bash
ls ~/Mail
```

- [ ] **Step 2: Run the application**

```bash
./build/src/qtmaildir
```

- [ ] **Step 3: Walk the checklist, recording results**

Create `docs/manual-verification.md` and record pass/fail for each:

1. Startup shows no configuration warnings with a valid config.
2. `tag:inbox` returns threads; the count in the status bar matches
   `notmuch count --output=threads tag:inbox`.
3. A large query (`*`) paints the first rows within a second and keeps filling.
4. Typing a new query while one is running discards the old results.
5. A malformed query (`tag:`) reports an error and does not crash.
6. Selecting a thread renders every message in it, oldest first.
7. In a thread where only some messages matched the query, the rest appear as
   one-line stubs.
8. A thread with many messages (find one with `notmuch search --output=threads`
   sorted by message count) renders without a noticeable stall, and
   `pgrep -c QtWebEngineProcess` does not grow with the message count.
9. An HTML newsletter renders with layout, and shows "Remote content blocked".
10. Clicking "Load remote content" re-renders with images.
11. Selecting a different thread clears the remote grant (banner returns).
12. A message with an inline image displays it without any remote load.
13. A thread containing two messages that use the same Content-ID shows each
    message its own image, not the same one twice. (Two newsletters from the
    same sender is the usual way to hit this.)
14. `h` toggles the whole thread to plain text and back.
15. Clicking a link in a message opens the system browser, and the pane does
    not navigate.
16. `a` archives the selected thread; `notmuch search` confirms `inbox` is gone.
17. Selecting several threads and pressing `a` archives all of them; confirm the
    count with `notmuch count`.
18. `u` after a bulk archive restores every thread it touched.
19. Sync runs, the log fills, and the query refreshes on completion.
20. Sync while `notmuch new` runs from cron reports a lock error rather than
    corrupting anything.

- [ ] **Step 4: Commit the results**

```bash
git add docs/manual-verification.md
git commit -S -m "docs: record manual verification results"
```

---

## Task 14: README and license

**Files:**
- Create: `README.md`
- Create: `LICENSE`

- [ ] **Step 1: Fetch the GPLv2 text**

```bash
curl -o LICENSE https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
```

Confirm the user wants GPLv2-only (the default) before committing.

- [ ] **Step 2: Add per-file license headers**

Prepend to every file in `src/`:

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
 */
```

- [ ] **Step 3: Write README.md**

Cover: what it is, what it deliberately does not do (no POP/IMAP/SMTP, no
send in v1), requirements with the verified versions, build instructions, the
full example config, the default keybindings table, and the security posture of
the message view.

End the file with the standard Development Approach section:

```markdown
## Development Approach

This project is developed using AI-assisted tools. Code is generated with the help of AI based on human-provided specifications, design decisions, and iterative feedback.

All contributions are reviewed, tested, and curated by the maintainer before being included in the codebase. AI is used as a productivity and exploration tool, while human oversight remains central to all decisions.

The goal is to combine the flexibility of AI-assisted development with standard open-source practices such as transparency, review, and accountability.
```

- [ ] **Step 4: Commit**

```bash
git add README.md LICENSE src/
git commit -S -m "docs: add README and GPLv2 license"
```

---

## Self-review notes

Checked against the spec on 2026-08-02:

- Spec §5 file table maps to Tasks 2-12; every file listed there has a task.
- Spec §8 web view security maps to Task 5 (policy, tested) and Task 11
  (profile settings, navigation interception).
- Spec §9 mutations maps to Task 8 (`applyTags`) and Task 12 (undo stack,
  optimistic update).
- Spec §13 testing maps to Tasks 2, 3, 4, 5, 6, 9, 10. The spec named three
  test targets; this plan has seven, because config, htmlbuilder, model, and
  sync each earned one.
- Spec §14 known gaps are preserved: `NotmuchWorker` has no unit test, and
  Task 13 is the compensating manual check.

No deviations from the spec remain. Two narrowings were present in the first
draft and were folded back in at the user's direction on 2026-08-02:

1. **Full thread rendering** (spec §7). The whole thread renders, oldest first,
   matched messages expanded and unmatched collapsed to stubs. This was called
   out as fundamental for newsletter threads.

   Folding it in surfaced two design consequences worth recording:

   - The thread renders as **one document in one web view**, not one view per
     message. A newsletter thread can hold dozens of messages, and a
     `QWebEngineView` each would spawn a Chromium render process each.
   - Because messages share a document, their `cid:` references **collide**:
     two newsletters both using `cid:logo@example.org` would resolve to
     whichever part won. Every reference is therefore rewritten to
     `cid:<prefix>!<id>` with a per-message prefix. This is covered by
     `threadNamespacesCidUrls` in Task 6.

   Determining which messages matched required a real change to the worker:
   `loadThread` now takes the user's query and intersects it with the thread,
   and `MessageRef` carries a `matched` flag.

2. **Batch tagging** (spec §9). Archive, flag, delete, and spam apply to every
   selected thread. The UI holds thread ids, not message ids, for rows it never
   opened, so `NotmuchWorker::applyTagsToThreads` resolves them in a single
   combined query rather than one query per thread.

   Undo follows: `ThreadTagCommand` stores thread ids and re-resolves on undo,
   so it stays correct even after the selection moves.
