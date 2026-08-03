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

Packaging builds do not need the tests, and building them pulls in `Qt6::Test`
to produce nothing that ships:

```bash
cmake -S . -B build -DQTMAILDIR_BUILD_TESTS=OFF
```

### Slackware package

`assets/slackbuild/` holds a SlackBuild. It follows SBo conventions, with two
deliberate departures: the tag is `_danix` rather than `_SBo`, and the package
type is `txz` rather than `tgz`, since this is not an SBo submission. `sbolint`
reports exactly those two as errors and nothing else.

```bash
cd assets/slackbuild
# fetch the source tarball named in qtmaildir.info next to the script
sudo ./qtmaildir.SlackBuild
```

`notmuch` is the only dependency outside Slackware itself; Qt6 (WebEngine
included), gmime and cmake are all stock.

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
; Optional. Starting zoom of the message pane, 0.5 to 3.0. Only the starting
; point: once you zoom with Ctrl+wheel or Ctrl+/Ctrl-, that is remembered
; separately and this value no longer applies.
; message_zoom = 1.0
; Optional. Which [queries] entry to open at startup, by name. Defaults to
; Unread. Falls back to the first saved query if no query by this name
; exists, and warns if you named one explicitly.
; startup_query = Unread
; Optional. Open the completion popup as soon as an empty query bar takes
; focus, without pressing the shortcut. Defaults to false.
; completion_on_focus = false

[completion]
; Optional. Extra content types offered after mimetype:, APPENDED to the
; built-in list rather than replacing it, so a typo here cannot leave you
; with fewer completions than the defaults.
; Entries are separated by ',', and within an entry '|' separates the value
; from its optional description. The two characters differ because QSettings
; splits comma lists itself, so a description containing a comma would
; otherwise be read as two entries. Neither character is legal in a mimetype.
extra_mimetypes = application/vnd.oasis.opendocument.text|ODT document, message/rfc822|forwarded mail, text/calendar

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
label = W                  ; optional chip text; defaults to the key
color = #2f6fa8            ; optional chip colour; generated when unset

[account.personal]
name = Your Name
address = you@example.net
maildir = personal
drafts = Drafts

[tagcolors]
; Optional. Colours resolve by exact tag first, then by top-level prefix, so
; one entry covers a whole hierarchy.
shopping = #3366cc         ; also colours shopping/amazon, shopping/nike, ...
shopping/amazon = #ff9900  ; ... unless the exact tag overrides it
work = #cc4444

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
hand-rolling an INI parser. Which query opens at startup is therefore a
separate setting, `[general] startup_query`, rather than "the first one".

## The query bar

The bar at the top takes a notmuch query and shows the matching threads.
Saved queries from `[queries]` sit beside it as buttons.

Completion helps with the syntax rather than replacing it. `Ctrl+Space`
opens the popup, and ordinary typing keeps it up to date. Candidates carry a
short description on the right, and matching is on substrings, so typing
`amazon` still finds `shopping/amazon`.

What completes:

- **Prefixes.** `tag:`, `from:`, `date:`, `mimetype:` and the rest, each with
  a one-line description of what it matches, plus the `and`, `or` and `not`
  operators.
- **Tag names**, after `tag:` and after `is:`, which notmuch treats as the
  same thing. The list is the real set of tags in your database, refreshed at
  startup, after a sync, and whenever tagging introduces a new one.
- **Dates**, after `date:`. `today`, `last_week` and similar, on either side
  of a `..` range independently, so `date:last_month..today` can be completed
  a bound at a time. Entries that are ranges in themselves, like `1week..`,
  are withheld once a range is already being written.
- **Content types**, after `mimetype:`, from a short built-in list you can
  extend through `[completion] extra_mimetypes`.
- **Account directories**, after `path:`, in both the plain and the
  recursive `<maildir>/**` form.

`from:`, `to:`, `subject:`, `folder:`, `attachment:`, `thread:` and `id:`
offer no values. Addresses and folder names would need an enumerator
libnotmuch does not expose, and the rest are free text.

notmuch's date parser also accepts free-form dates such as `2026-01-15` or
`15/01/2026..today`. Those cannot be offered as candidates, since there is no
finite list of them, so the date popup carries a footer line saying so. Type
them and they work; the popup simply has nothing to suggest.

## Tags

Tags render as coloured chips, and fall into two kinds.

**Account tags** (`account-<key>`, matching an `[account.<key>]` stanza) say
which mailbox a thread arrived in. They appear as a chip in front of the
subject in the thread list, coloured by that account's `color` key and labelled
by its `label` key. `label` changes the chip text only; the notmuch tag is
never renamed, so queries and external tagging are unaffected.

**Functional tags** say what state a thread is in. They fill one row under the
message pane, sorted, with whatever does not fit collapsing into a `+N` chip
whose tooltip lists the rest. Colours come from `[tagcolors]`, falling back to
built-in defaults for the usual state tags (`flagged`, `unread`, `deleted`,
`spam`, `attachment`, `replied`, and others), and finally to a colour derived
from the tag name so no chip is ever unstyled.

Lookup is exact tag first, then top-level prefix. One `shopping` entry
therefore covers `shopping/amazon` and `shopping/nike`, while a
`shopping/amazon` entry still overrides its own.

Note that a `/` in an INI key is a group separator to QSettings, so
`shopping/amazon = #ff9900` is stored as a nested key and written to the file
as `shopping\amazon`. It is read back correctly; the escaping is QSettings'
own.

## Attachments

A paperclip in the leftmost column of the thread list marks threads that carry
an attachment, so it is visible without opening the thread. It comes from the
`attachment` tag notmuch applies while indexing, not from parsing the message,
and costs no extra query.

Under the message pane, one **Attachments (N)** button opens a list of the
thread's attachments: which message each came from, its filename and its size,
with a **Save** for each. With more than one, **Save all** writes them into a
new subfolder named `<date> <subject>` inside a folder you choose. The subfolder
is named in the picker before you commit to a location, and an existing folder
of that name is never merged into.

Filenames in a message are untrusted, so what is displayed and written is
always a sanitised basename: an attachment named `../../etc/passwd` is written
inside the folder you chose and nowhere else. Where several messages in a thread
attach the same filename, later ones get a numeric suffix rather than
overwriting the earlier file.

Opening an attachment in its default application is deliberately not offered.
Handing a file from a stranger to `xdg-open` is a different security decision
from writing it where you asked, and is not one this program makes for you.

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
| `Ctrl+Space` | `complete_query` | Focus the query bar and offer completions |
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
