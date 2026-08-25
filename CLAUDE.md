# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run a single test binary directly for a tighter loop, **always with the
offscreen platform**:

```bash
QT_QPA_PLATFORM=offscreen ./build/tests/test_keymap
```

Or by ctest name (the name is the suffix, not the binary): `ctest --test-dir build -R keymap`.

**Never run a test binary without `QT_QPA_PLATFORM=offscreen`, and never launch
`./build/src/qtmaildir` unasked.** `tests/CMakeLists.txt` sets that variable for
ctest only, so a binary invoked directly inherits the desktop's own setting
(`wayland;xcb` here) and throws real windows onto the user's screen. Each test
function builds its own `MainWindow`, so one direct run of `test_mainwindow`
flashes over a hundred windows across the desktop. This is not cosmetic: the
user has asked for it to stop, having been given a headache by it.

The same applies to the application. Running it is a hand test and belongs to
the user; ask rather than launching it, and when a change genuinely needs
looking at, say what to look for and let them run it.

Adding a test: create `tests/test_<name>.cpp` and add `add_qtmaildir_test(<name>)` to
`tests/CMakeLists.txt`. That function links `qtmaildir_lib` and `Qt6::Test` and registers
the test. Fixture-driven tests get `FIXTURE_DIR` via `target_compile_definitions` (see
`test_mimeparser`).

Adding a source file: add the `.cpp` to the `qtmaildir_lib` list in `src/CMakeLists.txt`.
All logic lives in that static library; the `qtmaildir` executable is only `main.cpp`, so
tests can link everything without duplicating source lists.

## Environment constraints (verified 2026-08-02, Slackware)

- **notmuch installs no `notmuch.pc`.** CMake locates it with `find_path`/`find_library`.
  Never convert it to `pkg_check_modules`. GMime does ship `gmime-3.0.pc` and uses pkg-config.
- Qt 6.11.1 including WebEngine ships in Slackware's monolithic `qt6` package; there is no
  separate `qt6-webengine`.
- CMake 4.3.4 rejects `cmake_minimum_required(VERSION <3.5)`. Keep 3.21.
- **gmime headers must be included before any Qt header in the same translation unit.**
  glib declares a struct field named `signals`, which Qt defines as a macro.

## Architecture

One process, two threads. A GUI counterpart to neomutt over a local notmuch-indexed
Maildir. **No network protocol work at all** — fetching and sending are external scripts.

```
UI thread                          Worker thread
MainWindow                         NotmuchWorker
 ├ query row: QComboBox, QLineEdit,  └ owns the only notmuch_database_t*
 │   saved-query QPushButtons
 ├ ThreadListView (QTreeView) ── ThreadListModel (QAbstractItemModel)
 │   ONE column of cards; CardDelegate paints each whole, from CardLayout
 └ MessageView (header QLabel, QWebEngineView, attachment bar, TagStrip)

ComposeWindow (its own top-level window, one per message being written)
 ├ MarkdownFormat (namespace: what the formatting buttons do to a selection)
 ├ MarkdownRenderer (namespace, cmark-gfm)   MessageBuilder (namespace, GMime)
 ├ MessageSender (QProcess, the per-account send_command on stdin)
 └ SendDialog (the undo countdown)   DraftStore (autosave to the drafts folder)

ComposeContext (a struct: what a Reply or Forward inherits)
ComposeContextBuilder (namespace: fills one, and picks the account)
CardLayout (pure geometry, no painting)
SearchTerm (pure query strings, no widget)
Config (INI)   KeyMap   MailSync (QProcess)   MimeParser (GMime)
SyncMonitor (/proc/locks)   TagColors   QueryCompleter   ThreadCidMap
MaildirName (fresh Maildir filenames)
```

The query row and the message-pane header are **built inline in `MainWindow` and
`MessageView`**, not as named widget classes. Earlier revisions of this diagram
listed `QueryBar`, `SavedQueryBar`, `HeaderWidget` and `AttachmentBar`; none of
those types have ever existed, and looking for them wastes a search. The widget
classes that do exist are `MessageView`, `ThreadListView`, `TagStrip`,
`TagDialog`, `MessageDetailsDialog`, `RowStyleDelegate`, `CardDelegate`,
`ComposeWindow`, `SendDialog` and `BusyIndicator`; `TagChip` is a namespace of
painting helpers, not a widget, `SearchTerm` is a namespace of query builders,
and `ThreadCidMap`, `CardLayout`, `SearchOffer` and `HeaderRow` are structs.
`SubjectDelegate` existed until item 53 and is gone.

**The compose units are mostly NAMESPACES, and the same warning applies to
them.** `MarkdownRenderer`, `MarkdownFormat`, `MessageBuilder`,
`ComposeContextBuilder`, `DraftStore` and `MaildirName` are namespaces of free
functions over values, deliberately, so the markdown, the MIME assembly and
the account-picking are all testable without a widget. `MessageSender` IS a
QObject, because it owns a `QProcess`. There is no `FormatToolbar` class: the
composer's formatting row is built inline in `ComposeWindow` and asks
`MarkdownFormat` what each button does to the selection.

**`MessageDetailsDialog` was a `QPlainTextEdit` inside `MessageView` until item
85.** It is rows now so each value can carry its own context menu, and its
plain-textness was a SECURITY property rather than a style: header values come
from strangers and plain text cannot interpret markup. Every value label states
`Qt::PlainText` explicitly, because a `QLabel` guesses under `Qt::AutoText`.
Escaping into a rich-text label is the same protection one mistake away from
failing, so do not "simplify" it back.

**`ThreadListView` survives only for the expander hit-test.** `CardDelegate`
draws the reply count, and a delegate gets no click of its own without an
editor, so the view owns the click and asks the delegate for the rect rather
than recomputing it.

Until item 53 it also painted a row-wide strip of tag chips after the cells,
because a delegate cannot paint outside its column and the strip spanned all
five. That is why the class exists at all, and the history is worth keeping:
the arithmetic it needed produced a deleted row cut in half and every other row
showing a bare stripe, both because the view had to re-honour alternating
colours, the selection and `BackgroundRole` across cells it did not own. With
one column there is nothing to span, so the `paintEvent` and its band
arithmetic are deleted and none of that applies any more.

**The panes' marks are shipped SVGs, not font glyphs and not a `.qrc`.** `Marks`
(`src/marks.h`) carries six payloads as compiled-in string literals, generated
from `assets/icons/marks/*.svg`, which stay the editable originals. Not a
resource, because `src/CMakeLists.txt` already records that a qrc in the static
library registers itself from a global initialiser the linker drops, and the
tests link the library rather than the executable. Every payload paints with
`fill="currentColor"`, which `QSvgRenderer` renders BLACK rather than resolving;
`Marks::pixmap` composites the real colour with `CompositionMode_SourceIn`, which
is what lets one asset serve a light and a dark palette. The toolbar and menus
still use `QIcon::fromTheme` and must keep doing so: the split between "panes are
ours, chrome is the system's" is item 70's whole point. A tag drawn as a mark
must not also appear as a chip, which `isDrawnAsAMark()` in `threadlistmodel.cpp`
enforces for both roles at once; the duplicate survived every geometry test and
was found only by rendering a card and looking at it.

