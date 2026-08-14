# Exclude from search Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a third right-click search operation, "Exclude from search", which narrows the current query by everything that is not the value under the cursor.

**Architecture:** `SearchTerm` gains an `exclude()` beside `extend()` and a `SearchMode` enum. The `bool extend` carried from the two menu surfaces to `MainWindow` widens to that enum across four signatures. `MainWindow` pushes a `hasQuery` bool down into `MessageView` so both menus can grey the new entry when there is nothing to exclude from.

**Tech Stack:** C++17, Qt 6.11, QtTest. Build with CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-08-14-exclude-from-search-design.md`. Backlog item 86.

---

## Before you start

Build and test commands, from the repo root:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

**Never run a test binary without `QT_QPA_PLATFORM=offscreen`**, and never
launch `./build/src/qtmaildir`. `tests/CMakeLists.txt` sets that variable for
ctest only, so a binary invoked directly inherits the desktop's Wayland setting
and throws real windows onto the user's screen. Running the application is a
hand test and belongs to the user.

Commits are GPG-signed (`git commit -S`). Work directly on `master`.

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/searchterm.h` | Query grammar declarations, `SearchMode` | Modify: add enum + `exclude()` |
| `src/searchterm.cpp` | Query grammar | Modify: add `exclude()` |
| `tests/test_searchterm.cpp` | Grammar tests, no widgets | Modify: 3 new cases |
| `src/messageview.h/.cpp` | Message pane, header + body menus | Modify: signal type, `setHasQuery`, third entry |
| `src/messagedetailsdialog.h/.cpp` | Per-row detail menus | Modify: signal type, ctor arg, third entry |
| `tests/test_messagedetailsdialog.cpp` | Dialog tests | Modify: update calls, add guard test |
| `src/mainwindow.h/.cpp` | Owns the query bar, runs the query | Modify: mode switch, push `hasQuery` |

Task order matters: the grammar first, then the enum that every signature
depends on, then the two surfaces, then the guard.

---

### Task 1: `SearchTerm::exclude()`

The grammar, with no enum and no UI yet. Pure functions, no widget.

**Files:**
- Modify: `src/searchterm.h` (after the `extend()` declaration, ~line 77)
- Modify: `src/searchterm.cpp` (after `extend()`, ~line 91)
- Test: `tests/test_searchterm.cpp`

- [ ] **Step 1: Write the failing tests**

Add three slot declarations to the `private slots:` block in
`tests/test_searchterm.cpp`, after `extendOntoAnEmptyQueryIsAReplace();`:

```cpp
    void excludeParenthesisesBothSides();
    void excludeFromAnEmptyQueryIsEmpty();
    void excludeWithNothingToExcludeLeavesTheQuery();
```

Add the three implementations at the end of the file, before the
`QTEST_MAIN` / `#include "test_searchterm.moc"` lines:

```cpp
void TestSearchTerm::excludeParenthesisesBothSides()
{
    // The query bar can hold a hand-written disjunction. Unparenthesised,
    // `a or b AND NOT c` binds as `a or (b AND NOT c)`: the exclusion covers
    // only the second term and every message matching `a` stays on screen,
    // including the ones the user asked to be rid of. notmuch reports no
    // error for either form, so this assertion is the only thing that fails.
    QCOMPARE(SearchTerm::exclude(QStringLiteral("tag:inbox or tag:flagged"),
                                 QStringLiteral("from:\"someone\"")),
             QStringLiteral("(tag:inbox or tag:flagged) AND NOT "
                            "(from:\"someone\")"));
}

void TestSearchTerm::excludeFromAnEmptyQueryIsEmpty()
{
    // Deliberately NOT extend()'s behaviour. extend() returns the addition
    // alone, because narrowing nothing by x sensibly means x. Excluding from
    // nothing would mean the whole Maildir minus one value, which is a
    // legitimate query and an implausible thing to have meant by right
    // clicking a value in a fresh window. The UI greys the entry; this is the
    // second layer, against a caller that forgets the guard.
    QCOMPARE(SearchTerm::exclude(QString(), QStringLiteral("tag:inbox")),
             QString());
    QCOMPARE(SearchTerm::exclude(QStringLiteral("   "),
                                 QStringLiteral("tag:inbox")),
             QString());
}

void TestSearchTerm::excludeWithNothingToExcludeLeavesTheQuery()
{
    QCOMPARE(SearchTerm::exclude(QStringLiteral("tag:inbox"), QString()),
             QStringLiteral("tag:inbox"));
    QCOMPARE(SearchTerm::exclude(QString(), QString()), QString());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_searchterm
```

