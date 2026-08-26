# Card avatars and the account fade

Design for backlog item 169. Brainstormed with the user on 2026-08-26.

**Status:** specified, unbuilt.

## The problem

The user's note asks for two things about a card:

> the left border of a card expresses the account the mail belongs to. the
> background color of the card should fade left to right from the account color
> to the current background color we are using (or to transparent to work both
> in light and dark themes). On the left we should leave room for an account
> avatar (a squircle), for now it could be extracted from the sender name "From:
> john doe" becomes "JD" in the avatar. As soon as we include khard (or some
> other vcard provider/manager) we will switch to images if the corresponding
> vCard has one.

Half of it already shipped. The account colour is drawn as a solid bar down the
left edge (`CardLayout::accentRect`, filled in `CardDelegate::paint()` through
`CardDelegate::accentLineColour()`), and a reply's spines carry the same colour
muted against the pane's base. What does not exist is any gradient, and any
avatar: `CardLayout` reserves no rect for one, so the geometry has to grow
before the painting can.

The note calls it an "account avatar", but the brainstorm settled that it is
the SENDER's, not the account's. The account is already expressed twice, by the
accent bar and by the fade; an avatar in the account's colour would conflate
"which mailbox received this" with "who wrote it", which are different facts.

## What the card looks like

A card stays exactly three lines and every row keeps the same height, so
`setUniformRowHeights(true)` survives untouched.

```
+--+-------+------------------------------------------------+
|  |       | sender                            26/08 09:14  |
|##| [AV]  | * subject                        @  v 3 replies |
|  |       | [tag] [tag]                                    |
+--+-------+------------------------------------------------+
 ^     ^
 |     +-- avatarRect, full height, its own gutter
 +-------- accentRect, unchanged
```

The avatar is a full-height squircle in its own gutter, on roots and replies
alike. `contentLeft` shifts right by the gutter, on top of the existing indent.

Three alternatives were shown to the user and rejected: a two-line squircle
with the tag strip running full width beneath it, and a small squircle inline
on the sender line costing no width. The full-height form won on presence, with
the cost accepted explicitly: at `kMaxDepth` the subject loses the gutter's
width on top of the indent it already loses.

## The fade

A horizontal gradient of the account colour, drawn after the chrome and before
the text, so the selection highlight and the deleted-row tint still cover it.
That a selected row reads mostly as selection is expected, not a fault.

- **On a thread root** it starts at the card's left edge.
- **On a reply** it starts at that reply's INNERMOST spine, which is its own
  coloured left border. The wash therefore steps right with the nesting, and a
  deeper reply's outer spines stand in plain background.
- **It ends at 60% of the card's width**, in both cases. The end is
  proportional, so a reply's wash is shorter as well as further right.
- **A reply's gradient is weaker than a root's**, so an expanded thread reads as
  one coloured block with the root leading it.

The user chose the reply origin against the alternative of starting every fade
at the card's left edge. The spine reading is the correct one because the
spines are ALREADY the account colour: `carddelegate.cpp` muted them at 0.55
against `QPalette::Base` and resolves a reply's colour by walking to its root,
so the coloured border the fade hangs off is a thing that exists rather than
one this item introduces.

60% was chosen by the user over 50%. It is a percentage of card WIDTH, which
means the wash grows with the window; a fixed pixel distance and an
anchor-to-the-layout variant were both offered and declined.

## Whose face

The **sender's**, except in the flat views (Sent and Drafts) where it is the
**recipient's**, since those views already show recipients in the sender's slot
and `authors` is the user on every row there.

This costs no extra query. `MainWindow::m_sentView` is really "flat view": it is
assigned from `FlatResult`, and `generatorIsFlat()` in `config.cpp` is the closed
set `{sent, drafts}`, so both already request the recipients fold. The name is
misleading and deserves a comment, but not a rename inside this item.

For a thread row the sender is the one belonging to `firstMessageId`, the
message the card already stands for. Nothing new is resolved.

## The initials

Two letters, always, so every squircle reads the same shape:

| Input | Rule | Example |
|---|---|---|
| Display name, two or more words | first letter of the first two words | `John Doe` -> `JD` |
| Display name, one word | first two letters of that word | `Cofidis` -> `CO` |
| No display name | first of the local part, first of the domain | `noreply@cofidis.it` -> `NC` |
| Nothing usable at all | first two of the account's label | see the fallback below |

