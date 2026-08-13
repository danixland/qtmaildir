# A row builder for tagging rules: design

Backlog item 76, "Every field in the rules dialog is free text, so a rule is
easy to get wrong".

**Status:** design approved 2026-08-13, not implemented.

## The problem this solves

Item 44 shipped a rules dialog whose query field is a bare `QLineEdit`
(`src/tagrulesdialog.cpp:96`). A rule is written by typing a notmuch query by
hand, which puts the whole burden of the syntax on the user at exactly the
moment they are least likely to catch a mistake, because **notmuch's parser
rejects almost nothing**. `from:((((` parses cleanly and matches nothing. A
mistyped `path:` without its `/**` suffix matches nothing. Neither reports an
error anywhere; the rule simply stops tagging, silently, until somebody notices
mail is not being filed.

The user asked for the shape Thunderbird uses: dropdowns to build the logic,
`+`/`-` buttons to add and remove conditions, radio buttons for the joining
logic, and typing reduced to the values that genuinely have to be typed.

## The structural difference from Thunderbird, and why it decides the design

Thunderbird owns its filter format. Its rows **are** the storage.

Here they cannot be. The storage is a notmuch query string in
`~/.config/mailrules/rules.json`, shared with the companion `mailctl` project
and executed by the `post-new` hook. The hook runs the query; it knows nothing
about rows and never will.

So the builder is a **view over a string**, not a store. Everything below
follows from that:

- Rows compile to a query. That direction is easy.
- A query must parse back into rows. That direction is a parser, and every
  existing rule was written by hand.
- A rule the parser cannot represent must still open, still save, and still
  run, unchanged.

## What is being built

A `RuleQuery` value type that parses and compiles the query string, and a
builder section in `TagRulesDialog` that edits it. **The stored format does not
change.** `TagRule` gains no field, `rules.json` gains no key, and `mailctl`
needs no edit. This is a single-repo change, which is the main thing the design
buys by leaving the query string authoritative.

## Decisions taken, with the alternatives that were rejected

### Flat rows plus a separate exclusion block

Thunderbird offers one exclusive radio: match **all** conditions, or match
**any**. Measured against the user's seventeen real rules, that covers sixteen.
The seventeenth is an `or` group nested inside an `and` chain, which is the
shape a person reaches for when they mean "from any of these senders, but not
when the subject looks like this".

The builder therefore has two sections: a positive section governed by the
all/any radio, and a **"but not" block** whose rows are always joined `and not`.
That takes the real corpus from 16/17 to 17/17.

Rejected: flat-only, which would leave a permanent second-class rule that the
user looks at often.

### The query string is only rewritten when rows actually changed

Opening a rule, looking at it, and closing must not rewrite the file. A
recompile that is semantically identical but textually different
(`a or b` becoming `(a or b)`) churns a file that a second tool reads.

The dialog keeps the `RuleQuery` it parsed and compares the current widget state
against it on save. Equal means the stored string is written back byte for byte.

Rejected: a dirty flag driven by widget `changed` signals. Qt emits those during
programmatic population, so loading a rule into the form would mark it dirty
before the user touched anything, rewriting the file on open. The comparison
approach has no such failure, and it correctly treats an edit that was manually
undone as clean.

Rejected: storing the rows in the JSON alongside the query. That is a two-repo
format change, obliges `mailctl` to preserve a field it does not use, and makes
the rows authoritative over the query the hook actually runs.

### The exclusion block is the preferred reading of a trailing negation

Per-row negation and the exclusion block overlap: `not subject:x` can be a
negated row in the positive section or a row in the block. Both compile
correctly. When parsing, the block wins whenever the negations sit at the end of
an `and` chain, because that is how the user describes these rules in words and
how the original shell hook's comments reasoned about them.

Consequence, accepted: a query written with per-row negations may come back
displayed as block exclusions. Semantically identical, and nothing is rewritten
unless the rule is edited.

### Text mode is a toggle on every rule, not an error state

An unparseable rule opens with the toggle already flipped. There is no special
mode, no disabled builder, and no error. Every rule has both views, which also
gives an escape hatch on rules the builder *can* express but clumsily.

Rejected: hiding the builder, which reads as something breaking; and showing it
disabled and empty, which is a false affordance.

## `RuleQuery`

`src/rulequery.h`, `src/rulequery.cpp`. A plain value type with no widget
dependency, following `TagRules` and `CardLayout`: fully unit-testable without a
UI or a painter.

```cpp
struct RuleTerm
{
    enum Field { From, To, Cc, Subject, Tag, Folder, Attachment, Date };
    enum Op { Contains, ContainsNot, Is, IsNot, Has, HasNot, Before, After };

    Field field;
    Op op;
    QString value;
};

struct RuleQuery
{
    enum Join { All, Any };     ///< and / or, over the positive terms only.

    Join join = All;
    QList<RuleTerm> terms;      ///< Positive section.
    QList<RuleTerm> exclusions; ///< The "but not" block, always joined and-not.

    /// False when the query cannot be represented as rows. NOT an error: the
    /// rule opens in text mode and saves unchanged.
    bool parsed = false;

    static RuleQuery parse(const QString &query);
    QString compile() const;
};

bool operator==(const RuleQuery &a, const RuleQuery &b);
```

