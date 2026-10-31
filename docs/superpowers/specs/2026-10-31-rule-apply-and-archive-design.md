# Applying a rule to existing mail, and rules that archive on arrival

Date: 2026-10-31. Backlog items 212 and 213. Settled with the user in one
brainstorm; the three decisions they made are marked **Decided**.

## Problem

Two gaps, found together.

1. **A rule only ever reaches new mail.** The `post-new` hook scopes every rule
   to `tag:new`, so a rule edited today, a new sender added to `promo` say,
   leaves every message already in the index untouched. Applying it to the
   backlog is a hand-typed `notmuch tag`. The tagging-rules spec
   (`2026-08-12-tagging-rules-design.md`, "Out of scope") deferred this as
   backfill and named the two things it needed settled first: an unscoped
   rule is not as safe as a scoped one, and `CLAUDE.md`'s "undo, never
   confirm" rule does not obviously cover a write over the whole mailbox.

2. **A rule that removes `inbox` never runs at all (item 213, a defect).** The
   hook refuses to remove `inbox` or `unread` unattended, and it does so by
   skipping the WHOLE rule, not only the removal. The user's `promo` rule is
   `+promo -inbox`, so every sync logs

   ```
   post-new: rule 'promo' would remove inbox; skipped, this hook does not remove those unattended
   ```

   and new promotional mail arrives untagged and in the inbox. That is how
   10k of it accumulated before item 211's archive. The hook's own comment
   expected this day ("DELIBERATELY CONSERVATIVE, AND EXPECTED TO BE
   RELAXED ... the question to answer first is what confirms the rule before
   it runs").

The rules dialog does not warn about either. `CLAUDE.md` says a rule the hook
will refuse should be flagged in the UI; nothing does that today.

## Part A: Apply to existing mail

### Behaviour

A button, **Apply to existing mail**, sits beside *Preview in list* in the
rules dialog and acts on the SELECTED rule only. It is disabled for a rule
that is disabled, has an empty query, or has neither `add` nor `remove`.

**Decided: only what the hook would run is applied.** Pressing Apply first
SAVES the dialog, with the same validation `onSave()` does, but without
closing it. A validation failure or a write failure stops there, exactly as
OK does. So the rule that gets applied is byte for byte the rule the hook will
run on the next sync, and the user does not have to close and reopen the
dialog to apply an edit.

Then:

1. **Count what would change, not what matches.** The worker counts messages
   matching

   ```
   (<rule query>) and (not tag:a1 or not tag:a2 ... or tag:r1 or tag:r2 ...)
   ```

   built through `SearchTerm` (`tag()` for each term, `extend()` for the
   conjunction, so the rule's own disjunctions cannot escape the scope). A
   message already in the target state is not counted, so the number in the
   confirmation is the number of messages that will actually be touched.
   It goes through the existing `requestMessageCounts`, which already
   counts messages rather than threads and has its own generation counter.

2. **Zero: nothing to ask.** The status bar says the rule has nothing to
   change, and no dialog appears.

3. **Otherwise, confirm.** **Decided: confirm with the count.** A
   `QMessageBox::question` parented to the rules dialog:

   > Apply rule "promo" to 1,234 messages?
   > Adds: promo. Removes: inbox.
   > You can undo this from the main window.

   Cancel is the default button. The count and the tag lists are the
   information the user cannot get from the list on screen, which is the
   whole reason this write gets a confirmation and others do not.

4. **Write.** A new worker slot, `applyTagsToQuery(query, add, remove,
   description)`, runs the same change query to collect message ids, then
   calls the existing `applyTags()`. Everything downstream is inherited
   unchanged:
   - exact undo: `applyTags()` reports only the ids whose tags moved (item
     176), and the undo entry is a `MessageTagCommand` armed with
     `awaitTagConfirmation()` before the send, so Ctrl+Z reverts exactly
     those;
   - the pending-changes count and the auto sync, from `onTagsApplied()`;
   - the "applying… / done" status from item 211's `announceAction()`;
   - the write lock: if a sync holds it, the edit is held and replayed the
     same way a list edit is.

   After the confirmation arrives, the current query is refreshed, since the
   rows a backfill touches are mostly not in the model and cannot be updated
   optimistically.

5. **Protected tags.** Removing `inbox` or `unread` is ALLOWED here. The
   hook's refusal exists because "there is no human present to confirm
   anything when cron runs a sync"; Apply has a human present who has just
   confirmed a named count.

### The undo command needs its ids from the confirmation

`MessageTagCommand` is constructed with the ids it acts on. For a backfill
those ids are unknown until the worker has resolved the query. The command is
constructed with an empty id list and filled from the confirmation, which
`TagCommand::addChangedMessageIds()` already does for every command (item
176). The plan must verify that redo and undo both read the EFFECTIVE set and
never the constructor's list; if either reads the constructor's list, an
empty one would redo nothing, and the plan fixes that rather than working
around it.

### Rule change to CLAUDE.md

The "no destructive-action confirmation" section gains a second exception
beside `empty_trash`, stated by its property rather than by its name: **a
write whose scope is the whole mailbox, rather than rows on screen, confirms
with the count.** Undo still applies. The difference from `empty_trash` is
recorded too: that one confirms because it cannot be undone, this one
because the user cannot see what it will touch.

## Part B: rules that archive on arrival

### Format

**Decided: a per-rule opt-in.** A new optional boolean field on a rule:

```json
{ "id": "promo", "query": "...", "add": ["promo"], "remove": ["inbox"],
  "allow_archive": true }
```

- Absent or `false` means today's behaviour.
- Optional, so **no `kFormatVersion` / `FORMAT_VERSION` bump** ("Changing the
  rule format", step 2).
- Both readers learn it in the same change (step 1): `TagRule::allowArchive`
  in `src/tagrules.cpp`, `Rule.allow_archive` in `assets/hooks/mailrules.py`,
  each added to its known-keys list so it is no longer carried as an unknown
  field. A writer omits it when false, so a file with no archiving rules is
  unchanged by a save.
- A flag on a rule that does not remove `inbox` does nothing, and is NOT a
  validation error. Making it one would have the hook drop a whole rule over
  a harmless flag, and `CLAUDE.md` already records that repairs belong in the
  editor, never in the unattended hook. The editor keeps it from arising:
  the checkbox exists only while `inbox` is in the remove list, and the
  writer clears the flag on a rule that no longer removes `inbox`.

### Hook

`protected_removals(rule)` drops `inbox` from the refused set when the rule
has `allow_archive`. `unread` stays refused unconditionally: removing it
rewrites Maildir filenames under `maildir.synchronize_flags` and reaches the
server, and nothing about archiving needs it.

A rule that removes `inbox` WITHOUT the opt-in is still skipped whole, as
today. Skipping only the removal would be a smaller change in behaviour, but
it would silently run a rule differently from how it reads. The log line
gains the remedy:

```
post-new: rule 'promo' would remove inbox; skipped (set "Archive on arrival" on the rule to allow it)
```

The `tag:new` scope and the non-arrival carve-out are untouched. An archiving
rule still only ever sees newly indexed mail.

### Dialog

- A checkbox, **Archive matching mail on arrival (removes inbox)**, under the
  remove field. It is shown only while the rule's remove list holds `inbox`,
  and it maps to `allow_archive`.
- The warning banner, through the existing `setWarning()`, names any rule
  the hook will skip: one that removes `inbox` without the opt-in, or one
  that removes `unread` at all. This closes the gap `CLAUDE.md` already
  describes, where the dialog would let the user write a rule the hook then
  refuses with no word in the UI.

## Testing

Measurable properties only, per `CLAUDE.md`.

- **SearchTerm:** the change query for `+a -r` over a query containing ` or `
  is the exact expected string, with both sides parenthesised. Add-only and
  remove-only rules each produce the right half.
- **Worker:** `applyTagsToQuery` against a throwaway index holding messages in
  BOTH states (some already tagged, some not; the item 87 lesson) reports
  only the ones that changed, and leaves the rest alone.
- **MainWindow, worker-backed:** Apply on a rule with N changeable messages
  produces one undo entry, and undo restores the exact prior tags of a
  message that was already in the target state AND one that was not.
- **Dialog:** Apply with an unsaved edit writes the file before emitting;
  Apply with a validation failure emits nothing. Assert every route, per the
  `done(int)` lesson.
- **Format, both suites:** `allow_archive` round-trips through each reader;
  absent stays absent after a save; the C++ writer clears it on a rule
  without `-inbox`, while the Python reader accepts and ignores it there;
  an unknown sibling field still survives.
- **Hook:** an opted-in `-inbox` rule archives a `tag:new` message; the same
  rule without the flag is skipped and logs the remedy; `-unread` with the
  flag set is still refused.
- **By hand (rule-format step 4):** save an opted-in rule from the dialog, run
  the hook over a throwaway index, and confirm the rule count, the note and
  the flag survive.

The confirmation box itself is not unit-tested beyond its count reaching it;
the user looks at it.

## Out of scope

- **Applying every rule at once** in stage order. The user chose the selected
  rule; a whole-set run is a separate decision with ordering questions of its
  own.
- **Relaxing `unread`.** Nothing asks for it.
- **A dry-run list** of the affected messages. *Preview in list* already shows
  which mail a rule collects.
