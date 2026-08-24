# qtmaildir

A Qt6 desktop mail client for reading, organizing and writing mail in a
local, notmuch-indexed Maildir. A GUI counterpart to neomutt for the parts of
mail handling that are easier with a mouse and a real HTML renderer.

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

Sending follows the same rule. The application builds the message and hands
it to a command you configure, on stdin; it speaks no SMTP itself. Any sendmail
compatible program does (msmtp, ssmtp, the real sendmail), which keeps the
credentials in that program's own store rather than in this one's config.

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

### Translations

qtmaildir ships an Italian translation. By default the language comes from the
environment:

```bash
LANG=it_IT.UTF-8 qtmaildir
```

The `language` key under `[general]` overrides that, in both directions: it
selects Italian on an English desktop, and forces English on an Italian one.
A short code or a full locale name both work.

```ini
[general]
language = it        ; or it_IT
; language = en_US   ; force English whatever $LANG says
; language = system  ; follow the environment (the default)
```

Any language with no translation runs the application in English, which is also
what happens when the compiled translation is missing. A value that is not a
locale name at all is reported as a configuration problem rather than silently
falling back, since `language = itallian` and `language = en_US` would
otherwise look identical from the outside.

`translations/qtmaildir_it_IT.ts` is tracked in git; the `.qm` it compiles to
is generated at build time and is not. Building needs Qt6's `LinguistTools`,
and a build without them is English-only rather than broken.

Adding a language means copying the `.ts`, translating it, and adding it to
`src/CMakeLists.txt` beside the Italian one. After changing any user-facing
string, refresh the `.ts` so the new string is there to translate:

```bash
lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete -locations none
```

`ctest -R translations` fails on a string that is missing a translation, and on
one that `lupdate` cannot extract at all.

### Slackware package

