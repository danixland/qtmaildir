# qtmaildir

A Qt6 desktop mail client for reading and organizing a local,
notmuch-indexed Maildir. A GUI counterpart to neomutt for the parts of mail
handling that are easier with a mouse and a real HTML renderer.

## What it does not do

This is the important half of the description.

qtmaildir does **no network protocol work at all**. There is no POP, no
IMAP, no SMTP, and no sync implementation. Mail arrives in your Maildir by
whatever you already use (mbsync, offlineimap, fetchmail) and is indexed by
`notmuch new`. qtmaildir reads the result.

Fetching is delegated to a command you configure. Running your existing
script means joining the `flock` that already serializes it against cron; a
built-in implementation would sit outside that lock and could run two
`mbsync` processes over one Maildir, which corrupts UID state.

Sending is not implemented. Compose, reply, forward and send are planned for
v2 and need a companion send script that does not exist yet.

There is also no dry-run and no "are you sure?" on destructive actions. The
answer for a human at a GUI is undo, which is implemented, and which is
strictly better than a dialog that gets dismissed reflexively.

## Requirements

Verified against these versions on Slackware -current:

| Component | Version | Notes |
|---|---|---|
| Qt6 | 6.11.1 | Widgets and WebEngine. On Slackware both ship in the monolithic `qt6` package; there is no separate `qt6-webengine`. |
| libnotmuch | 0.39 (libnotmuch 5.6) | Installs no `notmuch.pc`, so CMake locates it with `find_path`/`find_library` rather than pkg-config. |
| GMime | 3.2.15 | Does ship `gmime-3.0.pc`. |
| CMake | 4.3.4 | 3.21 or newer. |
| GCC | 15.3.0 | C++17. |

You also need a notmuch database that is already set up and working from the
command line. qtmaildir does not create or configure one.

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/src/qtmaildir
```

Running the tests:

```bash
ctest --test-dir build --output-on-failure
```

A single test binary, for a tighter loop:

```bash
./build/tests/test_keymap
```

## Configuration

`~/.config/qtmaildir/qtmaildir.conf`, INI format.

The Maildir path is deliberately **not** configurable. notmuch already
stores it as `database.path` and libnotmuch reads it; duplicating it here
would create two sources of truth and let the GUI index a different tree
than the CLI. The only escape hatch is pointing at an alternate notmuch
config, so notmuch remains the authority either way.

Per-account subdirectories *are* configured, because notmuch does not model
accounts at all: it sees one flat tree. An account is a path prefix plus an
identity.

```ini
[general]
; Optional. Omit to let notmuch resolve its own config, which is what keeps
; the GUI and the CLI pointed at one database.
; notmuch_config = /home/you/.notmuch-config

[sync]
; Optional. Omit and the Sync button disables itself with a tooltip.
command = /home/you/bin/mailsync.sh

; Section names use a dot, not a slash: QSettings treats "/" as its own
; group separator, so [account/work] would be parsed as a nested group.
[account.work]
name = Your Name
address = you@example.org
maildir = work-mail        ; relative to notmuch's database.path
drafts = Drafts            ; recorded for v2; unused today

[account.personal]
name = Your Name
address = you@example.net
maildir = personal
drafts = Drafts

[queries]
Inbox = tag:inbox
Unread = tag:unread
Flagged = tag:flagged

