# A rule that moves mail to the spam or trash folder

Item 214. Designed 2026-11-16.

## Problem

The rules dialog can only add and remove tags. It was built (item 44,
`2026-08-12-tagging-rules-design.md`) before Delete and Mark spam became file
MOVES (items 103 and 187), so a rule can tag a message `spam` but cannot put it
in the spam folder. The result is mail the rule recognised still sitting in the
inbox folder, and showing up under *Find stranded spam*.

## Decisions made with the user

1. **Destination is an account ROLE, never a folder name.** A rule says
   `spam` or `trash`; each message goes to its OWN account's configured folder
   for that role. A typed folder name is refused because a wrong one is created
   by the move and propagated to the server (item 103). There is no `archive`
   role: archive is a tag, not a folder.
2. **The move strips `inbox` and keeps `unread`.** `inbox` goes because a
   message in the spam folder carrying it would still be listed by the Inbox
   filter. `unread` stays because `maildir.synchronize_flags` is on and
   removing it reaches the server; keeping it also lets the user see what a
   rule caught. `PROTECTED_REMOVALS` is NOT relaxed: the `-inbox` is part of
   the move, applied only to mail the move actually relocated, never to a
   rule's `remove` list.
3. **On arrival only.** The `post-new` hook performs the move. Item 212's Apply
   button (designed in `2026-10-31-rule-apply-and-archive-design.md`, unbuilt)
   will honour `move` when it is built, by routing through the existing
   `NotmuchWorker::moveMessages()` so the move gets undo. This item does not
   build that.

## Format

One optional field on a rule:

```json
{ "id": "known-spammers", "query": "from:example.org", "add": [], "remove": [],
  "move": "spam" }
```

- Values: `"spam"` or `"trash"`. Absent or empty means no move.
- Optional field, so **no `kFormatVersion` / `FORMAT_VERSION` bump.**
- Both readers change (`src/tagrules.cpp`, `assets/hooks/mailrules.py`) and both
  drop it from `unknown` handling by knowing it.
- The "adds and removes nothing" check becomes "adds, removes and moves
  nothing": a move-only rule is valid.
- An unknown value is an invalid rule in `TagRules::validate()` (the dialog
  cannot write one). The Python reader logs it and ignores the `move` while
  still applying the rule's tags, since the tags are harmless and the hook must
  never guess a destination.

## Hook

After all tag stages have run, and BEFORE `tag:new` is consumed:

1. For each enabled rule with a `move`, in stage order, collect message ids
   matching `tag:new and (<query>)`. A message claimed by an earlier rule is
   not reclaimed; the conflict is logged naming both rules.
2. For each claimed message, resolve its file to an account by path (the
   account whose `maildir` contains it, compared as paths) and that account's
   `spam` or `trash` key. `qtmaildirconf` gains a reader for those two keys.
3. Skip, with a log line: a message with MORE THAN ONE file (mail sent to
   another of the user's own accounts, item 166: two files, two accounts, and
   one message cannot carry two origins); a file in no configured account; an
   account with no folder for the role (`Config` already warns about that,
   since both keys are mandatory); a file already in the destination folder.
   Never invent a folder.
4. Rename the file into `<mail_root>/<account maildir>/<folder>/cur/` under a
   FRESH name (no `,U=` infix, item 162; ported from `MaildirName::fresh()`,
   keeping the `:2,` flags suffix and adding an empty `:2,` when the file
   came from `new/` with none). `cur/`, never `new/`. Record the origin before
   the rename.
5. After all renames, run ONE `notmuch new --no-hooks` so notmuch records each
   rename against the existing message id, keeping its tags. A renamed file
   with a known id is not a new message and gets no `new.tags`.
6. Then tag each relocated id: `+moved-from:<origin>`, where origin is the
   folder relative to the ACCOUNT's maildir (`MainWindow::originTagFor()`'s
   format, which Restore reads), `+spam` for the spam role or `+deleted` for
   the trash role (what Mark spam and Delete add), and `-inbox`. Do not touch
   `unread`. No existing `moved-from:` needs stripping: the message only just
   arrived.

The inner `notmuch new` also indexes any file that landed after the outer one,
and the closing `-new` would then consume that file's marker without rules
having seen it. Under `mailsync.sh` mbsync has finished and the lock is held,
so nothing lands; the window is accepted and commented in the code.

### Failure

- A failed rename leaves that message where it was, logged; the others proceed.
- A failed `notmuch new --no-hooks` or `notmuch tag` returns failure from the
  hook, so `tag:new` is NOT consumed (the existing safety rule). The next run
  sees the files already in the destination and only re-tags.

A file already inside the destination folder is not renamed but IS tagged
(without an origin): that covers mail a provider filed there itself, and a
previous run whose rename succeeded and whose tagging failed.

### To verify first (the hook task's first test)

That `notmuch new --no-hooks` can run from inside `post-new`, i.e. the outer
`notmuch new` has released the write lock when it calls the hook, and that a
renamed file with a known id keeps its tags and gets no `new.tags`. A test
installs the hook in a throwaway database and runs a plain `notmuch new`. If
it fails, the fallback is to defer moves to a step in `assets/mailsync.sh`
after `notmuch new` returns; that is a design change and goes back to the
user.

## Dialog

A **Move to** combo under the remove field: *Don't move*, *Spam folder*, *Trash
folder* (labels translated; the stored value is the wire-format role, kept as
item data, per the "translating a string something matches on" rule). No
per-account warning: `spam` and `trash` are mandatory keys and `Config` already
reports a missing one.

## Unchanged

- Restore and Not spam read `moved-from:` and work as they do for a manual move.
- A hook move has no undo, like every other hook tag.
- No version bump, no new dependency.

## Tests

- `assets/hooks/`: a real move over a throwaway Maildir: fresh name in `cur/`,
  tags survive, `moved-from:` written account-relative, `-inbox`, `unread`
  kept, `+spam` / `+deleted` by role, missing role skipped, two rules claiming
  one message, a multi-file message skipped, a file already in place left
  alone. Plus one test that installs the hook in the throwaway database's
  hook directory and runs a plain `notmuch new`, which is the nesting check.
- `test_tagrules`: `move` round-trips, an unknown value fails `validate()`, a
  move-only rule is valid, and a file written by Python with `move` survives a
  C++ save.
- The hand round trip under "Changing the rule format" in `AGENTS.md`.

## Out of scope

- Applying a move to existing mail (item 212 will).
- Moves to arbitrary folders.
- Relaxing `unread`.