The SlackBuild is not in this repository. It lives in
[my-slackbuilds](https://github.com/danixland/my-slackbuilds) under
`qtmaildir/`, follows SlackBuilds.org conventions, and builds from the release
tarball named in its `.info`:

```bash
# from the qtmaildir/ directory of that repo, with the tarball beside the script
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
; Optional. Which query to open at startup, by name. Defaults to Unread.
; Matches your own saved queries first, then the built-in filters (Unread,
; Inbox, Important, Sent), so either can be named here. Falls back to the
; Unread filter if no query by this name exists, and warns if you named one
; explicitly.
; startup_query = Unread
; Optional. Which account the dropdown starts on, by key: the suffix of an
; [account.<key>] section. Unset means All accounts. Combined with a
; startup_query naming a built-in filter, this opens the application on that
; account's view of it, so startup_account = work with startup_query = Inbox
; starts in "work - Inbox". It only sets the STARTING scope: clicking a saved
; query that names no account still clears the selection, as it always does.
; startup_account = work
; Optional. Interface language, overriding the one your environment asks for.
; A locale name, short or full: "it" and "it_IT" both select Italian. The
; default is "system", which follows $LANG. Set it to a language qtmaildir
; does not ship, or to English, and the interface stays in English.
; language = system
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
maildir = work-mail        ; relative to notmuch's mail root
trash = Trash              ; Delete moves the file here. Not optional in
                           ;   practice: without it the application reports a
                           ;   config problem and Delete does not work.
drafts = Drafts            ; optional; enables the Drafts button, and where
                           ;   the composer autosaves
sent = Sent                ; optional; enables the Sent button, and where a
                           ;   sent copy is filed
inbox = Inbox              ; optional; where Restore puts a message whose
                           ;   origin is unknown. Defaults to "Inbox"

; Optional, and its absence is what makes an account receive-only: with no
; send_command the compose actions are disabled for it. The message is written
; to the command's stdin. Run WITHOUT a shell, so no pipes or redirections;
; give an absolute path and plain arguments.
send_command = /usr/bin/msmtp -a work -t
label = W                  ; optional chip text; defaults to the key
color = #2f6fa8            ; optional chip colour; generated when unset
channel = work             ; optional mbsync channel; defaults to the key

[account.personal]
name = Your Name
address = you@example.net
maildir = personal
trash = Trash
drafts = Drafts

; Optional, and global rather than per-account. Every key below shows its
; default, so an omitted [compose] section behaves exactly like this one.
[compose]
; Send an HTML part alongside the plain text one. The composer's own checkbox
; overrides this per message. It seeds New and Forward only: a Reply follows
; whether the message being answered carried an HTML part, which is a fact
; about the sender's software rather than a guess about their taste.
send_html = true

; Where the quoted original goes relative to your reply: below (the default,
; you type at the top and the quote follows) or above (bottom-posting, the
; quote first and the cursor after it).
quote_position = below

; How long the send popup counts down before the command runs, in
; milliseconds. This is the window in which Undo can still stop it; 0 skips
; the countdown and sends at once.
send_delay_ms = 5000

; How often an open composer autosaves its draft, in milliseconds. Values
; below 1000 are raised to it.
autosave_interval_ms = 30000

; Preferred account for a new message while the dropdown is on All accounts.
; Ignored when it names an account that cannot send.
; default_account = work

; Warn before attaching a file larger than this, in bytes. 25 MiB by default,
; which is the limit most providers enforce.
attachment_warn_bytes = 26214400

[tagcolors]
; Optional. Colours resolve by exact tag first, then by top-level prefix, so
; one entry covers a whole hierarchy.
shopping = #3366cc         ; also colours shopping/amazon, shopping/nike, ...
shopping/amazon = #ff9900  ; ... unless the exact tag overrides it
work = #cc4444

[keys]
Ctrl+E = archive
Ctrl+D = delete
j = next_thread
k = prev_thread
```

### Saved queries

Saved queries live in `~/.config/qtmaildir/queries.json`, not in the config
file. They are written by the **Save query** action (`Ctrl+S`), which names the
query in the bar, optionally scopes it to one account, and chooses whether it
gets a button:

```json
{
  "version": 1,
  "queries": [
    { "name": "Inbox",  "query": "tag:inbox",  "pinned": true },
    { "name": "Unread", "query": "tag:unread", "pinned": true },
    { "name": "Billing", "query": "from:billing", "account": "work" }
  ]
}
```

The order in the file is the order the buttons appear in, so rearranging them
is a matter of moving lines. `pinned` decides between a button and the **More
queries** menu, which keeps the row usable once you have more than a handful.
`account` names an `[account.<key>]` section and scopes the query to it, the
same as choosing that account in the dropdown; leave it out for a query that
spans every account.

**Sent is an entry like any other**, and the one that carries `generated`
instead of `query`:

```json
{ "name": "Sent", "generated": "sent", "pinned": true }
```

A generated query is composed from your accounts every time you click it,
rather than stored. That is why Sent has no `query` of its own: it is built
from every account's `sent` key, so adding an account or correcting a folder
name updates the button with no edit here. A stored copy of the same string
would quietly go on naming the old folder.

Being an ordinary entry, it can be reordered, renamed, unpinned or deleted like
the rest. Renaming it to `Posta inviata` changes only the label. `sent` is the
only generator today, and it is skipped entirely when no account configures a
sent folder, rather than offering a button that finds nothing.

The name is what the button says, so `Important` and `Flagged` can run the same
query and differ only in the label.

**Right-click a saved query** (a button, or its entry in the menu) to edit it,
move it between the row and the menu, or delete it. Deleting asks first: it
rewrites this file and there is no undo for it. Editing a generated entry shows
its composed query read-only, since that one is built from your accounts rather
than stored.

**Upgrading from 0.17.0 or earlier.** Saved queries used to live in a
`[queries]` section of `qtmaildir.conf`. The first launch after upgrading reads
that section, writes `queries.json` from it, and marks every entry pinned so
your buttons stay where they were. Sent is added as a `generated` entry at the
end, where its button already sat, provided an account configures a sent
folder. Your config file is not modified: the old `[queries]` section is left
exactly as it is, ignored from then on, and you can delete it by hand whenever
you like. The reason it is not removed for you is that rewriting the file would
drop your comments and reorder your keys.

One behaviour changes with the move. Buttons used to appear in alphabetical
order, because the INI backend returns keys sorted and preserving file order
would have meant hand-rolling a parser. They now follow the file.

The built-in filter whose query is currently in the bar is drawn as a pressed
button, so the row shows which view you are in. It follows the query rather
than the last button you clicked: editing the query by hand clears the
highlight, and typing a filter's own query lights it. Changing the account
recomputes it, since a filter composes with the dropdown.

`startup_query` looks at your saved queries first and then at the built-in
filters, so it can name either; yours wins if both carry the same name. A name
matching neither falls back to the **Unread** filter.

A built-in filter can be named either by its English name (`Inbox`) or by the
name shown in your own language (`In arrivo`). The English one is the safer
choice: it is the filter's identity rather than its label, so a config written
that way keeps working whatever `LANG` is set to.

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
Saved queries sit on their own row beneath it: the pinned ones as buttons, the
rest behind **More queries**. `Ctrl+S` keeps the current query as a new one.

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

## Composing

**Ctrl+N** opens a composer; Reply, Reply all, Reply without quoting and
Forward start one from the selected message. Each is a window in its own
right, so several can be open at once and the main window stays usable behind
them.

The body is **markdown**. It is sent as plain text, and the markdown is what
you typed rather than a rendering of it, so a recipient reading plain text
sees exactly the source. Tick "Also send a formatted copy" and an HTML part is
rendered from that same source and sent alongside it, in a
`multipart/alternative`; `[compose] send_html` sets the default.

Drafts autosave to the account's `drafts` folder while you type, as ordinary
Maildir files, so mbsync carries them to the server like any other message and
another client can pick one up. Closing a composer with unsaved edits asks
first, and so does quitting with one open.

Sending goes through the account's `send_command`, which receives the finished
message on stdin. An account without one is receive-only, and the composer
says so rather than failing at the end. A copy of what was sent is filed in
the account's `sent` folder.

**Send waits.** A popup counts down before the command runs, and Undo during
that window stops it and returns you to the composer with everything intact.
Nothing has reached the network until the countdown ends;
`[compose] send_delay_ms` sets how long it lasts, and 0 removes it.

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
| `Ctrl+S` | `save_query` | Keep the current query as a saved query |
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