Expected: a COMPILE failure, `'exclude' is not a member of 'SearchTerm'`. That
is the correct failure at this stage.

- [ ] **Step 3: Declare it**

In `src/searchterm.h`, immediately after the `extend()` declaration:

```cpp
/// Narrows `existing` by everything that is NOT `addition`, as
/// `(existing) AND NOT (addition)`.
///
/// **Both sides are parenthesised, for the same load-bearing reason as
/// extend().** The query bar may hold a hand-written disjunction, and
/// `a or b AND NOT c` binds as `a or (b AND NOT c)`: the exclusion would
/// cover only the second term, leaving on screen exactly the mail the user
/// asked to be rid of, with no error reported anywhere.
///
/// **An empty `existing` yields an EMPTY STRING, unlike extend().** Excluding
/// from nothing would mean the entire Maildir minus one value: a legitimate
/// query, and an implausible thing to have meant by right-clicking a value in
/// a fresh window. The menus grey the entry out when the query bar is empty;
/// this is the second layer, against a caller that forgets the guard.
///
/// An empty `addition` leaves `existing` untouched.
QString exclude(const QString &existing, const QString &addition);
```

- [ ] **Step 4: Implement it**

In `src/searchterm.cpp`, immediately after `extend()`:

```cpp
QString exclude(const QString &existing, const QString &addition)
{
    const QString left = existing.trimmed();
    const QString right = addition.trimmed();

    if (right.isEmpty())
        return left;
    // NOT a replace, unlike extend(): see the header. An empty left would make
    // this "everything except", which no right-click asked for.
    if (left.isEmpty())
        return QString();

    return QStringLiteral("(%1) AND NOT (%2)").arg(left, right);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_searchterm
```

Expected: PASS, all cases.

- [ ] **Step 6: Commit**

```bash
git add src/searchterm.h src/searchterm.cpp tests/test_searchterm.cpp
git commit -S -m "feat(search): add SearchTerm::exclude

Parenthesises both sides, as extend() does: unparenthesised, a
disjunction in the query bar binds so the exclusion covers only its
last term and leaves the excluded mail on screen.

An empty existing query returns empty rather than the addition alone,
which is where this deliberately differs from extend(). Excluding from
nothing means the whole Maildir minus one value; the menus grey the
entry out and this is the second layer.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: The `SearchMode` enum

Introduce the type on its own, before anything uses it. No behaviour change.

**Files:**
- Modify: `src/searchterm.h`

- [ ] **Step 1: Add the enum**

In `src/searchterm.h`, inside `namespace SearchTerm`, immediately BEFORE the
`quote()` declaration (so it reads before the functions it selects between):

```cpp
/// Which of the three search operations a menu entry asked for.
///
/// Replaces the `bool extend` that carried two operations, which had no room
/// for a third. Plain enum class, NOT registered as a metatype: every
/// connection carrying it is direct, within the UI thread. The `Q_ENUM` trap
/// recorded in CLAUDE.md belongs to the queued signals into NotmuchWorker and
/// does not apply here.
enum class SearchMode {
    /// Replace the query bar outright.
    Replace,
    /// Narrow what is there, via extend().
    Narrow,
    /// Narrow by everything that is not this value, via exclude().
    Exclude,
};
```

- [ ] **Step 2: Verify it compiles**

```bash
cmake --build build
```

Expected: builds clean. Nothing uses the type yet.

- [ ] **Step 3: Commit**

```bash
git add src/searchterm.h
git commit -S -m "refactor(search): add SearchMode, the type replacing bool extend

