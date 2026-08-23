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
"""Unit checks for the qtmaildir.conf reader the post-new hook uses to find
the sent folders.

The file is written by QSettings, not by configparser, so the cases that
matter are the ones where the two disagree: a section name carrying a dot, a
comment introduced by `;`, and a key present but empty.

Run: ./test_qtmaildirconf.py
"""

import tempfile
from pathlib import Path

import qtmaildirconf


def write_config(tmp, text):
    path = Path(tmp) / "qtmaildir" / "qtmaildir.conf"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def test_sent_folders_are_read_per_account():
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "[account.work]\n"
                                 "maildir = work\n"
                                 "sent = Sent\n"
                                 "trash = Trash\n")
        assert qtmaildirconf.sent_folders(path) == ["work/Sent"]


def test_drafts_are_excluded_alongside_sent():
    """A draft never arrived either, so it must not carry `inbox`. Both keys
    feed one list: the hook asks a single question, "is this a folder mail
    arrives in", and sent and drafts answer it the same way.

    Trash is deliberately NOT here. qtmaildir's own Delete leaves `inbox` on
    a trashed message so Restore can put it back where it came from, and
    stripping it here would fight that.
    """
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "[account.work]\n"
                                 "maildir = work\n"
                                 "sent = Sent\n"
                                 "drafts = Drafts\n"
                                 "trash = Trash\n")
        assert qtmaildirconf.sent_folders(path) == ["work/Sent", "work/Drafts"]


def test_an_account_with_only_drafts_still_contributes():
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "[account.a]\nmaildir = a\ndrafts = Drafts\n")
        assert qtmaildirconf.sent_folders(path) == ["a/Drafts"]


def test_an_account_section_may_carry_a_dot():
    """QSettings writes `[account.a.b]` for the key `a.b`, and the account
    key is everything after the first dot. Splitting on the LAST dot names
    an account that does not exist and finds no folder."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "[account.provider.name]\n"
                                 "maildir = provider-name\n"
                                 "sent = Sent\n")
        assert qtmaildirconf.sent_folders(path) == ["provider-name/Sent"]


def test_a_folder_may_contain_spaces_and_brackets():
    """`[Gmail]/Posta inviata` is a real folder name here. The brackets are
    the provider's, not INI syntax, because they are in a VALUE."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "[account.g]\n"
                                 "maildir = gmail\n"
                                 "sent = [Gmail]/Posta inviata\n")
        assert qtmaildirconf.sent_folders(path) == [
            "gmail/[Gmail]/Posta inviata"]


def test_an_account_without_a_sent_key_contributes_nothing():
    """`sent` is optional: an account may keep no sent mail locally. It must
    not contribute an entry, since a bare `maildir/` prefix would match the
    whole account."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "[account.a]\nmaildir = a\ntrash = Trash\n"
                                 "[account.b]\nmaildir = b\nsent = Sent\n")
        assert qtmaildirconf.sent_folders(path) == ["b/Sent"]


def test_an_empty_sent_value_contributes_nothing():
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "[account.a]\nmaildir = a\nsent =\n")
        assert qtmaildirconf.sent_folders(path) == []


def test_an_account_without_a_maildir_contributes_nothing():
    """Without the account's own subdirectory the folder cannot be located,
    and a bare `Sent` would match every account's sent folder at once."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "[account.a]\nsent = Sent\n")
        assert qtmaildirconf.sent_folders(path) == []


def test_comments_and_other_sections_are_ignored():
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "; a comment\n"
                                 "[general]\n"
                                 "language = it\n"
                                 "[sync]\n"
                                 "command = /bin/true\n"
                                 "[account.a]\n"
                                 "; another comment\n"
                                 "maildir = a\n"
                                 "sent = Sent\n")
        assert qtmaildirconf.sent_folders(path) == ["a/Sent"]


def test_a_missing_file_yields_no_folders():
    """The hook must run on a system with no qtmaildir config at all: it
    then protects nothing, rather than failing the sync."""
    with tempfile.TemporaryDirectory() as tmp:
        assert qtmaildirconf.sent_folders(Path(tmp) / "absent.conf") == []


def test_an_unreadable_file_yields_no_folders():
    """A malformed config must not fail the sync. notmuch new has already
    run at this point; refusing to tag is worse than not protecting sent
    mail for one cycle."""
    with tempfile.TemporaryDirectory() as tmp:
        path = write_config(tmp, "this is not an ini file\n[[[\n")
        assert qtmaildirconf.sent_folders(path) == []


def test_the_query_scopes_every_folder():
    folders = ["a/Sent", "g/[Gmail]/Posta inviata"]
    query = qtmaildirconf.sent_query(folders)
    assert query == ('path:"a/Sent/**" or path:"g/[Gmail]/Posta inviata/**"')


def test_the_query_is_empty_when_no_folder_is_configured():
    """An empty query means "match everything" to notmuch, so the caller
    must be able to tell "nothing to protect" from "protect the world"."""
    assert qtmaildirconf.sent_query([]) == ""


def test_a_folder_containing_a_quote_cannot_break_out_of_the_query():
    """The folder name reaches a notmuch query as a quoted string. A stray
    double quote would end the term and let the rest be read as syntax."""
    query = qtmaildirconf.sent_query(['a/He said "hi"'])
    assert query.count('"') % 2 == 0
    assert "\\\"" in query or '""' in query


def main():
    tests = [value for name, value in sorted(globals().items())
             if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
        print(f"ok  {test.__name__}")
    print(f"\n{len(tests)} passed")


if __name__ == "__main__":
    main()