**A card layout must be testable without a painter.** `CardLayout` computes
every rect on a card and touches no `QPainter` and no widget, so the geometry
has tests that a blank render cannot defeat. When changing what a card shows,
change `CardLayout` and assert there; a test that renders the delegate and
counts pixels proves nothing, for the reasons under "Rendering probes lie".
Two traps it already handles: `QRect::right()` is inclusive, so the right edge
is carried as an exclusive one, and `QFont::pointSizeF()` returns -1 for a font
set in pixels, which qt6ct does.

**`QTreeView`'s Up/Down already walk into an expanded thread's replies**, and
that is where message-to-message navigation comes from. Do not bind arrow keys
as `QAction` shortcuts to get it: a shortcut is dispatched before the focused
widget sees the key and Qt withholds only plain LETTERS from editable widgets,
so a bare `Up` would break the query bar, the tag dialog and the web view at
once. `Alt+Up`/`Alt+Down` are chords and therefore safe; `Shift+Up`/`Down` is
the built-in extend-selection and must be left alone. Binding two sequences to
one action needs `setShortcuts`, not `setShortcut`, which keeps only the last.

**`Q_ENUM` is not enough to send an enum across a queued connection.** It gives
the type a meta-object entry, not a metatype registered under the name
`invokeMethod` resolves, so a `Q_ARG` carrying it is dropped at runtime with a
warning and the slot runs with a default. `NotmuchWorker::SortOrder` is
registered beside the type for this reason, not in `MainWindow`, so a caller
that never constructs one still gets it.

**It is a `QTreeView` over a `QAbstractItemModel` since item 20**, because a
thread's replies are child rows and a table can neither indent nor expand. What
did NOT survive that port is anything keyed on a row NUMBER: a tree numbers rows
per parent, so `row 0` exists once per expanded thread and `current.row() + 1`
names a sibling rather than the next thread. Navigation walks with
`indexBelow`/`indexAbove`; `QTableView::isRowSelected(int)` has no equivalent —
use `selectionModel()->isSelected(index)`. Row height comes from
`setUniformRowHeights` plus `CardDelegate::sizeHint`, since a tree has no
vertical header to carry a default section size. Indentation is
`setIndentation(0)`: `CardLayout` draws the indent inside the card's own rect,
so `visualRect` reports the SAME left edge for a thread and its reply and a
geometry probe sees no nesting in a correctly nested list.

**Three traps in the expander, all of which shipped a plausible-looking broken
build before being caught.** `QTreeView::drawBranches` is the documented hook and
does not work when the expander sits on a content column: it runs BEFORE the
row's cells, so the delegate's background paints over it (a 60-pixel triangle
survived as 8). `CardDelegate` draws it instead, as the reply count on the
card's second line. `setRootIsDecorated(false)`, needed to stop the style
drawing its own indicator underneath, also removes the style's HIT AREA, so the
glyph renders perfectly and is inert; `ThreadListView::mousePressEvent` handles
the click, asking `CardDelegate::expanderRectFor` for the target so the drawn
and clickable rects cannot drift. A fourth trap died with the grid: `isExpanded`
is keyed on column 0, which used to disagree with the subject-column index.

**Visible, clickable and toggling are three separate properties.** A test for
one passes against the other two being broken, which happened twice in one
session: a pixel test proved the triangle was drawn while nothing could click
it, and a click test proved it opened while it could never close.

**Assert a reply's indent on where the TEXT lands, never on `visualRect`.** The
reason has inverted twice and the rule has not. Under item 20 the geometry was
indented while the text was not, because the delegate laid text out from its own
left edge; now `setIndentation(0)` means `visualRect` reports no indent at all
while the text is indented, because `CardLayout` draws it inside the card's rect.
A probe on `visualRect` therefore endorsed a broken layout then and would fail a
correct one now. Assert on `CardLayout::contentLeft`.

**`paintEvent` ran AFTER the cells**, which is why anything the view filled
across a row covered the text the delegate had just drawn: the reply tint filled
the full row height in one version and erased every sender and subject, measured
at zero surviving text pixels. Recorded because it is the class of bug a view
that paints invites. `ThreadListView` no longer paints at all.

**No `notmuch_*` pointer ever crosses the thread boundary.** Data crosses as the plain
value structs in `src/types.h` (`ThreadSummary`, `MessageRef`, `MessageNode`,
`ActionScope`, `TagChange`), over queued
signals in both directions. `notmuchworker.cpp` is the only file that includes `notmuch.h`
outside `src/nmraii.h`; C handles are owned by the `NmQuery`/`NmMessages`/`NmThread`/…
RAII aliases there so they cannot leak.

**The one exception, and it is a double-free if undone.** Messages reached
through `notmuch_thread_get_toplevel_messages` / `notmuch_message_get_replies`
are owned by the THREAD and freed with it (`notmuch.h:1637`), so `walkReplies`
in `notmuchworker.cpp` holds them as raw `notmuch_message_t*`: an `NmMessage`
wrapper would call `notmuch_message_destroy` on memory the thread frees again.
The whole walk must finish while the `NmThread` is alive. Related: replies are
unreachable from a query walk at all — `notmuch_message_get_replies` returns
NULL for a message from `notmuch_query_search_messages` (`notmuch.h:1617-1628`),
which is why `loadThreadTree` exists beside `loadThread` rather than replacing
it.

**Generation counters, not cancellation.** Each query bumps a `quint64` generation passed
through to the worker and back on every result signal. The UI discards results whose
generation is stale. The worker never needs to know a query was superseded. Threads are
emitted in batches of `kBatchSize` (200) so a 10k-thread query paints immediately.

**Read-only by default, read-write in bursts.** notmuch's write lock is exclusive
process-wide, so holding it open would block the user's cron `notmuch new`. `applyTags`
closes the read-only handle, opens read-write, applies, closes. notmuch permits only one
open handle per process, so that close-first ordering is required, not stylistic.

**No dry-run, no destructive-action confirmation.** Those gates belonged to the retired
`mailctl` CLI, where they restrained an agent; a human at a GUI gets **undo** instead — every
mutation pushes its inverse (`TagChange::inverted()`) onto a `QUndoStack`. Do not add
confirmation dialogs for tag mutations.

**There is exactly ONE exception, and its shape is the rule's own logic rather
than a hole in it.** `empty_trash` (item 118) destroys files and index entries,
so it has no inverse to push, and the protection the rule actually provides —
that a user never loses work to a keystroke — has to come from somewhere else.
It therefore asks, naming the count and the account, defaulting to Cancel, and
it carries **no default shortcut** for the same reason. `NotmuchWorker::purgeMessages()`
is a separate entry point from `moveMessages()` deliberately: the two look
alike and only one of them can be undone. A second confirmation anywhere is a
defect unless the action is likewise irreversible. All actions funnel through one `applyTags` path;
multi-row selections go through `applyTagsToThreads`, which resolves every thread in ONE
combined `thread:a or thread:b` query rather than one query per thread.

**`notmuch_database_get_path()` is not the mail root, and assuming it is
moves mail somewhere mbsync cannot see.** notmuch can split the index from the
mail with `mail_root` and `path` as separate keys, which is how the Xapian
index goes on faster storage while the Maildir stays put. Under that layout
`get_path()` returns the INDEX directory. `mailRootOf()` in `notmuchworker.cpp`
wraps `notmuch_config_get(NOTMUCH_CONFIG_MAIL_ROOT)`, which is correct under
BOTH layouts, so there is no conditional and no reason to reach for the old
accessor again. Item 124, and the developer's own index has run split since
2026-08-20, so this is live rather than hypothetical.