Introduced alone, ahead of the four signatures that change to it, so
that change is a mechanical one commit later.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Widen the four signatures

Mechanical: `bool extend` becomes `SearchMode` end to end, with the two
existing operations mapping to `Replace` and `Narrow`. No new menu entry yet,
so behaviour is unchanged and the suite must stay green.

**Files:**
- Modify: `src/messageview.h` (signal, ~line 193), `src/messageview.cpp` (~lines 566-580, ~648)
- Modify: `src/messagedetailsdialog.h` (~lines 75, 80), `src/messagedetailsdialog.cpp` (~lines 88-96, 148)
- Modify: `src/mainwindow.h` (~line 381), `src/mainwindow.cpp` (~line 1654)
- Modify: `tests/test_messagedetailsdialog.cpp` (~lines 135-136, 177)

- [ ] **Step 1: Update the existing test calls first**

`tests/test_messagedetailsdialog.cpp` already calls `requestSearch` with a
bool, so it stops compiling the moment the signature changes. Update it now so
the build error you see next is only ever the one you intend.

At `tests/test_messagedetailsdialog.cpp:135-136`, replace:

```cpp
    dialog.requestSearch(*from, false);
    dialog.requestSearch(*from, true);
```

with:

```cpp
    dialog.requestSearch(*from, SearchTerm::SearchMode::Replace);
    dialog.requestSearch(*from, SearchTerm::SearchMode::Narrow);
```

At `tests/test_messagedetailsdialog.cpp:177`, replace:

```cpp
    dialog.requestSearch(*id, false);
```

with:

```cpp
    dialog.requestSearch(*id, SearchTerm::SearchMode::Replace);
```

Both spies assert on `searchRequested`'s arguments. Where a spy compares the
second argument against a bool, compare against the enum instead, e.g.
`QCOMPARE(spy.at(0).at(1).value<SearchTerm::SearchMode>(), SearchTerm::SearchMode::Replace);`.
Read the surrounding assertions and adjust each to match; do not guess at
their shape.

- [ ] **Step 2: Change `MessageView`**

In `src/messageview.h`, replace the `searchRequested` signal and the tail of
its doc comment:

```cpp
    /// `mode` says whether to replace the query bar, narrow it, or narrow it
    /// by everything that is not this value. The view does not know what the
    /// query bar holds and must not: the window owns that field and does the
    /// combining.
    ///
    /// Separate from queryRequested(), which carries a gate against a link in
    /// a rendered document driving the thread list. These menus are chrome
    /// built by our own code from values we extracted, so they need no gate,
    /// and widening the existing signal would change what that gate protects.
    void searchRequested(const QString &query, SearchTerm::SearchMode mode);
```

In `src/messageview.cpp:566`, `addSearchEntries` becomes:

```cpp
void MessageView::addSearchEntries(QMenu *menu, const QList<SearchOffer> &offers)
{
    for (const SearchOffer &entry : offers) {
        auto *sub = menu->addMenu(tr("Search for %1").arg(entry.label));

        auto *replace = sub->addAction(tr("Search for this"));
        connect(replace, &QAction::triggered, this, [this, entry]() {
            emit searchRequested(entry.query, SearchTerm::SearchMode::Replace);
        });

        auto *narrow = sub->addAction(tr("Add to search"));
        connect(narrow, &QAction::triggered, this, [this, entry]() {
            emit searchRequested(entry.query, SearchTerm::SearchMode::Narrow);
        });
    }
}
```

In `src/messageview.cpp:~648`, the dialog connection. Keep the comment above it
untouched, it documents the accept()-before-emit ordering:

```cpp
    connect(&dialog, &MessageDetailsDialog::searchRequested, this,
            [this, &dialog](const QString &query, SearchTerm::SearchMode mode) {
                dialog.accept();
                emit searchRequested(query, mode);
            });
```

- [ ] **Step 3: Change `MessageDetailsDialog`**

In `src/messagedetailsdialog.h`:

```cpp
    /// Emits searchRequested for `row`, or nothing when the row carries no
    /// searchable query. The menu entries call this; a test can too, without
    /// popping a menu.
    void requestSearch(const HeaderRow &row, SearchTerm::SearchMode mode);

signals:
    /// The user chose a search from a row's menu. `mode` says whether to
    /// replace the query, narrow it, or narrow it by everything that is not
    /// this value.
    void searchRequested(const QString &query, SearchTerm::SearchMode mode);
```

In `src/messagedetailsdialog.cpp:148`:

```cpp
void MessageDetailsDialog::requestSearch(const HeaderRow &row,
                                         SearchTerm::SearchMode mode)
{
    if (row.query.isEmpty())
        return;
    emit searchRequested(row.query, mode);
}
```

In `src/messagedetailsdialog.cpp:~88`, the per-row menu lambda:

```cpp
                        QMenu menu(this);
                        auto *replace = menu.addAction(tr("Search for this"));
                        connect(replace, &QAction::triggered, this,
                                [this, row]() {
                                    requestSearch(
                                        row, SearchTerm::SearchMode::Replace);
                                });
                        auto *narrow = menu.addAction(tr("Add to search"));
                        connect(narrow, &QAction::triggered, this,
                                [this, row]() {
                                    requestSearch(
                                        row, SearchTerm::SearchMode::Narrow);
                                });
                        menu.exec(value->mapToGlobal(pos));
```

- [ ] **Step 4: Change `MainWindow`**

In `src/mainwindow.h:~381`, update the declaration and its comment:

```cpp
    /// `mode` says whether to replace the query bar, narrow it, or narrow it
    /// by everything that is not this value. The panes do not read the query
    /// bar; this is where the combining happens.
    void runSearchFromPane(const QString &query, SearchTerm::SearchMode mode);
```

In `src/mainwindow.cpp:1654`:

```cpp
void MainWindow::runSearchFromPane(const QString &query,
                                   SearchTerm::SearchMode mode)
{
    if (query.isEmpty())
        return;

    QString next;
    switch (mode) {
    case SearchTerm::SearchMode::Replace:
        next = query;
        break;
    case SearchTerm::SearchMode::Narrow:
        next = SearchTerm::extend(m_queryEdit->text(), query);
        break;
    case SearchTerm::SearchMode::Exclude:
        next = SearchTerm::exclude(m_queryEdit->text(), query);
        break;
    }

    // exclude() returns empty when there is nothing to exclude from, which the
    // greyed menu entry should already have prevented. Running it would clear
    // the query bar and show the whole Maildir, so refuse instead.
    if (next.isEmpty())
        return;

    // Through the query bar and the existing runner, so the account scope, the
    // generation counter and the flat-mode reset all behave exactly as they do
    // for a typed query. Nothing here builds a second query path.
    m_queryEdit->setText(next);
    runCurrentQuery();
}
```

- [ ] **Step 5: Build and run the full suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: builds clean, all tests pass. This task changes no behaviour, so a
failure here is a mistake in the mechanical change, not a real finding.

- [ ] **Step 6: Commit**

