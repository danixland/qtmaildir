# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

While the version is below 1.0.0, the configuration file format and the
keybinding action names may change in a minor release. 1.0.0 will mark the
point at which they are stable.

## [Unreleased]

### Added

- The rules that tag your mail as it arrives are now visible and editable from
  Message > Tagging rules, and each one shows how much mail it matches so you
  can judge a rule before the next sync applies it. They live in a shared file,
  `~/.config/mailrules/rules.json`, which the notmuch `post-new` hook reads to
  do the tagging and which the companion `mailctl` tool can read too. The rules
  previously lived inside the hook as shell, where nothing but a text editor
  could see them. Each rule keeps a note, so the reasoning behind it (which
  senders it deliberately excludes, and why) travels with the rule instead of
  being a comment only one program could read.
- A tag change now syncs itself out, about two seconds after you stop making
  changes, instead of waiting for the Sync button or your cron job. The delay is
  a debounce, so tagging several threads in a row produces one sync rather than
  one per thread, and a sync already running is never interrupted or queued
  behind. Set `auto_sync_delay_ms` in `[general]` to change the delay, or to any
  negative value to turn the behaviour off and get the previous one back.
- The panes draw their own marks instead of borrowing font glyphs. Flagged,
  attachment, forwarded, replied and the thread expander are six SVGs shipped
  with the application, recoloured from your palette, so they look the same on
  every desktop and cannot turn into a tofu box on a font that lacks a
  codepoint. Forwarded and replied were words in the tag strip and are now marks
  beside the subject, and the message pane shows the flagged and attachment
  marks next to the subject in its header. The toolbar and the menus are
  untouched and still follow your icon theme.

### Fixed

- A sync you started no longer clears the message pane and the undo stack. It
  now reconciles the thread list the way a background sync already did, so a
  message stays on screen and open while the list updates around it. If the
  thread has stopped matching the current query, which is what happens when the
  message you are reading in Unread gets marked read, the pane keeps showing it
  and offers "Show it anyway" instead of going blank.

### Upgrading

The tagging rules moved out of the notmuch `post-new` hook and into
`~/.config/mailrules/rules.json`. The dialog reads and writes that file, but
nothing applies the rules until the new hook is installed, so this needs two
files copied from the companion `mailctl` project into your notmuch hooks
directory:

```bash
DB="$(notmuch config get database.path)"
cp post-new mailrules.py "$DB/.notmuch/hooks/"
chmod +x "$DB/.notmuch/hooks/post-new"
```

Keep a backup of your previous hook until a sync has run with the new one. Your
existing rules do not convert themselves: each `notmuch tag` line becomes one
entry in the JSON file, with the part after `tag:new and` as its query. Leave
`tag:new` out of the stored query, the hook adds it, and do not carry over the
final `notmuch tag -new` line, which the hook now does itself.

Two behaviours of the new hook are worth knowing. It refuses to remove `unread`
or `inbox`, since neither belongs in an unattended job that runs every ten
minutes, and it will not consume the `tag:new` marker if the rules file fails
to load, so a broken file delays tagging rather than losing it.

## [0.15.0] - 2026-08-11

Sent mail becomes a place you can go. A Sent button beside Inbox, Unread and
Important shows what you sent across every account, as a flat list of
recipients rather than as threads, because a message you sent is not a
conversation you are following. The blank pane counts sent mail and drafts
alongside unread, flagged and inbox, and the date on a thread card can now be
given a format of your own.

### Upgrading

Both new features read per-account folder names, and neither appears until you
name them. Add `sent` and `drafts` to each `[account.*]` section that has them:

```ini
[account.example]
maildir = example
sent = Sent
drafts = Drafts
```

Providers that nest a localised folder under a bracketed parent take the full
path, e.g. `sent = [Provider]/Posta inviata`. An account that keeps no sent or
drafts folder locally simply omits the key: the Sent button and the counted
lines are absent rather than empty, and nothing warns about it.

`drafts` was already accepted and documented as having no effect. If you set it
earlier, it now counts drafts on the blank pane.

### Added

- **A Sent view.** A new `sent` key on each `[account.*]` names that account's
  sent folder, and a Sent button beside Inbox, Unread and Important shows what
  you sent across every account that configures one. Selecting an account
  narrows it to that account. The button is absent entirely when no account has
  the key.
- Sent mail is shown as a flat list rather than as threads, and the cards name
  the **recipients** instead of the sender, which is you on every row.
  Selecting one opens what you sent, not the conversation your message started.
