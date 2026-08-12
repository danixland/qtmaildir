# Shared Tagging Rules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the notmuch auto-tagging rules out of a hand-written shell hook into `~/.config/mailrules/rules.json`, a store both `mailctl` and `qtmaildir` read, and give qtmaildir a dialog to view, edit and dry-run them.

**Architecture:** One JSON file is the source of truth. `mailctl` owns the shared Python library (`mailrules.py`) and the `post-new` hook that executes rules scoped to `tag:new`. qtmaildir owns a C++ reader/writer (`TagRules`) and the management dialog. A rule stores no scope, so the same rule serves the hook, a dry run, and a future backfill. Both readers preserve JSON fields they do not recognise, so neither tool owns the format.

**Tech Stack:** Python 3 stdlib only (`json`, `pathlib`, `argparse`, `subprocess`) in `../mailctl`. Qt 6 (`QJsonDocument`, `QSaveFile`, `QDialog`) and `Qt6::Test` in this repo. `notmuch` CLI for the hook, `libnotmuch` via the existing `NotmuchWorker` for dry-run counts.

**Spec:** `docs/superpowers/specs/2026-08-12-tagging-rules-design.md`

**Repos:** Tasks 1-7 are in `../mailctl`. Tasks 8-13 are in this repo. Task 14 is the migration and touches neither repo's committed code.

---

## Correction to the spec, made before writing this plan

The spec says qtmaildir's dry-run adds "a new worker slot, `countRules`". While
planning, `NotmuchWorker::requestCounts(const QStringList &, quint64)` was found
to already exist (`src/notmuchworker.h:131`), emitting
`countsReady(const QVector<int> &, quint64)`. It is generation-stamped and was
built for the placeholder pane.

It cannot be reused unchanged: it calls `notmuch_query_count_threads`
(`src/notmuchworker.cpp:637`). A tagging rule tags MESSAGES, and a thread count
misreports every rule that matches a few replies inside large threads. Task 10
therefore adds a message-counting slot beside it rather than a rule-specific
one, which keeps the worker's vocabulary general.

## File structure

| File | Repo | Responsibility |
|---|---|---|
| `mailrules.py` | mailctl | Load, validate, order, save `rules.json`. Unknown-key preservation. No notmuch calls. |
| `post-new` | mailctl | Hook. Loads rules, applies them scoped to `tag:new`, consumes `tag:new` last. |
| `mailctl.py` | mailctl | Adds the read-only `rules` subcommand. Modified, not created. |
| `test_mailrules.py` | mailctl | Plain-assert tests for `mailrules.py`. |
| `src/tagrules.h/.cpp` | qtmaildir | The same format in C++: parse, validate, save atomically. No widgets. |
| `src/tagrulesdialog.h/.cpp` | qtmaildir | The management dialog. |
| `tests/test_tagrules.cpp` | qtmaildir | Format tests. |
| `src/notmuchworker.h/.cpp` | qtmaildir | Adds `requestMessageCounts`. Modified. |
| `src/mainwindow.cpp` | qtmaildir | Menu action opening the dialog. Modified. |

---

## Task 1: The rules file loads

**Files:**
- Create: `../mailctl/mailrules.py`
- Test: `../mailctl/test_mailrules.py`

- [ ] **Step 1: Write the failing test**

Create `../mailctl/test_mailrules.py`:

```python
#!/usr/bin/env python3
#
# Copyright (C) 2026 Danilo M. <danix@danix.xyz>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""Self-checks for mailrules.py, the shared tagging-rule store.

The risk in this file is the format, not the notmuch calls: a rule that
silently loses a field on save, or one that sorts into the wrong stage,
mis-tags real mail on the next sync and does it quietly.

Run: ./test_mailrules.py
"""

import json
import tempfile
from pathlib import Path

import mailrules


def write_rules(tmp, payload):
    path = Path(tmp) / "rules.json"
    path.write_text(json.dumps(payload))
    return path


def test_loads_a_rule():
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "rules": [
                {
                    "id": "notify-forge",
                    "stage": 50,
                    "enabled": True,
                    "add": ["notify/forge"],
                    "remove": [],
                    "query": "from:notifications@example.com",
                    "note": "All repositories, not one project.",
                }
            ],
        })
        store = mailrules.load(path)
        assert store.warnings == [], store.warnings
        assert len(store.rules) == 1
        rule = store.rules[0]
        assert rule.id == "notify-forge"
        assert rule.stage == 50
        assert rule.enabled is True
        assert rule.add == ["notify/forge"]
        assert rule.remove == []
        assert rule.query == "from:notifications@example.com"
        assert rule.note == "All repositories, not one project."


def test_defaults_are_applied():
    """stage, enabled, remove and note are all optional in the file."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "rules": [{"id": "minimal", "add": ["x"],
                       "query": "from:someone@example.com"}],
        })
        store = mailrules.load(path)
        assert store.warnings == [], store.warnings
        rule = store.rules[0]
        assert rule.stage == 50
        assert rule.enabled is True
        assert rule.remove == []
        assert rule.note == ""


def run_all():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print(f"ok  {name}")


if __name__ == "__main__":
    run_all()
    print("\nall passed")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: FAIL with `ModuleNotFoundError: No module named 'mailrules'`

- [ ] **Step 3: Write minimal implementation**

Create `../mailctl/mailrules.py`:

```python
#!/usr/bin/env python3
#
# Copyright (C) 2026 Danilo M. <danix@danix.xyz>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""Shared notmuch tagging-rule store.

The rules live in ~/.config/mailrules/rules.json and are read by both this
tool and qtmaildir, so the format belongs to neither: a field one tool does
not understand is preserved verbatim across a save by the other.

A rule carries NO scope. The post-new hook supplies `tag:new`, a dry run
supplies nothing and counts against the whole corpus. This is what lets one
rule serve arrivals, a dry run, and (later) a backfill over history.

Stdlib only, deliberately: this module is imported by a notmuch hook that
runs on every sync, and mailctl has no dependencies to inherit.
"""

import json
import os
from dataclasses import dataclass, field
from pathlib import Path

FORMAT_VERSION = 1
DEFAULT_STAGE = 50

# Fields this version understands. Anything else in a rule object is kept in
# `unknown` and written back untouched, which is what makes the file neutral
# rather than this tool's file that another program may read.
KNOWN_KEYS = {"id", "stage", "enabled", "add", "remove", "query", "note"}


@dataclass
class Rule:
    id: str
    query: str
    add: list = field(default_factory=list)
    remove: list = field(default_factory=list)
    stage: int = DEFAULT_STAGE
    enabled: bool = True
    note: str = ""
    unknown: dict = field(default_factory=dict)


@dataclass
class Store:
    rules: list = field(default_factory=list)
    warnings: list = field(default_factory=list)
    unknown: dict = field(default_factory=dict)


def default_path():
    """$XDG_CONFIG_HOME/mailrules/rules.json, or ~/.config/... as fallback.

    No hardcoded home directory: both tools must resolve the same path, and
    a user with XDG_CONFIG_HOME set expects it honoured.
    """
    base = os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config"
    return Path(base) / "mailrules" / "rules.json"


def load(path=None):
    """Read the store. Never raises for a bad file: problems land in
    Store.warnings and the offending rule is dropped, so one malformed rule
    cannot stop the other nineteen from running."""
    path = Path(path) if path else default_path()
    store = Store()

    raw = json.loads(path.read_text())

    for obj in raw.get("rules", []):
        store.rules.append(Rule(
            id=obj["id"],
            query=obj["query"],
            add=list(obj.get("add", [])),
            remove=list(obj.get("remove", [])),
            stage=int(obj.get("stage", DEFAULT_STAGE)),
            enabled=bool(obj.get("enabled", True)),
            note=obj.get("note", ""),
            unknown={k: v for k, v in obj.items() if k not in KNOWN_KEYS},
        ))

    return store
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: PASS, printing `ok  test_defaults_are_applied`, `ok  test_loads_a_rule`, `all passed`

- [ ] **Step 5: Commit**

```bash
cd ../mailctl
chmod +x mailrules.py test_mailrules.py
git add mailrules.py test_mailrules.py
git commit -S -m "feat(rules): load a shared tagging-rule store

The rules that tag incoming mail live in a notmuch post-new hook as
hand-written shell. This is the first piece of moving them into a JSON
store that both this tool and qtmaildir read.

A rule carries no scope: the hook supplies tag:new, a dry run supplies
nothing. Unknown fields are kept per rule so the format belongs to
neither tool."
```

---

## Task 2: A malformed rule is dropped with a warning, not fatal

**Files:**
- Modify: `../mailctl/mailrules.py`
- Test: `../mailctl/test_mailrules.py`

- [ ] **Step 1: Write the failing tests**

Add to `../mailctl/test_mailrules.py`, above `run_all()`:

```python
def test_a_bad_rule_is_dropped_and_the_rest_survive():
    """One malformed rule must not stop the others. The hook runs every ten
    minutes on real mail; losing all tagging because of one typo is worse
    than losing one rule."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "rules": [
                {"id": "good", "add": ["x"], "query": "from:a@example.com"},
                {"id": "no-query", "add": ["y"]},
                {"id": "no-tags", "query": "from:b@example.com"},
                {"id": "bad id!", "add": ["z"], "query": "from:c@example.com"},
                {"add": ["w"], "query": "from:d@example.com"},
            ],
        })
        store = mailrules.load(path)
        assert [r.id for r in store.rules] == ["good"]
        assert len(store.warnings) == 4, store.warnings
        joined = " ".join(store.warnings)
        assert "no-query" in joined
        assert "no-tags" in joined
        assert "bad id!" in joined


def test_duplicate_ids_keep_the_first():
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "rules": [
                {"id": "dup", "add": ["first"], "query": "from:a@example.com"},
                {"id": "dup", "add": ["second"], "query": "from:b@example.com"},
            ],
        })
        store = mailrules.load(path)
        assert len(store.rules) == 1
        assert store.rules[0].add == ["first"]
        assert any("dup" in w for w in store.warnings)


def test_a_missing_file_is_empty_not_an_error():
    """qtmaildir must open on a machine that has never written this file."""
    with tempfile.TemporaryDirectory() as tmp:
        store = mailrules.load(Path(tmp) / "absent.json")
        assert store.rules == []
        assert store.warnings == []
        assert store.missing is True


def test_unparseable_json_warns_and_yields_no_rules():
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "rules.json"
        path.write_text("{not json")
        store = mailrules.load(path)
        assert store.rules == []
        assert len(store.warnings) == 1
        assert store.failed is True


