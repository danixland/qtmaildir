# Contact Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the user copy-pasting addresses. The composer's To/Cc/Bcc complete
from the synced vcards, and the query bar's `from:` and `to:` complete from the
same store.

**Architecture:** A `ContactStore` namespace of free functions over values reads
a vdir of vCard 3.0 files into a `QList<Contact>`. `Config` gains one
`[general] contacts_dir` key. `ComposeWindow` attaches a `QCompleter` to each
recipient field with `setWidget()`, never `setCompleter()`. `QueryCompleter`
gains `setContacts()` beside its existing `setTags()`, and `entriesFor()` grows
a `from:`/`to:` branch.

**Tech Stack:** Qt 6.11 (Widgets only, no new component), CMake 3.21+/Ninja,
QtTest. **No new dependency:** libical is installed but its vCard parser is 4.0
and this machine has 3.0.20, and `libicalvcal` is the old vCalendar-1.0
converter, not a vCard reader. Four fields of a hand parse is the proportionate
answer.

**Spec:** none. Backlog item 204 carries the decisions; there was no brainstorm
because the shape was settled with the user on 2026-09-18. This plan restates
them so it can be executed without reading the backlog.

---

## Required reading before Task 1

Read these before writing any code. Each records a trap this plan walks past.

- `AGENTS.md`, the whole file. In particular the `tr()` rules, the
  `QT_TRANSLATE_NOOP` trap for literals in arrays, and the test-writing rules
  under "Rendering probes lie".
- `src/querycompleter.h:88-115` and `src/querycompleter.cpp:595-657`, the class
  this extends and the branch that currently returns `{}` for `from:`.
- `src/composewindow.cpp:70-83` (`splitRecipients`) on how a recipient field is
  parsed, and why a display name containing a comma must be quoted.

Five facts that will otherwise cost a session each:

1. **Never run a test binary without `QT_QPA_PLATFORM=offscreen`**, and never
   launch `./build/src/qtmaildir` yourself. `tests/CMakeLists.txt` sets that
   variable for ctest only. One direct run of `test_mainwindow` throws a hundred
   windows onto the user's screen.
2. **`QLineEdit::setCompleter()` is WRONG for every field in this plan**, and
   this repository has been bitten by it twice (`QueryCompleter` at 01ba356, and
   `TagDialog`). The line edit overwrites the completer's `completionPrefix`
   with the widget's ENTIRE text on every keystroke, so in a field holding a
   list the first value completes and nothing after it ever does. Attach with
   `QCompleter::setWidget()`, drive `setCompletionPrefix()` from `textEdited`,
   and replace the token under the cursor on `activated`.
3. **A test that uses `setText()` passes against that bug**, because `setText`
   does not drive a completer at all. The keys must be typed
   (`QTest::keyClicks`).
4. **The contacts vdir is NOT `~/.local/share/contacts`.** That path belongs to
   Akonadi and holds a README warning against touching it. The vdirsyncer pair
   writes `~/.local/share/vdirsyncer/contacts/`, in per-collection
   subdirectories.
5. **Most cards have no address.** Measured on the real store, 2026-09-18: 117
   cards, of which 103 carry NO `EMAIL` line, 13 carry one and 1 carries two. A
   test fixture where every card completes is not representative, and a store
   that yields 14 candidates from 117 files is correct rather than broken.

## What was measured, and what it implies

All figures from the user's real store on 2026-09-18. They are the evidence
behind the decisions below, not illustrations.

| | |
|---|---|
| cards | 117, one per file, in per-collection subdirectories |
| version | `VERSION:3.0` throughout |
| with `EMAIL` | 14 (13 with one, 1 with two) |
| `EMAIL` parameters seen | `TYPE=HOME` (3), `TYPE=WORK` (12) |
| `FN` containing a comma | 1 |
| `FN` containing a quote | 0 |
| folded lines | 0; the longest line is 4402 characters (a base64 `PHOTO`) |
| line endings | LF |

**The folding measurement is the one not to over-read.** This store happens to
be unfolded because of what the server sends, but folding is in the vCard 3.0
grammar (RFC 2426) and any other server may use it. The parser unfolds
regardless; the fixtures therefore include a folded card, which the real store
would never have caught.