- The blank message pane counts **sent mail and drafts** beside unread, flagged
  and inbox. Both are composed from each account's `sent` and `drafts` folder
  rather than from a tag, so they follow the same per-account configuration the
  Sent view uses. A line is absent entirely when no account configures that
  folder, rather than reading 0. The `drafts` key was already accepted and
  documented as unused; it now has an effect.
- `[general] date_format`, an optional pattern for the date on a thread card.
  Absent or empty keeps the system locale's short format, which is unchanged
  and remains the default. A pattern containing no date or time field is
  refused with a message rather than printing the same fixed text on every
  card.

### Changed

- The Sync button carries the refresh icon instead of a mailbox one. With the
  toolbar following the desktop's "icon only" style, the icon is the whole
  control, and a mailbox glyph read as "mail" rather than "fetch again".

## [0.14.0] - 2026-08-10

The thread list stops going stale. A sync running in the background now
updates it directly: new mail appears, threads you have read leave, and
whatever you were in the middle of stays where it was. Reading a thread out of
a view no longer strands you either, since the message pane says when the
thread it is showing has left the list and offers to bring it back.

### Upgrading

Nothing to change. The "Background sync completed. Press Enter in the query bar
to refresh." message is gone because there is nothing left to press Enter for;
if that keystroke is in your fingers, it still re-runs the query and is now
simply redundant.

### Changed

- The thread list now follows a background sync on its own. New mail appears
  where the sort puts it, threads that stopped matching leave, and threads whose
  state changed repaint, with no keystroke. Previously the status bar asked you
  to press Enter, because refreshing meant re-running the query, which cleared
  the list, the selection and the message pane; the list is now reconciled
  instead, so an expanded thread stays expanded, the selection stays put and the
  message being read stays on screen. The "Background sync completed" message is
  gone: a refresh that changes nothing should be invisible.

### Added

- A notice above the message when the thread being read no longer matches the
  current query, with a button that brings it back. Reading a thread to the end
  of an Unread view now removes it from the list as it should, and this is the
  way back to it: the whole thread is listed, and the message that was on screen
  is re-selected, so returning to reply four of eight lands on reply four.

## [0.13.0] - 2026-08-10

The thread list stops being a table. Each thread is a card of three lines,
carrying its sender and date, its subject with the marks that apply to it, and
its tags; expanding one shows its replies indented beneath it rather than as
more rows of the same grid. The account colour moves from a chip in front of
every subject to a bar down the card's edge, and threads can now be sorted
oldest first.

This replaces the presentation built for 0.12, which was finished and working
and read as a table of records rather than as a list of conversations. The
model, the reply walk and the keyboard navigation underneath it are unchanged.

### Changed

- **The thread list is now a list of cards rather than a table of columns.**
  Each thread shows its sender and date, its subject with the flag, attachment
  and reply-count marks, and its tags, on three lines at one uniform height.
  Expanding a thread shows its replies indented under a continuous spine,
  carrying only the tags the thread itself does not have, and without the `Re:`
  prefix every reply used to repeat.
- **Threads can be listed newest or oldest first**, from a new control beside
  the query bar. The choice is remembered between sessions.
- **An account's colour now runs down the left edge of its threads**, and down
  the spine of their replies, replacing the account chip that used to sit in
  front of every subject. The account dropdown shows the same colours, so which
  colour means which account is readable in one place.
- **Alt+Up and Alt+Down step between threads**, alongside the existing Ctrl+J
  and Ctrl+K. Plain Up and Down now step message by message through an expanded
  thread, which is the view's own behaviour rather than a binding.
- **Selecting a thread now shows its first message**, not the whole
  conversation. The card at the head of a thread is that message, and the
  replies under it are the rest; a thread's opening message was previously
  unreachable, since the pane rendered every message at once and no row in the
  list offered it on its own.
- **Dates follow the desktop's locale.** They were written in a fixed
  `yyyy-MM-dd hh:mm` regardless of locale, which is not the format most desktops
  use.
- **The reply count reads as a control.** It is drawn as a rounded chip saying
  "3 replies", rather than as a bare number beside the subject that gave no hint
  it could be clicked.
- **An out-of-range `message_zoom` now says so.** The documented 0.5 to 3.0
  range was already enforced on the way to the web view, so a `message_zoom` of
  500 rendered at 3.0 rather than unusably, but nothing reported that the value
  in the file was not the value on screen. It is now listed with the other
  configuration problems at startup.