```bash
git add src/messageview.h src/messageview.cpp src/messagedetailsdialog.h \
        src/messagedetailsdialog.cpp src/mainwindow.h src/mainwindow.cpp \
        tests/test_messagedetailsdialog.cpp
git commit -S -m "refactor(search): carry SearchMode instead of bool extend

Four signatures, no behaviour change: the two shipped operations map to
Replace and Narrow. runSearchFromPane becomes a switch and gains the
Exclude arm, which nothing can reach until the menu entry exists.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: The `hasQuery` guard

`MainWindow` pushes down whether the query bar holds anything, so the menus can
grey the new entry. Still no third entry, so still no behaviour change.

**Files:**
- Modify: `src/messageview.h`, `src/messageview.cpp`
- Modify: `src/messagedetailsdialog.h`, `src/messagedetailsdialog.cpp`
- Modify: `src/mainwindow.cpp:~370`

- [ ] **Step 1: Add the setter to `MessageView`**

In `src/messageview.h`, in the public section near the other small accessors:

```cpp
    /// Tells the pane whether the query bar currently holds anything.
    ///
    /// The menus need it to grey out "Exclude from search": excluding from an
    /// empty query would mean the whole Maildir minus one value. The pane
    /// cannot read the query bar and must not, so the window pushes the fact
    /// down as it changes. Passed on to the details dialog at construction,
    /// which is built fresh per invocation and so cannot go stale.
    void setHasQuery(bool hasQuery) { m_hasQuery = hasQuery; }
```

And in the private members, beside `m_headerOffers`:

```cpp
    bool m_hasQuery = false;
```

- [ ] **Step 2: Take it in the dialog's constructor**

In `src/messagedetailsdialog.h`:

```cpp
    explicit MessageDetailsDialog(const QList<ThreadRenderItem> &items,
                                  bool hasQuery = false,
                                  QWidget *parent = nullptr);
```

and in the private members:

```cpp
    bool m_hasQuery = false;
```

In `src/messagedetailsdialog.cpp`, update the constructor definition to match
the new parameter list and store it (`m_hasQuery(hasQuery)` in the init list,
keeping the existing `QDialog(parent)` base and the existing body unchanged).

The default argument keeps every existing test call compiling; a test that
cares passes it explicitly.

- [ ] **Step 3: Pass it at the construction site**

In `src/messageview.cpp`, where the dialog is constructed inside
`showDetailsDialog()` (just above the connect from Task 3, ~line 640), add the
argument:

```cpp
    MessageDetailsDialog dialog(m_items, m_hasQuery, this);
```

Read the existing line first and preserve its exact variable name and the
`this` parent; only the middle argument is new.

- [ ] **Step 4: Push it from `MainWindow`**

In `src/mainwindow.cpp`, the `updateSaveState` lambda at ~line 370 already
runs on every `textChanged`. Widen it, and rename it to say what it now does:

```cpp
        auto updateQueryState = [this, save]() {
            const bool hasQuery = !m_queryEdit->text().trimmed().isEmpty();
            save->setEnabled(hasQuery);
            // The message pane greys "Exclude from search" without it: there
            // is nothing to exclude from.
            m_messageView->setHasQuery(hasQuery);
        };
        connect(m_queryEdit, &QLineEdit::textChanged, this, updateQueryState);
        updateQueryState();
```

**Ordering, verified 2026-08-14, no guard needed.** The line numbers look
alarming: this lambda is at ~370 and `m_messageView` is constructed at
`src/mainwindow.cpp:668`. But this block lives in `registerActions()`, called
from the constructor at line 344, while both `m_queryEdit` (495) and
`m_messageView` (668) are constructed inside `buildUi()`, called one line
earlier at 343. Both pointers are live by the time the lambda is defined or
invoked. Do not add an `if (m_messageView)` guard: it would be dead code that
implies a hazard which does not exist.

- [ ] **Step 5: Build and run the full suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: builds clean, all tests pass, no behaviour change.

- [ ] **Step 6: Commit**

```bash
git add src/messageview.h src/messageview.cpp src/messagedetailsdialog.h \
        src/messagedetailsdialog.cpp src/mainwindow.cpp
git commit -S -m "feat(search): push the query bar's emptiness into the panes

The menus cannot read the query bar and must not. MainWindow already
watched textChanged for the Save button; the same lambda now tells
MessageView, which passes it to the details dialog at construction.

