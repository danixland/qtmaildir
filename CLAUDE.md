# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run a single test binary directly for a tighter loop: `./build/tests/test_keymap`.
Or by ctest name (the name is the suffix, not the binary): `ctest --test-dir build -R keymap`.

Adding a test: create `tests/test_<name>.cpp` and add `add_qtmaildir_test(<name>)` to
`tests/CMakeLists.txt`. That function links `qtmaildir_lib` and `Qt6::Test` and registers
the test. Fixture-driven tests get `FIXTURE_DIR` via `target_compile_definitions` (see
`test_mimeparser`).

Adding a source file: add the `.cpp` to the `qtmaildir_lib` list in `src/CMakeLists.txt`.
All logic lives in that static library; the `qtmaildir` executable is only `main.cpp`, so
tests can link everything without duplicating source lists.

## Environment constraints (verified 2026-08-02, Slackware)

- **notmuch installs no `notmuch.pc`.** CMake locates it with `find_path`/`find_library`.
  Never convert it to `pkg_check_modules`. GMime does ship `gmime-3.0.pc` and uses pkg-config.
- Qt 6.11.1 including WebEngine ships in Slackware's monolithic `qt6` package; there is no
  separate `qt6-webengine`.
- CMake 4.3.4 rejects `cmake_minimum_required(VERSION <3.5)`. Keep 3.21.
- **gmime headers must be included before any Qt header in the same translation unit.**
  glib declares a struct field named `signals`, which Qt defines as a macro.

## Architecture

One process, two threads. A GUI counterpart to neomutt over a local notmuch-indexed
Maildir. **No network protocol work at all** — fetching and sending are external scripts.

```
UI thread                          Worker thread
MainWindow                         NotmuchWorker
 ├ QueryBar / SavedQueryBar          └ owns the only notmuch_database_t*
 ├ ThreadListView ── ThreadListModel
 └ MessageView (HeaderWidget, QWebEngineView, AttachmentBar)

Config (INI)   KeyMap   MailSync (QProcess)   MimeParser (GMime)
```

**No `notmuch_*` pointer ever crosses the thread boundary.** Data crosses as the plain
value structs in `src/types.h` (`ThreadSummary`, `MessageRef`, `TagChange`), over queued
signals in both directions. `notmuchworker.cpp` is the only file that includes `notmuch.h`
outside `src/nmraii.h`; C handles are owned by the `NmQuery`/`NmMessages`/`NmThread`/…
RAII aliases there so they cannot leak.

**Generation counters, not cancellation.** Each query bumps a `quint64` generation passed
through to the worker and back on every result signal. The UI discards results whose
generation is stale. The worker never needs to know a query was superseded. Threads are
emitted in batches of `kBatchSize` (200) so a 10k-thread query paints immediately.

**Read-only by default, read-write in bursts.** notmuch's write lock is exclusive
process-wide, so holding it open would block the user's cron `notmuch new`. `applyTags`
closes the read-only handle, opens read-write, applies, closes. notmuch permits only one
open handle per process, so that close-first ordering is required, not stylistic.

**No dry-run, no destructive-action confirmation.** Those gates exist in the companion
project `../mailctl` to restrain an agent; a human at a GUI gets **undo** instead — every
mutation pushes its inverse (`TagChange::inverted()`) onto a `QUndoStack`. Do not add
confirmation dialogs for tag mutations. All actions funnel through one `applyTags` path;
multi-row selections go through `applyTagsToThreads`, which resolves every thread in ONE
combined `thread:a or thread:b` query rather than one query per thread.

**The Maildir path is deliberately not configurable.** notmuch stores it as
`database.path` and libnotmuch reads it; duplicating it would create two sources of truth.
The only escape hatch is `general/notmuch_config`, pointing at an alternate notmuch config.
Per-account subdirectories *are* configured, since notmuch does not model accounts at all.