The consequences were asymmetric, which is why it is worth remembering: a wrong
root made message paths resolve to `../..` escapes that match no account, a
display defect, but `moveMessages` composes its destination from the same root,
so Delete would have written into the Xapian tree. Two related traps sit outside
the code. `database.hook_dir` defaults to `<database.path>/.notmuch/hooks`, so a
split config silently stops running `post-new` while `notmuch new` still reports
success; it must be set explicitly. And a test cannot see any of this in the
ordinary fixture layout, where the index lives inside the mail root and both
accessors return the same string: `NotmuchFixture::splitIndex()` exists for
that, and a test without it passes whichever accessor the code uses.

**The Maildir path is deliberately not configurable.** notmuch stores it as
`database.path` and libnotmuch reads it; duplicating it would create two sources of truth.
The only escape hatch is `general/notmuch_config`, pointing at an alternate notmuch config.
Per-account subdirectories *are* configured, since notmuch does not model accounts at all.

**Delete MOVES the file, and a wrong folder name reaches the mail server.**
Item 103. Every account carries a mandatory `trash` key and an optional
`inbox` one, both naming a folder relative to `maildir`. Naming a folder that
does not exist does not fail: the move CREATES it, mbsync adopts it and writes
state files for it, and under `Create Both` it then propagates to the server,
where every other client sees it. This is not theoretical. A folder name
containing a space was truncated by the origin tag, a bogus folder was created
beside the real one, and four messages of a thread were stranded in it on the
user's real mail. Treat any code that composes a folder name as reaching the
server, because it does.

**A message records where it came from in a tag, because nothing else can.**
`deleted-from:<folder>` is written when Delete moves the file, and read back by
Restore. The file has moved, so neither the path nor anything in notmuch still
knows the original folder. A notmuch tag MAY contain a space, so tags crossing
the thread boundary are joined by a TAB rather than a space; joining on a space
truncated every folder name containing one. A message trashed by another client
carries no such tag at all, which is why the trash view is path-based and why
Restore falls back to the account's inbox rather than refusing.

**Restore reads the DATABASE, never the model.** The model's tags come from the
query, so a row whose delete has not been re-queried still carries its pre-delete
tags: measured `[inbox,unread]` on a message already in the trash, one run in
three. The origin tag is then not found, the message falls into the no-origin
branch, and it goes to the inbox instead of where it came from, silently and
irreversibly. A restore must be right about its destination or it is worse than
doing nothing.

**The sync script lives here, in `assets/mailsync.sh`.** It moved from the
retired `mailctl` project, which never called it: the script is `mbsync` plus
`notmuch new` with a lock, and qtmaildir is the only thing that runs it
programmatically. Two properties exist for this application's sake and
must survive any edit. It **prints to stdout as well as its log file**, because
`MailSync` shows what the command prints and a self-redirecting script leaves
the pane empty; and it **exits with the real status**, because a `0` from a
failed sync makes qtmaildir report success, clear the unsynced-changes count,
and quit on a sync that never happened.

**Every user-facing string is translatable.** Wrap UI text in `tr()`, including strings
that are only ever shown in passing: status bar messages, tooltips, dialog prose,
completion descriptions. Query syntax itself is not user-facing text — notmuch keywords
like `tag:` and `date:` are wire format and must never be translated, only the prose
describing them. The tree was audited against this rule by item 22 on
2026-08-15, and an Italian translation ships, so a new string that misses
`tr()` is now a regression rather than pre-existing debt.

**`tr()` alone does not make a string translatable, and the source cannot tell
you which.** A literal in an ARRAY or any other place with no enclosing class
needs the context named on the literal itself:
`QT_TRANSLATE_NOOP("TheClass", "Text")`. `QT_TR_NOOP` there compiles, reads
correctly, and extracts NOTHING — `lupdate` prints "tr() cannot be called
without context" and skips it, while the use site's `tr()` looks it up at
runtime under a context no `.ts` file contains. That shipped for the eight
rule-builder field labels in `tagrulesdialog.cpp` and made every one of them
permanently untranslatable in any language.

`Q_DECLARE_TR_FUNCTIONS` is NOT the fix for that case, though it is the fix for
a free FUNCTION calling `tr()` (which is what `querycompleter.cpp` uses it
for). Measured: a class carrying the macro beside the array still extracts 0.
The context must be on the literal.

**`lupdate` output is the evidence, never reading**, and `ctest -R
translations` encodes it: it fails on a string with no translation and on one
`lupdate` cannot see. Refresh with `lupdate-qt6 src/ -ts
translations/qtmaildir_it_IT.ts -no-obsolete -locations none` after changing
any user-facing string; a clean run reports zero context warnings, and
`lrelease` must report 0 unfinished, since it silently DROPS an unfinished
string and ships it as English inside an otherwise Italian UI.

A `QTranslator` must live on `main`'s stack: one scoped to a helper function
unloads on return and every string reverts to English with nothing to see.

**Translating a string that something MATCHES on breaks config in a language
the author never runs.** The built-in filters' names are labels and are
translated; `startup_query` resolved by comparing the config's text against
those names, so `startup_query = Inbox` matched nothing under `LANG=it_IT`
where the filter is called "In arrivo". The application opened the wrong view
AND warned that the user's own working config was invalid. It resolves on the
GENERATOR as well now, which is stored in queries.json and identical in every
locale. Before wrapping a string in `tr()`, ask whether anything compares
against it; if so, match on the wire-format identifier and treat the
translated name as an additional convenience, never as the identity. The
regression test installs a real `QTranslator` rather than a stub, because the
bug lives in the gap between the stored string and the displayed one and only
a real translation opens it.

A second trap sits under that test and cost a wrong green: the warning it
asserts on is guarded by `!m_savedQueries.isEmpty()`, so a test with no
`queries.json` never reaches the branch and passes against a broken check. It
writes one, and asserts the file loaded before asserting on what it produced.

**One config file has two readers, and both are now in this repo.**
`~/.config/mailrules/rules.json` is read and written by `src/tagrules.cpp` and
by `assets/hooks/mailrules.py`, which share no code and agree by test.
**Before changing anything about that file's format, read "Changing the rule
format" at the bottom of this document.** It used to be a cross-repo coupling
with the `mailctl` CLI; that project is retired and the hooks moved here on
2026-08-23, so a format change is now one repo and two suites.

**The auto-tagging rules live in a config file, not in the source, and notmuch's
parser rejects almost nothing.** Rules are in `~/.config/mailrules/rules.json`,
applied by the notmuch `post-new` hook in `assets/hooks/`, which the live
`database.hook_dir` symlinks to; `TagRules` here reads and writes the same file
and `TagRulesDialog` edits it.
Two things bite. A stored query carries NO scope: the hook supplies `tag:new`
and wraps the query in parentheses, because `tag:new and a or b` binds as
`(tag:new and a) or b` and a rule that is a disjunction of senders would escape
its scope and match everything. And **a malformed query is not an error to
notmuch**: `from:((((` parses cleanly and matches nothing, so a test asserting
a failure or a `-1` count fails against correct code. This was recorded in
`test_notmuchworker.cpp` for thread counts and then learned again, twice, while
building the rules. Assert on the positional contract, never on a provoked
failure.

