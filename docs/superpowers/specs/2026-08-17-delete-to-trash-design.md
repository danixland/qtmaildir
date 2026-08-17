# Delete moves mail to trash

Item 103. Design settled 2026-08-17 with the user.

## The problem, measured

`Delete` adds the notmuch tag `deleted` and does nothing else. Nothing
downstream acts on that tag, so the mail stays where it is, forever, on the
local disk and on the server.

Every step of that chain was measured this session rather than reasoned about:

1. `src/mainwindow.cpp:825` adds and removes `deleted`, through the undo stack.
   No file is touched.
2. `maildir.synchronize_flags` is `true`, but notmuch's tag-to-flag table is
   `D/draft`, `F/flagged`, `P/passed`, `R/replied`, `S/unread`. There is no row
   for `deleted` and no `T` flag.
3. A probe on a throwaway database confirmed it: `notmuch tag +deleted` left the
   filename `1234567890.probe:2,` unchanged, while the control `+flagged`
   immediately produced `1234567890.probe:2,F`. The probe works; the tag simply
   does not reach the filename.
4. `mbsync` propagates filename flags. The tag never becomes one, so
   `Expunge Both` (set on all five channels) never sees anything to expunge.
5. `assets/mailsync.sh` is `mbsync` followed by `notmuch new`. It contains no
   `notmuch tag` call and no delete path.

So Delete is a filing action wearing a destructive name. The user's verdict:
"if I want to delete a mail, I don't want to archive it."

At the time of writing there are **848 messages tagged `deleted`**, all in one
account's inbox, and **0** of them in any trash folder.

## What Delete becomes

Delete moves the message file into its account's trash folder, reindexes it, and
tags it `deleted` plus `deleted-from:<folder>`. The tag stops being the action
and becomes the record of who deleted it and where it came from.

Undo moves the file back and strips both tags. `Restore from trash` does the
same thing from the trash view, days later, using `deleted-from:` to find the
destination.

## Prerequisite, already done

Every account must have a local trash folder. In the setup this was measured
against, five accounts across two providers, two already did; the three on the
provider that nests localised folders under a bracketed parent did not, because
their `Patterns` lines named only INBOX, Sent, Drafts and Starred.

`~/.mbsyncrc` now carries the bracketed trash folder on those three channels,
and one `mbsync` run created the local folders. Verified: all three
`<trash>/{cur,new,tmp}` exist, and all three arrived empty (far side 0
messages), so the trash view starts clean there.

`mbsync -l` lists what a channel's `Patterns` would sync, not what the server
offers. That is why the trash folder appeared to be missing from the server
until the pattern was temporarily widened. Worth remembering: it is a config
readout, not a server listing.

The remote list also revealed a top-level user label literally named `Trash` on
one account, distinct from that provider's real bracketed trash folder. Only the
latter is synced. Moving mail into the former would look like it worked while
the server never treated the mail as deleted.

## Config

One new per-account key, `trash`, relative to `maildir`, beside the existing
`sent` and `drafts`.

**Mandatory**, at the user's decision: the program cannot function properly
without it, so a missing `trash` is a config error reported through the existing
warnings path, not a per-account disabled state. This removes the "some accounts
can delete and some cannot" branch entirely.

Shape of the values, from the setup this was measured against:

| account | `trash` |
|---|---|
| provider-a, three accounts | `[Provider]/<localised trash>` |
| provider-b, two accounts | `Trash` |

The bracketed form is the same shape `sent` already handles for that provider's
localised sent folder, so `SearchTerm`'s quoting is already exercised on it.

## The worker

```cpp
void moveMessages(const QStringList &messageIds, const QString &destFolder);
```

Per message: resolve the filename from notmuch, compute the destination as
`<account maildir>/<destFolder>/cur/<basename>`, `rename()`, then index the new
path and drop the old one from the index.

**The destination is a folder name, not "trash".** The user asked for this
explicitly, against the arrival of Send in v2: composing a message will need to
move it to Drafts or Sent, and that is the same operation with a different
argument. Nothing trash-specific belongs inside this function.

Deliberately NOT generalised further. A move policy or a folder registry would
be speculative until Send exists.

**Ordering is rename, reindex, tag.** A failed rename must leave no tag, so the
UI cannot show a delete that did not happen. The result carries what actually
moved, so a partial failure is visible rather than assumed.

This is the first mutation in the app that is not a notmuch tag. Everything
today goes through `applyTags` on the worker; a move is a rename plus a reindex,
and the worker has no single-file indexing at present.

