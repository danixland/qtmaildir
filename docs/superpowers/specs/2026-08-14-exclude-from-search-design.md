# Excluding a value from a search

Resolves backlog item **86**. Follows item 85, which shipped the two operations
this adds a third to. Depends on nothing unshipped.

## What this is

Every right-click search menu gains a third entry, **Exclude from search**,
which narrows the current query by everything that is *not* the value under the
cursor. The two shipped entries keep their labels and their behaviour.

The user's words, from the notes on 2026-08-14: "the current available search
options are to run the query from scratch or add to the existing query. A
missing option is to add negatively (not)."

## Why it is not a one-line menu addition

Item 85 shipped exactly two operations and encoded the choice between them as a
single `bool extend`, carried end to end. `MessageView::addSearchEntries`
(`src/messageview.cpp:566`) builds the pair per offer,
`MessageDetailsDialog` (`src/messagedetailsdialog.cpp:88`) repeats it per row,
and `MainWindow::runSearchFromPane` (`src/mainwindow.cpp:1654`) branches on it:

```cpp
extend ? SearchTerm::extend(m_queryEdit->text(), query) : query;
```

`SearchTerm` has no exclusion form at all. So there is no third state for a new
menu entry to select, and the signature cannot express one. The work is a
widening of that signature plus one function, not a menu edit.

## The grammar

One function in `src/searchterm.h`, beside `extend()`:

```cpp
QString exclude(const QString &existing, const QString &addition);
```

It returns `(existing) AND NOT (addition)`.

**Both sides are parenthesised, and that is load-bearing rather than tidy.**
The query bar may hold a hand-written disjunction. Unparenthesised,
`a or b AND NOT c` binds as `a or (b AND NOT c)`: the exclusion covers only the
second term, and every message matching `a` stays on screen including the ones
the user asked to be rid of. Nothing reports an error. This is the same trap
`extend()` documents and the same one the `post-new` hook handles when it scopes
a rule with `tag:new`.

### The empty cases, and where they differ from `extend()`

| `existing` | `addition` | result |
|---|---|---|
| non-empty | non-empty | `(existing) AND NOT (addition)` |
| non-empty | empty | `existing`, untouched |
| empty | non-empty | **empty string** |
| empty | empty | empty string |

The third row is the decision. `extend()` returns the addition alone when
`existing` is empty, because narrowing nothing by `x` sensibly means `x`.
Excluding from nothing would mean `NOT (addition)`: the entire Maildir minus
one sender. That is a legitimate query and an implausible thing to have meant by
right-clicking a value in a fresh window, so the grammar refuses to build it.

The UI never asks for it either (see the guard below). Two layers, deliberately:
the guard is what the user sees, and the empty return is what stops a future
caller that forgets the guard from producing a whole-Maildir scan.

## The signal

`bool extend` cannot carry three states. It becomes an enum in
`src/searchterm.h`, beside the functions it selects between:

```cpp
enum class SearchMode { Replace, Narrow, Exclude };
```

It crosses **no thread boundary**, so the `Q_ENUM` metatype trap recorded in
`CLAUDE.md` does not apply here. Every one of these connections is direct; that
trap belongs to the queued signals into `NotmuchWorker`.

Four signatures change:

| where | from | to |
|---|---|---|
| `MessageView::searchRequested` | `(QString, bool)` | `(QString, SearchMode)` |
| `MessageDetailsDialog::searchRequested` | `(QString, bool)` | `(QString, SearchMode)` |
| `MessageDetailsDialog::requestSearch` | `(HeaderRow, bool)` | `(HeaderRow, SearchMode)` |
| `MainWindow::runSearchFromPane` | `(QString, bool)` | `(QString, SearchMode)` |

`runSearchFromPane` becomes a three-way switch over the mode. Its existing
comment stays true and stays put: the result goes through the query bar and
`runCurrentQuery()`, so the account scope, the generation counter and the
flat-mode reset behave exactly as they do for a typed query. Nothing here builds
a second query path.

## The guard

The menus cannot currently tell whether the query bar is empty. `MessageView`
and `MessageDetailsDialog` both emit outward and never read the query, and the
dialog is a child of the view rather than of the window.

`MainWindow` pushes the fact down:

```cpp
void MessageView::setHasQuery(bool hasQuery);
```

`MainWindow` already hangs a lambda on `m_queryEdit`'s `textChanged` at
`src/mainwindow.cpp:373`, for the Save button's enabled state. That lambda gains
one line. `MessageView` stores the bool and passes it to
`MessageDetailsDialog` at construction, since the dialog is built fresh per
invocation and cannot go stale.

Both menu builders then disable the Exclude action when the bool is false. The
entry stays **visible and greyed**, not hidden: a user exploring a fresh window
is exactly the one who should be able to see that the feature exists.

Rejected alternatives, recorded so they are not revisited. A `std::function`
callback into `MainWindow` is always fresh but adds an indirection with one
implementation and a dangling-callback risk. Leaving the entry always enabled
and having `MainWindow` silently do nothing is fewer lines and worse: a live
menu entry that does nothing is a worse failure than a greyed one.

## The menus

Three entries, in this order, in both surfaces:

```
Search for this
Add to search
Exclude from search
```

The two shipped labels are untouched, so nothing the user already knows moves.
All three are wrapped in `tr()`.

Both call sites: `MessageView::addSearchEntries` (`src/messageview.cpp:566`),
which builds a submenu per offer, and the per-row lambda in
`MessageDetailsDialog` (`src/messagedetailsdialog.cpp:88`).

## Testing

**`test_searchterm`** carries the grammar, and needs no widget, no painter and
no web engine. Cover every row of the empty-case table, and cover the
disjunction: `exclude("tag:inbox or tag:flagged", "from:\"x\"")` must
parenthesise the left side, which is the assertion that fails if someone
"simplifies" the parentheses away.

**Assert on the constructed string, never on a result count.** notmuch rejects
almost nothing: `from:((((` parses cleanly and matches zero, so a malformed
query produces an empty result rather than an error. A test asserting a failure
or a `-1` count passes against correct code and against broken code alike. This
is recorded in `searchterm.h` and in `test_notmuchworker.cpp`, and has been
learned twice already.

**`test_messagedetailsdialog`** covers the guard: the Exclude action is disabled
when the dialog is constructed with no query and enabled when it is constructed
with one.

**The dialog inherits item 85's ordering trap.** A modal's signal to its parent
is a DIRECT connection, so the emit runs the handler synchronously while
`exec()` is still on the stack. `accept()` must run before the emit, or the
search clears the model and blanks the pane behind a dialog the user still has
to dismiss, while it holds the `m_items` it was built from. The new action is on
the same path and has the same requirement.

**Its mutation check hangs rather than fails.** Without the `accept()`, nothing
ever leaves `exec()`. A hung `test_messagedetailsdialog` binary also leaves a
stale binary that a later `ctest` will re-run, which is item 84's second trap:
the failure appears to persist after being fixed. Kill the binary and rebuild
before concluding anything about a hang here.

## Out of scope

The thread list's context menu. Item 85 left it alone deliberately and that
holds: `ThreadSummary::authors` comes from `notmuch_thread_get_authors` and is a
display summary reading like "Alice, Bob", so a `from:` query built from it
matches nothing. Item 78 carries what is left of that.

## Size

**S.** The grammar is one function beside an existing one with an existing test
binary. The spread is the signature change across three files.
