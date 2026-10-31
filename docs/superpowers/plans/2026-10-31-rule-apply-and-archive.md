# Rule Apply and Archive-on-Arrival Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the rules dialog apply a saved rule to existing mail (confirmed, undoable), and let a rule opt in to removing `inbox` on arrival so the `post-new` hook stops skipping it.

**Architecture:** Spec: `docs/superpowers/specs/2026-10-31-rule-apply-and-archive-design.md`. Part B adds an optional `allow_archive` rule field to both readers (`src/tagrules.cpp`, `assets/hooks/mailrules.py`) and teaches `post-new` to honour it for `inbox` only. Part A adds `SearchTerm::changes()` (the "would change" query), a worker slot `applyTagsToQuery` that resolves ids and calls the existing `applyTags()`, an Apply button in `TagRulesDialog` that saves first and emits `applyRequested(TagRule)`, and `MainWindow::applyRuleToExistingMail()` that counts, confirms, and writes the rule one tag at a time, each as a `MessageTagCommand` filled from its confirmation, grouped in one undo macro.

**Tech Stack:** C++17, Qt 6.11 (Widgets, Test), libnotmuch, Python 3 stdlib (hook), CMake/Ninja.

**Conventions for every task:**
- Build: `cmake --build build`. Run a C++ test binary ONLY as `QT_QPA_PLATFORM=offscreen ./build/tests/test_<name> [function]`. Never launch `./build/src/qtmaildir`.
- Python hook tests run from `assets/hooks/`: `./test_mailrules.py && ./test_post_new.py`.
- `tests/test_mainwindow` has 4 failures on clean master (`everyBuiltinFilterButtonCarriesAnIconAndItsText`, `everyActionCarriesAnIcon`, `noTwoActionsShareAnIcon`, `undoMovesTheMessageBack`). They are environmental and pre-existing; any OTHER failure is yours.
- Commits are GPG-signed: `git commit -S`. End every message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never stage `HANDOFF.md`.
- No em dashes anywhere (code comments, docs, commit messages). No real names, addresses or subjects in fixtures: use `example.org` / `example.net`.
- Every user-facing string goes through `tr()`. Notmuch query syntax is never translated.

---

### Task 1: `SearchTerm::changes()`, the "would change" query

**Files:**
- Modify: `src/searchterm.h` (after `exclude()`, before the closing `}  // namespace SearchTerm`)
- Modify: `src/searchterm.cpp`
- Test: `tests/test_searchterm.cpp`

- [ ] **Step 1: Write the failing tests**

Add to the `private slots:` list in `tests/test_searchterm.cpp`:

```cpp
    void changesNarrowsToMessagesTheTagsWouldMove();
    void changesKeepsADisjunctionInsideItsScope();
    void changesWithNothingToDoMatchesNothing();
```

Add the bodies before `QTEST_MAIN`:

```cpp
void TestSearchTerm::changesNarrowsToMessagesTheTagsWouldMove()
{
    // Item 212. A message already carrying every added tag and none of the
    // removed ones would not change, so it must not be counted or written.
    QCOMPARE(SearchTerm::changes(QStringLiteral("from:a@example.org"),
                                 { QStringLiteral("promo") },
                                 { QStringLiteral("inbox") }),
             QStringLiteral("(from:a@example.org) AND "
                            "(not tag:promo or tag:inbox)"));
    QCOMPARE(SearchTerm::changes(QStringLiteral("from:a@example.org"),
                                 { QStringLiteral("x"), QStringLiteral("y") },
                                 {}),
             QStringLiteral("(from:a@example.org) AND "
                            "(not tag:x or not tag:y)"));
    QCOMPARE(SearchTerm::changes(QStringLiteral("from:a@example.org"), {},
                                 { QStringLiteral("inbox") }),
             QStringLiteral("(from:a@example.org) AND (tag:inbox)"));
}

void TestSearchTerm::changesKeepsADisjunctionInsideItsScope()
{
    // `a or b AND c` binds as `a or (b AND c)`, which would write to every
    // message matching `a` whatever its tags. extend() parenthesises both.
    QCOMPARE(SearchTerm::changes(QStringLiteral("from:a@example.org or "
                                                "from:b@example.org"),
                                 { QStringLiteral("promo") }, {}),
             QStringLiteral("(from:a@example.org or from:b@example.org) AND "
                            "(not tag:promo)"));
}

void TestSearchTerm::changesWithNothingToDoMatchesNothing()
{
    // notmuch reads an EMPTY query as "match everything". A caller writing
    // through this must never be handed one.
    QCOMPARE(SearchTerm::changes(QString(), { QStringLiteral("x") }, {}),
             Config::matchNothingQuery());
    QCOMPARE(SearchTerm::changes(QStringLiteral("from:a@example.org"), {}, {}),
             Config::matchNothingQuery());
}
```

Add `#include "config.h"` beside `#include "searchterm.h"` in the test if it is not already there.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -m3 error`
Expected: compile error, `'changes' is not a member of 'SearchTerm'`.

- [ ] **Step 3: Implement**

In `src/searchterm.h`, before the namespace's closing brace:

```cpp
/// The messages among `query` that `+add -remove` would actually change:
/// `(query) AND (not tag:a1 or ... or tag:r1 ...)`.
///
/// Used to count and to write a rule applied to existing mail (item 212), so
/// the number in the confirmation is the number of messages touched, not the
/// number matched. Built on extend(), so a disjunction in the rule cannot
/// escape the scope.
///
/// **Never empty.** An empty query or an empty change yields
/// Config::matchNothingQuery(), because notmuch reads an empty query as
/// "match everything" and this string is written to.
QString changes(const QString &query, const QStringList &add,
                const QStringList &remove);
```

In `src/searchterm.cpp`, add `#include "config.h"` below `#include "searchterm.h"`, and at the end of the namespace:

```cpp
QString changes(const QString &query, const QStringList &add,
                const QStringList &remove)
{
    QStringList terms;
    for (const QString &name : add) {
        const QString term = tag(name);
        if (!term.isEmpty())
            terms.append(QStringLiteral("not ") + term);
    }
    for (const QString &name : remove) {
        const QString term = tag(name);
        if (!term.isEmpty())
            terms.append(term);
    }
    if (query.trimmed().isEmpty() || terms.isEmpty())
        return Config::matchNothingQuery();
    return extend(query, terms.join(QStringLiteral(" or ")));
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_searchterm`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/searchterm.h src/searchterm.cpp tests/test_searchterm.cpp
git commit -S -m "feat: build the query for what a tag change would move

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `allow_archive` in both rule readers

Read "Changing the rule format" at the bottom of `AGENTS.md` first. Optional field, so **no version bump**.

**Files:**
- Modify: `src/tagrules.h` (`struct TagRule`, and a new static in `class TagRules`)
- Modify: `src/tagrules.cpp` (`isKnownKey`, `load`, `save`, new `hookWouldSkip`)
- Modify: `assets/hooks/mailrules.py` (`KNOWN_KEYS`, `Rule`, `_parse_rule`, `_rule_to_dict`)
- Test: `tests/test_tagrules.cpp`, `assets/hooks/test_mailrules.py`

- [ ] **Step 1: Write the failing C++ tests**

Add to `private slots:` in `tests/test_tagrules.cpp`:

```cpp
    void allowArchiveRoundTripsAndIsOmittedWhenFalse();
    void allowArchiveIsDroppedFromARuleThatKeepsInbox();
    void hookWouldSkipMirrorsThePostNewRefusal();
```

