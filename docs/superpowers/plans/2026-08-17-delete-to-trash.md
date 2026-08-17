# Delete Moves Mail To Trash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Delete move a message into its account's trash folder instead of only tagging it, with a Trash filter, a Restore action, and a repeatable cleanup for mail stranded by the old behaviour.

**Architecture:** A new per-account `trash` config key (mandatory), a `moveMessages(ids, destFolder)` mutation on `NotmuchWorker` shaped for reuse by v2's Send, a fifth built-in query generator composed exactly as `sent` is, and UI actions that route through the existing undo stack.

**Tech Stack:** C++17, Qt 6.11, libnotmuch, QtTest. Build with CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-08-17-delete-to-trash-design.md`. Read it before starting; it records the measurements behind these decisions.

---

## Before you start

Run the suite once so you know it is green before you touch anything:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 24 tests, all passing.

**Never run a test binary without `QT_QPA_PLATFORM=offscreen`, and never launch
`./build/src/qtmaildir`.** The project's `CLAUDE.md` explains why: a direct run
throws real windows onto the user's desktop, and `test_mainwindow` alone flashes
over a hundred.

## File structure

| File | Responsibility | Change |
|---|---|---|
| `src/config.h` / `src/config.cpp` | The `trash` account key, the `trash` generator, its queries | Modify |
| `src/notmuchworker.h` / `src/notmuchworker.cpp` | `moveMessages()`, the first non-tag mutation | Modify |
| `src/mainwindow.h` / `src/mainwindow.cpp` | Delete rewired, Restore, cleanup menu entry, undo commands | Modify |
| `tests/test_config.cpp` | Key parsing, generator composition, the missing-key error | Modify |
| `tests/test_notmuchworker.cpp` | The move, its ordering, its failure modes | Modify |
| `tests/test_mainwindow.cpp` | Delete's new behaviour, Restore's visibility, undo | Modify |
| `CHANGELOG.md` | The `### Upgrading` note for the mandatory key | Modify |

No new files. Every change lands in a file that already owns that responsibility.

---

## Task 1: The `trash` account key

**Files:**
- Modify: `src/config.h` (the `Account` struct, near the `sent` member around line 58)
- Modify: `src/config.cpp` (`Account::trashQuery()` beside `sentQuery()` at line 130, and the account parser)
- Test: `tests/test_config.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_config.cpp`, and declare both slots in the `private slots:` block:

```cpp
void TestConfig::anAccountCarriesItsTrashFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.work]\n"
            << "maildir=work\n"
            << "trash=Trash\n";
    }

    Config config;
    config.load(path);

    const Account account = config.account(QStringLiteral("work"));
    QCOMPARE(account.trash, QStringLiteral("Trash"));
    // Quoted and globbed exactly as sentQuery() does it, so a folder with a
    // space or a bracket cannot break the query.
    QCOMPARE(account.trashQuery(), QStringLiteral("path:\"work/Trash/**\""));
}

void TestConfig::aBracketedTrashFolderIsQuoted()
{
    // The real setup nests a localised trash folder under a bracketed parent.
    // The brackets are not notmuch syntax, but the quoting has to survive them.
    Account account;
    account.maildir = QStringLiteral("provider-a");
    account.trash = QStringLiteral("[Provider]/Cestino");

    QCOMPARE(account.trashQuery(),
             QStringLiteral("path:\"provider-a/[Provider]/Cestino/**\""));
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL to compile, `'trash' is not a member of 'Account'`.

- [ ] **Step 3: Write minimal implementation**

In `src/config.h`, add to `Account` immediately after the `sent` member:

```cpp
    /// The account's trash folder, relative to maildir.
    ///
    /// MANDATORY, unlike `sent` and `drafts`. Delete moves a file into this
    /// folder, so an account without one cannot delete at all, and the user
    /// chose a config error over a per-account disabled state: "it is
    /// mandatory for the program to function properly". Config::load()
    /// reports a missing key through the warnings path.
    QString trash;
```

And beside `sentQuery()`:

```cpp
    /// Matches this account's trash, or empty when `trash` is unset.
    ///
    /// Empty is a config error rather than a legitimate state, unlike
    /// sentQuery(). The query helper still returns empty so callers compose
    /// uniformly; it is Config::load() that reports the problem.
    QString trashQuery() const;
