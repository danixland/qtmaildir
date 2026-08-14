# Saved query to tagging rule: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A right-click action on a stored saved query that opens the tagging
rules dialog with a new rule seeded from that query.

**Architecture:** `TagRulesDialog` gains a constructor taking a seed `TagRule`
and a `seedRule()` method for the already-open case;
`MainWindow::showTagRulesDialog()` gains an optional seed it passes through;
`addSavedQueryActions()` gains one action, guarded on `!saved.isGenerated()`.
No new file, no change to the shared rule format.

**Tech Stack:** Qt 6.11, C++17, QtTest. Build with CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-08-14-query-to-rule-design.md`

---

## Before you start

Read these, they will save you from three traps this codebase has already hit:

- `CLAUDE.md`, sections "Web view security" excepted. In particular the entries
  on `QDialog::done()`, on rendering probes, and on the offscreen platform.
- The rules dialog is **non-modal and single-instance**
  (`src/mainwindow.cpp:1325`): it uses `show()` with `WA_DeleteOnClose`, and
  raises the existing dialog if one is open. This is why Task 3 exists.

**Build and test commands**, used throughout:

```bash
cmake --build build
QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules
QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow
ctest --test-dir build --output-on-failure
```

**Never run a test binary without `QT_QPA_PLATFORM=offscreen`**, and never
launch `./build/src/qtmaildir`. Each test function builds a `MainWindow`; one
unguarded run throws a hundred windows onto the user's screen. This is a
standing instruction in `CLAUDE.md` and the user has asked for it twice.

---

## File structure

| File | Responsibility | Change |
|---|---|---|
| `src/tagrulesdialog.h` | Dialog interface | Add seed constructor, `seedRule()`, one test seam |
| `src/tagrulesdialog.cpp` | Dialog behaviour | Implement the above; existing ctor delegates |
| `src/mainwindow.h` | Window interface | `showTagRulesDialog()` takes an optional seed |
| `src/mainwindow.cpp` | Menu and dialog wiring | Pass the seed through; add the menu action |
| `tests/test_tagrules.cpp` | Dialog tests | Tasks 1, 2, 3 |
| `tests/test_mainwindow.cpp` | Menu tests | Task 5 |

---

### Task 1: `TagRulesDialog` accepts a seed rule

**Files:**
- Modify: `src/tagrulesdialog.h:53`
- Modify: `src/tagrulesdialog.cpp:78` (constructor)
- Test: `tests/test_tagrules.cpp`

- [ ] **Step 1: Write the failing test**

Add to the `private slots:` block in `tests/test_tagrules.cpp`, after
`aRuleAddedAndNamedInTheDialogSurvivesAReopen();`:

```cpp
    void aSeededDialogOpensOnTheNewRuleWithoutWritingIt();