Bodies, before `QTEST_MAIN`:

```cpp
void TestTagRules::allowArchiveRoundTripsAndIsOmittedWhenFalse()
{
    // Item 213. Optional, so a file with no archiving rule must save
    // unchanged: no "allow_archive": false appears from nowhere.
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "promo", "add": ["promo"], "remove": ["inbox"],
         "query": "from:a@example.org", "allow_archive": true},
        {"id": "plain", "add": ["x"], "query": "from:b@example.org"}
      ]
    })");

    TagRules rules;
    rules.load(path);
    QCOMPARE(rules.rules().size(), 2);
    QVERIFY(rules.rules().at(0).allowArchive);
    QVERIFY(!rules.rules().at(1).allowArchive);
    // Known now, so it is not ALSO carried as an unknown field.
    QVERIFY(!rules.rules().at(0).unknown.contains(QStringLiteral("allow_archive")));

    QVERIFY(rules.save(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonArray saved = QJsonDocument::fromJson(file.readAll())
                                 .object().value(QStringLiteral("rules")).toArray();
    QCOMPARE(saved.at(0).toObject().value(QStringLiteral("allow_archive")),
             QJsonValue(true));
    QVERIFY(!saved.at(1).toObject().contains(QStringLiteral("allow_archive")));
}

void TestTagRules::allowArchiveIsDroppedFromARuleThatKeepsInbox()
{
    // A flag that does nothing is cleared by the WRITER, never rejected by a
    // reader: the hook must not drop a whole rule over a harmless flag.
    TagRule rule;
    rule.id = QStringLiteral("x");
    rule.query = QStringLiteral("from:a@example.org");
    rule.add = { QStringLiteral("x") };
    rule.allowArchive = true;

    TagRules rules;
    rules.setRules({ rule });
    QVERIFY(TagRules::validate(rules.rules()).isEmpty());
    const QString path = m_dir.filePath(QStringLiteral("cleared.json"));
    QVERIFY(rules.save(path));

    TagRules reloaded;
    reloaded.load(path);
    QVERIFY(!reloaded.rules().at(0).allowArchive);
}

void TestTagRules::hookWouldSkipMirrorsThePostNewRefusal()
{
    // Mirrors PROTECTED_REMOVALS in assets/hooks/post-new, which the Python
    // suite tests against the hook itself.
    TagRule rule;
    rule.remove = { QStringLiteral("inbox") };
    QVERIFY(TagRules::hookWouldSkip(rule));
    rule.allowArchive = true;
    QVERIFY(!TagRules::hookWouldSkip(rule));

    rule.remove = { QStringLiteral("unread") };
    QVERIFY(TagRules::hookWouldSkip(rule));   // never opted out

    rule.remove = { QStringLiteral("promo") };
    rule.allowArchive = false;
    QVERIFY(!TagRules::hookWouldSkip(rule));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -m3 error`
Expected: `'struct TagRule' has no member named 'allowArchive'`.

- [ ] **Step 3: Implement the C++ reader and writer**

In `src/tagrules.h`, in `struct TagRule`, after `bool enabled = true;`:

```cpp
    /// Lets the post-new hook remove `inbox` from arriving mail for this rule
    /// (item 213). Meaningless, and cleared on save, unless `remove` holds
    /// `inbox`. `unread` stays refused regardless.
    bool allowArchive = false;
```

In `class TagRules`, beside `validate()`:

```cpp
    /// Whether the post-new hook will skip this rule whole. Mirrors
    /// PROTECTED_REMOVALS in assets/hooks/post-new: `unread` always, `inbox`
    /// unless the rule opts in with allowArchive.
    static bool hookWouldSkip(const TagRule &rule);
```

In `src/tagrules.cpp`:

1. `isKnownKey`: add `QStringLiteral("allow_archive"),` to the list.
2. In `load()`, after `rule.note = ...`:

```cpp
        rule.allowArchive =
            object.value(QStringLiteral("allow_archive")).toBool(false);
```

3. In `save()`, after `object.insert(QStringLiteral("note"), rule.note);`:

```cpp
        // Written only where it does something, so a file without an
        // archiving rule saves byte-identical (item 213).
        if (rule.allowArchive && rule.remove.contains(QStringLiteral("inbox")))
            object.insert(QStringLiteral("allow_archive"), true);
```

4. New function, after `validate()`:

```cpp
bool TagRules::hookWouldSkip(const TagRule &rule)
{
    if (rule.remove.contains(QStringLiteral("unread")))
        return true;
    return rule.remove.contains(QStringLiteral("inbox")) && !rule.allowArchive;
}
```

- [ ] **Step 4: Run C++ tests**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules`
Expected: all PASS.

- [ ] **Step 5: Write the failing Python tests**

Append to `assets/hooks/test_mailrules.py`, above `def run_all():`:

```python
def test_allow_archive_round_trips_and_is_omitted_when_false():
    """Item 213. Known, so not carried as unknown; written only when true."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "rules": [
                {"id": "promo", "add": ["promo"], "remove": ["inbox"],
                 "query": "from:a@example.com", "allow_archive": True},
                {"id": "plain", "add": ["x"], "query": "from:b@example.com"},
            ],
        })
        store = mailrules.load(path)
        assert store.rules[0].allow_archive is True
        assert store.rules[1].allow_archive is False
        assert "allow_archive" not in store.rules[0].unknown

        mailrules.save(store, path)
        raw = json.loads(path.read_text())
        assert raw["rules"][0]["allow_archive"] is True
        assert "allow_archive" not in raw["rules"][1]


def test_allow_archive_on_a_rule_keeping_inbox_is_accepted_and_ignored():
    """Never a reason to drop a rule: the hook runs unattended."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "rules": [{"id": "x", "add": ["x"], "query": "from:a@example.com",
                       "allow_archive": True}],
        })
        store = mailrules.load(path)
        assert len(store.rules) == 1
        assert store.warnings == []
```

- [ ] **Step 6: Run to verify it fails**

Run: `cd assets/hooks && ./test_mailrules.py`
Expected: `AttributeError: 'Rule' object has no attribute 'allow_archive'`.

- [ ] **Step 7: Implement the Python reader and writer**

In `assets/hooks/mailrules.py`:

1. `KNOWN_KEYS = {"id", "stage", "enabled", "add", "remove", "query", "note", "allow_archive"}`
2. In `class Rule`, after `note: str = ""`: `allow_archive: bool = False`
3. In `_parse_rule`'s `return Rule(...)`, after the `note=` line:

```python
        allow_archive=obj.get("allow_archive") is True,
```

4. In `_rule_to_dict`, before `out.update(rule.unknown)`:

```python
    # Written only where it does something, matching src/tagrules.cpp, so a
    # file without an archiving rule round-trips unchanged (item 213).
    if rule.allow_archive and "inbox" in rule.remove:
        out["allow_archive"] = True
```

- [ ] **Step 8: Run both suites**

Run: `cd assets/hooks && ./test_mailrules.py && ./test_post_new.py; cd ../.. && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules | tail -1`
Expected: `all passed` twice, and `0 failed`.

- [ ] **Step 9: Commit**

```bash
git add src/tagrules.h src/tagrules.cpp tests/test_tagrules.cpp assets/hooks/mailrules.py assets/hooks/test_mailrules.py
git commit -S -m "feat: an optional allow_archive field on a tagging rule

Both readers learn it together, as the rule format requires. No version
bump: the field is optional, and each writer emits it only on a rule that
removes inbox, so a file without one saves unchanged.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: the hook honours `allow_archive`

