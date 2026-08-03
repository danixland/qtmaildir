# qtmaildir — Design (v1)

Date: 2026-08-02
Status: approved, not yet implemented

## 1. Purpose

A Qt6 desktop mail client backed by notmuch: a GUI counterpart to neomutt.

It does no network protocol work. There is no POP, IMAP, or SMTP code path.
Mail arrives in the local Maildir by an external sync script, and qtmaildir
reads and organizes what is already there.

v1 is read and organize only. Compose, reply, and send are v2.

The motivating feature is proper HTML mail rendering, which terminal clients
cannot provide.

## 2. Scope

### In scope for v1

1. Query bar with saved queries; thread list; message pane.
2. Message reading: headers, HTML and plain bodies, inline images,
   attachment listing and save-to-disk.
3. Tag operations: archive, flag, read/unread, delete, spam, arbitrary
   add/remove, with undo.
4. Manual sync by shelling out to a configured sync command.
5. Configurable keybindings, neomutt-flavoured defaults.
6. Multi-account awareness for query scoping and identity.

### Out of scope for v1

Compose, reply, forward, send. Threading tree view. Address book. GPG/PGP.
Search-as-you-type. Automatic or scheduled sync. Filesystem-level message
deletion (expunge).

`+deleted` is a tag. No code path in v1 unlinks a mail file.

## 3. Decisions

Recorded with reasoning, since each closed off alternatives.

**Relationship to mailctl.** qtmaildir links libnotmuch directly for search,
read, and tag operations, and shells out only for sync. It does not call
`mailctl.py`. mailctl's safety gates (dry-run, `--confirm-destructive`,
forced account scoping) exist to restrain an agent that cannot see what it is
about to do; a human looking at a selected row does not need them. A
subprocess per query would also make the UI feel slow.

qtmaildir does not depend on the mailctl repository. The concepts it borrows
(the account map, the sync script contract) are reimplemented as config.

**Language and toolkit.** C++ with Qt6 Widgets. Qt6 is C++-native, so there
is no binding layer; libnotmuch and GMime are C libraries that link directly.
Rejected: Python + PySide6, which would be roughly half the lines but adds a
binding layer and per-keystroke overhead on a virtual list over 100k
messages.

**Layout.** Permanent query bar across the top with saved-query buttons,
above a two-pane thread list and message view. Notmuch is query-first, so the
query belongs in the primary position rather than hidden in a search box.
Rejected: classic three-pane folder layout (wrong model for notmuch);
full-window index-to-pager like neomutt (discards the preview pane, the main
thing a GUI buys).

**HTML engine.** QtWebEngine (Chromium), locked down. It is the only
maintained embeddable engine that renders real-world mail correctly.
Verified already installed as part of the monolithic `qt6` package on this
system, so it adds a link line rather than a new dependency.

Rejected: `QTextDocument`, which is dependency-free but supports only an
HTML4 subset and renders modern newsletters badly, defeating the project's
main purpose. Rejected: QtWebKit forks, which lag on security patches and are
unsuitable for hostile input. Gecko/Firefox is not an option at all — Mozilla
removed the embedding API, and Thunderbird gets Gecko only by being part of
the Mozilla build.

**MIME parsing.** GMime 3.0. Hand-rolling MIME means reimplementing RFC 2047
encoded words, RFC 2231 parameter continuations, transfer encodings, charset
conversion, and tolerance for malformed real-world mail. Rejected: kmime and
mimetreeparser, which are installed but pull in the KDE PIM stack and tie
releases to KF6.

**Config format.** INI via `QSettings`. Built into Qt, hand-editable,
plain-text and git-friendly. `QKeySequence::fromString` parses key names for
free, including chords. Rejected: TOML (no Qt parser, would need a
dependency); JSON (no comments, unpleasant to hand-edit).

**Threading.** A dedicated worker thread owns the notmuch database handle.
libnotmuch is not thread-safe and queries over a large database block. All
notmuch access happens on that thread; the UI never blocks.

**Sync.** qtmaildir runs a configured external command rather than
reimplementing `mbsync` orchestration. The decisive reason is the `flock`
guard in the existing script: it is the shared mutex between the user's
cron sync (every 10 minutes) and any manual sync. Reimplementing it internally
would place qtmaildir outside that mutex, and two concurrent `mbsync -a` runs
on one Maildir corrupt UID state. Calling the script joins the mutex for
free. Reimplementing would also not remove the dependency, since `mbsync`
remains the actual requirement.

## 4. Dependencies

All verified present on the development machine (Slackware 15.0+/-current)
on 2026-08-02:

| Dependency | Version | Notes |
|---|---|---|
| Qt6 Widgets, WebEngineWidgets | 6.11.1 | Ships inside the monolithic `qt6` package. There is no separate `qt6-webengine` package. |
| libnotmuch | 0.39 (`libnotmuch.so.5.6.0`) | Header at `/usr/include/notmuch.h`. |
| GMime | 3.2.15 | `gmime-3.0.pc` present. |

**Build note.** notmuch installs no `notmuch.pc` (verified absent: not on
disk, `pkg-config --exists notmuch` fails, absent from the package file
list). This is upstream behaviour, not a packaging fault. CMake must locate
it manually:

```cmake
find_path(NOTMUCH_INCLUDE_DIR notmuch.h)
find_library(NOTMUCH_LIBRARY NAMES notmuch)
if(NOT NOTMUCH_INCLUDE_DIR OR NOT NOTMUCH_LIBRARY)
  message(FATAL_ERROR "libnotmuch not found (need notmuch.h and libnotmuch)")
endif()

pkg_check_modules(GMIME REQUIRED gmime-3.0)
```

The build asserts `LIBNOTMUCH_MAJOR_VERSION` from `notmuch.h` so an API
change fails at compile time rather than crashing at runtime.

## 5. Architecture

One process, two threads.

```
UI thread                          Worker thread
─────────                          ─────────────
MainWindow                         NotmuchWorker
 ├ QueryBar                          └ owns notmuch_database_t*
 ├ SavedQueryBar
 ├ ThreadListView ── ThreadListModel
 └ MessageView
    ├ HeaderWidget
    ├ MessagePage (QWebEngineView)
    └ AttachmentBar

Config (INI)          MailSync (QProcess)
KeyMap                MimeParser (GMime)
```

No `notmuch_*` pointer crosses the thread boundary. Data crosses as plain
value structs:

```cpp
struct ThreadSummary {
    QString threadId, subject, authors;
    QDateTime date;
    int totalCount, matchedCount;
    QStringList tags;
};

struct MessageRef {
    QString messageId, filePath;
    QStringList tags;
};
```

Communication is queued signals in both directions.

### Files

Each has one purpose.

| File | Responsibility |
|---|---|
| `notmuchworker.{h,cpp}` | The only file that includes `notmuch.h`. RAII wrappers (`NmQuery`, `NmThreads`, `NmMessage`) so C handles cannot leak. |
| `threadlistmodel.{h,cpp}` | `QAbstractTableModel`, batch append. |
| `messageview.{h,cpp}` | Assembles a message into HTML, drives the web view. |
| `mimeparser.{h,cpp}` | Message file to parts tree, bodies, attachments. |
| `config.{h,cpp}` | INI read/write, accounts, saved queries. |
| `keymap.{h,cpp}` | Key sequence to action dispatch. |
| `mailsync.{h,cpp}` | `QProcess` wrapper around the sync command. |
| `mainwindow.{h,cpp}` | Wiring only, no logic. |

## 6. Configuration

Location: `~/.config/qtmaildir/qtmaildir.conf`.

The Maildir path is **not** configured. notmuch already stores it as
`database.path` in its own config, and libnotmuch reads it. Duplicating it
would create two sources of truth and allow the GUI to index a different tree
than the CLI. The only escape hatch is an alternate notmuch config file, so
notmuch remains the authority either way.

Per-account subdirectories **are** configured, because notmuch does not model
accounts at all; it sees one flat tree. An account is a path prefix plus an
identity.

```ini
[general]
; optional; omit to use notmuch's own database.path
; notmuch_config = /home/danix/.notmuch-config

[sync]
command = /home/danix/bin/mailsync.sh

[account.provider-personal]
name = Danilo M.
address = danix@danix.xyz
maildir = provider-personal    ; relative to notmuch's database.path
drafts = Drafts

[account.gmail]
name = Danilo M.
address = <the gmail address>
maildir = gmail
drafts = [Gmail]/Bozze

[queries]
Inbox = tag:inbox
Unread = tag:unread
Flagged = tag:flagged

[keys]
j = next_thread
k = prev_thread
Return = open_thread
a = archive
d = delete
N = toggle_unread
F = flag
/ = focus_query
h = toggle_html
u = undo
G = sync
Ctrl+Q = quit
```

`drafts` is unused in v1 (send is v2) but recorded now, since the account
layer exists in v1 and the field costs nothing.

If `sync.command` is unset or missing on disk, the Sync button disables
itself with an explanatory tooltip rather than failing at click time.

## 7. Data flow

### Query to list

Submitting a query bumps a generation counter, clears the model, and emits
`queryRequested(text, generation)`. The worker creates the notmuch query,
sorts newest-first by default, and iterates threads, emitting
`threadsReady(batch, generation)` every 200 threads. The UI discards batches
whose generation is stale. A 10k-thread query paints its first screenful in
milliseconds and fills in behind the user.

