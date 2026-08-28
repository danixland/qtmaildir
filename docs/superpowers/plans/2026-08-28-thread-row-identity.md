# Thread Row Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a thread row stand for the conversation rather than for its first message, so that what a row shows, what an action on it does, and whether it belongs in a view all answer the same question.

**Architecture:** `ThreadSummary::totalCount` splits rows into two kinds. A row with one message behaves exactly as today. A row with replies drops its first-message identity, draws the thread's own tag union, resolves every action to the whole conversation, and renders a dashboard instead of a message. View membership becomes the union with no exceptions. A new worker entry point supplies the dashboard's data as a plain value struct.

**Tech Stack:** C++17, Qt 6.11 (Widgets, Test), libnotmuch, CMake + Ninja, ctest.

**Spec:** `docs/superpowers/specs/2026-08-28-thread-row-identity-design.md`. Read it before Task 1; this plan implements it and does not restate its reasoning.

**Branch:** `thread-row-identity`, off master at `177d37e`.

---

## Before you start

Read `CLAUDE.md` in full. It records traps that have each cost a session, and several apply directly to this work:

- **Never run a test binary without `QT_QPA_PLATFORM=offscreen`**, and never launch `./build/src/qtmaildir` unasked. Each test function builds a `MainWindow`; one direct run throws a hundred windows onto the user's screen. The user has asked for this to stop.
- **A test needs two threads in DISAGREEING states.** Two threads in the same state answer identically whichever way the code resolves them, so a green test proves nothing. Put the interesting case under the SECOND thread.
- **Rendering probes lie.** Do not count pixels. Assert on `CardLayout`, on model data, and on value structs.
- **`ThreadListModel::threadAt(int)` takes a ROW** and is wrong for any index that might be a reply. Use `threadFor(const QModelIndex &)`.
- **A bare `MainWindow` in a test runs its startup query against the LIVE index.** The default startup query is `tag:unread`. A test that waits long enough will see real mail arrive in its model. Fire timers with a zero interval and one event-loop turn rather than a long `QTRY`.

Build and test commands:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/tests/test_threadlistmodel
```

The full suite takes about 170 seconds. `undoMovesTheMessageBack` fails on master already (item 136) and is not yours to fix.

---

## File Structure

**Created:**

- `src/threaddigest.h` — the `ThreadDigest` value struct crossing the worker boundary. Header only; it is data, and it lives beside `types.h` rather than in it because only the dashboard uses it.
- `src/threaddashboard.h` / `.cpp` — the dashboard widget. Owns no data of its own: it is handed a `ThreadDigest` and renders it.
- `tests/test_threaddashboard.cpp` — the dashboard's own tests, over value structs, no rendering.

**Modified:**

- `src/threadlistmodel.h` / `.cpp` — row kinds, the union for membership and tags, `scopeFor`/`messageScopeFor`, deletion of the first-message substitution.
- `src/mainwindow.h` / `.cpp` — action scoping and labels, menu construction, the dashboard's wiring, membership sync.
- `src/messageview.h` / `.cpp` — a third pane state beside the message and the placeholder.
- `src/notmuchworker.h` / `.cpp` — `loadThreadDigest`.
- `src/carddelegate.h` / `.cpp`, `src/cardlayout.h` / `.cpp` — deletion of the sibling tier.
- `tests/test_threadlistmodel.cpp`, `tests/test_mainwindow.cpp`, `tests/test_cardlayout.cpp` — updated expectations.
- `tests/CMakeLists.txt`, `src/CMakeLists.txt` — the new files.
- `CHANGELOG.md`, `CLAUDE.md`, the backlog.

---

## Task 1: A thread row reports its kind

`ThreadListModel` gains one predicate that everything else keys on. `hasChildren()` already contains this logic; this exposes it as the model's own answer so callers stop re-deriving it.

**Files:**
- Modify: `src/threadlistmodel.h`, `src/threadlistmodel.cpp`
- Test: `tests/test_threadlistmodel.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_threadlistmodel.cpp`, declaring both in the `private slots:` block:

```cpp
void TestThreadListModel::aSummaryWithOneMessageIsAMessageRow()
{
    ThreadListModel model;
    ThreadSummary one = makeThread(QStringLiteral("t1"), QStringLiteral("Alone"));
    one.totalCount = 1;
    one.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ one });

    const QModelIndex row = model.index(0, 0, QModelIndex());
    QVERIFY2(!model.isConversationRow(row),
             "a thread of one message is not a conversation: it has no replies "
             "to stand for, and must open its message on one click");
}

void TestThreadListModel::aSummaryWithRepliesIsAConversationRow()
{
    ThreadListModel model;
    ThreadSummary many = makeThread(QStringLiteral("t1"), QStringLiteral("Talk"));
    many.totalCount = 4;
    many.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ many });

    const QModelIndex row = model.index(0, 0, QModelIndex());
    QVERIFY(model.isConversationRow(row));
}

void TestThreadListModel::aLoadedThreadTrustsItsChildrenOverItsCount()
{
    // notmuch's totalCount counts duplicates, so a "thread of 2" can load with
    // no replies at all. Once loaded the children are the truth, exactly as
    // hasChildren() already decides.
    ThreadListModel model;
    ThreadSummary many = makeThread(QStringLiteral("t1"), QStringLiteral("Dupe"));
    many.totalCount = 2;
    many.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ many });

    MessageNode root;
    root.messageId = QStringLiteral("m1");
    root.threadId = QStringLiteral("t1");
    root.depth = 0;
    model.setThreadMessages(QStringLiteral("t1"), { root });

    const QModelIndex row = model.index(0, 0, QModelIndex());
    QVERIFY2(!model.isConversationRow(row),
             "a thread whose count came from duplicates still claims to be a "
             "conversation after loading no replies at all");
}

