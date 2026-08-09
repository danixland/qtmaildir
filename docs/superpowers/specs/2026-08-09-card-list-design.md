# Card list: the thread pane without a column grid

**Status:** specified 2026-08-09, not implemented.
**Supersedes:** the presentation half of item 20, built on `item-20-message-rows`
and rejected. Resolves backlog item 53. Retires item 51 as a side effect.

## Why

Item 20 shipped message rows exactly as its four decisions specified, and the
user's verdict on the finished result was that the table view does not fit the
use. Item 53 recorded the cause verified in code: a message row fills the same
five columns as a thread row, so replies land on the same rigid column
boundaries as the threads around them, and the eye reads columns before it reads
indentation or tint.

The user's decision on 2026-08-09 is that the grid is wrong for the **whole**
left pane, not only for reply rows. Threads and replies both become cards.

This is a presentation change. The model's data, the reply tree from notmuch,
the action scope, undo, and the worker are all kept.

## The card

One column. Every row is a card of exactly three lines, thread and reply alike.

```
  sender ............................................ date
  ★ subject                              @    ▾ 3 replies
  [unread] [work]
```

- **Line 1:** sender, and the date flush right.
- **Line 2:** the flag mark, the subject, the attachment mark, and the reply
  count. Flag and attachment become inline marks on this line rather than
  columns of their own; `attachmentGlyph()` and `flagGlyph()` already answer
  what character to draw and keep their font-fallback behaviour.
- **Line 3:** the thread's pill tags, from the existing `PillTagsRole` and
  `PillColoursRole`.

A **reply card** is the same three lines, indented, dimmed with `readColour()`,
tinted with `replyBackground()`, with the `Re: ` prefix stripped from line 2 and
no reply count. Its line 3 is specified below.

**Every card is the same height**, including replies and including cards whose
line 3 is empty. This is the single cheapest property of the design:
`setUniformRowHeights(true)` stays, the delegate's `sizeHint` is one constant
computed from the font metrics, and no scrolling or hit-testing arithmetic has
to account for rows of differing size. The cost is a blank band under untagged
cards, which the user accepted explicitly.

## Reply line 3: only what the thread does not already say

A reply card's line 3 carries **the tags that message has and its thread does
not**. A reply tagged `todo` inside an untagged thread shows `[todo]`. A reply
carrying only the thread's own tags shows nothing.

The rule exists so a tag applied to one message stays findable inside its
thread, without the thread's tags repeating identically down the whole
expansion. That repetition is the striping `ThreadListView`'s own header
comments give as the reason the current strip is painted on thread rows only.