### Fixed

- **The message pane no longer comes back as a sliver.** A splitter position is
  saved in pixels, so one saved in a wide window did not fit a narrower one: the
  thread list kept its full saved width and the message pane got whatever was
  left, in one real case 29px. The pane now has a minimum width and cannot be
  collapsed, which covers the restore and the equivalent drag.

- **Clicking a thread no longer scrolls the list sideways.** A card is exactly
  the width of the pane, so there is nowhere to scroll to.
- **Next and previous thread no longer step onto a reply** when a thread is
  expanded. They skip message rows, so they keep meaning thread-to-thread.
- **Threads without reply structure now expand.** A thread whose messages carry
  no usable `In-Reply-To` header, which notmuch reports as a flat list rather
  than a tree, advertised a reply count that opened onto nothing. Its replies
  now appear, indented under the same spine as any other thread's.
- **The date is no longer clipped on unread threads.** Unread rows draw in bold,
  which is wider than the font the layout measured, so the leading digit of the
  year was cut off.
- **The account colour down a card's edge is now visible.** It was drawn in a
  colour blended so far toward the pane's background that it matched it exactly
  on a dark theme, and account colours, which are chosen to be readable behind
  chip text, are muted enough that a few pixels of one barely registers.

### Upgrading

- **Saved thread-list column widths are ignored.** There is one column now, so
  the `threadlist/header` and `threadlist/columns` entries in
  `~/.local/state/qtmaildir/uistate.conf` no longer do anything. Nothing needs
  to be done: they are read past and can be left in place or deleted. Window
  geometry, the splitter position and the message zoom are unaffected.

## [0.12.1] - 2026-08-09

A single fix for a defect 0.12.0 introduced.

### Fixed

- **Archive and Mark all read no longer share an icon.** Both used the same one
  in 0.12.0, which was harmless while the toolbar showed text beside every icon
  and ambiguous once it follows a desktop set to icon-only. Archive now uses a
  distinct icon, and a test compares every action's icon against every other so
  the next duplicate fails the build rather than shipping.

## [0.12.0] - 2026-08-09

Syncing gets narrower and more honest: a sync fetches only the accounts you
have actually edited, and one run from cron now clears the unsynced-changes
indicator instead of leaving it claiming work that had already gone out.
The toolbar gains icons throughout and defers to your desktop's own style.

### Upgrading

Two things you may want to change in your own config, neither of which breaks
if you leave it alone:

- **Saved-query button labels are your own key names.** If you have
  `Flagged = tag:flagged` under `[queries]`, that button still reads "Flagged"
  after the action was renamed to "Important". Rename the key to
  `Important = tag:flagged` if you want the two to agree; the query is
  unchanged either way.
- **The toolbar now honours your desktop's toolbar button style.** If that is
  set to "icon only" the toolbar loses its text labels, which it previously
  ignored. Icons are 24px by default; `[general] toolbar_icon_size` changes it.

### Added

- **Threads expand in the list to show their replies.** A thread with more than
  one message carries an expander; opening it lists the replies as indented rows
  beneath it, marked with a thread line, a tinted background and smaller text.
  The replies are fetched when you expand, not with the query, so a large result
  still paints immediately.
- **Selecting a reply opens that message on its own**, rather than the whole
  conversation, which is the point of having message rows at all.
- **Actions follow what you selected, and the status bar says what they will
  touch.** A thread row acts on the whole thread and reports "1 thread selected
  (7 messages)" before and "(whole thread)" after; a reply row acts on that one
  message. Both are undoable. There is no confirmation dialog, deliberately:
  undo is this application's answer to a mistaken action, and naming the scope
  is what makes it usable.
- **A sync now fetches only the accounts you have edited.** Tagging mail in one
  account and syncing no longer pulls every other account as well. A sync with
  nothing outstanding is a plain fetch and still covers everything, since
  narrowing that to wherever the last edit happened to be would quietly stop
  collecting mail everywhere else.
- **An optional `channel` key per account**, naming the mbsync channel when it
  differs from the section key. It defaults to the key, so accounts whose two
  names already agree need no change. The two can genuinely diverge: a QSettings
  section key may carry dots that the channel does not, and mbsync treats an
  unknown channel as fatal rather than skipping it.
- **`assets/mailsync.sh` takes channel names as arguments**, syncing all
  channels when given none. A replacement sync script that ignores its arguments
  still works, it just always syncs everything.