Nothing consumes it yet.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: The third menu entry

**Files:**
- Modify: `src/messageview.cpp:566`
- Modify: `src/messagedetailsdialog.cpp:~88`
- Test: `tests/test_messagedetailsdialog.cpp`

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` block in `tests/test_messagedetailsdialog.cpp`:

```cpp
    void excludeIsOfferedOnlyWithAQueryToExcludeFrom();
```

And the implementation, modelled on the existing
`offersASearchForEachValue()`. Read that test first and reuse its fixture
construction verbatim, including how it builds `items` and finds a row:

```cpp
void TestMessageDetailsDialog::excludeIsOfferedOnlyWithAQueryToExcludeFrom()
{
    // Build `items` exactly as offersASearchForEachValue() does.
    const QList<ThreadRenderItem> items = /* same fixture as that test */;

    // The menu is built per row in a customContextMenuRequested lambda and
    // cannot be popped without a real context-menu event, so assert on the
    // property the entry's enabled state is derived from: the dialog was told
    // whether a query exists.
    MessageDetailsDialog withQuery(items, true);
    MessageDetailsDialog withoutQuery(items, false);

    QVERIFY(withQuery.canExcludeFromSearch());
    QVERIFY(!withoutQuery.canExcludeFromSearch());

    // And the emit itself refuses when there is nothing to exclude from, so
    // the guard does not rest on the menu alone.
    const auto rows = withoutQuery.rows();
    const auto *from = std::find_if(
        rows.cbegin(), rows.cend(),
        [](const HeaderRow &row) { return !row.query.isEmpty(); });
    QVERIFY(from != rows.cend());

    QSignalSpy spy(&withoutQuery, &MessageDetailsDialog::searchRequested);
    withoutQuery.requestSearch(*from, SearchTerm::SearchMode::Exclude);
    QCOMPARE(spy.count(), 0);
}
```

This requires one small accessor, added in the same step, in
`src/messagedetailsdialog.h`:

```cpp
    /// Whether "Exclude from search" is offered. False with an empty query
    /// bar: there would be nothing to exclude from. Exposed for testing
    /// without popping a context menu.
    bool canExcludeFromSearch() const { return m_hasQuery; }
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_messagedetailsdialog
```

Expected: FAIL on the `spy.count()` assertion, because `requestSearch` does not
yet refuse an Exclude with no query. (`canExcludeFromSearch()` passes as soon
as the accessor exists, which is fine; the emit guard is the real subject.)

- [ ] **Step 3: Guard the emit**

In `src/messagedetailsdialog.cpp`, `requestSearch`:

```cpp
void MessageDetailsDialog::requestSearch(const HeaderRow &row,
                                         SearchTerm::SearchMode mode)
{
    if (row.query.isEmpty())
        return;
    // Nothing to exclude from: the entry is greyed, and this is the second
    // layer in case it is reached another way.
    if (mode == SearchTerm::SearchMode::Exclude && !m_hasQuery)
        return;
    emit searchRequested(row.query, mode);
}
```

- [ ] **Step 4: Add the entry to the dialog's menu**

In `src/messagedetailsdialog.cpp`, after the `narrow` action in the per-row
lambda and before `menu.exec(...)`:

```cpp
                        auto *exclude =
                            menu.addAction(tr("Exclude from search"));
                        // Visible but greyed rather than hidden: someone
                        // exploring a fresh window is exactly who should see
                        // that the feature exists.
                        exclude->setEnabled(m_hasQuery);
                        connect(exclude, &QAction::triggered, this,
                                [this, row]() {
                                    requestSearch(
                                        row, SearchTerm::SearchMode::Exclude);
                                });
