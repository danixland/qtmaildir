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

**Config format gotcha:** QSettings treats `/` in a section name as a group separator, so
account sections are `[account.work]`, not `[account/work]`. `childKeys` returns keys
sorted alphabetically, never in file order. Config lives at
`~/.config/qtmaildir/qtmaildir.conf`.

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