```

In `src/config.cpp`, beside `Account::sentQuery()` at line 130:

```cpp
QString Account::trashQuery() const
{
    return folderQuery(maildir, trash);
}
```

Then find where the account parser reads `sent` (search for `QStringLiteral("sent")` inside the account-loading loop) and add the sibling line, following whatever form the neighbouring keys use:

```cpp
        account.trash = settings.value(QStringLiteral("trash")).toString();
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_config
```

Expected: PASS, all tests.

- [ ] **Step 5: Commit**

```bash
git add src/config.h src/config.cpp tests/test_config.cpp
git commit -m "feat(config): read a per-account trash folder"
```

---

## Task 2: A missing `trash` key is a config error

**Files:**
- Modify: `src/config.cpp` (the account-loading loop, beside the existing warnings)
- Test: `tests/test_config.cpp`

First read how warnings are currently raised: search `src/config.cpp` for
`m_warnings` and copy the surrounding form exactly. Do not invent a new
mechanism.

- [ ] **Step 1: Write the failing test**

```cpp
void TestConfig::anAccountWithoutATrashFolderWarns()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.work]\n"
            << "maildir=work\n";
    }

    Config config;
    config.load(path);

    // The account still loads. A missing trash folder disables Delete, it does
    // not invalidate the account: the user can still read mail.
    QVERIFY(config.account(QStringLiteral("work")).isValid());

    const QStringList warnings = config.warnings();
    QVERIFY(!warnings.isEmpty());
    // Names the account and the key, so the warning is actionable. A warning
    // the user cannot act on teaches them to ignore warnings, which item 83
    // recorded the hard way.
    const QString joined = warnings.join(QLatin1Char('\n'));
    QVERIFY(joined.contains(QStringLiteral("work")));
    QVERIFY(joined.contains(QStringLiteral("trash")));
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_config -functions | grep -i trash
QT_QPA_PLATFORM=offscreen ./build/tests/test_config anAccountWithoutATrashFolderWarns
```

Expected: FAIL, `warnings` is empty.

- [ ] **Step 3: Write minimal implementation**

In the account-loading loop in `src/config.cpp`, after the account is parsed and
before it is appended:

```cpp
        if (account.trash.isEmpty()) {
            // Mandatory, unlike `sent`. Delete moves a file into this folder,
            // so without it the action cannot work at all. Reported rather
            // than defaulted to "Trash": that folder does not exist under
            // every provider, and a default pointing at a folder mbsync does
            // not sync would move mail somewhere the server never sees.
            m_warnings.append(
                tr("Account \"%1\" has no trash folder. Add a `trash` key to "
                   "qtmaildir.conf; Delete cannot work without it.")
                    .arg(account.key));
        }
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_config
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/config.cpp tests/test_config.cpp
git commit -m "feat(config): warn when an account configures no trash folder"
```

---

## Task 3: The `trash` query generator

**Files:**
- Modify: `src/config.h` (declare `allTrashQuery()` beside `allSentQuery()`)
- Modify: `src/config.cpp` (`kQueryGenerators` line 60, `builtinFilter()` line 757, `resolvedQuery()` line 790, `allTrashQuery()` beside line 140)
- Test: `tests/test_config.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestConfig::theTrashFilterComposesPerAccount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.work]\n"
            << "maildir=work\n"
            << "trash=Trash\n"
            << "[account.personal]\n"
            << "maildir=personal\n"
            << "trash=[Provider]/Cestino\n";
    }

    Config config;
    config.load(path);

    const SavedQuery trash = Config::builtinFilter(QStringLiteral("trash"));
    QVERIFY(trash.isGenerated());

    // All accounts: the union, never a bare path that would match one account.
    const QString all = config.resolvedQuery(trash, QString());
    QVERIFY(all.contains(QStringLiteral("path:\"work/Trash/**\"")));
    QVERIFY(all.contains(QStringLiteral("path:\"personal/[Provider]/Cestino/**\"")));

    // One account: that account's OWN query. Asserting on the STRING, not on a
    // row count: the all-accounts query wrapped in this account's path returns
    // exactly the right rows, because path: is hierarchical, so a count passes
    // against the wrong thing. Config::resolvedQuery documents this trap.
    const QString scoped = config.resolvedQuery(trash, QStringLiteral("work"));
    QCOMPARE(scoped, QStringLiteral("path:\"work/Trash/**\""));
    QVERIFY(!scoped.contains(QStringLiteral("personal")));
}

void TestConfig::theTrashFilterMatchesNothingWithoutAFolder()
{
    // An empty query means "match everything" to notmuch, so a filter with
    // nothing to match must say so explicitly. A button labelled Trash that
    // showed the whole Maildir is the failure this prevents.
    Config config;
    const SavedQuery trash = Config::builtinFilter(QStringLiteral("trash"));
    QCOMPARE(config.resolvedQuery(trash, QString()), Config::matchNothingQuery());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_config theTrashFilterComposesPerAccount
```

Expected: FAIL, `trash.isGenerated()` is false because the generator is unknown.

- [ ] **Step 3: Write minimal implementation**

In `src/config.cpp`, add to `kQueryGenerators` at line 60, last so it sits
rightmost on the query row:

```cpp
const QStringList kQueryGenerators = { QStringLiteral("unread"),
                                       QStringLiteral("inbox"),
                                       QStringLiteral("flagged"),
                                       QStringLiteral("sent"),
                                       QStringLiteral("trash") };
```

In `Config::builtinFilter()`, add a branch beside the `sent` one:

```cpp
    } else if (generator == QStringLiteral("trash")) {
        filter.name = tr("Trash");
        // NOT flat, unlike Sent. A deleted message still belongs to its
        // conversation, and folding it back is what Sent had to avoid rather
        // than something every folder filter wants.
    }
```

In `src/config.h`, beside `allSentQuery()`:

```cpp
    /// The union of every account's trashQuery(), empty when none has one.
    QString allTrashQuery() const;