**Files:**
- Modify: `assets/hooks/post-new` (`protected_removals`, the skip log line, the header comments that describe the policy)
- Test: `assets/hooks/test_post_new.py`

- [ ] **Step 1: Write the failing tests**

Add above `def setup_accounts(` in `assets/hooks/test_post_new.py`:

```python
def test_an_opted_in_rule_archives_arriving_mail():
    """Item 213: the promo rule removed inbox and was skipped on every sync,
    so arrivals were never even tagged."""
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [
            {"id": "promo", "stage": 50, "add": ["promo"],
             "remove": ["inbox"], "allow_archive": True,
             "query": "from:promo@example.net"},
        ])
        result = subprocess.run([str(HOOK)], env=env, capture_output=True,
                                text=True)
        assert result.returncode == 0, result.stderr
        assert count(env, "tag:promo") == 1
        assert count(env, "tag:inbox and from:promo@example.net") == 0
        # unread is untouched by an archive
        assert count(env, "tag:unread and from:promo@example.net") == 1
        assert count(env, "tag:inbox") == 2
        assert count(env, "tag:new") == 0


def test_without_the_flag_the_rule_is_skipped_and_the_log_says_how_to_allow_it():
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [
            {"id": "promo", "stage": 50, "add": ["promo"],
             "remove": ["inbox"], "query": "from:promo@example.net"},
        ])
        result = subprocess.run([str(HOOK)], env=env, capture_output=True,
                                text=True)
        assert result.returncode == 0, result.stderr
        assert count(env, "tag:promo") == 0
        assert "Archive on arrival" in result.stderr, result.stderr


def test_the_flag_never_unlocks_unread():
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [
            {"id": "greedy", "stage": 50, "add": ["seen"],
             "remove": ["inbox", "unread"], "allow_archive": True,
             "query": "from:promo@example.net"},
        ])
        result = subprocess.run([str(HOOK)], env=env, capture_output=True,
                                text=True)
        assert result.returncode == 0, result.stderr
        assert count(env, "tag:seen") == 0
        assert count(env, "tag:unread") == 3
        assert count(env, "tag:inbox") == 3
```

- [ ] **Step 2: Run to verify they fail**

Run: `cd assets/hooks && ./test_post_new.py`
Expected: `AssertionError` in `test_an_opted_in_rule_archives_arriving_mail` (`tag:promo` is 0).

- [ ] **Step 3: Implement**

In `assets/hooks/post-new`, replace `protected_removals`:

```python
def protected_removals(rule):
    """The protected tags this rule would remove, if any.

    `inbox` is released by the rule's own `allow_archive` opt-in (item 213),
    which a human set in the rules dialog. `unread` never is: removing it
    rewrites Maildir filenames and reaches the server.
    """
    refused = set(PROTECTED_REMOVALS)
    if rule.allow_archive:
        refused.discard("inbox")
    return sorted(refused.intersection(rule.remove))
```

Replace the skip log call in `main()`:

```python
        if refused:
            hint = (' (set "Archive on arrival" on the rule to allow it)'
                    if refused == ["inbox"] else "")
            log(f"rule '{rule.id}' would remove {', '.join(refused)}; "
                f"skipped, this hook does not remove those unattended{hint}")
            continue
```

Update the comment block above `PROTECTED_REMOVALS`: replace the paragraph starting `DELIBERATELY CONSERVATIVE, AND EXPECTED TO BE RELAXED.` (through `... not whether the guard is annoying.`) with:

```python
# RELAXED ONCE, FOR INBOX ONLY (item 213). A rule may opt in with
# `allow_archive`, set by a human in the rules dialog, and then this hook
# removes `inbox` for it. That checkbox is the confirmation the old comment
# here asked for before relaxing anything. `unread` has no opt-in.
```

Update the module docstring line 23-24 so it reads: `Rules may add any tag and remove most, but this hook REFUSES to remove \`unread\`, and \`inbox\` unless the rule sets allow_archive, and skips any rule that asks: see PROTECTED_REMOVALS`.

- [ ] **Step 4: Run both suites**

Run: `cd assets/hooks && ./test_post_new.py && ./test_mailrules.py`
Expected: `all passed` twice. The existing `test_a_protected_removal_is_skipped_whole_and_the_run_continues` must still pass (its rule removes `unread`).

- [ ] **Step 5: Commit**

```bash
git add assets/hooks/post-new assets/hooks/test_post_new.py
git commit -S -m "fix: let an opted-in rule archive arriving mail

The hook refused to remove inbox by skipping the whole rule, so the promo
rule (+promo -inbox) had never tagged a single arrival. A rule carrying
allow_archive now runs, and inbox is removed for it. unread is still
refused, and a rule without the flag is still skipped, with a log line
saying how to allow it.

Item 213.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: worker slot `applyTagsToQuery`

**Files:**
- Modify: `src/notmuchworker.h` (public slots, beside `applyTagsToThreads`)
- Modify: `src/notmuchworker.cpp` (after `applyTagsToThreads`)
- Test: `tests/test_notmuchworker.cpp`

- [ ] **Step 1: Write the failing test**

Add to `private slots:` in `tests/test_notmuchworker.cpp`:

```cpp
    void applyTagsToQueryWritesOnlyWhatWouldChange();
