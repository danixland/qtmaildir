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
import re
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

FORMAT_VERSION = 1
DEFAULT_STAGE = 50

# Fields this version understands. Anything else in a rule object is kept in
# `unknown` and written back untouched, which is what makes the file neutral
# rather than this tool's file that another program may read.
KNOWN_KEYS = {"id", "stage", "enabled", "add", "remove", "query", "note"}

# An id is a handle, not a display name: a UI selects on it and a diff tracks
# it. Tags may contain '/' and may be renamed; ids may not.
ID_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")


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
    # Distinguishes "no file yet" from "a file that would not load". The hook
    # treats them differently: the first is a fresh install, the second must
    # not consume tag:new.
    missing: bool = False
    failed: bool = False


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


def ordered(rules):
    """Enabled rules in execution order: by stage ascending, ties by position.

    `sorted` is stable, so sorting on stage alone preserves file order within
    a stage. That is the tie-break the format promises, and it is why this
    does not sort on (stage, id): an id-sorted tie would reorder rules a user
    deliberately sequenced.
    """
    return sorted([r for r in rules if r.enabled], key=lambda r: r.stage)


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