```

In `src/config.cpp`, beside `allSentQuery()` at line 140:

```cpp
QString Config::allTrashQuery() const
{
    return joinAccountQueries(m_accounts, &Account::trashQuery);
}
```

In `Config::resolvedQuery()`, extend both branches. In the all-accounts branch,
beside the `sent` case:

```cpp
        if (query.generated == QStringLiteral("trash")) {
            const QString all = allTrashQuery();
            return all.isEmpty() ? matchNothingQuery() : all;
        }
```

And in the per-account branch, beside the `sent` case:

```cpp
    if (query.generated == QStringLiteral("trash")) {
        // The account's OWN query, for the reason spelled out above the sent
        // case: wrapping the all-accounts query in this account's path works
        // by accident of path: being hierarchical.
        const QString trash = scope.trashQuery();
        return trash.isEmpty() ? matchNothingQuery() : trash;
    }
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_config
```

Expected: PASS.

- [ ] **Step 5: Verify the mutation fails**

Temporarily change the per-account branch to `return allTrashQuery();` and
rebuild. `theTrashFilterComposesPerAccount` must FAIL on the `QCOMPARE`. Revert
the mutation. This proves the test asserts on the string rather than on rows.

- [ ] **Step 6: Commit**

```bash
git add src/config.h src/config.cpp tests/test_config.cpp
git commit -m "feat(config): add the trash query generator"
```

---

## Task 4: `moveMessages()` on the worker

**Files:**
- Modify: `src/notmuchworker.h` (declare the slot and its signal)
- Modify: `src/notmuchworker.cpp` (implement beside `applyTags()` at line 550)
- Test: `tests/test_notmuchworker.cpp`

This is the first mutation in the project that is not a notmuch tag. Read
`applyTags()` at `src/notmuchworker.cpp:550` first: the close-first ordering it
uses is required, not stylistic, because notmuch permits one open handle per
process.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_notmuchworker.cpp`, declaring each slot in `private slots:`:

```cpp
void TestNotmuchWorker::moveMessagesRelocatesTheFile()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(fixture.addMessage(QStringLiteral("Inbox"), QStringLiteral("m1@example.org"),
                               QStringLiteral("Movable"),
                               QStringLiteral("Alice <alice@example.org>"),
                               QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), false));

    NotmuchWorker worker;
    worker.setConfigPath(fixture.configPath());

    QSignalSpy spy(&worker, &NotmuchWorker::messagesMoved);
    worker.moveMessages({ QStringLiteral("m1@example.org") },
                        QStringLiteral("Trash"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toStringList(),
             QStringList{ QStringLiteral("m1@example.org") });

    // The file is where it was sent, and no longer where it was.
    const QDir trash(fixture.maildirPath() + QStringLiteral("/Trash/cur"));
    QCOMPARE(trash.entryList(QDir::Files).count(), 1);
    const QDir inbox(fixture.maildirPath() + QStringLiteral("/Inbox/cur"));
    QCOMPARE(inbox.entryList(QDir::Files).count(), 0);
}

void TestNotmuchWorker::moveMessagesReindexesAtTheNewPath()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(fixture.addMessage(QStringLiteral("Inbox"), QStringLiteral("m2@example.org"),
                               QStringLiteral("Reindexed"),
                               QStringLiteral("Alice <alice@example.org>"),
                               QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), false));

    NotmuchWorker worker;
    worker.setConfigPath(fixture.configPath());
    worker.moveMessages({ QStringLiteral("m2@example.org") },
                        QStringLiteral("Trash"));

    // notmuch knows the message at its new path and not at its old one. This
    // is the half a filesystem check cannot see: a moved file with a stale
    // index entry looks correct on disk and is invisible to every query.
    NotmuchWorker reader;
    reader.setConfigPath(fixture.configPath());
    QSignalSpy spy(&reader, &NotmuchWorker::threadsReady);
    reader.runQuery(QStringLiteral("path:\"Trash/**\""), 1,
                    NotmuchWorker::NewestFirst, false);
    QVERIFY(spy.count() > 0);
    const auto threads = spy.at(0).at(0).value<QVector<ThreadSummary>>();
    QCOMPARE(threads.count(), 1);

    QSignalSpy old(&reader, &NotmuchWorker::threadsReady);
    reader.runQuery(QStringLiteral("path:\"Inbox/**\""), 2,
                    NotmuchWorker::NewestFirst, false);
    QVERIFY(old.count() > 0);
    QCOMPARE(old.at(0).at(0).value<QVector<ThreadSummary>>().count(), 0);
}

void TestNotmuchWorker::moveMessagesKeepsTheMessagesTags()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(fixture.addMessage(QStringLiteral("Inbox"), QStringLiteral("m3@example.org"),
                               QStringLiteral("Tagged"),
                               QStringLiteral("Alice <alice@example.org>"),
                               QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), true));

    NotmuchWorker worker;
    worker.setConfigPath(fixture.configPath());
    worker.applyTags(TagChange{ { QStringLiteral("m3@example.org") },
                                { QStringLiteral("keepme") }, {}, QString() });
    worker.moveMessages({ QStringLiteral("m3@example.org") },
                        QStringLiteral("Trash"));

    // Indexing the new path BEFORE removing the old one is what preserves
    // these. The other order removes the last filename for the message id,
    // which destroys the database entry and every tag on it.
    NotmuchWorker reader;
    reader.setConfigPath(fixture.configPath());
    QSignalSpy spy(&reader, &NotmuchWorker::messageLoaded);
    reader.loadMessage(QStringLiteral("m3@example.org"), 1);
    QVERIFY(spy.count() > 0);
    const auto ref = spy.at(0).at(0).value<MessageRef>();
    QVERIFY(ref.tags.contains(QStringLiteral("keepme")));
}

void TestNotmuchWorker::moveMessagesReportsOnlyWhatMoved()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.isValid());
    QVERIFY(fixture.addMessage(QStringLiteral("Inbox"), QStringLiteral("m4@example.org"),
                               QStringLiteral("Real"),
                               QStringLiteral("Alice <alice@example.org>"),
                               QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000"),
                               QStringLiteral("body"), false));

    NotmuchWorker worker;
    worker.setConfigPath(fixture.configPath());
    QSignalSpy spy(&worker, &NotmuchWorker::messagesMoved);

    // A stale id alongside a live one must not abort the batch, exactly as
    // applyTags() documents for the same case.
    worker.moveMessages({ QStringLiteral("m4@example.org"),
                          QStringLiteral("gone@example.org") },
                        QStringLiteral("Trash"));

    QCOMPARE(spy.count(), 1);
    // Reports what MOVED, not what was asked for. A caller that assumed the
    // request succeeded would show a delete that never happened.
    QCOMPARE(spy.at(0).at(0).toStringList(),
             QStringList{ QStringLiteral("m4@example.org") });
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL to compile, `'moveMessages' is not a member of 'NotmuchWorker'`.

- [ ] **Step 3: Write minimal implementation**

In `src/notmuchworker.h`, beside `applyTags()`:

```cpp
    /// Moves messages into `destFolder`, relative to the database path.
    ///
    /// A folder NAME rather than a "move to trash" call, because v2's Send
    /// needs exactly this operation for Drafts and Sent. Nothing
    /// trash-specific belongs here.
    ///
    /// The first mutation in this class that is not a notmuch tag: a rename on
    /// disk plus a reindex. Ordering is rename, index the new path, drop the
    /// old one. Indexing first is required, not stylistic: removing the last
    /// filename for a message id deletes the database entry and every tag on
    /// it, so removing before indexing loses the message's tags.
    void moveMessages(const QStringList &messageIds, const QString &destFolder);