Account scoping rewrites the query as `path:"<maildir>/**" and (<query>)`.
The query bar shows what the user typed; an account chip shows the scope.

No search-as-you-type. Enter runs the query. A notmuch query per keystroke
thrashes the database.

### List to message

Selecting a row emits `threadRequested(threadId)`. The worker returns
`QVector<MessageRef>`. The UI then parses those files on the UI thread:
parsing one message is fast, and it keeps MIME work out of the database
thread. (Large-message parsing is a known future optimization, to be marked
in code rather than pre-built.)

v1 renders a thread as a flat chronological list, oldest first, matched
messages expanded and unmatched ones collapsed to a one-line stub. Notmuch
provides reply structure, but an indented tree is significant work that does
not change what is readable.

The whole thread renders as **one HTML document in one web view**, not one
view per message. A newsletter thread can hold dozens of messages, and a
`QWebEngineView` each would spawn a Chromium render process each.

Sharing one document means `cid:` references from different messages collide:
two newsletters both using `cid:logo@example.org` would resolve to whichever
part won. Every reference is therefore rewritten to `cid:<prefix>!<id>` with a
per-message prefix, and the scheme handler is keyed on that namespaced form.

Determining which messages matched requires the query: `loadThread` intersects
the user's current query with the thread, and each `MessageRef` carries a
`matched` flag.

### Message to HTML

`MimeParser` walks the MIME tree and produces a chosen body part, inline CID
parts, and attachments. Selection prefers `text/html` from a
`multipart/alternative` when HTML display is on, otherwise `text/plain`. A
per-message toggle switches to the plain version.

Plain text is escaped and wrapped in minimal HTML (`white-space: pre-wrap`)
and rendered through the same web view, so there is one render path rather
than two. Quoted lines are muted, and quote levels past the first collapse
behind a toggle.

## 8. Web view security

The most security-sensitive component in the program: a browser engine
pointed at input from strangers.

- A dedicated off-the-record `QWebEngineProfile`. No cookies, no cache, no
  local storage, nothing persistent.
- JavaScript disabled. `LocalContentCanAccessRemoteUrls` and
  `LocalContentCanAccessFileUrls` both false.
- A `QWebEngineUrlRequestInterceptor` that **blocks every request by
  default**. Only the initial document load and `cid:` references resolved
  from the current message's own parts are permitted. Remote images, CSS,
  and fonts are blocked before a connection opens, which defeats tracking
  pixels and read receipts.

  The document-load exemption is scoped to the **exact** base URL passed to
  `setHtml()`, not to the whole `qtmaildir:` scheme. A blanket
  scheme-level allow would let a hostile body reference
  `qtmaildir://anything` and have it trusted, making the interceptor's
  correctness depend on the scheme handler's. The interceptor fails closed:
  with no document URL set, every `qtmaildir:` URL is denied. `MessageView`
  must therefore call `setDocumentUrl()` with the same URL it gives
  `setHtml()`.
- When anything was blocked, the header bar shows "Remote content blocked"
  with a **Load remote content** button. Clicking it re-renders that one
  message with remote loads permitted. The grant is never sticky and never
  remembered.
- `QWebEnginePage::acceptNavigationRequest` intercepts every link click and
  hands the URL to `QDesktopServices::openUrl`, so a message can never
  replace the pane with an arbitrary page.
- A `QWebEngineUrlSchemeHandler` serves `cid:` from an in-memory map scoped
  to the currently displayed message only.

**Attachment filenames are untrusted input.** A filename in a MIME header is
attacker-controlled and may contain path separators or `..`. On save, the
name is reduced to its basename, stripped of separators, and the result is
resolved against the chosen directory; a path escaping that directory is
refused. An empty or fully stripped name falls back to a generated one.

## 9. Mutations

Queries use a read-only database handle. Tagging requires read-write, and
notmuch's write lock is exclusive process-wide, so holding it open would
block the user's cron `notmuch new`. The worker therefore opens read-write,
applies, and closes immediately, holding the lock for milliseconds.

All actions funnel through one `applyTags(msgIds, add, remove)` path:

| Action | Tags |
|---|---|
| Archive | `-inbox` |
| Mark read / unread | `-unread` / `+unread` |
| Flag | `+flagged` |
| Delete | `+deleted` |
| Spam | `+spam -inbox` |
| Custom | user-entered add/remove |

Actions apply to **every selected thread**, not just the open one. The UI holds
thread ids rather than message ids for rows it never opened, so the worker
resolves them: `applyTagsToThreads` builds one combined `thread:a or thread:b`
query rather than issuing one query per thread, which matters when archiving
hundreds of rows.

