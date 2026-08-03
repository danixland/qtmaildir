# Manual verification against a real database

Run 2026-08-03 against the maintainer's live notmuch index
(`~/Mail`, 5 maildirs, ~36,000 threads, 4,174 in `tag:inbox`).

Items are marked:

- **PASS** / **FAIL** where the result was observed.
- **PENDING** where the item needs a person looking at the screen. The
  automated pass deliberately stopped short of these: a full-screen capture
  exposes whatever else is on the desktop, so the visual items are for the
  maintainer to walk.
- **DEFERRED** for the items that write to the live index. Undo is
  implemented, but a bug in the mutation path is exactly what this checklist
  is meant to catch, so those run with someone watching.

## Configuration used

`~/.config/qtmaildir/qtmaildir.conf`, all five accounts, three saved
queries, the default keybindings, and no `[sync] command` (no
`mailsync.sh` exists on this machine yet).

`general/notmuch_config` is deliberately left unset so libnotmuch resolves
`~/.notmuch-config` itself and the GUI and CLI cannot drift onto different
databases.

## Results

| # | Item | Result |
|---|------|--------|
| 1 | Startup shows no configuration warnings with a valid config | **FAIL, then fixed** |
| 2 | `tag:inbox` count matches `notmuch count --output=threads` | **PASS** |
| 3 | A large query paints the first rows within a second | **PASS** |
| 4 | A new query discards the running one's results | PENDING |
| 5 | A malformed query (`tag:`) reports an error and does not crash | **PASS, item reworded** |
| 6 | Selecting a thread renders every message, oldest first | PENDING |
| 7 | Unmatched messages appear as one-line stubs | PENDING |
| 8 | A large thread renders without stalling; `QtWebEngineProcess` count stays flat | PENDING |
| 9 | An HTML newsletter renders, and shows "Remote content blocked" | PENDING |
| 10 | "Load remote content" re-renders with images | PENDING |
| 11 | Selecting a different thread clears the remote grant | PENDING |
| 12 | An inline image displays without any remote load | PENDING |
| 13 | Two messages sharing a Content-ID each show their own image | PENDING |
| 14 | `h` toggles the thread to plain text and back | PENDING |
| 15 | A link click opens the system browser without navigating the pane | PENDING |
| 16 | `a` archives the selected thread | DEFERRED |
| 17 | `a` over a multi-row selection archives all of them | DEFERRED |
| 18 | `u` after a bulk archive restores every thread | DEFERRED |
| 19 | Sync runs, the log fills, the query refreshes | BLOCKED (no sync script on this machine) |
| 20 | Sync during cron's `notmuch new` reports a lock error | BLOCKED (same) |
| 21 | A sync command path containing a space behaves consistently | **PASS, by inspection** |
| 22 | Deleting the sync script mid-run reports a failed start | **PASS, covered by test** |

## Item 1: FAIL, then fixed

With a valid config that simply had no `[sync] command`, every launch
opened a blocking modal ("No sync command configured; syncing is
disabled.") that had to be dismissed before the window was usable.

Nothing was broken. Sync is optional and the status bar already reported
the warning count. A modal on every launch is how users learn to dismiss
dialogs unread, which costs you the ones that matter.

Fixed in `3ee73d4`: `Config` now separates *problems* (something
configured but wrong: a sync command that does not exist, an account with
no maildir) from *notices* (an optional feature simply absent). Problems
open a dialog, as does every `KeyMap` warning, since each of those means a
binding the user wrote is being ignored. Notices go to the status bar only.

Re-verified against the real config: warning still reported, no modal.

## Item 2: PASS

| Query | Worker | `notmuch count --output=threads` |
|---|---|---|
| `tag:inbox` | 4174 | 4174 |
| `tag:` | 1917 | 1917 |

An earlier run showed 4174 against a CLI baseline of 4161. That was not a
defect: cron syncs every 10 minutes and had pulled in 13 threads between
the two measurements. Re-running the CLI immediately agreed exactly.

## Item 3: PASS

Query `*` over the whole database, 36,335 threads:

- first batch (200 threads) emitted after **21 ms**
- full enumeration in **2.86 s**, 182 batches

The first screenful is available essentially immediately and the rest
fills in behind, which is what the batching exists for.

## Item 5: PASS, but the item was wrong

The checklist assumed `tag:` is malformed and should raise an error. It is
not: notmuch's query parser is lenient and accepts it, matching 1,917
threads. The worker returned those threads, emitted no error, and did not
crash. The CLI agrees exactly, so the GUI is faithfully reporting what
notmuch does.

The same leniency was found in Task 8 with an unbalanced quote
(`subject:"unterminated`, accepted, matches nothing). Worth remembering
that notmuch will rarely tell a user their query is wrong; it will just
return something surprising.

## Items 21 and 22

Item 21 (`config.cpp` splits the sync command on a space, `MailSync` uses
`QProcess::splitCommand`): confirmed by inspection. For
`command = "/path/with a space/mailsync.sh"` the two disagree, config
checks a path that does not exist, and sync disables itself. The failure
is safe but the message misleads. Not fixed: no such path is in use, and
the fix is a one-line change to `config.cpp` whenever it matters.

Item 22 (script deleted while running): covered by
`test_mailsync.cpp::missingBinaryReportsFailureNotSilence`, which asserts
the launch failure surfaces as `finished(false, -1)` with a log line
rather than a hung spinner.

## Still to do

Items 4, 6-15 need a person at the screen. Items 16-18 write to the live
index and should be run with someone watching. Items 19-20 need a sync
script to exist on this machine.