```

And in the signals block:

```cpp
    /// Carries the ids that ACTUALLY moved, which may be fewer than requested.
    /// A stale id, a missing folder or a failed rename drops out here rather
    /// than aborting the batch.
    void messagesMoved(const QStringList &messageIds, const QString &destFolder);
```

In `src/notmuchworker.cpp`, beside `applyTags()`. Add `#include <QDir>` and
`#include <QFileInfo>` at the top if they are not already there:

```cpp
void NotmuchWorker::moveMessages(const QStringList &messageIds,
                                 const QString &destFolder)
{
    if (messageIds.isEmpty() || destFolder.isEmpty())
        return;

    // Close first: notmuch permits one open handle per process, so the
    // read-only handle has to go before a read-write one opens. Same
    // constraint applyTags() documents.
    close();

    const QByteArray configPath = configPathArg();
    notmuch_database_t *db = nullptr;
    char *error = nullptr;
    const notmuch_status_t status = notmuch_database_open_with_config(
        nullptr,
        NOTMUCH_DATABASE_MODE_READ_WRITE,
        configPath.isEmpty() ? nullptr : configPath.constData(),
        nullptr,
        &db,
        &error);

    if (status != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(
            QStringLiteral("Cannot open database for writing: %1")
                .arg(QString::fromUtf8(error ? error
                                             : notmuch_status_to_string(status))));
        free(error);
        return;
    }

    const QString root = QString::fromUtf8(notmuch_database_get_path(db));
    QStringList moved;

    for (const QString &id : messageIds) {
        notmuch_message_t *raw = nullptr;
        if (notmuch_database_find_message(db, id.toUtf8().constData(), &raw)
                != NOTMUCH_STATUS_SUCCESS || !raw) {
            continue;
        }
        NmMessage message(raw);

        const QString from =
            QString::fromUtf8(notmuch_message_get_filename(message.get()));
        if (from.isEmpty())
            continue;

        // cur/, never new/. A message that has been seen by any client lives
        // in cur, and a file dropped into new/ would be re-announced as fresh
        // mail by every reader of this Maildir.
        const QString destDir =
            root + QLatin1Char('/') + destFolder + QStringLiteral("/cur");
        if (!QDir().mkpath(destDir)) {
            emit errorOccurred(
                QStringLiteral("Cannot create folder: %1").arg(destDir));
            continue;
        }

        const QString to = destDir + QLatin1Char('/') + QFileInfo(from).fileName();
        if (from == to)
            continue;

        if (!QFile::rename(from, to)) {
            emit errorOccurred(
                QStringLiteral("Cannot move message to %1").arg(destFolder));
            continue;
        }

        // Index the NEW path before dropping the old one. The reverse order
        // removes the last filename for this message id, and notmuch then
        // deletes the database entry outright, taking every tag with it.
        notmuch_message_t *indexed = nullptr;
        const notmuch_status_t added = notmuch_database_index_file(
            db, to.toUtf8().constData(), nullptr, &indexed);
        if (indexed)
            notmuch_message_destroy(indexed);

        // DUPLICATE_MESSAGE_ID is success here: it means the id was already
        // known, which is exactly the case for a file we just moved.
        if (added != NOTMUCH_STATUS_SUCCESS
            && added != NOTMUCH_STATUS_DUPLICATE_MESSAGE_ID) {
            // Put it back rather than leaving a file notmuch cannot see.
            QFile::rename(to, from);
            emit errorOccurred(
                QStringLiteral("Cannot index moved message in %1").arg(destFolder));
            continue;
        }

        notmuch_database_remove_message(db, from.toUtf8().constData());
        moved.append(id);
    }

    notmuch_database_close(db);
    notmuch_database_destroy(db);

    emit messagesMoved(moved, destFolder);
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_notmuchworker
```