void TestThreadListModel::aMessageRowIsNeverAConversationRow()
{
    ThreadListModel model;
    ThreadSummary many = makeThread(QStringLiteral("t1"), QStringLiteral("Talk"));
    many.totalCount = 2;
    many.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ many });

    MessageNode root;
    root.messageId = QStringLiteral("m1");
    root.threadId = QStringLiteral("t1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m2");
    reply.threadId = QStringLiteral("t1");
    reply.depth = 1;
    model.setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex thread = model.index(0, 0, QModelIndex());
    const QModelIndex replyRow = model.index(0, 0, thread);
    QVERIFY(model.isMessageRow(replyRow));
    QVERIFY2(!model.isConversationRow(replyRow),
             "a reply row answered yes, so an action on it would scope to the "
             "whole conversation");
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_threadlistmodel`
Expected: compilation fails, `'isConversationRow' is not a member of 'ThreadListModel'`.

- [ ] **Step 3: Implement**

In `src/threadlistmodel.h`, beside `isMessageRow`:

```cpp
    /// Whether this row stands for a CONVERSATION rather than for one message.
    ///
    /// The single question every scope, label and membership decision keys on
    /// (item 177). A row with replies is the conversation; a row without them
    /// is its message and behaves as it always has. A message row is never
    /// either, so the answer is false there rather than undefined.
    bool isConversationRow(const QModelIndex &index) const;
```

In `src/threadlistmodel.cpp`:

```cpp
bool ThreadListModel::isConversationRow(const QModelIndex &index) const
{
    if (!index.isValid() || isMessageRow(index))
        return false;
    if (index.row() < 0 || index.row() >= m_threads.size())
        return false;

    // A flat view has no conversations by construction: every row is one
    // message and there is nothing to expand.
    if (m_flatMode)
        return false;

    const ThreadNode &node = m_threads.at(index.row());

    // Identical to hasChildren()'s rule, and deliberately so: an expander and a
    // conversation are the same fact. Once loaded the children are the truth,
    // which is how a thread whose totalCount counted DUPLICATES stops claiming
    // to be a conversation it cannot open.
    if (node.loaded)
        return !node.children.isEmpty();
    return node.summary.totalCount > 1;
}
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_threadlistmodel`
Expected: PASS, and every pre-existing test still passing.

- [ ] **Step 5: Commit**

```bash
git add src/threadlistmodel.h src/threadlistmodel.cpp tests/test_threadlistmodel.cpp
git commit -S -m "feat: let a row say whether it is a conversation

