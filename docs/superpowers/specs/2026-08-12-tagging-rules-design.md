# Shared tagging rules: design

Backlog item 44, "No way to manage the filters applied at sync time".

**Status:** design approved 2026-08-12, not implemented.

## The problem this solves

The user asked for a way to "manage filters to be applied when syncing (view
existing, edit, delete, create new, copy as new, dry-run)". The backlog recorded
the item as unspecified for a week because nothing in qtmaildir applies rules at
sync time, and the item could not be planned until it was known whether such
rules existed anywhere.

They do. They live in the notmuch `post-new` hook inside the user's Maildir, as
a sequence of hand-written `notmuch tag` commands, each scoped to `tag:new` so
it applies to newly indexed mail only. The hook is tag-only by design: it never
removes `inbox` or `unread`, so nothing is archived or marked read unattended.

Those rules are invisible from every tool. They are also where a substantial
amount of hard-won reasoning lives, in comments recording which senders were
deliberately excluded from a rule and why. That reasoning is readable only by
opening the hook in an editor, and it is exactly the context a person needs when
deciding whether a rule is still correct.

## What is being built

A rule store both tools read, in a format neither owns.

```
        ~/.config/mailrules/rules.json
                     |
     +---------------+---------------+--------------+
     |               |               |              |
  post-new       qtmaildir       qtmaildir      mailctl rules
  (mailctl)      TagRules        dialog          list/show/
     |           read+write      dry-run         dry-run
     |               |               |              |
  scope:          atomic          via           read-only
  tag:new         QSaveFile    NotmuchWorker
     |                          (generation-
  notmuch tag                    stamped)
  per rule
     |
  -new consumer
  (hardcoded,
   only if all
   rules ran)
```

The constraint that shaped this: qtmaildir is the main consumer of the mail
system, but mailctl must keep working through it. A format with one
implementation is not neutral, it is qtmaildir's format that mailctl is invited
to parse. Both readers are therefore written together, and the format carries an
explicit provision for fields one tool does not understand.

## The file

`$XDG_CONFIG_HOME/mailrules/rules.json`, falling back to
`~/.config/mailrules/rules.json`. Neither tool hardcodes a home directory, and
the location is under neither project's own config directory.

```json
{
  "version": 1,
  "rules": [
    {
      "id": "account-work",
      "stage": 10,
      "enabled": true,
      "add": ["account-work"],
      "remove": [],
      "query": "path:\"work-account/**\"",
      "note": "Account tags use the maildir spelling, not the account keys another tool shows. Must run before topic rules."
    },
    {
      "id": "notify-forge",
      "stage": 50,
      "enabled": true,
      "add": ["notify/forge"],
      "remove": [],
      "query": "from:notifications@example.com",
      "note": "All repositories, not one project."
    }
  ]
}
```

JSON, and not the INI the item originally named. Both tools parse JSON with no
new dependency: `QJsonDocument` in Qt, `json` in the Python standard library,
and mailctl is deliberately stdlib-only. INI was rejected on a concrete hazard
rather than taste: tag names in use contain `/` (`notify/forge`,
`mailing-list/*`, `shopping/*`), and `QSettings` treats `/` in a section name as
a group separator. `CLAUDE.md` already records that trap costing this project
once, over the `[general]` section.

The cost of JSON is that the file cannot carry loose comments. This is
acceptable because every comment in the current hook is attached to a specific
rule, and `note` gives it a home that a UI can display. The reasoning stops
being visible only to someone reading shell.

### Fields

| Field | Meaning |
|---|---|
| `version` | Format version, currently `1`. A reader finding a higher number refuses the file rather than guessing at semantics. |
| `id` | Stable unique handle, `[a-z0-9-]`. What a UI selects and a diff tracks. |
| `stage` | Integer, ascending. Ties broken by array order. |
| `enabled` | A rule switched off without losing its note. |
| `add` / `remove` | Tags, as separate arrays. |
| `query` | notmuch query. Carries no scope and no `tag:new`. |
| `note` | Why this rule is shaped the way it is. |

**`id` is never the tag name.** Tags contain `/`, and a tag can be renamed while
the rule stays the same rule.

**`add` and `remove` are arrays, not `+tag`/`-tag` strings.** Every current rule
only adds, but the format should not need a version bump the first time a rule
wants a removal, and parsing sigils off strings invites the class of bug where a
tag legitimately beginning with a sigil character is mangled.

**Unknown keys are preserved across a write by either tool.** A reader stashes
every field it does not recognise and a writer merges them back. Without this,
one qtmaildir save silently strips whatever a newer mailctl wrote, and the file
is neutral in name only.

