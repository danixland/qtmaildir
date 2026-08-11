# Sent mail: a per-account folder, a composed button, and recipients on the card

**Status:** specified and built 2026-08-11, hand-verified by the user. See
"Outcome" at the end for what the spec did not anticipate.
**Resolves:** backlog item 63.
**Size:** M, revised up from the backlog's S. The query half is the S that was
scoped correctly; the recipients half is its own piece of work.

## Why

The user's notes asked for a "Sent mail filter". Nothing in the codebase knows
what sent means: `Account` carries `name`, `address`, `maildir`, `drafts`,
`label`, `channel` and `color` (`src/config.cpp:254-270`) and no `sent` field,
and no query anywhere composes one.

The original entry could not be planned because it turned on a fact about the
user's mail rather than a design choice: whether "sent" is a notmuch tag their
filters apply, or a maildir path per account.

## What the database answered, 2026-08-11

Measured rather than asked in the abstract.

**No `sent` tag exists.** `notmuch search --output=tags '*'` matches nothing
case-insensitively, so the tag branch, which would have made this XS, is dead.

Every account keeps sent mail in a folder, and the folders disagree. Account
keys below are generic; the shapes and the counts are the real measurement.

| Account | Sent folder | Messages |
|---|---|---|
| `webmail-primary` | `Sent` | 21 |
| `webmail-secondary` | `Sent` | 52 |
| `provider-a` | `[Provider]/Posta inviata` | 190 |
| `provider-b` | `[Provider]/Posta inviata` | 531 |
| `provider-c` | **none; the account has only `Inbox`** | n/a |

794 combined. Three shapes across five accounts, and one account with no sent
folder at all. That is what rules out a convention such as `<maildir>/Sent` and
forces a per-account key: a convention would silently produce an empty view for
the two bracket-path accounts and a wrong one for the account that has no such
folder.

Note the bracket form is a real provider's layout, not an invention: the folder
is nested under a bracketed parent and localised, which is why both halves of
this item have to survive a path that is neither ASCII-simple nor flat.

**The query already works by hand.** The user pasted the composed `path:` OR
into the query bar and confirmed it returns their sent mail. This item is
therefore not "make it possible" but "make it a button that stays correct as
accounts change", which caps what the query half is allowed to cost.

## Decisions (user, 2026-08-11)

**A per-account `sent` key, composed at run time.** Not a shipped `[queries]`
entry. A saved query is one fixed string, so it cannot narrow to the selected
account, and it goes stale silently the moment an account is added or a
provider renames a folder. The user asked for the button to sit beside Inbox,
Unread and Important, and it does; only its query is built rather than stored.

**An account with no `sent` key is omitted silently.** No config problem. The
key is optional exactly as `drafts` is, and `provider-c` is a real account
that legitimately has no sent folder; reporting it would warn on every launch
about nothing.

**All accounts combines, one account scopes.** With no account selected the
button runs the OR of every configured sent path. With one selected,
`runCurrentQuery()`'s existing wrap (`src/mainwindow.cpp:1478-1480`) narrows it,
and the intersection is that account's sent mail.

**A Sent view shows RECIPIENTS on the card, not the sender.** Every message in
it was sent by the user, so a sender column repeats their own name down the
whole list and carries no information.

### On recipients, which contradicts item 2 and does so correctly

Item 2 refused a `To:` line on the thread header and deferred a participants
list to its own pass. **That ruling stands where it was made and does not govern
here**, because it was scoped to a different problem.

Item 2's case is a MIXED conversation: message 1 is To the user, message 2 is To
the other party, so the union of recipients is a participants list wearing a
"To:" label that misdescribes it, and the intersection is frequently empty. The
user's call was that this overcomplicates, and it does.

A Sent view is one-directional. Every message in it was sent BY the user, so
"who did I send this to" is well posed, and it is the question the view exists
to answer. The ambiguity item 2 avoided is absent here; it was not overruled.

This distinction is recorded rather than the reversal, because a future reader
finding both entries needs to know why both stand.

## Approach

Three pieces, in this order. The first two are independently shippable and give
a working Sent view on their own.

### 1. Config

An optional `sent` key on `[account.<key>]`, a folder path relative to
`maildir`, beside the existing `drafts`.

`Account::sentQuery()` returns `path:"<maildir>/<sent>/**"`, or an empty string
when the key is absent. It sits beside `scopedQuery()` (`src/config.cpp:42-45`)
and follows its shape.

### 2. The button

Built beside the saved-query buttons, running the OR of every non-empty
`sentQuery()`. Hidden entirely when no account configures one, so a user with no
`sent` key anywhere does not get a button that finds nothing.

### 3. Recipients on the card

`ThreadSummary` gains a recipients summary, filled in the worker, shown by
`CardDelegate` in the sender's place when the row belongs to a Sent view.

**The fold must be OPT-IN per query, and this is not a preference.** Measured
2026-08-11 against the real database: `notmuch_message_get_header(m, "To")` is
NOT served from the index, it reads the message file. Folding it for every
thread of a 4411-thread inbox took **38.2 seconds**, 8.7 ms per thread. The
same fold over the 601-thread Sent view took **663 ms**, 1.1 ms per thread,
which the existing 200-thread batching hides.

So the worker takes a flag on the query, set only when the query is a Sent one,
and skips the walk entirely otherwise. A version that always folds turns an
instant inbox into a 38-second one, and it would look correct in every test:
the data is right, only the cost is wrong.