Expected: PASS, all tests.

- [ ] **Step 5: Verify the ordering mutation fails**

Move the `notmuch_database_remove_message` call to immediately BEFORE the
`notmuch_database_index_file` call and rebuild.
`moveMessagesKeepsTheMessagesTags` must FAIL, because the tags are gone. Revert
the mutation. This is the single most important check in the task: the wrong
order silently destroys user tags and every other test still passes.

- [ ] **Step 6: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp tests/test_notmuchworker.cpp
git commit -m "feat(worker): move messages between maildir folders

The first mutation here that is not a notmuch tag. Indexes the new path
before dropping the old one, since removing the last filename for a
message id deletes the database entry and every tag on it."
```

---

## Task 5: Route Delete through the move

**Files:**
- Modify: `src/mainwindow.h` (a `MoveCommand` beside the existing tag commands around line 1130)
- Modify: `src/mainwindow.cpp` (the `delete` action at line 825)
- Test: `tests/test_mainwindow.cpp`

Read the existing `delete` action first. It is a toggle over the whole
selection, resolved through `everySelectedRowHasTag()`, and `CLAUDE.md` records
two separate bugs that lived in those three lines. Preserve the toggle: Delete
twice still means "put it back".

- [ ] **Step 1: Write the failing test**

```cpp
void TestMainWindow::deleteMovesTheMessageToTrash()
{
    WorkerBackedWindow window;
    QVERIFY(window.isValid());
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() > 0, 5000);
    window.selectFirstThread();

    const QString id = window.currentMessageId();
    QVERIFY(!id.isEmpty());

    window.triggerAction(QStringLiteral("delete"));

    // The file lands in the account's trash folder, and notmuch knows it
    // there. Asserting on the trash QUERY rather than on the tag: the tag is
    // the record of who deleted it, the folder is where it is.
    QTRY_VERIFY_WITH_TIMEOUT(
        window.countMatching(QStringLiteral("path:\"work/Trash/**\"")) == 1, 5000);
    QCOMPARE(window.countMatching(QStringLiteral("path:\"work/Inbox/**\"")), 0);
}

void TestMainWindow::deleteRecordsWhereTheMessageCameFrom()
{
    WorkerBackedWindow window;
    QVERIFY(window.isValid());
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() > 0, 5000);
    window.selectFirstThread();
    const QString id = window.currentMessageId();

    window.triggerAction(QStringLiteral("delete"));

    // Restore needs a destination, and a Maildir filename does not record one.
    // Without this tag a restore three days later can only guess.
    QTRY_VERIFY_WITH_TIMEOUT(
        window.tagsOf(id).contains(QStringLiteral("deleted-from:Inbox")), 5000);
    QVERIFY(window.tagsOf(id).contains(QStringLiteral("deleted")));
}

void TestMainWindow::undoMovesTheMessageBack()
{
    WorkerBackedWindow window;
    QVERIFY(window.isValid());
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() > 0, 5000);
    window.selectFirstThread();
    const QString id = window.currentMessageId();

    window.triggerAction(QStringLiteral("delete"));
    QTRY_VERIFY_WITH_TIMEOUT(
        window.countMatching(QStringLiteral("path:\"work/Trash/**\"")) == 1, 5000);

    window.triggerAction(QStringLiteral("undo"));

    // Back to the exact folder it came from, not merely out of the trash, and
    // both tags stripped so the row does not still read as deleted.
    QTRY_VERIFY_WITH_TIMEOUT(
        window.countMatching(QStringLiteral("path:\"work/Inbox/**\"")) == 1, 5000);
    QCOMPARE(window.countMatching(QStringLiteral("path:\"work/Trash/**\"")), 0);
    QVERIFY(!window.tagsOf(id).contains(QStringLiteral("deleted")));
    QVERIFY(!window.tagsOf(id).contains(QStringLiteral("deleted-from:Inbox")));
}
```

`WorkerBackedWindow` exists but may lack `countMatching()`, `tagsOf()` and
`currentMessageId()`. Read its definition in `tests/test_mainwindow.cpp` and add
whichever helpers are missing, following the form of the ones already there.
`CLAUDE.md` records that the worker is unreachable by `findChild`, so wait on
observable state with `QTRY_VERIFY_WITH_TIMEOUT` and never on worker signals or
a fixed `qWait(n)`.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow deleteMovesTheMessageToTrash
```