**Qt emits no `triggered` for a `QAction` that owns a menu.** Setting a submenu
on an action makes clicking it open that submenu and nothing else, so any
`connect(action, &QAction::triggered, ...)` on the same action is dead code that
compiles, links and never runs. The saved-query overflow menu shipped this way:
every entry carried both a run connection and a submenu of edit actions, and no
entry in that menu had ever been runnable. It went unnoticed because the menu was
the rarely-used half while the user's queries were pinned buttons, and surfaced
only when item 93 moved every query into it. An action that must both run
something and offer actions needs the run as an item INSIDE its submenu.

**A generator must be asked for one account's query, never handed its
all-accounts query to wrap.** `Config::resolvedQuery(query, accountKey)` exists
for this. Wrapping produces `path:"a/**" and (path:"a/Sent/**" or
path:"b/Sent/**")`, which returns exactly the right rows, because `path:` is
hierarchical and the other account's half cannot match inside `a`. That is why
it is dangerous: a row-count assertion passes against it, so the tests assert on
the generated STRING. The mutation putting the wrap back fails two of them.
Related: an EMPTY query means "match everything" to notmuch, so a generator with
nothing to match returns `Config::matchNothingQuery()` rather than an empty
string. An account that configures no sent folder would otherwise give a button
labelled Sent that shows the whole Maildir.

**Every query this application builds goes through `SearchTerm`
(`src/searchterm.h`), and that is what stops five surfaces growing five quoting
rules.** It holds no widget, so the grammar is tested without a painter or a web
engine. Two of its rules are load-bearing rather than cosmetic. `quote()`
escapes backslashes BEFORE quotes, since the other order escapes the
backslashes it just added; it truncates before escaping, so a cut cannot land
mid-escape. And `extend()` parenthesises BOTH sides, because the query bar can
hold a hand-written disjunction and `a or b AND c` binds as `a or (b AND c)`,
which widens a search the user asked to narrow, reporting nothing. This is the
same trap the `post-new` hook handles when it scopes a rule with `tag:new`.

**A writer that does not validate what its reader requires loses data
silently.** `TagRules::save()` wrote any id and `load()` required
`^[a-z0-9][a-z0-9-]*$`, so a rule named `justeat orders` in a field labelled
**Name** was written correctly, dropped on every read, invisible in the dialog,
still occupying the file, and never applied by the hook. The next save from the
dialog would have deleted it outright. `TagRules::validate()` is now the single
predicate both sides use; a bad id loads REPAIRED rather than dropped, so the
rule can be seen and fixed. Two lessons beyond the fix. The load warning already
existed and was correct and useless, because the rule it named could not be
reached, and a warning the user cannot act on teaches them to ignore warnings.
And the repair belongs in the editor, not in `mailrules.py`: the hook tags real
mail unattended, where a silent rename is worse than a drop.

**Rule counts must count MESSAGES.** `requestCounts` counts threads, which is
right for the placeholder pane because a click there produces thread rows. A
rule tags messages, so a thread count understates every rule matching part of a
large thread; `requestMessageCounts` exists beside it for that reason. The two
are separate signals with separate generation counters, and a count request
must never bump `m_generation`: that is the *query* generation, and bumping it
discards any thread load in flight, blanking the message pane because the user
asked for counts.

**Config format gotcha:** QSettings treats `/` in a section name as a group separator, so
account sections are `[account.work]`, not `[account/work]`. `childKeys` returns keys
sorted alphabetically, never in file order. **`[general]` keys are read WITHOUT the
`general/` prefix** — QSettings' INI backend treats a section literally named `[general]`
as its own fallback section and strips it, so a `general/<key>` lookup silently matches
nothing (this is how `notmuch_config` went unnoticed as broken). Config lives at
`~/.config/qtmaildir/qtmaildir.conf`.

Machine-written UI state is a **separate** file, `~/.local/state/qtmaildir/uistate.conf`
via `MainWindow::uiStatePath()`. Never write window blobs into the hand-edited config.
Build the path from `QStandardPaths::GenericStateLocation`, not `StateLocation`: the
latter appends both the organization and the application name, and both are `qtmaildir`.

**`QLineEdit::setCompleter` is wrong for any field holding more than one
value.** It hands completion to the line edit, which then overwrites the
completer's `completionPrefix` with the widget's **entire text** on every
keystroke. In a field holding a list, the first value completes and nothing
after it ever does, because "unread, fl" is matched whole against the
candidates. Setting the prefix from a `textEdited` handler does not help: the
line edit sets it again afterwards. Use `setCompleter` only for a field whose
whole contents are the thing being completed; otherwise attach with
`QCompleter::setWidget` and drive `setCompletionPrefix` and `complete()`
yourself, and replace the token under the cursor on `activated` rather than
letting QCompleter overwrite the field. This has been hit twice, in
`QueryCompleter` (01ba356) and in `TagDialog`; the trap belongs to Qt, not to
either class. A test that uses `setText()` passes against the bug, since
`setText` does not drive a completer at all: the keys must be typed.

**No test may read the real `/proc/locks`, and restoring it after a test is a
BUG, not cleanup.** `TestMainWindow::init()` points every test at an empty lock
table in its own `QTemporaryDir`. Without that the suite observes the machine's
real sync state, so a `mailsync.sh` run makes `SyncMonitor` report a sync in
progress and tests that never mention syncing fail: measured 0 failures in 30
runs with no lock held, 30 in 30 with one held, and it caused three separate
misdiagnoses (item 61). Reproduce with `flock /tmp/mbsync.lock -c 'sleep 60'` in
one shell and the suite in another. The three tests that observe a sync write
their own table content; none of them restores `"/proc/locks"` at the end any
more, because doing so handed the real table to the next test and re-exposed the
whole suite. `noTestCanSeeTheRealLockTable` fails if that protection is ever
lost.

**`QItemSelectionModel::currentRowChanged` is emitted BEFORE the selection model is
updated.** A handler on it reading `selectedRows()` sees the *previous* selection, not the
one the user just made. Verified against Qt 6.11. This produced two separate faults in one
change (987a9e7): a Ctrl+click taking a selection from one row to two arrived reporting
one, and a click collapsing three rows to one arrived reporting three. Any decision that
depends on how many rows are selected belongs in a `selectionChanged` handler, which does
see the true count; `currentRowChanged` is only safe for "which row is current".

The related trap: **`selectAll()` emits no `currentRowChanged` at all** and leaves the
current index invalid when nothing was current. A test that calls `selectAll()` on a fresh
view therefore passes against a missing selection guard, because no signal ever fires. Test
multi-select from a row that is already current, which is also how a user reaches it.

**A `QDialog`'s buttons do not send a `QCloseEvent`.** `accept()` and `reject()`
go through `done(int)`, which hides the dialog without ever closing a window, so
a `closeEvent` override runs only for the window manager's X button. Anything a
dialog must persist on the way out belongs in a `done(int)` override, which both
buttons and `close()` reach. This shipped wrong in the rules dialog and the test
covering it passed, because the test used `close()` and the user used Cancel:
one route out of three. Assert every route. Underneath sits a second trap:
`close()` on a widget that was never shown returns early WITHOUT reaching
`done()`, so a test for the closed path has to `show()` the dialog first or it
asserts nothing at all.

**A modal dialog must close BEFORE the action it asked for runs, not after.**
A signal from a dialog to its parent is a DIRECT connection, so the emit runs
the handler synchronously while `exec()` is still on the stack: the details
dialog's search ran the query, cleared the model and blanked the message pane
while the dialog was still up, holding the `m_items` it was built from. Call
`accept()` first, then emit. The mutation check for this HANGS rather than
failing, since without the `accept()` nothing ever leaves `exec()`, and a hung
test binary is item 84's second trap waiting to mislead the next run.

