# Item 20: Message Rows in the Thread List — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the flat thread list into a tree where a thread's root row is its first message, expanding reveals the replies indented by reply depth, and selecting a reply opens that single message in the reading pane.

**Architecture:** Replace `QAbstractTableModel` + `QTableView` with `QAbstractItemModel` + `QTreeView`. The model holds a two-level-plus tree of nodes: thread roots (backed by `ThreadSummary`) and message nodes (backed by a new `MessageNode`). The worker gains a tree-aware `loadThreadTree` built on `notmuch_thread_get_toplevel_messages` (NOT the existing query-based walk, which cannot yield replies) and a `loadMessage` for single-message rendering. Action scope follows the selected row and is stated in the status bar.

**Tech Stack:** Qt 6.11 (`QAbstractItemModel`, `QTreeView`), libnotmuch 5, GMime 3, Qt Test.

**Spec:** `docs/superpowers/plans/2026-08-03-post-0.1.0-usability-closed.md`
section 20 (the section moved there when item 20 closed; the status table row
stays in `2026-08-03-post-0.1.0-usability.md`).

**Branch:** `item-20-message-rows`.

---

## Verified constraints (checked 2026-08-08, do not re-derive)

These were confirmed against the installed headers and source, not recalled. Several
contradict the obvious approach.

1. **`notmuch_message_get_replies` returns NULL for a message obtained from
   `notmuch_query_search_messages`.** `/usr/include/notmuch.h:1617-1628` states the
   call "only makes sense if 'message' was ultimately obtained from a
   notmuch_thread_t object". The existing `loadThread`
   (`src/notmuchworker.cpp`) uses the query walk, so it CANNOT be extended to
   produce a tree. The tree needs `notmuch_thread_get_toplevel_messages`
   (`notmuch.h:1395`).
2. **Thread-derived messages are owned by the thread.** `notmuch.h:1637` — "The
   returned list will be destroyed when the thread is destroyed." Therefore the
   whole walk must finish while the `NmThread` is alive, and thread-derived
   `notmuch_message_t*` must **NOT** be wrapped in `NmMessage`: that alias calls
   `notmuch_message_destroy` and would double-free. Use raw pointers inside the
   walk, scoped by the live `NmThread`.
3. **`notmuch_messages_valid` accepts NULL** and returns FALSE
   (`notmuch.h:1630-1632`), so a leaf message needs no NULL check before
   iterating its replies.
4. **`QTreeView` has the geometry calls `ThreadListView::paintEvent` needs:**
   `rowAt`, `rowViewportPosition`, `rowHeight`, `columnViewportPosition` all
   exist with table-compatible semantics. What does NOT exist is
   `QTableView::isRowSelected(int)`; use
   `selectionModel()->isSelected(index)` instead.
5. **`QItemSelectionModel::currentRowChanged` fires BEFORE the selection model
   updates** (CLAUDE.md, verified against Qt 6.11). Any decision depending on
   how many rows are selected belongs in `selectionChanged`. This plan preserves
   the existing split; do not "simplify" it.
6. **`selectAll()` emits no `currentRowChanged`.** Tests for multi-select must
   start from a row that is already current.

---

## File structure

| File | Responsibility | Action |
|---|---|---|
| `src/types.h` | Add `MessageNode` value struct | Modify |
| `src/notmuchworker.h/.cpp` | `loadThreadTree`, `loadMessage`, `threadTreeLoaded`, `messageLoaded` | Modify |
| `src/threadlistmodel.h/.cpp` | `QAbstractItemModel` tree; node storage; `nodeAt`, `isMessageRow`, `scopeFor` | Modify |
| `src/threadlistview.h/.cpp` | `QTreeView` base; strip painted for root rows only | Modify |
| `src/mainwindow.cpp` | Tree wiring, per-row-kind selection, scope in status bar | Modify |
| `tests/test_threadlistmodel.cpp` | Tree shape, roles, scope resolution | Create |
| `tests/test_notmuchworker.cpp` | `loadThreadTree` structure against the fixture DB | Modify |
| `tests/test_mainwindow.cpp` | Selection scope and status-bar text | Modify |
| `tests/CMakeLists.txt` | Register `threadlistmodel` test | Modify |

**Ordering is mandatory.** Task 1 (data) → 2 (worker) → 3-5 (model) → 6-7 (view) → 8-10 (MainWindow). Each later task depends on the types the earlier one defines.

---

### Task 1: The `MessageNode` value struct

The tree needs per-message facts the current `MessageRef` does not carry: sender,
subject, date, and depth. Depth is stored rather than derived so the model never
has to walk upward to paint a row.

**Files:**
- Modify: `src/types.h` (after `MessageRef`, around line 64)

- [ ] **Step 1: Write the failing test**

Create `tests/test_threadlistmodel.cpp` with the license header used by every
other file in `tests/`, then:

```cpp
#include "types.h"
#include <QTest>

class TestThreadListModel : public QObject
{
    Q_OBJECT
private slots:
    void messageNodeHoldsDisplayFacts();
};

void TestThreadListModel::messageNodeHoldsDisplayFacts()
{
    MessageNode node;
    node.messageId = QStringLiteral("id@example.org");
    node.from = QStringLiteral("A Sender <sender@example.org>");
    node.subject = QStringLiteral("Re: a subject");
    node.date = QDateTime::fromSecsSinceEpoch(1000);
    node.depth = 2;
    node.tags = QStringList{ QStringLiteral("unread") };

    QCOMPARE(node.depth, 2);
    QVERIFY(node.isUnread());
    QCOMPARE(node.from, QStringLiteral("A Sender <sender@example.org>"));
}

QTEST_MAIN(TestThreadListModel)
#include "test_threadlistmodel.moc"
```

Add to `tests/CMakeLists.txt`, beside the existing calls:

```cmake
add_qtmaildir_test(threadlistmodel)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/tests/test_threadlistmodel`
Expected: compile error, `'MessageNode' was not declared in this scope`.

- [ ] **Step 3: Write minimal implementation**

In `src/types.h`, after the `MessageRef` struct:

```cpp
/// One message as a row in the thread list.
///
/// Separate from MessageRef, which exists for RENDERING a thread and carries
/// only what the message pane needs. A row has to be drawn without opening the
/// message, so the display facts live here.
struct MessageNode
{
    QString messageId;
    QString from;
    QString subject;
    QDateTime date;
    QStringList tags;
    QString filePath;

    /// Reply depth within the thread. 0 is the thread's first message, which
    /// occupies the ROOT row rather than a child row: the user's model is
    /// "N replies", so a thread of 7 shows 1 root and 6 descendants.
    int depth = 0;

    bool isUnread() const { return tags.contains(QStringLiteral("unread")); }
    bool isFlagged() const { return tags.contains(QStringLiteral("flagged")); }
    bool hasAttachment() const
    {
        return tags.contains(QStringLiteral("attachment"));
    }
};
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/tests/test_threadlistmodel`
Expected: `Totals: 1 passed, 0 failed, 0 skipped`.

- [ ] **Step 5: Commit**

```bash
git add src/types.h tests/test_threadlistmodel.cpp tests/CMakeLists.txt
git commit -S -m "feat(types): add MessageNode for message rows in the thread list"
```

---

### Task 2: Worker emits the reply tree

**Files:**
- Modify: `src/notmuchworker.h` (signal + slot declarations)
- Modify: `src/notmuchworker.cpp` (new method beside `loadThread`)
- Test: `tests/test_notmuchworker.cpp`

- [ ] **Step 1: Write the failing test**

The fixture DB builder in `tests/test_notmuchworker.cpp` already generates a
Maildir. Add a message with an `In-Reply-To` header so a reply actually exists,
then assert the tree shape. Append this test, following the file's existing
fixture helpers:

```cpp
void TestNotmuchWorker::loadThreadTreeReportsReplyDepth()
{
    // A root and one reply to it. In-Reply-To is what notmuch threads on;
    // without it the two messages are separate threads and the test would
    // assert nothing.
    writeMessage(QStringLiteral("root@example.org"), {},
                 QStringLiteral("A subject"));
    writeMessage(QStringLiteral("reply@example.org"),
                 QStringLiteral("root@example.org"),
                 QStringLiteral("Re: A subject"));
    reindex();

    QVector<MessageNode> nodes;
    QSignalSpy spy(m_worker, &NotmuchWorker::threadTreeLoaded);
    QMetaObject::invokeMethod(m_worker, "loadThreadTree",
                              Qt::DirectConnection,
                              Q_ARG(QString, threadIdOf(QStringLiteral("root@example.org"))),
                              Q_ARG(QString, QString()),
                              Q_ARG(quint64, 1));

    QCOMPARE(spy.count(), 1);
    nodes = spy.at(0).at(0).value<QVector<MessageNode>>();

    QCOMPARE(nodes.size(), 2);
    QCOMPARE(nodes.at(0).depth, 0);
    QCOMPARE(nodes.at(0).messageId, QStringLiteral("root@example.org"));
    QCOMPARE(nodes.at(1).depth, 1);
    QCOMPARE(nodes.at(1).messageId, QStringLiteral("reply@example.org"));
    QVERIFY(!nodes.at(1).from.isEmpty());
}
```

If `writeMessage` in that file does not yet take an `In-Reply-To` argument, extend
it: add a second `const QString &inReplyTo` parameter and, when non-empty, write
`In-Reply-To: <%1>` into the generated headers.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/tests/test_notmuchworker loadThreadTreeReportsReplyDepth`
Expected: compile error, no member named `threadTreeLoaded`.

- [ ] **Step 3: Write minimal implementation**

In `src/notmuchworker.h`, beside `loadThread` and `threadLoaded`:

```cpp
public slots:
    /// Loads a thread as a reply TREE, for the message rows in the list.
    ///
    /// Separate from loadThread rather than replacing it: loadThread walks
    /// notmuch_query_search_messages, and a message from that walk returns NULL
    /// from notmuch_message_get_replies (notmuch.h:1617-1628), so it cannot
    /// produce depth at all. The pane still wants the flat list; the list wants
    /// the tree.
    void loadThreadTree(const QString &threadId, const QString &matchQuery,
                        quint64 generation);

signals:
    void threadTreeLoaded(const QVector<MessageNode> &nodes, quint64 generation);
```

In `src/notmuchworker.cpp`, add a file-local recursive helper above
`loadThreadTree`, then the method:

```cpp
namespace {

/// Walks a thread's reply structure depth-first, appending each message with
/// its depth.
///
/// Takes RAW notmuch_message_t*, deliberately. Messages reached through a
/// thread are owned by that thread and freed with it (notmuch.h:1637); wrapping
/// them in NmMessage would call notmuch_message_destroy on memory the thread
/// also frees. The NmThread in the caller is what keeps every pointer here
/// alive, so this must not outlive it.
/// Takes no match-set argument, unlike loadThread. A row is drawn for every
/// message in the thread regardless of the query: the list is where the user
/// goes to SEE the thread's shape, and hiding replies that did not match would
/// make the "N replies" count disagree with the rows beneath it.
void walkReplies(notmuch_messages_t *messages, int depth,
                 QVector<MessageNode> *out)
{
    for (; notmuch_messages_valid(messages);
           notmuch_messages_move_to_next(messages)) {

        notmuch_message_t *message = notmuch_messages_get(messages);
        if (!message)
            continue;

        MessageNode node;
        node.messageId =
            QString::fromUtf8(notmuch_message_get_message_id(message));
        node.filePath =
            QString::fromUtf8(notmuch_message_get_filename(message));
        node.from = QString::fromUtf8(
            notmuch_message_get_header(message, "from"));
        node.subject = QString::fromUtf8(
            notmuch_message_get_header(message, "subject"));
        node.date = QDateTime::fromSecsSinceEpoch(
            notmuch_message_get_date(message));
        node.tags = tagsOf(message);
        node.depth = depth;
        out->append(node);

        // NULL is a legitimate "no replies" here: notmuch_messages_valid
        // accepts it and returns FALSE (notmuch.h:1630), so a leaf needs no
        // guard of its own.
        walkReplies(notmuch_message_get_replies(message), depth + 1, out);
    }
}

}  // namespace