`parse` never fails and never throws; it returns a value with `parsed = false`.
`compile` is called only on a parsed value whose rows were edited.

### Data flow

Opening a rule:

```
TagRule.query ──parse()──> RuleQuery ──> builder widgets
                  │
                  └─ parsed == false ──> text mode, toggle flipped
```

Saving:

```
rows edited?  yes ──compile()──> TagRule.query
              no  ─────────────> TagRule.query unchanged, byte for byte
```

## Grammar

### What compiles

| Field | Operators | Compiles to |
|---|---|---|
| From | contains / is | `from:x` / `from:"x"` |
| To | contains / is | `to:x` / `to:"x"` |
| Cc | contains / is | `cc:x` / `cc:"x"` |
| Subject | contains / is | `subject:x` / `subject:"x"` |
| Tag | is | `tag:x` |
| Folder | is | `path:"x/**"` |
| Attachment | has | `attachment:x` |
| Date | before / after | `date:..x` / `date:x..` |

Every operator has a negated twin (`contains not`, `is not`, `has not`) which
prefixes `not `. Date has none: "not before" is "after". Tag and Folder carry
only is / is not, because "contains" is meaningless for an exact token and an
exact path.

**Folder appends the `/**` suffix itself.** A `path:` without it matches
nothing, and notmuch does not report that.

**Quoting.** `is` quotes, `contains` does not. A value containing a space is
quoted regardless, or the query breaks. A value containing a `"` is refused at
the row rather than escaped: notmuch's quoting rules inside a quoted phrase are
not worth modelling for a case that has never occurred.

### How a query assembles

Positive terms joined by the radio, then exclusions appended as `and not`:

```
join = Any
terms = [ from:vendor.example.org, from:vendor.example.net ]
exclusions = [ subject:receipt, subject:refund ]

  → (from:vendor.example.org or from:vendor.example.net)
    and not subject:receipt and not subject:refund
```

**Parenthesisation rule:** the positive group is parenthesised when
`join == Any` **and** there is at least one exclusion. Otherwise no parens.

This is the same binding hazard `CLAUDE.md` already records for the hook's
`tag:new` scope: `tag:new and a or b` binds as `(tag:new and a) or b`. Inside a
rule the same mistake turns a narrow disjunction into a filter that matches
everything.

### What parses

A query parses into rows when it is:

- a flat `and` chain of recognised terms, or
- a flat `or` chain of recognised terms, or
- a parenthesised `or` chain followed by `and not` terms, all flat.

**An empty query parses to zero rows**, not to a failure. One existing rule has
an empty query and must open in the builder ready to receive a row.

Everything else sets `parsed = false`: nested parens beyond that one shape,
mixed `and`/`or` without parens, `xor`, an unrecognised prefix (`body:`, `mid:`,
`folder:`), a bare word with no prefix, a `path:` not ending in `/**`, a
two-sided `date:` range, a trailing operator, an unterminated quote.

A parenthesis **inside a value** is not a shape at all: `from:((((` is a From
term whose value happens to contain parens, and it parses and round-trips like
any other. Only a parenthesis in grouping position is a shape question.

### The parser is strict, and that is the safety property

**Recognise the whole query or reject it whole. Never partially.**

A lenient parser that salvages the parts it understands is how a `not` clause
gets silently dropped and a filter quietly widens. Strict rejection means the
worst outcome is text mode, never a wrong rule.

**This parser is not notmuch's parser and must not pretend to be.** It
recognises the shapes this builder emits plus the shapes the existing rules use.
A query it rejects is not invalid: notmuch accepts `from:((((` happily. The
wording in the UI is "can't be shown as rows", never "invalid".

## Extending this later

The design is future-proof in one specific dimension, and it is worth being
precise about which.

**What can never need a redesign:** any query notmuch accepts stays expressible,
saveable and runnable, whether or not the builder understands it. The builder is
not the storage. A rule written years from now with a prefix nobody anticipated
opens in text mode, saves correctly, and runs correctly. No migration, nothing
lost.

**Three extension points, in ascending cost:**

| Change | Cost |
|---|---|
| A new field (`body:`, `reply-to:`, `mid:`) | One entry in the enum, the compile switch, the parse table. Additive. |
| A new operator on an existing field | Same, additive. |
| A new query **shape** (nested `or` within `or`, three-level parens, `xor`) | A parser change. |

Only the third is real work, and hitting it costs a parser extension rather than
a redesign, because storage never depended on the parser. For calibration: the
user's seventeen rules use two shapes, and Thunderbird offers exactly two after
decades.

**`parsed = false` is a first-class state, not an error path**, and is tested
with real unparseable queries rather than asserted unreachable.

## The dialog

The builder replaces the query line edit. Everything else in the form stays.

