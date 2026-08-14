# Turning a saved query into a tagging rule

Resolves backlog item **81**. Depends on item 23 (the saved-query file and its
context menu) and item 76 (the rule builder), both shipped.

## What this is

A saved query is a view: it costs nothing if it is wrong, and the user can see
exactly what it collects. A tagging rule is the same query given an action and
handed to a `post-new` hook that runs against real mail every ten minutes. This
item is the journey from the first to the second, for a query the user has
already run and whose results are on screen.

Item 77 shipped the opposite direction: a rule in the rules dialog can be
previewed in the thread list. This is that road backwards.

## The shape

**Entry point.** The saved-query context menu gains **Create tagging rule...**,
after Delete and below a separator. The menu is built once, in
`MainWindow::addSavedQueryActions()` (`src/mainwindow.cpp:1743`), and is used
for both the pinned buttons and the More queries entries, so one action added
there appears in both places.

**Stored queries only.** A generated entry (Sent, and the account-scoped ones)
does not get the item. Its query is composed at runtime from the configured
accounts, so a rule made from it would freeze a snapshot that goes stale the day
an account is added, in a file the hook reads unattended and nobody re-reads.
`SavedQuery::isGenerated()` (`src/config.h:144`) is the test, and item 82
already uses it to make the Edit dialog show a generated query read-only.

**What the action does.** Opens `TagRulesDialog` seeded with one new rule,
appended to the working list and selected:

| field | value |
|---|---|
| `query` | the saved query's stored query, verbatim |
| `id` | `TagRules::uniqueId(saved.name, <ids already in the list>)` |
| `add` / `remove` | empty, and the Add tags field takes focus |
| `enabled` | `true` |
| `stage` | 50, the default |

Nothing reaches disk until Save, exactly as the existing **Add rule** button
behaves. Cancel discards the seeded rule along with any other pending edit.

## Decisions, and why

**Not a checkbox in the Save query dialog.** The backlog proposed one. It was
rejected in favour of a separate action for three reasons: it would give the
save path a second, more dangerous job; it would make one dialog write two
files, `queries.json` here and `rules.json` shared with mailctl, raising a
partial-failure question with no good answer; and `SaveQueryDialog` is
deliberately pure UI that writes nothing and returns a value
(`src/savequerydialog.h:31-35`), which is worth keeping. The rules dialog is
also where the match count and the preview live, and those are what make a rule
safe to create.

**The seeded rule is enabled.** It matches what Add rule already does, and an
inconsistent default between two ways of making the same thing is worse than
either default. A rule created disabled and then forgotten is its own silent
failure, and the match count in the list is the check that the user has anyway.

**The tags are left empty on purpose.** A rule that adds and removes nothing
fails `TagRules::validate()`, so Save is refused with the red banner naming the
rule. The one field the user must supply is the one the dialog opens on, and
forgetting it is caught rather than written. This is the validation added for
item 83 doing the job it was built for.

**The scope difference is not restated in the dialog.** When the hook applies a
rule it supplies `tag:new` and parenthesises the query, so a rule only ever
matches newly arrived mail, never the messages the user was just looking at.
That is a real and counterintuitive difference, and it is already stated in the
dialog's intro text: "Rules tag mail as it arrives ... it does not retag mail
you already have." A second notice beside the red warning banner would dilute
the banner, which item 83 has just finished making load-bearing. If the intro
turns out not to carry it, fix the intro rather than adding a second line.

## Structure

Three small changes, no new file.

- **`TagRulesDialog`** gains a constructor taking a seed `TagRule`. It appends
  the seed to `m_working` after the normal load, selects it, and focuses the Add
  tags field. The existing constructor delegates to it with no seed.
- **`MainWindow::showTagRulesDialog()`** gains an optional seed parameter,
  passed through. Every existing caller is unchanged.
- **`MainWindow::addSavedQueryActions()`** gains the action, guarded on
  `!saved.isGenerated()`.

**Item 78 folds into this.** "Create rule from sender" is the same seeded-dialog
path with a different seed, so it becomes a second caller rather than a second
mechanism. This is the reason the seed is a whole `TagRule` rather than a query
string: a sender rule will want to set tags too.

## Testing

In `test_tagrules`:

- A seeded dialog carries the query and a sanitised id, and the rule is in the
  working list and **not** on disk.
- Save writes it. Cancel does not, asserted by rereading the file.
- A seed whose name collides with an existing rule id gets a suffix rather than
  replacing that rule. This is the case `uniqueId` exists for, and it is
  reachable here in a way it is not from the rules dialog alone, since the name
  comes from a different file.
- A seeded rule saved with no tags is refused, and the banner names it.

In `test_mainwindow`:

- The menu carries `Create tagging rule...` for a stored query and does not for
  a generated one. The generated half needs a guard proving the menu was built
  at all: item 82 records that a test asserting only the ABSENCE of a widget
  passes against no implementation whatever.

## Out of scope

**Backfill.** Applying the new rule to the mail the user is looking at is the
obvious next question and is deliberately unbuilt, here as everywhere else. See
`2026-08-12-tagging-rules-design.md` for what it needs first, including the
revision it forces to the no-confirmation rule in CLAUDE.md.

**Editing the saved query from the rules dialog.** The two objects are
independent once the rule exists. A rule that remembers where it came from would
raise the question of what happens when the query is later edited, and neither
answer is obviously right.