There is deliberately no dry-run and no destructive-action confirmation. Those
gates exist in mailctl to restrain an agent that cannot see its own target.
The GUI equivalent is **undo**: each applied mutation pushes its inverse onto
a `QUndoStack`, so the last tag change can be reversed. For a human, undo is
strictly better than a confirmation dialog. The undo entry stores thread ids
and re-resolves them, so it stays correct after the selection moves.

The UI updates optimistically and reverts with a status-bar error if the
worker reports failure.

## 10. Sync

`QProcess` runs the configured command. The button becomes a spinner; stdout
and stderr accumulate into an openable log pane. On exit code 0 the current
query re-runs so new mail appears. On non-zero, the status bar shows the last
stderr lines with a "Show log" link.

Sync never runs automatically in v1: no timer, no sync on startup. The user's
cron already syncs every 10 minutes, and a second scheduler competing with it is
exactly what the script's `flock` guard exists to prevent. The button means
"now".

The UI stays usable during sync, since reads do not require the write lock.

## 11. Keybindings

Actions are named strings (`next_thread`, `archive`, `toggle_html`,
`focus_query`). `KeyMap` holds a `QHash<QKeySequence, QString>` built from
defaults and then overridden by the INI `[keys]` section. A single event
filter on the main window maps sequence to action name, dispatched through a
`QHash<QString, std::function<void()>>` that widgets register into.

Chords such as `g,i` work because `QKeySequence::fromString` parses them.

An unknown action name in the config is a startup warning, not a crash. A
binding colliding with a Qt shortcut loses, and that is logged.

## 12. Error handling

| Class | Treatment |
|---|---|
| Config problems (missing sync command, bad account maildir, unparseable key) | Collected at startup, shown once in a dismissible banner. The app runs degraded. |
| Notmuch failures (database locked by cron, malformed query, database missing) | Status bar message, keep running. A malformed query is normal user input: the query bar turns red and reports what notmuch said. |
| Message failures (unparseable MIME, file missing because sync moved it) | An error card in the message pane showing the raw file path, with an "open raw" action. The pane never crashes. |

The only hard failure is libnotmuch missing or a major-version mismatch,
which aborts at startup with a clear message.

## 13. Testing

Qt Test, three targets, all runnable without a real mailbox.

1. **`test_mimeparser`** — fixture `.eml` files under `tests/fixtures/`:
   multipart/alternative, nested multipart, CID inline images, RFC 2047
   encoded subjects, quoted-printable, and a truncated/malformed message.
   Asserts part selection, decoded text, attachment enumeration, and that a
   decoded attachment written to a temporary directory matches its expected
   bytes (covering the save-to-disk path, including a hostile filename that
   must not escape the chosen directory).
2. **`test_interceptor`** — the security-critical test. Asserts that remote
   `http`/`https` are blocked, `cid:` for the current message is allowed,
   `cid:` for a foreign id is blocked, `file:` is blocked, and that enabling
   the allow-remote flag permits `http` while still blocking `file:`.
3. **`test_keymap`** — defaults load, INI overrides win, unknown actions warn
   without crashing, chords parse.

## 14. Known gaps

- ~~`NotmuchWorker` is not unit-tested.~~ **Resolved 2026-08-02.** It is
  tested against a throwaway notmuch database built in a temporary directory
  by the test fixture (`notmuch new` over a generated Maildir, with
  `NOTMUCH_CONFIG` pointed at it). The original reasoning — that testing
  requires a real database — was answered by building a fake one instead of
  skipping the tests. This is the only code that writes to a notmuch index,
  so it warranted the effort.
- Large-message MIME parsing happens on the UI thread and could stutter on
  pathological messages. Opening a thread parses every message in it, so this
  is more likely to show on a long thread than on a single message. Deferred
  until measured.
- Threading structure is flattened for display: messages appear in date order
  with no reply indentation.
- The `QtWebEngineProcess` sandbox helper's installed location was not
  verified during design. CMake handles it, but it is unconfirmed.

## 15. v2 and beyond

Compose, reply, forward, and send. Sending requires a companion script that
does not yet exist; per the project's separation of concerns, that script is
mailctl-side work and a separate project. The account and identity layer
built in v1 is what v2 builds on.

## License

To be added before first release. Default GPLv2 (v2-only) unless decided
otherwise.

## Development Approach

This project is developed using AI-assisted tools. Code is generated with the help of AI based on human-provided specifications, design decisions, and iterative feedback.

All contributions are reviewed, tested, and curated by the maintainer before being included in the codebase. AI is used as a productivity and exploration tool, while human oversight remains central to all decisions.

The goal is to combine the flexibility of AI-assisted development with standard open-source practices such as transparency, review, and accountability.