## Scope belongs to the runner, not the rule

A rule is a `(add, remove, query, stage)` tuple that knows nothing about which
mail it applies to. Whoever runs it supplies the scope.

| Runner | Scope | Effect |
|---|---|---|
| `post-new` | `tag:new` | Tags newly indexed mail. |
| Dry run | none | Counts against the whole corpus. |
| Backfill (future) | none | Applies to the whole corpus. |
| A future timer or hook | its own | Not designed here. |

This is why stored queries omit `tag:new`. A query that carried its own scope
would have to be stripped back down for a dry run to be useful, and a
hand-written rule that forgot the scope would silently retag the entire archive.

It also settles where the `tag:new` consumer lives. `notmuch tag -new -- tag:new`
is not a tagging rule, it is the `post-new` runner's own cleanup, and it is
hardcoded there rather than stored as data. It cannot be deleted, disabled or
reordered by either tool because it is not a rule. The failure mode if it went
missing justifies putting it out of reach: `tag:new` would accumulate, and every
rule would begin matching the whole backlog instead of new arrivals.

Rules genuinely untied to `tag:new` already exist as manual work today:
retroactively applying a newly written rule, and re-running a corrected rule
over history after a mistake. Both are the same rule at a different scope, which
is what this split makes expressible.

## The runner

Today's hook is a sequence of hand-written `notmuch tag` lines. It becomes a
loop over `rules.json`.

**Python, not shell.** The hook must parse JSON. Shell would mean either a new
`jq` dependency on a path that runs every ten minutes, or hand-parsing JSON in
`sh`. Python 3 is already required by mailctl and needs no imports beyond the
standard library here.

**It lives in the mailctl repository, installed to `~/bin/`.** The hook is a
mail-organization concern and mailctl is the mail-organization tool. Note the
asymmetry with `mailsync.sh`, which lives in qtmaildir precisely because
qtmaildir runs it as a subprocess and depends on its behaviour; here the
relationship is reversed, since qtmaildir never invokes the hook and `notmuch
new` does.

```
mailctl repo
  mailrules.py     load/save/validate/stage-order, unknown-key preservation
  post-new         reads rules.json, applies with tag:new scope, then -new
  mailctl.py       mailctl rules list|show|dry-run

qtmaildir repo
  src/tagrules.*   the same format, read/write/validate in C++
  (dialog)         view, edit, create, delete, copy-as-new, dry-run
```

### Algorithm, and the properties that must survive any edit

1. Load `rules.json`. **On any error, missing file, bad JSON or failed
   validation, log to stderr and exit non-zero WITHOUT running the `-new`
   consumer.**
2. Sort enabled rules by `stage`, then by array position.
3. For each rule, run
   `notmuch tag <+add...> <-remove...> -- 'tag:new and (<query>)'`.
4. **Only after every rule succeeded**, run `notmuch tag -new -- tag:new`.

**Step 1 is the critical safety property.** If the consumer runs while the rules
did not, `tag:new` is cleared from mail that was never tagged, and that mail can
never be tagged by these rules again. The failure is silent, permanent, and
invisible until someone notices a gap months later. A rules file that fails to
load must leave `tag:new` in place so the next successful run catches up.

**The query is parenthesised in step 3, and this is not cosmetic.** Several
current rules are a disjunction of senders. Without the parentheses,
`tag:new and a or b` binds as `(tag:new and a) or b`, and the rule matches every
message in the corpus that satisfies `b` rather than only new arrivals.

**Step 4 makes the run idempotent.** A failure partway through leaves `tag:new`
set, so re-running is safe and completes the work. This is also what makes the
migration verifiable: old and new hooks can be run against the same `tag:new`
set and compared.

## qtmaildir

**`TagRules` (`src/tagrules.h`, `src/tagrules.cpp`)**, a plain value type with
no widget dependency, following `Config` and `KeyMap`: it parses, validates,
collects warnings rather than throwing, and is unit-testable without a UI.
`QJsonDocument` to parse and serialize, `QSaveFile` to write.

```cpp
struct TagRule {
    QString id, query, note;
    QStringList add, remove;
    int stage = 50;
    bool enabled = true;
    QJsonObject unknown;   // fields this version does not know, preserved on save
};
```

The `unknown` member is the neutrality guarantee made concrete.

**Validation degrades, it does not refuse.** Following `Config`'s existing
pattern: a duplicate id, an empty query, or a rule with neither `add` nor
`remove` drops that rule and records a warning the dialog shows. A file that
will not parse leaves the list empty with one warning. qtmaildir must never fail
to open because of this file.