def test_a_newer_format_version_is_refused():
    """Guessing at semantics a later version defined is how a rule silently
    changes meaning. Refuse instead."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 2,
            "rules": [{"id": "x", "add": ["a"], "query": "from:a@example.com"}],
        })
        store = mailrules.load(path)
        assert store.rules == []
        assert store.failed is True
        assert any("version" in w for w in store.warnings)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: FAIL with `KeyError: 'query'` on the first new test

- [ ] **Step 3: Write the implementation**

In `../mailctl/mailrules.py`, add `re` to the imports:

```python
import json
import os
import re
from dataclasses import dataclass, field
from pathlib import Path
```

Add after `KNOWN_KEYS`:

```python
# An id is a handle, not a display name: a UI selects on it and a diff tracks
# it. Tags may contain '/' and may be renamed; ids may not.
ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
```

Replace the `Store` dataclass:

```python
@dataclass
class Store:
    rules: list = field(default_factory=list)
    warnings: list = field(default_factory=list)
    unknown: dict = field(default_factory=dict)
    # Distinguishes "no file yet" from "a file that would not load". The hook
    # treats them differently: the first is a fresh install, the second must
    # not consume tag:new.
    missing: bool = False
    failed: bool = False
```

Replace `load()` entirely:

```python
def load(path=None):
    """Read the store. Never raises for a bad file: problems land in
    Store.warnings and the offending rule is dropped, so one malformed rule
    cannot stop the other nineteen from running."""
    path = Path(path) if path else default_path()
    store = Store()

    if not path.exists():
        store.missing = True
        return store

    try:
        raw = json.loads(path.read_text())
    except (json.JSONDecodeError, OSError) as exc:
        store.warnings.append(f"{path}: cannot read: {exc}")
        store.failed = True
        return store

    if not isinstance(raw, dict):
        store.warnings.append(f"{path}: top level is not an object")
        store.failed = True
        return store

    version = raw.get("version", FORMAT_VERSION)
    if version != FORMAT_VERSION:
        store.warnings.append(
            f"{path}: format version {version} is newer than this tool "
            f"understands ({FORMAT_VERSION}); refusing to guess")
        store.failed = True
        return store

    store.unknown = {k: v for k, v in raw.items()
                     if k not in ("version", "rules")}

    seen = set()
    for index, obj in enumerate(raw.get("rules", [])):
        rule = _parse_rule(obj, index, seen, store.warnings)
        if rule is not None:
            seen.add(rule.id)
            store.rules.append(rule)

    return store


def _parse_rule(obj, index, seen, warnings):
    """One rule, or None with a warning appended. `index` names the rule when
    it has no usable id of its own."""
    where = f"rule #{index + 1}"

    if not isinstance(obj, dict):
        warnings.append(f"{where}: not an object; dropped")
        return None

    rule_id = obj.get("id", "")
    if not isinstance(rule_id, str) or not ID_RE.match(rule_id):
        warnings.append(
            f"{where}: id '{rule_id}' is missing or not lowercase "
            f"letters, digits and dashes; dropped")
        return None

    if rule_id in seen:
        warnings.append(f"rule '{rule_id}': duplicate id; keeping the first")
        return None

    query = obj.get("query", "")
    if not isinstance(query, str) or not query.strip():
        warnings.append(f"rule '{rule_id}': no query; dropped")
        return None

    add = [t for t in obj.get("add", []) if isinstance(t, str) and t.strip()]
    remove = [t for t in obj.get("remove", []) if isinstance(t, str) and t.strip()]
    if not add and not remove:
        warnings.append(
            f"rule '{rule_id}': adds and removes nothing; dropped")
        return None

    try:
        stage = int(obj.get("stage", DEFAULT_STAGE))
    except (TypeError, ValueError):
        warnings.append(
            f"rule '{rule_id}': stage '{obj.get('stage')}' is not a "
            f"number; using {DEFAULT_STAGE}")
        stage = DEFAULT_STAGE

    return Rule(
        id=rule_id,
        query=query,
        add=add,
        remove=remove,
        stage=stage,
        enabled=bool(obj.get("enabled", True)),
        note=obj.get("note", "") if isinstance(obj.get("note", ""), str) else "",
        unknown={k: v for k, v in obj.items() if k not in KNOWN_KEYS},
    )
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: PASS, 7 `ok` lines then `all passed`

- [ ] **Step 5: Commit**

```bash
cd ../mailctl
git add mailrules.py test_mailrules.py
git commit -S -m "feat(rules): drop a malformed rule rather than the whole file

One typo must not stop the other rules from tagging. A missing file and
a file that will not parse are tracked separately: the hook may run on
the first and must refuse on the second."
```

---

## Task 3: Rules run in stage order

**Files:**
- Modify: `../mailctl/mailrules.py`
- Test: `../mailctl/test_mailrules.py`

- [ ] **Step 1: Write the failing test**

Add to `../mailctl/test_mailrules.py`:

```python
def test_ordered_sorts_by_stage_then_file_position():
    """Account tags must run before topic rules. Ties keep file order, so
    the file still reads as a sequence."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "rules": [
                {"id": "topic-b", "stage": 50, "add": ["b"],
                 "query": "from:b@example.com"},
                {"id": "account", "stage": 10, "add": ["acct"],
                 "query": "path:\"work/**\""},
                {"id": "topic-a", "stage": 50, "add": ["a"],
                 "query": "from:a@example.com"},
            ],
        })
        store = mailrules.load(path)
        assert [r.id for r in mailrules.ordered(store.rules)] == [
            "account", "topic-b", "topic-a"]


def test_ordered_skips_disabled_rules():
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "rules": [
                {"id": "on", "add": ["a"], "query": "from:a@example.com"},
                {"id": "off", "add": ["b"], "query": "from:b@example.com",
                 "enabled": False},
            ],
        })
        store = mailrules.load(path)
        assert [r.id for r in mailrules.ordered(store.rules)] == ["on"]
        # The disabled rule is still LOADED, so a UI can show and re-enable it.
        assert [r.id for r in store.rules] == ["on", "off"]
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: FAIL with `AttributeError: module 'mailrules' has no attribute 'ordered'`

- [ ] **Step 3: Write the implementation**

Add to `../mailctl/mailrules.py`:

```python
def ordered(rules):
    """Enabled rules in execution order: by stage ascending, ties by position.

    `sorted` is stable, so sorting on stage alone preserves file order within
    a stage. That is the tie-break the format promises, and it is why this
    does not sort on (stage, id): an id-sorted tie would reorder rules a user
    deliberately sequenced.
    """
    return sorted([r for r in rules if r.enabled], key=lambda r: r.stage)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: PASS, 9 `ok` lines

- [ ] **Step 5: Commit**

```bash
cd ../mailctl
git add mailrules.py test_mailrules.py
git commit -S -m "feat(rules): order rules by stage, ties by file position

Account tags must run before topic rules, which is what the stage field
is for. Disabled rules stay loaded so a UI can re-enable them."
```

---

## Task 4: Saving preserves fields this version does not know

**Files:**
- Modify: `../mailctl/mailrules.py`
- Test: `../mailctl/test_mailrules.py`

- [ ] **Step 1: Write the failing test**

Add to `../mailctl/test_mailrules.py`:

```python
def test_save_round_trips_unknown_fields():
    """The neutrality guarantee. If this tool strips a field qtmaildir
    added, the file is this tool's file that qtmaildir may read."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_rules(tmp, {
            "version": 1,
            "future_top_level": {"set_by": "another tool"},
            "rules": [{
                "id": "keeper",
                "add": ["x"],
                "query": "from:a@example.com",
                "future_field": [1, 2, 3],
            }],
        })
        store = mailrules.load(path)
        assert store.rules[0].unknown == {"future_field": [1, 2, 3]}

        mailrules.save(store, path)

        raw = json.loads(path.read_text())
        assert raw["future_top_level"] == {"set_by": "another tool"}
        assert raw["rules"][0]["future_field"] == [1, 2, 3]
        assert raw["rules"][0]["id"] == "keeper"
        assert raw["version"] == 1


def test_save_is_atomic():
    """A reader must never see a half-written file: the hook runs every ten
    minutes and a truncated read would be a failed sync."""
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "rules.json"
        store = mailrules.Store(rules=[
            mailrules.Rule(id="a", query="from:a@example.com", add=["x"])])
        mailrules.save(store, path)
        # The temp file the write went through must not be left behind.
        assert [p.name for p in Path(tmp).iterdir()] == ["rules.json"]
        assert json.loads(path.read_text())["rules"][0]["id"] == "a"


def test_save_creates_the_directory():
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "nested" / "rules.json"
        mailrules.save(mailrules.Store(), path)
        assert path.exists()
        assert json.loads(path.read_text()) == {"version": 1, "rules": []}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: FAIL with `AttributeError: module 'mailrules' has no attribute 'save'`

- [ ] **Step 3: Write the implementation**

Add `import tempfile` to `../mailctl/mailrules.py`'s imports, then add:

```python
def save(store, path=None):
    """Write the store atomically: a temp file in the same directory, then
    rename. Rename within a filesystem is atomic, so a concurrent reader sees
    either the old file or the new one and never a partial write.

    There is no locking. Last writer wins on a true collision, which is
    accepted for a single-user setup; the failure that would actually hurt is
    a truncated read by the hook, and rename eliminates it.
    """
    path = Path(path) if path else default_path()
    path.parent.mkdir(parents=True, exist_ok=True)

    payload = dict(store.unknown)
    payload["version"] = FORMAT_VERSION
    payload["rules"] = [_rule_to_dict(r) for r in store.rules]

    # delete=False plus an explicit replace: NamedTemporaryFile would unlink
    # the file on close, and the rename is the whole point.
    handle = tempfile.NamedTemporaryFile(
        mode="w", dir=path.parent, prefix=".rules-", suffix=".tmp",
        delete=False)
    try:
        with handle:
            json.dump(payload, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(handle.name, path)
    except BaseException:
        # A failed write must not leave the temp file beside the real one.
        try:
            os.unlink(handle.name)
        except OSError:
            pass
        raise


def _rule_to_dict(rule):
    """Known fields first in a stable order, then anything this version did
    not understand. Stable ordering keeps a diff of this file readable."""
    out = {
        "id": rule.id,
        "stage": rule.stage,
        "enabled": rule.enabled,
        "add": list(rule.add),
        "remove": list(rule.remove),
        "query": rule.query,
        "note": rule.note,
    }
    out.update(rule.unknown)
    return out
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: PASS, 12 `ok` lines

- [ ] **Step 5: Commit**

```bash
cd ../mailctl
git add mailrules.py test_mailrules.py
git commit -S -m "feat(rules): save atomically, preserving unknown fields

Write to a temp file and rename, so the hook can never read a partial
file. Fields this version does not understand round-trip untouched,
which is what makes the format belong to neither tool."
```

---

## Task 5: The scoped query the hook will run

**Files:**
- Modify: `../mailctl/mailrules.py`
- Test: `../mailctl/test_mailrules.py`

- [ ] **Step 1: Write the failing test**

Add to `../mailctl/test_mailrules.py`:

```python
def test_scoped_query_parenthesises_the_rule():
    """Without the parentheses `tag:new and a or b` binds as
    `(tag:new and a) or b`, and the rule matches every message in the corpus
    satisfying b rather than only new arrivals. Several real rules are a
    disjunction of senders, so this is the difference between tagging four
    messages and tagging four thousand."""
    rule = mailrules.Rule(
        id="disjunction",
        query="from:a@example.com or from:b@example.com",
        add=["promo"])
    assert mailrules.scoped_query(rule, "tag:new") == (
        "tag:new and (from:a@example.com or from:b@example.com)")


def test_scoped_query_with_no_scope_is_the_bare_query():
    """A dry run counts against the whole corpus, which is what makes the
    same rule answer 'what would this tag on arrival' and 'what does this
    match in all my mail'."""
    rule = mailrules.Rule(id="x", query="from:a@example.com", add=["y"])
    assert mailrules.scoped_query(rule, None) == "from:a@example.com"
    assert mailrules.scoped_query(rule, "") == "from:a@example.com"


