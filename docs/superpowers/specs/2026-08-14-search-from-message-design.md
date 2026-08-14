# Searching from what is on screen

Resolves the first half of backlog item **78**. Depends on nothing unshipped.

## What this is

Five surfaces in the message pane become right-clickable. Each offers two
entries: **Search for this**, which replaces the query bar, and **Add to
search**, which narrows what is already there. The value under the cursor
becomes a notmuch query and runs.

Nothing here writes a rule.

## What this is not, and why

Item 78 as filed asks for a **tagging rule** built from something visible in a
message. That is deliberately not this. The user's reasoning, taken as the
decision: a saved query can already be promoted to a rule (item 81), so the
road from "I see something interesting" to "a rule tags it" already exists as
search, save the query, build the rule from it. Three steps, each reversible,
each showing its result before the next.

Searching is the step that is missing, and it is the safe one: a query costs
nothing if it is wrong, while a rule is handed to a `post-new` hook that runs
unattended against real mail every ten minutes. Building the shortcut before
the road works would put the dangerous end first.

**Item 78 therefore stays open** after this ships, carrying the rule shortcut
alone. Revisit it once the search actions have been used enough to know which
values are worth promoting directly.

## The surfaces

| Surface | Widget | Query built | Offered when |
|---|---|---|---|
| Subject | header `QLabel` | `subject:"..."` | always |
| Date | header `QLabel` | `date:YYYY-MM-DD..YYYY-MM-DD` | always |
| From, To, Cc | header `QLabel` | `from:` / `to:` / `cc:` | single-message thread |
| Tag chip | `TagStrip` | `tag:x` | per chip |
| Body selection | `QWebEngineView` | `"..."` | selection non-empty |
| Every header, per message | details dialog | all of the above | always |

**From, To and Cc are offered exactly when the header displays them**, which is
when the thread holds one message. The header already adapts this way
(`messageview.cpp:470-489`) and the reason is recorded there: for a real thread
the recipient differs message to message and neither the union nor the
intersection is "the" recipient. The menu must not offer a value the header is
not stating, so it shares the condition rather than restating it.

For a multi-message thread those three live in the details dialog, where they
are per-message and unambiguous. That is the fallback, not the primary route: a
single-message thread is the common case and reaching its sender should take one
click, not a trip through a dialog.

**The `+N` overflow chip offers nothing.** It stands for a list, not a tag.

**The header menu lists the fields; it does not hit-test them.** The header is
one rich-text `QLabel` holding up to four rendered lines, and working out which
line the cursor is on means mapping a point through laid-out rich text, which is
fiddly and breaks as soon as the label wraps. A right-click anywhere on the
header instead offers every field available for that message, each entry naming
its value:

```
Search for subject "Invoice 4471"          >  Search for this
Search for from foo@example.org            >  Add to search
Search for mail from 2026-08-14
```

The values are kept beside the label as a small list, populated by the same pass
that renders it, rather than parsed back out of the markup. Building the display
string and the searchable value in one place is what stops them disagreeing.

Listing is also the better affordance: the user sees which fields are searchable
without discovering it by clicking in the right spot. A long value is elided in
the entry's text; the query uses the full one.

The tag strip is hit-tested rather than listed, because a chip is a discrete
widget-like thing with its own rect and the user is aiming at one.

## Two operations

**Search for this** sets the query bar to the term and runs it.

**Add to search** combines with what the bar already holds:

```
(existing) AND (new)
```

**Both sides are parenthesised, and this is load-bearing.** The bar may hold a
hand-written query containing `or`, and `a or b AND c` binds as `a or (b AND c)`,
which silently widens the search instead of narrowing it. This is the same trap
the `post-new` hook already handles when it scopes a rule with `tag:new`, and it
is recorded in CLAUDE.md for that reason.

When the bar is empty, **Add to search** behaves as **Search for this** rather
than producing `() AND (new)`.

The account scope is not touched. `MainWindow::runQuery()` wraps the bar's text
in the selected account's scope on every run (`mainwindow.cpp:1981-1983`), so a
scope applied here would be applied twice.

## Quoting

One helper, `searchTermFor(const QString &)`, used by every surface:

- trim, and collapse runs of whitespace and newlines to single spaces
- escape embedded `"`
- wrap in double quotes
- cap the length; a multi-kilobyte selection is a mis-drag, not a query
- an empty or whitespace-only result yields no menu entry at all

**This gets its own tests, and they matter more than they look.** notmuch's
parser rejects almost nothing: CLAUDE.md records that `from:((((` parses cleanly
and matches zero. A mis-quoted query therefore does not error, it silently
returns nothing, and the feature looks broken with no clue why. Body selections
are arbitrary user-chosen prose and can contain quotes, colons, parentheses,
`AND`, newlines, or a pasted log.