The user chose the local-plus-domain form over first-two-of-local (`NO`) and
over a single letter (`N`), because it never degrades to one letter and never
reads as a truncated word.

## The fill

Two fills, both generated locally from a hash of the sender's address, both
stable per sender.

- **Identicon** - a 5x5 symmetric grid from the hash bits, with a translucent
  dark veil between the pattern and the letters so the initials stay readable
  whatever the pattern does.
- **Two-tone** - two related hues and a split angle from the hash, initials on a
  large flat field.

Which one, in order:

1. The address is listed in `business-senders` -> **two-tone**, whatever display
   name it presents.
2. Otherwise a display name is present -> **identicon**.
3. Otherwise -> **two-tone**.

Rule 2 is the one the user asked for by name: `Ian Farrell
<notifications@github.com>` gets `IF` and the identicon, because it presented
itself as a person, even though the address is corporate. Rule 1 is the override
that lets a listed sender be forced back to two-tone.

Colours are generated at a FIXED lightness, so `TagColors::textColourOn()`-style
contrast reasoning holds and the initials stay legible in both themes.

**None of this is Gravatar in the sense of contacting Gravatar.** A real lookup
would send a hash of every correspondent's address to a third party on each
repaint, which is out on the project's privacy stance and on the no-network
rule. Only the generated half of the idea is taken, and it works entirely
offline with `QCryptographicHash`, which Qt Core already provides.

Item 72's vCard photo would later replace the fill without touching the rect.

## The fallback

There is always an avatar. When no sender address is available at all, the
squircle is hashed from the **account's own address**, which the card always
knows through its account, and the initials are the first two letters of the
account's label. This keeps a stable, themed squircle rather than a hole, and
it cannot be confused with a real sender because the two-letter source is the
account.

## `ThreadSummary::firstMessageSender`

**This is the one structural change, and it exists because the card currently
has no address to hash.**

`ThreadSummary::authors` is notmuch's own summarised string and carries display
names ONLY. Measured against the real index on 2026-08-26: `'Standreas'`,
`'Randstad Italia'`, `'Ryanair'`, `'The Hacker News tramite LinkedIn'`. No `@`
anywhere. The initials rule survives that, but two things do not:

- the identicon has nothing stable to hash, and
- the `business-senders` list has nothing to match.

Hashing the display name instead was considered and rejected: notmuch builds
those strings, so one sender varies its identity as the string varies
(`The Hacker News tramite LinkedIn` is a constructed label, not a header value).

So `ThreadSummary` gains `firstMessageSender`, the bare address of the message
the card stands for. It is filled by the SAME worker walk that already fills
`firstMessageId` and `firstMessageTags`, from the same message, and `From` is
served from notmuch's index rather than from the file. This is exactly the
pattern item 111 used for `firstMessageTags` and it carries the same "free, for
the same reason" note.

The Sent/normal split is already resolved in that walk and must be respected:
`withRecipients` selects the first MATCHED message for a flat view and the
thread's opening message otherwise, so the address follows whichever message
the card is standing for.

**Measured cost of reading senders from the index**, on the developer's own
database: 1322 distinct senders deduplicated in 12 ms, and 5105 messages
enumerated in 76 ms. There is no cost problem here, which is what makes the
whole feature and its list practical.

## The `business-senders` list

`~/.config/qtmaildir/business-senders`. Plain text, one entry per line, `#`
comments, blank lines ignored.