def test_tag_arguments():
    rule = mailrules.Rule(id="x", query="from:a@example.com",
                          add=["one", "two"], remove=["three"])
    assert mailrules.tag_arguments(rule) == ["+one", "+two", "-three"]
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: FAIL with `AttributeError: module 'mailrules' has no attribute 'scoped_query'`

- [ ] **Step 3: Write the implementation**

Add to `../mailctl/mailrules.py`:

```python
def scoped_query(rule, scope):
    """The rule's query narrowed by `scope`, or the bare query when scope is
    empty.

    The parentheses are load-bearing. notmuch's `and` binds tighter than
    `or`, so `tag:new and a or b` means `(tag:new and a) or b`: a rule that
    is a disjunction of senders would escape its scope and match the whole
    corpus. Do not remove them, and do not build this string anywhere else.
    """
    if not scope:
        return rule.query
    return f"{scope} and ({rule.query})"


def tag_arguments(rule):
    """The +tag/-tag arguments for `notmuch tag`, adds before removes."""
    return [f"+{t}" for t in rule.add] + [f"-{t}" for t in rule.remove]
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd ../mailctl && ./test_mailrules.py`
Expected: PASS, 15 `ok` lines

- [ ] **Step 5: Commit**

```bash
cd ../mailctl
git add mailrules.py test_mailrules.py
git commit -S -m "feat(rules): build the scoped query in one place

A rule stores no scope. The hook supplies tag:new, a dry run supplies
nothing. The parentheses around the rule's own query are what stop a
disjunction of senders from escaping the scope and matching everything."
```

---

## Task 6: The post-new hook

**Files:**
- Create: `../mailctl/post-new`
- Test: manual, per the steps below

This task has no unit test. The hook's logic is a loop over already-tested
functions plus `subprocess` calls to `notmuch`; a test would mostly assert that
mocks were called. Its real risks (does it refuse when rules fail to load, does
it consume `tag:new` only on success) are verified against a throwaway notmuch
database in Task 7.

- [ ] **Step 1: Write the hook**

Create `../mailctl/post-new`:

```python
#!/usr/bin/env python3
#
# Copyright (C) 2026 Danilo M. <danix@danix.xyz>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""notmuch post-new hook: auto-tag incoming mail from the shared rule store.

Runs after every `notmuch new`. Reads ~/.config/mailrules/rules.json and
applies each enabled rule scoped to `tag:new`, in stage order, then consumes
the `tag:new` marker.

TAG-ONLY in practice: the rules currently in use add tags and remove none.
Nothing here forbids a `remove`, but note that touching `unread` would rewrite
Maildir filenames and propagate to IMAP on the next mbsync, because
maildir.synchronize_flags is true.

Requires `new` in [new] tags= in ~/.notmuch-config. Without it every scoped
query matches nothing and this silently no-ops.

Install: copy to <database.path>/.notmuch/hooks/post-new, with mailrules.py
importable (same directory, or on PYTHONPATH).
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mailrules

SCOPE = "tag:new"


def log(message):
    print(f"post-new: {message}", file=sys.stderr)


def run_tag(arguments, query):
    result = subprocess.run(["notmuch", "tag"] + arguments + ["--", query],
                            capture_output=True, text=True)
    if result.returncode != 0:
        log(f"notmuch tag failed: {result.stderr.strip()}")
        return False
    return True


def main():
    store = mailrules.load()

    # A file that will not load must NOT reach the consumer below. If the
    # marker were cleared while the rules did not run, that mail could never
    # be tagged by these rules again: the failure is silent, permanent, and
    # invisible until someone notices a gap months later. Leaving tag:new in
    # place makes the next successful run catch up instead.
    if store.failed:
        for warning in store.warnings:
            log(warning)
        log("rules did not load; leaving tag:new in place")
        return 1

    if store.missing:
        log("no rules file; nothing to do")
        return 0

    # A dropped rule is not fatal, but it must be visible: this goes to the
    # sync log, which is where someone looks when a tag stops appearing.
    for warning in store.warnings:
        log(warning)

    rules = mailrules.ordered(store.rules)
    for rule in rules:
        query = mailrules.scoped_query(rule, SCOPE)
        if not run_tag(mailrules.tag_arguments(rule), query):
            log(f"rule '{rule.id}' failed; leaving tag:new in place")
            return 1

    # Only after every rule succeeded. A failure part way through leaves the
    # marker set, so re-running the hook is safe and finishes the work.
    if not run_tag(["-new"], SCOPE):
        return 1

    log(f"applied {len(rules)} rule(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Verify it refuses to run with no rules file**

Run:
```bash
cd ../mailctl && chmod +x post-new
XDG_CONFIG_HOME=/tmp/no-such-dir ./post-new; echo "exit=$?"
```
Expected: `post-new: no rules file; nothing to do` and `exit=0`

- [ ] **Step 3: Verify it refuses on a broken rules file**

Run:
```bash
mkdir -p /tmp/rulestest/mailrules && echo '{bad' > /tmp/rulestest/mailrules/rules.json
cd ../mailctl && XDG_CONFIG_HOME=/tmp/rulestest ./post-new; echo "exit=$?"
```
Expected: a `cannot read` warning, `leaving tag:new in place`, and `exit=1`

- [ ] **Step 4: Commit**

```bash
cd ../mailctl
git add post-new
git commit -S -m "feat(rules): post-new hook driven by the rule store

Applies each enabled rule scoped to tag:new in stage order, then
consumes the marker. A rules file that will not load leaves tag:new
alone: clearing it while the rules did not run would permanently orphan
that mail."
```

---

## Task 7: The hook is verified against a throwaway database

**Files:**
- Create: `../mailctl/test_post_new.py`

- [ ] **Step 1: Write the failing test**

Create `../mailctl/test_post_new.py`:

```python
#!/usr/bin/env python3
#
# Copyright (C) 2026 Danilo M. <danix@danix.xyz>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""End-to-end checks for the post-new hook against a throwaway notmuch
database. Nothing here touches the user's real mail: NOTMUCH_CONFIG points at
a generated maildir under a temp directory.

The properties worth proving are the ones that cannot be unit-tested from
mailrules.py alone:

  - a rule actually tags the mail its query matches, and only that mail
  - the tag:new marker is consumed on success
  - the marker SURVIVES when a rule fails, so a re-run catches up

