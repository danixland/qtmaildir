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
  - a rule removing a protected tag is skipped whole, and the run continues

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
    notmuch call, and tag:new must survive so the next run catches up.

    The query has to be one notmuch genuinely rejects, which is a narrower
    set than it looks: notmuch 0.39's parser accepts unbalanced parentheses
    and bare punctuation without complaint, tags nothing, and exits 0. A
    malformed date range is rejected by the date parser and does exit
    non-zero, which is why the fixture uses one.
    """
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [{
            "id": "broken",
            "add": ["x"],
            "query": "date:zzz..zzz",
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


def test_a_protected_removal_is_skipped_whole_and_the_run_continues():
    """The PROTECTED_REMOVALS guard, end to end.

    Four assertions in one run, because three of them pass against a guard
    that is broken in a different way. A guard that skipped only the removal
    would still apply the rule's adds; a guard that aborted the run would
    starve every later rule; and a guard that aborted before the consumer
    would strand tag:new, so every future sync would refuse the same rule
    again and nothing would ever be tagged after it.
    """
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_database(tmp)
        write_rules(env, [
            {"id": "over-reaching", "stage": 10,
             "add": ["archived"], "remove": ["unread", "inbox"],
             "query": "from:notifications@example.com"},
            {"id": "well-behaved", "stage": 20, "add": ["promo"],
             "query": "from:promo@example.net"},
        ])
        assert count(env, "tag:unread") == 3
        assert count(env, "tag:inbox") == 3

        result = subprocess.run([str(HOOK)], env=env, capture_output=True,
                                text=True)
        assert result.returncode == 0, result.stderr
        assert "over-reaching" in result.stderr, result.stderr

        # 1. the protected tags survive on the message the rule matched
        assert count(env, "tag:unread and from:notifications@example.com") == 1
        assert count(env, "tag:inbox and from:notifications@example.com") == 1
        # 2. the rule is skipped ENTIRELY, so its adds never land either
        assert count(env, "tag:archived") == 0
        # 3. a later, well-behaved rule still runs
        assert count(env, "tag:promo") == 1
        # 4. the marker is still consumed, so the next sync is not stuck
        assert count(env, "tag:new") == 0


def setup_accounts(tmp, sent_config=True):
    """A maildir laid out as qtmaildir configures it: two accounts, each with
    an Inbox and a Sent folder, one message in each.

    Separate from setup_database() because the sent carve-out is the only
    thing that cares where a file sits. The folder names are the awkward
    ones deliberately: a bracketed, spaced provider folder is what the real
    config carries, and a flat `Sent` is what the other half carries.
    """
    root = Path(tmp) / "Mail"
    folders = {
        "one": ("acct-one/Inbox", "acct-one/Sent"),
        "two": ("acct-two/[Provider]/Posta inviata",
                "acct-two/[Provider]/Posta inviata"),
    }
    for sub in ("acct-one/Inbox", "acct-one/Sent",
                "acct-two/Inbox", "acct-two/[Provider]/Posta inviata"):
        for part in ("new", "cur", "tmp"):
            (root / sub / part).mkdir(parents=True)

    make_message(root / "acct-one/Inbox", "arrived-one",
                 "friend@example.org", "an arrival")
    make_message(root / "acct-one/Sent", "sent-one",
                 "you@example.org", "something sent")
    make_message(root / "acct-two/Inbox", "arrived-two",
                 "friend@example.org", "another arrival")
    make_message(root / "acct-two/[Provider]/Posta inviata", "sent-two",
                 "you@example.org", "something else sent")

    config = Path(tmp) / "notmuch-config"
    config.write_text(
        f"[database]\npath={root}\n\n"
        f"[new]\ntags=new;unread;inbox\n\n"
        f"[user]\nname=Test\nprimary_email=you@example.org\n")

    env = dict(os.environ)
    env["NOTMUCH_CONFIG"] = str(config)
    env["XDG_CONFIG_HOME"] = str(Path(tmp) / "config")

    if sent_config:
        conf = Path(env["XDG_CONFIG_HOME"]) / "qtmaildir" / "qtmaildir.conf"
        conf.parent.mkdir(parents=True, exist_ok=True)
        conf.write_text(
            "[account.one]\nmaildir = acct-one\nsent = Sent\n"
            "[account.two]\nmaildir = acct-two\n"
            "sent = [Provider]/Posta inviata\n")

    subprocess.run(["notmuch", "new"], env=env, capture_output=True,
                   check=True)
    return env


def test_sent_mail_does_not_keep_the_inbox_tag():
    """The carve-out. notmuch's new.tags applies `inbox` to every file it
    indexes, including the copy the composer files into a sent folder, so
    mail the user SENT shows up in an inbox view it never arrived in.

    Both accounts are asserted, because the folder shapes differ and a
    reader that mishandles the bracketed, spaced one would still pass on the
    flat `Sent`.
    """
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_accounts(tmp)
        write_rules(env, [])
        assert count(env, "tag:inbox") == 4

        result = subprocess.run([str(HOOK)], env=env, capture_output=True,
                                text=True)
        assert result.returncode == 0, result.stderr

        # The two sent copies lose it...
        assert count(env, 'tag:inbox and path:"acct-one/Sent/**"') == 0
        assert count(
            env,
            'tag:inbox and path:"acct-two/[Provider]/Posta inviata/**"') == 0
        # ...and the two arrivals keep it. This is the half that fails if the
        # query is unscoped, which is the expensive mistake here.
        assert count(env, "tag:inbox") == 2
        assert count(env, 'tag:inbox and path:"acct-one/Inbox/**"') == 1
        assert count(env, 'tag:inbox and path:"acct-two/Inbox/**"') == 1


def test_sent_mail_keeps_every_other_tag():
    """Only `inbox` is stripped. `unread` in particular must survive:
    maildir.synchronize_flags is true, so removing it rewrites Maildir
    filenames and reaches the server on the next mbsync.
    """
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_accounts(tmp)
        write_rules(env, [])
        subprocess.run([str(HOOK)], env=env, capture_output=True, check=True)
        assert count(env, 'tag:unread and path:"acct-one/Sent/**"') == 1


def test_the_carve_out_only_touches_newly_indexed_mail():
    """Scoped to tag:new like every rule, so the hook never rewrites tags
    across the whole corpus on a sync. A sent message whose `inbox` tag was
    put back by hand stays that way until it is reindexed.
    """
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_accounts(tmp)
        write_rules(env, [])
        subprocess.run([str(HOOK)], env=env, capture_output=True, check=True)
        assert count(env, 'tag:inbox and path:"acct-one/Sent/**"') == 0

        subprocess.run(["notmuch", "tag", "+inbox", "--",
                        'path:"acct-one/Sent/**"'], env=env, check=True)
        subprocess.run([str(HOOK)], env=env, capture_output=True, check=True)
        assert count(env, 'tag:inbox and path:"acct-one/Sent/**"') == 1


def test_no_qtmaildir_config_leaves_every_tag_alone():
    """The hook must run on a system with no qtmaildir config: it then
    protects nothing rather than failing the sync, and above all does not
    treat an empty folder list as "every path", which is what an empty
    notmuch query means.
    """
    with tempfile.TemporaryDirectory() as tmp:
        env = setup_accounts(tmp, sent_config=False)
        write_rules(env, [])
        result = subprocess.run([str(HOOK)], env=env, capture_output=True,
                                text=True)
        assert result.returncode == 0, result.stderr
        assert count(env, "tag:inbox") == 4


def run_all():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
            print(f"ok  {name}")


if __name__ == "__main__":
    run_all()
    print("\nall passed")