```
Id       [ vendor-receipts        ]   Stage [ 50 ]  [x] Applied on every sync

Match    (o) all   ( ) any
   [From    v] [contains v] [vendor.example.org  ] [+] [-]
   [From    v] [contains v] [vendor.example.net  ] [+] [-]
But not
   [Subject v] [contains v] [receipt             ] [+] [-]
   [Subject v] [contains v] [refund              ] [+] [-]
                                                   [+] add exclusion

Add tags     [ vendor, receipts    ]
Remove tags  [                     ]
Note         [ ...                 ]

Query    (from:vendor.example.org or ...) and not subject:receipt
                                                   [ ] Edit as text
```

**The "Edit as text" toggle belongs to the QUERY row, not to the match row.**
An earlier draft of this sketch put it beside the all/any radios, which is
where it reads best and is also wrong: switching to text mode hides the
builder, and a checkbox living inside the builder disappears with it, leaving
no way back except closing the dialog. That shipped and a hand test found it
within minutes. The query row is visible in both modes, so a toggle there is
always reachable.

The test for this must assert **reachability**, not the checked state. A
hidden checkbox reports its state perfectly well, so a state assertion passes
against the broken layout.

**The query line stays visible in builder mode, read-only.** It is what ships to
the hook, and watching it update as rows change is what makes the builder
trustworthy rather than a black box. In text mode the same widget becomes
editable: one widget, two states.

**"Edit as text" is always present.** Flipping it on shows the compiled query,
editable. Flipping it back re-parses: on success the builder repopulates, on
failure the checkbox refuses to clear and says why.

**The "But not" block appears only when it has rows**, plus an "add exclusion"
affordance. Sixteen of seventeen rules have no exclusions and an empty block on
every rule is noise.

**Folder rows get a dropdown** populated from the accounts in `Config`, so `/**`
is never typed. A folder present in the file but absent from the config still
displays, as an editable entry, or opening an old rule would silently blank it.

**Count matches is unchanged.** It already works and is generation-stamped on
`m_ruleCountGeneration` (`src/mainwindow.h:657-664`), which must stay separate
from `m_generation`: bumping the query generation for a count discards any
thread load in flight and blanks the message pane. In text mode it counts what
was typed.

**Completion**, the other half of item 76, is independent of this design and can
land before or after it: tag names on the add and remove fields, the query
grammar in text mode. It carries its own trap, recorded in `CLAUDE.md`: a
multi-value field must not use `QLineEdit::setCompleter`, because the line edit
overwrites the completer's prefix with the widget's entire text, so the first
tag completes and nothing after it does. Attach with `QCompleter::setWidget` and
drive the prefix by hand. A test using `setText()` passes against that bug,
since `setText` never drives a completer; the keys must be typed.

## Testing

`tests/test_rulequery.cpp`, a plain unit test with no widget.

**The corpus test is the one that matters.** Every real query shape, asserted to
parse, compile back byte-identical, and compare equal after a round trip. That
single test is the whole "the user's file does not churn" guarantee, and it is
what catches a parenthesisation slip on the one nested rule.

Those queries go in as **generic placeholders**, never the user's real senders
or account names, per the standing rule that nothing personal reaches a commit.
The shapes are what is under test and they survive substitution intact: a
disjunction of eight job-alert senders becomes eight `from:jobs<n>.example.org`
terms and tests exactly the same thing.

**Rejection tests**, which carry the safety property. Queries that must set
`parsed = false` and must not partially parse: nested `or` inside `or`, mixed
`and`/`or` without parens, `body:foo`, a bare word, a `path:` without `/**`, a
two-sided `date:` range, and a trailing operator.

**`from:((((` is not among them, and the reason is worth stating.** notmuch
treats those parens as characters to search for rather than as grouping, so the
query is meaningful, matches nothing, and reports no error. This parser accepts
it as a From row whose value is that literal text, which is what it means. The
assertion there is the **round trip**, never a rejection and never a provoked
notmuch failure: `CLAUDE.md` records twice that notmuch accepts it cleanly, and
a test expecting an error fails against correct code.

**Compile tests** for every field and operator pair including both negations,
and the parenthesisation rule at its boundary: `join == Any` with zero
exclusions gets no parens, with one exclusion gets parens.

**A mutation check on the corpus test.** Make `compile()` always parenthesise; if
the corpus test still passes it is not testing what it claims. Per `CLAUDE.md`, a
passing test here proves nothing without one.

**Dialog-level tests**, added to `test_tagrules`:

- Open a rule, change nothing, save, assert the stored string is **byte-identical**.
- Open a rule with exclusions, toggle to text and back, assert the builder state survives.
- Open an unparseable rule, assert the toggle starts flipped and refuses to clear.

Not tested, deliberately: populating the folder dropdown from a live notmuch
index. That needs the fixture database for little value; tests populate it from
`Config`.

## Out of scope

**Item 77, previewing a rule's matches in the thread list**, and **item 78,
building a rule from a right-click**. Both touch this dialog and both are
independent of the builder. Item 78 wants this design to land first, so the
created rule arrives in a form that can hold it.

**Backfill** remains out of scope, as item 44's spec records. Nothing here
changes that.