## File Structure

**Created:**

- `src/contactstore.h` / `src/contactstore.cpp` — a `Contact` struct
  (`name`, `email`) and the `ContactStore` namespace: `unfold()`, `parseCard()`,
  `loadDirectory()`. Pure over values, no widget, no `QCompleter`: this is what
  makes the parse testable without a window, exactly as `MimeParser` and
  `SearchTerm` are.
- `tests/test_contactstore.cpp` — the parse, the unfold, the directory walk.
- `tests/fixtures/*.vcf` — hand-written vCard fixtures, in the FLAT `fixtures`
  directory the tree already has (it holds `.eml` files today, no
  subdirectories). Reached with `FIXTURE_DIR`, which
  `tests/CMakeLists.txt:47-49` sets per test target; `test_contactstore` needs
  the same two lines `test_mimeparser` has. **Every address is `@example.org`
  and every name is invented.** Nothing is copied from the real store; see the
  personal-data rule in `AGENTS.md`.

**Modified:**

- `src/CMakeLists.txt:1-45` — add `contactstore.cpp` to `qtmaildir_lib`.
- `src/config.h` / `src/config.cpp` — the `contacts_dir` key and its accessor.
- `src/composewindow.h` / `src/composewindow.cpp` — a completer per recipient
  field.
- `src/querycompleter.h` / `src/querycompleter.cpp` — `setContacts()`, the
  `from:`/`to:` branch, and the correction to the comment at
  `querycompleter.cpp:652-656`.
- `src/mainwindow.cpp` — load the store once and feed both consumers.
- `tests/CMakeLists.txt` — register `test_contactstore`.
- `tests/test_querycompleter.cpp` — the `from:`/`to:` cases.
- `tests/test_composewindow.cpp` — the typed-completion cases.
- `translations/qtmaildir_it_IT.ts` — refreshed by `lupdate`.
- `README.md`, `CHANGELOG.md`.

---

## Task 1: `ContactStore`, the parse

**Files:**
- Create: `src/contactstore.h`, `src/contactstore.cpp`
- Create: `tests/test_contactstore.cpp`, `tests/fixtures/contacts/*.vcf`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

**TDD, tests first.** Write each assertion before the code that satisfies it.

- [ ] `Contact` is a struct of two `QString`s, `name` and `email`. Nothing else:
      `PHOTO`, `ADR` and `TEL` are ignored entirely, per the user's decision.
- [ ] `unfold()` joins a continuation line (one beginning with a space or a tab)
      to its predecessor, removing the leading whitespace character and nothing
      else. A fold may land ANYWHERE, including mid-token and inside base64, so
      unfolding happens before any field is looked at.
- [ ] `parseCard()` takes one file's unfolded text and returns a
      `QList<Contact>`: one entry per `EMAIL` line, each carrying the card's
      `FN`. A card with no `EMAIL` returns an empty list. A card with no `FN`
      but with an `EMAIL` returns a contact whose `name` is empty, which is
      valid and completes on the address alone.
- [ ] A property name may carry parameters before the colon
      (`EMAIL;TYPE=WORK:a@example.org`), and parameter values may themselves be
      quoted and contain a colon. The split is on the first colon that is not
      inside a double-quoted parameter value, not on the first colon.
- [ ] Property names are case-insensitive per RFC 2426. `email:` parses.
- [ ] `N` and `FN` use backslash escapes (`\,` `\;` `\\` `\n`). `FN` is
      unescaped before use, or a name written `Rossi\, Mario` reaches the UI
      with its backslash showing.
- [ ] `loadDirectory()` walks a directory RECURSIVELY, since a vdir keeps one
      subdirectory per collection, reads every `*.vcf`, and returns the
      concatenation. A file that cannot be read or does not parse is SKIPPED,
      not fatal: one bad card must not cost the other 116. **The recursion test
      builds its tree in a `QTemporaryDir`**, since the shared `fixtures`
      directory is flat and adding subdirectories to it for one test would be a
      layout change the rest of the suite has no use for.