Expected: FAIL, the message is still in Inbox because Delete only tags.

- [ ] **Step 3: Write minimal implementation**

Add a `MoveCommand` in `src/mainwindow.h`, beside `MessageTagCommand` around
line 1176, following its shape exactly:

```cpp
/// Undo entry for a folder move. Separate from MessageTagCommand because the
/// inverse of a move is another move, and the destination has to be carried
/// rather than derived: a Maildir filename does not record where a message
/// came from.
class MoveCommand : public QUndoCommand
{
public:
    MoveCommand(MainWindow *window, const QStringList &messageIds,
                const QString &from, const QString &to,
                const QStringList &addedTags, const QString &text)
        : QUndoCommand(text)
        , m_window(window)
        , m_messageIds(messageIds)
        , m_from(from)
        , m_to(to)
        , m_addedTags(addedTags)
    {
    }

    void redo() override
    {
        m_window->sendMove(m_messageIds, m_to, m_addedTags, {});
    }

    void undo() override
    {
        // Strips the tags the move added, so an undone delete does not leave a
        // row still reading as deleted.
        m_window->sendMove(m_messageIds, m_from, {}, m_addedTags);
    }

private:
    MainWindow *m_window;
    QStringList m_messageIds;
    QString m_from;
    QString m_to;
    QStringList m_addedTags;
};
```

Declare `sendMove()` in `MainWindow`, near `sendMessageTagChange()`:

```cpp
    /// Moves messages to `destFolder`, applying `add` and `remove` in the same
    /// gesture. Invokable for the same reason sendThreadTagChange() is.
    Q_INVOKABLE void sendMove(const QStringList &messageIds,
                              const QString &destFolder,
                              const QStringList &add,
                              const QStringList &remove);
```

Implement it in `src/mainwindow.cpp` beside `sendMessageTagChange()`, routing to
the worker with the same queued-invocation form the neighbouring senders use,
and applying the tags after the move reports success.

Then rewire the `delete` action at line 825. Keep the toggle and the
whole-selection direction; change only what each direction DOES:

```cpp
    addAction(QStringLiteral("delete"), tr("&Delete"),
              tr("Move to trash, or restore when already there"), [this]() {
        // Still a toggle over the whole selection, for the reasons the old
        // implementation recorded: one keystroke must not leave the selection
        // in two states. What changed is that each direction now MOVES the
        // file, and the tag is the record rather than the action.
        const bool everyRowDeleted =
            everySelectedRowHasTag(QStringLiteral("deleted"));
        if (everyRowDeleted)
            restoreSelected();
        else
            trashSelected();
    });
```

Write `trashSelected()` and `restoreSelected()` as private helpers. `trashSelected()`
resolves each selected row to its message id and its account (from the message's
path prefix), reads the account's `trash` key, computes the origin folder from
the current path, and pushes one `MoveCommand` carrying
`deleted` and `deleted-from:<origin>`. An account with no `trash` key contributes
nothing and reports through `statusMessage`, since Task 2 already warned at load.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow
```

Expected: PASS, all tests.

- [ ] **Step 5: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp tests/test_mainwindow.cpp
git commit -m "feat: Delete moves mail to the account's trash folder"
```

---

## Task 6: Restore from trash

**Files:**
- Modify: `src/mainwindow.cpp` (a `restore` action beside `delete`, and the icon table)
- Modify: `src/keymap.cpp` (`knownActions()` and `defaultBindings()`)
- Test: `tests/test_mainwindow.cpp`

**Adding an action is four places**, and `CLAUDE.md` records that three of them
are enforced by tests that fail confusingly: `KeyMap::knownActions()` (a
`Q_ASSERT` fires otherwise, in whichever suite builds a `MainWindow` first),
`defaultBindings()` (every action must be keyboard-reachable), and the icon
table (every action must carry one).

- [ ] **Step 1: Write the failing test**