```

- [ ] **Step 5: Add the entry to the message pane's menus**

In `src/messageview.cpp`, `addSearchEntries`, after the `narrow` action:

```cpp
        auto *exclude = sub->addAction(tr("Exclude from search"));
        // Visible but greyed rather than hidden, as in the details dialog.
        exclude->setEnabled(m_hasQuery);
        connect(exclude, &QAction::triggered, this, [this, entry]() {
            emit searchRequested(entry.query, SearchTerm::SearchMode::Exclude);
        });
```

- [ ] **Step 6: Run the full suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including the new one.

- [ ] **Step 7: Mutation check**

Verify the new test can fail. Temporarily change the guard in `requestSearch`
to `if (false && mode == ...)`, rebuild, and confirm
`test_messagedetailsdialog` FAILS. Then revert the change and rebuild.

**Do not restore from a backup file.** A backup taken before a fix and restored
afterwards silently reverted a fix in the item 85 session, leaving the test and
its comment orphaned. Edit the line back by hand and re-read it.

**If the binary hangs rather than failing**, that is item 85's ordering trap:
without `accept()` before the emit, nothing leaves `exec()`. Kill the binary
and rebuild before concluding anything, since a later `ctest` will otherwise
re-run a stale one.

- [ ] **Step 8: Commit**

```bash
git add src/messageview.cpp src/messagedetailsdialog.h \
        src/messagedetailsdialog.cpp tests/test_messagedetailsdialog.cpp
git commit -S -m "feat(search): offer Exclude from search in both menus

Third entry in the message pane's submenus and in each details row,
greyed rather than hidden when the query bar is empty, so the feature
stays visible to someone exploring a fresh window.

requestSearch refuses an Exclude with no query as well, so the guard
does not rest on the menu's enabled state alone.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Close the item

**Files:**
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md`
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability-closed.md`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Run the full suite one more time**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Record the actual pass count for the commit message. Do not write a number you
did not read.

- [ ] **Step 2: Hand it to the user**

Do NOT launch the application. Tell the user what to look at: right-click a
header value, a body selection and a details row, with the query bar empty and
then with a query in it, and confirm the third entry is greyed in the first
case and narrows correctly in the second. Wait for their verdict before the
remaining steps.

- [ ] **Step 3: Move the backlog section**

Set item 86's status cell to `**done** 2026-08-14` with the spec reference, and
move its whole `## 86.` section from the backlog to
`2026-08-03-post-0.1.0-usability-closed.md`, keeping the number. The backlog
keeps the table row only. Move it on this commit, not in a later cleanup pass.

- [ ] **Step 4: Add the changelog entry**

Under `## [Unreleased]`, in `### Added`:

```markdown
- A third right-click search action, **Exclude from search**, which narrows the
  current query by everything that is not the value under the cursor. Offered
  everywhere the other two are, and greyed out when the query bar is empty,
  since there would be nothing to exclude from.
```

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md \
        docs/superpowers/plans/2026-08-03-post-0.1.0-usability-closed.md \
        CHANGELOG.md
git commit -S -m "docs: close item 86, excluding a value from a search

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Self-review notes

Spec coverage, section by section: the grammar and its empty-case table are
Task 1; the `SearchMode` enum is Task 2 and its four signatures Task 3; the
`setHasQuery` guard is Task 4; the menus are Task 5; the testing section is
split across Tasks 1 and 5, including the mutation check and both traps it
inherits. "Out of scope" needs no task, the thread list is untouched throughout.

Type consistency: `SearchTerm::SearchMode` is spelled with its namespace at
every use outside `searchterm.h`. `m_hasQuery` is the member in both
`MessageView` and `MessageDetailsDialog`; `setHasQuery` is the setter on the
view and a constructor argument on the dialog, deliberately different because
the dialog is rebuilt per invocation.

Two steps ask the implementer to read surrounding code rather than trusting the
plan: the spy assertions in Task 3 Step 1, and the dialog fixture in Task 5
Step 1. Both are existing test code whose exact shape is not reproduced here;
the plan says so explicitly rather than inventing it.