- **Every action now carries an icon**, where before only eight of twenty-four
  did and adjacent menu entries disagreed with each other. Icons come from the
  desktop's icon theme; one the theme does not provide falls back to text alone.
- **An optional `[sync] log` key**, naming the sync script's log file. It
  defaults to where `assets/mailsync.sh` writes, and only needs setting if you
  changed the script's `LOGFILE`.
- **An optional `[general] toolbar_icon_size` key**, 16 to 64 pixels, defaulting
  to 24. Most styles report 16, which is a small target now that the toolbar can
  be icons only. Out-of-range values are clamped and reported rather than
  applied.

### Changed

- **"Flag" is now "Important"**, on the menu entry, the undo history and the
  star column's tooltip. `Ctrl+I` is unchanged, and so is the `flagged` tag
  itself: neomutt, your saved queries and anything else reading the same Maildir
  keep working. The `flag` action name in `[keys]` is also unchanged, so
  existing bindings are untouched.
- **The toolbar follows your desktop's toolbar button setting** instead of
  always showing text beside icons. If your desktop is set to "Icon only", the
  toolbar is now icons only; it previously ignored that.

### Fixed

- **A sync run from cron now clears the unsynced-changes indicator.** Edits made
  in the application reach the mail store through any sync, but only a sync
  started from the window cleared the count, so the indicator kept reporting
  work that had already gone out and the quit prompt offered to sync for it.
  A failed sync, or one whose outcome cannot be read, still leaves the count
  standing.

## [0.11.0] - 2026-08-07

The empty message pane now carries the application's own identity and the
counts worth knowing, and Escape finally does what it does everywhere else.

### Added

- **The blank message pane shows a placeholder** instead of nothing: the
  wordmark over a soft grid, the number of unread, flagged and inbox threads,
  and a footer with the version and a link to the website. Each count is a link
  that runs its query. A sync line appears only when something needs attention,
  either that the last sync failed or that edits are waiting to go out, so the
  pane cannot turn into wallpaper that stops being read. It follows the desktop
  between a light and a dark version of the brand palette.
- **A Maildir overview**, under Help. Total messages, threads and tags from
  notmuch, plus the configured accounts, since notmuch does not model accounts
  at all. It opens straight away and fills the counts in when they arrive,
  rather than making the window wait: counting every message in a large
  database is not instant. A count that cannot be answered reads as unknown,
  never as zero.
- **Escape clears the selection as well as blanking the pane.** Blanking while
  the row stayed highlighted read as half an action. The narrower behaviour is
  still there on `Shift+Esc` for anyone who wants it, and either can be rebound
  in `[keys]` as `clear_selection` and `clear_pane`.
- **The thread list shows each thread's tags**, as small coloured chips in a
  strip under the row, using the same colours as the message pane. Rows are
  taller to make room, and alternate in colour so one can be followed across
  the width. The strip spans the whole row rather than sitting inside the
  subject column, so a well-tagged thread does not lose its last tags off the
  edge. Tags the row already shows another way are left out: the account, the
  flag, the attachment, and read state.
- **A star column for flagged threads**, beside the existing attachment
  paperclip.
- **Mark all read**, on the toolbar, the Message menu and `Ctrl+Shift+U`. It
  acts on every thread in the current view rather than the selection, as one
  write and one undo entry, so a single `Ctrl+Z` puts back a view of 400
  threads. It stays disabled until the query has reported its total: threads
  arrive in batches, and an action that says "all" must not run against
  whatever happened to have loaded. A view with nothing unread does nothing and
  says so, rather than pushing an undo entry that restores nothing.
- **The status bar says which account is syncing**, then that notmuch is
  reindexing, instead of "Syncing..." for the whole run. The account name and
  the progress both come from mbsync's own output as it streams.

### Changed

- **The sync script runs `mbsync -V`.** Without it mbsync prints nothing at all
  until it exits, then a single summary line, so a run of over a minute was
  silent and there was nothing for the status bar to report. This is not a
  buffering problem and `stdbuf` does not help.
- **Read threads are dimmed in the list**, so unread mail stands out by colour
  as well as by weight. Bold alone was the only distinction, which leaves
  nothing to see when the desktop's own font is configured bold. Unread keeps
  the palette's text colour and read recedes toward the background; bold still
  applies on top.

### Fixed

