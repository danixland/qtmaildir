# A status file, so qtmaildir is told about a sync instead of inferring it

Item 174, and item 125 with it. 2026-08-29.

## The premise, which is the user's and which reverses the usual direction

`assets/mailsync.sh` was written for another system and adapted for qtmaildir.
It is now the only consumer, so the script serves the application rather than
the application accommodating the script. Where a gap is found in qtmaildir,
the script is reshaped to bridge it. mbsync and IMAP stay out of the
application; the script stays a commodity.

Two facts settle the scope, both verified rather than assumed:

- The user's crontab runs `mailsync.sh` every ten minutes and nothing else
  touches mail. There is no third-party mbsync and no bare `notmuch new`.
- `~/bin/mailsync.sh` is a SYMLINK to `assets/mailsync.sh` in this repo, so an
  edit here is live on the next cron tick with no deploy step.

## What is actually wrong

qtmaildir learns about a sync it did not start through two indirect channels,
both of which are inferences about a process that has already exited:

- **An inode in `/proc/locks`** (`syncmonitor.cpp:64`), watching the file the
  script flocks. This answers "is a sync running", and it is the reason item
  125 exists: a skipped run (exit 75) releases no lock the monitor ever saw
  held, so the spinner runs for ever.
- **A grep of the log** for `RUN END ... status=OK`
  (`MailSync::lastRunOutcome`, one production caller at `mainwindow.cpp:5182`).
  This makes a human-readable log line into load-bearing wire format: anyone
  reformatting that banner breaks the application silently.

Neither channel carries what the application needs, which is WHICH CHANNELS a
run synced, WHEN, and WITH WHAT RESULT. The consequence is visible in the code:
the local sync path narrows its clear to the accounts the run carried
(`mainwindow.cpp:4623`, `subtract(accountsThisRunCarried)`), while the external
path cannot and does a blanket `m_pendingTagEdits.clear()` at `:5183`. The
external path is coarser than the local one for want of information the script
has and does not report.

## What is NOT wrong, and must not be "fixed"

**A bare `notmuch new` cannot clear the pending count, and should not.** The
count means "confirmed tag mutations not yet known to have reached the MAIL
STORE" (`mainwindow.h:1763`), which is the server. A tag edit is in notmuch the
moment it is made; what is outstanding is mbsync pushing the renamed Maildir
files. `notmuch new` re-indexes local files and pushes nothing.

Item 174's own "Approach" section proposes watching
`notmuch_database_get_revision()`. That is rejected here: a revision moves when
mail ARRIVES as well as when our edits land, and in neither case does it say
anything about the server. Clearing on a revision bump would make the indicator
claim work is safe to quit on when it is still local, which is item 28's defect
returning in a new costume. The item's own constraint gestures at this and then
resolves it with a per-message re-check, but that check reads notmuch, which
also cannot see the server.

The status file is the correct instrument precisely because the script knows
what notmuch cannot: whether MBSYNC ran and what it returned.

## The design

The script writes one JSON file at the end of every run, including a skipped
one. qtmaildir watches that file.

**Path:** `~/.local/state/qtmaildir/syncstatus.json`, configurable beside
`syncLog` for the same reasons that key exists. Note it goes under the
application's own state directory, not beside `mailsync.log` in
`~/.local/state/`: the log is the script's, the status file is the interface.

**Shape:**

```json
{
  "version": 1,
  "run_id": "2026-08-29T10:33:07+02:00",
  "started": "2026-08-29T10:33:07+02:00",
  "ended": "2026-08-29T10:33:41+02:00",
  "state": "ok",
  "channels": ["work", "personal"],
  "mbsync_status": 0,
  "notmuch_status": 0
}
```

- `state` is `ok`, `failed` or `skipped`. Three states, not a boolean, because
  a skip is neither: item 125 exists because a skip currently reads as neither
  success nor failure and so resolves nothing.
- `channels` is the channel list the run actually synced, or `["-a"]` for a
  full run. This is what lets the external path narrow its clear the way the
  local path already does.