**Measured against the user's database, 2026-08-09**, because the alternative
(showing a reply's full tag set) was rejected on this evidence rather than on
taste: of 48691 messages, 7 carry `unread` and 75 carry `flagged`. Those are the
only two tags that vary within a thread in practice; the rest (`account-*`,
`lists`, and so on) are applied to whole threads and are identical on every
message in them. Both varying tags are already visible another way, `unread` as
the sender's weight and `flagged` as the mark on line 2. So a full per-message
tag set would render blank on essentially every reply and identical chips on the
rest, which is cost without payoff. The set difference degrades to blank in the
same places and lights up exactly where the user put a tag deliberately.

Computed in the model from data it already holds: `MessageNode::tags` against
the parent `ThreadSummary::tags`. **No worker change.**

## The spine

Replies are indented by depth with a **continuous vertical line per depth
level**, drawn the full height of each reply card, in `threadLineColour()`.

No elbows, no horizontal tick into the card, and no different glyph on the last
child. The alternatives were shown and this one chosen: elbows would require the
delegate to know whether a row is its parent's last child, and box-drawing
characters (`├─`, `└─`) depend on the UI font carrying glyphs a proportional
font often lacks or spaces badly, and do not scale with the row when the font
size changes.

**Indent caps at depth 4.** A reply at depth 5 or deeper renders at depth 4's
indent, spines included, with **no marker** saying it was flattened. Item 20
already accepted that deep chains must be capped in the view rather than
flattened in the model, and the cap belongs to the delegate.

**No horizontal scrolling.** Uncapped indent with a horizontally scrollable pane
was considered and rejected: it reopens item 51 in a worse form. Today's
sideways scroll on click happens because the Subject column is wider than the
viewport, and `QAbstractItemView`'s auto-scroll brings the clicked index into
view. With cards there are no columns and a card is exactly viewport width, so
the horizontal scrollbar disappears and **item 51 is resolved for free**.
Restoring an over-wide row would put it back, and this time clicking any deep
reply would scroll the pane sideways. The card's right-aligned date is a second
casualty: it either scrolls out of sight or stops being right-aligned.

## Expanding

**The reply count on line 2 is the expander.** Clicking it toggles the thread;
clicking anywhere else on the card selects it and opens the message.

No separate chevron in a left gutter. That would cost horizontal space on every
card including the ones with no replies, and the branch already hit the trap
that `setRootIsDecorated(false)`, needed to stop the style drawing its own
indicator, also removes the style's **hit area**, leaving a glyph that renders
and does nothing.

Two consequences that must be honoured, both learned on the branch:

- The delegate draws the expander, so the **view** must own the click, since a
  delegate gets no click of its own without an editor. `ThreadListView` keeps
  `mousePressEvent` for exactly this.
- `isExpanded` and `setExpanded` are keyed on **column 0**, which is now the
  only column.

## Sorting

A **sort dropdown** in the query row, two entries: newest first (the default)
and oldest first. Passed to `notmuch_query_set_sort`, which today is hardcoded
to `NOTMUCH_SORT_NEWEST_FIRST` at `notmuchworker.cpp:135`.

**This adds a feature rather than replacing one.** The current column header is
decorative: nothing in the codebase implements click-to-sort, so removing the
header loses nothing.

Two entries and not four. notmuch offers `NOTMUCH_SORT_MESSAGE_ID` and
`NOTMUCH_SORT_UNSORTED` as well, and neither is a sort order a human wants.
Sorting by sender or subject was declined: notmuch cannot do it, so the model
would have to sort after results arrive, which fights the 200-at-a-time batching
that makes a 10k-thread query paint immediately.

The chosen order persists in `~/.local/state/qtmaildir/uistate.conf` via
`MainWindow::uiStatePath()`, never in the hand-edited config.

Changing the sort re-runs the current query, so it bumps the generation counter
like any other query.

## What is deleted

A net removal of code:

- `ThreadListView::paintEvent` and the tag strip's band arithmetic.
- `SubjectDelegate` and `RowStyleDelegate`.
- The five `Column` enumerators (`AttachmentColumn`, `FlagColumn`, `DateColumn`,
  `AuthorsColumn`, `SubjectColumn`), collapsing `ColumnCount` to 1.
- `headerData` and the view's header.
- The `HasRepliesRole` reservation logic that told the subject cell to leave
  room for a glyph, now the delegate's own layout.

**`ThreadListView` survives, with a much smaller job.** It keeps
`mousePressEvent` for the expander hit-test. It no longer paints anything.

This retires two bug classes `CLAUDE.md` documents for the strip: a deleted row
cut in half, and every other row showing a bare stripe, both caused by the view
having to re-honour alternating colours, selection and `BackgroundRole` itself
because the strip spanned cells it did not own. With one column and one delegate
painting the whole card, neither is reachable.

## What is kept

- Every model role on the branch: `ThreadIdRole`, `AccountLabelRole`,
  `AccountColourRole`, `TagsRole`, `PillTagsRole`, `PillColoursRole`,
  `IsMessageRole`, `MessageIdRole`, `MessageDepthRole`, `HasRepliesRole`.
- `QAbstractItemModel` with the reply tree, lazy child loading, and
  `hasChildren` answered from `totalCount` rather than from loaded children.
- Action scope by row kind, and the status-bar scope naming before and after an
  action. No confirmation dialogs, per the standing rule.
- Undo through `TagChange::inverted()`.
- The account chip, the `deletedColour()` / `spamColour()` row fills, and the
  unread/read weight and colour cues.
- `setUniformRowHeights(true)`.

## New

- **`MessageOwnTagsRole`** and **`MessageOwnColoursRole`**, the set difference
  described above and its chip colours in the same order. Model-side only, and
  mirroring the existing `PillTagsRole` / `PillColoursRole` pair so the colours
  keep coming from the model's `TagColors` rather than from a delegate reading
  config as a second source of truth.
- **`CardDelegate`**, replacing `SubjectDelegate` and `RowStyleDelegate`. It
  paints the whole card and owns every measurement: the three line baselines,
  the indent per depth with its cap, the spine rects, the expander's rect, and
  the chip run on line 3.
- The sort dropdown and its `uistate.conf` key.

## Testing

Per the rendering-probe warnings in `CLAUDE.md`, which were written after a
whole session was lost to probes that lied:

- **Assert on the delegate's computed geometry, not on pixels.** `CardDelegate`
  exposes its layout (line rects, indent width, expander rect, spine rects) as
  a testable function of a row and a width. Those are the assertions.
  `sizeHint` is asserted to be constant across thread rows, reply rows, tagged
  and untagged.
- **Never count lit pixels.** It cannot tell bold from regular in either
  direction. Where a rendered check is genuinely needed, use text width or a
  strict pixel diff.
- **Every rendering test carries a mutation check** and a guard proving it can
  fail: assert the geometry it depends on rather than assuming it.
- The set-difference rule gets a plain model test: a reply tagged with one tag
  its thread lacks reports that tag alone; a reply carrying only thread tags
  reports nothing.
- The indent cap gets a test at depths 3, 4, 5 and 9, asserting depth 5 and 9
  compute the same indent as 4.
- The expander hit-test gets a test that a click on the reply count toggles and
  a click elsewhere on the card does not, since being visible and being
  clickable are separate properties here.
- **Item 51 gets a regression test**: with cards, the view reports no horizontal
  scroll range, and clicking a card does not change `horizontalScrollBar()`'s
  value.

Two constraints on writing these, from `CLAUDE.md`: nothing may be keyed on a
row **number**, because a tree numbers rows per parent; and the offscreen
platform chooses the window width itself and has been seen to choose
differently between runs, so a test must not depend on a particular width.

## Open, deliberately not decided here

- **Moving between messages in a thread without returning to the list.** Named
  by the user during item 20 and deferred there. Still deferred.
- Whether the message pane's own presentation should change to match. Out of
  scope: this spec is the left pane only.
