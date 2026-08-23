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


def run_all():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print(f"ok  {name}")


if __name__ == "__main__":
    run_all()
    print("\nall passed")
