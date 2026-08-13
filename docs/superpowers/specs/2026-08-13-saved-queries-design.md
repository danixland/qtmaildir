# Saved queries in a file of their own: design

Backlog item 23, "No way to save a search query from the UI", and item 81, "No
way to turn a saved query into a tagging rule", which depends on it.

**Status:** design approved 2026-08-13, not implemented.

## The problem this solves

A saved query can only be created by hand-editing `qtmaildir.conf`. There is no
way to keep a query you have just written and are looking at the results of,
which is exactly the moment you know it is worth keeping.

Underneath that sits a second problem the user named separately: every saved
query becomes a button in the query row (`src/mainwindow.cpp:557`), so the row
grows without bound and cannot distinguish "Inbox", which belongs on screen
permanently, from "all messages from one correspondent", which belongs in a
menu.

## Why the storage moves, and what that decides

The obvious implementation writes back into `[queries]`. It was rejected, and
the reason shapes the rest of this document.

**`[queries]` cannot express order.** It is read through
`QSettings::childKeys()`, which returns keys alphabetically and never in file
order. `src/config.cpp:401` already carries a comment saying a hand-rolled
parser would be needed to change that, and the README documents the alphabetical
button order as though it were a feature. A row of pinned buttons that cannot be
arranged is not worth building.

**QSettings would also destroy the file it wrote to.** It preserves neither
comments nor key order, so the first save from the dialog reformats a
hand-edited config wholesale. Item 1 established that machine-written state stays
out of `qtmaildir.conf` for precisely this reason.

So saved queries move to **`~/.config/qtmaildir/queries.json`**, and gain three
things the INI could not hold: an order, a `pinned` flag, and a per-query account
scope.

**In `~/.config/`, not `~/.local/state/`.** A named, scoped query is user intent,
hand-editable, and belongs in a config backup; a state directory is commonly
excluded from one. `uistate.conf` keeps window blobs and column widths, which is
a different kind of thing.

### The rules.json analogy, and where it stops

The user proposed this by analogy with `~/.config/mailrules/rules.json`. The
analogy is right about the shape and wrong about the machinery, and the
difference is worth stating because copying too much of it would be a real cost.

`rules.json` carries unknown-field preservation and a `kFormatVersion` handshake
because it has **two independent implementations** that agree by test rather than
by shared code: `src/tagrules.cpp` here and `mailrules.py` in `../mailctl`. All
of that exists to stop two programs destroying each other's writes.

`queries.json` has exactly one reader. What carries across:

- a versioned document, so a future breaking change has a way to announce itself
- unknown fields preserved through a load/save round trip, so a field added by a
  later build is not stripped by an older one

What does not carry across: any notion of a second implementation, a shared
contract, or a two-repo change. This is a single-repo change and must stay one.

## The format

```json
{
  "version": 1,
  "queries": [
    { "name": "Inbox",  "query": "tag:inbox",   "pinned": true },
    { "name": "Unread", "query": "tag:unread",  "pinned": true },
    { "name": "Work invoices", "query": "from:billing", "account": "work" }
  ]
}
```

`queries` is an **array**, and its order is the display order. That is the whole
point of the change; nothing may sort it on load.

| field | required | meaning |
|---|---|---|
| `name` | yes | Display name, and what `startup_query` matches against |
| `query` | yes | notmuch query, wire format, never translated |
| `pinned` | no, default `false` | Renders as a button; otherwise it lives in the menu |
| `account` | no | An account **key**, the INI group suffix (`work` from `[account.work]`), not a path or a display name |

**`account` stores the key, not the maildir path.** `Account::scopedQuery()`
(`src/config.cpp:42`) composes `path:"<maildir>/**"` from the account, so storing
the path would duplicate config that already exists in one place and go stale
when the user edits it. A key naming an account that no longer exists resolves to
no scope, with a config problem reported the way an unmatched `startup_query`
already is.

**Scope composes, it does not replace.** A query with an `account` runs as
`Account::scopedQuery(query)`, which yields `path:"<maildir>/**" and (<query>)`.
The parenthesisation is load-bearing for the same reason it is in the rules hook:
`path:... and a or b` binds as `(path:... and a) or b`, so an unparenthesised
disjunction escapes its scope and matches every account. `scopedQuery` already
does this correctly; the requirement is to use it rather than concatenating.

## Migration

On a run where `queries.json` does not exist and `[queries]` does: read the INI
section, write the JSON, and **leave `[queries]` in place, untouched**. From then
on the JSON is the only source and `[queries]` is ignored entirely.