**`Qt::RFC2822Date` validates the weekday against the date.** `Thu, 14 Aug
2026` parses as INVALID because that day is a Friday, and an invalid parse here
is indistinguishable from the trailing-comment trap `MimeParser::parseDate`
exists to handle. Two fixtures carried a wrong weekday, one of them
pre-existing and unnoticed until something finally parsed it. Write a date
fixture with `date -d <yyyy-mm-dd> +%A`, never from memory.

**Under a tiling compositor a window's size is not the application's to
restore, and the user's desktop is Hyprland.** `saveGeometry` stores
`frameGeometry` and `normalGeometry`; `restoreGeometry` restores the NORMAL
one. When the compositor tiles the window to fill its slot, the size the user
drags is the tile's, and `normalGeometry` keeps whatever the code last passed to
`resize()`. Measured against the real state file after a hand test: frame
2248x806, normal 760x664, so the dialog correctly restored 760 and correctly
looked broken. A whole session went into "the geometry restore is broken" before
the blob was decoded. Decode the stored geometry before theorising, and expect
`maximized` to read as a value no bool should hold, which is the tiled state Qt
records and does not round-trip.

The corollary for tests: **the offscreen platform cannot test window sizing at
all.** It prints "This plugin does not support propagateSizeHints()" and returns
an identical frame for a correct restore and a broken one, verified in a
standalone program containing none of this project's code. A size assertion
there passes against both, and a mutation putting the bug back leaves the suite
green. Assert on the stored value, and leave the frame to a hand test.

**`ThreadListModel::threadAt(int)` takes a ROW and is wrong for any index that
might be a reply.** A tree numbers rows per parent, so a reply's `row()` indexes
its siblings and `threadAt(current.row())` on the first reply of any thread
returns the FIRST THREAD IN THE LIST. This shipped in `markCurrentThreadRead`,
was mostly masked while the write it guarded was thread-wide, and became "a
random message was marked read" the moment a fix scoped that write to one
message (items 87 and 88).

**Use `threadFor(const QModelIndex &)`**, which resolves a message row through
its parent and a thread row through itself. Item 88 added it on 2026-08-16 and
converted every caller; `threadAt(int)` survives only for loops over
`rowCount()`, which genuinely hold a top-level number. A new caller that has an
index and reaches for `threadAt(index.row())` is reintroducing the bug.

**The audit found four live sites, not the one that was reported**, which is the
part worth remembering: `delete` and `toggle_unread` each chose their DIRECTION
from the wrong thread, and the tag dialog counted the wrong thread's tags. All
three were reachable by clicking a reply, none had a test, and the reported
symptom named only `markCurrentThreadRead` (which was in fact protected by an
unrelated guard and could not fire). One bad accessor produced four defects with
one symptom between them.

**A thread's first message is NOT among its children, and two lookups forgot
it.** `setThreadMessages` drops depth 0 because the root row stands for that
message, so `children` never holds it. `applyMessageTagChange` and
`messageById` both search the root first now (item 109); before that, a
message-scoped write to a root card repainted nothing, and the strip refresh set
the pane's chips to the empty node the lookup returned, destroying a strip that
had been correct. Item 108 made that the ordinary gesture rather than an edge
case: the two changes were each correct and broken together.

**`ThreadSummary::tags` is notmuch's UNION over the thread, and a card that
stands for one message must not draw it.** A four-message thread whose third
message is `signed` reads as signed, so the root card and the message pane both
claimed a tag the displayed message did not have (item 110). `MessageRef`
carries the message's own tags and arrives on every load;
`ThreadListModel::setRootMessageTags()` records them on `ThreadNode::first`, and
a thread row's `data()` substitutes `first.tags` for the summary's when that
node exists. Only the TAGS are substituted: the subject, authors, date and reply
count describe the thread and are correct. The summary itself is never
rewritten, because the thread-scoped actions and the query read it.

That was also why a root card could not repaint: with no per-message tags, a
message-scoped write had nothing to change. `applyMessageTagChange` keeps the
summary in step only when `totalCount <= 1`, where the union IS the message.

**The card shows BOTH tiers, and that is item 111 rather than a leftover.**
`PillTagsRole` returns the displayed message's tags first and the thread's other
tags after; `PillOwnCountRole` is the boundary the delegate switches fonts at.
The second tier is drawn in `CardLayout::siblingFont()` and
`CardDelegate::mutedChipColour()`.

**The split comes from the QUERY, not from the message load**, and that
distinction was worth a whole round trip. `ThreadSummary::firstMessageTags` is
read by the same worker walk that finds `firstMessageId`, so an UNOPENED row
already knows which tags are its own. Deriving it from the load instead left
every unopened row drawing one tier and correcting itself on selection, which is
most of the list and is exactly the "chip changed when I clicked" the feature
exists to remove. `nodeFor()` seeds the node on arrival; `reconcile()` must
refresh it AND compare it, since a survivor keeps its node and a sync can move
the root's tags while the thread's union stands still.

**A size step must be a FRACTION, not a subtraction, and the padding has to
follow it.** One point off a 14pt desktop font is a 7% step and reads as the
same size; the user reported exactly that. `CardLayout::siblingFont()` is 0.70
of the card font. `TagChip::kPaddingX` is a fixed 9px a side, so an unscaled
sibling chip is 18px of padding around ~30px of text and stays wide while its
letters shrink: `TagChip::sizeFor()` takes a scale, and
`CardDelegate::chipSize()` is where the tier chooses it. Assert on ratios rather
than sizes, so the test is about the distinction and not the constant.

Muting is **saturation only**. Hue stays so the tag is recognisable; lightness
stays so `TagColors::textColourOn()` keeps its choice and the chip cannot become
unreadable. Do not blend toward the background here: `accentLineColour()`
records what that costs on a dark theme, and a chip is worse because its fill
carries text.

**`TagStrip::visibleTags()` measures the LAYOUT, not the data.** It is one row
that collapses the overflow into a trailing "+N" chip, and an unshown window
under the offscreen platform has no width, so nearly everything lands in
`hiddenTags()`. A test asserting on `visibleTags()` alone passes or fails on how
many tags happened to fit; two shipped that way before it was noticed. Assert on
`visibleTags() + hiddenTags()`.

**A message-scoped write repaints the MESSAGE's row, never the thread's.**
`ThreadListModel::applyMessageTagChange()` is the counterpart to
`applyTagChange()` and exists because there was no optimistic update at all for
a one-message edit: the correct observation that repainting a thread card for a
one-message change is a lie was turned into the wrong conclusion that nothing
should repaint, so Delete and Toggle unread on a reply moved the pending count
and changed nothing on screen (item 105). The thread card deliberately stays
put; one deleted reply does not doom the conversation.

**A thread ROW means the one message its card displays, not the conversation.**
Item 108, 2026-08-16. `ThreadListModel::messageScopeFor()` is what the ordinary
tag actions resolve through; `scopeFor()` still returns whole threads and is
what the five `*_thread` actions use. A thread row's message is
`ThreadSummary::firstMessageId`, carried from the query, so no expansion is
needed; in the Sent view that is the first MATCHED message, which is right for
the same reason it is right on the card. A row with no id contributes NOTHING
rather than falling back to its thread: that fallback is the silent escalation
this removed.