One predicate for the question every scope, label and membership decision
in item 177 keys on. It repeats hasChildren()'s rule deliberately: an
expander and a conversation are the same fact, including that a loaded
thread trusts its children over a count that included duplicates."
```

---

## Task 2: A conversation row draws the thread's own tags

Delete the first-message substitution. A conversation row's tags become `summary.tags`, notmuch's union, in one tier.

**Files:**
- Modify: `src/threadlistmodel.cpp:367-400` (the `PillTagsRole` and `TagsRole` thread branch), `src/threadlistmodel.h`
- Test: `tests/test_threadlistmodel.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestThreadListModel::aConversationRowDrawsTheThreadsTags()
{
    // Item 110 made a card draw its first message's tags so a four-message
    // thread would stop claiming a `signed` its displayed message lacked.
    // Under item 177 the row IS the conversation, so the union is what it
    // means and the substitution is wrong.
    ThreadListModel model;
    ThreadSummary t = makeThread(QStringLiteral("t1"), QStringLiteral("Talk"));
    t.totalCount = 4;
    t.tags = QStringList{ QStringLiteral("inbox"), QStringLiteral("signed") };
    t.firstMessageId = QStringLiteral("m1");
    t.firstMessageTags = QStringList{ QStringLiteral("inbox") };
    model.appendBatch({ t });

    const QModelIndex row = model.index(0, 0, QModelIndex());
    const QStringList pills =
        model.data(row, ThreadListModel::PillTagsRole).toStringList();

    QVERIFY2(pills.contains(QStringLiteral("signed")),
             "the card dropped a tag the conversation carries, so a signed "
             "thread does not read as one until it is expanded");
    QCOMPARE(model.data(row, ThreadListModel::TagsRole).toStringList(),
             t.tags);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_threadlistmodel aConversationRowDrawsTheThreadsTags`
Expected: FAIL, "the card dropped a tag the conversation carries".

- [ ] **Step 3: Implement**

In the thread branch of `ThreadListModel::data()`, delete the block that substitutes `node.first.tags` for `summary.tags` and read the summary directly. Delete `PillOwnCountRole` from the role enum in `src/threadlistmodel.h` and every `case` for it.

Delete `ThreadListModel::setRootMessageTags()` (declaration and definition), and in `nodeFor()` delete the block that seeds `node.first` from the summary, leaving:

```cpp
ThreadListModel::ThreadNode
ThreadListModel::nodeFor(const ThreadSummary &summary)
{
    // No `first` node. A conversation row stands for the thread and draws the
    // union; a one-message row's message arrives with its children like any
    // other. Seeding it here is what made a row an ambiguous half-message
    // (item 177).
    return ThreadNode{ summary, {}, {}, false };
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_threadlistmodel`
Expected: the new test PASSES. Several item 110/111 tests now FAIL — that is correct, they assert the behaviour being removed. Delete them: any test naming `PillOwnCountRole`, `setRootMessageTags`, or asserting a card drops a sibling's tag.

- [ ] **Step 5: Fix the one caller**

`src/mainwindow.cpp:4076` calls `m_model->setRootMessageTags(ref.messageId, ref.tags)`. Delete that call and the loop around it if it does nothing else.

- [ ] **Step 6: Run the full suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: only `undoMovesTheMessageBack` failing.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -S -m "feat: draw a conversation row's own tags, in one tier

Items 110 and 111 reconciled a card that showed one message with a row that
was a thread. The row is the conversation now, so the union is simply what
it means: the first-message substitution, PillOwnCountRole and the seeded
first node all go."
```

---

## Task 3: Delete the sibling chip tier

The muted second tier has nothing left to show.

**Files:**
- Modify: `src/cardlayout.h` / `.cpp` (`siblingFont`), `src/carddelegate.h` / `.cpp` (`mutedChipColour`, `chipSize`'s scale)
- Test: `tests/test_cardlayout.cpp`, `tests/test_carddelegate.cpp` if present

- [ ] **Step 1: Find what to remove**

Run: `grep -rn "siblingFont\|mutedChipColour\|PillOwnCount" src/ tests/`
Every hit is either a deletion or a caller to simplify.

- [ ] **Step 2: Delete, and simplify the callers**

Remove `CardLayout::siblingFont()` and `CardDelegate::mutedChipColour()` with their declarations. `CardDelegate::chipSize()` loses its scale parameter and calls `TagChip::sizeFor()` at full size. `TagChip::sizeFor()` keeps its scale parameter — it is a general facility and nothing else changes.

- [ ] **Step 3: Delete the tests that assert the tier**

Any test asserting a sibling font ratio, a muted chip colour, or a chip-size scale is asserting a removed feature. Delete rather than adapt.

- [ ] **Step 4: Run**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: only `undoMovesTheMessageBack` failing.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -S -m "refactor: remove the sibling chip tier

Nothing is a sibling any more: a conversation row draws the thread's tags
and a message row draws its own."
```

---

## Task 4: An action scopes to what the row means

`scopeFor` and `messageScopeFor` are replaced by one resolver that reads the row kind.

**Files:**
- Modify: `src/threadlistmodel.h` / `.cpp`
- Test: `tests/test_threadlistmodel.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
void TestThreadListModel::aConversationRowResolvesToItsThread()
{
    ThreadListModel model;
    ThreadSummary one = makeThread(QStringLiteral("t1"), QStringLiteral("Alone"));
    one.totalCount = 1;
    one.firstMessageId = QStringLiteral("m1");
    // SECOND, so a wrong answer is visible rather than accidentally right.
    ThreadSummary many = makeThread(QStringLiteral("t2"), QStringLiteral("Talk"));
    many.totalCount = 4;
    many.firstMessageId = QStringLiteral("m2");
    model.appendBatch({ one, many });

    const ActionScope scope =
        model.scopeForSelection({ model.index(1, 0, QModelIndex()) });

    QCOMPARE(scope.threadIds, QStringList{ QStringLiteral("t2") });
    QVERIFY2(scope.messageIds.isEmpty(),
             "a conversation row named a message, so an action on it would "
             "touch one message of the thread it claims to act on");
    QVERIFY(scope.wholeThread);
}

void TestThreadListModel::aLoneMessageRowResolvesToItsMessage()
{
    ThreadListModel model;
    ThreadSummary many = makeThread(QStringLiteral("t1"), QStringLiteral("Talk"));
    many.totalCount = 4;
    many.firstMessageId = QStringLiteral("m1");
    ThreadSummary one = makeThread(QStringLiteral("t2"), QStringLiteral("Alone"));
    one.totalCount = 1;
    one.firstMessageId = QStringLiteral("m2");
    model.appendBatch({ many, one });

    const ActionScope scope =
        model.scopeForSelection({ model.index(1, 0, QModelIndex()) });

    QCOMPARE(scope.messageIds, QStringList{ QStringLiteral("m2") });
    QVERIFY(scope.threadIds.isEmpty());
    QVERIFY(!scope.wholeThread);
}

void TestThreadListModel::aReplyRowResolvesToItsMessage()
{
    ThreadListModel model;
    ThreadSummary first = makeThread(QStringLiteral("t1"), QStringLiteral("One"));
    first.totalCount = 1;
    first.firstMessageId = QStringLiteral("m0");
    ThreadSummary many = makeThread(QStringLiteral("t2"), QStringLiteral("Talk"));
    many.totalCount = 2;
    many.firstMessageId = QStringLiteral("m1");
    model.appendBatch({ first, many });

    MessageNode root;
    root.messageId = QStringLiteral("m1");
    root.threadId = QStringLiteral("t2");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m2");
    reply.threadId = QStringLiteral("t2");
    reply.depth = 1;
    model.setThreadMessages(QStringLiteral("t2"), { root, reply });

    const QModelIndex thread = model.index(1, 0, QModelIndex());
    const ActionScope scope =
        model.scopeForSelection({ model.index(0, 0, thread) });

    QCOMPARE(scope.messageIds, QStringList{ QStringLiteral("m2") });
    QVERIFY(scope.threadIds.isEmpty());
}

void TestThreadListModel::aMixedSelectionCarriesBothScopes()
{
    ThreadListModel model;
    ThreadSummary one = makeThread(QStringLiteral("t1"), QStringLiteral("Alone"));
    one.totalCount = 1;
    one.firstMessageId = QStringLiteral("m1");
    ThreadSummary many = makeThread(QStringLiteral("t2"), QStringLiteral("Talk"));
    many.totalCount = 4;
    many.firstMessageId = QStringLiteral("m2");
    model.appendBatch({ one, many });

    const ActionScope scope =
        model.scopeForSelection({ model.index(0, 0, QModelIndex()),
                                  model.index(1, 0, QModelIndex()) });

    QCOMPARE(scope.messageIds, QStringList{ QStringLiteral("m1") });
    QCOMPARE(scope.threadIds, QStringList{ QStringLiteral("t2") });
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_threadlistmodel`
Expected: compilation fails, `'scopeForSelection' is not a member`.

- [ ] **Step 3: Implement**

Add to `src/threadlistmodel.h`:

```cpp
    /// What a selection means, resolved per row from what that row IS.
    ///
    /// Replaces the scopeFor()/messageScopeFor() pair, which made the caller
    /// choose the scope and so let one gesture mean two things (item 177). A
    /// conversation row contributes its thread, any other row its message, and
    /// a mixed selection carries both.
    ActionScope scopeForSelection(const QModelIndexList &selection) const;
```

In `src/threadlistmodel.cpp`:

```cpp
ActionScope ThreadListModel::scopeForSelection(
    const QModelIndexList &selection) const
{
    ActionScope scope;

    for (const QModelIndex &index : selection) {
        if (isConversationRow(index)) {
            const ThreadSummary &summary = m_threads.at(index.row()).summary;
            if (scope.threadIds.contains(summary.threadId))
                continue;
            scope.threadIds.append(summary.threadId);
            // totalCount, not the loaded children: an unexpanded conversation
            // still has all of its messages, and the count is what the status
            // bar names after the fact.
            scope.messageCount += summary.totalCount;
            scope.wholeThread = true;
            continue;
        }

        const QString messageId =
            isMessageRow(index)
                ? messageAt(index).messageId
                : (index.row() >= 0 && index.row() < m_threads.size()
                       ? m_threads.at(index.row()).summary.firstMessageId
                       : QString());

        // Skipped rather than widened. Falling back to the thread would
        // silently act on messages the row does not stand for.
        if (messageId.isEmpty() || scope.messageIds.contains(messageId))
            continue;
        scope.messageIds.append(messageId);
        scope.messageCount += 1;
    }

    return scope;
}
```

Delete `scopeFor()` and `messageScopeFor()` once Task 5 has moved their callers.

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_threadlistmodel`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/threadlistmodel.h src/threadlistmodel.cpp tests/test_threadlistmodel.cpp
git commit -S -m "feat: resolve a selection's scope from what each row is

One resolver replacing the scopeFor/messageScopeFor pair. The caller no
longer chooses the scope, which is what let one gesture mean two things."
```

---

## Task 5: Actions read the selection, and say what they will do

`tagSelected` loses its `TagScope` parameter. The five `*_thread` actions and their submenu go. Labels name their scope.

**Files:**
- Modify: `src/mainwindow.cpp` (action construction ~1732-1830, `tagSelected` ~5460, the submenu ~5393, `KeyMap::knownActions()` in `src/keymap.cpp`)
- Test: `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
void TestMainWindow::theUnreadActionNamesTheThreadOnAConversationRow()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(model && view);

    ThreadSummary one = makeThread(QStringLiteral("t1"),
                                   QStringList{ QStringLiteral("unread") });
    one.totalCount = 1;
    ThreadSummary many = makeThread(QStringLiteral("t2"),
                                    QStringList{ QStringLiteral("unread") });
    many.totalCount = 4;
    model->appendBatch({ one, many });

    auto *action = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(action);

    selectThreadRow(view, 0);
    QApplication::processEvents();
    const QString onMessage = action->text();

    selectThreadRow(view, 1);
    QApplication::processEvents();
    const QString onThread = action->text();

    QVERIFY2(onMessage != onThread,
             "the label reads the same on a message and on a conversation, so "
             "nothing tells the user which one the key will act on");
    QVERIFY2(onThread.contains(QStringLiteral("thread"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a conversation row's label does not "
                                       "name the thread: %1").arg(onThread)));
}

void TestMainWindow::deleteIsAbsentOnAReplyRow()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(model && view);

    ThreadSummary first = makeThread(QStringLiteral("t1"), {});
    first.totalCount = 1;
    ThreadSummary many = makeThread(QStringLiteral("t2"), {});
    many.totalCount = 2;
    model->appendBatch({ first, many });

    MessageNode root;
    root.messageId = QStringLiteral("m1");
    root.threadId = QStringLiteral("t2");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m2");
    reply.threadId = QStringLiteral("t2");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t2"), { root, reply });

    const QModelIndex thread = model->index(1, 0, QModelIndex());
    view->expand(thread);
    const QModelIndex replyRow = model->index(0, 0, thread);
    view->selectionModel()->select(
        replyRow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(replyRow);
    QApplication::processEvents();

    auto *del = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(del);
    QVERIFY2(!del->isVisible() || !del->isEnabled(),
             "Delete is offered on a reply: deleting is a conversation-level "
             "action and a single reply cannot be removed from a thread");
}

void TestMainWindow::theWholeThreadSubmenuIsGone()
{
    const Config config;
    MainWindow window(config);

    for (const QString &name : { QStringLiteral("archive_thread"),
                                 QStringLiteral("delete_thread"),
                                 QStringLiteral("spam_thread"),
                                 QStringLiteral("flag_thread"),
                                 QStringLiteral("mark_thread_read") }) {
        QVERIFY2(!window.findChild<QAction *>(name),
                 qPrintable(QStringLiteral("%1 still exists; the scope now "
                                           "comes from the row, so a separate "
                                           "action is a second answer to a "
                                           "settled question").arg(name)));
    }
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow theUnreadActionNamesTheThreadOnAConversationRow deleteIsAbsentOnAReplyRow theWholeThreadSubmenuIsGone`
Expected: all three FAIL.

- [ ] **Step 3: Implement**

- `tagSelected(add, remove, description)` drops its `TagScope` parameter and calls `m_model->scopeForSelection(rows)`. It already splits on `scope.threadIds` / `scope.messageIds` being non-empty and pushes the matching command; that code is unchanged.
- Delete the five `addAction(...)` calls for `archive_thread`, `delete_thread`, `spam_thread`, `flag_thread`, `mark_thread_read`, their entries in the icon table, and the `Whole thread` submenu construction at `src/mainwindow.cpp:5393`.
- Delete those five names from `KeyMap::knownActions()` and from `defaultBindings()`. A `Q_ASSERT` in `KeyMap`'s constructor fires otherwise, and it surfaces in whichever suite builds a `MainWindow` first.
- Extend `refreshUnreadAction()` — which already sets a dynamic label — to set every scoped action's text from the selection. Use `QT_TRANSLATE_NOOP` only if a literal ends up in an array; a `tr()` at the call site is fine here.
- In the same refresh, hide `delete`, `restore` and `archive` when every selected row is a reply.

- [ ] **Step 4: Run**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: the three new tests PASS. Tests naming the deleted actions FAIL and must be deleted; tests calling `tagSelected` with a `TagScope` need the argument dropped. `everyActionIsReachableFromAMenu()` and `everyActionCarriesAnIcon()` must still pass — they are what catch a half-finished deletion.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -S -m "feat: scope an action to the row it was invoked on

The five *_thread actions and their submenu are gone: the row's identity is
what decides the scope, so a second set of actions was a second answer to a
settled question. Labels name the scope, and Delete is absent on a reply."
```

---

## Task 6: Membership is the union

**Files:**
- Modify: `src/threadlistmodel.cpp` (`removeThreadsWithoutTag`), `src/mainwindow.cpp` (the membership sync)
- Test: `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
void TestMainWindow::aConversationStaysWhileAnyMessageMatches()
{
    // The reported case: a 44-message thread with two replies still unread.
    // Reading one message must not evict the conversation.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && queryEdit);

    queryEdit->setText(
        config.resolvedQuery(Config::builtinFilter(QStringLiteral("unread")),
                             QString()));

    ThreadSummary other = makeThread(QStringLiteral("t1"),
                                     QStringList{ QStringLiteral("unread") });
    other.totalCount = 1;
    ThreadSummary big = makeThread(QStringLiteral("t2"),
                                   QStringList{ QStringLiteral("unread") });
    big.totalCount = 44;
    model->appendBatch({ other, big });

    // One message of the conversation is read. The union still says unread.
    window.sendMessageTagChangeForTesting({ QStringLiteral("m-one@example.org") },
                                          {}, { QStringLiteral("unread") },
                                          QStringLiteral("Mark read"));

    QCOMPARE(model->rowCount(QModelIndex()), 2);
}

void TestMainWindow::aConversationLeavesWhenItsUnionEmpties()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(
        config.resolvedQuery(Config::builtinFilter(QStringLiteral("unread")),
                             QString()));

    ThreadSummary keep = makeThread(QStringLiteral("t1"),
                                    QStringList{ QStringLiteral("unread") });
    keep.totalCount = 1;
    ThreadSummary go = makeThread(QStringLiteral("t2"),
                                  QStringList{ QStringLiteral("unread") });
    go.totalCount = 4;
    model->appendBatch({ keep, go });

    // Selected elsewhere, so the never-evict-the-current-row rule is not what
    // is being measured here.
    selectThreadRow(view, 0);
    QApplication::processEvents();

    window.sendThreadTagChangeForTesting({ QStringLiteral("t2") }, {},
                                         { QStringLiteral("unread") },
                                         QStringLiteral("Mark thread read"));

    QCOMPARE(model->rowCount(QModelIndex()), 1);
    QCOMPARE(model->threadAt(0).threadId, QStringLiteral("t1"));
}
```

`sendThreadTagChangeForTesting` does not exist yet; add it in `src/mainwindow.h` beside `sendMessageTagChangeForTesting`, forwarding to `sendThreadTagChange`.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow aConversationStaysWhileAnyMessageMatches aConversationLeavesWhenItsUnionEmpties`
Expected: FAIL.

- [ ] **Step 3: Implement**

`removeThreadsWithoutTag(threadIds, tag)` judges on `node.summary.tags` only — never on a first-message node, which no longer exists. Restore the membership sync from the stashed work with that one change, keeping both rules the spec records: never evict the current row, and defer an automatic write's eviction until the selection moves.

The stash is recoverable and is the starting point rather than the answer:

```bash
git stash list          # find the item 170 entry
git stash show -p stash@{N} -- src/mainwindow.cpp
```

- [ ] **Step 4: Run**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: only `undoMovesTheMessageBack` failing.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -S -m "feat: judge a row's membership on the thread's union

Closes item 170. A conversation belongs to a view while any of its messages
match, so reading one message of a thread no longer takes the conversation
out of the Unread view. The current row is never evicted, and an automatic
write defers its eviction until the selection moves."
```

---

## Task 7: The digest struct and the worker call

**Files:**
- Create: `src/threaddigest.h`
- Modify: `src/notmuchworker.h` / `.cpp`, `src/CMakeLists.txt`
- Test: `tests/test_notmuchworker.cpp`

- [ ] **Step 1: Write `src/threaddigest.h`**

```cpp
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 */
#ifndef QTMAILDIR_THREADDIGEST_H
#define QTMAILDIR_THREADDIGEST_H

#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QVector>

#include "types.h"

/// What the thread dashboard shows, as a plain value.
///
/// Crosses the worker boundary like every other structure here: no
/// notmuch_* pointer, no widget, no model. Everything in it is read from the
/// INDEX, so building one opens no message files.
struct ThreadDigest
{
    QString threadId;

    /// Display name and message count, most prolific first.
    QList<QPair<QString, int>> senders;

    /// The unread messages, NEWEST FIRST, at most kUnreadShown of them.
    QVector<MessageRef> unread;

    /// How many there really are. The list above is capped, so a count taken
    /// from its size would under-report on exactly the threads that need the
    /// number most.
    int unreadTotal = 0;

    int totalCount = 0;

    /// Always kBuckets entries. A fixed count is what keeps the sparkline's
    /// geometry testable; a thread spanning five days and one spanning two
    /// years cannot share a bucket size, so the span is what varies and the
    /// label beneath carries the truth.
    QVector<int> buckets;

    qint64 firstTimestamp = 0;
    qint64 lastTimestamp = 0;
    int busiestBucket = -1;

    static constexpr int kBuckets = 7;
    static constexpr int kUnreadShown = 5;
};

Q_DECLARE_METATYPE(ThreadDigest)

#endif // QTMAILDIR_THREADDIGEST_H
```

- [ ] **Step 2: Write the failing test**

In `tests/test_notmuchworker.cpp`, against the existing fixture:

```cpp
void TestNotmuchWorker::aDigestCountsSendersAndUnread()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.addMessage(QStringLiteral("inbox"),
                               QStringLiteral("d0@example.org"),
                               QStringLiteral("Digest root"),
                               QStringLiteral("alice@example.org"),
                               QStringLiteral("Mon, 24 Aug 2026 10:00:00 +0200"),
                               QStringLiteral("Root."), false));
    QVERIFY(fixture.addMessage(QStringLiteral("inbox"),
                               QStringLiteral("d1@example.org"),
                               QStringLiteral("Re: Digest root"),
                               QStringLiteral("alice@example.org"),
                               QStringLiteral("Tue, 25 Aug 2026 10:00:00 +0200"),
                               QStringLiteral("Again."), false,
                               QStringLiteral("d0@example.org")));
    QVERIFY(fixture.addMessage(QStringLiteral("inbox"),
                               QStringLiteral("d2@example.org"),
                               QStringLiteral("Re: Digest root"),
                               QStringLiteral("bob@example.org"),
                               QStringLiteral("Wed, 26 Aug 2026 10:00:00 +0200"),
                               QStringLiteral("Unread one."), true,
                               QStringLiteral("d0@example.org")));
    QVERIFY(fixture.index());

    NotmuchWorker worker(fixture.configPath());
    QSignalSpy spy(&worker, &NotmuchWorker::threadDigestLoaded);

    const QString threadId = worker.threadIdForTesting(
        QStringLiteral("id:d0@example.org"));
    QVERIFY(!threadId.isEmpty());
    worker.loadThreadDigest(threadId, 1);

    QCOMPARE(spy.count(), 1);
    const ThreadDigest digest = spy.at(0).at(0).value<ThreadDigest>();

    QCOMPARE(digest.totalCount, 3);
    QCOMPARE(digest.unreadTotal, 1);
    QCOMPARE(digest.unread.size(), 1);
    QCOMPARE(digest.unread.at(0).messageId, QStringLiteral("d2@example.org"));

    // Alice twice, Bob once, most prolific first.
    QCOMPARE(digest.senders.size(), 2);
    QCOMPARE(digest.senders.at(0).second, 2);
    QCOMPARE(digest.senders.at(1).second, 1);

    QCOMPARE(digest.buckets.size(), ThreadDigest::kBuckets);
    int summed = 0;
    for (int n : digest.buckets)
        summed += n;
    QCOMPARE(summed, 3);
}

void TestNotmuchWorker::aDigestCapsItsUnreadListButNotItsCount()
{
    NotmuchFixture fixture;
    QVERIFY(fixture.addMessage(QStringLiteral("inbox"),
                               QStringLiteral("c0@example.org"),
                               QStringLiteral("Cap root"),
                               QStringLiteral("alice@example.org"),
                               QStringLiteral("Mon, 24 Aug 2026 10:00:00 +0200"),
                               QStringLiteral("Root."), false));
    for (int i = 1; i <= 8; ++i) {
        QVERIFY(fixture.addMessage(
            QStringLiteral("inbox"),
            QStringLiteral("c%1@example.org").arg(i),
            QStringLiteral("Re: Cap root"),
            QStringLiteral("bob@example.org"),
            QStringLiteral("Tue, 25 Aug 2026 %1:00:00 +0200")
                .arg(i, 2, 10, QLatin1Char('0')),
            QStringLiteral("Reply."), true, QStringLiteral("c0@example.org")));
    }
    QVERIFY(fixture.index());

    NotmuchWorker worker(fixture.configPath());
    QSignalSpy spy(&worker, &NotmuchWorker::threadDigestLoaded);
    worker.loadThreadDigest(
        worker.threadIdForTesting(QStringLiteral("id:c0@example.org")), 1);

    const ThreadDigest digest = spy.at(0).at(0).value<ThreadDigest>();
    QCOMPARE(digest.unreadTotal, 8);
    QCOMPARE(digest.unread.size(), ThreadDigest::kUnreadShown);
    // Newest first: c8 is the latest.
    QCOMPARE(digest.unread.at(0).messageId, QStringLiteral("c8@example.org"));
}
```

`threadIdForTesting(query)` may not exist; if not, add it to `NotmuchWorker` returning the first matching thread's id, guarded for tests only.

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_notmuchworker`
Expected: compilation fails, `loadThreadDigest` undeclared.

- [ ] **Step 4: Implement**

Register the metatype beside the struct, not in `MainWindow` — a caller that never constructs one still needs it (`Q_ENUM` trap, CLAUDE.md):

```cpp
qRegisterMetaType<ThreadDigest>("ThreadDigest");
```

`loadThreadDigest(const QString &threadId, quint64 generation)` opens read-only, runs `thread:<id>`, walks the messages once collecting sender counts, unread refs and timestamps, buckets the span into `kBuckets`, sorts senders by count and unread by date descending, truncates to `kUnreadShown`, and emits `threadDigestLoaded(ThreadDigest, quint64)`.

Its generation counter is its OWN. Bumping `m_generation` would discard a thread load in flight and blank the message pane because the user selected a row.

Add `src/threaddigest.h` to the `qtmaildir_lib` sources in `src/CMakeLists.txt`.

- [ ] **Step 5: Run**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_notmuchworker`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -S -m "feat: read a thread's digest from the index

Senders, unread messages and an activity histogram for the dashboard, as a
plain value struct over a queued signal. Everything comes from the index, so
no message file is opened; the unread list is capped and unreadTotal carries
the real number."
```

---

## Task 8: The dashboard widget

**Files:**
- Create: `src/threaddashboard.h`, `src/threaddashboard.cpp`, `tests/test_threaddashboard.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/test_threaddashboard.cpp`:

```cpp
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 */
#include <QtTest>

#include "threaddashboard.h"
#include "threaddigest.h"

class TestThreadDashboard : public QObject
{
    Q_OBJECT

private slots:
    void itNamesTheUnreadCountItWasGiven();
    void itSaysAllCaughtUpWhenNothingIsUnread();
    void itOffersMoreWhenTheListIsCapped();
    void itOffersNoMoreLinkWhenTheListIsComplete();
};

static ThreadDigest digestWith(int unreadTotal, int shown)
{
    ThreadDigest d;
    d.threadId = QStringLiteral("t1");
    d.totalCount = 44;
    d.unreadTotal = unreadTotal;
    for (int i = 0; i < shown; ++i) {
        MessageRef ref;
        ref.messageId = QStringLiteral("m%1@example.org").arg(i);
        ref.subject = QStringLiteral("Re: something");
        d.unread.append(ref);
    }
    d.buckets = QVector<int>(ThreadDigest::kBuckets, 1);
    return d;
}

void TestThreadDashboard::itNamesTheUnreadCountItWasGiven()
{
    ThreadDashboard dashboard;
    dashboard.setDigest(digestWith(3, 3));
    QCOMPARE(dashboard.unreadCountShown(), 3);
    QVERIFY(!dashboard.showingAllCaughtUp());
}

void TestThreadDashboard::itSaysAllCaughtUpWhenNothingIsUnread()
{
    ThreadDashboard dashboard;
    dashboard.setDigest(digestWith(0, 0));
    QVERIFY2(dashboard.showingAllCaughtUp(),
             "a fully read thread shows an empty Waiting for you block rather "
             "than saying so");
}

void TestThreadDashboard::itOffersMoreWhenTheListIsCapped()
{
    // 20 unread, 5 shown: the link has to name the 15 the pane cannot list, or
    // the dashboard under-reports exactly when the number matters most.
    ThreadDashboard dashboard;
    dashboard.setDigest(digestWith(20, ThreadDigest::kUnreadShown));
    QCOMPARE(dashboard.hiddenUnreadCount(), 15);
}

void TestThreadDashboard::itOffersNoMoreLinkWhenTheListIsComplete()
{
    ThreadDashboard dashboard;
    dashboard.setDigest(digestWith(4, 4));
    QCOMPARE(dashboard.hiddenUnreadCount(), 0);
}

QTEST_MAIN(TestThreadDashboard)
#include "test_threaddashboard.moc"
```

Add `add_qtmaildir_test(threaddashboard)` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
Expected: fails, no `threaddashboard.h`.

- [ ] **Step 3: Implement**

`ThreadDashboard : public QWidget`, built once in its constructor and repopulated by `setDigest()`. Structure, per the spec:

- A `QScrollArea` holding the content; the action strip is its sibling, outside it, pinned to the bottom.
- Content, top to bottom: header (`QLabel`, `Qt::PlainText` — subjects come from strangers), tag chips, counts with a `QProgressBar`, the Waiting-for-you list, the sparkline.
- Colours come from `Avatar::pixmapFor()` and `TagColors::colourFor()`. The pane defines none of its own.
- The sparkline is a small `QWidget` subclass painting `digest.buckets` at fixed height, highlighting `busiestBucket`.
- Signals: `messageActivated(QString messageId)` from an unread entry, `expandRequested()` from the `+N more` link, and one per action button.

Accessors `unreadCountShown()`, `hiddenUnreadCount()` and `showingAllCaughtUp()` exist so the content is assertable without rendering.

Every user-facing string goes through `tr()`.

- [ ] **Step 4: Run**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_threaddashboard`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -S -m "feat: add the thread dashboard

A widget over a ThreadDigest: header, tags, counts, the unread list capped
with a link to the rest, and an activity sparkline, scrolling under a pinned
action strip. It invents no colours."
```

---

## Task 9: Wire the dashboard into the pane

**Files:**
- Modify: `src/messageview.h` / `.cpp`, `src/mainwindow.cpp` (`onThreadSelected` ~3854)
- Test: `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
void TestMainWindow::selectingAConversationShowsTheDashboard()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(model && view && pane);

    ThreadSummary one = makeThread(QStringLiteral("t1"), {});
    one.totalCount = 1;
    ThreadSummary many = makeThread(QStringLiteral("t2"), {});
    many.totalCount = 4;
    model->appendBatch({ one, many });

    selectThreadRow(view, 1);
    QApplication::processEvents();

    QVERIFY2(pane->showingDashboard(),
             "selecting a conversation rendered a message: the row stands for "
             "the thread and has no message to show");
}

void TestMainWindow::selectingALoneMessageShowsTheMessage()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(model && view && pane);

    ThreadSummary many = makeThread(QStringLiteral("t1"), {});
    many.totalCount = 4;
    ThreadSummary one = makeThread(QStringLiteral("t2"), {});
    one.totalCount = 1;
    model->appendBatch({ many, one });

    selectThreadRow(view, 1);
    QApplication::processEvents();

    QVERIFY2(!pane->showingDashboard(),
             "a thread of one message showed a dashboard: it must open on one "
             "click, which is the case the whole split exists to protect");
}
```

- [ ] **Step 2: Run to verify they fail**

Expected: compilation fails, `showingDashboard` undeclared.

- [ ] **Step 3: Implement**

`MessageView` gains `showDashboard(const ThreadDigest &)` and `showingDashboard()`, switching its stack exactly as `showPlaceholder()` already does.

`onThreadSelected` branches on `m_model->isConversationRow(current)`: a conversation requests a digest and shows the dashboard; anything else loads its message as today. The automatic mark-read is **not armed for a conversation row** — there is no displayed message to mark.

Connect `ThreadDashboard::messageActivated` to select that message's row, expanding first, and `expandRequested` to expand the thread.

- [ ] **Step 4: Run**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: only `undoMovesTheMessageBack` failing.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -S -m "feat: show the dashboard when a conversation is selected

A thread row has no message to render, so the pane shows the conversation
instead. A thread of one message still opens its message on one click, and
the automatic mark-read is not armed for a row that displays nothing."
```

---

## Task 10: Item 176, undo covers what the write changed

**Files:**
- Modify: `src/notmuchworker.h` / `.cpp` (`applyTags`), `src/mainwindow.h` (`MessageTagCommand`)
- Test: `tests/test_notmuchworker.cpp`, `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestNotmuchWorker::applyTagsReportsOnlyTheMessagesItChanged()
{
    // Item 176. Undo inverts the tags and keeps the scope, so the inverse of
    // "remove unread from 44 messages" was "add unread to 44 messages",
    // whether or not they carried it. Measured on real mail: a thread of 44
    // with 2 unread came back with 43 unread.
    NotmuchFixture fixture;
    QVERIFY(fixture.addMessage(QStringLiteral("inbox"),
                               QStringLiteral("u0@example.org"),
                               QStringLiteral("Read one"),
                               QStringLiteral("alice@example.org"),
                               QStringLiteral("Mon, 24 Aug 2026 10:00:00 +0200"),
                               QStringLiteral("Body."), false));
    QVERIFY(fixture.addMessage(QStringLiteral("inbox"),
                               QStringLiteral("u1@example.org"),
                               QStringLiteral("Unread one"),
                               QStringLiteral("bob@example.org"),
                               QStringLiteral("Tue, 25 Aug 2026 10:00:00 +0200"),
                               QStringLiteral("Body."), true));
    QVERIFY(fixture.index());

    NotmuchWorker worker(fixture.configPath());
    QSignalSpy spy(&worker, &NotmuchWorker::tagsApplied);

    worker.applyTags(TagChange{
        { QStringLiteral("u0@example.org"), QStringLiteral("u1@example.org") },
        {}, { QStringLiteral("unread") }, QStringLiteral("Mark read") });

    QCOMPARE(spy.count(), 1);
    const TagChange applied = spy.at(0).at(0).value<TagChange>();
    QCOMPARE(applied.messageIds,
             QStringList{ QStringLiteral("u1@example.org") });
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_notmuchworker applyTagsReportsOnlyTheMessagesItChanged`
Expected: FAIL, both ids reported.

- [ ] **Step 3: Implement**

In `applyTags`, before freezing each message, read its current tags and record the id only if the change would actually move one. Emit `tagsApplied` with a `TagChange` carrying that reduced list.

`MessageTagCommand` stores the ids from `tagsApplied` rather than the ones it asked for. A thread command's undo is already honest under item 177 — a thread action really does mean the whole conversation — so the remaining exposure is the multi-message case, which this closes.

- [ ] **Step 4: Write the window-level test**

Add to `tests/test_mainwindow.cpp`, declared in the `private slots:` block. It
must sit BELOW the definition of `notmuchCount()`, which is a file-local static
declared partway down the file:

```cpp
void TestMainWindow::undoingAMarkReadRestoresOnlyWhatWasUnread()
{
    // Item 176, end to end. The thread has messages in DISAGREEING states:
    // two in the same state answer identically whichever way the code
    // resolves them, so a test built on agreement passes against the bug.
    //
    // Measured on the user's real mail before the fix: a thread of 44 with 2
    // unread was marked read, undone, and came back with 43 unread. Because
    // maildir.synchronize_flags is on, that rewrote the files and would have
    // reached the server.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("ur0@example.org"),
        QStringLiteral("UR root"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Root body."), false));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("ur1@example.org"),
        QStringLiteral("Re: UR root"), QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Already read."), false,
        QStringLiteral("ur0@example.org")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("ur2@example.org"),
        QStringLiteral("Re: UR root"), QStringLiteral("third@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 12:00:00 +0200"),
        QStringLiteral("The only unread one."), true,
        QStringLiteral("ur0@example.org")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    const QString cfg = backed.fixture().configPath();
    const QString thread = QStringLiteral("thread:{id:ur0@example.org}");

    // A `thread:` query, not `tag:unread`: the row must survive the write for
    // the undo to be driven through the interface at all.
    queryEdit->setText(thread);
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    QCOMPARE(notmuchCount(cfg, thread), 3);
    QCOMPARE(notmuchCount(cfg, thread + QStringLiteral(" and tag:unread")), 1);

    // A conversation row, so this is the thread-scoped write.
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    QVERIFY(model->isConversationRow(model->index(0, 0, QModelIndex())));
    window.findChild<QAction *>(QStringLiteral("toggle_unread"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, thread + QStringLiteral(" and tag:unread")) == 0,
        15000);

    window.findChild<QAction *>(QStringLiteral("undo"))->trigger();

    // ONE message unread again, the one that was. Before the fix this was 3.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, thread + QStringLiteral(" and tag:unread")) == 1,
        15000);
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:ur2@example.org and "
                                              "tag:unread")),
             1);
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:ur0@example.org and "
                                              "tag:unread")),
             0);
}
```

- [ ] **Step 5: Run**

Run: `ctest --test-dir build --output-on-failure`
Expected: only `undoMovesTheMessageBack` failing.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -S -m "fix: undo only what the write actually changed

Closes item 176. applyTags reports the messages whose tags really moved, and
a command stores that rather than what it asked for, so undoing a mark-read
no longer marks the whole conversation unread."
```

---

## Task 11: Documentation and close-out

**Files:**
- Modify: `CHANGELOG.md`, `CLAUDE.md`, `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md`, `translations/qtmaildir_it_IT.ts`

- [ ] **Step 1: Refresh the translation**

```bash
lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete -locations none
```

Translate every new string. `lrelease` must report 0 unfinished — it silently DROPS an unfinished string and ships it as English inside an Italian UI.

Run: `ctest --test-dir build -R translations`
Expected: PASS.

- [ ] **Step 2: Changelog**

Under `[Unreleased]`, in `### Changed`, describe: a thread row is the conversation; one-message rows unchanged; the `Whole thread` submenu is gone and labels name their scope; Delete and Archive are conversation-level; the dashboard. Add an `### Upgrading` section naming the five removed action names and what replaces them.

- [ ] **Step 3: CLAUDE.md**

Rewrite the thread-row sections that item 177 invalidates. Items 108, 110 and 111 are reversed; say so and why, so the next reader does not restore them. Replace the two-tier chip paragraphs with the union rule, and record the row-kind split and the membership rule.

- [ ] **Step 4: Backlog**

Mark items 170, 176 and 177 done with dates and evidence, and move their sections to `2026-08-03-post-0.1.0-usability-closed.md` on this commit. Restate item 168 in thread terms or record it as still open.

- [ ] **Step 5: Full suite and hand-off**

```bash
ctest --test-dir build --output-on-failure
```

Expected: only `undoMovesTheMessageBack` failing (item 136, pre-existing).

Do NOT launch the application. Hand it to the user with what to look at: a conversation row showing the dashboard, a one-message row opening on one click, reading a reply not evicting its thread from Unread, and the labels naming their scope.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -S -m "docs: close items 170, 176 and 177"
```

---

## Notes for whoever executes this

**Tasks 1 to 6 are one coherent change.** Between Task 2 and Task 6 the tree is internally consistent but the application behaves oddly — cards draw thread tags while actions may still be message-scoped. Do not hand it over mid-way for a hand test; the first useful stopping point is the end of Task 6.

**The stash is a starting point, not an answer.** The item 170 work stashed on 2026-08-28 contains a working membership sync, the deferred eviction, and about eight tests. Its `first.tags` handling is wrong under this design. Read it, take the parts that survive, and rewrite the rest.

**Item 136 is not yours.** `undoMovesTheMessageBack` fails on master and has its own open item.
