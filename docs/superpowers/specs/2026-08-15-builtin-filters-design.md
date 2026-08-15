# Built-in filters, separate from saved queries

**Resolves backlog items 93 and 90.** Item 90 (a saved-query button clears the
account selection) is not fixed in place: the button that misbehaves stops being
a saved query at all.

Status: **specified, not built.** Written 2026-08-15 from the user's own framing,
recorded verbatim below because it is the whole design.

## The user's model

> The queries that show as buttons shouldn't be in the same league as the ones I
> write and store in the "more queries" menu. If I see "Unread" as a button, I
> read it as "filter all my mails and show me only what is not yet read", but
> being a button, in my head it should cooperate with other UI elements. So if a
> dropdown offers to select an account, that same button should work with that
> selection transparently, not fight it.

Two kinds of thing, and today they share one mechanism:

- **A filter** narrows whatever the user is already looking at. It composes with
  the account dropdown. It never decides scope on its own.
- **A saved query** is a destination. It is self-contained, says exactly what it
  shows, and is entitled to set the account, because the user saved it that way.

## Why this is not a small fix to `runSavedQuery()`

`MainWindow::runSavedQuery()` (`src/mainwindow.cpp:1907`) resets the account box
whenever the entry names no account, which is what item 90 observes. The obvious
fix, inheriting the selection for unscoped entries, would make the buttons
compose. It would also make every unscoped entry in the MENU compose, and those
are exactly the self-contained destinations that should keep resetting.

The distinction has to be a property of the entry. It cannot be `pinned`, which
decides button-versus-menu today: making `pinned` change semantics would mean
unpinning a query silently changes what it does.

## How the buttons became saved queries

Worth recording, because the drift is the defect. **Nothing ships as a default.**
`Config::startupSavedQuery()` falls back to `m_savedQueries.first()` and a fresh
install has an empty query row. Every button the user has is something they wrote
into `[queries]` in the INI and that the queries.json migration
(`src/config.cpp:455`) pinned:

```cpp
// Pinned, because these are buttons today. A migration that left
// them unpinned would empty the query row on the first launch
// after an upgrade, which reads as data loss.
query.pinned = true;
```

Correct for that migration, and it is how "the buttons" came to be "whatever the
user pinned" rather than a designed set. The user's words: "that's the direction
I wanted from the start, we drifted to what is today".

## The design

**Four built-in filters ship: Unread, Inbox, Flagged, Sent.** They are generated
entries, named in `kQueryGenerators` (`src/config.cpp:60`), which is already a
closed set validated on load. Adding a fifth later is one entry in that list.

**All four compose with the account dropdown.** "All accounts" gives the union
across every account; one account selected gives that account only. Saved queries
are unchanged: they still set the account from what they stored.

`generated` and its validation already exist for Sent, including the property
this design depends on:

> Storing the GENERATOR rather than its output keeps both: the query stays live,
> and the entry is an ordinary row the user owns.

### A generator must answer per account, not only for all

This is the one non-obvious piece, and getting it wrong produces a query that
works by accident.

`Config::allSentQuery()` joins every account's `sentQuery()` with ` or `, giving
`path:"a/Sent/**" or path:"b/Sent/**"`. Scoping that with the selected account's
`Account::scopedQuery()` yields:

```
path:"a/**" and (path:"a/Sent/**" or path:"b/Sent/**")
```

That returns the right rows, because `path:` is hierarchical and the `b` half
cannot match inside `a`. It is still wrong to build: it double-scopes, and it
relies on a property of the path syntax rather than on saying what is meant. The
correct query is `path:"a/Sent/**"`, which `Account::sentQuery()` already
returns.

So a generator needs a per-account entry point beside the all-accounts one.
`Account::sentQuery()` is that entry point for Sent and already exists;
`allSentQuery()` is its join. The other three are `tag:` queries with no path of
their own, so `Account::scopedQuery()` is exactly right for them and no second
entry point is needed.