Assert on the constructed string, never on a provoked parser failure.

## Wiring

`MessageView` already has the right signal:

```cpp
void queryRequested(const QString &query);
```

It exists for the placeholder pane's helper lines and carries a documented
security gate: a message body is attacker-controlled HTML and can hold a
`qtmaildir-query:` link, so the signal is emitted only when the placeholder is
what is displayed (`messageview.h:140-149`).

**Reuse it rather than adding a parallel signal.** The menus are application
chrome built by our own code from values we extracted, not links in a rendered
document, so they do not weaken that gate. The gate stays exactly as it is for
links; these are a separate emitter of the same signal.

`TagStrip` and the details dialog emit their own equivalent. Every one of them
carries a finished query string and nothing else; the panes never touch the
query bar. `MainWindow` receives, sets `m_queryEdit`, and calls the existing
runner, which keeps the account scope and the generation counter working.

**Extend needs the bar's current text**, which only `MainWindow` has. The signal
therefore carries the term plus which operation was chosen, and `MainWindow`
does the combining. A pane that built `(existing) AND (new)` itself would have
to read the query bar, which is exactly the coupling the existing signal's
comment says to avoid.

## The details dialog is rebuilt as rows

Today it is one `QPlainTextEdit` holding every message's headers as flat text,
built inline in a 40-line `showDetailsDialog()` and shown with `exec()`
(`messageview.cpp:500-552`). The user does not like that it appears as a text
box, and a flat text box cannot carry a per-value context menu without parsing
displayed text back into structure.

It becomes a scrollable column of labelled rows, one row per header per message,
each row holding its own value and its message index. The context menu then
carries the real value with no parsing.

**One property of the current dialog must survive.** It uses `setPlainText` and
a `QPlainTextEdit` deliberately, and the comment says why: header values come
from strangers, and plain text cannot interpret markup, so there is nothing to
escape and nothing that could render. Row widgets reintroduce that risk, because
a `QLabel` interprets rich text by default and Qt will guess when the format is
`Qt::AutoText`.

Every value label is therefore **explicitly `Qt::PlainText`**. Not escaped
input into a rich-text label, which is the same protection one mistake away from
failing.

The dialog moves out of `MessageView` into its own `MessageDetailsDialog` class.
A 40-line inline builder that now grows rows, menus and signals is past what
belongs inline, and the class is separately testable.

## Testing

- `searchTermFor` against quotes, newlines, colons, parentheses, `AND`, empty
  and whitespace-only input, and an over-long selection.
- Term construction per surface: a tag chip yields `tag:x`, a date yields the
  day's range, a subject is quoted.
- **Add to search** parenthesises both sides, and an `or` in the existing query
  survives it. Empty bar falls back to replace.
- `TagStrip::chipAt` against the painted rects, including a miss in the gap
  between two chips and the `+N` chip yielding nothing. The hit test and the
  paint take their geometry from one function so they cannot drift, as
  `CardDelegate::expanderRectFor` already does for the expander.
- Header menu contents: From/To/Cc entries present for a single-message thread,
  absent for a multi-message one, with subject and date present in both. **The
  absence half needs a guard proving the menu was built at all**; item 82 records
  that a test asserting only the absence of a widget passes against no
  implementation whatever. Asserting that subject and date are still there is
  that guard.
- An empty header field yields no entry. A message with no Cc must not offer
  `cc:""`, which parses cleanly and matches nothing.
- Details dialog rows carry the right value and message index, and value labels
  report `Qt::PlainText`.
- One `test_mainwindow` case that a triggered action leaves the expected text in
  the query bar.

Three traps from CLAUDE.md apply and are not re-derived here: a rendering probe
proves nothing about which widget is clickable, visible and clickable and working
are three separate properties, and a hit test asserted against `visualRect` can
endorse a broken layout.

## Out of scope

**Creating a rule directly from a value.** The remainder of item 78, above.

**Searching from the thread list.** Its context menu is tag actions today and
the row's `authors` is a display summary from
`notmuch_thread_get_authors`, not an address: it reads `Alice, Bob` or
`Alice| Bob`, so `from:` built from it matches nothing. Any thread-list search
needs a real address resolved from a message first. Worth recording because the
backlog's own approach line for item 78 assumed the row held a usable sender.

**Refining by anything not on screen.** No date-range picker, no "from anyone at
this domain". Both are reasonable and neither is this.

**The web view's standard context menu entries.** Copy and the rest stay as
they are; the search entry is added alongside.