## The view

A fifth built-in filter, `trash`, appended to `kQueryGenerators` and named in
`Config::builtinFilter()`.

It composes with the account dropdown exactly as the others do, per the user:
per-account trash and an "All accounts" trash. `Sent` is the template, since it
is the existing filter built from per-account folders rather than a tag:

- All accounts: the union of the accounts' trash queries, `matchNothingQuery()`
  if that union is empty.
- One account: `scope.trashQuery()`, the account's OWN query. Never the
  all-accounts query wrapped in this account's path. That wrap returns the right
  rows by accident of `path:` being hierarchical, which is exactly why a
  row-count assertion passes against it.

Unlike Sent it is not `flat`: a deleted message belongs to its conversation.

**`path:` based, not `tag:deleted` based.** The folder is the truth. This is what
lets the view show mail trashed by another client or a provider's webmail, and what
makes the view correct itself after a restore without a tag having to be
stripped. The user's reason for choosing it: it "allows us to differentiate
mails deleted by qtmaildir (`+deleted`) and mails deleted by other clients".

A card in the trash view therefore shows the `deleted` chip when this app did it
and nothing when something else did. There is one real example of the latter in
the Maildir today: a single message sitting in an account's trash folder
carrying no `deleted` tag.

## Restore

Available only when the current view is the trash filter.

Destination is `deleted-from:` when present, Inbox when not, and the status
message says which. A message trashed by another client carries no origin tag,
so Inbox is the documented fallback rather than a silent guess.

The origin tag is needed because a Maildir filename does not record where a
message came from, and notmuch cannot know once the file has moved. Undo does
not need it (it moves back explicitly), but a restore three days later does.

## Cleanup of stranded mail

The 848 messages tagged `deleted` but sitting in an inbox would otherwise be
invisibly half-deleted: struck through on their cards, absent from the trash
view, and unreachable by Restore.

A **menu entry**, deliberately not a sixth filter button, so it cannot be
confused with the Trash view. It runs the query `tag:deleted and not <trash
paths>` into the ordinary thread list. The user browses the result, selects what
should really go, and moves it with the ordinary Delete. Undo covers it like any
other move.

**Repeatable, not a one-time migration.** The user's correction: "I don't like
that it's one time. Maybe I don't have time to deal with it at that moment." It
reports what it finds whenever it is run, says so when it finds nothing, and
never fires on startup. That also covers mail tagged `deleted` by some other
route later, so it is not scaffolding to be thrown away.

Reviewing the mail as ordinary rows in the list is the preview: it reuses the
existing list, selection and multi-select machinery instead of building a review
dialog, and "check before deleting" becomes looking at your own mail the normal
way.

## Two lifetimes, one button

One provider in this setup purges its trash folder after 30 days, server-side,
and `Expunge Both` does not change that. The other's trash never empties itself,
which the user verified.

So Delete means "gone in 30 days" on three of the accounts and "moved to a
folder you can browse forever" on the other two. Same button, two lifetimes.
This belongs in the docs rather than in a warning dialog.

## The no-confirmation rule

`CLAUDE.md` records that a human at a GUI gets undo instead of confirmation
dialogs, and that the premise is that undo covers the action. After this change
that premise holds only until the server purges the trash.

The rule stands: the mail lands in a trash folder that can be browsed, selected
and restored, rather than vanishing. But its stated justification needs amending
to say that Delete's reversibility is bounded by the provider where the trash
folder is purged on a timer.

## Testing

- **The move**, against the throwaway database the worker tests already build: a
  real Maildir, a real rename. Assert the file is at the destination, that the
  index knows it there and not at the origin, and that both tags landed.
- **Undo**, asserting the round trip returns the file to its exact original
  path, not merely to a plausible one.
- **Ordering**, asserting that a failed rename leaves no tag.
- **The filter**, on the generated STRING for both the all-accounts and the
  per-account case, as the other filters are tested. A row count passes against
  the double-scoped wrap, which is the trap `Config::resolvedQuery` documents.
- **Restore**, in both directions: with `deleted-from:` present, and without it
  falling back to Inbox.
- **The cleanup query**, asserting it excludes mail already in a trash folder.

## Out of scope

No purge, no empty-trash, no expiry inside the app. Mail leaves the trash folder
when the server does it or when the user restores it. Purging from inside
qtmaildir is a separate item if it is ever wanted.
