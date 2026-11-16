# Receiving invitations

Item 207, receiving half. Designed 2026-11-16. Sending invitations is item 216.

## Problem

"As it is today I don't have a way of accepting an invitation." `MimeParser`
has no branch for `text/calendar`, so an invitation reaches the pane as an
attachment chip or as nothing. The calendar (item 206) can store events but
nothing connects a message to it.

## Decisions made with the user

1. **Receiving first.** Sending invites (attendee editing, `METHOD:REQUEST`,
   applying the `REPLY`s that come back) is item 216.
2. **The target calendar is mapped per account.** An optional
   `calendar = <collection>` key in `[account.<key>]`. Unset: the first write
   asks for a collection from a list.
3. **A reply goes both ways.** Accept / Tentative / Decline send at once
   through the existing send countdown; each also has a "...with note" entry
   that opens a composer carrying the calendar part.
4. **Decline writes nothing**, and removes the event if an earlier Accept
   stored it.
5. **Organiser updates and cancellations wait for a button.** Opening the
   message changes nothing.

## Approach

A native **invitation bar** in `MessageView`, between the message bar and the
web view, in the same family as the blocked-content and stale bars. Labels
are `Qt::PlainText`, buttons are `QPushButton`s. Rejected: rendering it in
the HTML body, which needs the interceptor to admit `qtmaildir:` actions and
loosens the most security-sensitive area for a convenience; and a dialog from
the attachment chip, which hides the invitation.

## Parsing

- `MimeParser` keeps the FIRST `text/calendar` part's decoded bytes on
  `ParsedMessage` (`calendarPart`), whether it arrived inline in a
  `multipart/alternative` or as an attachment. It stays an attachment chip too,
  as today.
- `CalendarStore` gains pure functions over bytes, so the logic is tested with
  no widget, the `CardLayout` rule:
  - `parseInvitation(bytes)` returns an `Invitation` value (`caltypes.h`):
    method, uid, sequence, recurrence-id, summary, location, start, end,
    all-day, organizer, attendees. A part that does not parse, has no `METHOD`,
    or has no `VEVENT` returns an invalid one.
  - `storedText(inviteBytes, myAddress, partstat)`: the event file to write.
    `METHOD` removed (a stored object carries none), my `ATTENDEE`'s
    `PARTSTAT` set, `RSVP` cleared, everything else as the organiser sent it,
    including `TZID` parameters (the 120-files rule from 206: never convert).
  - `replyText(inviteBytes, myAddress, partstat)`: a `METHOD:REPLY` calendar
    holding the `VEVENT` with `UID`, `SEQUENCE`, `DTSTAMP`, `ORGANIZER`,
    `RECURRENCE-ID` when present, and ONLY my `ATTENDEE` line, per RFC 5546.
  - `findByUid(events, uid)`: the stored event, if any.
- **"Me"** is the attendee whose address matches the `address` of the account
  the message arrived on (case-insensitive, `mailto:` stripped). No match: the
  bar says so and offers no reply.

## Bar states

A pure function `invitationState(invitation, storedEvent, meFound)` picks one,
returning an enum; the widget only renders it.

| State | Shown | Buttons |
|---|---|---|
| New request | title, time, place, organiser, attendee count | Accept, Tentative, Decline, each with "...with note" |
| Update (stored, higher `SEQUENCE`) | the same, plus what changed: time, place | Update calendar, and the reply buttons |
| Already answered (stored, same `SEQUENCE`) | the same, plus "In your calendar: <partstat>" | the reply buttons, to change the answer |
| Cancel, stored | "Cancelled by the organiser" | Remove from calendar |
| Cancel, not stored | "Cancelled; not in your calendar" | none. Normal, not an error |
| Out of date (lower `SEQUENCE` than stored) | "Out of date: your calendar holds a newer version" | none |
| One occurrence (`RECURRENCE-ID`) | details | none: "Changes to a single occurrence aren't supported yet" |
| Not invited | details | none: "You are not on the attendee list" |
| Other method (`REPLY`, `PUBLISH`, ...) | no bar | |

No calendar configured (`calendarsDir()` empty): details only, no buttons.

## Reply and write

- `OutgoingMessage` gains `calendarPart` (bytes) and `calendarMethod`.
  `MessageBuilder` adds it as a `text/calendar; method=<METHOD>; charset=utf-8`
  part beside the text body. Subject: `Accepted: <summary>` /
  `Tentative: ...` / `Declined: ...`, translated. To: the organiser.
  From: the account the invitation arrived on.
- One click hands the built message to the existing `SendDialog` countdown.
  "...with note" opens a `ComposeWindow` with the part attached and the subject
  and recipient filled; the user's text is the body.
- **The calendar write happens only when the send SUCCEEDS.** An aborted
  countdown or a failed send leaves the calendar untouched, so the calendar
  never claims an answer the organiser never received. Accept and Tentative
  write `storedText`; Decline removes a stored event; a change of answer
  rewrites it.
- Update calendar and Remove from calendar send nothing and write at once.
- Every write goes through `CalendarWriter::replace` with the stored event's
  bytes as `expected` (a new file expects absent), and a `Stale` result is
  reported in the status bar rather than retried. A new event is written as
  `<collection>/<uid-derived name>.ics`, the name sanitised as an attachment
  filename is. A `CalendarSync` runs after.
- An open `CalendarWindow` reloads after any such write.

## Safety

- Every value in the bar comes from a stranger: `Qt::PlainText`, no `URL`,
  no `ATTACH`, no `DESCRIPTION` (it can be long and is in the body anyway).
- A button acts on the `Invitation` the bar is displaying, captured with it,
  never on a re-read of the message.
- The `UID` reaches a filename: sanitised, and the resolved path must stay
  inside the collection directory, compared as paths (the attachment rule).

## Tests

- `CalendarStore`: fixture `.ics` for a request, a cancel, an update, an older
  sequence, a recurrence-id, and an invite without me. `storedText` keeps
  `TZID`, drops `METHOD`, sets only my `PARTSTAT`; `replyText` carries only my
  attendee and the right method.
- `invitationState`: one case per row of the table.
- `MimeParser`: the part is found inline and as an attachment.
- `MessageBuilder`: the part is present with its `method` parameter.
- The write-after-send rule: an aborted send writes nothing.
- How the bar looks is the user's hand test.

## Out of scope

- Single-occurrence updates (`RECURRENCE-ID`).
- Showing or applying replies to the user's own invitations, item 216.
- Editing attendees and sending invitations, item 216.
- Alarms on accepted events: kept if the organiser sent them, never fired,
  as in 206.