```cpp
void TestMainWindow::restoreIsOnlyEnabledInTheTrashView()
{
    WorkerBackedWindow window;
    QVERIFY(window.isValid());
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() > 0, 5000);
    window.selectFirstThread();

    // Not in an ordinary view: restoring a message that is not in the trash
    // has no meaning, and an enabled action that does nothing is worse than an
    // absent one.
    QVERIFY(!window.action(QStringLiteral("restore"))->isEnabled());

    window.runQuery(QStringLiteral("path:\"work/Trash/**\""));
    QTRY_VERIFY_WITH_TIMEOUT(window.isShowingTrash(), 5000);
    QVERIFY(window.action(QStringLiteral("restore"))->isEnabled());
}

void TestMainWindow::restoreReturnsAMessageToItsOriginFolder()
{
    WorkerBackedWindow window;
    QVERIFY(window.isValid());
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() > 0, 5000);
    window.selectFirstThread();
    const QString id = window.currentMessageId();
    window.triggerAction(QStringLiteral("delete"));
    QTRY_VERIFY_WITH_TIMEOUT(
        window.countMatching(QStringLiteral("path:\"work/Trash/**\"")) == 1, 5000);

    window.runQuery(QStringLiteral("path:\"work/Trash/**\""));
    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() == 1, 5000);
    window.selectFirstThread();
    window.triggerAction(QStringLiteral("restore"));

    QTRY_VERIFY_WITH_TIMEOUT(
        window.countMatching(QStringLiteral("path:\"work/Inbox/**\"")) == 1, 5000);
    QVERIFY(!window.tagsOf(id).contains(QStringLiteral("deleted")));
}

void TestMainWindow::restoreFallsBackToInboxWithoutAnOriginTag()
{
    WorkerBackedWindow window;
    QVERIFY(window.isValid());
    window.show();

    // A message trashed by another client: in the trash folder, carrying no
    // deleted-from: tag, because nothing here put it there. There is one such
    // message in the real Maildir, so this is not a hypothetical.
    QVERIFY(window.addMessageInFolder(QStringLiteral("work/Trash"),
                                      QStringLiteral("foreign@example.org")));
    window.runQuery(QStringLiteral("path:\"work/Trash/**\""));
    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() > 0, 5000);
    window.selectFirstThread();

    window.triggerAction(QStringLiteral("restore"));

    QTRY_VERIFY_WITH_TIMEOUT(
        window.countMatching(QStringLiteral("path:\"work/Inbox/**\"")) == 1, 5000);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow restoreIsOnlyEnabledInTheTrashView
```

Expected: FAIL, there is no `restore` action.

- [ ] **Step 3: Write minimal implementation**

Add `"restore"` to `KeyMap::knownActions()` and give it a binding in
`defaultBindings()`. Check what is free first:

```bash
grep -n 'defaultBindings' -A40 src/keymap.cpp
```

Add an entry to the icon table in `src/mainwindow.cpp` (search for
`QIcon::fromTheme` near the other actions), using a theme icon as the toolbar
and menus must keep doing.

Then the action itself, beside `delete`:

```cpp
    addAction(QStringLiteral("restore"), tr("&Restore from trash"),
              tr("Move the selected messages out of the trash"), [this]() {
        restoreSelected();
    });
```

Enable it from wherever the current query is applied, so it follows the view:

```cpp
    // Only meaningful on mail that is actually in a trash folder. Enabled from
    // the QUERY rather than from the selection's tags: a message trashed by
    // another client carries no tag of ours and must still be restorable.
    m_actions[QStringLiteral("restore")]->setEnabled(isShowingTrash());
```

`isShowingTrash()` compares the current resolved query against the `trash`
generator's, for the current account selection.

`restoreSelected()` reads each row's `deleted-from:` tag for its destination and
falls back to `Inbox`, reporting which through `statusMessage`.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow
ctest --test-dir build --output-on-failure
```

Expected: PASS, 24 of 24. The keymap and icon-table tests fail loudly if a
place was missed.

- [ ] **Step 5: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp src/keymap.cpp tests/test_mainwindow.cpp
git commit -m "feat: restore mail from the trash view"
```

---

## Task 7: The cleanup menu entry

**Files:**
- Modify: `src/mainwindow.cpp` (a `cleanup_stranded` action, added to a menu and NOT to the query row)
- Modify: `src/keymap.cpp` (`knownActions()` and `defaultBindings()`)
- Test: `tests/test_mainwindow.cpp`

The user's constraint, verbatim: "the cleanup should be a menu entry only, not
to be confused with the filter Trash". Do not add a sixth button.

- [ ] **Step 1: Write the failing test**

```cpp
void TestMainWindow::theCleanupQueryFindsStrandedMail()
{
    WorkerBackedWindow window;
    QVERIFY(window.isValid());
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() > 0, 5000);
    window.selectFirstThread();
    const QString id = window.currentMessageId();

    // Tag without moving: exactly the state the old Delete left mail in, and
    // the state 848 real messages are in today.
    window.sendMessageTagChangeForTesting({ id }, { QStringLiteral("deleted") },
                                          {}, QStringLiteral("Strand"));
    QTRY_VERIFY_WITH_TIMEOUT(
        window.tagsOf(id).contains(QStringLiteral("deleted")), 5000);

    window.triggerAction(QStringLiteral("cleanup_stranded"));

    // The stranded message is in the list; mail already in the trash is not,
    // since it needs no cleanup.
    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() == 1, 5000);
    QVERIFY(window.currentQuery().contains(QStringLiteral("tag:deleted")));
    QVERIFY(window.currentQuery().contains(QStringLiteral("not")));
}

void TestMainWindow::theCleanupQueryExcludesMailAlreadyInTrash()
{
    WorkerBackedWindow window;
    QVERIFY(window.isValid());
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() > 0, 5000);
    window.selectFirstThread();
    window.triggerAction(QStringLiteral("delete"));
    QTRY_VERIFY_WITH_TIMEOUT(
        window.countMatching(QStringLiteral("path:\"work/Trash/**\"")) == 1, 5000);

    window.triggerAction(QStringLiteral("cleanup_stranded"));

    // Properly trashed mail carries the tag AND sits in the folder, so it must
    // not appear here. Without the exclusion this reports every deleted
    // message ever, which makes the action useless the moment it works.
    QTRY_VERIFY_WITH_TIMEOUT(window.threadCount() == 0, 5000);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow theCleanupQueryFindsStrandedMail
```