```

Add the test body, before `void TestTagRules::aFolderRowUsesTheDropdownAndKeepsItsSuffix()`:

```cpp
void TestTagRules::aSeededDialogOpensOnTheNewRuleWithoutWritingIt()
{
    // The seeded rule is a pending edit, exactly like one made with Add rule:
    // in the working list, selected, and NOT on disk until Save. Asserting the
    // file is unchanged is the half that matters, since a dialog that wrote on
    // open would tag real mail from a menu click.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "vendor", "query": "from:vendor.example.org",
         "add": ["vendor"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRule seed;
    seed.id = QStringLiteral("weekly-digest");
    seed.query = QStringLiteral("from:digest.example.org");

    TagRulesDialog dialog(seed);

    // Appended after the rules already in the file, and current.
    QCOMPARE(dialog.ruleCountForTest(), 2);
    QCOMPARE(dialog.nameLineForTest(), QStringLiteral("weekly-digest"));
    QCOMPARE(dialog.queryLineForTest(),
             QStringLiteral("from:digest.example.org"));

    // Enabled, like a rule made with Add rule. A rule created disabled and
    // then forgotten is its own silent failure.
    QVERIFY(dialog.currentRuleEnabledForTest());

    // Nothing written. Reread from disk rather than trusting the dialog.
    TagRules onDisk;
    onDisk.load(stored);
    QCOMPARE(onDisk.rules().size(), 1);
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build 2>&1 | grep -E "error" | head -5
```

Expected: `no matching constructor for initialization of 'TagRulesDialog'`.
The test cannot run until Step 3 compiles.

- [ ] **Step 3: Add the constructor**

In `src/tagrulesdialog.h`, replace line 53:

```cpp
    explicit TagRulesDialog(QWidget *parent = nullptr);
```

with:

```cpp
    explicit TagRulesDialog(QWidget *parent = nullptr);

    /// Opens with one new rule already in the working list, selected, and the
    /// Add tags field focused. The rule is a pending edit like any other: it
    /// reaches the file on Save and is discarded on Cancel.
    ///
    /// The seed is a whole TagRule rather than a query string because item 78
    /// will seed from a sender and will want to set tags too.
    explicit TagRulesDialog(const TagRule &seed, QWidget *parent = nullptr);
```

In `src/tagrulesdialog.cpp`, find the constructor at line 78. Change its
signature line from:

```cpp
TagRulesDialog::TagRulesDialog(QWidget *parent)
    : QDialog(parent)
{
```

to:

```cpp
TagRulesDialog::TagRulesDialog(QWidget *parent)
    : TagRulesDialog(TagRule(), parent)
{
}

TagRulesDialog::TagRulesDialog(const TagRule &seed, QWidget *parent)
    : QDialog(parent)
{
```

Then find the END of that constructor. It currently finishes with:

```cpp
    showWarnings();

    // Last, and after reloadList(): a header state cannot be applied before
    // the columns it describes exist, and reloadList is what fills them.
    restoreUiState();
}
```

Replace that with:

```cpp
    showWarnings();

    // Last, and after reloadList(): a header state cannot be applied before
    // the columns it describes exist, and reloadList is what fills them.
    restoreUiState();

    // After restoreUiState, so the seeded rule's selection is not overwritten
    // by anything the restore does to the list.
    if (!seed.query.isEmpty())
        seedRule(seed);
}
```

- [ ] **Step 4: Add the test seam for the enabled flag**

Declare in `src/tagrulesdialog.h`, beside the other `ForTest` seams:

```cpp
    /// The selected rule's enabled flag, so the seeded default is asserted
    /// rather than assumed from TagRule's initialiser.
    bool currentRuleEnabledForTest() const;
```

Implement in `src/tagrulesdialog.cpp`, beside `ruleCountForTest()`:

```cpp
bool TagRulesDialog::currentRuleEnabledForTest() const
{
    return m_enabled->isChecked();
}
```

- [ ] **Step 5: Add `seedRule()`**

Declare it in `src/tagrulesdialog.h`, in the `public:` section immediately
after the two constructors:

```cpp
    /// Appends `seed` to the working rules, selects it and focuses Add tags.
    /// Public because the dialog is single-instance and non-modal: a second
    /// Create tagging rule while it is open seeds the dialog already up
    /// rather than being dropped.
    void seedRule(const TagRule &seed);
```

Implement it in `src/tagrulesdialog.cpp`, immediately after `onAddRule()`:

```cpp
void TagRulesDialog::seedRule(const TagRule &seed)
{
    // Flushed first, as onAddRule does: reloadList() repaints every row from
    // m_working, so an edit still sitting in the form would be lost.
    applyEditsToCurrentRule();

    TagRule rule = seed;

    // enabled and stage are TagRule's own defaults (true, 50), matching what
    // Add rule produces. An inconsistent default between two ways of making
    // the same thing is worse than either default.

    // Against the ids already present, not only against the file: the working
    // list may hold unsaved rules whose ids would collide just as hard.
    QStringList taken;
    for (const TagRule &existing : m_working)
        taken.append(existing.id);
    rule.id = TagRules::uniqueId(rule.id, taken);

    m_working.append(rule);
    reloadList();
    m_list->setCurrentItem(m_list->topLevelItem(m_working.size() - 1));

    // The one field the user must supply. A rule that tags nothing fails
    // validate(), so Save refuses it rather than writing a rule the hook
    // would ignore.
    m_add->setFocus();
}
```

- [ ] **Step 6: Run the test**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules
```

Expected: all pass, including `aSeededDialogOpensOnTheNewRuleWithoutWritingIt`.

- [ ] **Step 7: Verify the test can fail**

Temporarily change `m_working.append(rule);` to `// m_working.append(rule);`,
rebuild, and confirm the test FAILS on the rule count. Then restore the line,
rebuild, and confirm it passes again. A test that has never been seen to fail
is not evidence of anything.

- [ ] **Step 8: Commit**

```bash
git add src/tagrulesdialog.h src/tagrulesdialog.cpp tests/test_tagrules.cpp
git commit -S -m "feat(rules): open the rules dialog on a seeded rule

The seed is a whole TagRule rather than a query string, so item 78 can
reuse the same path to seed from a sender. It is a pending edit like one
made with Add rule: appended, selected, Add tags focused, and written
only on Save."
```

---

### Task 2: A seeded id that collides gets its own

**Files:**
- Test: `tests/test_tagrules.cpp`

No production change. This pins behaviour Task 1 already implements, and it is
worth its own task because the collision is reachable here in a way it is not
from the rules dialog alone: the name comes from `queries.json`, a different
file, so nothing has ever checked it against the rule ids.

- [ ] **Step 1: Write the test**

Add to `private slots:`:

```cpp
    void aSeededIdThatCollidesDoesNotReplaceTheRuleItMatches();
```

Add the body after the Task 1 test:

```cpp
void TestTagRules::aSeededIdThatCollidesDoesNotReplaceTheRuleItMatches()
{
    // A saved query named "Vendor" sanitises to "vendor", which is already a
    // rule id here. Replacing that rule would silently retag mail against a
    // query the user never associated with it.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "vendor", "query": "from:vendor.example.org",
         "add": ["vendor"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRule seed;
    seed.id = TagRules::sanitiseId(QStringLiteral("Vendor"));
    seed.query = QStringLiteral("from:other.example.org");

    TagRulesDialog dialog(seed);

    QCOMPARE(dialog.ruleCountForTest(), 2);
    QCOMPARE(dialog.nameLineForTest(), QStringLiteral("vendor-2"));

    // And the rule it collided with is untouched.
    dialog.selectRuleForTest(0);
    QCOMPARE(dialog.nameLineForTest(), QStringLiteral("vendor"));
    QCOMPARE(dialog.queryLineForTest(),
             QStringLiteral("from:vendor.example.org"));
}
```

- [ ] **Step 2: Run it**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules
```

Expected: PASS, since Task 1 implemented `uniqueId` against `m_working`.

- [ ] **Step 3: Verify it can fail**

Temporarily replace `rule.id = TagRules::uniqueId(rule.id, taken);` with
`rule.id = seed.id;`, rebuild, and confirm this test FAILS on `vendor-2`.
Restore, rebuild, confirm PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/test_tagrules.cpp
git commit -S -m "test(rules): pin a seeded id against colliding with a rule

The name comes from queries.json, so nothing had ever checked it against
the ids in rules.json. Replacing the matched rule would retag mail
against a query the user never associated with it."
```

---

### Task 3: Seeding a dialog that is already open

**Files:**
- Modify: `src/mainwindow.h:379`
- Modify: `src/mainwindow.cpp:1325`
- Test: `tests/test_tagrules.cpp`

The rules dialog is single-instance: `showTagRulesDialog()` raises the existing
one and returns. Without this task, choosing Create tagging rule while the
dialog is open silently does nothing, which reads as a broken menu item.

- [ ] **Step 1: Write the failing test**

Add to `private slots:`:

```cpp
    void seedingTwiceAddsTwoRulesRatherThanReplacingOne();
```

Add the body:

```cpp
void TestTagRules::seedingTwiceAddsTwoRulesRatherThanReplacingOne()
{
    // The dialog is non-modal and single-instance, so a second Create tagging
    // rule arrives at a dialog that is already up. It must append, not replace
    // the first seed and not be dropped.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "vendor", "query": "from:vendor.example.org",
         "add": ["vendor"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRule first;
    first.id = QStringLiteral("first-seed");
    first.query = QStringLiteral("from:one.example.org");

    TagRulesDialog dialog(first);
    QCOMPARE(dialog.ruleCountForTest(), 2);

    TagRule second;
    second.id = QStringLiteral("second-seed");
    second.query = QStringLiteral("from:two.example.org");
    dialog.seedRule(second);

    QCOMPARE(dialog.ruleCountForTest(), 3);
    QCOMPARE(dialog.nameLineForTest(), QStringLiteral("second-seed"));

    // The first seed survived rather than being overwritten.
    dialog.selectRuleForTest(1);
    QCOMPARE(dialog.nameLineForTest(), QStringLiteral("first-seed"));
}
```

- [ ] **Step 2: Run it**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules
```

Expected: PASS. `seedRule()` is public and already appends. If it fails,
`seedRule` is replacing rather than appending and Task 1 needs revisiting.

- [ ] **Step 3: Thread the seed through `showTagRulesDialog`**

In `src/mainwindow.h`, replace line 379:

```cpp
    void showTagRulesDialog();
```

with:

```cpp
    /// `seed` is an optional rule to open on, used by Create tagging rule on a
    /// saved query. A default-constructed TagRule (empty query) means none.
    void showTagRulesDialog(const TagRule &seed = TagRule());
```

In `src/mainwindow.cpp`, change the definition at line 1325 from:

```cpp
void MainWindow::showTagRulesDialog()
{
```

to:

```cpp
void MainWindow::showTagRulesDialog(const TagRule &seed)
{
```

Then change the early-return block from:

```cpp
    if (m_tagRulesDialog) {
        m_tagRulesDialog->raise();
        m_tagRulesDialog->activateWindow();
        return;
    }
```

to:

```cpp
    if (m_tagRulesDialog) {
        // Seeded into the dialog already up rather than dropped: the menu item
        // must do something visible, and a second dialog would edit a stale
        // copy whose Save would silently win.
        if (!seed.query.isEmpty())
            m_tagRulesDialog->seedRule(seed);
        m_tagRulesDialog->raise();
        m_tagRulesDialog->activateWindow();
        return;
    }
```

And change the construction from:

```cpp
    auto *dialog = new TagRulesDialog(this);
```

to:

```cpp
    auto *dialog = new TagRulesDialog(seed, this);
```

- [ ] **Step 4: Check `mainwindow.h` includes `tagrules.h`**

Run:

```bash
grep -n '#include "tagrules.h"' src/mainwindow.h
```

If there is no output, add `#include "tagrules.h"` to the includes in
`src/mainwindow.h`. A forward declaration will not do: the default argument
`TagRule()` needs the complete type.

- [ ] **Step 5: Build and run the whole suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: 20 of 20 pass. Every existing `showTagRulesDialog()` caller still
compiles, because the parameter is defaulted.

- [ ] **Step 6: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp tests/test_tagrules.cpp
git commit -S -m "feat(rules): seed the rules dialog even when it is open

The dialog is non-modal and single-instance, so a second Create tagging
rule reaches one that is already up. Seeding it beats dropping the
request, which would read as a broken menu item."
```

---

### Task 4: The menu action

**Files:**
- Modify: `src/mainwindow.cpp:1743` (`addSavedQueryActions`)

- [ ] **Step 1: Add the action**

In `src/mainwindow.cpp`, find the end of `addSavedQueryActions()`. It currently
finishes with the Delete action:

```cpp
    auto *remove = new QAction(tr("Delete"), target);
    remove->setObjectName(QStringLiteral("deleteQuery"));
    connect(remove, &QAction::triggered, this,
            [this, saved]() { deleteSavedQuery(saved); });
    target->addAction(remove);
}
```

Replace that with:

```cpp
    auto *remove = new QAction(tr("Delete"), target);
    remove->setObjectName(QStringLiteral("deleteQuery"));
    connect(remove, &QAction::triggered, this,
            [this, saved]() { deleteSavedQuery(saved); });
    target->addAction(remove);

    // Stored queries only. A generated entry composes its query from the
    // accounts at run time, so a rule made from one would freeze a snapshot
    // that goes stale the day an account is added, in a file the post-new hook
    // reads unattended.
    if (saved.isGenerated())
        return;

    auto *ruleSeparator = new QAction(target);
    ruleSeparator->setSeparator(true);
    target->addAction(ruleSeparator);

    auto *toRule = new QAction(tr("Create tagging rule..."), target);
    toRule->setObjectName(QStringLiteral("queryToRule"));
    connect(toRule, &QAction::triggered, this, [this, saved]() {
        TagRule seed;
        seed.id = TagRules::sanitiseId(saved.name);
        seed.query = saved.query;
        showTagRulesDialog(seed);
    });
    target->addAction(toRule);
}
```

- [ ] **Step 2: Build**

```bash
cmake --build build 2>&1 | grep -E "error" | head -5
```

Expected: no output. If `TagRules` is not declared, add
`#include "tagrules.h"` to `src/mainwindow.cpp`'s includes.

- [ ] **Step 3: Run the suite**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: 20 of 20 pass.

- [ ] **Step 4: Commit**

```bash
git add src/mainwindow.cpp
git commit -S -m "feat(queries): create a tagging rule from a saved query

Right-click a stored saved query and the rules dialog opens on a new
rule carrying its query, with the tags left empty and focused. Generated
entries are excluded: their query is composed from the accounts, so a
rule made from one would freeze a snapshot that goes stale."
```

---

### Task 5: The menu action is offered only where it makes sense

**Files:**
- Test: `tests/test_mainwindow.cpp`

The helpers this needs already exist, found at `tests/test_mainwindow.cpp:5793`
(`contextActionNamed`) and `:5805` (`aSavedQueryButtonOffersEditUnpinAndDelete`,
which shows the whole setup). Follow that test's shape rather than inventing
one.

- [ ] **Step 1: Write the test**

Add to `private slots:` in `tests/test_mainwindow.cpp`, beside the other
saved-query menu tests:

```cpp
    void onlyAStoredQueryOffersToBecomeATaggingRule();
```

Add the body, after `aSavedQueryButtonOffersEditUnpinAndDelete()`:

```cpp
void TestMainWindow::onlyAStoredQueryOffersToBecomeATaggingRule()
{
    // A generated entry composes its query from the accounts, so a rule made
    // from one freezes a snapshot that goes stale when an account is added.
    //
    // Both halves are asserted together on purpose: a test that only checks a
    // menu item is ABSENT passes just as well against a feature that was never
    // built, which item 82 recorded the hard way.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true },
            { "name": "Sent", "generated": "sent", "pinned": true }
        ]
    })"));

    MainWindow window(config);
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);

    const QList<QPushButton *> buttons = row->findChildren<QPushButton *>();
    QPushButton *stored = nullptr;
    QPushButton *generated = nullptr;
    for (QPushButton *button : buttons) {
        if (button->text().contains(QStringLiteral("Inbox")))
            stored = button;
        else if (button->text().contains(QStringLiteral("Sent")))
            generated = button;
    }

    QVERIFY2(stored, "no button was built for the stored query");
    QVERIFY2(generated, "no button was built for the generated query");

    QVERIFY2(contextActionNamed(window, stored, QStringLiteral("queryToRule")),
             "a stored query must offer Create tagging rule");
    QVERIFY2(!contextActionNamed(window, generated,
                                 QStringLiteral("queryToRule")),
             "a generated query must not: its query is a snapshot");

    // The guard proving the generated button HAS a menu, so the assertion
    // above is about this one action and not about a button with no actions.
    QVERIFY2(contextActionNamed(window, generated,
                                QStringLiteral("deleteQuery")),
             "the generated button must still carry its other actions");
}
```

- [ ] **Step 2: Run it**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow
```

Expected: PASS.

If the buttons cannot be told apart by their text (they may carry an icon and a
shortened label), fall back to matching on the order they were built: with the
JSON above, `buttons.at(0)` is Inbox and `buttons.at(1)` is Sent. Assert
`buttons.size() >= 2` first so the indexing cannot read past the end.

- [ ] **Step 3: Verify it can fail**

Temporarily change `if (saved.isGenerated())` to `if (false)` in
`addSavedQueryActions()`, rebuild, and confirm this test FAILS on the generated
half. Restore, rebuild, confirm PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/test_mainwindow.cpp
git commit -S -m "test(queries): pin which saved queries offer a tagging rule

Both halves asserted together, plus a guard proving the generated button
carries a menu at all: a test for the absence of a menu item passes
against no implementation, which item 82 recorded the hard way."
```

---

### Task 6: Close the item

**Files:**
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md`
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability-closed.md`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Move the backlog section**

Cut the whole `## 81.` section out of
`docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md` and append it to
`docs/superpowers/plans/2026-08-03-post-0.1.0-usability-closed.md`, in numeric
order (after item 80, before 82 if 82 is already there). Add a closing note to
the moved section:

```markdown
**Done 2026-08-14.** A Create tagging rule action on the context menu of a
stored saved query, seeding `TagRulesDialog` with the query and a sanitised
id. Single repo after all: the rule it writes is an ordinary one.

Two things the spec did not anticipate. The dialog is non-modal and
single-instance, so a second request arrives at a dialog already open and is
seeded into it rather than dropped. And the seeded id is uniqued against the
WORKING list rather than the file, since that list can hold unsaved rules
whose ids collide just as hard.
```

Update the status table row for 81 to:

```markdown
| 81 | No way to turn a saved query into a tagging rule | workflow | S | **done** 2026-08-14, unreleased; see `specs/2026-08-14-query-to-rule-design.md` |
```

- [ ] **Step 2: Add the changelog entry**

Under `## [Unreleased]` in `CHANGELOG.md`, in an `### Added` section (create it
if absent):

```markdown
- A saved query can be turned into a tagging rule: right-click a stored query
  and choose **Create tagging rule...**. The rules dialog opens on a new rule
  carrying that query, with the tags left for you to fill in. Generated
  entries such as Sent are excluded, since their query is composed from your
  accounts and a rule would freeze a stale copy of it.
```

- [ ] **Step 3: Run the whole suite one last time**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: 20 of 20 pass.

- [ ] **Step 4: Commit**

```bash
git add docs/ CHANGELOG.md
git commit -S -m "docs: close item 81, saved query to tagging rule"
```

---

## Done when

- `Create tagging rule...` appears on a stored saved query's context menu and
  not on a generated one.
- Choosing it opens the rules dialog on a new rule with the query and a
  sanitised id, tags empty and focused, nothing written until Save.
- Choosing it while the dialog is open seeds that dialog and raises it.
- A colliding id gets a suffix rather than replacing the rule it matched.
- 20 of 20 suites green.

## Hand test, for the user

The suite cannot judge any of this on screen. Worth a look:

1. Right-click a saved query button. The new entry is below Delete, after a
   separator.
2. Choose it. The rules dialog opens with the new rule selected and the cursor
   in Add tags.
3. Press Save without typing tags. The red banner refuses it and names the rule.
4. Type a tag, Save, reopen the dialog. The rule is there.
5. Right-click Sent. No Create tagging rule entry.
