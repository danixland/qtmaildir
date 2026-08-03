# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

While the version is below 1.0.0, the configuration file format and the
keybinding action names may change in a minor release. 1.0.0 will mark the
point at which they are stable.

## [Unreleased]

Nothing yet.

## [0.1.0] - 2026-08-03

First release. Reads and organizes a local notmuch-indexed Maildir; it does
no network protocol work at all, since fetching and sending are left to
external commands.

### Added

- Permanent notmuch query bar with saved-query buttons and per-account
  scoping, over a two-pane thread list and message view.
- Threads streamed from the database in batches of 200, so a query over tens
  of thousands of threads paints its first rows immediately and fills in
  behind. Measured at 21 ms to the first batch against a 36,000-thread
  database.
- Whole-thread rendering: every message in a thread renders into one
  document, oldest first, with messages that did not match the current query
  collapsed to one-line stubs. The last message always renders expanded, so
  a thread is never nothing but stubs.
- HTML mail rendered through QtWebEngine, with a locked-down profile:
  off-the-record, no cookies, no cache, JavaScript disabled, and a
  deny-by-default request interceptor. Remote content is blocked until the
  user asks for it, which defeats tracking pixels and read receipts; the
  grant applies to one render and is never remembered.
- Inline `cid:` images served from an in-memory map scoped to the displayed
  thread. References are namespaced per message, so two newsletters sharing
  a Content-ID do not resolve to each other's images.
- Attachment handling that treats MIME filenames as untrusted: names are
  reduced to a basename and refused if the resolved path escapes the chosen
  directory.
- Tagging (archive, delete, spam, flag, read/unread, custom) over a
  multi-thread selection, applied optimistically and reverted if the write
  fails. Thread ids are resolved to message ids in a single combined query
  rather than one query per thread.
- Undo for tag changes, backed by a `QUndoStack`. Entries store thread ids
  and re-resolve them, so undo stays correct after the selection moves on.
  There is deliberately no dry-run and no destructive-action confirmation:
  undo is the better answer for a human at a GUI.
- Sync by running the user's existing script through `QProcess`, joining the
  `flock` that already serializes it against cron rather than reimplementing
  mbsync orchestration.
- Configurable keybindings via the `[keys]` section, validated against a
  known action list so a typo warns instead of binding silently.
- `--version` and `--help`.

### Known limitations

- Compose and send are not implemented; they are planned for v2 and need a
  companion send script that does not exist yet.
- A thread renders as a flat chronological list. Reply structure is
  available from notmuch but is not drawn as an indented tree.
- MIME parsing happens on the UI thread. Opening a very long thread parses
  every message in it, which could stutter; deferred until measured.
- The sync log pane shows what the sync command writes to stdout and stderr.
  A script that redirects its own output to a file will leave the pane
  empty.