The automatic mark-read follows the same rule (item 87): `m_markReadMessageId`,
armed for a reply as well as a root. One approximation is deliberate and
documented at the call site: a thread row arms from `ThreadSummary::isUnread()`,
a union over the conversation, so it can arm for a thread whose displayed
message is already read. The write is still scoped to that message, so the cost
is a no-op rather than a wrong write.

**Adding an action is FIVE places, and three of them are enforced by tests that
fail in confusing ways.** `KeyMap::knownActions()` (a `Q_ASSERT` in the
constructor fires otherwise, and it surfaces in whichever suite happens to build
a `MainWindow` first — `test_tagrules` did), `defaultBindings()` (OPTIONAL
since item 132: a shortcut is a chosen subset, not a requirement, so an action
nobody would press a chord for simply gets no entry and the shortcut reference
prints it as `(unbound)`), the icon table (every action must carry one), and
a MENU. The no-duplicate-icons rule is narrowed to actions that can reach the
toolbar, by a named exception list; the five thread actions share their twins'
icons because a submenu entry always carries text, and the test asserts none of
them is on the toolbar so the exemption cannot be abused.

**The menu was the fifth place, and this document said four until item 103.**
Nothing enforced it, so `restore` shipped on the trash branch reachable by
`Ctrl+R` and by nothing a user could see or discover. The three existing
coverage tests each assert a different property and all three pass against an
action that appears nowhere in the interface.
`everyActionIsReachableFromAMenu()` closes it, walking every menu and submenu
from the menu bar; it found three more of the same the moment it was written
(`open_thread`, `clear_pane`, `clear_selection`). The toolbar is deliberately
NOT the test's instrument: it is a small chosen subset and always will be. An
action owning a submenu is not itself counted as reachable, since Qt emits no
`triggered` for it.

**A toggle must read the state of what the row STANDS FOR, not of its thread.**
`MainWindow::everySelectedRowHasTag()` is the one question `delete` and
`toggle_unread` both ask; a reply row answers from its message, a thread row
from its thread. Reading the thread makes a toggle ONE-WAY on a reply, and the
failure is silent in a specific way worth knowing: the write is message-scoped,
so it never changes the thread's tags, so the answer never moves however many
times the key is pressed. The second press re-sends a tag the message already
has, which is a no-op, and a no-op repaints nothing. The user reports this as
"the key does nothing", not as "the key did the wrong thing" (item 105).

This is the SECOND fix to the same three lines. Item 88 corrected which thread
they resolved; that was necessary and not sufficient, because a reply needs a
message read rather than a better thread. "Resolved through the index" and
"resolved to the right object" are separate properties, and a test for the first
passes against the second being wrong.

**Any state a thread row draws, a reply row has to draw too, and this was
missed once already.** The message-row branch of `data()` is a separate switch
from the thread branch, so a cue added to one is simply absent from the other
with nothing to flag it. The doomed fill and the strike-out were thread-only
from item 13 until item 105, which is why updating the node was not enough on
its own to make Delete visible. When adding a visual state, check both branches.
One asymmetry is deliberate and must survive: a reply carries no tag strip. A
reply is now BOLD when unread, at the user's request on 2026-08-16, combining
with the dimming for the same two-cue reason a thread row has both; the smaller
reply font is what keeps it subordinate. A `deleted` reply deliberately shows
the chip as well as the fill and strike-out, matching the thread row, confirmed
with the user rather than treated as redundancy to remove.

**And the reverse: a thread-scoped write must reach the thread's LOADED
replies.** `applyTagChange` updated the summary only, so marking an expanded
thread read left every reply bold and undimmed until the next query (item 107).
The symptom reads as a missed repaint and is not: the rows were redrawn from
data that had not changed. When a model update looks like it did not paint,
check whether the data behind those rows actually moved.

**Every path a thread-scoped write travels, a message-scoped one travels too,
and each one was missed separately.** Three of them, found one hand-test round
apart: the optimistic model update (item 105), the message pane's tag strip
(also 105, keyed on `m_currentMessageId` and read by id through
`ThreadListModel::messageById()`, never from `currentIndex()`), and
`flushHeldEdits()`, which re-sent only thread edits and therefore DROPPED any
message edit made during a sync after showing it and counting it as pending
(item 106, data loss, never reported). When adding anything to
`sendThreadTagChange`, check whether `sendMessageTagChange` needs it. Escalating
a message edit to its thread is never the fix: it deletes a whole conversation
when the user deleted one reply.

**Anything applied optimistically must also be reverted.**
`revertPendingTagChange()` keyed on `m_pendingThreadIds` alone, so adding the
message-scoped optimistic update would have left a FAILED message write showing
its optimistic state for good. Both scopes revert now. Undo needs nothing extra:
`MessageTagCommand` routes back through `sendMessageTagChange`.

**Testing this needs two things that are easy to miss.** Put the reply under the
SECOND thread, so the wrong answer is plausible rather than accidentally right,
and give the two threads OPPOSITE states, since two threads in the same state
answer identically whichever way the code resolves them. That second point is
why the reverted item 87 fix was mutation-checked and green while corrupting
real mail. For a toggle, assert on `undoText()`: both directions push one
command over the same rows, so depth and ids cannot tell them apart.

**A test for a mutation on a data-writing path must exercise the REPLY case,
not only the root.** The reverted fix above was mutation-checked and green: it
asserted on a root selection, which is the one case where `current.row()` is
correct. A green mutation check proves the test can fail, not that it covers the
case that matters.

**`test_mainwindow` can now drive a real worker, and three things about it will
waste a session each.** `WorkerBackedWindow` builds a throwaway notmuch
database and writes a `qtmaildir.conf` pointing at it; `wireWorker()` reads
`notmuch_config` like any other key, so no production hook exists or is needed.
It is opt-in per test because the fifty-odd bare-window cases must not pay for a
`notmuch new`. The three traps, all found by a probe that reported success while
measuring nothing:

- **The worker is unreachable by `findChild`.** It is created parentless and
  moved to its own thread, so it is not in the window's hierarchy. Wait on
  observable state with `QTRY_VERIFY_WITH_TIMEOUT`, never on worker signals and
  never on a fixed `qWait(n)`, which passes when the result never arrives.
- **`rowCount()` on a thread row is 0 until the thread is expanded**, since
  children are populated by the expansion. `hasChildren()` is the pre-expansion
  question and falls back to `summary.totalCount > 1`. An assertion on
  `rowCount` fails against correct code.
- **A `ThreadSummary` fixture needs `firstMessageId`.** Since item 108 an
  ordinary tag action resolves a thread row to that id, so a summary without one
  names no message and every action on it silently does nothing. Ten tests
  failed this way at once, all reporting "the action did not happen", which
  reads as a defect in the action rather than a gap in the fixture.
  `makeThread()` sets it; a hand-built summary must too.
- **`currentThreadId()` reports INTENT, not content.** It is assigned
  synchronously in the selection handler before any worker round-trip, so a test
  asserting on it passes with `onThreadLoaded()` disabled entirely, measured.
  `MessageView::showingPlaceholder()` is what the user sees; assert the pane is
  blank BEFORE the gesture so the check after it means something.