Run: ./test_post_new.py    (requires notmuch on PATH)
"""

import json
import os
import subprocess
import tempfile
from pathlib import Path

HOOK = Path(__file__).resolve().parent / "post-new"


def make_message(maildir, name, sender, subject):
    path = maildir / "new" / name
    path.write_text(
        f"From: {sender}\n"
        f"To: you@example.org\n"
        f"Subject: {subject}\n"
        f"Message-Id: <{name}@example.org>\n"
        f"Date: Mon, 11 Aug 2026 10:00:00 +0000\n"
        f"\nbody\n")


def setup_database(tmp):
    """A maildir with three messages, indexed, every message carrying the
    `new` marker the rules key off."""
    maildir = Path(tmp) / "Mail"
    for sub in ("new", "cur", "tmp"):
        (maildir / sub).mkdir(parents=True)

    make_message(maildir, "one", "notifications@example.com", "a notification")
    make_message(maildir, "two", "friend@example.org", "a real message")
    make_message(maildir, "three", "promo@example.net", "an advertisement")

    config = Path(tmp) / "notmuch-config"
    config.write_text(
        f"[database]\npath={maildir}\n\n"
        f"[new]\ntags=new;unread;inbox\n\n"
        f"[user]\nname=Test\nprimary_email=you@example.org\n")

    env = dict(os.environ)
    env["NOTMUCH_CONFIG"] = str(config)
    env["XDG_CONFIG_HOME"] = str(Path(tmp) / "config")
    subprocess.run(["notmuch", "new"], env=env, capture_output=True, check=True)
    return env


def write_rules(env, rules):
    path = Path(env["XDG_CONFIG_HOME"]) / "mailrules" / "rules.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"version": 1, "rules": rules}))


def count(env, query):
    out = subprocess.run(["notmuch", "count", query], env=env,
                         capture_output=True, text=True, check=True)
    return int(out.stdout.strip())


def test_a_rule_tags_only_what_it_matches():
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [{
            "id": "notify",
            "add": ["notify/forge"],
            "query": "from:notifications@example.com",
        }])
        assert count(env, "tag:new") == 3

        result = subprocess.run([str(HOOK)], env=env, capture_output=True,
                                text=True)
        assert result.returncode == 0, result.stderr

        assert count(env, "tag:notify/forge") == 1
        assert count(env, "tag:notify/forge and from:friend@example.org") == 0
        # The marker is consumed, so the next sync's rules see only new mail.
        assert count(env, "tag:new") == 0


def test_stage_order_is_honoured():
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [
            {"id": "late", "stage": 50, "add": ["second"],
             "query": "tag:first"},
            {"id": "early", "stage": 10, "add": ["first"],
             "query": "from:notifications@example.com"},
        ])
        subprocess.run([str(HOOK)], env=env, capture_output=True, check=True)
        # `late` matches only what `early` tagged, so a wrong order gives 0.
        assert count(env, "tag:second") == 1


def test_a_failing_rule_leaves_the_marker_in_place():
    """The property that makes a re-run safe. An invalid query fails the
    notmuch call, and tag:new must survive so the next run catches up."""
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [{
            "id": "broken",
            "add": ["x"],
            "query": "from:((((",
        }])
        result = subprocess.run([str(HOOK)], env=env, capture_output=True,
                                text=True)
        assert result.returncode == 1
        assert count(env, "tag:new") == 3


def test_a_disjunction_stays_inside_its_scope():
    """The parenthesisation guard, end to end. Both senders are already
    indexed and out of tag:new after a first run; a rule that escaped its
    scope would tag them anyway."""
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [{"id": "noop", "add": ["pass-one"],
                           "query": "from:nobody@example.invalid"}])
        subprocess.run([str(HOOK)], env=env, capture_output=True, check=True)
        assert count(env, "tag:new") == 0

        write_rules(env, [{
            "id": "disjunction",
            "add": ["promo"],
            "query": "from:friend@example.org or from:promo@example.net",
        }])
        subprocess.run([str(HOOK)], env=env, capture_output=True, check=True)
        # Nothing carries tag:new any more, so a correctly scoped rule tags
        # nothing. Unparenthesised, the `or` branch would tag one message.
        assert count(env, "tag:promo") == 0


def run_all():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print(f"ok  {name}")


if __name__ == "__main__":
    run_all()
    print("\nall passed")
```

- [ ] **Step 2: Run the test to verify it fails**

First confirm the guard test genuinely detects the bug it targets. Temporarily
break the parenthesisation in `mailrules.py`:

```python
    return f"{scope} and {rule.query}"
```

Run: `cd ../mailctl && chmod +x test_post_new.py && ./test_post_new.py`
Expected: FAIL on `test_a_disjunction_stays_inside_its_scope` with
`assert 1 == 0`

- [ ] **Step 3: Restore the correct implementation**

Put the parentheses back in `../mailctl/mailrules.py`:

```python
    return f"{scope} and ({rule.query})"
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd ../mailctl && ./test_post_new.py && ./test_mailrules.py`
Expected: both PASS, 4 `ok` lines then 15 `ok` lines

- [ ] **Step 5: Commit**

```bash
cd ../mailctl
git add test_post_new.py
git commit -S -m "test(rules): verify the hook against a throwaway database

Covers what unit tests cannot: that a rule tags only what it matches,
that stage order decides what a later rule can see, that a failed rule
leaves tag:new in place, and that a disjunction stays inside its scope.
The last one was confirmed to fail against unparenthesised code."
```

---

## Task 8: `mailctl rules list` and `show`

**Files:**
- Modify: `../mailctl/mailctl.py`
- Modify: `../mailctl/CLAUDE.md`
- Modify: `../mailctl/README.md`

- [ ] **Step 1: Add the commands**

In `../mailctl/mailctl.py`, add the import beside the existing ones:

```python
import mailrules
```

Add these functions immediately before `def build_parser():`:

```python
def cmd_rules_list(args):
    store = mailrules.load()
    for warning in store.warnings:
        print(f"warning: {warning}", file=sys.stderr)
    if store.missing:
        print(f"No rules file at {mailrules.default_path()}", file=sys.stderr)
        return
    for rule in mailrules.ordered(store.rules) if args.enabled_only \
            else store.rules:
        state = " " if rule.enabled else "-"
        tags = " ".join(mailrules.tag_arguments(rule))
        print(f"{state} {rule.stage:>4}  {rule.id:<28.28}  {tags}")
    total = len(store.rules)
    disabled = sum(1 for r in store.rules if not r.enabled)
    print(f"\n{total} rule(s), {disabled} disabled", file=sys.stderr)


def cmd_rules_show(args):
    store = mailrules.load()
    for rule in store.rules:
        if rule.id != args.id:
            continue
        print(f"id:      {rule.id}")
        print(f"stage:   {rule.stage}")
        print(f"enabled: {rule.enabled}")
        print(f"add:     {', '.join(rule.add) or '(none)'}")
        print(f"remove:  {', '.join(rule.remove) or '(none)'}")
        print(f"query:   {rule.query}")
        if rule.note:
            print(f"note:    {rule.note}")
        return
    print(f"No rule with id '{args.id}'", file=sys.stderr)
    sys.exit(1)
```

In `build_parser()`, add before the final `return p`:

```python
    sp = sub.add_parser("rules", help="inspect the shared tagging rules "
                                      "(read-only)")
    rules_sub = sp.add_subparsers(dest="rules_command", required=True)

    rp = rules_sub.add_parser("list", help="list every rule in stage order")
    rp.add_argument("--enabled-only", action="store_true",
                    help="only the rules the hook would run")
    rp.set_defaults(func=cmd_rules_list)

    rp = rules_sub.add_parser("show", help="show one rule in full")
    rp.add_argument("id")
    rp.set_defaults(func=cmd_rules_show)
```

- [ ] **Step 2: Verify against a scratch rules file**

Run:
```bash
mkdir -p /tmp/rulesdemo/mailrules
cat > /tmp/rulesdemo/mailrules/rules.json <<'JSON'
{"version": 1, "rules": [
  {"id": "account-work", "stage": 10, "add": ["account-work"],
   "query": "path:\"work/**\"", "note": "Runs before topic rules."},
  {"id": "notify-forge", "stage": 50, "add": ["notify/forge"],
   "query": "from:notifications@example.com"},
  {"id": "old-rule", "stage": 50, "add": ["x"],
   "query": "from:dead@example.com", "enabled": false}
]}
JSON
cd ../mailctl
XDG_CONFIG_HOME=/tmp/rulesdemo ./mailctl.py rules list
XDG_CONFIG_HOME=/tmp/rulesdemo ./mailctl.py rules show account-work
```

Expected: three rules listed with `-` against `old-rule`, `3 rule(s), 1 disabled`,
then the full `account-work` rule including its note.

- [ ] **Step 3: Document the commands**

In `../mailctl/README.md` and `../mailctl/CLAUDE.md`, add to the command list
under "Running":

```
./mailctl.py rules list [--enabled-only]     # shared tagging rules, read-only
./mailctl.py rules show <id>
./mailctl.py rules dry-run [<id>]            # what each rule matches now
```

In `../mailctl/CLAUDE.md`, add to the "Safety model" section:

```markdown
- **Rules are read-only from this tool.** `mailctl rules` lists, shows and
  dry-runs the shared store at `~/.config/mailrules/rules.json`, and cannot
  edit it. A rule edit is a mutation whose blast radius is every future sync,
  and the gate for that is not designed yet; qtmaildir has the editor.
  `mailrules.save()` exists because the format's write semantics must be
  shared, but no CLI surface reaches it.
```

- [ ] **Step 4: Commit**

```bash
cd ../mailctl
git add mailctl.py README.md CLAUDE.md
git commit -S -m "feat(rules): read-only rules list and show

Editing stays out of this tool: a rule edit affects every future sync
and its gate is not designed yet. qtmaildir has the editor."
```

---

## Task 9: `mailctl rules dry-run`

**Files:**
- Modify: `../mailctl/mailctl.py`

- [ ] **Step 1: Add the command**

In `../mailctl/mailctl.py`, add after `cmd_rules_show`:

```python
def cmd_rules_dry_run(args):
    """What each rule matches right now. Two numbers, because they answer
    different questions: the corpus count says whether a rule is still doing
    anything and whether it would bury real correspondence, the pending count
    says what the next sync would tag."""
    store = mailrules.load()
    rules = [r for r in store.rules if not args.id or r.id == args.id]
    if not rules:
        print(f"No rule with id '{args.id}'", file=sys.stderr)
        sys.exit(1)

    print(f"{'rule':<28}  {'corpus':>8}  {'pending':>8}")
    for rule in rules:
        corpus = run_notmuch(["count", mailrules.scoped_query(rule, None)])
        pending = run_notmuch(["count",
                               mailrules.scoped_query(rule, "tag:new")])
        state = "" if rule.enabled else "  (disabled)"
        print(f"{rule.id:<28.28}  {corpus.strip():>8}  "
              f"{pending.strip():>8}{state}")
```

In `build_parser()`, beside the other `rules` subcommands:

```python
    rp = rules_sub.add_parser("dry-run",
                              help="count what each rule matches, changing "
                                   "nothing")
    rp.add_argument("id", nargs="?", help="one rule, default is all")
    rp.set_defaults(func=cmd_rules_dry_run)
```

- [ ] **Step 2: Verify against the real index**

Run:
```bash
cd ../mailctl && XDG_CONFIG_HOME=/tmp/rulesdemo ./mailctl.py rules dry-run
```
Expected: a table of three rules with corpus counts from the real database and
`0` pending for each (nothing carries `tag:new` between syncs).

- [ ] **Step 3: Commit**

```bash
cd ../mailctl
git add mailctl.py
git commit -S -m "feat(rules): dry-run counts what a rule matches

Two numbers per rule: what it matches across all mail, and what it
would tag on the next sync. The first is what decides whether a rule
is still earning its place."
```

---

## Task 10: qtmaildir parses the same file

**Files:**
- Create: `src/tagrules.h`, `src/tagrules.cpp`
- Create: `tests/test_tagrules.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_tagrules.cpp`:

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

#include <QTemporaryDir>
#include <QtTest>

#include "tagrules.h"

/// The risk in TagRules is the format, not painting: a field silently dropped
/// on save mis-tags real mail on the next sync, and does it quietly. These
/// tests are therefore about round-trips and rejections, and the file they
/// read is byte-for-byte what mailctl writes.
class TestTagRules : public QObject
{
    Q_OBJECT

private slots:
    void aRuleLoadsWithEveryField();
    void absentFieldsTakeTheirDefaults();
    void aMalformedRuleIsDroppedWithAWarning();
    void unknownFieldsSurviveASave();
    void stageOrderPutsAccountsFirst();
    void aQueryWithQuotesRoundTrips();
    void aMissingFileIsEmptyNotAnError();
    void aNewerVersionIsRefused();

private:
    QString writeRules(const QString &json);
    QTemporaryDir m_dir;
};

QString TestTagRules::writeRules(const QString &json)
{
    const QString path = m_dir.filePath(QStringLiteral("rules.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(json.toUtf8());
    file.close();
    return path;
}

void TestTagRules::aRuleLoadsWithEveryField()
{
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [{
        "id": "notify-forge",
        "stage": 50,
        "enabled": true,
        "add": ["notify/forge"],
        "remove": [],
        "query": "from:notifications@example.com",
        "note": "All repositories, not one project."
      }]
    })");

    TagRules rules;
    rules.load(path);

    QVERIFY2(rules.warnings().isEmpty(),
             qPrintable(rules.warnings().join(QStringLiteral("; "))));
    QCOMPARE(rules.rules().size(), 1);

    const TagRule &rule = rules.rules().first();
    QCOMPARE(rule.id, QStringLiteral("notify-forge"));
    QCOMPARE(rule.stage, 50);
    QVERIFY(rule.enabled);
    QCOMPARE(rule.add, QStringList{ QStringLiteral("notify/forge") });
    QVERIFY(rule.remove.isEmpty());
    QCOMPARE(rule.query, QStringLiteral("from:notifications@example.com"));
    QCOMPARE(rule.note, QStringLiteral("All repositories, not one project."));
}

void TestTagRules::absentFieldsTakeTheirDefaults()
{
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [{"id": "minimal", "add": ["x"],
                 "query": "from:a@example.com"}]
    })");

    TagRules rules;
    rules.load(path);

    QVERIFY(rules.warnings().isEmpty());
    const TagRule &rule = rules.rules().first();
    QCOMPARE(rule.stage, 50);
    QVERIFY(rule.enabled);
    QVERIFY(rule.remove.isEmpty());
    QVERIFY(rule.note.isEmpty());
}

void TestTagRules::aMalformedRuleIsDroppedWithAWarning()
{
    // One bad rule must not cost the others. Four separate defects, and the
    // good rule sits first so a parser that stops at the first problem is
    // caught by the count rather than by an empty list.
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "good", "add": ["x"], "query": "from:a@example.com"},
        {"id": "no-query", "add": ["y"]},
        {"id": "no-tags", "query": "from:b@example.com"},
        {"id": "Bad Id", "add": ["z"], "query": "from:c@example.com"}
      ]
    })");

    TagRules rules;
    rules.load(path);

    QCOMPARE(rules.rules().size(), 1);
    QCOMPARE(rules.rules().first().id, QStringLiteral("good"));
    QCOMPARE(rules.warnings().size(), 3);
}

void TestTagRules::unknownFieldsSurviveASave()
{
    // The neutrality guarantee. If qtmaildir strips a field mailctl wrote,
    // the file is qtmaildir's file that mailctl may read.
    const QString path = writeRules(R"({
      "version": 1,
      "future_top_level": {"set_by": "another tool"},
      "rules": [{
        "id": "keeper",
        "add": ["x"],
        "query": "from:a@example.com",
        "future_field": [1, 2, 3]
      }]
    })");

    TagRules rules;
    rules.load(path);
    QVERIFY(rules.save(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root =
        QJsonDocument::fromJson(file.readAll()).object();

    QCOMPARE(root.value(QStringLiteral("future_top_level"))
                 .toObject().value(QStringLiteral("set_by")).toString(),
             QStringLiteral("another tool"));

    const QJsonObject saved =
        root.value(QStringLiteral("rules")).toArray().first().toObject();
    QCOMPARE(saved.value(QStringLiteral("future_field")).toArray().size(), 3);
    QCOMPARE(saved.value(QStringLiteral("id")).toString(),
             QStringLiteral("keeper"));
}

void TestTagRules::stageOrderPutsAccountsFirst()
{
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "topic-b", "stage": 50, "add": ["b"],
         "query": "from:b@example.com"},
        {"id": "account", "stage": 10, "add": ["acct"],
         "query": "path:\"work/**\""},
        {"id": "topic-a", "stage": 50, "add": ["a"],
         "query": "from:a@example.com"},
        {"id": "off", "stage": 20, "add": ["c"],
         "query": "from:c@example.com", "enabled": false}
      ]
    })");

    TagRules rules;
    rules.load(path);

    QStringList ids;
    for (const TagRule &rule : rules.ordered())
        ids.append(rule.id);

    // Stage ascending, ties in file order, disabled excluded.
    QCOMPARE(ids, (QStringList{ QStringLiteral("account"),
                                QStringLiteral("topic-b"),
                                QStringLiteral("topic-a") }));
    // Still loaded, so the dialog can show and re-enable it.
    QCOMPARE(rules.rules().size(), 4);
}

void TestTagRules::aQueryWithQuotesRoundTrips()
{
    // Not hypothetical: every account rule is written path:"account/**".
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [{"id": "account", "add": ["acct"],
                 "query": "path:\"work-account/**\""}]
    })");

    TagRules rules;
    rules.load(path);
    QCOMPARE(rules.rules().first().query,
             QStringLiteral("path:\"work-account/**\""));

    QVERIFY(rules.save(path));

    TagRules reloaded;
    reloaded.load(path);
    QVERIFY(reloaded.warnings().isEmpty());
    QCOMPARE(reloaded.rules().first().query,
             QStringLiteral("path:\"work-account/**\""));
}

void TestTagRules::aMissingFileIsEmptyNotAnError()
{
    // qtmaildir must open on a machine that has never written this file.
    TagRules rules;
    rules.load(m_dir.filePath(QStringLiteral("absent.json")));
    QVERIFY(rules.rules().isEmpty());
    QVERIFY(rules.warnings().isEmpty());
    QVERIFY(rules.missing());
}

void TestTagRules::aNewerVersionIsRefused()
{
    const QString path = writeRules(R"({
      "version": 2,
      "rules": [{"id": "x", "add": ["a"], "query": "from:a@example.com"}]
    })");

    TagRules rules;
    rules.load(path);
    QVERIFY(rules.rules().isEmpty());
    QCOMPARE(rules.warnings().size(), 1);
}

QTEST_MAIN(TestTagRules)
#include "test_tagrules.moc"
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add to `tests/CMakeLists.txt`, after `add_qtmaildir_test(tagdialog)`:

```cmake
add_qtmaildir_test(tagrules)
```

Run: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
Expected: FAIL, `fatal error: tagrules.h: No such file or directory`

- [ ] **Step 3: Write the header**

Create `src/tagrules.h`:

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

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

/// One auto-tagging rule, as stored in ~/.config/mailrules/rules.json.
///
/// A rule carries NO scope. The notmuch post-new hook supplies `tag:new`, a
/// dry run supplies nothing and counts against the whole corpus. That split is
/// what lets one rule answer both "what would this tag on arrival" and "what
/// does this match in all my mail".
struct TagRule
{
    QString id;       ///< Stable handle, [a-z0-9-]. Never the tag name: tags
                      ///< contain '/' and can be renamed.
    QString query;    ///< notmuch query, unscoped.
    QString note;     ///< Why the rule is shaped this way. Shown in the dialog.
    QStringList add;
    QStringList remove;
    int stage = 50;   ///< Ascending. Account tags 10, topic rules 50.
    bool enabled = true;

    /// Fields this version of qtmaildir does not understand, kept verbatim and
    /// written back on save. Without this, one save from here silently strips
    /// whatever a newer mailctl wrote, and the shared format would belong to
    /// whichever tool saved last.
    QJsonObject unknown;
};

/// Reads and writes the shared rule store.
///
/// Degrades rather than refusing, exactly as Config does: a malformed rule is
/// dropped with a warning and the rest still load, because one typo must not
/// cost every rule. qtmaildir must never fail to open because of this file.
class TagRules
{
public:
    /// $XDG_CONFIG_HOME/mailrules/rules.json, or ~/.config/... as fallback.
    /// Deliberately not under qtmaildir's own config directory: mailctl reads
    /// the same file and neither tool owns it.
    static QString defaultPath();

    /// Replaces the current contents. Never throws; see warnings().
    void load(const QString &path = QString());

    /// Atomic: QSaveFile writes a temporary and renames, so the hook can never
    /// read a partial file. Returns false if the write failed.
    bool save(const QString &path = QString()) const;

    QList<TagRule> rules() const { return m_rules; }
    void setRules(const QList<TagRule> &rules) { m_rules = rules; }

    /// Enabled rules in execution order: stage ascending, ties in file order.
    QList<TagRule> ordered() const;

    QStringList warnings() const { return m_warnings; }

    /// No file yet, as distinct from a file that would not load. A fresh
    /// install is not an error and must not be reported as one.
    bool missing() const { return m_missing; }

private:
    QList<TagRule> m_rules;
    QStringList m_warnings;
    QJsonObject m_unknown;   ///< Unrecognised top-level keys.
    bool m_missing = false;
};
```

- [ ] **Step 4: Write the implementation**

Create `src/tagrules.cpp`:

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

#include "tagrules.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace {

constexpr int kFormatVersion = 1;
constexpr int kDefaultStage = 50;

/// Fields this version understands. Anything else is preserved.
bool isKnownKey(const QString &key)
{
    static const QStringList known{
        QStringLiteral("id"),     QStringLiteral("stage"),
        QStringLiteral("enabled"), QStringLiteral("add"),
        QStringLiteral("remove"), QStringLiteral("query"),
        QStringLiteral("note"),
    };
    return known.contains(key);
}

QStringList stringsOf(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    return out;
}

} // namespace

QString TagRules::defaultPath()
{
    // Not QStandardPaths::ConfigLocation: that appends the organization and
    // application names, which would put the file under qtmaildir's own
    // directory. mailctl reads this path too, so it must be tool-neutral.
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/mailrules/rules.json");
}

void TagRules::load(const QString &path)
{
    const QString target = path.isEmpty() ? defaultPath() : path;

    m_rules.clear();
    m_warnings.clear();
    m_unknown = QJsonObject();
    m_missing = false;

    QFile file(target);
    if (!file.exists()) {
        m_missing = true;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        m_warnings.append(
            QObject::tr("Cannot read %1: %2").arg(target, file.errorString()));
        return;
    }

    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        m_warnings.append(QObject::tr("Cannot read %1: %2")
                              .arg(target, error.errorString()));
        return;
    }

    const QJsonObject root = document.object();

    const int version = root.value(QStringLiteral("version"))
                            .toInt(kFormatVersion);
    if (version != kFormatVersion) {
        m_warnings.append(
            QObject::tr("%1 uses format version %2, newer than this version "
                        "understands (%3); refusing to guess")
                .arg(target).arg(version).arg(kFormatVersion));
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() != QStringLiteral("version")
            && it.key() != QStringLiteral("rules")) {
            m_unknown.insert(it.key(), it.value());
        }
    }

    // An id is a handle: a UI selects on it and a diff tracks it.
    static const QRegularExpression idPattern(
        QStringLiteral("^[a-z0-9][a-z0-9-]*$"));

    QStringList seen;
    const QJsonArray array = root.value(QStringLiteral("rules")).toArray();
    for (int index = 0; index < array.size(); ++index) {
        const QJsonObject object = array.at(index).toObject();
        const QString where = QObject::tr("rule #%1").arg(index + 1);

        TagRule rule;
        rule.id = object.value(QStringLiteral("id")).toString();
        if (!idPattern.match(rule.id).hasMatch()) {
            m_warnings.append(
                QObject::tr("%1: id '%2' is missing or not lowercase letters, "
                            "digits and dashes; dropped")
                    .arg(where, rule.id));
            continue;
        }

        if (seen.contains(rule.id)) {
            m_warnings.append(QObject::tr("Rule '%1': duplicate id; keeping "
                                          "the first").arg(rule.id));
            continue;
        }

        rule.query = object.value(QStringLiteral("query")).toString().trimmed();
        if (rule.query.isEmpty()) {
            m_warnings.append(
                QObject::tr("Rule '%1': no query; dropped").arg(rule.id));
            continue;
        }

        rule.add = stringsOf(object.value(QStringLiteral("add")));
        rule.remove = stringsOf(object.value(QStringLiteral("remove")));
        if (rule.add.isEmpty() && rule.remove.isEmpty()) {
            m_warnings.append(QObject::tr("Rule '%1': adds and removes "
                                          "nothing; dropped").arg(rule.id));
            continue;
        }

        rule.stage = object.value(QStringLiteral("stage")).toInt(kDefaultStage);
        rule.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        rule.note = object.value(QStringLiteral("note")).toString();

        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!isKnownKey(it.key()))
                rule.unknown.insert(it.key(), it.value());
        }

        seen.append(rule.id);
        m_rules.append(rule);
    }
}

bool TagRules::save(const QString &path) const
{
    const QString target = path.isEmpty() ? defaultPath() : path;

    QDir().mkpath(QFileInfo(target).absolutePath());

    QJsonArray array;
    for (const TagRule &rule : m_rules) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), rule.id);
        object.insert(QStringLiteral("stage"), rule.stage);
        object.insert(QStringLiteral("enabled"), rule.enabled);
        object.insert(QStringLiteral("add"),
                      QJsonArray::fromStringList(rule.add));
        object.insert(QStringLiteral("remove"),
                      QJsonArray::fromStringList(rule.remove));
        object.insert(QStringLiteral("query"), rule.query);
        object.insert(QStringLiteral("note"), rule.note);
        for (auto it = rule.unknown.begin(); it != rule.unknown.end(); ++it)
            object.insert(it.key(), it.value());
        array.append(object);
    }

    QJsonObject root = m_unknown;
    root.insert(QStringLiteral("version"), kFormatVersion);
    root.insert(QStringLiteral("rules"), array);

    // QSaveFile writes a temporary and renames on commit, which is the same
    // atomicity mailrules.py gets from os.replace. The hook must never read a
    // half-written file.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QList<TagRule> TagRules::ordered() const
{
    QList<TagRule> out;
    for (const TagRule &rule : m_rules) {
        if (rule.enabled)
            out.append(rule);
    }
    // stable_sort, not sort: ties must keep file order, which is the tie-break
    // the format promises and what lets a user sequence rules within a stage.
    std::stable_sort(out.begin(), out.end(),
                     [](const TagRule &a, const TagRule &b) {
                         return a.stage < b.stage;
                     });
    return out;
}
```

Add `tagrules.cpp` to the `qtmaildir_lib` source list in `src/CMakeLists.txt`.

Add the include `#include <QObject>` is not needed; `QObject::tr` comes in via
`QJsonObject`'s transitive includes in Qt 6, but add `#include <QFileInfo>` and
`#include <QCoreApplication>` if the build reports them missing.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build && ctest --test-dir build -R tagrules --output-on-failure`
Expected: PASS, 8 test functions

- [ ] **Step 6: Mutation-check the unknown-field guard**

The neutrality guarantee is the one property with no visible symptom when
broken, so prove the test detects its loss. In `src/tagrules.cpp`, temporarily
comment out the unknown-field loop in `save()`:

```cpp
        // for (auto it = rule.unknown.begin(); it != rule.unknown.end(); ++it)
        //     object.insert(it.key(), it.value());
```

Run: `cmake --build build && ctest --test-dir build -R tagrules --output-on-failure`
Expected: FAIL on `unknownFieldsSurviveASave`

Restore the loop (copy the file to the scratchpad first and `cp` it back;
`git checkout` would discard the whole implementation, which has already cost
this project real work).

- [ ] **Step 7: Commit**

```bash
git add src/tagrules.h src/tagrules.cpp src/CMakeLists.txt \
        tests/test_tagrules.cpp tests/CMakeLists.txt
git commit -S -m "feat(rules): read and write the shared rule store

The same ~/.config/mailrules/rules.json mailctl reads, parsed here with
QJsonDocument and written atomically with QSaveFile. Fields this version
does not understand round-trip untouched, which is what keeps the format
neutral between the two tools.

Mutation-checked: removing the unknown-field write fails
unknownFieldsSurviveASave."
```

---

## Task 11: The worker counts messages, not threads

**Files:**
- Modify: `src/notmuchworker.h`, `src/notmuchworker.cpp`
- Modify: `tests/test_notmuchworker.cpp`

- [ ] **Step 1: Write the failing test**

`requestCounts` already exists and counts THREADS
(`src/notmuchworker.cpp:637`, `notmuch_query_count_threads`). A tagging rule
tags messages, and a thread count misreports any rule matching a few replies
inside large threads. Add a message-counting slot beside it.

Add to `tests/test_notmuchworker.cpp`, following the fixture pattern already in
that file (locate the existing `private slots:` block and add the declaration
there):

```cpp
void messageCountsCountMessagesNotThreads();
void messageCountsReportAnInvalidQueryAsMinusOne();
```

And the implementations, beside the other count tests:

```cpp
void TestNotmuchWorker::messageCountsCountMessagesNotThreads()
{
    // The fixture is 6 messages in 5 threads: thread A carries a reply, every
    // other thread is a single message. That difference is the whole reason
    // this slot exists beside requestCounts, and it is what the numbers below
    // assert. A rule that matched one reply of a 30-message thread would be
    // reported as 1 by a thread count, understating it by 29.
    NotmuchWorker worker(m_fixture.configPath());

    QSignalSpy messages(&worker, &NotmuchWorker::messageCountsReady);
    QSignalSpy threads(&worker, &NotmuchWorker::countsReady);

    worker.requestMessageCounts({ QStringLiteral("*") }, 1);
    worker.requestCounts({ QStringLiteral("*") }, 1);

    QCOMPARE(messages.count(), 1);
    QCOMPARE(threads.count(), 1);

    const QVector<int> messageCounts =
        messages.first().at(0).value<QVector<int>>();
    const QVector<int> threadCounts =
        threads.first().at(0).value<QVector<int>>();

    QCOMPARE(messageCounts, (QVector<int>{ 6 }));
    // The guard that makes this test mean something: if requestMessageCounts
    // were implemented with count_threads it would return 5 here and match
    // the thread count, and the assertion above would be the only thing that
    // caught it.
    QCOMPARE(threadCounts, (QVector<int>{ 5 }));
}


void TestNotmuchWorker::messageCountsReportAnInvalidQueryAsMinusOne()
{
    // Paired positionally with the caller's rules, so a dropped answer would
    // put a real number against the wrong rule. -1 says "this one failed".
    NotmuchWorker worker(m_fixture.configPath());

    QSignalSpy spy(&worker, &NotmuchWorker::messageCountsReady);
    worker.requestMessageCounts({ QStringLiteral("from:(((("),
                                  QStringLiteral("*") }, 1);

    const QVector<int> counts = spy.first().at(0).value<QVector<int>>();
    QCOMPARE(counts.size(), 2);
    QCOMPARE(counts.at(0), -1);
    QCOMPARE(counts.at(1), 6);
}
```

Declare both in the `private slots:` block. The fixture builds 6 messages in 5
threads in `initTestCase()` (`tests/test_notmuchworker.cpp:95`); read it before
writing the numbers, and if a message has been added since this plan was
written, use the real counts rather than these.

Use whatever accessor the existing tests use to reach the fixture's notmuch
config, matching the surrounding tests rather than the `m_fixture.configPath()`
written here.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build && ctest --test-dir build -R notmuchworker --output-on-failure`
Expected: build FAILS with `no member named 'requestMessageCounts' in 'NotmuchWorker'`

- [ ] **Step 3: Write the implementation**

In `src/notmuchworker.h`, beside `requestCounts` in `public slots:`:

```cpp
    /// Message counts for each query, positionally paired with the input.
    ///
    /// Beside requestCounts rather than replacing it: that one counts THREADS,
    /// which is right for the placeholder pane because a click there produces
    /// thread rows. A tagging rule tags messages, so a thread count would
    /// understate any rule matching part of a large thread.
    void requestMessageCounts(const QStringList &queries, quint64 generation);
```

And in `signals:`:

```cpp
    void messageCountsReady(const QVector<int> &counts, quint64 generation);
```

In `src/notmuchworker.cpp`, beside `requestCounts`:

```cpp
void NotmuchWorker::requestMessageCounts(const QStringList &queries,
                                         quint64 generation)
{
    if (!openReadOnly())
        return;

    QVector<int> counts;
    counts.reserve(queries.size());

    for (const QString &query : queries) {
        NmQuery nmQuery(notmuch_query_create(m_db, query.toUtf8().constData()));

        unsigned int count = 0;
        // -1 rather than a skipped entry, matching requestCounts: the caller
        // pairs these with its own rules positionally, so a dropped answer
        // would put a real number against the wrong rule.
        if (!nmQuery ||
            notmuch_query_count_messages(nmQuery.get(), &count)
                != NOTMUCH_STATUS_SUCCESS) {
            counts.append(-1);
            continue;
        }

        counts.append(static_cast<int>(count));
    }

    emit messageCountsReady(counts, generation);
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build && ctest --test-dir build -R notmuchworker --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp tests/test_notmuchworker.cpp
git commit -S -m "feat(worker): count messages as well as threads

requestCounts counts threads, which is right for the placeholder pane.
A tagging rule tags messages, so a dry run over rules needs the message
count or it understates every rule that matches part of a thread."
```

---

## Task 12: The rules dialog

**Files:**
- Create: `src/tagrulesdialog.h`, `src/tagrulesdialog.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Write the dialog**

Create `src/tagrulesdialog.h`:

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

#include <QDialog>

#include "tagrules.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

/// Views and edits the shared auto-tagging rules.
///
/// Does NOT apply rules to existing mail. The counts are shown so a rule can be
/// judged before it runs, but the only thing that tags mail is the notmuch
/// post-new hook. Backfill is deliberately out of this version: a rule that is
/// safe against arrivals is not automatically safe unscoped, and the
/// confirmation story for a bulk write does not exist yet.
class TagRulesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TagRulesDialog(QWidget *parent = nullptr);

    /// The queries whose message counts the dialog wants, in the order its
    /// rows appear. MainWindow hands these to the worker; the dialog never
    /// touches the database itself, because NotmuchWorker owns the only
    /// handle and notmuch permits one per process.
    QStringList countQueries() const;

signals:
    /// Asks the owner to run countQueries() through the worker.
    void countsRequested();

public slots:
    /// Corpus counts, positionally paired with countQueries().
    void setCounts(const QVector<int> &counts);

private slots:
    void onSelectionChanged();
    void onAddRule();
    void onCopyRule();
    void onDeleteRule();
    void applyEditsToCurrentRule();
    void onSave();

private:
    void reloadList();
    void showWarnings();
    int currentIndex() const;

    TagRules m_rules;
    QList<TagRule> m_working;   ///< Edited copy; written only on Save.

    QTreeWidget *m_list = nullptr;
    QLineEdit *m_id = nullptr;
    QLineEdit *m_add = nullptr;
    QLineEdit *m_remove = nullptr;
    QLineEdit *m_query = nullptr;
    QPlainTextEdit *m_note = nullptr;
    QSpinBox *m_stage = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_warningLabel = nullptr;
    QPushButton *m_saveButton = nullptr;
};
```

Create `src/tagrulesdialog.cpp` with the standard GPL header, then:

```cpp
#include "tagrulesdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

/// Columns of the rule list.
enum Column { ColumnEnabled = 0, ColumnStage, ColumnId, ColumnTags,
              ColumnCount };

QStringList splitTags(const QString &text)
{
    QStringList out;
    for (const QString &part : text.split(QLatin1Char(','))) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

} // namespace

TagRulesDialog::TagRulesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tagging rules"));
    resize(760, 520);

    m_rules.load();
    m_working = m_rules.rules();

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Rules tag mail as it arrives, applied by the notmuch post-new "
           "hook. Editing them here changes what the next sync does; it does "
           "not retag mail you already have."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setVisible(false);
    layout->addWidget(m_warningLabel);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(ColumnCount + 1);
    m_list->setHeaderLabels({ tr("On"), tr("Stage"), tr("Rule"), tr("Tags"),
                              tr("Matches") });
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    layout->addWidget(m_list, 1);

    auto *form = new QFormLayout;
    m_id = new QLineEdit(this);
    m_stage = new QSpinBox(this);
    m_stage->setRange(0, 999);
    m_enabled = new QCheckBox(tr("Applied on every sync"), this);
    m_add = new QLineEdit(this);
    m_add->setPlaceholderText(tr("comma separated"));
    m_remove = new QLineEdit(this);
    m_remove->setPlaceholderText(tr("comma separated"));
    m_query = new QLineEdit(this);
    // Query syntax is wire format, never translated. Only the prose is.
    m_query->setPlaceholderText(QStringLiteral("from:sender@example.com"));
    m_note = new QPlainTextEdit(this);
    m_note->setMaximumHeight(70);

    form->addRow(tr("Name"), m_id);
    form->addRow(tr("Stage"), m_stage);
    form->addRow(QString(), m_enabled);
    form->addRow(tr("Add tags"), m_add);
    form->addRow(tr("Remove tags"), m_remove);
    form->addRow(tr("Query"), m_query);
    form->addRow(tr("Note"), m_note);
    layout->addLayout(form);

    auto *buttons = new QHBoxLayout;
    auto *addButton = new QPushButton(tr("&New"), this);
    auto *copyButton = new QPushButton(tr("&Copy"), this);
    auto *deleteButton = new QPushButton(tr("&Delete"), this);
    auto *refreshButton = new QPushButton(tr("Count &matches"), this);
    buttons->addWidget(addButton);
    buttons->addWidget(copyButton);
    buttons->addWidget(deleteButton);
    buttons->addStretch();
    buttons->addWidget(refreshButton);
    layout->addLayout(buttons);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save
                                         | QDialogButtonBox::Cancel,
                                     this);
    m_saveButton = box->button(QDialogButtonBox::Save);
    layout->addWidget(box);

    connect(m_list, &QTreeWidget::currentItemChanged,
            this, &TagRulesDialog::onSelectionChanged);
    connect(addButton, &QPushButton::clicked,
            this, &TagRulesDialog::onAddRule);
    connect(copyButton, &QPushButton::clicked,
            this, &TagRulesDialog::onCopyRule);
    connect(deleteButton, &QPushButton::clicked,
            this, &TagRulesDialog::onDeleteRule);
    connect(refreshButton, &QPushButton::clicked,
            this, &TagRulesDialog::countsRequested);
    connect(box, &QDialogButtonBox::accepted,
            this, &TagRulesDialog::onSave);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Edits land on the working copy as they are made, so switching rows does
    // not silently discard what was typed.
    connect(m_id, &QLineEdit::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_add, &QLineEdit::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_remove, &QLineEdit::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_query, &QLineEdit::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_stage, &QSpinBox::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_enabled, &QCheckBox::toggled,
            this, &TagRulesDialog::applyEditsToCurrentRule);

    reloadList();
    showWarnings();
}

void TagRulesDialog::showWarnings()
{
    const QStringList warnings = m_rules.warnings();
    if (warnings.isEmpty()) {
        m_warningLabel->setVisible(false);
        return;
    }
    m_warningLabel->setText(
        tr("%n rule(s) could not be read and were skipped: %1", "",
           warnings.size()).arg(warnings.join(QStringLiteral("; "))));
    m_warningLabel->setVisible(true);
}

void TagRulesDialog::reloadList()
{
    m_list->clear();
    for (const TagRule &rule : m_working) {
        auto *item = new QTreeWidgetItem(m_list);
        item->setCheckState(ColumnEnabled,
                            rule.enabled ? Qt::Checked : Qt::Unchecked);
        item->setText(ColumnStage, QString::number(rule.stage));
        item->setText(ColumnId, rule.id);
        QStringList tags;
        for (const QString &tag : rule.add)
            tags.append(QStringLiteral("+") + tag);
        for (const QString &tag : rule.remove)
            tags.append(QStringLiteral("-") + tag);
        item->setText(ColumnTags, tags.join(QStringLiteral(" ")));
    }
    m_list->resizeColumnToContents(ColumnEnabled);
    m_list->resizeColumnToContents(ColumnStage);
    if (!m_working.isEmpty())
        m_list->setCurrentItem(m_list->topLevelItem(0));
}

int TagRulesDialog::currentIndex() const
{
    return m_list->currentItem()
               ? m_list->indexOfTopLevelItem(m_list->currentItem())
               : -1;
}

void TagRulesDialog::onSelectionChanged()
{
    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;
    const TagRule &rule = m_working.at(index);
    m_id->setText(rule.id);
    m_stage->setValue(rule.stage);
    m_enabled->setChecked(rule.enabled);
    m_add->setText(rule.add.join(QStringLiteral(", ")));
    m_remove->setText(rule.remove.join(QStringLiteral(", ")));
    m_query->setText(rule.query);
    m_note->setPlainText(rule.note);
}

void TagRulesDialog::applyEditsToCurrentRule()
{
    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;
    TagRule &rule = m_working[index];
    rule.id = m_id->text().trimmed();
    rule.stage = m_stage->value();
    rule.enabled = m_enabled->isChecked();
    rule.add = splitTags(m_add->text());
    rule.remove = splitTags(m_remove->text());
    rule.query = m_query->text().trimmed();
    rule.note = m_note->toPlainText();

    QTreeWidgetItem *item = m_list->topLevelItem(index);
    item->setCheckState(ColumnEnabled,
                        rule.enabled ? Qt::Checked : Qt::Unchecked);
    item->setText(ColumnStage, QString::number(rule.stage));
    item->setText(ColumnId, rule.id);
    QStringList tags;
    for (const QString &tag : rule.add)
        tags.append(QStringLiteral("+") + tag);
    for (const QString &tag : rule.remove)
        tags.append(QStringLiteral("-") + tag);
    item->setText(ColumnTags, tags.join(QStringLiteral(" ")));
}

void TagRulesDialog::onAddRule()
{
    TagRule rule;
    rule.id = QStringLiteral("new-rule");
    rule.query = QStringLiteral("from:sender@example.com");
    rule.add = { QStringLiteral("tag") };
    m_working.append(rule);
    reloadList();
    m_list->setCurrentItem(m_list->topLevelItem(m_working.size() - 1));
}

void TagRulesDialog::onCopyRule()
{
    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;
    TagRule copy = m_working.at(index);
    copy.id += QStringLiteral("-copy");
    m_working.insert(index + 1, copy);
    reloadList();
    m_list->setCurrentItem(m_list->topLevelItem(index + 1));
}

void TagRulesDialog::onDeleteRule()
{
    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;
    m_working.removeAt(index);
    reloadList();
}

QStringList TagRulesDialog::countQueries() const
{
    QStringList queries;
    for (const TagRule &rule : m_working)
        queries.append(rule.query);
    return queries;
}

void TagRulesDialog::setCounts(const QVector<int> &counts)
{
    for (int i = 0; i < counts.size() && i < m_list->topLevelItemCount(); ++i) {
        // -1 is the worker's "this query could not be run", which for a rule
        // means the query is malformed. Saying so beats showing a number.
        m_list->topLevelItem(i)->setText(
            ColumnCount,
            counts.at(i) < 0 ? tr("invalid")
                             : QString::number(counts.at(i)));
    }
    m_list->resizeColumnToContents(ColumnCount);
}

void TagRulesDialog::onSave()
{
    applyEditsToCurrentRule();

    m_rules.setRules(m_working);
    if (!m_rules.save()) {
        QMessageBox::warning(this, tr("Tagging rules"),
                             tr("Could not write %1.")
                                 .arg(TagRules::defaultPath()));
        return;
    }
    accept();
}
```

Add both files to the `qtmaildir_lib` source list in `src/CMakeLists.txt`.

- [ ] **Step 2: Build**

Run: `cmake --build build`
Expected: builds clean

- [ ] **Step 3: Commit**

```bash
git add src/tagrulesdialog.h src/tagrulesdialog.cpp src/CMakeLists.txt
git commit -S -m "feat(rules): a dialog to view and edit the tagging rules

Edits land on a working copy and reach the file only on Save. The
dialog never opens a notmuch database of its own: it publishes the
queries it wants counted and MainWindow runs them through the worker,
because the worker owns the only handle."
```

---

## Task 13: Wire the dialog into MainWindow

**Files:**
- Modify: `src/mainwindow.h`, `src/mainwindow.cpp`

- [ ] **Step 1: Add the action and the counting round-trip**

In `src/mainwindow.h`, add to the private slots:

```cpp
    void showTagRulesDialog();
    void onRuleCountsReady(const QVector<int> &counts, quint64 generation);
```

And to the members:

```cpp
    /// The open rules dialog, or null. Held so a counts reply can reach it,
    /// and cleared when it closes: a reply that arrives after the dialog is
    /// gone must find nothing rather than a dangling pointer.
    QPointer<TagRulesDialog> m_tagRulesDialog;
    quint64 m_ruleCountGeneration = 0;
```

Add `#include <QPointer>` and a forward declaration `class TagRulesDialog;`.

In `src/mainwindow.cpp`, add `#include "tagrulesdialog.h"`.

Register the action beside the other `addAction` calls in `registerActions()`:

```cpp
    addAction(QStringLiteral("tag_rules"), tr("Tagging &rules..."),
              [this] { showTagRulesDialog(); });
```

Add it to the Mail menu beside the other entries in `buildMenus()`:

```cpp
    messageMenu->addAction(m_actions.value(QStringLiteral("tag_rules")));
```

Add the implementations:

```cpp
void MainWindow::showTagRulesDialog()
{
    // One dialog. A second would edit a stale copy and the last Save would
    // silently win, which is the lost-edit case the atomic write cannot help
    // with because both writers are this process.
    if (m_tagRulesDialog) {
        m_tagRulesDialog->raise();
        m_tagRulesDialog->activateWindow();
        return;
    }

    auto *dialog = new TagRulesDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_tagRulesDialog = dialog;

    connect(dialog, &TagRulesDialog::countsRequested, this, [this, dialog] {
        m_ruleCountGeneration = ++m_generation;
        QMetaObject::invokeMethod(
            m_worker, "requestMessageCounts", Qt::QueuedConnection,
            Q_ARG(QStringList, dialog->countQueries()),
            Q_ARG(quint64, m_ruleCountGeneration));
    });

    dialog->show();
}

void MainWindow::onRuleCountsReady(const QVector<int> &counts,
                                   quint64 generation)
{
    // Stale reply, or the dialog closed while the count was in flight. Both
    // are ordinary: counting every rule against a cold index takes seconds
    // (item 74), which is long enough for the user to close the dialog.
    if (generation != m_ruleCountGeneration || !m_tagRulesDialog)
        return;
    m_tagRulesDialog->setCounts(counts);
}
```

Connect the worker signal in `wireWorker()`:

```cpp
    connect(m_worker, &NotmuchWorker::messageCountsReady,
            this, &MainWindow::onRuleCountsReady);
```

- [ ] **Step 2: Build and run the suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS, 19/19 (18 existing plus `tagrules`)

- [ ] **Step 3: Hand-test the dialog**

Run: `./build/src/qtmaildir`

Verify by looking, not by inference (this project's own rule: a geometry probe
cannot see what it was not built to look for):

1. Mail > Tagging rules opens the dialog and lists the rules from
   `~/.config/mailrules/rules.json`.
2. "Count matches" fills the Matches column with numbers that agree with
   `mailctl rules dry-run`.
3. Editing a rule, saving, and reopening shows the edit.
4. `mailctl rules list` shows the same edit, proving both parsers agree.

- [ ] **Step 4: Commit**

```bash
git add src/mainwindow.h src/mainwindow.cpp
git commit -S -m "feat(rules): open the tagging rules from the Mail menu

Counts are generation-stamped and dropped when stale or when the dialog
has closed: counting every rule against a cold index takes seconds, so
an in-flight reply outliving its dialog is ordinary rather than rare."
```

---

## Task 14: Migrate the real rules

**Files:**
- Read: `/data/Mail/.notmuch/hooks/post-new`
- Create: `~/.config/mailrules/rules.json`
- Create: `<database.path>/.notmuch/hooks/post-new.new`

Nothing in this task is committed to either repository. The real hook contains
account names and sender addresses, which must not reach a commit.

- [ ] **Step 1: Convert the rules**

Read `/data/Mail/.notmuch/hooks/post-new` and write
`~/.config/mailrules/rules.json` with one rule per `notmuch tag` line:

- `id` derived from the tag, lowercased, `/` and `.` replaced with `-`
  (`+notify/github` becomes `notify-github`).
- `stage` 10 for the five account rules, 50 for every topic rule. This
  preserves the ordering the current file gets from line order, and the
  account rules must run first because nothing depends on them but they are
  the coarsest.
- `add` from the `+tag` arguments.
- `query` from the part after `tag:new and`, with the outer parentheses
  removed if present. **Do not include `tag:new`**: the hook adds it.
- `note` from the comment block above the rule, joined into one paragraph.
  This is the whole point of the migration; a rule that loses its "deliberately
  excludes PayPal receipts, and here is why" comment is a rule someone will
  break later.

Do not convert the final `notmuch tag -new -- tag:new` line. It is the hook's
own cleanup and is hardcoded there.

- [ ] **Step 2: Prove equivalence before switching anything**

For each converted rule, compare what the old and new constructions match
across the whole corpus:

```bash
# For one rule, with OLD the query as written in the current hook and
# NEW the stored query:
notmuch count -- 'OLD'
notmuch count -- 'tag:new and (NEW)'
```

Since nothing carries `tag:new` between syncs, both scoped counts read 0 and
prove nothing on their own. Compare the UNSCOPED forms, which is what actually
tests the conversion:

```bash
notmuch count -- 'OLD_WITHOUT_TAG_NEW'
notmuch count -- 'NEW'
```

Build the full table with `mailctl rules dry-run` on one side and a shell loop
over the old queries on the other. **Every pair must match exactly.** A
mismatch means the conversion changed the rule, most likely by dropping or
adding parentheses around a disjunction.

Save the table to a local file outside both repositories, for example
`~/rules-migration-check.txt`. It names real senders and is not committed.

- [ ] **Step 3: Stage the new hook**

```bash
DB="$(notmuch config get database.path)"
cp ../mailctl/post-new "$DB/.notmuch/hooks/post-new.new"
cp ../mailctl/mailrules.py "$DB/.notmuch/hooks/mailrules.py"
chmod +x "$DB/.notmuch/hooks/post-new.new"
```

The existing `post-new` is untouched and keeps running on every sync.

- [ ] **Step 4: Dry-run the new hook against real arrivals**

Wait for or trigger a sync so that some mail carries `tag:new`, then run the
staged hook by hand and inspect what it did:

```bash
notmuch count tag:new          # how many arrived
"$DB/.notmuch/hooks/post-new.new"
notmuch count tag:new          # 0 if it completed
```

Compare the tags applied against what the old hook would have applied. If
anything is wrong, `tag:new` is already consumed, so re-tag by hand from the
saved table rather than re-running.

- [ ] **Step 5: Switch, once the user is satisfied**

This is the user's decision and their command to run:

```bash
DB="$(notmuch config get database.path)"
mv "$DB/.notmuch/hooks/post-new" "$DB/.notmuch/hooks/post-new.shell-backup"
mv "$DB/.notmuch/hooks/post-new.new" "$DB/.notmuch/hooks/post-new"
```

Keep the backup. It is the only copy of the original comments outside the
converted `note` fields.

---

## Task 15: Close out the backlog item

**Files:**
- Modify: `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Update the status table**

Change item 44's row to:

```markdown
| 44 | No way to manage the filters applied at sync time | workflow | M | **done** <DATE>; see `specs/2026-08-12-tagging-rules-design.md`. Spans this repo and `mailctl` |
```

- [ ] **Step 2: Add an Outcome section**

Under `## 44.`, after the existing prose:

```markdown
### Outcome (done <DATE>)

The rules moved from a hand-written shell hook to
`~/.config/mailrules/rules.json`, read by both qtmaildir and mailctl. The
`post-new` hook and the shared `mailrules.py` live in the mailctl repository;
qtmaildir has `TagRules` and a management dialog reached from the Mail menu.

Two things worth keeping. The parenthesisation of a rule's own query is
load-bearing: `tag:new and a or b` binds as `(tag:new and a) or b`, so a rule
that is a disjunction of senders would escape its scope and match the whole
corpus. And the hook must not consume `tag:new` when the rules failed to load,
because clearing the marker while the rules did not run orphans that mail
permanently and invisibly.

Backfill remains out of scope; see the spec's "Out of scope" section for what
it needs first.
```

- [ ] **Step 3: Add the changelog entry**

Under `## [Unreleased]`, in `### Added`:

```markdown
- Tagging rules are now managed from Mail > Tagging rules, reading and writing
  a shared store at `~/.config/mailrules/rules.json`. Each rule shows how much
  mail it matches, so a rule can be judged before the next sync applies it.
  The rules are applied by the notmuch `post-new` hook, which now reads that
  same file; the companion `mailctl` tool reads it too.
```

Add an `### Upgrading` note, since this requires the user to install a hook:

```markdown
### Upgrading

The tagging rules moved out of the notmuch `post-new` hook into
`~/.config/mailrules/rules.json`. Install the new hook and its library from the
`mailctl` project (`post-new` and `mailrules.py` into
`<database.path>/.notmuch/hooks/`), and keep a backup of the previous shell
hook until a sync has run with the new one.
```

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md CHANGELOG.md
git commit -S -m "docs(backlog): record item 44 as done"
```

---

## Self-review

**Spec coverage:**

| Spec section | Task |
|---|---|
| File location, `$XDG_CONFIG_HOME` | 1 (Python), 10 (C++) |
| Field table, defaults | 1, 2, 10 |
| Unknown-key preservation | 4, 10 |
| `version` refusal | 2, 10 |
| Scope belongs to the runner | 5 |
| `-new` hardcoded, only on success | 6, 7 |
| Parenthesisation | 5, 7 |
| Runner in Python, in mailctl's repo | 6 |
| Validation degrades, not fatal | 2, 10 |
| Dry-run on the worker, generation-stamped | 11, 13 |
| Two counts per rule | 9 (mailctl), 12 (dialog) |
| Dialog: view/edit/create/delete/copy | 12 |
| mailctl read-only `rules` subcommand | 8, 9 |
| Atomic writes, no locking | 4, 10 |
| Migration, equivalence proof, staged hook | 14 |
| Backfill out of scope | Stated in 12's header comment and 15's outcome |
| Testing across both repos | 1-5, 7, 10, 11 |

**Placeholder scan:** none. Every code step carries the code; the one task
without a unit test (6) says why and is covered end-to-end by 7.

**Type consistency:** `mailrules.load/save/ordered/scoped_query/tag_arguments`
and `Rule`/`Store` are used consistently in Tasks 1-9 and 14.
`TagRules::load/save/rules/setRules/ordered/warnings/missing/defaultPath` and
`TagRule` are consistent across 10, 12 and 13.
`requestMessageCounts`/`messageCountsReady` match between 11, 12 and 13.
`countQueries`/`setCounts`/`countsRequested` match between 12 and 13.

**Known deviation from the spec**, stated at the top of this plan: the spec's
`countRules` worker slot is implemented as a general `requestMessageCounts`,
because `requestCounts` already existed and counts threads rather than messages.