Deliberately NOT in `qtmaildir.conf` and deliberately not INI. The user's stated
workflow is grep-and-edit ("if a personal email ends in the list by mistake I
can grep it out and remove it"), QSettings would fight a bare list, and the main
config is already large.

An entry is either:

- an exact address, `noreply@cofidis.it`, or
- a whole domain, `@cofidis.it`, which is what a company sending from six
  addresses actually needs.

**No globs.** A pattern language nobody asked for is a rule the user cannot grep
for literally, which defeats the file's whole purpose.

Read once at startup and on an explicit reload. Never per repaint, and never
stat-per-row.

## How the list fills itself

At the end of every sync the application runs, the worker collects the senders
of the newly arrived mail and appends CANDIDATES, commented out:

```
# noreply@cofidis.it (47 messages)
```

**How much mail the scan covers** depends on whether the list has ever been
used. With no file, or a file holding no active entry, it scans the WHOLE
database; afterwards it scans the last week. The first run is exactly when a
full scan earns its cost: a week of mail proposes almost nothing, so a
week-only rule would leave the list taking months to become useful. It is
affordable because it happens once, measured at 76 ms over 5105 messages.

A file holding only rejected candidates still counts as unused. Rescanning it
re-proposes none of them, since anything already mentioned is skipped.

A candidate is an address whose local part is in a small built-in word list
(`noreply`, `no-reply`, `donotreply`, `info`, `support`, `billing`,
`newsletter`, `notifications`, `mailer-daemon`), or one that recurs with no
display name.

Two rules make this safe, and both are borrowed from `mailrules.py`'s existing
discipline:

- **Never writes an uncommented entry.** Nothing on screen changes until the
  user uncomments a line. A step that silently reclassified forty senders would
  have to be audited line by line anyway.
- **Never removes, and never re-adds.** An address already present in the file
  in ANY form, commented or not, is skipped. An entry the user grepped out
  therefore stays out, instead of reappearing within ten minutes with no
  explanation.

### Why the application writes it, and not the `post-new` hook

Putting the scan in `assets/hooks/` was considered at length and rejected. It
is the better placement on paper: `post-new` runs after EVERY `notmuch new`
whatever started it, including the user's cron, whereas `mailsync.sh` is only
the route qtmaildir drives, and the hook is already scoped to `tag:new` so it
would see only new mail.

It was rejected on weight. The hook is Python and the reader is C++, so it would
recreate the two-implementations-of-one-format situation that `CLAUDE.md`
documents for `rules.json`, with the same requirement that both sides change
together and the same by-test-only agreement. That is a heavy contract for a
cosmetic feature.

The application writing it has one implementation, already knows when a sync
finished, and the scan is free. The only case lost is mail indexed by cron while
qtmaildir is not running, and those senders are picked up at the next run.

## What is testable, and what is not

Per the standing rule (`tests-only-for-measurable-things`), this feature is
mostly judged by looking. Split accordingly:

**Assert in tests:**

- `CardLayout` places `avatarRect` and moves `contentLeft`, at several depths
  and for a root against a reply. Geometry only, no painter, per the existing
  rule that a card layout must be testable without one.
- The initials function, over all four rules in the table above, including the
  one-word and no-display-name cases and an address with no `@` at all.
- The fill CHOICE, over the three ordered rules, including a listed address
  that presents a display name.
- Hash stability: the same address yields the same fill twice, and two
  different addresses differ.
- `business-senders` parsing: exact entries, `@domain` entries, comments, blank
  lines, and an entry with leading or trailing space.
- The append step: never writes an uncommented line, and skips an address
  already present commented out. This is the data-loss-adjacent property, so it
  is the one worth the most care.
- `ThreadSummary::firstMessageSender` is filled by the query walk, for a normal
  view and for a flat one, where the two must resolve DIFFERENT messages.

**Hand to the user to look at:** the fade's weight and its 60% end, the two
fills against real mail, whether the initials are legible over an identicon at
the desktop's own font size, and whether the gutter costs too much subject at
depth.

A rendering probe must not be used to judge any of that, for the reasons
`CLAUDE.md` records under "Rendering probes lie".

## Risks

- **The gutter compounds with the indent.** A deep reply already loses
  `kMaxDepth * kIndentStep`; it now loses the avatar gutter as well. If it reads
  badly, the cap is the knob to turn, not the avatar.
- **An identicon behind two letters is the classic legibility failure.** The
  veil exists for exactly that and its opacity is a value to tune against the
  real font, not to fix on the first guess.
- **`firstMessageSender` is a new field on a struct crossing the thread
  boundary.** It is a plain `QString` and crosses like the rest, so there is no
  ownership question, but the walk that fills it must finish while the
  `NmThread` is alive, exactly as the surrounding code already documents.
- **The candidate word list is a guess.** It will miss senders and propose
  wrong ones, which is precisely why nothing it writes takes effect until the
  user uncomments it.