**Do not strip the section.** Removing it means rewriting the hand-edited file
with QSettings, which drops comments and key order across the whole file, not
just the part being removed. That is the loss this design exists to avoid.
Leaving it costs a few stale lines the user can delete by hand whenever they
like, and keeps a downgrade to an older build working.

Migrated entries get `pinned: true`, since they are buttons today and the
migration must not silently empty the query row. Order is alphabetical on
migration because that is genuinely all the INI knows; the user reorders once, by
hand or in the dialog, and it sticks from then on.

**Rejected: reading both forever.** Two sources of truth for one thing, with no
answer for a name colliding across them and no obvious place for the dialog to
write.

## Consequences for `startup_query`

`[general] startup_query` names a saved query and resolves case-insensitively,
falling back to the first entry (`Config::startupSavedQuery()`,
`src/config.cpp:422`). It keeps working unchanged, but **its fallback quietly
changes meaning**: "the first entry" stops being "alphabetically first" and
becomes "first in the user's own order".

That is an improvement, and it is still a user-visible behaviour change for a
config whose `startup_query` matches nothing. It belongs in the changelog, and
it makes this a minor version bump rather than a patch.

## The UI

### A Save query button beside the search bar

Opens a dialog on the current query bar contents, holding:

- **name**, required, and unique. A name that already exists offers to overwrite
  rather than silently creating a duplicate the menu would render twice.
- **query**, editable, prefilled from the query bar.
- **account scope**, a dropdown of configured accounts plus "All accounts".
- **pinned**, a checkbox: button or menu.

### The button row moves to a row of its own

Today the query row holds the account box, the sort dropdown, the query bar and
every saved-query button (`src/mainwindow.cpp:554-587`). Pinned queries move to
their own row beneath it, and the unpinned ones go into a menu.

**There is no `SavedQueryBar` class**, whatever older revisions of the backlog
said. These buttons are built inline in `MainWindow` and this is a change to that
layout code.

Sent stays where it is. It is not a saved query, it is built from
`allSentQuery()` (`src/mainwindow.cpp:579`), and folding it into `queries.json`
would mean generating a per-account path query into stored config, which is the
duplication the `account`-key decision just rejected.

## Editing and deleting are NOT here, and that is a defect

This document specifies creating a saved query and says nothing about changing
or removing one. That gap shipped: the first hand test produced "how do I unpin
a query?", and the honest answer was a text editor. Recorded as **item 82**,
sized S, and it should land before this work is called finished.

Anything built there must merge the stored entry's `unknown` fields the way
`saveCurrentQuery()` does, and must match a rename on the name the dialog was
opened with rather than the one it returns.

## What is deliberately not here

**Item 81, saving a query as a tagging rule.** A saved query is a view and costs
nothing if it is wrong. A rule is applied to real mail by the `post-new` hook
every ten minutes and lives in the file shared with mailctl. Keeping it out means
item 23 ships as a single-repo change; folding it in would give a presentation
change a two-repo commitment and a live blast radius.

**Item 10's account persistence.** Per-query account scope answers item 10's
remaining complaint as a side effect, which is noted in both entries. Item 10
itself stays postponed and is not reopened here: the user asked that the rest of
it not be proposed unprompted.

## Testing

`Config` is already unit-tested, and this is mostly a format with an ordering
guarantee, so most of it is testable without a window.

- Round trip: load, save, load, and assert the document is unchanged **including
  order**, which is the property the INI could not provide.
- Unknown fields survive a round trip. Write a field no build knows, load, save,
  assert it is still there.
- Migration: an `[queries]` section and no JSON produces a JSON file with every
  entry `pinned`, and leaves the INI file **byte-identical**. Assert on the file
  bytes, not on a re-read through QSettings, which would pass against a rewrite
  that happened to preserve values while dropping comments.
- A malformed or unreadable `queries.json` reports a config problem and does not
  take the query row down with it.
- `startup_query` still resolves by name, and its fallback now returns the first
  entry in document order.
- Scope composition yields `path:"<maildir>/**" and (<query>)` with the
  parentheses present. Assert on the composed string for a query containing
  `or`, which is the case that breaks without them.

**The dialog itself needs a hand test**, per the offscreen-platform constraints
already recorded in CLAUDE.md: sizing cannot be asserted there at all, and a
dialog's Cancel path goes through `done(int)` rather than `closeEvent`, so a test
that only exercises `close()` proves less than it appears to.