[keys]
Ctrl+E = archive
Ctrl+D = delete
j = next_thread
k = prev_thread
```

Saved-query buttons appear in alphabetical order rather than file order:
QSettings returns keys sorted, and preserving file order would mean
hand-rolling an INI parser.

## Keybindings

Defaults, all rebindable through `[keys]`:

| Key | Action | Does |
|---|---|---|
| `Ctrl+J` | `next_thread` | Select the next thread |
| `Ctrl+K` | `prev_thread` | Select the previous thread |
| `Return` | `open_thread` | Focus the thread list |
| `Ctrl+E` | `archive` | Remove `inbox` from every selected thread |
| `Ctrl+D` | `delete` | Add `deleted` |
| `Ctrl+Shift+S` | `spam` | Add `spam`, remove `inbox` |
| `Ctrl+U` | `toggle_unread` | Toggle `unread` |
| `Ctrl+I` | `flag` | Add `flagged` |
| `Ctrl+L` | `focus_query` | Focus and select the query bar |
| `Ctrl+H` | `toggle_html` | Switch the thread between HTML and plain text |
| `Ctrl+M` | `load_remote` | Load remote images for the current thread |
| `Ctrl+Z` | `undo` | Undo the last tag change |
| `Ctrl+G` | `sync` | Run the configured sync command |
| `Ctrl+Q` | `quit` | Quit |

Every action now carries a default binding, and every one appears in a menu.
**Help > Keyboard shortcuts** lists the current bindings, generated from the
actions themselves, so it shows your overrides rather than these defaults.

An unknown action name in `[keys]` produces a warning at startup rather than
binding silently, so a typo is visible.

### Upgrading from 0.1.0

0.1.0 used single letters (`j`, `k`, `a`, `d`, `N`, `F`, `h`, `u`, `G`, `/`).
Those still work if you keep them in `[keys]`, and single letters remain safe
to bind: Qt suppresses a plain-letter shortcut while the query bar has focus,
so typing a query is unaffected.

Three of the old defaults never actually fired. Typing a capital sends
`Shift`+the key, but `N`, `F` and `G` were stored as the unshifted key, a
combination no keystroke produces, so `toggle_unread`, `flag` and `sync` were
dead. A bare capital in `[keys]` is now read as `Shift`+that letter, which is
what you press, so those bindings work whether you keep the old names or move
to the new defaults. Note this makes `y` and `Y` two different keys.

Tag actions apply to **every selected thread**, not only the focused one.

## Security posture of the message view

A mail client rendering HTML is a browser engine pointed at input from
strangers, and it is treated that way.

- A dedicated off-the-record `QWebEngineProfile`: no cookies, no cache,
  nothing persisted.
- JavaScript disabled. `LocalContentCanAccessRemoteUrls` and
  `LocalContentCanAccessFileUrls` both false. Plugins and fullscreen off.
- A request interceptor that **blocks everything by default**. Remote
  images, stylesheets and fonts are stopped before a connection opens,
  which defeats tracking pixels and read receipts.
- When something was blocked the header shows "Remote content blocked" with
  a **Load remote content** button. The grant applies to that one render and
  is never remembered. Switching threads discards both the grant and
  anything already fetched under it.
- Link clicks are intercepted and handed to the system browser, so a message
  can never replace the pane with a page of its own choosing.
- A whole thread renders as one document rather than one web view per
  message, which would spawn a Chromium render process per message.
  Sharing one document means `cid:` references could collide between
  messages, so every reference is namespaced per message; two newsletters
  using `cid:logo@example.org` resolve to their own images.
- Attachment filenames are treated as untrusted input. A name from a MIME
  header is reduced to its basename and the result is resolved against the
  chosen directory; anything escaping that directory is refused.

## Documentation

- `CHANGELOG.md` - what changed in each release.
- `docs/RELEASING.md` - versioning policy and the release steps.
- `docs/manual-verification.md` - results of the manual checklist run
  against a real mailbox, including the defects it caught.

## License

GPL-2.0-only. See `LICENSE` for the full text.

## Development Approach

This project is developed using AI-assisted tools. Code is generated with the help of AI based on human-provided specifications, design decisions, and iterative feedback.

All contributions are reviewed, tested, and curated by the maintainer before being included in the codebase. AI is used as a productivity and exploration tool, while human oversight remains central to all decisions.

The goal is to combine the flexibility of AI-assisted development with standard open-source practices such as transparency, review, and accountability.
