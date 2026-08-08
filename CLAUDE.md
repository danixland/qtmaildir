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
 ├ query row: QComboBox, QLineEdit,  └ owns the only notmuch_database_t*
 │   saved-query QPushButtons
 ├ ThreadListView (QTreeView) ── ThreadListModel (QAbstractItemModel)
 │   thread rows, expanding to message rows; RowStyleDelegate every
 │   column, SubjectDelegate on Subject (chip, subject, expander)
 └ MessageView (header QLabel, QWebEngineView, attachment bar, TagStrip)

Config (INI)   KeyMap   MailSync (QProcess)   MimeParser (GMime)
SyncMonitor (/proc/locks)   TagColors   QueryCompleter   ThreadCidMap
```

The query row and the message-pane header are **built inline in `MainWindow` and
`MessageView`**, not as named widget classes. Earlier revisions of this diagram
listed `QueryBar`, `SavedQueryBar`, `HeaderWidget` and `AttachmentBar`; none of
those types have ever existed, and looking for them wastes a search. The widget
classes that do exist are `MessageView`, `ThreadListView`, `TagStrip`,
`TagDialog`, `RowStyleDelegate` and `SubjectDelegate`; `TagChip` is a namespace
of painting helpers, not a widget, and `ThreadCidMap` is a struct.

**`ThreadListView` exists because a delegate cannot paint outside its column.**
The tag chips under each row are one strip spanning the whole width, so they
are drawn in the view's `paintEvent` after the cells. Consequences that are
easy to undo by accident: `SubjectDelegate` reads `AccountLabelRole`, which
belongs to the ROW, so installing it view-wide draws the account chip into
every column (a `Q_ASSERT` catches this); and because alternating colours, the
selection and the model's `BackgroundRole` are all painted per cell, the view
has to fill the strip's band itself, honouring all three or a deleted row is
cut in half and every other row shows a bare stripe.

**It is a `QTreeView` over a `QAbstractItemModel` since item 20**, because a
thread's replies are child rows and a table can neither indent nor expand. What
did NOT survive that port is anything keyed on a row NUMBER: a tree numbers rows
per parent, so row 0 exists once per expanded thread and a flat `0..N` walk
paints the first thread's strip over every one of them. The strip walk goes by
index, alternating colour follows visual position rather than `index.row()`, and
`QTableView::isRowSelected(int)` has no equivalent — use
`selectionModel()->isSelected(index)`. Row height comes from
`setUniformRowHeights` plus the delegate's `sizeHint`, since a tree has no
vertical header to carry a default section size.

**Four traps in the expander, all of which shipped a plausible-looking broken
build before being caught.** `QTreeView::drawBranches` is the documented hook and
does not work when the expander sits on a content column: it runs BEFORE the
row's cells, so the delegate's background paints over it (a 60-pixel triangle
survived as 8). `SubjectDelegate` draws it instead, from BOTH of its branches —
calling it only from the no-chip branch leaves every real row without one, since
every real row has an account chip. `setRootIsDecorated(false)`, needed to stop
the style drawing its own indicator underneath, also removes the style's HIT
AREA, so the glyph renders perfectly and is inert; `ThreadListView::mousePressEvent`
handles the click. And `isExpanded`/`setExpanded` are keyed on **column 0**, so
asking them about the subject-column index always answers false and every click
expands again instead of toggling.

**Visible, clickable and toggling are three separate properties.** A test for
one passes against the other two being broken, which happened twice in one
session: a pixel test proved the triangle was drawn while nothing could click
it, and a click test proved it opened while it could never close.

**A reply row's indent must beat the account chip's width.** A thread row draws
a chip before its subject and a reply row does not, so a reply's text starts
roughly a chip-width to the LEFT of its thread's before any indent applies.
Qt's 20px default is swallowed entirely by that difference and the replies read
as flush or outdented. `SubjectDelegate::kReplyIndent` is 72px for this reason.
Note that `visualRect` reports the indent correctly the whole time, so a
geometry probe endorses a layout with no visible nesting: assert on where the
TEXT lands.

**`paintEvent` runs AFTER the cells.** Anything it fills across a row covers the
text the delegate just drew: the reply tint filled the full row height in its
first version and erased every sender and subject, measured at zero surviving
text pixels. The fill and the thread-line stub stay in the band below the text,
where the tag strip lives on thread rows.

**No `notmuch_*` pointer ever crosses the thread boundary.** Data crosses as the plain
value structs in `src/types.h` (`ThreadSummary`, `MessageRef`, `MessageNode`,
`ActionScope`, `TagChange`), over queued
signals in both directions. `notmuchworker.cpp` is the only file that includes `notmuch.h`
outside `src/nmraii.h`; C handles are owned by the `NmQuery`/`NmMessages`/`NmThread`/…
RAII aliases there so they cannot leak.

**The one exception, and it is a double-free if undone.** Messages reached
through `notmuch_thread_get_toplevel_messages` / `notmuch_message_get_replies`
are owned by the THREAD and freed with it (`notmuch.h:1637`), so `walkReplies`
in `notmuchworker.cpp` holds them as raw `notmuch_message_t*`: an `NmMessage`
wrapper would call `notmuch_message_destroy` on memory the thread frees again.
The whole walk must finish while the `NmThread` is alive. Related: replies are
unreachable from a query walk at all — `notmuch_message_get_replies` returns
NULL for a message from `notmuch_query_search_messages` (`notmuch.h:1617-1628`),
which is why `loadThreadTree` exists beside `loadThread` rather than replacing
it.

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

**Rendering probes lie in specific, repeatable ways.** A whole session was spent chasing
a defect that did not exist because of these; each was believed until it was contradicted.

- **Counting lit pixels cannot tell bold from regular.** Antialiasing lights a similar
  number either way, so an "ink count" reads identical whichever is true, in both
  directions. It measures nothing. **Text width** distinguishes weights (277px against
  306px for one string at 12pt), and a **strict pixel diff** distinguishes renders.
- **`viewport()->render()` returns a blank image** in several ordinary situations: before
  the widget is exposed, when the content sits outside a viewport narrower than the
  columns, and sometimes with no discernible cause. A probe that reports "no ink anywhere"
  is far more likely broken than the code it is testing. Check that it finds the thing it
  expects to find *before* trusting it to report the thing it expects to miss.
- **A "saturated pixel" threshold catches antialiased edges of the selection highlight**,
  hundreds of distinct near-background colours, and will pass whatever the code does. Match
  the exact colours the model supplies instead. Two versions of one test passed under
  mutation before this was noticed.
- Every rendering test needs a **mutation check** and a guard proving it *can* fail: assert
  the geometry it depends on (a column is on screen, a row has non-zero height) rather than
  assuming it.

The bug that started all this was not in the code at all: the desktop's Qt font was
configured **Bold** in qt6ct, so every row rendered bold and `setBold(true)` changed
nothing. Before concluding a Qt facility is broken, check the desktop's own font and theme
configuration.

**`QString::arg()` does not collapse `%%` into `%`.** `printf` does, and the habit
transfers silently. In generated CSS this is quietly destructive: every percentage written
`%%` to escape it reaches the browser malformed, and a browser does not report a bad
declaration, it **drops that one rule and renders the rest**. The 0.11.0 placeholder lost
its mask, its glow and both radial gradients this way while still painting a plausible
pane, so nothing looked broken. Write `%` directly; `arg()` only ever consumes `%1`..`%99`.

The reason it survived review is worth more than the rule: **a geometry probe endorsed the
layout**, because it measured only properties that carried no percentage. A probe that
cannot see the thing that breaks will report success forever. When asserting on generated
CSS, assert on the **generated string** as well as on the rendered result, and make sure
the assertion covers the declarations that actually went missing.

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

**Then print the open items as a table, and stop.** The user picks what to work
on; do not start on one, and do not recommend a single item as though the choice
were made. Read the status table for anything not marked `done`, `dropped` or
`postponed`, and render:

| # | Item | Size | Note |
|---|------|------|------|

- **Item** is a short description, not the table's own terse title. Say what the
  user would notice, not the internal name.
- **Size** is the backlog's own: XS under an hour, S a sitting, M a session,
  `?` for an item whose shape is not known yet.
- **Note** is the one thing that decides whether it can be picked up now: a
  defect rather than an enhancement, a decision needed from the user first, a
  dependency on another item, or a constraint that makes it bigger than it
  looks. Leave it empty when there is nothing of the sort.

Flag defects separately from enhancements. They read alike in a numbered list
and do not deserve equal billing: item 28 sat as "a counter is wrong" while the
indicator was quietly lying about whether the user's work was safe to quit on.

Items marked `open, unspecified` (20, 21) cannot be planned from the backlog
alone; they need the user to describe what they pictured. Say so in the Note
rather than proposing a design.

Two gotchas when reading the status table. Item 12 lives in the **"Deferred,
unsized, or split out"** table further down, which has different columns and
carries no size, so a grep across `^| <n> |` picks it up with its description
where the size should be. And an item's status cell is prose, not a keyword:
`open, on demand` (36) and `open, unspecified` (20) are both open. Read the
cell, do not match on `open` alone.

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
