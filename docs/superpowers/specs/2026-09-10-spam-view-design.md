# Mark spam moves mail, and there is a Spam view

**Resolves items 187, 190 and 195.** Design settled 2026-09-10 with the user,
on top of the shape item 187 recorded on 2026-08-29. Item 196 (automatic spam
tagging from abusectl) is out of scope and blocked on item 194's sidecar.

## The problem, measured

`Mark spam` adds the notmuch tag `spam` and removes `inbox`
(`src/mainwindow.cpp:1786`). Nothing else happens:

1. The file never moves. It sits in the inbox folder with a tag on it, which is
   exactly the half-done state item 103 removed for Delete.
2. There is no view that lists it. `kQueryGenerators` (`src/config.cpp:67`) is a
   closed set of six: `unread`, `inbox`, `flagged`, `sent`, `drafts`, `trash`.
3. `unread` is not touched, so a message marked spam without being read keeps
   counting toward every unread view. This is item 195, reported by the user in
   their own notes and verified in the code rather than assumed: the call names
   two tags and `unread` is not one of them.
4. The action is absent from the message bar (item 190), though it meets the
   bar's rule of being selection-scoped and undoable.

So Mark spam is a filing action that files nothing, and the mail it marks is
invisible afterwards.

## Prerequisite, already true

Every account has a spam folder, and every one of them is synced. Verified
2026-09-10 against the live Maildir and `~/.mbsyncrc` rather than assumed:

| account | spam folder | how it syncs |
|---|---|---|
| three provider-a accounts | `[Gmail]/Spam` | named explicitly in `Patterns` |
| provider-b, account one | `Spam` | covered by `Patterns *` |
| provider-b, account two | `Spam` | covered by `Patterns *` |

Two folders were rejected rather than overlooked. One account has a `Junk`
beside its `Spam`, which item 187 already ruled out of scope as unused. The
other has a `confirmed-spam` beside its `Spam`; the user chose `Spam`, so
`confirmed-spam` stays a folder they file into by hand and this application
never writes to it.

This matters because of item 103's trap: `Create Both` turns a wrong folder
name into a folder created on the server, where every other client then sees
it. The names above were read off the disk, not composed.

## Config

One new per-account key, `spam`, relative to `maildir`, beside `trash`.

**Mandatory**, exactly as `trash` is, and for the same reason: a per-account
optional folder reintroduces the "some accounts can mark spam and some cannot"
branch that item 103 deliberately removed. A missing `spam` is a config error
reported through the existing warnings path.

`Account::spamQuery()` beside `trashQuery()`, and `Config::allSpamQuery()`
beside `allTrashQuery()`. Both are the existing helpers with a different field:
`folderQuery(maildir, spam)` and `joinAccountQueries(m_accounts,
&Account::spamQuery)`. Nothing new is invented here.

## The view

A seventh built-in filter, `spam`, appended to `kQueryGenerators` and named in
`Config::builtinFilter()`.

**Trash is the template in every respect**, which is the whole reason this item
is M rather than L:

- **Path-based, not `tag:spam` based.** The folder is the truth. This is what
  lets the view show mail the provider's own filter caught, which is most of
  what those folders hold, and it is the user's stated reason for choosing it on
  Trash: it distinguishes what this application marked from what something else
  did.
- **Threaded, not flat.** Like Trash and unlike Sent: a spam message still
  belongs to its conversation, and folding it back is a problem Sent had to
  avoid rather than one every folder filter has.
- **Composes with the account dropdown.** All accounts gives the union,
  `matchNothingQuery()` when that union is empty. One account gives
  `scope.spamQuery()`, the account's OWN query, never the all-accounts query
  wrapped in this account's path. That wrap returns the right rows by accident
  of `path:` being hierarchical, which is exactly why the test asserts on the
  generated STRING.

## The action

`spam` moves the file into the account's spam folder, through the existing
`sendMove()`. It stops being a tag-only action and becomes the same shape as
Delete.

```
sendMove(ids, <account>/<spam folder>,
         { "spam", kOriginTagPlaceholder() },   // add
         { "unread", "inbox" },                 // remove
         tr("Mark spam"))
```

Three things about that call are decisions:

**The origin tag is `moved-from:`, renamed from `deleted-from:`.** The user
chose this over reusing the deletion-named tag, and the objection that killed
reuse in the first pass was priced wrong. The migration cost was estimated as
"hundreds of messages, so a dual-prefix reader indefinitely"; measured on the
live index on 2026-09-10 it is **8 messages carrying one distinct tag value**,
all of them drafts. A one-time rename does it:

```
notmuch tag +moved-from:'<folder>' -deleted-from:'<folder>' \
  -- tag:'deleted-from:<folder>'
```

So the reader stays single-prefix, there is no compatibility branch, and the
tag stops being named for an action it no longer only serves. Delete and Mark
spam both write it; `originTagFor()` changes the prefix it composes and nothing
else.

**One origin tag ever, overwritten on each move.** This is the rule the user
chose, and it is what makes Empty Spam below safe. A move STRIPS any existing
`moved-from:` and writes the folder it is leaving now, so a message can never
carry two. Restore is then one hop back per press: out of the trash into spam,
out of spam into the inbox.

The alternative, keeping the whole chain, was rejected on a property of notmuch
rather than on taste: **notmuch tags are an unordered set**, so a reader cannot
ask which origin came first without the tag encoding its own ordering. The
existing reader takes the FIRST tag matching the prefix and breaks
(`mainwindow.cpp:6901`), which with two origins picks one silently. That is the
same defect the code already records at `mainwindow.cpp:5971`, where a message
held `deleted-from:inbox` and `deleted-from:Trash` at once "and no way to tell".
Overwriting removes the question instead of answering it.

The cost is accepted and is small: a message that reached the trash by way of
spam has forgotten it was ever in the inbox, so returning it home is two
presses rather than one. The user's own words: "once it goes back in spam it can
be restored in inbox, no need to remember where it came from."

**`unread` is stripped, in the SAME write.** This is item 195, and it follows
Delete's precedent from item 168 exactly. In the same change rather than as a
second write, so one undo returns the folder and the tag together:
`TagChange::inverted()` gives it back only if it travelled with the move. As
with Delete, this rewrites the Maildir filename because
`maildir.synchronize_flags` is true, and so reaches the server on the next
mbsync. That is the same mechanism the `post-new` hook refuses to touch, and the
difference is the same one: the hook tags arriving mail unattended, while this
is an explicit gesture on a message in front of the user.

**`inbox` goes with it**, for the reason item 168 measured on Delete: without
it, a message marked spam FROM the inbox keeps the tag the Inbox filter matches
on and stays in that view after being thrown away.

**Undo needs no new code, and Restore needs only the new prefix.** A move is already undoable and
`restoreSelectedFromTrash()` already resolves the origin from the database
rather than from the model, which is the property item 170 exists to protect.

## The message bar

`spam` joins the bar's ordinary branch beside Reply, Forward, Star, Archive and
Delete (item 190). It meets the bar's rule: selection-scoped and undoable.

**Icon: `bug`, falling back to `mail-mark-junk`.** Measured on the user's
machine rather than chosen from memory:

- Their theme draws `mail-mark-junk` as a warning octagon with an exclamation
  mark, which is not what the note asked for ("a bug, or a skull, or something
  that signifies bad/evil") and reads as generic warning beside the other bar
  icons.
- Their theme ships `bug` as a clear ladybug, which is.
- `bug` is NOT a freedesktop standard name and appears in 0 of the 24 system
  themes on that machine; `mail-mark-junk` is standard and appears in 8.

So the chain is strictly better on their desktop and identical to today's
behaviour everywhere else, where `bug` resolves to nothing and the fallback
answers. A bare `bug` was rejected because it gives a BLANK toolbar button on
every standard theme, which is the failure item 70's split exists to avoid.

This is the first entry in the icon table to carry a fallback, so the table's
value becomes a name plus an optional fallback rather than a single name. The
no-duplicate-icons test compares what the table holds, so it keeps working on
the primary name.

## The trash predicate must not answer for spam

`everySelectedRowIsInATrashFolder()` decides what the message bar offers:
Delete hides when it is true, Restore and Purge appear. A spam folder must NOT
satisfy it, or marking a message spam would hide Delete on it and offer Purge,
which destroys mail with no undo.

The predicate compares against `account.trash` only, so it is already correct.
It is named here because the handoff flagged it and because the mistake is
invisible: nothing would fail, the bar would simply offer the wrong actions on
spam. A test asserts it directly.

## Empty Spam

**A MOVE into the trash, not a purge**, and that difference decides everything
else about it. `empty_trash` destroys files and is the one irreversible action
in this application, so item 118 gave it a confirmation dialog and no default
shortcut. Empty Spam destroys nothing: it moves mail one folder further along,
the move is undoable like any other, and Restore brings it back. It therefore
inherits NEITHER safeguard, and adding a confirmation to it would be the defect
`AGENTS.md` names, a second confirmation on an action that has an inverse.

**Per account, never pooled.** The user's constraint, and it is item 103's trap
restated: one account's spam must not land in another account's trash. The
grouping already exists and is reused rather than rebuilt. `trashMessages()`
builds a `QHash<QString, QStringList> byTrash` keyed on
`account.maildir + "/" + account.trash`, resolved from each message's OWN path
through `accountForMessagePath()`, precisely because a selection can span
accounts. Empty Spam groups the same way, so five accounts produce up to five
moves and each message reaches its own account's trash.

Scoped to the account dropdown like every other account-aware surface, exactly
as `emptyTrash()` is: All accounts empties every configured spam folder, a
selected account empties only its own.

**The origin tag is rewritten, not appended**, per the one-tag rule above. Each
message leaves with `moved-from:<its own account's spam folder>`, replacing the
`moved-from:<inbox>` it may have carried. Restore from the trash then returns it
to Spam, and a second Restore returns it to the inbox.

**An empty query must never be run.** `emptyTrash()` refuses when the resolved
query is empty and says why, because an empty notmuch query matches EVERYTHING.
This inherits that guard rather than trusting the worker's own.

## Cleanup of stranded spam

Every message the action has ever marked is tagged `spam` and sitting where it
always was. That mail is now half-filed in exactly the way item 103 described:
absent from the Spam view, and unreachable by Restore.

A menu entry beside `cleanup_stranded`, copying
`showStrandedDeletedMail()` rather than inventing a second mechanism. It runs
`tag:spam and not <spam paths>` into the ordinary thread list, reports what it
finds, and moves nothing. The user selects what should go and presses Mark
spam.

Two details of the precedent are load-bearing and must be copied:

- An empty folder list must never be written as `not ()`, which notmuch parses
  happily and matches nothing, reporting a clean database.
- It runs `AlreadyScoped`, so the account dropdown does not narrow it and hide
  another account's stranded mail.

**Repeatable, not a one-time migration**, for the reason the user gave on item
103: "I don't like that it's one time. Maybe I don't have time to deal with it
at that moment."

## What is NOT in scope

- **Automatic spam tagging from abusectl** is item 196, blocked on item 194's
  sidecar. Nothing here reads an external source.
- **Purging spam outright.** `empty_trash` and `purge` stay trash-only. Empty
  Spam moves mail to the trash; destroying it is then the trash's job, through
  the machinery that already asks before it runs. Spam never gains a second
  irreversible action.
- **An "unmark spam" / "not spam" action.** Filed as a backlog item rather than
  built here, at the user's decision. Restore already covers the case this
  application created: a message it moved carries `moved-from:` and goes back
  where it came from. What is genuinely missing is reverting a decision made by
  the PROVIDER's filter, on mail that was never in the inbox and carries no
  origin tag, and that needs its own answer about where such a message should
  go and whether the provider can be told its filter was wrong. Building a
  seam for it now is what YAGNI names: `sendMove()` already takes any
  destination and any tags, so there is nothing left to provision.
- **A `Junk` folder.** One account has one; it is unused and the key names one
  folder.

## Testing

Following the rule this repo states for itself: test what has a right answer,
and hand the visual half to the user.

- **The generated query STRING**, for both the all-accounts and the per-account
  case, as every other filter is tested. A row count passes against the
  double-scoped wrap, which is the trap `Config::resolvedQuery` documents.
- **The move**, against the throwaway database the worker tests already build:
  assert the file is at the destination, that the index knows it there and not
  at the origin, and that `spam` and the origin tag both landed.
- **`unread` is stripped by the move** (item 195), asserted on a message that
  was unread when it was marked.
- **Undo**, asserting the round trip returns the file to its exact original
  path and strips both tags.
- **The trash predicate answers false for a message in a spam folder**, which
  is the silent failure named above.
- **The cleanup query** excludes mail already in a spam folder, and is not
  `not ()` when no account configures one.
- **A missing `spam` key warns**, through the same path `trash` uses.
- **Empty Spam groups per account**, asserted on a selection spanning two
  accounts: each message reaches its OWN account's trash. This is the user's
  stated constraint and the one that silently corrupts filing if wrong.
- **Empty Spam rewrites the origin tag rather than appending**, so a message
  moved inbox -> spam -> trash carries exactly one `moved-from:`, naming the
  spam folder.
- **Empty Spam refuses an empty query**, since an empty notmuch query matches
  everything.

The icon and the bar's appearance are handed over to be looked at, per the rule
that a green suite is not evidence a UI design is right.