## Constraints

**Do not invent a tag qtmaildir applies itself.** v1 is read-and-organize;
nothing here sends mail, so nothing here can know a message was sent except by
where it landed. Carried from the original entry and now load-bearing, since the
answer came back "path".

**`sentQuery()` composes with `scopedQuery()` and must not bypass it.** A Sent
view under one account must not show another account's sent mail. The existing
wrap already does this; the constraint is not to add a second path around it.

**The folder path is untrusted config and lands inside a notmuch query.**
`[Provider]/Posta inviata` contains `[` and `]`, which are Xapian syntax, and
the quoting is what makes the shipped query work at all: the four-way OR returns
794 only because each path is quoted. Quote every composed path and pin a
bracketed path in a test, or the two accounts that need this most break.

**notmuch has no recipients call.** `notmuch_thread_get_authors` exists
(`notmuch.h:1435`) and there is no `get_recipients` at any level: To is a
per-message header, not a thread property. The summary is folded in the worker
from `notmuch_message_get_header(msg, "To")` over the thread's messages.

**The recipient walk obeys the thread-ownership rule.** Messages reached through
the thread are owned by it and freed with it, so they are held as raw
`notmuch_message_t*` and the whole walk finishes while the `NmThread` is alive,
exactly as `walkReplies` does. An `NmMessage` wrapper here is a double-free.
See `CLAUDE.md`.

**Address parsing is required, and GMime already does it correctly.** Splitting
on commas is wrong: `"Rossi, Mario" <mario@example.org>, info@example.net` is
two addresses, not three. Verified empirically against
`internet_address_list_parse`, which returns 2 for that input, keeps the display
name whole, and reports `undisclosed-recipients:;` as a group.

**It returns NULL for an empty string**, which is a crash if unguarded and is
the first test case to write.

**Prefer the display name, fall back to the address.** A row reading
`mario@example.org` where the rest of the list reads `Mario Rossi` is the
inconsistency the card's sender column already avoids.

**Decide what several recipients render as when building, and keep it in
`CardLayout`** so it is testable without a painter. "Name, Name" elided by the
existing sender rect is the cheap answer; a `+N` suffix mirrors the tag strip's
overflow chip. Do not compute it in the delegate's `paint()`: a card layout must
be testable without a painter, per `CLAUDE.md`.

**gmime headers before any Qt header** in the same translation unit, if the
address parsing lands in a file that has both. glib declares a struct field
named `signals`, which Qt defines as a macro.

## Verification

- The composed query returns the same **794** the hand-written one does, with
  the bracketed paths included, and an account with no key contributes
  nothing.
- Selecting one account narrows Sent to that account and never leaks another's.
- A message with a comma inside a quoted display name renders **one** recipient.
- An empty or absent `To` renders blank rather than crashing.
- A bracketed path containing `[` and `]` survives composition into a query
  and returns its real count rather than zero.

The first and last are the ones that fail loudest if the quoting is wrong, and
they are the reason to write them before the UI work rather than after.

## Outcome (done 2026-08-11)

Built in the three pieces above and hand-verified by the user. Four things the
spec did not anticipate, each found by using it rather than by reading code.

**A flat list, which the spec never mentioned.** The user's first report was
that the Sent view showed the replies they had RECEIVED. That is correct
behaviour, since the query matches messages and the list groups them into
threads, but it is not what a Sent view is for: their stated mental model is
that sent mail "lives on its own". `ThreadListModel::setFlatMode()` makes
`hasChildren()` and `ReplyCountRole` answer differently and nothing else
changes. It is one flag on the existing model rather than a second model or a
filtered query, which was the user's condition for building it at all.

**Flat mode cannot leak, and that is structural rather than careful.**
`runQuery()` sets the mode on EVERY run, so any query that is not the Sent
button restores the tree on its way through. The window test mutates this
directly: making the flag one-way leaves it passing every model test and
flattens the whole application from the first Sent click.

**The pane needed its own fix, and the flat list is why.** With the list flat
and correct, selecting a Sent row still opened the whole conversation. The
single-message path needs `ThreadNode::first`, which is only filled when a
thread is EXPANDED, so in a flat list it is always empty and every selection
falls through to `loadThread`. That now takes `matchedOnly`, dropping the
messages that did not match rather than rendering them as stubs. The per-message
`matched` flag it needs was already computed.

**The recipient fold is cheaper than the spec's measurement.** 251 ms for the
601-thread Sent view against the 663 ms measured while specifying, because the
worker stops at the first usable `To` per thread rather than reading every
message. The inbox, with the flag off, is unchanged at 148 ms for 4411 threads.
The opt-in is mutation-tested: always folding fails with "the To header was read
for a query that never asked for it".

**One mutation survived, and the comment was corrected rather than the code.**
Removing the `haveMatchSet` guard beside `matchedOnly` changes nothing, because
`ref.matched` is already true for every message when no query was given. The
guard is redundant today and kept as a stated invariant at the point that
depends on it; the test that appeared to cover it now says plainly that it does
not.

**Known limit, accepted.** A flat row is still one row per THREAD, not per sent
message: 795 sent messages live in 601 threads here, so a thread written to
twice appears once, dated by its newest match. The model is thread-keyed
throughout, so per-message rows would be a different piece of work.
