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
; Optional. Toolbar icon size in pixels, 16 to 64. Defaults to 24. The
; toolbar follows your desktop's toolbar button style, so if that is set to
; "icon only" this is the whole size of the control; 16 matches what most
; styles report, which is small for a button with no text beside it.
; toolbar_icon_size = 24
; Optional. Open the completion popup as soon as an empty query bar takes
; focus, without pressing the shortcut. Defaults to false.
; completion_on_focus = false
; Optional. How long an opened thread stays unread before it is marked read,
; in milliseconds. Defaults to 2000. Zero marks it read at once; any negative
; value turns the behaviour off, leaving threads unread until you toggle them
; with Ctrl+U. Arrowing quickly through a list marks only the thread you stop
; on, never the ones you pass through.
; mark_read_delay_ms = 2000
; Optional. How long to wait after a tag change before syncing it out for you,
; in milliseconds. Defaults to 2000. The delay is a debounce, so tagging several
; threads in a row produces one sync after you stop, not one per thread. Zero
; syncs immediately; any negative value turns this off, leaving changes for the
; Sync button or your own cron job, which is how every release before 0.16.0
; behaved. A sync already running, including one started by cron, is never
; interrupted or queued behind: the change simply stays pending.
; auto_sync_delay_ms = 2000
; Optional. What to do about unsynced tag changes when you quit. "ask" (the
; default) offers to sync, quit anyway, or stay; "always" syncs without asking
; and quits when it finishes; "never" quits silently. A sync that fails never
; closes the window, so a failure cannot discard the changes quietly.
; sync_on_exit = ask
; Optional. How the date on a thread card is written, as a QDateTime pattern
; (yyyy year, MM month, dd day, hh:mm time; anything in single quotes is kept
; literally). Absent or empty means your system locale's short format, which is
; what every other application on your desktop shows, and is the default. A
; pattern that contains no date or time field at all is refused with a message,
; since it would print the same fixed text on every card.
; date_format = yyyy-MM-dd hh:mm

[completion]
; Optional. Extra content types offered after mimetype:, APPENDED to the
; built-in list rather than replacing it, so a typo here cannot leave you
; with fewer completions than the defaults.
; Entries are separated by ',', and within an entry '|' separates the value
; from its optional description. The two characters differ because QSettings
; splits comma lists itself, so a description containing a comma would
; otherwise be read as two entries. Neither character is legal in a mimetype.
; extra_mimetypes = application/vnd.oasis.opendocument.text|ODT document, message/rfc822|forwarded mail, text/calendar

[sync]
; Optional. Omit and the Sync button disables itself with a tooltip.
; assets/mailsync.sh is the reference implementation; see "The sync command".
; command = /home/you/bin/mailsync.sh

; Optional. The sync script's log file, read to tell whether a sync started
; outside the application (a cron run, say) succeeded, so the unsynced-edits
; indicator can clear itself for one. Defaults to the path assets/mailsync.sh
; writes; set it only if you changed the script's LOGFILE.
; log = /home/you/.local/state/mailsync.log

; Section names use a dot, not a slash: QSettings treats "/" as its own
; group separator, so [account/work] would be parsed as a nested group.
[account.work]
name = Your Name
address = you@example.org
maildir = work-mail        ; relative to notmuch's database.path
drafts = Drafts            ; recorded for v2; unused today
sent = Sent                ; optional; enables the Sent button for this account
label = W                  ; optional chip text; defaults to the key
color = #2f6fa8            ; optional chip colour; generated when unset
channel = work             ; optional mbsync channel; defaults to the key

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
Important = tag:flagged

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

The button text is the key you write here, so these names are yours to
choose. `Important = tag:flagged` and `Flagged = tag:flagged` run the same
query and differ only in what the button says.

### Sent mail

A **Sent** button appears beside the saved queries once at least one account
carries a `sent` key naming its sent folder, relative to that account's
`maildir`:

```ini
[account.work]
maildir = work-mail
sent = Sent

[account.webmail]
maildir = webmail
sent = [Provider]/Posta inviata   ; nested and localised folders are fine

[account.list-only]
maildir = list-only
; no sent key: this account is simply left out of the Sent view
```