- **The message pane follows the desktop theme.** Its stylesheet hardcoded
  light-theme greys and set no background at all, so plain-text mail rendered
  as black on white inside a dark window. The colours now derive from the
  palette, with the secondary ones blended from it rather than fixed, since a
  grey chosen to read as subtle on white is nearly invisible on near-black. A
  message that brings its own HTML still brings its own colours: that styling
  is deliberately left alone.
- **A message whose HTML body carries a `Content-Id` renders**, instead of
  opening blank with the app reporting no HTML part. A content id makes a part
  referenceable, not undisplayable, and setting one on the body is legal and
  common in bulk-sender output.
- **Removing a tag suggests only the tags the selected threads carry**, rather
  than every tag in the database. Adding still reaches the whole vocabulary,
  since naming a tag that does not exist yet is what that field is for.
- **A sync that finishes quickly no longer loses its own progress.** The
  per-run reset happened after the process launched, so a run that delivered
  its output before control returned wiped the state those lines had produced.

### Internal

- The test suite pinned itself to the offscreen platform. `ctest` sets no
  platform of its own, so the result depended on how the suite was invoked:
  green when run by hand with `QT_QPA_PLATFORM=offscreen` and red under `ctest`
  in the same tree. The popup test at the centre of it also now checks its own
  geometry, because the compositor had been handing it a popup more than twice
  the width it exists to test, which would have passed for the wrong reason had
  the grab succeeded.

## [0.10.0] - 2026-08-06

Tag edits no longer stall the window when a background sync is running, and
Sync is one control instead of two that disagreed.

### Changed

- **A tag edit made while a background sync runs is held and sent when the sync
  finishes**, instead of being sent straight into a database open that blocks.
  The open never failed, it blocked and then succeeded, and because the worker
  is a single thread everything queued behind it waited too: the message pane
  froze on whichever thread was selected first and replayed the queue on
  release. The row keeps its tag in the meantime and the edit still counts as
  unsynced, so the quit prompt cannot let work leave silently.
- **One Sync control.** The button beside the query bar is gone; Sync lives on
  the toolbar, the File menu and its shortcut. The two used to behave
  differently, and only the button showed the sync log, disabled itself, or
  reported that a sync was already running.
- **The saved-query buttons moved onto the query row**, after the query field,
  so the bar is framed by the account selector on one side and the saved
  queries on the other. The row they occupied is gone and the thread list has
  that space.

### Added

- **A clear button in the query field**, Qt's own, drawn inside the field and
  shown only when there is something to clear.

### Fixed

- **Sync stayed clickable from the toolbar and the menu during a background
  sync.** 0.9.0 disabled the button beside the query bar and nothing else, so
  every other route to Sync could still start a run that could only be skipped.
- **A rejected tag write no longer clears the whole undo stack**, only the edit
  that was rejected.

### Internal

- Two tests depended on the machine they ran on: one read the live
  `/proc/locks` and failed whenever a real sync happened to be running, the
  other asserted a window wider than the offscreen platform's screen. Neither
  indicated a fault in the application.

## [0.9.0] - 2026-08-04

Small corrections from using 0.8.0, most of them things the application was
saying that were not quite true.

### Added

- **Escape blanks the message pane**, on `clear_pane`, rebindable like any other
  action. A view change only: the selection, the query and the undo stack are
  untouched.
- **Delete is now a toggle.** Pressing it on a thread that is already deleted
  removes the tag instead, which is the natural way to say "no, put it back".
  Over several selected threads it picks one direction for all of them: it
  undeletes only when every selected thread is already deleted, so one keystroke
  can never leave the selection in two states.

### Changed

- **Transient status messages expire** after a few seconds, leaving the thread
  count behind. Messages that describe a state rather than an event do not:
  "Syncing...", the selection count, and a sync failure, which must not vanish
  before it is read.
- **The Sync button is disabled while a background sync holds the lock**, since
  starting one then could only produce a skip. It stays usable where the lock
  cannot be observed at all, because nothing is known there and a permanently
  dead button would be worse.
- **The quit prompt names its default button.** The default was always set, and
  Qt agrees it is set, but the active style draws no visible mark, so the button
  now says so in words rather than fighting the theme.

### Fixed

- **Unsynced changes are counted as net state rather than as writes.** Letting a
  thread be marked read automatically and then pressing Ctrl+U put it back
  reported two unsynced changes with the mail store exactly where it started.
  An edit and its inverse now cancel, per message and per tag, so two different
  tags on one message still count as two.

## [0.8.0] - 2026-08-04