**The sync script lives here, in `assets/mailsync.sh`.** It moved from the
companion `mailctl` project, which documents that it never calls it: the script
is `mbsync` plus `notmuch new` with a lock, and qtmaildir is the only thing that
runs it programmatically. Two properties exist for this application's sake and
must survive any edit. It **prints to stdout as well as its log file**, because
`MailSync` shows what the command prints and a self-redirecting script leaves
the pane empty; and it **exits with the real status**, because a `0` from a
failed sync makes qtmaildir report success, clear the unsynced-changes count,
and quit on a sync that never happened.

**Every user-facing string is translatable.** Wrap UI text in `tr()`, including strings
that are only ever shown in passing: status bar messages, tooltips, dialog prose,
completion descriptions. Query syntax itself is not user-facing text — notmuch keywords
like `tag:` and `date:` are wire format and must never be translated, only the prose
describing them. Pre-existing code has not been audited against this rule.

**Config format gotcha:** QSettings treats `/` in a section name as a group separator, so
account sections are `[account.work]`, not `[account/work]`. `childKeys` returns keys
sorted alphabetically, never in file order. **`[general]` keys are read WITHOUT the
`general/` prefix** — QSettings' INI backend treats a section literally named `[general]`
as its own fallback section and strips it, so a `general/<key>` lookup silently matches
nothing (this is how `notmuch_config` went unnoticed as broken). Config lives at
`~/.config/qtmaildir/qtmaildir.conf`.

Machine-written UI state is a **separate** file, `~/.local/state/qtmaildir/uistate.conf`
via `MainWindow::uiStatePath()`. Never write window blobs into the hand-edited config.
Build the path from `QStandardPaths::GenericStateLocation`, not `StateLocation`: the
latter appends both the organization and the application name, and both are `qtmaildir`.

**`QLineEdit::setCompleter` is wrong for any field holding more than one
value.** It hands completion to the line edit, which then overwrites the
completer's `completionPrefix` with the widget's **entire text** on every
keystroke. In a field holding a list, the first value completes and nothing
after it ever does, because "unread, fl" is matched whole against the
candidates. Setting the prefix from a `textEdited` handler does not help: the
line edit sets it again afterwards. Use `setCompleter` only for a field whose
whole contents are the thing being completed; otherwise attach with
`QCompleter::setWidget` and drive `setCompletionPrefix` and `complete()`
yourself, and replace the token under the cursor on `activated` rather than
letting QCompleter overwrite the field. This has been hit twice, in
`QueryCompleter` (01ba356) and in `TagDialog`; the trap belongs to Qt, not to
either class. A test that uses `setText()` passes against the bug, since
`setText` does not drive a completer at all: the keys must be typed.

**`QItemSelectionModel::currentRowChanged` is emitted BEFORE the selection model is
updated.** A handler on it reading `selectedRows()` sees the *previous* selection, not the
one the user just made. Verified against Qt 6.11. This produced two separate faults in one
change (987a9e7): a Ctrl+click taking a selection from one row to two arrived reporting
one, and a click collapsing three rows to one arrived reporting three. Any decision that
depends on how many rows are selected belongs in a `selectionChanged` handler, which does
see the true count; `currentRowChanged` is only safe for "which row is current".

The related trap: **`selectAll()` emits no `currentRowChanged` at all** and leaves the
current index invalid when nothing was current. A test that calls `selectAll()` on a fresh
view therefore passes against a missing selection guard, because no signal ever fires. Test
multi-select from a row that is already current, which is also how a user reaches it.

**A queued load can outlive the state that started it.** `loadThread` crosses to the worker
on a queued connection, so its reply lands after whatever the UI did in the meantime. The
generation counter covers a superseded *query*, not a superseded *selection*: blanking the
pane and then receiving an in-flight thread repaints it. `onThreadLoaded` therefore drops a
reply that arrives while more than one row is selected. This class of bug cannot be
reproduced in `test_mainwindow`, which has no worker and never fires `threadLoaded`; it
needs the notmuch fixture or a hand test.