Expected: FAIL, there is no `cleanup_stranded` action.

- [ ] **Step 3: Write minimal implementation**

Register the action in `knownActions()`, `defaultBindings()` and the icon table
as in Task 6, then:

```cpp
    addAction(QStringLiteral("cleanup_stranded"), tr("Find &stranded deleted mail"),
              tr("Show mail tagged deleted that is not in a trash folder"), [this]() {
        // Repeatable, never a startup migration: the user asked for something
        // they could come back to. It reports what it finds and moves nothing.
        const QString trash = m_config.allTrashQuery();
        const QString query =
            trash.isEmpty()
                ? QStringLiteral("tag:deleted")
                : QStringLiteral("tag:deleted and not (%1)").arg(trash);
        runQuery(query);
        statusMessage(tr("Mail tagged deleted but not in a trash folder. "
                         "Select what should go and press Delete."));
    });
```

Add it to a menu, not to the query row. Find where the other menu entries are
built and follow that form.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp src/keymap.cpp tests/test_mainwindow.cpp
git commit -m "feat: find mail tagged deleted but never moved to trash"
```

---

## Task 8: Translations and changelog

**Files:**
- Modify: `translations/qtmaildir_it_IT.ts`
- Modify: `CHANGELOG.md`

Every user-facing string added above needs a translation, and `lrelease`
silently DROPS an unfinished string and ships it as English inside an otherwise
Italian UI. Item 108 shipped fifteen strings that way.

- [ ] **Step 1: Refresh the translation source**

```bash
lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete -locations none
```

Expected: a clean run reporting zero context warnings. A "tr() cannot be called
without context" warning means a literal needs `QT_TRANSLATE_NOOP("TheClass",
"Text")` rather than `tr()`.

- [ ] **Step 2: Translate every new string**

Open `translations/qtmaildir_it_IT.ts` and fill in each `<translation
type="unfinished">`. The new strings are the Trash filter name, the Restore and
cleanup action texts and tooltips, the trash config warning, and the status
messages.

Do NOT translate notmuch query syntax. `tag:deleted`, `path:` and
`deleted-from:` are wire format.

- [ ] **Step 3: Verify nothing is unfinished**

```bash
lrelease-qt6 translations/qtmaildir_it_IT.ts
```

Expected: "Generated N translation(s) (N finished, 0 unfinished)". A nonzero
unfinished count means a string will ship as English.

- [ ] **Step 4: Run the translations test**

```bash
ctest --test-dir build -R translations --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Write the changelog entry**

Under `## [Unreleased]` in `CHANGELOG.md`, with an `### Upgrading` section,
since a working config now warns until five keys are added:

```markdown
### Added

- Delete now moves mail into the account's trash folder instead of only
  tagging it. A `Trash` filter sits beside Unread, Inbox, Important and Sent,
  and composes with the account selector like the others.
- `Restore from trash`, available while the trash view is showing. A message
  this application deleted returns to the folder it came from; one trashed by
  another client returns to the inbox.
- `Find stranded deleted mail`, in the menu, listing mail tagged `deleted`
  that never moved. Run it whenever you like; it moves nothing on its own.

### Upgrading

Every account now needs a `trash` key in `qtmaildir.conf`, naming its trash
folder relative to `maildir`:

    [account.work]
    maildir = work
    trash = Trash

The folder must be one your `mbsync` configuration actually syncs, or the move
will never reach the server. Accounts without the key still load and still
read mail, but Delete cannot work on them and a warning says so at startup.

Mail deleted by earlier versions carries the `deleted` tag and sits wherever
it was. It is not migrated automatically. Use `Find stranded deleted mail` to
review it and Delete to move what should really go.

Note that Delete's reversibility depends on your provider: a trash folder that
the provider purges on a timer will eventually remove the mail for good.
```

- [ ] **Step 6: Commit**

```bash
git add translations/qtmaildir_it_IT.ts CHANGELOG.md
git commit -m "i18n: translate the trash strings, and document the trash key"
```

---

## Task 9: Full verification

- [ ] **Step 1: Clean build**

```bash
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Expected: no warnings from the changed files.

- [ ] **Step 2: Full suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 24 of 24 passing.

- [ ] **Step 3: Confirm the spec's claims hold**

Re-read `docs/superpowers/specs/2026-08-17-delete-to-trash-design.md` and check
each testing bullet has a test. The spec lists six; all six are covered by
Tasks 1, 3, 4, 5, 6 and 7.

- [ ] **Step 4: Hand the build to the user**

Do NOT run `./build/src/qtmaildir`. Report what to look at:

- Delete on a message, then look in the trash view for it.
- Undo, and confirm it returns to the folder it came from.
- The trash view with the account dropdown on each account and on All accounts.
- `Find stranded deleted mail`, which should list a large number of messages on
  the account that has them, and nothing on the others.
- Delete on a message whose account has no `trash` key, which should report
  rather than silently doing nothing.

The startup warning about missing `trash` keys will appear until the config is
updated; that is Task 8's `### Upgrading` note in action.