The button composes its query from those keys every time you press it, rather
than storing one, so adding an account or correcting a folder name is a config
edit and nothing else. With no account selected it shows every configured
account's sent mail; selecting one narrows it to that account. An account
without the key is omitted silently, since keeping no sent mail locally is a
legitimate setup rather than a mistake.

Sent mail is presented differently from the rest, because it reads differently:

- **A flat list, not threads.** A message you sent otherwise drags in the
  replies you received, and a view labelled Sent then shows conversations.
- **Cards name the recipients**, not the sender, which is you on every row.
- **Selecting one opens what you sent**, rather than the whole conversation it
  started.

This applies only to the Sent button. The same query typed into the bar by hand
behaves like any other query, threads and all.

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

## Tagging

Archive, delete, spam, mark-important and toggle-unread write fixed tags. For anything
else, **Ctrl+T** opens a dialog over the selected threads: type tags to add or
remove, separated by commas, or clear a checkbox to drop a tag already present.

Both fields complete against every tag in your database, which is a guard
against typing `shoppping` beside `shopping`, not a restriction: a tag that does
not exist yet is exactly what the dialog is for, so any valid name is accepted.

With several threads selected, a tag on only some of them shows a partially
checked box saying how many. **Leaving it alone changes nothing.** Check it to
apply it to all, clear it to remove it from all.

Tag names are rejected if empty, if they start with `-` (notmuch reads that as
"remove this tag"), or if they contain spaces or unprintable characters. The
dialog says which name was refused and why, and applies nothing until the whole
set is valid.

Every change goes on the undo stack, so `Ctrl+Z` reverses a mistyped tag.

## The sync command

qtmaildir does not fetch mail. `[sync] command` names a script it runs as a
subprocess, and `assets/mailsync.sh` is the reference implementation: `mbsync -a`
followed by `notmuch new`, under a `flock` so a cron timer and a click here
cannot run two `mbsync` processes over one Maildir.

```bash
ln -s "$PWD/assets/mailsync.sh" ~/bin/mailsync.sh
```

A symlink rather than a copy, so the same script serves cron and the running
application and there is only one of it to edit.

It writes to `~/.local/state/mailsync.log` itself, so a caller must **not**
redirect into that file as well. The obvious crontab line is the wrong one:

```cron
*/10 * * * * ~/bin/mailsync.sh                       # right
*/10 * * * * ~/bin/mailsync.sh >> mailsync.log 2>&1  # every line twice
```

Rotation is left to `logrotate`, which does it better than a shell script can.
An earlier version rotated by size on its own and fought `/etc/logrotate.d/`
over the same file, overwriting a compressed generation with an uncompressed
one.

Two things any replacement has to get right, both learned the hard way:

- **Print to stdout as well as any log file.** qtmaildir shows what the command
  prints. A script that redirects its own output to a log leaves the sync pane
  empty, which is what the previous version of this one did.
- **Exit non-zero when the sync failed.** qtmaildir believes the exit status:
  it reports success, clears the unsynced-changes count, and will quit on it
  during a sync-on-exit. The previous version ended in an unconditional
  `exit 0`, so a failed `mbsync` was indistinguishable from a clean run.
- **Exit 75 when another run holds the lock**, rather than 1. A skip is not a
  failure: the other run is doing the work, and with a timer every ten minutes
  a click landing inside one is routine. qtmaildir reports 75 as "a sync is
  already running" and leaves the log pane alone, where any other non-zero code
  raises an error.
- **Accept channel names as arguments, and sync everything when given none.**
  qtmaildir passes the mbsync channels of the accounts it has edited, so a sync
  after tagging one account's mail does not fetch all of them. A script that
  ignores its arguments still works, it just always syncs everything.

### Per-account sync

When tag changes are outstanding, a sync passes only the affected accounts'
channel names to the command. When nothing is outstanding the run is a plain
fetch and no names are passed, so every account is synced: narrowing a fetch to
wherever the last edit happened to be would quietly stop collecting mail
everywhere else.

The name passed is the **mbsync channel**, which is not always the account's
section key. `[account.mail-first.last]` may well be the channel
`mail-firstlast`, since a section key can carry dots that the channel does not.
Set `channel` in the account section wherever the two differ:

```ini
[account.mail-first.last]
maildir = mail-first.last
channel = mail-firstlast
```

Getting this wrong is not silent: `mbsync` fails on a channel it does not know,
and qtmaildir reports the sync as failed rather than clearing the count. An
account tag with no matching section falls back to syncing everything, since
skipping it would strand its edits with nothing to say so.

While a sync this window started is running, the status bar shows an
indeterminate progress bar. It is deliberately not a percentage: `mbsync`
reports no progress, so a bar filling left to right would be inventing one. A
sync started outside the application, by your cron timer, is not currently
visible here at all.

## Unsynced changes

Tagging changes the notmuch index at once, but the mail store only learns about
it on the next sync. The status bar therefore counts the tag changes made here
that a sync has not yet carried over, and clears the count when one succeeds. A
**failed** sync leaves the count standing, since the changes really are still
unsynced.

The count is a lower bound rather than a guarantee: an external `notmuch new`
from your own cron can carry changes over without this application noticing.

Quitting with changes outstanding asks what to do, controlled by
`[general] sync_on_exit`. See the configuration block above for the three
values. When a sync started at exit fails, the window stays open and says so
rather than quitting as though it had worked.

## Message details

The strip above the message pane says what it can say without guessing. A
thread holding **one message** shows its From, To and Cc under the subject.
A thread holding **several** shows the subject and the message count only:
From, To and Cc differ from message to message, and once you have replied there
is no single recipient the thread is addressed to, so naming one would be a
guess dressed as a fact.

The **Details...** button beside the subject, or `Ctrl+Shift+D`, opens the full
headers of every message in the thread, numbered, as read-only plain text.

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
| `Ctrl+Shift+U` | `mark_all_read` | Remove `unread` from every thread in the view |
| `Ctrl+I` | `flag` | Mark important (adds `flagged`) |
| `Ctrl+L` | `focus_query` | Focus and select the query bar |
| `Ctrl+Space` | `complete_query` | Focus the query bar and offer completions |
| `Ctrl+H` | `toggle_html` | Switch the thread between HTML and plain text |
| `Ctrl+M` | `load_remote` | Load remote images for the current thread |
| `Ctrl+T` | `edit_tags` | Add or remove any tag on the selected threads |
| `Ctrl+Shift+D` | `message_details` | Show the full headers of every message in the thread |
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

### Bundled fonts

Two fonts are bundled under `assets/fonts/`, both licensed **SIL Open Font
License 1.1**, which is compatible with the GPL and permits redistribution.
Each ships with its own licence text beside it.

| Font | Used for | Licence |
|------|----------|---------|
| Oxanium ExtraBold | the `qtMailDir` wordmark | `assets/fonts/OFL-Oxanium.txt` |
| IBM Plex Sans Regular | the placeholder pane's text | `assets/fonts/OFL-IBMPlexSans.txt` |

Both are **subsets**, not the complete fonts, because they are embedded into a
rendered document as data URIs and the whole family would be far larger than
the page using it. Oxanium is instanced to weight 800 and cut to the nine
characters of the wordmark, 43K down to 1.2K. IBM Plex Sans is cut to Latin-1
plus common punctuation rather than to the exact strings in use, 525K down to
13K: it carries interface text that will change, and a subset matching only
today's wording would break the moment a string is edited.

To regenerate either after changing what is drawn:

```bash
fonttools varLib.instancer Oxanium[wght].ttf wght=800 -o Oxanium-800.ttf
pyftsubset Oxanium-800.ttf --text="qtMailDir" --flavor=woff2 \
    --layout-features='' --desubroutinize \
    --output-file=assets/fonts/Oxanium-ExtraBold-subset.woff2
```

## Development Approach

This project is developed using AI-assisted tools. Code is generated with the help of AI based on human-provided specifications, design decisions, and iterative feedback.

All contributions are reviewed, tested, and curated by the maintainer before being included in the codebase. AI is used as a productivity and exploration tool, while human oversight remains central to all decisions.

The goal is to combine the flexibility of AI-assisted development with standard open-source practices such as transparency, review, and accountability.