- [ ] The result is de-duplicated on the address, case-insensitively, and
      sorted by name then address. Two collections holding the same person is
      the ordinary case, not an error.
- [ ] `loadDirectory()` on a non-existent or empty directory returns an empty
      list and does not warn. A machine with no vdir is the ordinary case for
      anyone who is not this user.

**Verification:** `ctest --test-dir build -R contactstore`. Then a mutation
check: break the unfold (join without stripping the space) and confirm a test
fails.

---

## Task 2: The `contacts_dir` config key

**Files:**
- Modify: `src/config.h`, `src/config.cpp`
- Modify: `tests/test_config.cpp`

- [ ] A `[general]` key named `contacts_dir`. **Read WITHOUT the `general/`
      prefix**: QSettings' INI backend treats a section literally named
      `[general]` as its own fallback section and strips it, which is how
      `notmuch_config` went unnoticed as broken. Follow `notmuch_config`'s own
      code, `config.cpp:231-237`.
- [ ] `~` is expanded, as every other path key in this file is.
- [ ] Absent or empty means the feature is OFF: no store, no completion, and
      **no warning**. This is not a misconfiguration.
- [ ] A path that is set and does not exist DOES warn, through `addProblem()`,
      because the user asked for something and is not getting it. That is the
      same rule `message_zoom` and `forward_prefixes` follow.
- [ ] There is no default path. The vdirsyncer location is documented in the
      README as the usual answer, but guessing it in code would make the app
      read a directory the user never named.

**Verification:** `ctest --test-dir build -R config`.

---

## Task 3: Completion in the composer

**Files:**
- Modify: `src/composewindow.h`, `src/composewindow.cpp`
- Modify: `tests/test_composewindow.cpp`

- [ ] One `QCompleter` over a model of completion strings, shared by `m_to`,
      `m_cc` and `m_bcc`, attached with **`setWidget()`** and never
      `setCompleter()`. Re-point `setWidget()` on focus, since one completer
      serves three fields.
- [ ] A candidate matches on the NAME and on the ADDRESS, per the user's
      decision, so `QCompleter`'s default prefix matching over a single string
      is not enough. Match case-insensitively with `Qt::MatchContains` over a
      model holding both, and display `Name <addr>`.
- [ ] Accepting inserts `Name <addr>` over the token under the cursor, where a
      token is bounded by commas. The rest of the field is untouched, which is
      the whole reason `setCompleter()` is unusable here.
- [ ] **A display name containing a comma is QUOTED on insertion**:
      `"Rossi, Mario" <m@example.org>`. `splitRecipients()`
      (`composewindow.cpp:73`) splits on commas, and its own comment says a name
      with a comma has to be quoted exactly as the wire format requires. One
      card in the real store has such a name, so this is not hypothetical. A
      name containing a double quote has its quotes backslash-escaped inside
      the quoted string.
- [ ] A contact with an empty name inserts the bare address, not `<addr>`.
- [ ] The store is loaded ONCE, when the composer is constructed, from whatever
      `MainWindow` already holds (Task 5). A composer opened with the feature off
      simply has no candidates and behaves exactly as today.

**Tests must TYPE**, with `QTest::keyClicks`, never `setText()`. At minimum:
completion works on the FIRST recipient, and it still works on the SECOND after
a comma, which is the exact case `setCompleter()` breaks and the reason this
task exists in this shape.

**Verification:** `ctest --test-dir build -R composewindow`, then the mutation
check: swap `setWidget()` for `setCompleter()` and confirm the
second-recipient test fails. If it still passes, the test is using `setText()`
somewhere.

---

## Task 4: Completion in the query bar

**Files:**
- Modify: `src/querycompleter.h`, `src/querycompleter.cpp`
- Modify: `tests/test_querycompleter.cpp`

- [ ] `setContacts(const QList<Contact> &)`, beside `setTags()` and with the
      same shape: it REPLACES the candidates and is called from `MainWindow`.
- [ ] `entriesFor()` grows a branch for `from:` and `to:`, beside the existing
      `path:` one. The description in each `CompletionEntry` is the contact's
      name, so the popup shows who an address belongs to.
