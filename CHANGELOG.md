# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

While the version is below 1.0.0, the configuration file format and the
keybinding action names may change in a minor release. 1.0.0 will mark the
point at which they are stable.

## [Unreleased]

Nothing yet.

## [0.2.0] - 2026-08-03

Menus, a toolbar and an in-app shortcut reference, so the app is usable
without memorizing keys. Tags render as coloured chips rather than a column
of text. Three default keybindings that had never worked now do.

### Added

- Tags render as coloured chips instead of text in a column. The account tag
  sits in front of the subject in the thread list, and the functional tags fill
  a single row under the message pane, with anything that does not fit
  collapsing into a `+N` chip whose tooltip names the rest.
- `[tagcolors]` config group. Colours resolve by exact tag first, then by
  top-level prefix, so one `shopping` entry covers `shopping/amazon` and
  `shopping/nike` while `shopping/amazon` can still override its own. Built-in
  defaults cover the usual state tags; anything unconfigured gets a stable
  colour derived from its name.
- `color` and `label` keys in an account stanza, setting the account chip's
  fill and its text. `label` shortens a long key for display only and renames
  nothing in notmuch; unset falls back to the key.
- The application icon is now used: window icon, a `.desktop` entry, and
  install rules placing both into `hicolor` and `share/applications`.
- Toolbar and menu actions carry icons from the system theme, falling back to
  text where a theme lacks one.
- Menu bar covering every action: File, Edit, Message, View and Help.
- Toolbar with the frequent subset, Sync, Archive, Delete and Undo.
- **Help > Keyboard shortcuts**, listing the current bindings. Generated from
  the actions themselves, so it shows configured overrides rather than a
  hand-written copy of the defaults.
- **Help > About**.
- Default bindings for `spam` and `load_remote`, which previously had none
  and were unreachable until bound by hand.

### Fixed

- Acting on a thread now visibly changes its row. A thread tagged `deleted`
  or `spam` is filled dark red or orange, in white struck-through text, across
  every column. The tag change was already applied, but `Tags` sat after the
  stretching `Subject` column and was pushed off-screen, so Delete looked like
  it had done nothing.
- Thread list columns are Date, From and Subject, all resizable. The tags
  column is gone: spelling out a dozen tags per row consumed most of the list's
  width. Widening past the viewport scrolls horizontally rather than squeezing
  the other columns.
- Hierarchical tags in `[tagcolors]` were silently ignored. QSettings treats
  `/` in a key as a group separator, so `shopping/amazon` becomes a nested key
  that `childKeys()` never returns, and every tag containing a `/` fell through
  to its prefix.
- Three default bindings never fired. Typing a capital sends `Shift`+the key,
  but `N`, `F` and `G` were stored as the unshifted key, which no keystroke
  produces, leaving `toggle_unread`, `flag` and `sync` dead. A bare capital in
  `[keys]` is now read as `Shift`+that letter. As a side effect `y` and `Y`
  are two distinct keys rather than a collision that silently dropped one.
- Modifier shortcuts such as `Ctrl+Q` now work while the query bar has focus.
  The old event filter suppressed every binding there, not only the plain
  letters that would have interfered with typing.

### Changed

- Default bindings moved to modifier shortcuts. **A `[keys]` section written
  for 0.1.0 keeps working and keeps the old keys**, which also means it hides
  every new default: delete the section to adopt them, or rebind individually.
  Single letters are still safe to bind, since Qt suppresses a plain-letter
  shortcut while the query bar has focus.

  | Action | 0.1.0 | 0.2.0 |
  |---|---|---|
  | `next_thread` | `j` | `Ctrl+J` |
  | `prev_thread` | `k` | `Ctrl+K` |
  | `open_thread` | `Return` | `Return` |
  | `archive` | `a` | `Ctrl+E` |
  | `delete` | `d` | `Ctrl+D` |
  | `spam` | *(unbound)* | `Ctrl+Shift+S` |
  | `toggle_unread` | `N` *(never fired)* | `Ctrl+U` |
  | `flag` | `F` *(never fired)* | `Ctrl+I` |
  | `focus_query` | `/` | `Ctrl+L` |
  | `toggle_html` | `h` | `Ctrl+H` |
  | `load_remote` | *(unbound)* | `Ctrl+M` |
  | `undo` | `u` | `Ctrl+Z` |
  | `sync` | `G` *(never fired)* | `Ctrl+G` |
  | `quit` | `Ctrl+Q` | `Ctrl+Q` |

  Action names are unchanged, so no existing binding becomes invalid.
- Actions are `QAction`s dispatched by shortcut rather than a hash of
  callbacks behind an event filter, which is what lets them appear in menus.
  The hand-maintained list of registered action names is now derived from the
  actions, so it can no longer drift from them.

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
