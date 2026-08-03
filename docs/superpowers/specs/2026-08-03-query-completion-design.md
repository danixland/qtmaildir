# Query bar completion

Design for backlog item 17 of `docs/superpowers/plans/2026-08-03-post-0.1.0-usability.md`.

## Problem

The query bar is a bare `QLineEdit` (`mainwindow.cpp:220`) with the placeholder
`notmuch query, e.g. tag:inbox`. Writing a query means knowing notmuch's
vocabulary from memory, and recalling tag hierarchies exactly:
`shopping/amazon` is not guessable.

Two distinct users are underserved:

- Someone who has never used notmuch or neomutt cannot discover that prefixes
  exist at all. An empty box teaches nothing.
- Someone fluent in notmuch still cannot recall every tag they have created.

The goal is a bar that documents the query language while being typed, without
slowing down a user who already knows it.

## Scope

In scope: completion of query prefixes, tag values, date values, path values
and mimetype values, each with a human description shown alongside.

Out of scope, deliberately:

- **Address completion** for `from:` and `to:`. libnotmuch exposes no
  all-addresses call; it would require shelling out to `notmuch address` or
  scanning messages. Both prefixes still appear in the prefix list so the
  vocabulary reads complete, but they complete no values. Deferred to its own
  backlog item.
- **Saved query names.** A saved query name is not valid notmuch syntax, so
  completing one into the bar would produce a query that errors. Making it
  useful would mean expanding the name to its query text, which is
  substitution rather than completion, and a different feature. The saved
  query buttons already sit directly below the bar.
- **Absolute date completion.** `2026-01-15` is not enumerable. See the
  free-form hint below.

## Architecture

A new class owns all of it. `mainwindow.cpp` is already 35K and gains only
construction plus two connections.

```
MainWindow
 └ QueryCompleter          src/querycompleter.{h,cpp}
      ├ completionContext()  free function, pure, unit-tested
      ├ static vocabulary    prefixes, dates, mimetypes (+ descriptions)
      ├ config-derived       path values from Account::maildir
      └ dynamic              tag list, supplied by MainWindow from the worker

NotmuchWorker
 ├ slot   requestAllTags(quint64 generation)
 └ signal allTagsReady(const QStringList &tags, quint64 generation)
```

`QueryCompleter` is constructed with the `QLineEdit` it manages and a `const
Config &`. It installs a `QCompleter` on the line edit, swaps that completer's
model as the cursor moves between contexts, and exposes `setTags(const
QStringList &)` for the worker result. It emits no signals.

The separation matters for testing: `completionContext()` is where the defects
will be, and it needs neither a widget nor a database to exercise.

## The tokenizer

`completionContext(const QString &text, int cursor)` returns:

```cpp
struct CompletionContext {
    enum Kind { None, Prefix, Value };
    Kind kind = None;
    QString prefix;   ///< For Value: the keyword left of ':', lowercased.
    QString stem;     ///< The text being matched.
    int replaceFrom = 0;
    int replaceLength = 0;
};
```

`replaceFrom`/`replaceLength` describe exactly the span an accepted completion
overwrites, so accepting never disturbs neighbouring text.

Rules:

1. Scan back from the cursor to the nearest unquoted whitespace, `(` or the
   start of the string. That span is the current token.
2. Within the token, the first `:` splits it. Text before is the prefix, text
   after is the value being typed.
3. A token with no `:` is a prefix completion.
4. Inside double quotes, return `None`. In `subject:"foo bar|` the cursor sits
   in a literal, not a keyword position.
5. A value containing `..` is a **range of two independent values**. The cursor
   before the separator completes the lower bound, after it the upper bound;
   both draw on the same model, and the replace span covers only the side being
   edited. `date:today..yes|` completes `yesterday` as the upper bound without
   touching `today`.

Matching is case-insensitive; insertion is always lowercase, since notmuch's
prefixes are lowercase.

`is:` and `tag:` resolve to the same model. notmuch treats `is:x` as a synonym
for `tag:x`, so offering tags for both is correct rather than a convenience.

### Range entries and the `..` trap

The relative date entries (`1week..`, `1month..`) contain `..` themselves: each
is already a complete open-ended range meaning "from then until now". Offering
one as a bound inside an existing range yields `date:1week....today`, which is
malformed.

Therefore: **entries containing `..` are offered only when the value has no
`..` yet.** Once a range is underway, only the bare symbolic values are
candidates on either side. This is a required test case, not an optimisation.

## Vocabulary

A `QStandardItemModel` per context: completion text in column 0, description in
column 1. `QCompleter` is set to match on column 0 only, so descriptions never
influence matching. A `QStyledItemDelegate` draws the description greyed and
right-aligned.

Every description passes through `tr()`. Keywords do not: they are syntax.

**Prefixes** — `tag:`, `is:`, `from:`, `to:`, `subject:`, `date:`,
`attachment:`, `mimetype:`, `folder:`, `path:`, `thread:`, `id:`, and the
operators `and`, `or`, `not`.

**Dates** — symbolic: `today`, `yesterday`, `this_week`, `last_week`,
`this_month`, `last_month`, `this_year`. Relative (contain `..`, subject to the
rule above): `1week..`, `1month..`.

**Mimetypes** — `application/pdf`, `image/jpeg`, `image/png`, `text/html`,
`application/zip`.

Mimetypes are the one list a user can extend, via `[completion]
extra_mimetypes` in the config. Entries **append to** the built-ins rather than
replacing them, so a typo or a short list can never leave completion worse off
than the defaults, and there is no way to lose a built-in by adding one entry.

Each entry accepts an optional description after a `|`:

```ini
[completion]
extra_mimetypes = application/epub+zip|EPUB book, message/rfc822
```

`,` separates entries and `|` separates a value from its description. They are
different characters deliberately: QSettings splits comma lists itself, so a
description containing a comma would otherwise be torn into two entries.
Neither character is legal in a mimetype, so nothing is ambiguous.

An entry without a description shows a blank right column rather than being
rejected. An entry that is empty, or whose value is empty, is skipped and
recorded through `Config::addProblem()`: the user configured something that is
not being honoured.

The other value lists are deliberately **not** configurable, because each has a
real source and a user-editable copy would only drift from it:

- **Prefixes** come from notmuch. Adding one to a config list would not teach
  notmuch the keyword; it would complete happily and then error on Enter.
- **Paths** are derived from the configured accounts.
- **Dates** are closed once symbolic and relative forms are covered. Everything
  beyond them is literal and handled by the free-form hint.
- **Tags** are read from the database and are never config.

**Paths** — `Account::maildir` for each configured account, offered after
`path:`. Config-derived rather than scanned: the Maildir root is deliberately
not duplicated in this project's config, and the configured accounts are the
directories actually queried.

`Account::scopedQuery()` (`config.cpp:27`) builds `path:"<maildir>/**"`, so
`path:` is the prefix these values genuinely belong to. `folder:` is a
different matcher in notmuch, against the Maildir folder name rather than the
directory path, and its values are not enumerable from config. `folder:`
therefore completes as a prefix only, like `from:` and `to:`.

The recursive form is worth offering directly: each account contributes both
`<maildir>` and `<maildir>/**`, the latter described as including
subdirectories, since `/**` is the form the application itself uses and is not
guessable.

**Tags** — supplied at runtime, see below.

### Free-form date hint

Date values cannot be enumerated, so the completion list cannot reveal that
notmuch accepts free-form dates at all. When the date model is active the popup
carries a footer label:

> also accepts free-form dates, e.g. `2026-01-15` or `15/01/2026..today`

It is a label beneath the popup's list view, **not a model row**. A row would
be dropped by `QCompleter`'s filter model on the first keystroke that did not
match it, and could in principle be selected and inserted, producing a broken
query. A footer sidesteps both: it cannot be filtered away and cannot be
accepted.

The wording is illustrative, not a specification. notmuch's date parser is
permissive and its exact accepted set varies by build, so the hint must not
read as a promise.

## Tag fetching and freshness

`NotmuchWorker` gains `requestAllTags(quint64 generation)`, wrapping
`notmuch_database_get_all_tags()` (notmuch.h:942) and emitting a sorted
`QStringList`. The `NmTags` RAII alias it needs already exists in `nmraii.h`.
No notmuch pointer crosses the thread boundary; the generation counter follows
the existing pattern.

The list refreshes:

- at startup,
- after each sync completes, since a sync can introduce tags,
- after an `applyTags` that added a tag not already in the list.

The third case is a set membership test in `MainWindow`, not a query. Without
it, a tag the user has just created would be missing from completion until the
next sync, which is precisely the tag they are most likely to type again.

Never per keystroke.

## Triggering

Config gains `[general] completion_on_focus`, a bool defaulting to **false**.
When true, focusing an empty query bar opens the popup showing the full prefix
list. `QCompleter` does not do this unaided: it requires an explicit
`complete()` call on the focus event.

A manual trigger always works, independent of that toggle: a new
`complete_query` action added to `KeyMap::defaultBindings()`, default
`Ctrl+Space`. Going through `KeyMap` rather than hardcoding means it appears in
the shortcut reference automatically and is rebindable in `[keys]`, consistent
with every other binding.

Ordinary typing completes as soon as a token is underway. That is `QCompleter`'s
normal behaviour and is not gated by the toggle; the toggle governs only the
empty-bar-on-focus case.

## Testing

TDD, tokenizer first. `tests/test_querycompleter.cpp`:

- Every tokenizer rule, including the quote escape and both sides of a range.
- The `..` trap: relative entries offered on a bare value, withheld once a
  range exists.
- Replace spans, asserted by applying the completion and comparing the
  resulting string.
- Model selection through the public API against a stub `Config`: `date:`
  selects dates, `path:` selects the configured maildirs in both bare and
  `/**` forms, `is:` selects the same model as `tag:`, and `folder:` selects
  nothing.
- `extra_mimetypes` appends: configured entries are present **and** every
  built-in survives. An entry with `|description` carries it; one without shows
  blank rather than being dropped. A description containing a comma survives
  intact. A malformed entry is skipped, the rest of the list still loads, and a
  problem is recorded.

`requestAllTags` is tested in the existing `tests/test_notmuchworker.cpp`
against its throwaway notmuch database, asserting the tags generated into that
fixture come back sorted.

Register with `add_qtmaildir_test(querycompleter)` in `tests/CMakeLists.txt`,
and add `querycompleter.cpp` to the `qtmaildir_lib` list in
`src/CMakeLists.txt`.

## Consequences

- `mainwindow.cpp` grows by construction and two connections only.
- `Config` gains two accessors: `completionOnFocus()` and `extraMimetypes()`.
- `KeyMap` gains one default binding, which the shortcut reference picks up
  without further change.
- `NotmuchWorker` gains one slot and one signal.
- The prefix list is a maintenance point: notmuch adds prefixes across releases
  (`mimetype:` and `thread:` were not always present), and a hardcoded list
  will not track them. Config would not fix this, it would only let a user
  guess at keywords notmuch may not accept. The real upgrade path is deriving
  the list from the installed notmuch rather than making it editable. Revisit
  if the list goes visibly stale.