**A queued load can outlive the state that started it.** `loadThread` crosses to the worker
on a queued connection, so its reply lands after whatever the UI did in the meantime. The
generation counter covers a superseded *query*, not a superseded *selection*: blanking the
pane and then receiving an in-flight thread repaints it. `onThreadLoaded` therefore drops a
reply that arrives while more than one row is selected. This class of bug USED to be
unreproducible in `test_mainwindow`, which had no worker and never fired `threadLoaded`.
Item 36 changed that: `WorkerBackedWindow` (above) gives a test a real worker, and the
`onThreadLoaded` guard is covered by one.

**Do not conclude a key binding is dead from `QTest::keyClick()`.** Whether a symbol needs
Shift is a layout property, not a Qt one. `Ctrl++` is the shipped `zoom_in` default and is
exactly what the `+` key emits on an Italian layout, while synthetic input never delivers
it. Verify against a real keyboard before changing a default on reachability grounds. The
separate, real trap `normalizeSequence()` handles is a **bare capital** (`N` parses to
unshifted Key_N, which no keystroke emits).

**Rendering probes lie in specific, repeatable ways.** A whole session was spent chasing
a defect that did not exist because of these; each was believed until it was contradicted.

- **Counting lit pixels cannot tell bold from regular.** Antialiasing lights a similar
  number either way, so an "ink count" reads identical whichever is true, in both
  directions. It measures nothing. **Text width** distinguishes weights (277px against
  306px for one string at 12pt), and a **strict pixel diff** distinguishes renders.
- **`viewport()->render()` returns a blank image** in several ordinary situations: before
  the widget is exposed, when the content sits outside a viewport narrower than the
  columns, and sometimes with no discernible cause. A probe that reports "no ink anywhere"
  is far more likely broken than the code it is testing. Check that it finds the thing it
  expects to find *before* trusting it to report the thing it expects to miss.
- **A row scrolled out of the viewport reports a `visualRect` with a real height**, so a
  guard asserting `rect.height() > 0` passes while the pixel loop below it walks zero
  rows and reports "0 pixels, the row was painted over". Item 93 hit this by adding four
  buttons to the query row: the thread list shrank, and a test sizing its window to 300px
  started failing with a message naming a defect that did not exist. Assert the rect is
  INSIDE the viewport, not merely non-empty.
- **A probe can be correct and still measure nothing, by being pointed at the
  wrong object.** A test for the sibling chip's padding called
  `TagChip::sizeFor()` directly: that proves what the function does and nothing
  about whether the delegate asks it for a scaled padding, so a mutation
  dropping the scale at the call site stayed green. Assert through the function
  the production path actually calls (`CardDelegate::chipSize()`), not through
  the one it calls INTO.
- **A "saturated pixel" threshold catches antialiased edges of the selection highlight**,
  hundreds of distinct near-background colours, and will pass whatever the code does. Match
  the exact colours the model supplies instead. Two versions of one test passed under
  mutation before this was noticed.
- Every rendering test needs a **mutation check** and a guard proving it *can* fail: assert
  the geometry it depends on (a column is on screen, a row has non-zero height) rather than
  assuming it.

The bug that started all this was not in the code at all: the desktop's Qt font was
configured **Bold** in qt6ct, so every row rendered bold and `setBold(true)` changed
nothing. Before concluding a Qt facility is broken, check the desktop's own font and theme
configuration.

**`QString::arg()` does not collapse `%%` into `%`.** `printf` does, and the habit
transfers silently. In generated CSS this is quietly destructive: every percentage written
`%%` to escape it reaches the browser malformed, and a browser does not report a bad
declaration, it **drops that one rule and renders the rest**. The 0.11.0 placeholder lost
its mask, its glow and both radial gradients this way while still painting a plausible
pane, so nothing looked broken. Write `%` directly; `arg()` only ever consumes `%1`..`%99`.

The reason it survived review is worth more than the rule: **a geometry probe endorsed the
layout**, because it measured only properties that carried no percentage. A probe that
cannot see the thing that breaks will report success forever. When asserting on generated
CSS, assert on the **generated string** as well as on the rendered result, and make sure
the assertion covers the declarations that actually went missing.

## Web view security

The most security-sensitive area: a browser engine pointed at input from strangers. Do not
loosen any of these without an explicit decision.

- Off-the-record `QWebEngineProfile`, JavaScript disabled, `LocalContentCanAccessRemoteUrls`
  and `LocalContentCanAccessFileUrls` both false.
- The interceptor **blocks every request by default** and **fails closed**: with no document
  URL set, every `qtmaildir:` URL is denied. The document-load exemption matches the
  **exact** base URL passed to `setHtml()`, never the `qtmaildir:` scheme as a whole — a
  scheme-wide allow would let a hostile body reference `qtmaildir://anything` and be trusted.
  Consequence: `MessageView` **must** call `setDocumentUrl()` with the same URL it gives
  `setHtml()`, or nothing renders.
- The pane renders a LIST of messages into one HTML document in one web view (a
  `QWebEngineView` per message would spawn a Chromium render process each). That makes `cid:`
  ids collide across messages, so every reference is rewritten to `cid:<prefix>!<id>`.
  **A `cidPrefix` must never contain `!`** — it is the namespace separator.
  Since item 66 removed the conversation view every caller passes exactly ONE message, so the
  collision cannot currently arise; the prefixing stays because the list-rendering path does,
  and a security property must not rest on every caller happening to pass one item.
- Remote content grants are per-render and never sticky.
- **Attachment filenames are untrusted input.** Reduce to basename, strip separators, resolve
  against the chosen directory, and refuse anything escaping it. Compare resolved paths as
  paths, not with `startsWith` — `/tmp/safe-evil` passes a `startsWith("/tmp/safe")` check.

## At the start of a session: reconcile the backlog with the user's notes

The backlog at `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md` is
**downstream** of the user's own notes at
`~/Documents/Obsidian/note/notes on qtmaildir.md`. The user writes to those notes
whenever they use the application and hit something, so the backlog goes stale on
its own between sessions.

**The backlog holds the status table and the open sections only.** A closed
item's section lives in `2026-08-03-post-0.1.0-usability-closed.md` beside it,
moved there by item 73 on 2026-08-13, so grepping the backlog for a done item's
evidence finds the table row and nothing else. Both files use one numbering
sequence: item 42 is `## 42.` in whichever file holds it. When an item closes,
move its section across on the same commit rather than leaving it for a later
cleanup, which is exactly how the file reached five thousand lines the first
time.

**Read both and diff them before picking up work.** Anything in the notes with no
item in the backlog gets appended with the next free number, in the backlog's
own format (Observed / Cause / Approach / Constraints), with the cause **verified
in the code, not copied from the note**. The two documents are numbered
independently and drifted long ago; never renumber to reconcile them.

This is not busywork. The 2026-08-04 pass found nine unrecorded entries, two of
them defects rather than enhancements, and one of those was a constraint this
backlog had already specified and that shipped unbuilt (item 29). A note saying
"X does not work" is a bug report, and it will sit in a personal notes file
indefinitely unless someone goes looking.

**The backlog covers the mail system, not only this binary.** An item can land
in `assets/hooks/` rather than in `src/`, and item 166 is one: the tagging hook
is part of the mail system the user sees, so a defect there gets an item here
like any other. Item 44 predates that and shipped as commits in this repo and in
the retired `mailctl`, which is why older entries mention a sibling repo; there
is no longer one to split work across.

**Then print the open items as a table, and stop.** The user picks what to work
on; do not start on one, and do not recommend a single item as though the choice
were made. Read the status table for anything not marked `done`, `dropped` or
`postponed`, and render:

| # | Item | Size | Note |
|---|------|------|------|

- **Item** is a short description, not the table's own terse title. Say what the
  user would notice, not the internal name.
- **Size** is the backlog's own: XS under an hour, S a sitting, M a session,
  `?` for an item whose shape is not known yet.
- **Note** is the one thing that decides whether it can be picked up now: a
  defect rather than an enhancement, a decision needed from the user first, a
  dependency on another item, or a constraint that makes it bigger than it
  looks. Leave it empty when there is nothing of the sort.

Flag defects separately from enhancements. They read alike in a numbered list
and do not deserve equal billing: item 28 sat as "a counter is wrong" while the
indicator was quietly lying about whether the user's work was safe to quit on.

Items marked `open, unspecified` (20, 21) cannot be planned from the backlog
alone; they need the user to describe what they pictured. Say so in the Note
rather than proposing a design.

Two gotchas when reading the status table. Item 12 lives in the **"Deferred,
unsized, or split out"** table further down, which has different columns and
carries no size, so a grep across `^| <n> |` picks it up with its description
where the size should be. And an item's status cell is prose, not a keyword:
`open, on demand` (36) and `open, unspecified` (20) are both open. Read the
cell, do not match on `open` alone.

## Working on this repo

Implementation follows `docs/superpowers/plans/2026-08-02-qtmaildir-v1.md` (14 tasks)
against `docs/superpowers/specs/2026-08-02-qtmaildir-design.md`.

**Treat every code block in the plan document as a draft, not as correct.** Nine defects
have already been found in code that was written confidently into it, two of them
security-relevant. Verify Qt API assumptions empirically rather than from memory — several
of those defects were wrong assumptions (e.g. `QKeySequence::fromString` never returns an
empty sequence for garbage input).

TDD, per the user's global preference. `NotmuchWorker` is unit-tested against a throwaway
notmuch database built in a temporary directory (generated Maildir + `notmuch new` +
`NOTMUCH_CONFIG` scoped to the test process), superseding the spec's original "no unit
test" position — it is the only code that writes to a notmuch index.

Work goes directly on `master`, no PR flow. Commits must be GPG-signed (`git commit -S`).
`HANDOFF.md` is local-only and gitignored; never stage or commit it.

**There is no "v1" and no "v2".** The project follows semver on its
user-visible surface, and those labels described a scope split that stopped
being true when compose and send shipped. Reading, organizing and sending are
all part of the application now. The phrase survives in the older spec and
plan documents, which are historical records and are not being rewritten; read
it there as "before compose" and "after compose".

## Cutting a release

Five steps, and **the GitHub Release is part of cutting a release, not a
follow-up.** The repo had fourteen tags and zero releases until 2026-08-09,
because the first four steps were treated as the whole job.

1. Move the `[Unreleased]` entries under a new `## [X.Y.Z] - YYYY-MM-DD`
   heading, leaving `[Unreleased]` in place and empty above it. Add a one or
   two sentence summary under the heading, and an `### Upgrading` section when
   a user's own config or habits need to change.
2. Bump `project(qtmaildir VERSION ...)` in `CMakeLists.txt`, the only place
   the version lives. Reconfigure, build, and check `./build/src/qtmaildir
   --version`. An ordinary dev build answers `X.Y.Z build N`, because
   `QTMAILDIR_BUILD_NUMBER` counts rebuilds so one binary of an unreleased
   version can be told from another (item 167). That is the DISPLAY version;
   what a release ships is the clean one, from
   `-DQTMAILDIR_BUILD_NUMBER=OFF`, which is what the SlackBuild configures and
   what a tarball with no build directory produces anyway. Check the clean
   form before tagging.
3. Commit as `release: X.Y.Z`, then `git tag -s vX.Y.Z -m "qtmaildir X.Y.Z"`.
   Tags are annotated and GPG-signed, matching every existing one.
4. `git push && git push --tags`. `origin` carries two push URLs, the personal
   server and GitHub, so one push reaches both.
5. Create the GitHub Release from the tag, with the body taken from that
   version's changelog section rather than written fresh:

   ```bash
   gh release create vX.Y.Z --repo danixland/qtmaildir \
     --title "qtmaildir X.Y.Z" --notes-file <section>.md --verify-tag
   ```

   Normal release, never `--prerelease`: GitHub's "latest" badge ignores
   prereleases, and every 0.x here is a real release the user runs daily. The
   pre-1.0 stability caveat is already stated at the top of the changelog.

**Version choice is semver on the user-visible surface**, pre-1.0 included. A
changed label, a changed default, or anything in an `### Upgrading` section is a
minor bump, not a patch: 0.12.0 renamed an action and changed the toolbar's
button style, and both are things a user notices without reading the changelog.

**The SlackBuild is no longer in this repository.** It moved on 2026-08-15 to
the `my-slackbuilds` repo, under `qtmaildir/`, SBo compliant and building from
the GitHub release tarball. It is **not** part of this procedure and never
was: bumping it is a task in that repo, which has its own workflow in its
`AGENTS.md` (edit the version in the `.SlackBuild` and the `.info`, two-pass
`sbodl` for the checksum, `sbolint`, then wait to be told to commit). It also
tracks upstream through an nvchecker stanza there, so a release here is picked
up by that repo's own sweep.

## Changing the rule format

`~/.config/mailrules/rules.json` has **two independent implementations**, both
in this repo, and they agree by test rather than by sharing code:

| | reads/writes | applies rules |
|---|---|---|
| `src/tagrules.cpp` | yes | no |
| `assets/hooks/mailrules.py` | yes | via the `post-new` hook |

They are two languages either side of one file, so nothing but the format
couples them. Both readers preserve fields they do not understand
(`TagRule::unknown`, `Rule.unknown`), which is what lets one save a file the
other wrote without stripping it. This was a cross-repo coupling with the
`mailctl` CLI until that project was retired and the hooks moved here on
2026-08-23; the discipline below survives the move because the two readers do.

**The live hook runs every ten minutes on real mail.** Before touching the
schema:

1. Change both readers, not one. A field added on one side and not the other is
   silently dropped on the next save from the other, which looks like data loss
   with no error anywhere.
2. Bump `kFormatVersion` / `FORMAT_VERSION` together only for a BREAKING change.
   Both readers refuse a file whose version they do not know, which is the
   correct behaviour and also means a half-applied bump stops the hook from
   tagging. Adding an optional field needs no bump.
3. Run both suites: `ctest --test-dir build -R tagrules`, and
   `./test_post_new.py && ./test_mailrules.py` from `assets/hooks/`.
4. Verify the round trip by hand, since no automated test spans the C++ and the
   Python: save from the dialog, then run the hook over a throwaway index, and
   confirm the rule count and a note survive.

**Two hook properties are safety-critical, and being ours now is not a reason to
weaken them.**
The hook refuses to remove `unread` or `inbox` (`maildir.synchronize_flags` is
true, so removing `unread` rewrites Maildir filenames and reaches the server),
and it does not consume the `tag:new` marker when the rules fail to load
(clearing it while rules did not run orphans that mail permanently and
invisibly). A dialog here that offers to write such a rule would produce one the
hook then refuses; that is the correct direction, but say so in the UI rather
than letting it fail silently.

Backfill, applying a rule to existing mail, is deliberately unbuilt. See
`docs/superpowers/specs/2026-08-12-tagging-rules-design.md` for what it needs
first, including the revision it forces to the no-confirmation rule above.