```

Body, beside `applyTagsToThreadsTagsEveryMessage`:

```cpp
void TestNotmuchWorker::applyTagsToQueryWritesOnlyWhatWouldChange()
{
    // Item 212. Two messages in OPPOSITE states (item 87's lesson): a1
    // already carries the tag, a2 does not. Only a2 may be reported, or the
    // undo would strip the tag from a message that had it before.
    const QString threadId = threadIdOf(QStringLiteral("Release notes"));
    QVERIFY(!threadId.isEmpty());

    NotmuchWorker setup(m_fixture.configPath());
    setup.applyTags(TagChange{ { QStringLiteral("a1@example.org") },
                               { QStringLiteral("backfill") }, {},
                               QStringLiteral("seed") });

    NotmuchWorker worker(m_fixture.configPath());
    QSignalSpy applied(&worker, &NotmuchWorker::tagsApplied);
    worker.applyTagsToQuery(QStringLiteral("thread:%1").arg(threadId),
                            { QStringLiteral("backfill") }, {},
                            QStringLiteral("Apply rule"));

    QCOMPARE(applied.size(), 1);
    const TagChange emitted = applied.first().at(0).value<TagChange>();
    QCOMPARE(emitted.messageIds, QStringList{ QStringLiteral("a2@example.org") });
    QCOMPARE(emitted.description, QStringLiteral("Apply rule"));
    QVERIFY(tagsOf(QStringLiteral("a2@example.org"))
                .contains(QStringLiteral("backfill")));

    NotmuchWorker restore(m_fixture.configPath());
    restore.applyTags(TagChange{ { QStringLiteral("a1@example.org"),
                                   QStringLiteral("a2@example.org") },
                                 {}, { QStringLiteral("backfill") },
                                 QStringLiteral("restore") });
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -m3 error`
Expected: `'class NotmuchWorker' has no member named 'applyTagsToQuery'`.

- [ ] **Step 3: Implement**

`src/notmuchworker.h`, in the same public-slots block as `applyTagsToThreads`:

```cpp
    /// Applies `+add -remove` to every message the change would move within
    /// `query` (item 212, a rule applied to existing mail). The query is
    /// narrowed with SearchTerm::changes() so already-correct messages are
    /// never written, then the ids go through applyTags(), which reports the
    /// effective set for undo. Emits nothing when nothing would change.
    void applyTagsToQuery(const QString &query, const QStringList &add,
                          const QStringList &remove,
                          const QString &description);
```

`src/notmuchworker.cpp`: add `#include "searchterm.h"` with the other project includes if absent (remember gmime/notmuch headers stay before Qt headers; project headers already follow that order in this file, so put it beside the other `#include "..."` lines). Then, after `applyTagsToThreads`:

```cpp
void NotmuchWorker::applyTagsToQuery(const QString &query,
                                     const QStringList &add,
                                     const QStringList &remove,
                                     const QString &description)
{
    if (!openReadOnly())
        return;

    QStringList messageIds;
    if (!collectMessageIds(m_db, SearchTerm::changes(query, add, remove),
                           &messageIds)) {
        emit errorOccurred(QStringLiteral("Cannot resolve the rule's query"));
        return;
    }

    // Empty is not an error: the rule has nothing left to do. applyTags()
    // returns at once on an empty list, and stays silent, as it must
    // (CLAUDE.md, item 176).
    applyTags(TagChange{ messageIds, add, remove, description });
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_notmuchworker`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp tests/test_notmuchworker.cpp
git commit -S -m "feat: apply a tag change to every message a query would move

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: undo bookkeeping stays linear on a large write

**Files:**
- Modify: `src/mainwindow.h` (`TagCommand::addChangedMessageIds`, `forgetChangedMessageIds`)

No test of its own: a performance property, and Task 7's test exercises the code path.

- [ ] **Step 1: Make `addChangedMessageIds` linear**

It runs `QStringList::contains` per id, which is quadratic on a 10k-message backfill (and on item 211's 10k archive). Replace the method and the member in `class TagCommand`:

```cpp
    void addChangedMessageIds(const QStringList &ids)
    {
        // A set beside the list, so a 10k-message write is not quadratic
        // here (items 211, 212). The list keeps the order for the undo.
        for (const QString &id : ids) {
            if (!m_seen.contains(id)) {
                m_seen.insert(id);
                m_changedMessageIds.append(id);
            }
        }
    }
```

```cpp
    void forgetChangedMessageIds()
    {
        m_changedMessageIds.clear();
        m_seen.clear();
    }

private:
    QStringList m_changedMessageIds;
    QSet<QString> m_seen;
```

(`QSet` is already included by `mainwindow.h`.)

No redo change: the application has no Redo action (it is an open wish in
the user's notes), so `MessageTagCommand::redo()` after the first push is
unreachable. When Redo is built, a command constructed without ids (Task 7)
must replay `changedMessageIds()`; Task 8 records that in AGENTS.md.

- [ ] **Step 2: Build and run the main suite**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow 2>&1 | grep -E "^FAIL|^Totals"`
Expected: only the 4 known failures.

- [ ] **Step 3: Commit**

```bash
git add src/mainwindow.h
git commit -S -m "fix: keep undo bookkeeping linear on large writes

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: the rules dialog, Apply button, archive checkbox, skip warning

**Files:**
- Modify: `src/tagrulesdialog.h`
- Modify: `src/tagrulesdialog.cpp`
- Test: `tests/test_tagrules.cpp` (the dialog tests live here)

- [ ] **Step 1: Write the failing tests**

Add to `private slots:`:

```cpp
    void applySavesFirstAndEmitsTheSavedRule();
    void applyEmitsNothingWhenTheSaveFails();
    void theArchiveBoxMapsToAllowArchive();
    void aRuleTheHookWillSkipIsNamedInTheWarning();
```

Bodies, before `QTEST_MAIN`. Each points `XDG_CONFIG_HOME` at its own temp dir, as the existing dialog tests do:

```cpp
static QString writeDialogRules(const QTemporaryDir &home, const QByteArray &json)
{
    QDir().mkpath(home.filePath(QStringLiteral("mailrules")));
    const QString path = home.filePath(QStringLiteral("mailrules/rules.json"));
    QFile out(path);
    if (out.open(QIODevice::WriteOnly))
        out.write(json);
    return path;
}

void TestTagRules::applySavesFirstAndEmitsTheSavedRule()
{
    // Item 212, decided with the user: only what the hook would run is
    // applied, so Apply writes the file before asking for anything.
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    const QString path = writeDialogRules(home, R"({"version": 1, "rules": [
        {"id": "promo", "add": ["promo"], "query": "from:a@example.org"}]})");

    TagRulesDialog dialog;
    QSignalSpy spy(&dialog, &TagRulesDialog::applyRequested);
    dialog.selectRuleForTest(0);
    dialog.setTagsForTest(QStringLiteral("promo, sale"));   // unsaved edit
    dialog.applyForTest();

    QCOMPARE(spy.size(), 1);
    const TagRule emitted = spy.first().at(0).value<TagRule>();
    QCOMPARE(emitted.add, (QStringList{ QStringLiteral("promo"),
                                        QStringLiteral("sale") }));

    TagRules onDisk;
    onDisk.load(path);
    QCOMPARE(onDisk.rules().at(0).add, emitted.add);
}

void TestTagRules::applyEmitsNothingWhenTheSaveFails()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    writeDialogRules(home, R"({"version": 1, "rules": [
        {"id": "promo", "add": ["promo"], "query": "from:a@example.org"}]})");

    TagRulesDialog dialog;
    QSignalSpy spy(&dialog, &TagRulesDialog::applyRequested);
    dialog.selectRuleForTest(0);
    dialog.setTagsForTest(QString());        // adds and removes nothing
    dialog.applyForTest();

    QVERIFY(spy.isEmpty());
    QVERIFY(!dialog.warningTextForTest().isEmpty());
}

void TestTagRules::theArchiveBoxMapsToAllowArchive()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    const QString path = writeDialogRules(home, R"({"version": 1, "rules": [
        {"id": "promo", "add": ["promo"], "remove": ["inbox"],
         "query": "from:a@example.org"}]})");

    {
        TagRulesDialog dialog;
        dialog.selectRuleForTest(0);
        dialog.setArchiveForTest(true);
        dialog.saveForTest();
    }
    TagRules onDisk;
    onDisk.load(path);
    QVERIFY(onDisk.rules().at(0).allowArchive);
}

void TestTagRules::aRuleTheHookWillSkipIsNamedInTheWarning()
{
    // CLAUDE.md: a rule the hook will refuse must be flagged in the UI.
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    writeDialogRules(home, R"({"version": 1, "rules": [
        {"id": "promo", "add": ["promo"], "remove": ["inbox"],
         "query": "from:a@example.org"},
        {"id": "fine", "add": ["x"], "query": "from:b@example.org"}]})");

    TagRulesDialog dialog;
    QVERIFY2(dialog.warningTextForTest().contains(QStringLiteral("promo")),
             qPrintable(dialog.warningTextForTest()));
    QVERIFY(!dialog.warningTextForTest().contains(QStringLiteral("fine")));

    dialog.selectRuleForTest(0);
    dialog.setArchiveForTest(true);
    QVERIFY2(!dialog.warningTextForTest().contains(QStringLiteral("promo")),
             qPrintable(dialog.warningTextForTest()));
}
```

`TagRule` must be a metatype for `QSignalSpy::value<TagRule>()`: add `Q_DECLARE_METATYPE(TagRule)` at the bottom of `src/tagrules.h`, outside the class, and `#include <QMetaType>` there.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -m3 error`
Expected: `'applyRequested' is not a member of 'TagRulesDialog'`.

- [ ] **Step 3: Header changes**

In `src/tagrulesdialog.h`:

- Test seams, beside `saveForTest()`:

```cpp
    void applyForTest() { onApply(); }
    void setArchiveForTest(bool on);
```

- Signal, after `previewRequested`:

```cpp
    /// Asks the owner to apply one SAVED rule to existing mail (item 212).
    /// Emitted only after the dialog has written rules.json, so the rule
    /// applied is exactly the rule the hook will run.
    void applyRequested(const TagRule &rule);
```

- Private slots: add `void onApply();`.
- Private: add `bool saveRules();` and `QString hookSkipWarning() const;`.
- Members: `QCheckBox *m_archive = nullptr;` and `QPushButton *m_applyButton = nullptr;`.

- [ ] **Step 4: Split saving from closing**

In `src/tagrulesdialog.cpp`, replace `onSave()` with:

```cpp
bool TagRulesDialog::saveRules()
{
    applyEditsToCurrentRule();

    const QStringList problems = TagRules::validate(m_working);
    if (!problems.isEmpty()) {
        setWarning(tr("%n rule(s) cannot be saved as they are: %1", "",
                      problems.size())
                       .arg(problems.join(QStringLiteral("; "))));
        return false;
    }

    m_rules.setRules(m_working);
    if (!m_rules.save()) {
        QMessageBox::warning(this, tr("Tagging rules"),
                             tr("Could not write %1.")
                                 .arg(TagRules::defaultPath()));
        return false;
    }
    return true;
}

void TagRulesDialog::onSave()
{
    if (saveRules())
        accept();
}

void TagRulesDialog::onApply()
{
    // Saved first, decided with the user (item 212): the rule applied is the
    // rule the hook runs, and a failed save stops here exactly as OK does.
    if (!saveRules())
        return;
    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;
    emit applyRequested(m_working.at(index));
}
```

Keep any comment that sat inside the old `onSave()` between `applyEditsToCurrentRule();` and `validate`: move it into `saveRules()` at the same place.

- [ ] **Step 5: The Apply button**

In the constructor, after the `m_previewButton` block (`buttons->addWidget(m_previewButton);`):

```cpp
    m_applyButton = new QPushButton(tr("&Apply to existing mail"), this);
    m_applyButton->setToolTip(
        tr("Save, then apply this rule to mail already in the index. Asks "
           "first, naming how many messages would change. Undoable from the "
           "main window."));
    buttons->addWidget(m_applyButton);
```

With the other connects:

```cpp
    connect(m_applyButton, &QPushButton::clicked, this, &TagRulesDialog::onApply);
```

Enable it only for a rule worth applying. At the end of `onSelectionChanged()` and at the end of `applyEditsToCurrentRule()`:

```cpp
    if (index >= 0 && index < m_working.size()) {
        const TagRule &current = m_working.at(index);
        m_applyButton->setEnabled(current.enabled && !current.query.isEmpty()
                                  && !(current.add.isEmpty()
                                       && current.remove.isEmpty()));
    }
```

(Both functions already compute `index` from `currentIndex()`; reuse that variable.)

- [ ] **Step 6: The archive checkbox**

In the constructor, right after `m_remove` is created and added to the form (find `form->addRow(tr("Remove tags"), m_remove);`):

```cpp
    m_archive = new QCheckBox(
        tr("Archive matching mail on arrival (removes inbox)"), this);
    m_archive->setToolTip(
        tr("Without this, the sync hook skips a rule that removes inbox, "
           "since it runs unattended."));
    form->addRow(QString(), m_archive);
    connect(m_remove, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_archive->setVisible(splitTags(text).contains(QStringLiteral("inbox")));
    });
    connect(m_archive, &QCheckBox::toggled,
            this, &TagRulesDialog::applyEditsToCurrentRule);
```

In `onSelectionChanged()`, beside `m_remove->setText(...)`:

```cpp
    m_archive->setChecked(rule.allowArchive);
    m_archive->setVisible(rule.remove.contains(QStringLiteral("inbox")));
```

In `applyEditsToCurrentRule()`, after `rule.remove = splitTags(m_remove->text());` (find the line that assigns `rule.remove`):

```cpp
    // Cleared in the editor when inbox leaves the list, so a flag that does
    // nothing never reaches the file (item 213).
    rule.allowArchive = m_archive->isChecked()
                        && rule.remove.contains(QStringLiteral("inbox"));
```

Seam:

```cpp
void TagRulesDialog::setArchiveForTest(bool on)
{
    m_archive->setChecked(on);   // toggled -> applyEditsToCurrentRule
}
```

`splitTags` is the file's existing helper used by `applyEditsToCurrentRule`; if it is a member or a static in an anonymous namespace, the lambda can call it the same way.

- [ ] **Step 7: The skip warning**

Add:

```cpp
QString TagRulesDialog::hookSkipWarning() const
{
    QStringList skipped;
    for (const TagRule &rule : m_working) {
        if (rule.enabled && TagRules::hookWouldSkip(rule))
            skipped.append(rule.id);
    }
    if (skipped.isEmpty())
        return {};
    return tr("The sync hook skips %n rule(s), which remove inbox or unread "
              "on mail nobody has looked at: %1. Tick \"Archive matching "
              "mail on arrival\" to let a rule remove inbox; unread is never "
              "removed on arrival.", "", skipped.size())
        .arg(skipped.join(QStringLiteral(", ")));
}
```

Replace `showWarnings()` so both sources share the one banner:

```cpp
void TagRulesDialog::showWarnings()
{
    QStringList parts;
    const QStringList warnings = m_rules.warnings();
    if (!warnings.isEmpty()) {
        parts.append(tr("%n rule(s) in the file need attention: %1. Save to "
                        "write them back.", "", warnings.size())
                         .arg(warnings.join(QStringLiteral("; "))));
    }
    const QString skips = hookSkipWarning();
    if (!skips.isEmpty())
        parts.append(skips);
    setWarning(parts.join(QStringLiteral(" ")));
}
```

Keep the comment that sat between the early return and the old `setWarning(tr("%n rule(s) in the file need attention...` call, above the `if (!warnings.isEmpty())`.

At the END of `applyEditsToCurrentRule()`, re-evaluate only when the skip text actually changed, so a dismissed banner is not resurrected by every keystroke:

```cpp
    const QString skips = hookSkipWarning();
    if (skips != m_lastSkipWarning) {
        m_lastSkipWarning = skips;
        showWarnings();
    }
```

Add the member `QString m_lastSkipWarning;` and set `m_lastSkipWarning = hookSkipWarning();` at the end of `reloadList()` (where `showWarnings()` is first called on load).

- [ ] **Step 8: Run**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_tagrules 2>&1 | grep -E "^FAIL|^Totals"`
Expected: `0 failed`. If an existing warning test compares the banner text exactly and now sees a skip sentence appended, check its fixture: a rule removing `inbox` without the flag legitimately gets the new sentence. Do not weaken the new assertions.

- [ ] **Step 9: Commit**

```bash
git add src/tagrulesdialog.h src/tagrulesdialog.cpp src/tagrules.h tests/test_tagrules.cpp
git commit -S -m "feat: apply a rule from the rules dialog, and opt a rule into archiving

Apply saves first, so only what the hook would run is applied, then asks
the owner to apply the saved rule. A checkbox maps to allow_archive, and
the banner names every rule the sync hook will skip.

Items 212 and 213.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: `MainWindow` counts, confirms, writes, and undoes

**Files:**
- Modify: `src/mainwindow.h`
- Modify: `src/mainwindow.cpp` (`showTagRulesDialog`, `onRuleCountsReady`, `announceAction`, new `applyRuleToExistingMail`)
- Test: `tests/test_mainwindow.cpp`

**Two deviations from the spec, deliberate.** First, the rule is written one tag at a time inside one undo macro, because the worker's effective set is per message, not per tag (see `ruleOperations()` below): a single `+promo -inbox` write would make the undo strip `promo` from messages that had it before. Second, the spec said a backfill during a sync is held and replayed like a list edit. `HeldEdit` carries message or thread ids, and a backfill has neither until the worker resolves the query. Instead, Apply during a sync is refused with a status message and does nothing; the user presses it again after the sync. Record this in the spec in Task 8.

- [ ] **Step 1: Write the failing test**

Add to `private slots:` beside `aWriteSaysItIsApplyingUntilTheWorkerHasRunIt`:

```cpp
    void applyingARuleConfirmsWithTheCountAndUndoesExactly();
```

Body, beside that test:

```cpp
void TestMainWindow::applyingARuleConfirmsWithTheCountAndUndoesExactly()
{
    // Items 212 and 176. Two matching messages in OPPOSITE states: `had`
    // already carries promo, `lacked` does not. The confirmation must count
    // one, and undo must leave `had` tagged.
    WorkerBackedWindow backed;
    const QString date = QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200");
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("had@example.org"),
        QStringLiteral("One"), QStringLiteral("deals@example.net"), date,
        QStringLiteral("Body.")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("lacked@example.org"),
        QStringLiteral("Two"), QStringLiteral("deals@example.net"), date,
        QStringLiteral("Body.")));
    QVERIFY2(backed.build(), qPrintable(backed.error()));

    const auto notmuch = [&backed](const QStringList &args) {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("NOTMUCH_CONFIG"),
                   backed.fixture().configPath());
        p.setProcessEnvironment(env);
        p.start(QStringLiteral("notmuch"), args);
        p.waitForFinished();
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    };
    notmuch({ QStringLiteral("tag"), QStringLiteral("+promo"), QStringLiteral("--"),
              QStringLiteral("id:had@example.org") });

    MainWindow window(backed.config());
    auto *undo = window.findChild<QAction *>(QStringLiteral("undo"));
    QVERIFY(undo);

    TagRule rule;
    rule.id = QStringLiteral("promo");
    rule.query = QStringLiteral("from:deals@example.net");
    rule.add = { QStringLiteral("promo") };
    rule.remove = { QStringLiteral("inbox") };

    // Answer the confirmation when it appears, recording what it said.
    QString asked;
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        asked = box->text();
        poll.stop();
        box->button(QMessageBox::Yes)->click();
    });
    poll.start();

    window.applyRuleToExistingMail(rule);
    QTRY_VERIFY_WITH_TIMEOUT(!asked.isEmpty(), 15000);
    // The whole rule's count: both messages still carry inbox, so both
    // would move, even though `had` already has promo.
    QVERIFY2(asked.contains(QStringLiteral("2")), qPrintable(asked));

    QTRY_COMPARE_WITH_TIMEOUT(
        notmuch({ QStringLiteral("count"), QStringLiteral("tag:inbox") }),
        QStringLiteral("0"), 15000);

    undo->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(
        notmuch({ QStringLiteral("count"), QStringLiteral("tag:inbox") }),
        QStringLiteral("2"), 15000);
    // `had` carried promo before the backfill, so undo must not take it off.
    QCOMPARE(notmuch({ QStringLiteral("count"),
                       QStringLiteral("tag:promo and id:had@example.org") }),
             QStringLiteral("1"));
    QCOMPARE(notmuch({ QStringLiteral("count"),
                       QStringLiteral("tag:promo and id:lacked@example.org") }),
             QStringLiteral("0"));
}
```

The undo action is named `undo` (`src/keymap.cpp`). Add `#include <QMessageBox>` and `#include <QProcess>` to the test file if absent, and `#include "tagrules.h"`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build 2>&1 | grep -m3 error`
Expected: `'class MainWindow' has no member named 'applyRuleToExistingMail'`.

- [ ] **Step 3: `announceAction` learns an optional completion callback**

In `src/mainwindow.h`, change the declaration to:

```cpp
    void announceAction(const QString &text,
                        const std::function<void()> &onDone = {});
```

In `src/mainwindow.cpp`, change the definition's signature to match, and in the worker round trip capture and run it (the bare-window branch runs it at once):

```cpp
        if (!m_worker) {
            showTransientStatus(text);
            if (onDone)
                onDone();
            return;
        }
```

```cpp
        QMetaObject::invokeMethod(m_worker, [this, text, ticket, onDone]() {
            QMetaObject::invokeMethod(this, [this, text, ticket, onDone]() {
                if (onDone)
                    onDone();
                // A newer announcement, or an error, owns the label now.
                if (ticket == m_announceTicket)
                    showTransientStatus(tr("%1, done").arg(text));
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
```

`onDone` runs whatever the ticket says: it is the caller's follow-up (a refresh), not a label. The sync-held branch at the bottom of `announceAction` does not run it; nothing calls it with a callback while a sync holds the lock (Step 4 refuses first).

- [ ] **Step 4: `applyRuleToExistingMail` and the count reply**

`src/mainwindow.h`, public slots (it is driven by the dialog and by the test):

```cpp
    /// Item 212. Counts what `rule` would change across the whole index,
    /// confirms with that count, then writes through the worker with exact
    /// undo. The rule has already been saved by the dialog.
    void applyRuleToExistingMail(const TagRule &rule);
```

Private members, beside `m_ruleCountGeneration`:

```cpp
    /// The rule awaiting its change count, and the generation that count
    /// will carry. Shares m_ruleCountGeneration's sequence, so a pending
    /// Apply and the dialog's Count matches can never be confused.
    TagRule m_pendingApplyRule;
    quint64 m_pendingApplyGeneration = 0;
```

Add `#include "tagrules.h"` to `mainwindow.h` if absent.

`src/mainwindow.cpp`, in `showTagRulesDialog`, beside the other dialog connects:

```cpp
    connect(dialog, &TagRulesDialog::applyRequested,
            this, &MainWindow::applyRuleToExistingMail);
```

New function, after `onRuleCountsReady`:

```cpp
void MainWindow::applyRuleToExistingMail(const TagRule &rule)
{
    if (!m_worker)
        return;

    // Refused rather than held: HeldEdit replays ids, and a backfill has none
    // until the worker resolves the query. A deliberate deviation from the
    // spec, recorded there.
    if (aSyncHoldsTheWriteLock()) {
        showTransientStatus(
            tr("A sync is running; apply the rule again when it finishes."));
        return;
    }

    // One count for the whole rule (what the confirmation names), then one per
    // single-tag operation, in the order ruleOperations() lists them. The
    // per-operation counts decide which writes are sent at all; see
    // confirmRuleApply() for why the rule is split.
    QStringList queries{ SearchTerm::changes(rule.query, rule.add, rule.remove) };
    for (const auto &op : ruleOperations(rule))
        queries.append(SearchTerm::changes(rule.query, op.first, op.second));

    m_pendingApplyRule = rule;
    m_pendingApplyGeneration = ++m_ruleCountGeneration;
    QMetaObject::invokeMethod(
        m_worker, "requestMessageCounts", Qt::QueuedConnection,
        Q_ARG(QStringList, queries),
        Q_ARG(quint64, m_pendingApplyGeneration));
}
```

And a file-local helper at the top of `mainwindow.cpp`'s anonymous namespace (create one beside the other file-local helpers if needed):

```cpp
/// A rule as single-tag operations: `+a`, `+b`, `-c`, each as (add, remove).
/// Item 212: the worker's effective set is per MESSAGE, not per tag, so one
/// `+promo -inbox` write reports a message that already had promo (it lost
/// inbox) and its undo would strip promo from it. One operation per tag makes
/// each effective set, and therefore each undo, exact.
QList<QPair<QStringList, QStringList>> ruleOperations(const TagRule &rule)
{
    QList<QPair<QStringList, QStringList>> ops;
    for (const QString &tag : rule.add)
        ops.append({ { tag }, {} });
    for (const QString &tag : rule.remove)
        ops.append({ {}, { tag } });
    return ops;
}
```

Replace the body of `onRuleCountsReady` so it routes the Apply count first (keep its existing comment above the dialog branch):

```cpp
void MainWindow::onRuleCountsReady(const QVector<int> &counts,
                                   quint64 generation)
{
    if (m_pendingApplyGeneration != 0
        && generation == m_pendingApplyGeneration) {
        m_pendingApplyGeneration = 0;
        confirmRuleApply(counts);
        return;
    }

    if (generation != m_ruleCountGeneration || !m_tagRulesDialog)
        return;

    m_tagRulesDialog->setCounts(counts);
}
```

Private helper, declared in the header beside `applyRuleToExistingMail` (private section): `void confirmRuleApply(const QVector<int> &counts);`

```cpp
void MainWindow::confirmRuleApply(const QVector<int> &counts)
{
    const TagRule rule = m_pendingApplyRule;
    const int count = counts.value(0, 0);
    if (count <= 0) {
        showTransientStatus(
            tr("Rule \"%1\" has nothing to change.").arg(rule.id));
        return;
    }

    // The one confirmation for a reversible write, and why (CLAUDE.md): the
    // scope is the whole index, so the user cannot see what it will touch.
    QStringList what;
    if (!rule.add.isEmpty())
        what.append(tr("Adds: %1.").arg(rule.add.join(QStringLiteral(", "))));
    if (!rule.remove.isEmpty())
        what.append(tr("Removes: %1.").arg(rule.remove.join(QStringLiteral(", "))));

    QWidget *parent = m_tagRulesDialog ? static_cast<QWidget *>(m_tagRulesDialog)
                                       : this;
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Apply rule"));
    box.setText(tr("Apply rule \"%1\" to %n message(s)?", "", count).arg(rule.id));
    box.setInformativeText(what.join(QStringLiteral(" "))
                           + QStringLiteral("\n")
                           + tr("You can undo this from the main window."));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
        return;

    const QString description = tr("Apply rule \"%1\"").arg(rule.id);

    // One write per single-tag operation, grouped so one Ctrl+Z reverts the
    // whole rule (see ruleOperations() for why it is split). An operation
    // whose count was 0 is NOT sent: applyTags() stays silent on an empty
    // write, and an armed command that never hears back would steal the next
    // write's confirmation. Commands are built with no ids; the worker
    // resolves them and the confirmation fills each one (item 176).
    const auto ops = ruleOperations(rule);
    m_undoStack.beginMacro(description);
    bool first = true;
    for (int i = 0; i < ops.size(); ++i) {
        if (counts.value(i + 1, 0) <= 0)
            continue;
        auto *command = new MessageTagCommand(this, {}, ops.at(i).first,
                                              ops.at(i).second, description);
        awaitTagConfirmation(command, first);
        first = false;
        QMetaObject::invokeMethod(
            m_worker, "applyTagsToQuery", Qt::QueuedConnection,
            Q_ARG(QString, rule.query), Q_ARG(QStringList, ops.at(i).first),
            Q_ARG(QStringList, ops.at(i).second), Q_ARG(QString, description));
        m_undoStack.push(command);
    }
    m_undoStack.endMacro();

    // The rows a backfill touches are mostly not in the model, so the list is
    // re-queried once the write has run rather than updated optimistically.
    announceAction(tr("%1: %n message(s)", "", count).arg(description),
                   [this]() { refreshCurrentQuery(); });
}
```

Add `#include "searchterm.h"` to `mainwindow.cpp` if absent.

**Verified while planning:** `awaitTagConfirmation(command, first)` keeps a FIFO; `first = true` clears anything an earlier gesture stranded, so the loop passes `true` for the first operation sent and `false` after, which is the "both halves of one gesture" usage `tagSelected()` already has. `QUndoStack::push()` calls `redo()` on each child inside a macro too, which `m_firstRedo` turns into the no-op it expects.

**Residual gap, accepted:** an operation counted > 0 can still move nothing if another writer (a sync, the hook) changed those messages between the count and the write. Its command then never hears back and steals a later confirmation. The count is taken a moment before, so this is a race rather than a path. Do not invent new plumbing for it; Task 8 records it in AGENTS.md.

- [ ] **Step 5: Run**

Run: `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow applyingARuleConfirmsWithTheCountAndUndoesExactly 2>&1 | grep -E "^(PASS|FAIL)|Totals"`
Expected: PASS.

Then the whole binary: `QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow 2>&1 | grep -E "^FAIL|^Totals"` shows only the 4 known failures.

- [ ] **Step 6: Mutation check**

Temporarily make `ruleOperations()` return ONE operation, `{ rule.add, rule.remove }`, so the rule is written in a single call. Rebuild and run the test: the assertion that `had` keeps `promo` after undo must FAIL. That is the defect the split exists for. Restore, rebuild, confirm PASS.

- [ ] **Step 7: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp tests/test_mainwindow.cpp
git commit -S -m "feat: apply a saved rule to existing mail, confirmed and undoable

The dialog's Apply reaches MainWindow, which counts the messages the rule
would actually move, confirms with that count, then writes through the
worker. The undo entry is filled from the confirmation, so it reverts
exactly what moved. Refused while a sync holds the write lock.

Item 212.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: documents, translations, the hand round trip

**Files:**
- Modify: `AGENTS.md`
- Modify: `docs/superpowers/specs/2026-10-31-rule-apply-and-archive-design.md`
- Modify: `CHANGELOG.md`
- Modify: `translations/qtmaildir_it_IT.ts`
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md` and `-closed.md`

- [ ] **Step 1: AGENTS.md, the confirmation rule**

In the paragraph beginning `**There is exactly ONE exception, and its shape is the rule's own logic rather than a hole in it.**`, change `exactly ONE exception` to `two exceptions` and append after the sentence ending `...unless the action is likewise irreversible.`:

```markdown
The second is **a write scoped to the whole index rather than to rows on
screen**: applying a tagging rule to existing mail (item 212). It IS undoable,
through the same exact-undo path as every tag write, and it confirms anyway,
naming the count, because the user cannot see what it will touch. The two
exceptions have different reasons and must keep them: `empty_trash` confirms
because it cannot be undone, Apply because it cannot be seen.
```

In "Changing the rule format", under the paragraph `**Two hook properties are safety-critical...`, replace `The hook refuses to remove \`unread\` or \`inbox\`` with `The hook refuses to remove \`unread\`, and \`inbox\` unless the rule sets \`allow_archive\` (item 213),`. Delete the closing paragraph `Backfill, applying a rule to existing mail, is deliberately unbuilt. See ...` and replace it with:

```markdown
Backfill exists since item 212 as the dialog's **Apply to existing mail**, and
it runs in qtmaildir, attended and confirmed, never in the hook. **The rule is
written one tag at a time, grouped in one undo macro, and that is
load-bearing**: `applyTags()` reports which MESSAGES moved, not which tags, so
one `+promo -inbox` write reports a message that already had `promo` (it lost
`inbox`), and its undo strips `promo` from it. One write per tag makes every
effective set exact. Each command is built with no ids and filled from its confirmation; the
application has no Redo yet, and when it gets one, such a command must redo
`changedMessageIds()`, not its empty constructor list. An operation counted at 0
is not sent, since a silent write would leave its armed command stealing the
next confirmation; a write racing another writer between count and send can
still do that, and is accepted rather than plumbed.
```

- [ ] **Step 2: The spec**

In the spec's Part A, item 4, replace the bullet `- the write lock: if a sync holds it, the edit is held and replayed the same way a list edit is.` with:

```markdown
  - the write lock: Apply during a sync is REFUSED with a status message.
    Changed during planning: `HeldEdit` replays message or thread ids, and a
    backfill has neither until the worker resolves its query.
```

Also in the spec's Part A, item 4, after the line `calls the existing \`applyTags()\`. Everything downstream is inherited`, add a paragraph:

```markdown
  **Written one tag at a time** (changed during planning). `applyTags()`
  reports which messages moved, not which tags, so one `+promo -inbox` write
  would list a message that already had `promo`, and undo would strip it.
  Each tag is its own write and its own undo command, grouped in one undo
  macro, and only the operations whose own count is above zero are sent.
```

- [ ] **Step 3: CHANGELOG**

Under `## [Unreleased]`, in `### Added`:

```markdown
- **Apply to existing mail** in the tagging rules dialog. It saves the
  rules, counts the messages the selected rule would actually change, asks
  with that count, and applies it. Undo reverts exactly what moved.
- **Archive matching mail on arrival**, a per-rule checkbox. A rule that
  removes `inbox` used to be skipped whole by the sync hook; with the box
  ticked it runs, and `inbox` is removed. `unread` is never removed on
  arrival. The dialog now names every rule the hook will skip.
```

And under `### Upgrading` (create it below the version summary area of `[Unreleased]`, after `### Fixed`, if absent):

```markdown
- A rule that removes `inbox` was never applied by the sync hook. Open the
  rules dialog: such rules are named in the warning. Tick "Archive matching
  mail on arrival" on each one you want to run, then use "Apply to existing
  mail" to catch up on what arrived meanwhile.
```

- [ ] **Step 4: Translations**

Run: `lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete -locations none`
Expected: zero "cannot be called without context" warnings. Fill every `type="unfinished"` entry with Italian. Suggested:

| English | Italian |
|---|---|
| `&Apply to existing mail` | `&Applica alla posta esistente` |
| `Save, then apply this rule ...` (tooltip) | `Salva, poi applica questa regola alla posta già indicizzata. Chiede prima, indicando quanti messaggi cambierebbero. Annullabile dalla finestra principale.` |
| `Archive matching mail on arrival (removes inbox)` | `Archivia all'arrivo la posta corrispondente (rimuove inbox)` |
| `Without this, the sync hook skips ...` | `Senza questa opzione, l'hook di sincronizzazione salta le regole che rimuovono inbox, perché agisce senza supervisione.` |
| `The sync hook skips %n rule(s) ...` | numerus forms: `L'hook di sincronizzazione salta %n regola, che rimuove inbox o unread su posta che nessuno ha visto: %1. Spunta "Archivia all'arrivo la posta corrispondente" per permettere a una regola di rimuovere inbox; unread non viene mai rimosso all'arrivo.` and the plural `... salta %n regole, che rimuovono ...` |
| `A sync is running; apply the rule again when it finishes.` | `Sincronizzazione in corso; applica di nuovo la regola al termine.` |
| `Rule "%1" has nothing to change.` | `La regola "%1" non ha nulla da cambiare.` |
| `Adds: %1.` / `Removes: %1.` | `Aggiunge: %1.` / `Rimuove: %1.` |
| `Apply rule` | `Applica regola` |
| `Apply rule "%1" to %n message(s)?` | `Applicare la regola "%1" a %n messaggio?` / `... a %n messaggi?` |
| `You can undo this from the main window.` | `Puoi annullare dalla finestra principale.` |
| `Apply rule "%1"` | `Applica regola "%1"` |

Then `cmake --build build 2>&1 | grep -i unfinished` must show `0 unfinished`, and `ctest --test-dir build -R translations --output-on-failure` must pass.

- [ ] **Step 5: Full suites**

Run:
```bash
ctest --test-dir build -j8 2>&1 | tail -5
cd assets/hooks && ./test_mailrules.py && ./test_post_new.py && cd ../..
```
Expected: only `mainwindow` fails, with only the 4 known failures; both Python suites `all passed`.

- [ ] **Step 6: Hand round trip (rule-format step 4)**

No automated test spans the C++ and the Python. In a scratch dir:

```bash
S=$(mktemp -d)
mkdir -p "$S/Mail/"{new,cur,tmp} "$S/config/mailrules"
printf 'From: promo@example.net\nTo: you@example.org\nSubject: sale\nMessage-ID: <s1@example.net>\nDate: Fri, 14 Aug 2026 10:00:00 +0200\n\nbody\n' > "$S/Mail/new/1"
printf '[database]\npath=%s/Mail\n[new]\ntags=new;unread;inbox\n[user]\nname=T\nprimary_email=you@example.org\n' "$S" > "$S/nm"
export NOTMUCH_CONFIG="$S/nm" XDG_CONFIG_HOME="$S/config"
notmuch new
```

Write a rule file FROM THE C++ SIDE, using a tiny throwaway test or by calling `TagRules::save` from the dialog test harness: the quickest route is to copy the JSON that `allowArchiveRoundTripsAndIsOmittedWhenFalse` saves, adding a `"note": "kept"`. Then:

```bash
./assets/hooks/post-new; echo "exit $?"
notmuch count tag:promo           # expect 1
notmuch count tag:inbox           # expect 0
python3 -c "import json,os;d=json.load(open(os.environ['XDG_CONFIG_HOME']+'/mailrules/rules.json'));r=d['rules'][0];print(r.get('allow_archive'),r.get('note'))"   # expect: True kept
```

Report the measured counts in the commit message.

- [ ] **Step 7: Backlog**

Mark items 212 and 213 `**done** 2026-10-31, unreleased` in the status table with a one-line summary each, and move both `## 212.` and `## 213.` sections from the backlog to `2026-08-03-post-0.1.0-usability-closed.md` on this commit, per `AGENTS.md`.

- [ ] **Step 8: Commit**

```bash
git add AGENTS.md CHANGELOG.md translations/qtmaildir_it_IT.ts docs/
git commit -S -m "docs: record rule apply and archive-on-arrival

The no-confirmation rule gains its second exception, by property: a write
scoped to the whole index confirms with the count, though it can be undone.
Hand round trip: <paste the measured counts>.

Items 212 and 213.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 9: Hand over**

Do not launch the application. Tell the user what to look at: open the rules dialog, see `promo` named in the warning, tick "Archive matching mail on arrival", press "Apply to existing mail", read the count, confirm, watch "applying… / done", and Ctrl+Z.
