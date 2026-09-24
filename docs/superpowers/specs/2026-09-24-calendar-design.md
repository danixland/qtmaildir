# A calendar window: view, add, edit and delete events

Date: 2026-09-24
Status: approved, not yet implemented
Backlog: item 206 (blocks 207; answers 205's write-during-sync question)

## Problem

qtmaildir has no calendar. The user's words (2026-09-18): "I'd prefer to be
able to work on my calendar from inside qtmaildir. I'm talking view, add,
remove, edit events, accept/refuse invitations, send invites to events, etc."
Invitations are item 207 and out of scope here; this spec is everything else.

khal is another consumer of the same calendar, exactly as neomutt is another
consumer of the Maildir. qtmaildir must not depend on khal, its config or its
presence: this feature replaces what khal was used for.

## Settled before this brainstorm

Not to be reopened: the calendar is its **own top-level window**, like
`ComposeWindow`, and iCalendar work uses **libical**, never a hand parse.

## Verified context

Measured on 2026-09-24 against the live data, read-only.

- **The data is a vdir.** 312 `.ics` files, one `VEVENT` each, in three
  collections under `~/.local/share/calendars/`, the local half of a
  vdirsyncer `calendars` pair against a CalDAV server built on Open-Xchange.
  vdirsyncer runs from cron every 30 minutes with `conflict_resolution = "b
  wins"`: the server wins a conflict and a local edit loses silently.
- **Names and colours are on the server and in the vdir.** The pair had no
  `metadata` key, so no collection carried a `displayname` or `color` file.
  With `metadata = ["color", "displayname"]` added and `vdirsyncer metasync`
  run, the server supplied all three display names. It supplied no colour;
  a hand-written local `color` file was PUSHED by metasync and a fresh pull
  from the server returned it, so the server stores colours it is given. Both
  are now in place on this machine. The config change is the user's, outside
  this repo.
- **Scheduling is mail-only in practice.** 305 events are a one-time OX
  migration import, the other 7 were written by clients (DAVx5, Thunderbird,
  khal), and none carries `SCHEDULE-AGENT`/`SCHEDULE-STATUS`. Nothing suggests
  the server writes events on its own; an organiser's update arrives as mail
  and changes nothing until a client applies it (207).
- **308 of 312 carry `ATTENDEE`.** Almost everything is a meeting.
- **Time forms are mixed**, and every one must work: UTC, `TZID=Europe/Rome`
  and `Europe/Berlin`, `VALUE=DATE` (59 all-day), and possibly a few floating
  times. **120 files name a `TZID` without embedding the `VTIMEZONE`** the RFC
  requires.
- **Recurrence is simple in the events** (37 files carry `RRULE`, many of them
  only inside `VTIMEZONE` DST rules): yearly for birthdays, one monthly
  `BYMONTHDAY` with `COUNT`, and `WKST` present on several. No file carries a
  `RECURRENCE-ID` override today, but other clients may write one at any time.
- **95 events carry a `VALARM`.**
- **libical 3.0.20** ships `libical.pc`, so it is `pkg_check_modules`, unlike
  notmuch. Its vCard support is 4.0 and irrelevant here.
- **Performance needs no worker thread.** A standalone C benchmark parsing all
  312 files with libical and expanding every `RRULE` over three months takes
  **about 5 ms**, with 0 unparsable files.

## Decisions

Taken with the user on 2026-09-24.

1. **One spec, one branch**, view and edit together.
2. **Views: Month (default) and Agenda.** Week is backlogged; every view reads
   the same list of expanded occurrences, so Week later is a new painter and
   not a rework.
3. **Writes are atomic, stale-checked, synced soon after and verified**
   (§ Writing).
4. **Events organised by someone else are read-only.** Replying to them is
   207. Events with no `ORGANIZER`, or organised by one of the user's own
   account addresses, are editable. Delete follows the same rule.
5. **Recurring edits offer "This occurrence" and "All".** "This and following"
   is backlogged.
6. **Editing a meeting the user organised is allowed**, bumps `SEQUENCE`, and
   says the attendees have not been notified. The attendee list is read-only
   until 207.
7. **Alarms are preserved and shown, never fired, never edited.**
8. **Layout:** a toolbar, the view, and a right-hand side pane for the
   selected event.
9. **Collections are single-select** with "All calendars", like the account
   combo, and the combo carries each collection's colour as a legend.
10. **Names and colours come from the vdir**, not from qtmaildir's config and
    not from khal's.
11. **Editing happens in the side pane**, which becomes the form.
12. **Mid-edit, navigation stays free and selection is locked**; closing with
    unsaved changes asks Save / Discard / Cancel.
13. **New and double-click on an empty day** open the form for that day in the
    default calendar.
14. **Create, edit and delete are undoable**, with no confirmation on delete.
15. **Architecture A:** everything on the UI thread, pure units, libical
    confined to one file.

## Architecture

```
CalendarWindow (QMainWindow, single instance, opened from MainWindow)
 ├ toolbar: collection combo · ‹ Today › · month combo · year spin · Month|Agenda · + New
 ├ QStackedWidget: MonthView | AgendaView   ← read QList<Occurrence> only
 └ EventPane: read-only details ⇄ edit form

CalendarStore   namespace; with icalraii.h, the only code including libical
CalendarWriter  namespace; atomic write, stale check, post-sync comparison
CalendarSync    QObject owning the QProcess for calendar_sync_command
MonthLayout     pure geometry for the month grid, no painter
RepeatRule      struct + RRULE <-> control conversion
```

**The libical rule is notmuch's rule.** C handles are owned by RAII aliases in
`src/icalraii.h`, following `nmraii.h`, and no libical pointer leaves
`calendarstore.cpp`. Everything the rest of the code sees is a plain value
struct in `src/caltypes.h`.

### Value structs

- `CalCollection { QString dir; QString displayName; QColor color; bool readOnly; }`.
  `displayName` from the vdir's `displayname` file, falling back to the
  directory name; `color` from its `color` file (`#RRGGBB`), falling back to a
  colour hashed from the directory name, so a collection is never invisible.
  `readOnly` when the directory is not writable.
- `CalEvent { QString filePath; QString uid; QString collectionDir; QString
  summary, location, description; QDateTime start, end; bool allDay;
  RepeatRule repeat; bool hasAlarm; CalPerson organizer; QList<CalPerson>
  attendees; int sequence; QByteArray rawText; QList<CalOverride> overrides;
  QList<QDateTime> exdates; }`. `rawText` is the file exactly as read: the
  stale check compares against it and undo writes it back.
- `CalPerson { QString name; QString address; QString partstat; }`.
- `Occurrence { int eventIndex; QDateTime start, end; bool isOverride; }`.
  This is the whole of what a view sees.

### `CalendarStore`

- `load(dir) -> LoadResult { collections, events, unparsable, unknownZones }`.
  A file that does not parse is skipped and counted; a `TZID` that cannot be
  resolved is treated as local time and counted. The window reports both
  counts in its status bar. Bad data never takes the window down.
- `occurrences(events, from, to) -> QList<Occurrence>`, sorted by start.
- `applyEdit(rawText, EventEdit, Scope, occurrenceStart) -> QByteArray`.
  Edits the PARSED file, never rebuilds it: every property the form does not
  own (`VALARM`, `ATTENDEE`, `X-*`, `WKST`, a custom `RRULE`, unknown
  components) is written back unchanged. This is the discipline `TagRule::unknown`
  applies to the rules file, for the same reason.
- `newEvent(EventEdit, zone) -> QByteArray`: a fresh `UID` (`QUuid`),
  `DTSTAMP`, `SEQUENCE:0`, `PRODID:-//qtmaildir//EN`, and the zone's
  `VTIMEZONE` embedded from libical's built-in database.
- `isEditable(event, collection, ownAddresses) -> bool`: the collection is not
  read-only AND (`ORGANIZER` is absent OR its address, `mailto:` stripped and
  compared case-insensitively, is one of the configured account addresses).

## Time and recurrence

- **Resolving a `TZID`**, in order: the file's embedded `VTIMEZONE`; then the
  name as an IANA zone through `QTimeZone`. Anything else, such as a
  Windows-style name, is local time and counted.
- **UTC** converts directly. **Floating** is local wall-clock time.
- **Expansion runs in the event's own zone, and converts to local after.** A
  weekly 10:00 `Europe/Rome` event stays 10:00 across a DST change; expanding
  in UTC would move it by an hour for half the year.
- **All-day `DTEND;VALUE=DATE` is exclusive.** A one-day event on the 24th
  ends on the 25th.
- **Recurrence** uses `icalrecur_iterator` started at the visible window, so an
  infinite series yields only what is on screen. `EXDATE`s are removed. A
  `RECURRENCE-ID` override replaces its original slot and is placed by its OWN
  `DTSTART`, which may be inside or outside the window independently of the
  slot it replaces.
- **New events** are written in the system zone with the `VTIMEZONE` embedded.
  An edited event keeps the zone it had.

### The repeat control

`RepeatRule` round-trips this set and nothing more:

| Mode | Control | `RRULE` |
|---|---|---|
| None | | no `RRULE` |
| Daily | every N days | `FREQ=DAILY;INTERVAL=N` |
| Weekly | every N weeks on weekday checkboxes | `FREQ=WEEKLY;INTERVAL=N;BYDAY=MO,WE,FR` |
| Monthly | every N months on day D | `FREQ=MONTHLY;INTERVAL=N;BYMONTHDAY=D` |
| Monthly | every N months on the first / second / third / fourth / last weekday | `FREQ=MONTHLY;INTERVAL=N;BYDAY=-1FR` |
| Yearly | every N years on the start date | `FREQ=YEARLY;INTERVAL=N` |
| Yearly | every N years on the Nth weekday of a month | `FREQ=YEARLY;INTERVAL=N;BYMONTH=9;BYDAY=-1FR` |
| Ends | never / on a date / after N times | none / `UNTIL` / `COUNT` |

`UNTIL` follows `DTSTART`'s type: a `DATE` for all-day events, UTC when
`DTSTART` carries a `TZID` (RFC 5545). `WKST` is the one part outside the
table that is carried through untouched on a rewrite, since several existing
rules carry it; any OTHER part makes the rule custom. A rule using anything
outside the table (`BYSETPOS`, `BYWEEKNO`,
`BYHOUR`, several months, `FREQ=HOURLY` and finer) is **"Custom rule, kept as
is"**, shown in words where possible, and an edit never rewrites its `RRULE`
line. Changing the start date re-proposes the mode's default (day 24, or "the
fourth Thursday"), as Thunderbird does.

## Writing

**Save** runs the form through `applyEdit`, then
`CalendarWriter::write(path, expected = rawText, newText)`:

1. **Stale check.** Re-read the file; if it is not `expected`, refuse. The
   event is reloaded, the form stays open holding the user's values, and the
   status bar says the event changed on disk and needs checking before saving
   again. Nothing is overwritten blind.
2. **Atomic write.** Write `.<name>.tmp` in the same directory and `rename()`
   it into place. vdirsyncer lists only `*.ics`, so it never sees the
   temporary.
3. Every edit sets `DTSTAMP` and `LAST-MODIFIED` and increments `SEQUENCE`.

A **new** event is `<uuid>.ics` in `default_calendar` or the collection chosen
in the form. **Delete** runs the same stale check and then unlinks.

### Recurring scopes

- **All:** edit the master `VEVENT`.
- **This occurrence:** add, or replace, an override `VEVENT` carrying
  `RECURRENCE-ID`: a copy of the master without `RRULE`/`EXDATE`, with the
  edit applied. Same file.
- **Delete this occurrence:** add an `EXDATE` to the master and remove any
  override for that slot.

The scope is asked when Edit or Delete is pressed on a recurring event, never
after the edit.

### Undo

The calendar window owns a `QUndoStack`, alive as long as the window. Each
command holds `{path, textBefore | none, textAfter | none}`; undo writes
`textBefore` back (or deletes the file when there was none) through the same
writer, **expecting `textAfter` on disk**. If a sync has changed the file
since, undo refuses and says so. Undoing a delete restores the file with its
original `UID`, which the next sync pushes back. This is item 176's rule: an
undo reverses what the write changed and nothing else.

### Sync

- `calendar_sync_command` defaults to
  `flock -w 60 /tmp/vdirsyncer.lock vdirsyncer sync calendars`. `CalendarSync`
  runs it `calendar_sync_delay_ms` (default 2000) after the last write, so a
  burst of saves costs one run. An empty command disables syncing.
- The window's status bar reports "Syncing calendars...", then the outcome; a
  non-zero exit shows the command's output, as `MailSync` does.
- **Post-sync comparison.** After a run, every file written since the previous
  run is re-read and compared on MEANING: `SUMMARY`, start and end, the repeat
  rule, and whether the file still exists. Bytes are not compared, because
  the server may normalise the text on the round trip. A difference reads "the
  server kept a different version of *Title*".
- **The cron line is the user's to change**: prefixing it with the same
  `flock -w 60 /tmp/vdirsyncer.lock` makes the two runs exclude each other.
  Appending `&& vdirsyncer metasync` there keeps names and colours current;
  `sync` does not run metasync.

### Reloading

A `QFileSystemWatcher` on each collection directory triggers a reload debounced
by 500 ms. The selection survives a reload by `UID` plus occurrence start. An
event changing under an open form is caught by the stale check at Save.

## The window

**Opening.** A `calendar` action in `MainWindow`: View menu and a toolbar
button, icon `x-office-calendar`, no default shortcut. That is AGENTS.md's
five places. A single instance: a second trigger raises it. Geometry, the
Month/Agenda choice and the selected collection persist in `uistate.conf`.

**Toolbar**, left to right: the collection combo (single-select, "All
calendars" first, each entry with its colour swatch), `‹`, **Today**, `›`, a
month combo, a year spin box, a stretch, the Month | Agenda toggle, **+ New**.

**Month view.** `MonthLayout` computes every rect, as `CardLayout` does for a
card, and `MonthView` only paints it:

- a 6×7 grid starting on the locale's first weekday; days outside the month
  dimmed; today's number on a pill;
- all-day events as a bar filled with the collection colour; timed events as
  a colour dot and `10:00 Title`; a multi-day event gets a chip on every day
  it covers;
- a day with more chips than fit ends in "+N more", which switches to Agenda
  at that day;
- clicking a chip selects it; double-clicking an empty day opens New for that
  day.

**Agenda view.** A list grouped by day over the same range the month combo and
year spin select, scrolled to the selected day or today.

**Side pane, read-only state.** Title; the time, with the repeat rule in words
("Monthly, on the last Friday"); the collection swatch and name; location;
notes; the bell when an alarm exists; the organiser; attendees with their
reply. Notices for the two read-only reasons ("Organised by *X*: read-only",
"Read-only calendar"). Edit and Delete are ABSENT, not disabled, when not
allowed.

**Side pane, editing state.** Title, calendar, all-day, starts, ends, repeat,
location, notes; the reminder and attendees shown read-only; "Attendees have
not been notified" when the event has attendees. Save and Cancel. While the
form is open the views still navigate, but selecting another event is refused
with a status-bar hint. Leaving the window with unsaved changes asks Save /
Discard / Cancel on EVERY route out: the close button, `close()` and a menu
action. AGENTS.md's `QDialog` trap is the reason each route is asserted.

**Untrusted text.** `SUMMARY`, `DESCRIPTION`, `LOCATION` and attendee names
come from strangers once invitations arrive. Every one is shown in a label set
to `Qt::PlainText`, `MessageDetailsDialog`'s rule. `URL` and `ATTACH` are not
rendered.

**Menus and keys**, the composer's pattern (item 161): a menu bar with File,
Edit and View holding every action; `Ctrl+N` New, `Ctrl+E` Edit, `Delete`,
`Ctrl+Z` / `Ctrl+Shift+Z`, `PgUp` / `PgDn` for the month, `Home` for today,
`Esc` to cancel an edit. Window shortcuts, not `KeyMap` entries.

**i18n.** Every string in `tr()`; the Italian `.ts` refreshed, `lrelease`
reporting 0 unfinished.

## Config

All in `[general]`, read without the `general/` prefix per AGENTS.md:

| Key | Default | Meaning |
|---|---|---|
| `calendars_dir` | `~/.local/share/calendars/` | The vdir root, tilde-expanded like `contacts_dir`. Empty turns the feature off: no action, no error. |
| `default_calendar` | first collection by name | Directory name of the collection new events go to. |
| `calendar_sync_command` | `flock -w 60 /tmp/vdirsyncer.lock vdirsyncer sync calendars` | Run after writes. Empty disables. |
| `calendar_sync_delay_ms` | `2000` | Debounce before the sync runs. |

Names and colours are not configured: they come from the vdir.

## Testing

Only what has a right answer. Fixtures are written by hand with example.org
addresses, never copied from the live vdir.

- **`test_calendarstore`.** Parsing: UTC, `TZID` with and without an embedded
  `VTIMEZONE`, floating, `DATE`; an unknown `TZID` counted; a broken file
  skipped and counted. Expansion: a weekly 10:00 `Europe/Rome` event across
  2026-10-25 stays at 10:00 local; an all-day event's end is exclusive;
  `EXDATE`; an override moving into and out of the window; an infinite series
  yields only the window; `COUNT` and `UNTIL`; monthly `BYDAY=-1FR`; yearly
  `BYMONTH`+`BYDAY`. `RepeatRule` round-trips every row of the table, detects
  a custom rule, keeps `WKST`. `applyEdit` keeps `VALARM`, `ATTENDEE` and
  `X-*` byte for byte, increments `SEQUENCE`, creates an override, adds an
  `EXDATE`. `newEvent` embeds its `VTIMEZONE`. `isEditable` for an organiser
  who is the user, someone else, nobody, and for a read-only collection.
- **`test_calendarwriter`**, in a temporary directory: the atomic write; the
  stale refusal; delete; undo refusing a changed file; the post-sync
  comparison passing normalised text and flagging a moved start.
- **`test_monthlayout`.** The 6×7 geometry, the first weekday, "+N more".
- **`test_calendarwindow`**, minimal: the edit lock refuses a selection change;
  every route out with unsaved changes asks.
- **A read-only run against the live vdir** before hand-off: load all 312,
  report unparsable and unresolved-zone counts. No test ever writes to the
  live directory.

Everything visual is the user's hand test.

## Out of scope, and where it goes

- **Invitations** (accept, decline, send, apply an organiser's update): 207.
- **Week view**: new backlog item. The occurrence list is the seam.
- **"This and following"** for recurring edits: new backlog item. It splits a
  series into two files, divides `EXDATE`s and overrides between them, shifts
  moved `RECURRENCE-ID`s when the time changes, and converts `COUNT`.
- **Alarm notifications**: declined by the user; not planned.
- **`VTODO`**: none exist in the data.
- **Editing the attendee list**: 207, since adding someone is inviting them.

## Delivery

Branch `calendar`, merged to master after the user's hand test. CMake gains
`pkg_check_modules(LIBICAL REQUIRED IMPORTED_TARGET libical)`. AGENTS.md gains
the calendar's traps: zone-local expansion, the exclusive all-day end, `TZID`
without `VTIMEZONE`, and the libical RAII rule. The backlog gains the two new
items, 206's row points here, and 205's row records that its write-during-sync
question is answered by § Writing.
