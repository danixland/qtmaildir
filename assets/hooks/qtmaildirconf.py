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
"""Reads the account layout out of qtmaildir.conf, for the post-new hook.

Only the sent folders are read, and only so the hook can tell mail the user
SENT from mail that arrived. Everything else in that file belongs to the
application.

Stdlib only: this is imported by a notmuch hook that runs on every sync.

The file is written by QSettings rather than by configparser, and the two
disagree in one place that matters here. QSettings treats `/` in a section
name as a group separator, so accounts are `[account.<key>]` with a DOT, and
that key may itself contain dots (`[account.provider.name]`). The account key
is therefore everything after the FIRST dot, never a split on the last one.
"""

import configparser
from pathlib import Path

ACCOUNT_PREFIX = "account."


def default_path():
    """~/.config/qtmaildir/qtmaildir.conf, honouring XDG_CONFIG_HOME.

    Read through the environment rather than hardcoded so a test can point
    at a throwaway config, which is how the hook's own tests reach it.
    """
    import os
    base = os.environ.get("XDG_CONFIG_HOME") or (Path.home() / ".config")
    return Path(base) / "qtmaildir" / "qtmaildir.conf"


def _accounts(path):
    """Every `[account.*]` section as a dict, or nothing at all.

    A file that will not parse yields NO accounts rather than raising. The
    caller is a hook running after `notmuch new` has already indexed the
    mail: failing the sync over a malformed application config is worse than
    not protecting sent mail for one cycle, and the hook logs the miss.
    """
    parser = configparser.ConfigParser(
        # QSettings writes `;` comments, and `#` appears inside values (a
        # colour is `#2f6fa8`), so `#` must NOT introduce a comment.
        comment_prefixes=(";",),
        # A value may contain `%` and `$`; neither is an interpolation here.
        interpolation=None,
        # `[Gmail]/Posta inviata` is a legal value. Nothing in this file
        # relies on duplicate keys, but tolerating them beats raising.
        strict=False)
    try:
        # Explicit UTF-8: QSettings writes it, and the C locale would
        # otherwise decide.
        with open(path, encoding="utf-8") as handle:
            parser.read_file(handle)
    except (OSError, UnicodeDecodeError, configparser.Error):
        return []

    return [(name[len(ACCOUNT_PREFIX):], parser[name])
            for name in parser.sections()
            if name.startswith(ACCOUNT_PREFIX)]


# Folders mail does not ARRIVE in: this system put the message there itself.
#
# Trash is deliberately absent. qtmaildir's own Delete leaves `inbox` on a
# trashed message so Restore can put it back where it came from, and stripping
# it here would fight that.
NOT_ARRIVALS = ("sent", "drafts")


def sent_folders(path=None):
    """Every folder mail does not arrive in, relative to the mail root.

    An account contributes nothing unless it names a maildir: a bare `Sent`
    would match every account's folder of that name at once. Each of the keys
    in NOT_ARRIVALS is optional on its own, since an account may keep no sent
    mail or no drafts locally.
    """
    if path is None:
        path = default_path()

    folders = []
    for _key, section in _accounts(path):
        maildir = section.get("maildir", "").strip()
        if not maildir:
            continue
        for key in NOT_ARRIVALS:
            folder = section.get(key, "").strip()
            if folder:
                folders.append(f"{maildir}/{folder}")
    return folders


def sent_query(folders):
    """A notmuch query matching everything inside the given folders.

    Empty for an empty list, and the caller MUST check: an empty query means
    "match everything" to notmuch, so handing this straight to a tag command
    would treat the whole corpus as sent mail.

    `path:` is hierarchical, so `<folder>/**` covers `cur/` and `new/` and
    any nesting a provider invents underneath.
    """
    if not folders:
        return ""

    terms = [f'path:"{_quote(folder)}/**"' for folder in folders]
    return " or ".join(terms)


def _quote(value):
    """Escape a folder name for a double-quoted notmuch term.

    Backslashes BEFORE quotes: the other order escapes the backslashes just
    added. Same rule as SearchTerm::quote() in the application, and the same
    reason.
    """
    return value.replace("\\", "\\\\").replace('"', '\\"')
