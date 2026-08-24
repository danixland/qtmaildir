# Signatures

Design for backlog item 152. Brainstormed with the user on 2026-08-24.

**Status:** specified, unbuilt.

## The problem

The composer has no signature support at all. Every message is typed from
nothing, and a user who signs their mail retypes the signature or pastes it in
by hand.

The user's own note (`notes on qtmaildir.md`) states the requirement in one
line and one constraint:

> signatures: not tied to an account, with a switch in the editor bar UI.

The constraint is the whole shape of this item. It rules out the obvious
`[account.*] signature` key as the entire answer, because a signature that
belongs to an account cannot be switched per message.

A second requirement came out of the brainstorm: one choice must serve both
the plain and the HTML form of the message, with the format transparent to the
user. Picking "work" must not also mean picking whether it is text or HTML.

## What makes this small

`MessageBuilder` already derives both parts from one string
(`messagebuilder.cpp:321-324`): `text/plain` is `markdownBody` verbatim, and
`text/html` is `MarkdownRenderer::toHtml()` over that same string.

A signature written in markdown and placed in the composer's buffer therefore
gets both forms for free, correctly, with **no change to `MessageBuilder`**.
The transparency the user asked for is a property the pipeline already has.

This is why the design stores markdown and splices into the buffer rather than
appending at build time. Two alternatives were considered and dropped:

- **Separate text and HTML files per signature** (`work.md` + `work.html`,
  the second overriding the rendered form). Considered and chosen briefly,
  then dropped by the user: "I'm overthinking it, let's drop the html part."
  It buys designed HTML signatures (coloured text, table layouts, inline
  logos) at the cost of the signature no longer being visible in the editor,
  because the two parts diverge and the buffer can only hold one of them.
  Nothing the user signs with needs it.
- **Appending at build time** from a key on `OutgoingMessage`. Necessary
  under the two-file design, pointless without it, and it makes the signature
  invisible while composing.

## Storage

```
~/.config/qtmaildir/signatures/
├── work.md
├── personal.md
└── short.md
```

One file per signature. The filename stem is the name shown in the switch,
listed alphabetically. Content is markdown, like the body.

- A missing directory is not a misconfiguration. It means no signatures, and
  the switch offers only "None".
- A file that cannot be read is skipped with a warning through
  `Config::addProblem()`, the same route every other config problem takes.
- No management UI. The directory is edited with the user's own editor. This
  is a deliberate stop on the ladder: a signature editor would be a text
  editor inside a mail client, and the user already has one.

The path is fixed rather than configurable, beside `qtmaildir.conf` in
`QStandardPaths::ConfigLocation`. Nothing yet suggests a second location, and
a key can be added later without breaking a file already on disk.

## Config

Three keys, all optional.

| Key | Default | Meaning |
|---|---|---|
| `[compose] signature` | empty | Name seeded when no account override applies |
| `[account.<key>] signature` | unset | Overrides the above, for that account |
| `[compose] signature_position` | `end` | `end` or `above_quote` |

### The account seeds, it does not bind

`[account.<key>] signature` does not contradict the user's constraint. The
constraint is that a signature is not *owned by* an account and is switchable
per message; this key supplies only a **starting value**. Every signature stays
reachable from the switch under any account, and switching the From: account
does not restrict the choice.

The user asked for this key explicitly after the constraint was restated
("D, one more config but it's a nice thing to have").

### `signature_position`

`end` by default, which is the user's own habit ("My usual placement is always
B"). `above_quote` exists because other clients offer the choice, and it is one
enum key over machinery the splice needs anyway (see below).

It follows `quote_position`'s exact shape (`config.cpp:529-545`): an absent key
is silent and the struct default holds, but a **present and malformed** value
is reported through `addProblem()` and falls back. `value(key, default)` alone
would accept `signature_position = abov` as `above_quote` silently, and this
file already refuses to be the one place that does that.

### A name matching no file

Reported as a config problem and seeds nothing. It is not a fatal error: the
composer opens, with no signature, and the switch still works.

## The switch

A `QToolButton` with a checkable menu on the composer's editor bar, at the
right end with Attach and Send as HTML. That end is where item 142 put the
controls *of the editor*, as against the formatting buttons on the left, and a
signature choice is one of those.

Entries are "None" plus one per file, the current one checked.

A menu rather than a `QComboBox` because the bar's other controls are tool
buttons and a combo would read as a different class of thing. The count is
small and static.