void NotmuchWorker::loadThreadTree(const QString &threadId,
                                   const QString &matchQuery,
                                   quint64 generation)
{
    Q_UNUSED(matchQuery);

    if (!openReadOnly())
        return;

    const QString query = QStringLiteral("thread:%1").arg(threadId);
    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(
            QStringLiteral("Cannot load thread %1").arg(threadId));
        return;
    }

    notmuch_threads_t *rawThreads = nullptr;
    if (notmuch_query_search_threads(nmQuery.get(), &rawThreads)
            != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(
            QStringLiteral("Cannot search thread %1").arg(threadId));
        return;
    }
    NmThreads threads(rawThreads);

    QVector<MessageNode> nodes;
    if (notmuch_threads_valid(threads.get())) {
        // Held for the whole walk: every message pointer below belongs to this
        // thread and dies with it.
        NmThread thread(notmuch_threads_get(threads.get()));
        if (thread) {
            walkReplies(notmuch_thread_get_toplevel_messages(thread.get()), 0,
                        &nodes);
        }
    }

    emit threadTreeLoaded(nodes, generation);
}
```

Register the type for queued connections. In `main.cpp` beside the existing
`qRegisterMetaType` calls (and in the test's `initTestCase` if it registers its
own):

```cpp
qRegisterMetaType<QVector<MessageNode>>("QVector<MessageNode>");
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/tests/test_notmuchworker loadThreadTreeReportsReplyDepth`
Expected: PASS.

Then the whole suite, since `types.h` is included widely:
Run: `ctest --test-dir build --output-on-failure`
Expected: all 15 binaries pass.

- [ ] **Step 5: Mutation check**

Change `depth + 1` to `depth` in `walkReplies`, rebuild, rerun. The test MUST
fail on `QCOMPARE(nodes.at(1).depth, 1)`. Restore it. A depth test that passes
flat is testing nothing.

- [ ] **Step 6: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp src/main.cpp tests/test_notmuchworker.cpp
git commit -S -m "feat(worker): load a thread as a reply tree with per-message depth"
```

---

### Task 3: Model becomes a tree, roots only

Convert the base class and get thread roots rendering exactly as before. No
children yet: this task's whole purpose is proving the table-to-tree conversion
did not change what the user sees.

**Files:**
- Modify: `src/threadlistmodel.h`, `src/threadlistmodel.cpp`
- Test: `tests/test_threadlistmodel.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestThreadListModel::rootRowsSurviveTheTreeConversion()
{
    ThreadListModel model;
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.subject = QStringLiteral("A subject");
    summary.authors = QStringLiteral("A Sender");
    summary.date = QDateTime::fromSecsSinceEpoch(1000);
    summary.totalCount = 3;
    model.appendBatch({ summary });

    // A tree model must report roots under an INVALID parent, and a root row
    // must have no children until one is asked for.
    QCOMPARE(model.rowCount(QModelIndex()), 1);
    QCOMPARE(model.columnCount(QModelIndex()), ThreadListModel::ColumnCount);

    const QModelIndex root = model.index(0, ThreadListModel::SubjectColumn,
                                         QModelIndex());
    QVERIFY(root.isValid());
    QVERIFY(!model.parent(root).isValid());
    QCOMPARE(model.data(root, ThreadListModel::ThreadIdRole).toString(),
             QStringLiteral("t1"));
    QCOMPARE(model.rowCount(root), 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/tests/test_threadlistmodel rootRowsSurviveTheTreeConversion`
Expected: FAIL — `QAbstractTableModel` has no usable `parent(QModelIndex)` and
`index(row, col, parent)` ignores the parent.

- [ ] **Step 3: Write minimal implementation**

In `src/threadlistmodel.h`, change the base class and add the tree overrides.
Replace `#include <QAbstractTableModel>` with `#include <QAbstractItemModel>`,
and the class declaration:

```cpp
class ThreadListModel : public QAbstractItemModel
```

Add to the public section, beside the existing overrides:

```cpp
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
```

Add to the `Role` enum, after `PillColoursRole`:

```cpp
    /// True when the row is a MESSAGE row rather than a thread root. Drives
    /// both the action scope and whether the view paints a tag strip under it.
    IsMessageRole,

    /// The message id behind a message row. Empty on a thread root.
    MessageIdRole,
```

Add the node storage to the private section, replacing the bare
`QVector<ThreadSummary> m_threads;`:

```cpp
private:
    /// One thread root and the message rows expanded under it.
    ///
    /// Children live beside the summary rather than in a separate map so that a
    /// row and its expansion are inserted, cleared and destroyed together. The
    /// model is rebuilt wholesale on every query, so nothing here has to
    /// survive a reset.
    struct ThreadNode
    {
        ThreadSummary summary;
        QVector<MessageNode> children;  ///< Empty until expanded.
        bool loaded = false;            ///< Distinguishes "no replies" from
                                        ///< "not asked yet".
    };

    QVector<ThreadNode> m_threads;
    const TagColors *m_tagColors = nullptr;
```

In `src/threadlistmodel.cpp`, implement the two new methods and adjust
`rowCount`. The internal id encodes which thread a child belongs to; roots carry
`-1`:

```cpp
QModelIndex ThreadListModel::index(int row, int column,
                                   const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    // A root row. -1 as the internal id marks it, so parent() can tell the two
    // kinds apart without storing a node pointer per index.
    if (!parent.isValid())
        return createIndex(row, column, static_cast<quintptr>(-1));

    // A child row: the internal id is its parent's row.
    return createIndex(row, column, static_cast<quintptr>(parent.row()));
}

QModelIndex ThreadListModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};

    const quintptr id = child.internalId();
    if (id == static_cast<quintptr>(-1))
        return {};

    return createIndex(static_cast<int>(id), 0, static_cast<quintptr>(-1));
}

int ThreadListModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_threads.size();

    // Only a root row has children, and only in its first column: a table takes
    // one set of children per row, and offering them under every column makes
    // the view draw the expander in each.
    if (parent.parent().isValid() || parent.column() != 0)
        return 0;

    if (parent.row() < 0 || parent.row() >= m_threads.size())
        return 0;

    return m_threads.at(parent.row()).children.size();
}
```