Selecting more than one thread stops being a secret, and the window notices
the syncs it did not start.

### Added

- **Select all threads**, on **Ctrl+A** or Edit > Select all threads. Selecting
  several threads always worked with Ctrl+click and Shift+click, but nothing in
  the interface said so, and every tag action was reachable only from the
  keyboard. Rebindable as `select_all` like any other action.
- **A right-click menu on the thread list**, holding archive, delete, spam,
  mark read/unread, flag, edit tags and select all. It is built from the same
  actions as the menu bar, so a rebinding in `[keys]` shows the new shortcut
  here too. Right-clicking inside a multi-thread selection keeps that
  selection rather than narrowing it to the row under the pointer.
- **A selection count in the status bar** while a selection is being built, and
  a note in Help > Keyboard shortcuts describing Ctrl+click and Shift+click.
  Mouse gestures belong to the view rather than to any action, so they cannot
  appear in the generated shortcut table.
- **Awareness of syncs started elsewhere.** A cron sync every ten minutes used
  to come and go unnoticed. The status bar now reports a background sync while
  it runs and when it finishes, and suggests refreshing. It deliberately does
  not refresh on its own: re-running the query clears the undo stack, the
  selection and the message pane, which is right for a query you typed and
  hostile for one a timer fired.
- **`assets/mailsync.sh`**, the reference sync command, moved here from the
  companion `mailctl` project. It never belonged there: `mailctl` does not call
  it, while qtmaildir runs it as a subprocess and depends on how it behaves.
  Symlink it into `~/bin` rather than copying, so one script serves both cron
  and the application.

### Changed

- **Selecting several threads no longer opens them.** The message pane blanks
  for a multi-thread selection instead of loading each row as the selection
  passes over it, and no thread selected that way is marked read. Selecting is
  not reading, and a selection gesture must never change what is in the
  Maildir. Narrowing back to a single thread opens it as before.

### Fixed

- **The sync log pane stayed empty**, listed as a known limitation since 0.1.0.
  The reference `mailsync.sh` redirected all its output to a log file, so the
  subprocess printed nothing for the pane to show. It now writes to both.
- **A failed sync reported success.** That script ended in an unconditional
  `exit 0`, so qtmaildir could not tell a clean sync from a broken one: it
  cleared the unsynced-changes count either way, and would have quit on a
  sync-on-exit that had not synced anything. It now exits with the real status.
- **A sync you started reported itself as a background one**, replacing its own
  result a moment after it finished. Whether a sync was local was decided when
  its lock was released, by which time the process had already exited and the
  answer was always "not ours".

## [0.7.0] - 2026-08-04

Tagging stops being limited to the five tags someone chose in advance, and the
application admits when your work has not reached the mail store yet.

### Added

- **An Edit tags dialog**, on **Ctrl+T** or Message > Edit tags. Type tags to
  add or remove, separated by commas, or clear a checkbox to drop a tag already
  on the selection without retyping it. Until now archive, delete, spam, flag
  and toggle-unread were the only tags reachable from the UI, and applying any
  other one meant leaving for a terminal.
- Both fields **complete against every tag in the database**, matching on
  substrings so `amazon` finds `shopping/amazon`. Completion is a guard against
  typing `shoppping` beside `shopping`, not a restriction: a tag that does not
  exist yet is exactly what the dialog is for.
- With several threads selected, a tag on only some of them shows a partially
  checked box saying how many. **Leaving it alone changes nothing.** Check it to
  apply to all, clear it to remove from all.
- Tag names are refused if empty, if they start with `-` (notmuch reads that as
  "remove this tag", so such a tag is a trap), or if they contain spaces or
  unprintable characters. Nothing is applied until the whole set is valid, since
  a half-applied change leaves you unable to tell which half landed.
- **The status bar counts tag changes a sync has not carried over**, and clears
  the count when one succeeds. A failed sync leaves it standing.
- **Quitting with changes outstanding asks what to do**, via the new
  `[general] sync_on_exit`: `ask` (the default) offers to sync, quit anyway or
  stay; `always` syncs without asking; `never` quits silently. A sync started at
  exit holds the window open until it finishes rather than being killed
  mid-run, and one that fails does not quit.

### Fixed

- In the tag fields, only the first tag completed. `QLineEdit::setCompleter`
  matches against the widget's entire text, so once a field read `unread, fl`
  that whole string was matched against the tag names and nothing was offered
  again. The same defect the query bar hit in 0.5.0, in a second place.