**Not a `KeyMap` action.** It is parented to the composer, exactly as the
formatting actions are (`composewindow.cpp:424-429`), so its scope is the
composer window, the main window's namespace is untouched, and item 132's
menu-reachability rule does not apply to it.

## The splice

One free function, in a new `Signatures` namespace
(`src/signatures.h` / `src/signatures.cpp`):

```cpp
QString replace(const QString &buffer, const QString &signature,
                Position position);
```

Stateless. No stored ranges, no tracked insertion point, nothing that can
desync from the undo stack. This is the property that makes the switch safe to
use repeatedly, and it is why the delimiter does the work.

`ComposeWindow` gets a companion for the directory:

```cpp
QStringList names(const QString &dir);       // stems, sorted
QString text(const QString &dir, const QString &name);
```

A namespace of free functions over values, matching `MarkdownFormat`,
`MessageBuilder`, `ComposeContextBuilder` and `DraftStore`. The splice is then
testable with no widget, which is the point of that convention.

### Finding an existing signature

Scan for the **last** line equal to `-- ` (dash, dash, space: the RFC 3676
delimiter) that is **not** followed by a run of quoted lines. From that
delimiter to the end of its block is the existing signature.

The "not followed by quoted lines" clause is what makes one scan serve both
positions. Under `end` the signature is the buffer's tail and a naive tail rule
would do; under `above_quote` it sits before the quote, and a tail rule would
select the quote and destroy it. The user chose to cover both.

The quote is recognised as a contiguous run of lines beginning with `>`. That
is a scan of the buffer, not stored state, so it survives editing and undo.

### Inserting

- `end`: append to the buffer.
- `above_quote`: insert before the first quoted line; with no quote in the
  buffer, this is identical to `end`. A New message under `above_quote` is
  therefore not a special case.

Selecting "None" removes the found block and inserts nothing.

### Accepted limit

A buffer in which the user has typed a literal `-- ` line is indistinguishable
from one holding a signature, and the switch will replace from there. This is
the correct reading rather than a defect: that string is the signature
delimiter, and typing it means what it means.

## Seeding a new composer

On open, after `seedBody()`, `ComposeWindow` resolves a name:

```
account override → [compose] signature → none
```

and splices it in.

### A resumed draft seeds nothing

The draft branch of `seedBody()` puts the saved body in verbatim, "no
attribution, no quote markers, no blank lines added"
(`composewindow.cpp:627-634`), because a draft is the message itself. That body
already contains whatever signature it was saved with. Seeding again would
append a second one, and the user would find two signatures on a message they
had written once.

### The seeded signature is not an undo step

`clearUndoRedoStacks()` already covers the seeded quote for this reason
(`composewindow.cpp:661-665`): one Ctrl+Z on a fresh composer must not wipe
content the user never typed. The signature is seeded before that call and is
covered by it.

### Changing the From: account

The signature re-splices when the account changes, **but only while the user
has not touched the switch**. A bool on the window records the first use of the
menu and stops the automatic follow from then on.

This matches how `send_html` behaves: seeded from context, then left alone
(`composewindow.cpp:606-610`). The rejected alternative was re-seeding
unconditionally, which can silently discard a signature the user picked
deliberately a moment earlier.

## Testing

`test_signatures`, no widget, over the namespace:

- names and text read back from a directory; a missing directory yields empty
- insert into an empty buffer, both positions
- replace an existing signature, both positions
- remove for "None"
- `above_quote` with no quote in the buffer behaves as `end`
- **the delimiter scan against a quoted reply**, which is the case a naive
  tail rule gets wrong: assert the quote survives

`test_composewindow`, for the wiring:

- seeded from the account override
- seeded from `[compose] signature` when the account has no key
- a resumed draft seeds nothing
- From: follows the account until the switch is used, and stops after
- an unknown name seeds nothing and does not block the composer

`test_config`:

- all three keys read
- a malformed `signature_position` is reported and falls back to `end`
- an unknown signature name is reported

### Not covered, stated rather than faked

The editor bar's own layout, like item 141's, is a look-at-it property. The
switch's position on the bar is verified by hand.

## What this does not do

- **No signature editor.** The directory is edited externally.
- **No per-signature HTML.** Dropped by the user; see "What makes this small".
- **No automatic signature on a draft resume.** By design; the draft carries
  its own.
- **No `From:`-address-derived signature** (e.g. one per identity within an
  account). Accounts are the only identity this application models.