**Dry-run runs on the worker.** `NotmuchWorker` owns the only
`notmuch_database_t*` and notmuch permits one open handle per process, so
counting cannot happen on the UI thread even if it were fast. It is not fast:
item 74 measured a 4444-thread query at 5.7 seconds against a cold page cache,
and a dry run counts every rule. The counting slot is generation-stamped like
every other query so a superseded result is discarded.

Each rule shows two numbers:

- **Corpus count**, what the rule matches across all mail. This is the number
  that answers "is this rule still doing anything" and "would this rule bury
  real correspondence", which is what the comments in the current hook spend
  most of their words reasoning about.
- **Pending count**, what it would tag on the next sync. Usually zero between
  syncs.

**The dialog** lists rules in stage order, each showing its tags, query, note,
enabled state and counts. Edit, create, delete and copy-as-new operate on the
list; Save writes atomically. Rules are a property of the mail system rather
than of an account, so the account dropdown does not scope them.

## mailctl

Read-only in this version:

```
mailctl rules list                 # id, stage, tags, enabled, note
mailctl rules show <id>            # one rule, full query and note
mailctl rules dry-run [<id>]       # corpus counts, all rules or one
```

**No `rules edit`, `add` or `delete`.** mailctl's safety model is that reads are
free and mutations are gated, and a rule edit is a mutation whose blast radius is
every future sync. What that gate should be is a real design question and it is
not needed yet: qtmaildir has the editor, and the file has a text editor. Writing
is fully designed and implemented in `mailrules.py`, because the hook's library
and qtmaildir must agree on atomic-write and unknown-key semantics, but no CLI
surface exposes it.

`dry-run` reuses the existing `run_notmuch` and counting paths. **Its numbers
must equal qtmaildir's for the same rule**, and checking that once by hand after
migration is the cheap cross-check that the two parsers agree.

## Concurrency

Atomic writes, no locking. Every writer does write-to-temp then `rename()`,
which is atomic within a filesystem, so a reader always sees a complete file,
old or new. A truncated read by the hook is therefore impossible, which is the
failure that would actually hurt.

Last writer wins on a genuine collision, silently. Accepted: this is a
single-user setup, agents run only when asked, and simultaneous edits from two
tools are rare enough to be corrected by eye. Locking was rejected because a
stale lock would block the hook, which is the one thing that must not be
blocked.

## Migration

The existing rules are converted once, preserving each comment as the rule's
`note`.

**The live hook is not touched until the user has reviewed the conversion.** The
new hook is staged alongside as `post-new.new` and swapped by the user.

**Equivalence is proved, not assumed.** For each rule, `notmuch count` the old
query and the new `tag:new and (query)` construction against the full corpus,
unscoped, and compare rule by rule. Identical counts are the evidence that the
conversion changed nothing.

The conversion table names real senders and is therefore built and kept locally,
outside version control, per the project rule against personal details in
committed documentation. Every example in this document uses placeholders.

## Testing

**qtmaildir, `tests/test_tagrules.cpp`:** parse; round-trip preserving unknown
fields; each validation warning; stage ordering including ties resolved by array
position; and JSON escaping of queries containing quotes, which is not
hypothetical since path-scoped queries are written `path:"account/**"`.

**mailctl:** plain-assert tests for `mailrules.py` matching the existing
`test_mailctl.py` style, covering the same load, save and ordering semantics.

**Cross-tool:** the dry-run counts from both tools compared once by hand after
migration.

The risk here is in the file format and the runner's failure modes, not in
painting. `CLAUDE.md`'s warnings about rendering probes apply to the dialog if
it grows custom painting, which this design does not call for.

## Out of scope for v1

**Backfill: applying a rule to existing mail.** The counts are shown; the button
is not built. This is wanted and is the natural next step, since "apply this new
rule to the backlog" is currently a hand-run `notmuch tag`.

Two things must be settled before it lands. A rule that is safe against arrivals
is not automatically safe against the whole corpus: a rule removing `inbox`, run
unscoped, would archive years of mail in one action. And `CLAUDE.md` currently
states that this project has no destructive-action confirmation and uses undo
instead, which does not cover a bulk tag write over thousands of messages. **The
user has stated that rule is due for revision**; backfill is the change that
forces it.

**Rule edits from mailctl**, pending the gating decision above.

**`post-insert` and timer-driven rules.** The scope split makes them
expressible, and nothing is designed for them here.