### Notes

The unsynced count is a lower bound rather than a guarantee: an external
`notmuch new` from your own cron can carry changes over without the application
noticing.

The exit prompt is not a destructive-action confirmation of the kind this
project avoids. Those cover tag mutations, which keep undo instead of a dialog.
This asks about losing work at the one point where undo cannot help.

## [0.6.0] - 2026-08-04

Two things the app knew and would not say: who a message was addressed to,
and whether you had read it.

### Added

- **From, To and Cc in the message header.** All three were parsed on every
  message and then discarded before rendering. A thread holding one message
  now shows them under the subject.
- A thread holding **several** messages still shows only the subject and the
  count. From, To and Cc differ per message, and once you have replied there is
  no single address the thread is addressed to, so naming one would be a guess
  presented as a fact. An empty Cc omits its row rather than printing a label
  with nothing after it.
- **A details dialog**, behind a `Details...` button beside the subject or
  `Ctrl+Shift+D`, listing Subject, From, To, Cc, Date and Message-Id for every
  message in the thread, numbered. Read-only plain text: these values come from
  strangers, and the format that cannot interpret markup is the right one for
  showing them verbatim.
- **An opened thread is marked read after a delay**, 2 seconds by default.
  Arrowing quickly through a list marks only the thread you stop on, never the
  ones you pass through. Configurable through `[general] mark_read_delay_ms`:
  zero marks read at once, and any negative value turns the behaviour off.

### Notes

The automatic mark-read is deliberately **not** on the undo stack. Undoing an
action you never took is worse than leaving a thread read, and `Ctrl+U` already
puts it back. Marking a thread unread by hand cancels any pending timer, so the
key cannot be silently reversed a moment later.

**HTML messages already opened as HTML**, which a backlog item had doubted.
Verified against real mail; no code changed. No preference was added for
defaulting to plain text, since `Ctrl+H` already switches a thread by hand.

## [0.5.0] - 2026-08-04

Completion in the query bar. The point is not to save typing but to make the
notmuch query language discoverable: every candidate carries a description, so
the bar teaches the syntax to someone who has never written a notmuch query.

### Added

- **Query prefixes** complete with a description each: `tag:`, `is:`, `from:`,
  `to:`, `subject:`, `date:`, `attachment:`, `mimetype:`, `folder:`, `path:`,
  `thread:`, `id:`, and the `and` / `or` / `not` operators. The list is
  hardcoded, since notmuch exposes no way to enumerate its own prefixes.
- **Tag names** after `tag:` and `is:`, which notmuch treats as synonyms. The
  list is the real set of tags in the database, refreshed at startup, after a
  sync, and whenever a tag mutation introduces one that was not there before.
- **Dates** after `date:`, symbolic and relative, completing each bound of a
  `..` range independently. Entries that are themselves open-ended ranges,
  like `1week..`, are withheld once a range is already underway, since they
  would produce malformed queries inside one.
- **Content types** after `mimetype:`, from a built-in list extensible through
  the new `[completion] extra_mimetypes` key. Entries are appended to the
  built-ins rather than replacing them, so a typo cannot leave you with fewer
  completions than the defaults.
- **Account directories** after `path:`, in both the plain and the recursive
  `<maildir>/**` form.
- `complete_query`, bound to **Ctrl+Space**, opens the popup on demand.
- `[general] completion_on_focus`, off by default, opens it as soon as an empty
  query bar takes focus.
- Accepting a prefix chains straight into its values, so taking `tag:` offers
  the tag list without a second keystroke.

### Fixed

- Return in the query bar ran nothing and moved focus to the thread list.
  Return is bound to `open_thread` as a window shortcut, and a shortcut is
  dispatched before the focused widget sees the key; Qt withholds plain-letter
  shortcuts from editable widgets, but Return is not a letter and got no such
  protection. The query bar now claims the key back while it has focus.
- Tab and the arrow keys crashed the application outright while the popup was
  open. `QCoreApplication::sendEvent` re-runs application-level event filters,
  so the filter forwarding a key to the popup was handed the same key straight
  back, recursing until the stack was exhausted.

### Notes

The example configuration in the README had two keys that were live rather
than commented, so copying the block activated a sync command and three
mimetypes the reader never chose. Both are commented now.

`from:` and `to:` complete no addresses: libnotmuch exposes no call to
enumerate them. `folder:` completes nothing either, as a Maildir folder name
is not something the configuration can enumerate. Saved query names are
deliberately absent, a name not being valid notmuch syntax.