**Do not conclude a key binding is dead from `QTest::keyClick()`.** Whether a symbol needs
Shift is a layout property, not a Qt one. `Ctrl++` is the shipped `zoom_in` default and is
exactly what the `+` key emits on an Italian layout, while synthetic input never delivers
it. Verify against a real keyboard before changing a default on reachability grounds. The
separate, real trap `normalizeSequence()` handles is a **bare capital** (`N` parses to
unshifted Key_N, which no keystroke emits).

## Web view security

The most security-sensitive area: a browser engine pointed at input from strangers. Do not
loosen any of these without an explicit decision.

- Off-the-record `QWebEngineProfile`, JavaScript disabled, `LocalContentCanAccessRemoteUrls`
  and `LocalContentCanAccessFileUrls` both false.
- The interceptor **blocks every request by default** and **fails closed**: with no document
  URL set, every `qtmaildir:` URL is denied. The document-load exemption matches the
  **exact** base URL passed to `setHtml()`, never the `qtmaildir:` scheme as a whole — a
  scheme-wide allow would let a hostile body reference `qtmaildir://anything` and be trusted.
  Consequence: `MessageView` **must** call `setDocumentUrl()` with the same URL it gives
  `setHtml()`, or nothing renders.
- A whole thread is one HTML document in one web view (a `QWebEngineView` per message would
  spawn a Chromium render process each). That makes `cid:` ids collide across messages, so
  every reference is rewritten to `cid:<prefix>!<id>`. **A `cidPrefix` must never contain
  `!`** — it is the namespace separator.
- Remote content grants are per-render and never sticky.
- **Attachment filenames are untrusted input.** Reduce to basename, strip separators, resolve
  against the chosen directory, and refuse anything escaping it. Compare resolved paths as
  paths, not with `startsWith` — `/tmp/safe-evil` passes a `startsWith("/tmp/safe")` check.

## At the start of a session: reconcile the backlog with the user's notes

The backlog at `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md` is
**downstream** of the user's own notes at
`~/Documents/Obsidian/note/notes on qtmaildir.md`. The user writes to those notes
whenever they use the application and hit something, so the backlog goes stale on
its own between sessions.

**Read both and diff them before picking up work.** Anything in the notes with no
item in the backlog gets appended with the next free number, in the backlog's
own format (Observed / Cause / Approach / Constraints), with the cause **verified
in the code, not copied from the note**. The two documents are numbered
independently and drifted long ago; never renumber to reconcile them.

This is not busywork. The 2026-08-04 pass found nine unrecorded entries, two of
them defects rather than enhancements, and one of those was a constraint this
backlog had already specified and that shipped unbuilt (item 29). A note saying
"X does not work" is a bug report, and it will sit in a personal notes file
indefinitely unless someone goes looking.

## Working on this repo

Implementation follows `docs/superpowers/plans/2026-08-02-qtmaildir-v1.md` (14 tasks)
against `docs/superpowers/specs/2026-08-02-qtmaildir-design.md`.

**Treat every code block in the plan document as a draft, not as correct.** Nine defects
have already been found in code that was written confidently into it, two of them
security-relevant. Verify Qt API assumptions empirically rather than from memory — several
of those defects were wrong assumptions (e.g. `QKeySequence::fromString` never returns an
empty sequence for garbage input).

TDD, per the user's global preference. `NotmuchWorker` is unit-tested against a throwaway
notmuch database built in a temporary directory (generated Maildir + `notmuch new` +
`NOTMUCH_CONFIG` scoped to the test process), superseding the spec's original "no unit
test" position — it is the only code that writes to a notmuch index.

Work goes directly on `master`, no PR flow. Commits must be GPG-signed (`git commit -S`).
`HANDOFF.md` is local-only and gitignored; never stage or commit it.

v1 is read-and-organize only. Compose and send are v2.