Two shapes, one rule: **ask the generator for this filter in this account's
scope, or across all accounts.** Do not compose by wrapping an all-accounts
query.

### Sent stays flat, the other three do not

`SavedQuery::flat` lists messages rather than threads, and Sent sets it. This is
correct and must not be unified away: a thread would fold the user's sent message
back into the conversation it belongs to, which is item 63's whole finding.

The four buttons therefore behave identically in SCOPE and not in view mode.
Flagged by the user as a thing to know before it surprises anyone: "same
behavior for all four" was said about the account, and holds there.

## Migration: unpin, never delete

The user's existing pinned queries keep their names, and some will collide with a
built-in filter's. **Unpin them once the new buttons are confirmed working**, at
the user's explicit instruction:

> when we'll get to testing the new buttons, simply unpin my queries, so they
> fold into the menu and the new buttons will have their real estate

They fold into the menu, keep working, and are recoverable by pinning them again.
Nothing the user wrote is deleted. Do not "clean up" a shadowed duplicate.

## Changing the account does NOT run a query

**Decided by the user, 2026-08-15:**

> changing the account should not run the query, hitting the button after
> changing the account is what queries

The dropdown selects scope and nothing else. The button is the verb. Selecting an
account and then clicking Unread is the gesture; selecting an account on its own
changes what the next click will mean and leaves the list alone.

This is also what the code does today: `m_accountBox` has **no signal connected
to it**, verified 2026-08-15. So nothing has to be built for this, and the thing
to be careful of is not building it by reflex. Do not connect
`currentIndexChanged` to a re-run while making the buttons compose.

Two reasons this is the right call beyond the user having made it. A re-run on
every dropdown change moves the list under a user who is reading, which is the
class of complaint item 89 is about. And the account combo is a plain
`QComboBox`, so a keyboard user arrowing through it would fire a query per
account passed on the way to the one they wanted.

## Ordering, and why it is temporary

The four filters and the user's remaining pinned queries share one row, so
something has to order them. **Filters first, in the fixed order Unread, Inbox,
Flagged, Sent; the user's pinned queries after them, keeping their own order.**

Do not build anything configurable for this. The arrangement is transitional:
item 94 removes `pinned` entirely once the user has confirmed the four buttons
cover what they use, after which the row is filters only and there is nothing
left to order. A settings surface for a mixed row would be built and deleted
inside two items.

`pinned` itself is untouched by this item and stays exactly as it is. The user's
own queries are unpinned as a migration step, which is a change to their data,
not to the mechanism.

## Constraints

- **One rule across all three surfaces.** Buttons, the "more queries" menu and
  the dropdown all reach `runSavedQuery()`. Three behaviours would be worse than
  either one.
- **A saved query that names an account keeps overriding the box.** Not in
  question; no test should lose it.
- `Account::scopedQuery()` is the only place a scope is applied. Do not add a
  second one to make a caller behave.
- **An unknown generator is reported and KEPT**, never dropped
  (`src/config.cpp:555`). A filter added in a later build must survive a save
  from an older one.
- No `kQueriesFormatVersion` bump. Adding generators to a closed set is not a
  breaking change, and queries.json has one implementation, so this is not a
  two-repo change the way rules.json would be.

## Verification

- Each filter, per account and across all accounts, asserted on the GENERATED
  QUERY STRING rather than on a row count. A count test passes against a
  double-scoped query, which is the failure this design exists to avoid.
- Sent for one account must be `path:"a/Sent/**"`, with no enclosing
  `path:"a/**" and (...)`. This is the assertion that catches composing by
  wrapping.
- A saved query with a stored account still sets the box.
- A saved query with no stored account still CLEARS the box, which is the
  behaviour item 90 leaves alone.
- Sent stays flat and the other three do not.
- An account whose config sets no `sent` folder must not produce a Sent filter
  that matches everything. `folderQuery()` returns empty for an unset folder and
  an empty query means "match everything" to notmuch, so this is a real trap and
  needs its own assertion.