Update every existing `m_threads.at(row)` in `data()`, `threadAt()`,
`accountKeysForThread()` and `applyTagChange()` to `m_threads.at(row).summary`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/tests/test_threadlistmodel`
Expected: both tests PASS.

Run: `ctest --test-dir build --output-on-failure`
Expected: all pass. `test_threadlistview` and `test_mainwindow` exercise this
model; if either fails here the conversion changed root behaviour, which this
task exists to prevent.

- [ ] **Step 5: Commit**

```bash
git add src/threadlistmodel.h src/threadlistmodel.cpp tests/test_threadlistmodel.cpp
git commit -S -m "refactor(model): convert ThreadListModel to QAbstractItemModel"
```

---

### Task 4: Message rows as children

**Files:**
- Modify: `src/threadlistmodel.h`, `src/threadlistmodel.cpp`
- Test: `tests/test_threadlistmodel.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestThreadListModel::repliesBecomeChildRowsUnderTheirThread()
{
    ThreadListModel model;
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.subject = QStringLiteral("A subject");
    summary.totalCount = 3;
    model.appendBatch({ summary });

    // depth 0 is the thread's FIRST message and belongs on the root row, not
    // in the children: the user's model is "N replies".
    QVector<MessageNode> nodes;
    MessageNode first;
    first.messageId = QStringLiteral("m0@example.org");
    first.from = QStringLiteral("First Sender");
    first.depth = 0;
    nodes.append(first);

    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.from = QStringLiteral("A Replier");
    reply.depth = 1;
    nodes.append(reply);

    MessageNode nested;
    nested.messageId = QStringLiteral("m2@example.org");
    nested.from = QStringLiteral("Third Sender");
    nested.depth = 2;
    nodes.append(nested);

    model.setThreadMessages(QStringLiteral("t1"), nodes);

    const QModelIndex root = model.index(0, 0, QModelIndex());
    QCOMPARE(model.rowCount(root), 2);  // the two replies, not all three

    const QModelIndex child = model.index(0, ThreadListModel::SubjectColumn,
                                          root);
    QVERIFY(child.isValid());
    QCOMPARE(model.parent(child), model.index(0, 0, QModelIndex()));
    QVERIFY(model.data(child, ThreadListModel::IsMessageRole).toBool());
    QCOMPARE(model.data(child, ThreadListModel::MessageIdRole).toString(),
             QStringLiteral("m1@example.org"));

    // A root row is not a message row, and carries no message id.
    QVERIFY(!model.data(root, ThreadListModel::IsMessageRole).toBool());
    QVERIFY(model.data(root, ThreadListModel::MessageIdRole)
                .toString().isEmpty());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/tests/test_threadlistmodel repliesBecomeChildRowsUnderTheirThread`
Expected: compile error, `no member named 'setThreadMessages'`.

- [ ] **Step 3: Write minimal implementation**

In `src/threadlistmodel.h`, public section:

```cpp
    /// Fills in a thread's message rows once the worker has walked its tree.
    ///
    /// The depth-0 message is dropped: it is the thread's first message and the
    /// ROOT row already stands for it. Keeping it would show a thread of 7 as 1
    /// root and 7 children, contradicting the "6 replies" the row advertises.
    void setThreadMessages(const QString &threadId,
                           const QVector<MessageNode> &nodes);

    /// True when the index is a message row rather than a thread root.
    bool isMessageRow(const QModelIndex &index) const;

    /// The message row's node. Only call when isMessageRow() is true.
    MessageNode messageAt(const QModelIndex &index) const;
```

In `src/threadlistmodel.cpp`:

```cpp
void ThreadListModel::setThreadMessages(const QString &threadId,
                                        const QVector<MessageNode> &nodes)
{
    for (int row = 0; row < m_threads.size(); ++row) {
        if (m_threads.at(row).summary.threadId != threadId)
            continue;

        const QModelIndex parent = index(row, 0, QModelIndex());

        // Replace rather than append: a thread reloaded after a sync must not
        // end up with its replies listed twice.
        if (!m_threads.at(row).children.isEmpty()) {
            beginRemoveRows(parent, 0, m_threads.at(row).children.size() - 1);
            m_threads[row].children.clear();
            endRemoveRows();
        }

        QVector<MessageNode> children;
        children.reserve(nodes.size());
        for (const MessageNode &node : nodes) {
            if (node.depth > 0)
                children.append(node);
        }

        if (!children.isEmpty()) {
            beginInsertRows(parent, 0, children.size() - 1);
            m_threads[row].children = children;
            endInsertRows();
        }

        m_threads[row].loaded = true;
        return;
    }
}

bool ThreadListModel::isMessageRow(const QModelIndex &index) const
{
    return index.isValid() && index.parent().isValid();
}

MessageNode ThreadListModel::messageAt(const QModelIndex &index) const
{
    if (!isMessageRow(index))
        return {};

    const int threadRow = index.parent().row();
    if (threadRow < 0 || threadRow >= m_threads.size())
        return {};

    const QVector<MessageNode> &children = m_threads.at(threadRow).children;
    if (index.row() < 0 || index.row() >= children.size())
        return {};

    return children.at(index.row());
}
```

In `data()`, handle message rows before the existing thread-row logic:

```cpp
    if (isMessageRow(index)) {
        const MessageNode node = messageAt(index);

        switch (role) {
        case IsMessageRole:
            return true;
        case MessageIdRole:
            return node.messageId;
        case ThreadIdRole:
            // A message row still belongs to a thread, and callers that only
            // need the containing thread must not have to walk up themselves.
            return m_threads.at(index.parent().row()).summary.threadId;
        case PillTagsRole:
        case TagsRole:
            // No strip under a child row: the strip is a ROW-wide band and
            // nesting one under every reply turns the list into stripes.
            return QStringList();
        case Qt::DisplayRole:
            switch (index.column()) {
            case AuthorsColumn:
                return node.from;
            case SubjectColumn:
                return node.subject;
            case DateColumn:
                return node.date.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
            case AttachmentColumn:
                return node.hasAttachment() ? attachmentGlyph() : QString();
            case FlagColumn:
                return node.isFlagged() ? flagGlyph() : QString();
            default:
                return {};
            }
        case Qt::ForegroundRole:
            return node.isUnread() ? QVariant() : QVariant(readColour());
        default:
            return {};
        }
    }

    // Thread rows fall through to the existing logic below, which must answer
    // IsMessageRole and MessageIdRole rather than leaving them invalid: an
    // invalid QVariant converts to false and an empty string anyway, but a
    // caller reading a role the model never mentions is a latent bug.
```

Add to the thread-row switch in `data()`:

```cpp
    case IsMessageRole:
        return false;
    case MessageIdRole:
        return QString();
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/tests/test_threadlistmodel`
Expected: all three tests PASS.

- [ ] **Step 5: Mutation check**

Change `if (node.depth > 0)` to `if (node.depth >= 0)`. The test MUST fail on
`QCOMPARE(model.rowCount(root), 2)` with an actual of 3. Restore it.

- [ ] **Step 6: Commit**

```bash
git add src/threadlistmodel.h src/threadlistmodel.cpp tests/test_threadlistmodel.cpp
git commit -S -m "feat(model): expose a thread's replies as child rows"
```

---

### Task 5: Action scope resolution

The model owns the row-kind-to-scope mapping so no call site has to reinvent it.

**Files:**
- Modify: `src/threadlistmodel.h`, `src/threadlistmodel.cpp`
- Test: `tests/test_threadlistmodel.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestThreadListModel::scopeFollowsTheSelectedRowKind()
{
    ThreadListModel model;
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.totalCount = 3;
    model.appendBatch({ summary });

    QVector<MessageNode> nodes;
    MessageNode first;
    first.messageId = QStringLiteral("m0@example.org");
    first.depth = 0;
    nodes.append(first);
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.depth = 1;
    nodes.append(reply);
    model.setThreadMessages(QStringLiteral("t1"), nodes);

    const QModelIndex root = model.index(0, 0, QModelIndex());
    const QModelIndex child = model.index(0, 0, root);

    const ActionScope threadScope = model.scopeFor({ root });
    QCOMPARE(threadScope.threadIds, QStringList{ QStringLiteral("t1") });
    QVERIFY(threadScope.messageIds.isEmpty());
    QCOMPARE(threadScope.messageCount, 3);
    QVERIFY(threadScope.wholeThread);

    const ActionScope messageScope = model.scopeFor({ child });
    QVERIFY(messageScope.threadIds.isEmpty());
    QCOMPARE(messageScope.messageIds,
             QStringList{ QStringLiteral("m1@example.org") });
    QCOMPARE(messageScope.messageCount, 1);
    QVERIFY(!messageScope.wholeThread);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/tests/test_threadlistmodel scopeFollowsTheSelectedRowKind`
Expected: compile error, `'ActionScope' was not declared`.

- [ ] **Step 3: Write minimal implementation**

In `src/types.h`, after `MessageNode`:

```cpp
/// What an action is about to touch, resolved from the selection.
///
/// Exists because the thread list now holds two kinds of row and a keypress
/// alone no longer says which one it hit. Every action path takes one of these
/// rather than a bare list of thread ids, and the status bar reports it, which
/// is this project's answer to the ambiguity instead of a confirmation dialog.
struct ActionScope
{
    QStringList threadIds;   ///< Whole threads to act on.
    QStringList messageIds;  ///< Individual messages to act on.

    /// Messages the action will touch in total, for the status bar. A thread
    /// contributes all of its messages, a message row contributes one.
    int messageCount = 0;

    /// True when any whole thread is in scope. Drives the "(whole thread)"
    /// suffix in the status bar.
    bool wholeThread = false;

    bool isEmpty() const
    {
        return threadIds.isEmpty() && messageIds.isEmpty();
    }
};
```

In `src/threadlistmodel.h`, public section:

```cpp
    /// Resolves a selection into what an action should touch.
    ///
    /// Mixed selections are honoured as given: selecting a thread root and an
    /// unrelated reply acts on that whole thread and that one message. Nothing
    /// is escalated or narrowed silently, which is the whole point of the scope
    /// being visible.
    ActionScope scopeFor(const QModelIndexList &selection) const;
```

In `src/threadlistmodel.cpp`:

```cpp
ActionScope ThreadListModel::scopeFor(const QModelIndexList &selection) const
{
    ActionScope scope;

    for (const QModelIndex &index : selection) {
        if (isMessageRow(index)) {
            const MessageNode node = messageAt(index);
            if (node.messageId.isEmpty()
                || scope.messageIds.contains(node.messageId))
                continue;
            scope.messageIds.append(node.messageId);
            scope.messageCount += 1;
            continue;
        }

        if (index.row() < 0 || index.row() >= m_threads.size())
            continue;

        const ThreadSummary &summary = m_threads.at(index.row()).summary;
        if (scope.threadIds.contains(summary.threadId))
            continue;

        scope.threadIds.append(summary.threadId);
        // totalCount, not the loaded children: a thread that was never expanded
        // still has all of its messages, and reporting only what happens to be
        // on screen would understate what the action does.
        scope.messageCount += qMax(1, summary.totalCount);
        scope.wholeThread = true;
    }

    return scope;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/tests/test_threadlistmodel`
Expected: all four tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/types.h src/threadlistmodel.h src/threadlistmodel.cpp tests/test_threadlistmodel.cpp
git commit -S -m "feat(model): resolve action scope from the selected row kind"
```

---

### Task 6: View becomes a QTreeView

**Files:**
- Modify: `src/threadlistview.h`, `src/threadlistview.cpp`
- Test: `tests/test_threadlistview.cpp`

- [ ] **Step 1: Write the failing test**

Per CLAUDE.md, a rendering probe must first prove it can see what it expects to
find. This test asserts geometry, not pixels:

```cpp
void TestThreadListView::childRowsAreIndentedUnderTheirThread()
{
    ThreadListModel model;
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.subject = QStringLiteral("A subject");
    summary.totalCount = 2;
    model.appendBatch({ summary });

    QVector<MessageNode> nodes;
    MessageNode first;
    first.messageId = QStringLiteral("m0@example.org");
    first.depth = 0;
    nodes.append(first);
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.from = QStringLiteral("A Replier");
    reply.depth = 1;
    nodes.append(reply);
    model.setThreadMessages(QStringLiteral("t1"), nodes);

    ThreadListView view;
    view.setModel(&model);
    view.resize(800, 400);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const QModelIndex root = model.index(0, 0, QModelIndex());
    view.expand(root);

    // Guard: the probe must be able to see both rows before it can claim
    // anything about them.
    QVERIFY(view.visualRect(root).height() > 0);
    const QModelIndex child = model.index(0, 0, root);
    QVERIFY(child.isValid());
    QVERIFY(view.visualRect(child).height() > 0);

    // The actual claim: a reply is indented relative to its thread.
    QVERIFY(view.visualRect(child).left() > view.visualRect(root).left());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/tests/test_threadlistview childRowsAreIndentedUnderTheirThread`
Expected: compile error, `'class ThreadListView' has no member named 'expand'`.

- [ ] **Step 3: Write minimal implementation**

In `src/threadlistview.h`, change the base class from `QTableView` to
`QTreeView` and update the include:

```cpp
#include <QTreeView>

/// The thread list.
///
/// A QTreeView rather than a QTableView since item 20: a thread's replies are
/// child rows, and a table cannot indent or expand. The tag strip below is why
/// this class exists at all, and it is written against geometry calls that both
/// classes share (rowAt, rowViewportPosition, columnViewportPosition), so the
/// port did not have to rewrite it.
class ThreadListView : public QTreeView
{
```

In `src/threadlistview.cpp`, three changes to `paintEvent`:

```cpp
    QTreeView::paintEvent(event);  // was QTableView::paintEvent
```

Replace `rowHeight(row)`, which a `QTreeView` does not take by row number, and
the row walk generally. Walk indexes rather than row numbers:

```cpp
    // A QTreeView numbers rows per parent, so a flat "row 0..N" walk would
    // revisit row 0 under every expanded thread. Walk by index from the top of
    // the viewport instead.
    QModelIndex index = indexAt(QPoint(0, 0));

    while (index.isValid()) {
        const QRect rect = visualRect(index);
        if (rect.top() > viewport()->height())
            break;

        // No strip under a message row. The strip is a row-wide band carrying
        // the THREAD's tags; drawing one under each reply would stripe the
        // list and repeat the same tags down the whole expansion.
        if (index.parent().isValid()) {
            index = indexBelow(index);
            continue;
        }

        const int rowTop = rect.top();
        const int height = rect.height();
        if (height <= 0) {
            index = indexBelow(index);
            continue;
        }

        // ... existing band and chip painting, unchanged, using rowTop/height
        // and model()->index(index.row(), SubjectColumn, index.parent())
        // in place of the old model()->index(row, SubjectColumn).

        index = indexBelow(index);
    }
```

Replace the selection check, since `QTableView::isRowSelected(int)` does not
exist on `QTreeView`:

```cpp
        else if (selectionModel() && selectionModel()->isSelected(index))
```

And the alternating-colour check, which used the row number:

```cpp
        // Alternation follows VISUAL position in a tree, not the row number:
        // row 0 under three different threads is three different stripes.
        else if (alternatingRowColors() && (visualRow % 2))
```

where `visualRow` is a counter incremented once per painted row in the walk.

In `src/mainwindow.cpp`, wherever the view is configured, add:

```cpp
    // The expander column is the subject, not the leading marker columns: an
    // expander drawn in the narrow attachment column has nowhere to go and
    // pushes the glyph out of view.
    m_threadView->setTreePosition(ThreadListModel::SubjectColumn);
```

(`setTreePosition(int logicalIndex)`, confirmed at
`/usr/include/qt6/QtWidgets/qtreeview.h:97`.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/tests/test_threadlistview`
Expected: all tests in the file PASS, including the pre-existing strip tests.

- [ ] **Step 5: Verify the strip did not regress**

Run the existing strip rendering tests specifically. If `test_threadlistview`
has a tag-strip test, it must still pass unchanged: the port must not have moved
the band. If it fails, the walk rewrite changed geometry and that is the bug,
not the test.

- [ ] **Step 6: Commit**

```bash
git add src/threadlistview.h src/threadlistview.cpp src/mainwindow.cpp tests/test_threadlistview.cpp
git commit -S -m "refactor(view): make ThreadListView a QTreeView for message rows"
```

---

### Task 7: Expand loads the thread's messages

**Files:**
- Modify: `src/mainwindow.cpp`
- Test: manual, plus Task 8's automated coverage

- [ ] **Step 1: Wire the expansion**

In `MainWindow`'s view setup, beside the other view connections:

```cpp
    // Loading on expansion rather than with the query: walking the reply tree
    // of every thread in a 10k-thread result would cost far more than the query
    // itself, and almost none of it would ever be looked at.
    connect(m_threadView, &QTreeView::expanded,
            this, &MainWindow::onThreadExpanded);
```

Add the handler:

```cpp
void MainWindow::onThreadExpanded(const QModelIndex &index)
{
    if (!index.isValid() || m_model->isMessageRow(index))
        return;

    const QString threadId =
        m_model->data(index, ThreadListModel::ThreadIdRole).toString();
    if (threadId.isEmpty())
        return;

    QMetaObject::invokeMethod(m_worker, "loadThreadTree", Qt::QueuedConnection,
                              Q_ARG(QString, threadId),
                              Q_ARG(QString, m_lastQuery),
                              Q_ARG(quint64, m_generation));
}
```

Add the reply handler, connected to `threadTreeLoaded`:

```cpp
void MainWindow::onThreadTreeLoaded(const QVector<MessageNode> &nodes,
                                    quint64 generation)
{
    // Same generation guard every other worker reply carries: an expansion
    // whose query has since been replaced must not insert rows into the new
    // result.
    if (generation != m_generation || nodes.isEmpty())
        return;

    // Every node in one reply belongs to one thread, so the first one names it.
    const QString threadId = nodes.first().threadId;
    m_model->setThreadMessages(threadId, nodes);
}
```

This requires `MessageNode` to carry its thread id. Add to `src/types.h`:

```cpp
    QString threadId;  ///< The thread this message belongs to.
```

and set it in `walkReplies`:

```cpp
        node.threadId =
            QString::fromUtf8(notmuch_message_get_thread_id(message));
```

- [ ] **Step 2: Build and hand-test**

Run: `cmake --build build && ./build/qtmaildir`
Expected: a thread with replies shows an expander; clicking it reveals indented
reply rows within a moment.

- [ ] **Step 3: Commit**

```bash
git add src/types.h src/notmuchworker.cpp src/mainwindow.cpp src/mainwindow.h
git commit -S -m "feat(ui): load a thread's replies when its row is expanded"
```

---

### Task 8: Selecting a message row renders that message

**Files:**
- Modify: `src/notmuchworker.h/.cpp`, `src/mainwindow.cpp`
- Test: `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestMainWindow::selectingAMessageRowDoesNotBlankThePane()
{
    // Guard first: this test is meaningless if the row is not really selectable.
    // test_mainwindow has no worker (backlog item 36), so this asserts the UI
    // decision, not the render.
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    ThreadListModel *model = window.modelForTesting();
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.subject = QStringLiteral("A subject");
    summary.totalCount = 2;
    model->appendBatch({ summary });

    QVector<MessageNode> nodes;
    MessageNode first;
    first.messageId = QStringLiteral("m0@example.org");
    first.depth = 0;
    nodes.append(first);
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.depth = 1;
    nodes.append(reply);
    model->setThreadMessages(QStringLiteral("t1"), nodes);

    QTreeView *view = window.threadViewForTesting();
    const QModelIndex root = model->index(0, 0, QModelIndex());
    view->expand(root);

    const QModelIndex child = model->index(0, 0, root);
    QVERIFY(child.isValid());
    view->setCurrentIndex(child);

    // A single message row is a reading selection, so the status bar must not
    // report a multi-row count.
    QCOMPARE(window.statusTextForTesting(), QString());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/tests/test_mainwindow selectingAMessageRowDoesNotBlankThePane`
Expected: FAIL or compile error, depending on which testing accessors exist.
Add whatever accessor is missing following the pattern the file already uses.

- [ ] **Step 3: Write minimal implementation**

Add the worker slot and signal. In `src/notmuchworker.h`:

```cpp
public slots:
    /// Loads ONE message, for a message row selected in the list.
    void loadMessage(const QString &messageId, quint64 generation);

signals:
    void messageLoaded(const QVector<MessageRef> &messages, quint64 generation);
```

In `src/notmuchworker.cpp`:

```cpp
void NotmuchWorker::loadMessage(const QString &messageId, quint64 generation)
{
    if (!openReadOnly())
        return;

    // id: is an exact-match prefix, and the id is quoted because a message id
    // can contain characters notmuch's parser would otherwise read as syntax.
    const QString query = QStringLiteral("id:\"%1\"").arg(messageId);
    NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));
    if (!nmQuery) {
        emit errorOccurred(
            QStringLiteral("Cannot load message %1").arg(messageId));
        return;
    }

    notmuch_messages_t *rawMessages = nullptr;
    if (notmuch_query_search_messages(nmQuery.get(), &rawMessages)
            != NOTMUCH_STATUS_SUCCESS) {
        emit errorOccurred(
            QStringLiteral("Cannot search message %1").arg(messageId));
        return;
    }
    NmMessages messages(rawMessages);

    QVector<MessageRef> result;
    if (notmuch_messages_valid(messages.get())) {
        NmMessage message(notmuch_messages_get(messages.get()));
        if (message) {
            MessageRef ref;
            ref.messageId = QString::fromUtf8(
                notmuch_message_get_message_id(message.get()));
            ref.filePath = QString::fromUtf8(
                notmuch_message_get_filename(message.get()));
            ref.tags = tagsOf(message.get());
            ref.matched = true;  // asked for by id: it is what the user picked
            result.append(ref);
        }
    }

    emit messageLoaded(result, generation);
}
```

In `MainWindow::onThreadSelected`, branch on the row kind before the existing
thread logic:

```cpp
    if (m_model->isMessageRow(current)) {
        const MessageNode node = m_model->messageAt(current);
        if (node.messageId.isEmpty())
            return;

        // No mark-read timer for a message row in this pass. Marking one
        // message of a thread read is a per-message tag write, which the
        // pending-edit map does not model yet; doing it here would count wrong.
        m_currentThreadId.clear();
        m_currentMessageId = node.messageId;
        m_messageView->setTags(node.tags);
        QMetaObject::invokeMethod(m_worker, "loadMessage",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, node.messageId),
                                  Q_ARG(quint64, m_generation));
        return;
    }
```

Connect `messageLoaded` to a handler that reuses the existing render path:

```cpp
void MainWindow::onMessageLoaded(const QVector<MessageRef> &messages,
                                 quint64 generation)
{
    // The same two guards onThreadLoaded carries: a stale generation, and a
    // reply that lands after the selection grew past one row.
    if (generation != m_generation || messages.isEmpty())
        return;
    if (m_threadView->selectionModel()->selectedRows().size() > 1)
        return;

    onThreadLoaded(messages, generation);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/tests/test_mainwindow`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp src/mainwindow.h src/mainwindow.cpp tests/test_mainwindow.cpp
git commit -S -m "feat(ui): render a single message when its row is selected"
```

---

### Task 9: Actions honour the selection scope

**Files:**
- Modify: `src/mainwindow.cpp`
- Test: `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestMainWindow::statusBarNamesTheScopeAfterAnAction()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    ThreadListModel *model = window.modelForTesting();
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.subject = QStringLiteral("A subject");
    summary.totalCount = 7;
    model->appendBatch({ summary });

    QTreeView *view = window.threadViewForTesting();
    const QModelIndex root = model->index(0, 0, QModelIndex());
    view->setCurrentIndex(root);
    view->selectionModel()->select(
        root, QItemSelectionModel::Select | QItemSelectionModel::Rows);

    // Selecting a thread root must say how many messages it stands for, so the
    // scope is visible BEFORE the action rather than only after it.
    QVERIFY(window.statusTextForTesting().contains(QStringLiteral("7")));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build && ./build/tests/test_mainwindow statusBarNamesTheScopeAfterAnAction`
Expected: FAIL — the status text is empty for a single-row selection today.

- [ ] **Step 3: Write minimal implementation**

In `MainWindow::onSelectionChanged`, replace the count message with a
scope-aware one:

```cpp
    const QModelIndexList selection =
        m_threadView->selectionModel()->selectedRows();
    const ActionScope scope = m_model->scopeFor(selection);

    // Stated for a single row too, not only for a multi-row selection. With two
    // kinds of row selectable, "what will this act on" is ambiguous at ONE row,
    // which is exactly where it was never in doubt before. This is the project's
    // answer to that ambiguity: no confirmation dialog, per CLAUDE.md, but the
    // scope is never a surprise either.
    if (scope.wholeThread && scope.threadIds.size() == 1
        && scope.messageIds.isEmpty()) {
        m_selectionMessage = tr("1 thread selected (%n message(s))", "",
                                scope.messageCount);
    } else if (!scope.threadIds.isEmpty() && scope.messageIds.isEmpty()) {
        m_selectionMessage = tr("%n thread(s) selected", "",
                                scope.threadIds.size());
    } else if (scope.threadIds.isEmpty() && scope.messageIds.size() == 1) {
        m_selectionMessage.clear();  // reading one message is not a bulk action
    } else {
        m_selectionMessage = tr("%n message(s) selected", "",
                                scope.messageIds.size());
    }
```

Route the action paths through the scope. Where `applyTagsToThreads` is invoked
today, pick the call by scope:

```cpp
void MainWindow::applyTagsToSelection(const QStringList &added,
                                      const QStringList &removed,
                                      const QString &description)
{
    const ActionScope scope =
        m_model->scopeFor(m_threadView->selectionModel()->selectedRows());
    if (scope.isEmpty())
        return;

    if (!scope.threadIds.isEmpty()) {
        QMetaObject::invokeMethod(m_worker, "applyTagsToThreads",
                                  Qt::QueuedConnection,
                                  Q_ARG(QStringList, scope.threadIds),
                                  Q_ARG(QStringList, added),
                                  Q_ARG(QStringList, removed));
    }

    if (!scope.messageIds.isEmpty()) {
        // applyTags already takes message ids: a message row needs no new
        // worker entry point, only the ids the scope resolved.
        QMetaObject::invokeMethod(m_worker, "applyTags", Qt::QueuedConnection,
                                  Q_ARG(QStringList, scope.messageIds),
                                  Q_ARG(QStringList, added),
                                  Q_ARG(QStringList, removed));
    }

    // The scope is named again after the fact, since the selection may be gone
    // by the time the user reads it.
    const QString message = scope.wholeThread
        ? tr("%1: %n message(s) (whole thread)", "", scope.messageCount)
              .arg(description)
        : tr("%1: %n message(s)", "", scope.messageCount).arg(description);
    showTransientStatus(message);
}
```

(`showTransientStatus(const QString &)`, confirmed at `src/mainwindow.h:176`.
Do not introduce a second helper.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/tests/test_mainwindow`
Expected: PASS. Existing selection tests may need their expected strings
updated, since a single-row selection now produces a message where it produced
none; that is the intended change, not a regression.

- [ ] **Step 5: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp tests/test_mainwindow.cpp
git commit -S -m "feat(ui): scope actions to the selected row kind and name it"
```

---

### Task 10: Full suite, hand test, docs

- [ ] **Step 1: Run everything**

Run: `ctest --test-dir build --output-on-failure`
Expected: all 16 binaries pass (15 pre-existing plus `test_threadlistmodel`).

- [ ] **Step 2: Hand test against real mail**

Run: `./build/qtmaildir`

Check each, since none is covered by a test that can reach the worker:
- A thread with replies shows an expander; expanding reveals indented rows.
- A nested reply indents further than its parent.
- Clicking a reply renders that message alone.
- Clicking the thread root renders the whole thread as before.
- The tag strip appears under thread rows and NOT under reply rows.
- Selecting a thread root reports "1 thread selected (N messages)".
- Deleting from a reply row reports 1 message; from a root, N with "(whole thread)".
- Ctrl+Z undoes either.
- Keyboard navigation reaches child rows with Left/Right collapsing/expanding.

- [ ] **Step 3: Update the docs**

In `CLAUDE.md`, the architecture diagram lists `ThreadListView ── ThreadListModel`
and the paragraph beginning "**`ThreadListView` exists because a delegate cannot
paint outside its column.**" states the view is a table. Update both: the class
is now a `QTreeView`, the model a `QAbstractItemModel`, and the strip is painted
for root rows only. Keep the explanation of WHY the class exists, which has not
changed.

In `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md`, mark item 20
**done** in the status table.

Add a `CHANGELOG.md` entry under Unreleased:

```markdown
### Added
- Threads expand in the list to show their replies as indented rows, and
  selecting a reply opens that single message.
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md CHANGELOG.md docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md
git commit -S -m "docs: record message rows in the thread list"
```

---

## Deferred, deliberately

Recorded so the next session does not mistake these for oversights.

- **Reading messages one after another within a thread** without returning to
  the list. The user named this as wanting it and explicitly deferred it. It is
  the natural follow-up item once these rows exist.
- **Marking a single message read on selection.** The mark-read timer stays
  thread-scoped in Task 8: a per-message read tag is a write the pending-edit
  map does not model, and item 28 is the record of what happens when that count
  goes wrong.
- **Indent capping for deep chains.** The spec accepts that deep reply chains
  indent off-screen. If it becomes a real problem, cap the visual indent in the
  view rather than flattening the model.
- **Sorting and filtering over the tree** (items 39 and 40). Both want a proxy
  model, and a proxy over a tree is materially harder than over a table. Neither
  is in this plan.