- `version` is refused rather than guessed at by a reader that does not know
  it, following the rule the rules file already uses for `kFormatVersion`.

**Written atomically**, to a temporary file in the same directory and then
`mv`, which is atomic within a filesystem. A reader watching the file must
never see a half-written one, and qtmaildir will be watching it while it is
written.

**The log keeps its banner.** `RUN END` stays exactly as it is, and
`MailSync::lastRunOutcome()` stays with its eight tests. It becomes the
FALLBACK for a status file that is missing or unreadable, which is what a first
run after upgrading looks like, rather than being deleted in the same change
that adds its replacement.

## What each side does

**`assets/mailsync.sh`.** Collect the channel list, write the file on every
exit path. The skip branch at the top exits before the status directory exists,
so it needs its own write. Two properties recorded as load-bearing survive
untouched: printing to stdout as well as the log, and exiting with the real
status.

**`src/mailsync.{h,cpp}`.** A `SyncStatus` value struct and a static reader,
beside `lastRunOutcome()` and in the same shape: a pure function of a path,
returning a value with an Unknown-equivalent for every failure. No widget, no
process.

**`src/syncmonitor.{h,cpp}`.** Watch the status file with `QFileSystemWatcher`,
beside the existing `/proc/locks` polling rather than replacing it. The lock
answers "a sync is running now" and the file answers "a run finished and here
is what it did"; these are different questions and a completion record cannot
answer the first.

**`src/mainwindow.cpp`.** The external path at `:5182` reads the status file
instead of the log, and narrows its clear by channel, resolving channels back
to accounts through `Account::channel`. A `skipped` state clears the spinner
without clearing the count, which is item 125.

## Constraints

- **The script is live.** Every edit reaches real mail within ten minutes.
  Writing the status file is purely additive and nothing reads it until the
  application does, so a partial state is harmless, but the ORDER matters: the
  script lands first and is left to run for a few cycles before the reader is
  written.
- **`Account::channel` may differ from the account key** and may be empty,
  in which case the key is the channel. A reverse lookup must handle both, and
  a channel naming no account must be ignored rather than dropped on the floor
  silently.
- **`-a` means every account**, and is not the same as a list naming them all.
  A reader that treats `["-a"]` as an unknown channel clears nothing on exactly
  the run that carried everything.
- **Only a successful run may clear the count**, per the rule the local path
  states at `:4614`. `failed` and `skipped` clear no edits.
- **Unknown changes no state**, exactly as `SyncOutcome::Unknown` and
  `SyncMonitor::State::Unknown` are treated today.
- **Two readers, one format.** This is the `rules.json` situation again, a bash
  writer and a C++ reader agreeing by test rather than by shared code. The
  discipline in AGENTS.md under "Changing the rule format" applies: change both
  sides together, bump the version only for a breaking change, and run both
  suites.
- **No new dependency for JSON.** The script writes it with `printf`, which is
  why the shape above has no nesting; qtmaildir reads it with `QJsonDocument`,
  which it already uses for `queries.json`.

## Tests

- `assets/hooks/`-style Python test for the script, in the shape the two hook
  suites already take: run it with a stub `mbsync` and `notmuch` on PATH,
  assert the file's contents for ok, failed and skipped.
- `test_mailsync.cpp`: the reader, against files written by hand. Every failure
  mode returns the Unknown equivalent, including a truncated file, an unknown
  version, and a file that is not JSON at all.
- `test_mainwindow.cpp`: the external path clears the count for a channel it
  carried and leaves an account it did not, which is the behaviour the blanket
  clear cannot express. And a `skipped` run clears the spinner and no edits.

## What this deliberately does not do

- No daemon, no socket, no D-Bus. The file is the interface.
- No change to how a LOCAL sync reports: it has the process and its exit
  status, which is better evidence than a file.
- No backfill of item 125's other half. The spinner is cleared by a `skipped`
  record here; whether `SyncMonitor` should also time out an observation it
  never saw end is a separate question and stays in item 125.