- [ ] **The value inserted is the bare ADDRESS**, not `Name <addr>`. This is one
      token in notmuch's grammar, and a display name is not something notmuch
      matches on.
- [ ] The value goes through `SearchTerm::quote()`, like every other query this
      application builds. An address needs no quoting in practice, but the rule
      is that nothing reaches a query unquoted, and a name-derived value never
      bypasses it.
- [ ] **Correct the comment at `querycompleter.cpp:652-656`**, which says
      "addresses need an enumerator libnotmuch does not expose". That was true
      and is now half wrong: the vcards are that enumerator. Leave the sentence
      for `folder:`, `subject:`, `thread:` and `id:`, which still complete
      nothing, and say why `from:`/`to:` no longer belong in that list.
- [ ] With no store configured the branch returns `{}` and the behaviour is
      exactly today's.

**Verification:** `ctest --test-dir build -R querycompleter`.

---

## Task 5: Load the store once, feed both

**Files:**
- Modify: `src/mainwindow.h`, `src/mainwindow.cpp`

- [ ] `MainWindow` loads the store once, when it is first needed, and holds the
      list. 117 files is nothing; a `QFileSystemWatcher` is speculative and is
      NOT part of this item.
- [ ] It calls `m_queryCompleter->setContacts()` at the same place it already
      calls `setTags()` (`mainwindow.cpp:2895`), and hands the same list to each
      `ComposeWindow` it constructs.
- [ ] No action is added, so none of the five places in "Adding an action is
      FIVE places" applies. Say so in the commit message rather than leaving the
      reader to check.

**Verification:** `ctest --test-dir build` in full.

---

## Task 6: Translations, docs, changelog

**Files:**
- Modify: `translations/qtmaildir_it_IT.ts`, `README.md`, `CHANGELOG.md`

- [ ] Every new user-facing string is wrapped in `tr()`. A literal in an ARRAY
      or anywhere with no enclosing class needs
      `QT_TRANSLATE_NOOP("TheClass", "Text")`; `QT_TR_NOOP` there compiles and
      extracts NOTHING.
- [ ] `lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete
      -locations none`, then translate the new strings. `lrelease` must report
      **0 unfinished**: an unfinished string is silently dropped and ships as
      English inside an Italian UI.
- [ ] `ctest --test-dir build -R translations` passes.
- [ ] README: a subsection documenting `contacts_dir`, naming
      `~/.local/share/vdirsyncer/contacts/` as the usual value and warning that
      `~/.local/share/contacts/` is Akonadi's and is the wrong one. State that
      the store is read-only, so editing contacts is not part of this.
- [ ] CHANGELOG: an `[Unreleased]` entry. No `### Upgrading` section is needed:
      an absent key means the feature is off and nothing a user has configured
      changes.

**Verification:** the full suite, plus `lrelease-qt6` reporting 0 unfinished.

---

## Out of scope, deliberately

Named here so they are not smuggled in, each with the item that owns it:

- **Writing a vcard.** Item 205. This plan reads and never writes.
- **An "add to contacts" gesture.** Item 208.
- **A sender's card in the message pane.** Offered to the user on 2026-09-18
  and declined: "I don't have much use for it."
- **Watching the directory for changes.** Speculative. A restart picks up a
  vdirsyncer run, and that is enough until a measurement says otherwise.
- **Completing from mail the user has actually corresponded with**, as distinct
  from the address book. A different feature with a different data source
  (notmuch's own address index), and not what the note asked for.

## Hand test to hand over at the end

The suite cannot judge whether this feels right, so the last step hands the
build over rather than declaring it done. What to look at:

1. Open a composer, type three letters of a known contact's name in To, and
   check the popup offers them and inserts `Name <addr>`.
2. Accept one, type a comma, and complete a SECOND recipient. This is the case
   that breaks under `setCompleter()`.
3. In the query bar, type `from:` and three letters, and check the popup offers
   addresses with the contact's name as the description.
4. Confirm the count is plausible: the store has 117 cards and only 14 carry an
   address, so a short list is correct.