## [0.4.1] - 2026-08-03

Packaging only. No change to the application itself.

### Added

- A SlackBuild under `assets/slackbuild/`, with the usual `.info`,
  `slack-desc`, `doinst.sh` and `README`. It follows SBo conventions except
  for the tag, `_danix` rather than `_SBo`, and the package type, `txz` rather
  than `tgz`, since it is not an SBo submission.
- `QTMAILDIR_BUILD_TESTS`, on by default. Turning it off skips the test suite
  and its `Qt6::Test` dependency, which a packaging build has no use for.
  0.4.0 accepted this flag but ignored it, because the option did not exist in
  that tarball; this release is the first where it takes effect.

## [0.4.0] - 2026-08-03

Attachments become reachable. They were parsed all along and there was simply
no way to get at one, and no way to tell a message had any without opening it.

### Added

- A paperclip column marks threads carrying an attachment, so it is visible
  without opening the thread. Driven by the `attachment` tag notmuch already
  applies, so it costs no extra query.
- The message pane lists attachments behind one **Attachments (N)** button:
  a dialog with the message number, filename and size, a **Save** for each,
  and **Save all** when there is more than one.
- **Save all** writes into a new subfolder named `<date> <subject>` inside a
  folder you pick, so a thread with sixteen files does not scatter them among
  whatever is already there. The folder is named in the picker before you
  choose, and an existing folder of that name is never merged into.

### Fixed

- **Attachments were unreachable.** The attachment bar had been created and
  added to the layout since it was written, and nothing ever populated it.
- **Saving several attachments could destroy files.** Messages in one thread
  commonly attach the same filename, and each save overwrote the previous one
  while still reporting success: sixteen attachments produced ten files. Batch
  saves now add a numeric suffix instead, and keep a compound extension like
  `.tar.gz` whole.
- **A `Date:` header carrying a timezone comment lost its date.** `+0200
  (CEST)` is legal and common, but Qt rejects the whole header rather than the
  comment, so those messages got no date prefix on their folder.

### Changed

- Thread-list column widths reset once on first launch after upgrading. The
  saved layout is from before the paperclip column existed, and applying it
  would have shifted every width onto the wrong column.

## [0.3.0] - 2026-08-03

The app remembers how you left it. Window, splitter and column sizes survive
a restart, the message pane owns its zoom and keeps it, and startup opens the
query you asked for rather than whichever one sorted first.

### Added

- Window geometry, splitter position and thread-list column widths persist
  across restarts. Machine-written state lives in a separate file,
  `~/.local/state/qtmaildir/uistate.conf`, and never touches the hand-edited
  config: a base64 geometry blob does not belong in a file you edit, and
  rewriting that file on exit would drop its comments and key order.
- The message pane owns its zoom, which is remembered across restarts. It was
  previously the web engine's own behavior, invisible to the application,
  which is why there was nothing to save.

  | Gesture | Does |
  |---|---|
  | `Ctrl++` | Zoom in |
  | `Ctrl+-` | Zoom out |
  | `Ctrl+0`, `Ctrl+=` | Actual size |
  | `Ctrl`+wheel | Zoom in and out |
  | `Ctrl`+middle-click | Actual size |

  All three actions appear in the View menu and are rebindable through
  `[keys]` as `zoom_in`, `zoom_out` and `zoom_reset`. The factor is clamped
  to 0.5 - 3.0.
- `[general] message_zoom` sets the starting zoom for a profile that has
  never zoomed. Once you zoom, the state file remembers that instead.
- `[general] startup_query` names the saved query to open at startup, and
  defaults to `Unread`.

### Changed

- **Startup no longer opens `savedQueries().first()`.** `[queries]` is read
  through `childKeys()`, which sorts alphabetically, so the query that opened
  was whichever name sorted first rather than one you chose. It is now
  selected by name. If your `[queries]` has no entry named `Unread`, set
  `[general] startup_query` to the one you want, or you will keep getting the
  alphabetically first one. Saved-query button order is unchanged.

### Fixed

- **`[general]` keys were never read.** They were looked up as
  `general/<key>`, which matches nothing: QSettings' INI backend treats a
  section literally named `[general]` as its own fallback section and strips
  the prefix. `notmuch_config` had therefore been silently ignored since it
  was introduced. If you set it and wondered why nothing changed, it works
  now. The file format is unchanged.

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
