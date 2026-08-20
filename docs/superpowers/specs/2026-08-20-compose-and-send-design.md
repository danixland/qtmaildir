# Compose and send

**Resolves backlog item 123.** Opens follow-up items for an outbox, inline
images, save-message-as-attachment, configurable markdown dialect, and a review
of the every-action-has-a-shortcut rule; those are listed at the end.

**Status: design only.** No code has been written. This document is the output
of the brainstorm the item asked for, on the branch its `#new-branch` tag asked
for.

v1 was read-and-organize. This is the other half.

## What decides the shape

Two facts about the machine, measured on 2026-08-20 and recorded in item 123,
constrain everything below.

**There is no MTA.** `msmtp` and `sendmail` are both absent. neomutt, the
application this one mirrors, sends over its own built-in SMTP configured per
account in `~/.config/neomutt/accounts/*.rc`. So the sentence "sending should be
an external script on the same model as `mailsync.sh`" describes a model that
does not exist on this machine and would have to be created.

**`CLAUDE.md` records that this application does no network protocol work at
all.** Fetching is `mbsync` through `assets/mailsync.sh`. That rule is
load-bearing: a socket in the process that also runs a browser engine is a
different project with a different security surface.

The decision taken is to keep the rule and make the send path a **configured
command**, exactly as `[sync] command` already is. The application never learns
what SMTP is. What the user installs behind that command is theirs to choose.

## Decisions

Each of these was settled in the brainstorm. The reasoning is kept because the
alternatives are all plausible and will be proposed again otherwise.

### Sending is a per-account command

```ini
[account.work]
send_command = msmtp -a work -t
```

The command receives the complete RFC822 message on **stdin**. Its exit status
is the result: 0 is success, anything else is failure and its stderr is shown.

This is `[sync] command`'s contract, deliberately. `mailsync.sh` already
documents why an honest exit status matters: a `0` from a failed sync makes the
application report success for work that never happened.

The application names no particular MTA. msmtp is the obvious thing to point it
at and is not required.

**Two properties are security-relevant.** The command string is split into an
argument list and run without a shell, so nothing in a message body, a
recipient address or a display name can reach `sh`. And **no message content is
ever placed in an argument**: recipients come from the message's own headers,
which is what `-t` means in the example, not from the command line.

### An account that has no `send_command` is receive-only

Not a separate key. The capability *is* the command's presence, so a
receive-only account is defined by omitting the same key that a sending account
sets. There is nothing to keep in step and nothing to contradict.

One of this user's five accounts is receive-only on purpose. It gains no
configuration, which is the whole point: the shape is expressed by omission.
(`listsonly` stands in for it below.)

On a message that arrived at such an account, `reply`, `reply_all`,
`reply_no_quote` and `forward` are **disabled**, and `MessageView` shows a
ribbon saying why and how to change it, naming the account:

> This account is receive-only. Add `send_command` to
> `[account.listsonly]` to send from it.

The ribbon is a widget in `MessageView`'s layout, **not** markup inside the web
view. Composing HTML from configuration into the one document that renders
input from strangers is the wrong direction, and the header row is already a
widget for the same reason.

Forward is disabled along with reply, rather than staying enabled with a
substituted From. One rule ("this account cannot compose") is easier to explain
than two, and the escape hatch is `save_message` (below), which writes the raw
message to a file that can then be attached to a new message from an account
that can send.

`compose` is disabled only when **no** account can send. An installation with
no `send_command` anywhere is a valid read-only installation and is not warned
about.

### The composer is a separate top-level window

`ComposeWindow`, a `QMainWindow`, one per draft, several open at once.

A modal dialog cannot consult another message while writing, which is most of
what replying is. Taking over the message pane fights the pane that exists to
show what is being replied to.

**No geometry restore.** `CLAUDE.md` records what `saveGeometry` does under a
tiling compositor: it stores `normalGeometry`, the compositor owns the tile, and
the restore is correct while looking broken. A whole session went into that
once. The composer opens at a sensible default size and the compositor places
it.

### Drafts autosave to the account's drafts folder

Every account already configures `drafts`, and notmuch indexes it, so the
destination was settled before this design started.

**30 second debounce, write only when the content changed since the last
write.** The previous revision is unlinked on each rewrite, because Maildir has
no in-place edit and drafts would otherwise accumulate one file per pause.

The cost, stated plainly: every autosave produces a Maildir write that mbsync
uploads. Thirty seconds and a dirty check is what keeps that to a few revisions
per message rather than dozens. The benefit is that a draft is visible to
neomutt, to the server and to a phone, which is the point of putting it there
rather than in a local scratch directory.

A composer whose autosave **failed** shows it. It must not interrupt typing and
it must not silently succeed, because the quit path's honesty depends on
knowing what is actually on disk.

### The body is markdown, rendered by cmark-gfm

The editor holds plain text and what is typed is markdown. That source is what
the `text/plain` part carries, unmodified; the `text/html` part is generated
from it.

**Plain-text storage does not mean a bare text box.** The two are separate
decisions and it is worth stating the second explicitly, because "the editor is
plain text" reads as "you are on your own with the syntax". The composer carries
a formatting toolbar with `Ctrl+B`-style shortcuts, described below.

**cmark-gfm, not a hand-written parser for a limited set.** A three-rule parser
and a real markdown parser share no code, so the first is deleted entirely when
the second arrives. The parser accepts CommonMark from the first commit; what is
"limited" initially is the set of affordances and help text the composer
advertises, not what it can parse.

**cmark-gfm rather than cmark**, for autolink. Under plain CommonMark a bare
`https://example.org` in a mail body is not a link, and a bare URL in mail is
expected to be clickable.

Extensions enabled: **autolink, strikethrough, tasklist**. Tables are off: they
render badly across mail clients regardless of who generates them. Tagfilter is
off because raw HTML is already suppressed wholesale.

Raw HTML in the input is refused (`CMARK_OPT_SAFE`). The body is the user's own
text, but a body that can inject markup into its own generated HTML part is a
sharp edge with no upside.

Known ceiling: a tasklist renders as `<input type="checkbox" disabled>`, which
many mail clients strip, so those recipients see the list item with no marker.
The plain part still shows `- [ ]` correctly, so nothing is lost.

**It is the only new dependency, and it is cheap.** `cmark-gfm` ships in stock
Slackware (`cmark-gfm-0.29.0.gfm.13-x86_64-3`, verified 2026-08-20) with a
pkg-config file, so it is a `pkg_check_modules` line beside the one GMime
already has, and the SlackBuild needs no `REQUIRES` entry, which lists only
non-stock dependencies. Note the version: cmark-gfm tracks an older CommonMark
base than plain cmark (0.31.2 here), which is a real if small staleness cost
accepted in exchange for autolink.

Neither Qt's `QTextDocument::setMarkdown` nor plain cmark was chosen. Qt's
markdown is a display facility whose `toHtml()` emits markup styled for
`QTextEdit`, which would need unpicking before it is fit to send: the same
throwaway problem one level up.

### The composer has a formatting toolbar

Each button is a **text transformation over the markdown source**, not
rich-text editing. Nothing about the buffer changes: it stays markdown that the
user can also type by hand.

| Button | Wraps in | Shortcut |
|---|---|---|
| Bold | `**` | `Ctrl+B` |
| Italic | `*` | `Ctrl+I` |
| Code | `` ` `` | `Ctrl+`` ` `` |
| Strikethrough | `~~` | none |
| Link | `[text](url)` | `Ctrl+K` |
| Quote | `> ` per line | none |

**Selection-aware.** With a selection, the tokens wrap it and the selection is
preserved. With none, the pair is inserted with the cursor between them, so
typing continues inside. Quote is line-based rather than a wrap, applying to
every line the selection touches.

**These shortcuts do not touch `KeyMap`.** They belong to the composer window,
which is a separate shortcut scope, so `Ctrl+B` here does not consume `Ctrl+B`
from the main window's map and does not participate in the
every-action-has-a-shortcut rule. Keeping the two namespaces apart matters for
item 132.

**No live syntax highlighting** in this design. A `QSyntaxHighlighter` colouring
`**bold**` in the editor is standard Qt and needs no dependency, but it has to
agree with the markdown grammar about nesting and about code spans suppressing
what is inside them, which is the sort of thing that looks nearly right and
stays annoying for years. It is a follow-up, better judged after living with the
toolbar.

### Whether the HTML part is sent is per-message

`sendHtml` decides whether the message is `multipart/alternative` (plain +
HTML) or `text/plain` alone. Both are built from the same source, so this is one
output branch rather than two editors.

The composer always shows the toggle and it is **never remembered**. What varies
is the seed:

- **New, Forward** seed from `[compose] send_html`.
- **Reply, Reply-all** seed from whether the original carried a `text/html`
  part, ignoring the config value.

The reply seed is evidence rather than inference: an HTML part in the original
is a fact about the sender's software, not a guess about their taste. Since it
seeds a visible toggle, a wrong seed costs one keystroke.

No per-recipient memory. That is an address-book feature and item 72 (khard) is
already queued behind this one.

The toggle is labelled for what it does, a formatted copy riding along with the
plain text, rather than "HTML", which reads as an either/or that it is not.

### Quote position is configuration, quoting on or off is a gesture

`[compose] quote_position = above|below`, default `above`.

Whether to quote at all is per-message, and it is decided by **which action was
invoked**: `reply` quotes, `reply_no_quote` does not. The quote is inserted or
not when the window opens, and after that the buffer is text the user owns.

There is deliberately no live toggle that inserts and removes the quote while
editing. Tracking "my text" and "the quote" as separate pieces to make a toggle
reversible is machinery for a case that is answered by closing the composer and
reopening it, or by deleting the quote by hand.

### MIME is built by GMime

Already a dependency, already linked, already how this application parses mail.

The alternative is assembling RFC822 by string, which means reimplementing RFC
2047 header encoding, quoted-printable for accented bodies, boundary
uniqueness and line-length limits. This user writes Italian; a body containing
`è` is every message, not an edge case. A bug there produces mail that looks
correct locally and arrives as mojibake.

**One built message object serves three consumers**: the autosaved draft, the
bytes on the send command's stdin, and the sent copy. A draft is therefore
byte-identical to what would be sent.

The include-order rule applies to every new file that touches GMime: gmime
headers before any Qt header in the same translation unit, because glib
declares a struct field named `signals`.

### Sending runs behind a cancellable delay, and does not queue

**Send opens a popup that owns the whole operation**, from a cancellable
countdown through to completion. It is modelled on Gmail's undo-send, and it is
what settles what "cancel" can mean here.

```
Sending in 5...        [ Undo ]     countdown running, Undo live
Sending...             [ Undo ]     send_command running, Undo disabled
Filing sent copy...                 DraftStore writing to the sent folder
Removing draft...                   the draft revision is unlinked
                                    popup and composer both close
```

**The delay is where cancelling is safe, and it is the only place it is.**
Nothing has reached a server during the countdown, so Undo means genuinely
nothing happened. Killing `send_command` once it is running does not: the
message may have been handed to the server in full before the kill, so the
result is an *unknown* send, which is worse than either clean outcome. Undo
therefore disables itself the moment the command starts, and there is no cancel
after that.

This is what the delay buys in practice, and it is the case the user named:
pressing Send and immediately noticing the missing attachment. Undo returns the
composer exactly as it was, editable, popup gone, nothing sent.

**In a popup rather than the composer's status bar.** A countdown nobody notices
is a countdown that does not work, and the objection to status bars is precisely
that they do not catch the eye. The same reasoning that puts a failed sent copy
in a modal puts the undo window in one.

**The popup owns the progress too**, rather than handing over to the status bar
once the countdown elapses: one widget changing state in one place, instead of a
popup vanishing and something else appearing somewhere else.

Two properties it must have:

- **Modal to the composer, not to the application.** Sending from one composer
  must not freeze a second composer or the main window.
- **No close button and no Escape dismiss.** During the countdown a dismissal is
  ambiguous, since it could mean cancel or send now. During the send there is
  nothing to dismiss. Undo is the only control and it disables itself.

The composer's inputs are disabled for the whole operation, countdown included:
body, recipient fields, subject, attachment controls, formatting toolbar and
Send. The message must not change between the user pressing Send and the bytes
being built. Undo re-enables all of it.

`[compose] send_delay_ms` sets the countdown, default 5000. **Zero skips it**
and sends at once, for anyone who finds it irritating.

The progress element is an **indeterminate `QProgressBar`** (`setRange(0, 0)`)
beside a label, which is what `MainWindow` already does for the sync indicator
(`m_syncProgress`) and for the same reason: neither operation has measurable
progress.

**This is the second instance of that pairing, so it becomes a widget class**
rather than a second inline build. Item 134 covers extracting it and converting
`MainWindow` to use it. Building the same thing twice is where a class earns
itself, and "this codebase builds small UI inline" describes what the code does
rather than justifying repeating it. Whether 134 lands before or after this
work, the composer uses the shared widget: if it has not happened yet, this work
creates the class and converts `MainWindow` as part of the same change.

Exit 0 closes the popup and the composer. Non-zero closes the popup and
re-enables the composer with everything intact, showing the command's stderr.

**There is no outbox in this design**, and the reason it is not simply
"deferred" is that it needs its own indicator story. This project has four
closed items (18, 19, 28, 54) about an indicator lying, and one open one (125)
about a spinner that never stops. An outbox adds a queue whose failures surface
long after the user stopped thinking about the message.

**The seam is designed in.** `MessageSender` takes a built message and an
account and returns a result. It knows nothing about composers. An outbox is
built around that funnel by calling it from a drain loop; nothing in the
composer needs to change. That is the whole reason it is a separate unit rather
than a method on the window.

### The sent copy is written locally

After a successful send, the same bytes are written to `<maildir>/<sent>/cur/`
with the Maildir `S` flag. The next `notmuch new` indexes it.

An account with `send_command` but no `sent` key sends correctly and files
nothing, with a startup warning. That is the case for a provider whose own SMTP
files sent mail server-side, and it needs no second key to express.

**A failed sent-copy write is never reported as a send failure.** The message
went. Reporting otherwise makes someone send it twice.

### Nothing here calls `notmuch new`

Drafts and sent copies become visible on the next sync, cron's or the user's.
No write path needs the notmuch write lock, and the read-only-by-default rule
in `CLAUDE.md` is untouched.

## Architecture

Four new units. Three of them have no widgets and are tested without a painter,
in the manner `SearchTerm`, `CardLayout` and `MimeParser` already are.

```
ComposeWindow  (QMainWindow, one per draft)
 ├ recipient fields, subject, body editor, attachment bar
 ├ the send-html toggle
 ├ the 30s dirty-debounce autosave timer
 └ composes the three below; contains no MIME and no process logic

MessageBuilder (GMime + cmark-gfm)   pure construction, no I/O beyond attachments
MessageSender  (QProcess)            the one send funnel; the outbox seam
DraftStore     (Maildir writes)      drafts and sent copies; same operation, two folders
```

Existing units touched: `Config` (new keys and their validation), `KeyMap` (six
actions), `MessageView` (the receive-only ribbon), `MainWindow` (the actions,
the account-resolution rules, the composer registry, the quit path).

**The composer never touches `NotmuchWorker`.** It reads its context from the
database once at open time through the existing worker, then works entirely in
files. No new worker signals, no new generation counters.

The boundary that matters: `ComposeWindow` is the only unit that knows about
widgets. A composer bug and a MIME bug are found in different files.

## Data flow

Two structs cross boundaries, in `types.h` beside the existing ones.

**`ComposeContext`**, what opens a composer. Built by `MainWindow`, consumed by
`ComposeWindow`.

| field | meaning |
|---|---|
| `accountKey` | which account sends, resolved by the rules below |
| `kind` | New, Reply, ReplyAll, Forward |
| `originalPath` | the `.eml` being replied to or forwarded; empty for New |
| `inReplyTo` | Message-ID of the original |
| `references` | the original's References plus its Message-ID |
| `to`, `cc` | pre-filled recipients, the user's own addresses already stripped |
| `subject` | `Re:` / `Fwd:` prefixed, an existing prefix not doubled |
| `quotedBody` | the `>`-prefixed original; empty when the action does not quote |
| `seedHtml` | did the original carry a `text/html` part |
| `attachments` | carried forward for Forward, empty otherwise |

**`OutgoingMessage`**, what the composer produces, consumed by
`MessageBuilder`.

| field | meaning |
|---|---|
| `accountKey`, `to`, `cc`, `bcc`, `subject` | as edited |
| `markdownBody` | the source text, exactly as typed |
| `sendHtml` | the composer's per-message toggle |
| `attachments` | local paths |
| `inReplyTo`, `references` | carried through unchanged |

`In-Reply-To` and `References` are not optional. Without them a reply appears as
an orphan thread in the sender's own client.

**Opening.** Action fires. `MainWindow` resolves the displayed message, builds a
`ComposeContext` **from the database rather than the model**, constructs a
`ComposeWindow`, and registers it so the quit path can see it.

Reading the database rather than the model is the rule Restore already follows,
and `CLAUDE.md` records why: the model's data comes from the query, so a row
whose state has not been re-queried carries stale values. A reply built from a
stale row would carry the wrong recipients.

**Autosave.** 30s idle, content changed → build `OutgoingMessage` →
`MessageBuilder` → `DraftStore` writes to the drafts folder and unlinks the
previous revision.

**Sending.** Send → build → `MessageSender` runs the command with the bytes on
stdin → 0: file the sent copy, delete the draft, close. Non-zero: re-enable,
show stderr, leave the draft.

## Which account sends

The displayed message's own maildir is the strongest available signal and wins
outright for **Reply, Reply-all and Forward**. Mail sent to an address landed in
that address's maildir, so replying from it is what the recipient expects. The
account dropdown is **not** consulted: replying from the All accounts view to a
message that arrived at account B sends from B.

A message can be in more than one maildir, on a list twice under two
addresses, or duplicated across accounts by mbsync, and notmuch returns
several filenames for one message id. Prefer the account matching a recipient in
`To` or `Cc`; failing that take the first. The From field shows the choice, so an
ambiguity resolved arbitrarily is visible rather than hidden.

The reply actions never need a fallback for "the resolved account cannot send",
because on such a message they are disabled and there is no composer.

For a **New message** there is nothing to resolve from:

1. The dropdown's current account, when it is a specific one and it can send.
2. `[compose] default_account`, when set and it can send.
3. `[general] startup_account`, on the same condition.
4. The first account in configuration order with a `send_command`.

The All accounts view falls through to 2. Rule 4 is arbitrary and is the reason
rules 2 and 3 exist.

**The From field is always editable** and lists every sending account, which is
what makes every rule above a default rather than a decision.

## Actions

Six, each needing the five places `CLAUDE.md` enumerates: `knownActions()`,
`defaultBindings()`, the icon table, a menu, and a handler.

| Action | Meaning | Scope |
|---|---|---|
| `compose` | New message | none needed |
| `reply` | Reply to the displayed message, quoted | sender only |
| `reply_all` | Reply to all, quoted | sender + To + Cc, own addresses removed |
| `reply_no_quote` | Reply with an empty body | sender only |
| `forward` | Forward, body quoted inline, attachments carried | none |
| `save_message` | Write the raw `.eml` to a chosen path | any message |

`reply_all_no_quote` is deliberately absent. Six actions is already a large
menu and the combination is reached by deleting the quote.

**Every action acts on the displayed message**, resolved with
`messageScopeFor()` semantics: a thread row means the one message its card
shows, a reply row means itself. Not `threadFor()`. Replying to a thread is
meaningless; a reply answers a message.

`save_message` is never disabled, including on a receive-only account. It is the
escape hatch for that case.

**Provisional key bindings.** All chords; the map has no bare letters, for the
reason `defaultBindings()` records at length.

| Action | Key | Why |
|---|---|---|
| `compose` | `Ctrl+N` | conventional; free |
| `reply` | `Ctrl+Shift+R` | `Ctrl+R` is `restore` |
| `reply_all` | `Ctrl+Shift+A` | `Ctrl+A` is `select_all` |
| `reply_no_quote` | `Ctrl+Alt+R` | the Ctrl+Alt tier, as the thread actions use it |
| `forward` | `Ctrl+Shift+F` | keeps the family on one modifier pattern |
| `save_message` | `Ctrl+Shift+E` | export; `Ctrl+Alt+S` is `spam_thread` |

These are provisional: the user intends to rework the bindings, and
`reply_no_quote` on `Ctrl+Alt+R` is an imperfect fit, since that tier elsewhere
means "wider scope" rather than "variant".

**A new top-level `Message` menu.** Six actions do not belong bolted onto an
existing one, and `everyActionIsReachableFromAMenu()` fails loudly if one is
missed.

**Toolbar: `compose` and `reply` only.** The rest are menu-and-key, which keeps
the no-duplicate-icons rule satisfiable.

## Configuration

```ini
[account.work]
send_command = msmtp -a work -t

[compose]
quote_position = above          ; above | below
send_html = true                ; seeds New and Forward; Reply seeds from the original
autosave_interval_ms = 30000
send_delay_ms = 5000            ; the undo window before sending; 0 sends at once
default_account = work
attachment_warn_bytes = 26214400
```

Every `[compose]` key is optional with the default shown.

**Startup validation**, following the pattern that already warns about an
unresolvable `startup_query`:

- `send_command` present, `sent` absent → sends work, no local copy is filed.
- `send_command` present, `drafts` absent → the composer runs without draft
  protection.
- `default_account` names an account that cannot send → warn, fall through.
- No account can send → **no warning**; a read-only installation is valid and
  the compose actions are simply disabled.

**Deliberately not configurable:** where drafts and sent copies go, and the
markdown dialect.

## Error handling

**The surface is chosen by consequence, not by convenience.** A warning the user
does not see is the same defect as an indicator that lies, and the status bar
does not catch the eye. The rule for this design:

| Consequence | Surface |
|---|---|
| Silent divergence the user would not otherwise discover | modal dialog |
| Something needing attention while they are mid-task | persistent banner, does not fade |
| Routine, self-correcting, or already visible | status bar, or nothing |

The status bar is for what is already obvious. Nothing whose failure the user
would learn about months later belongs there.

**Send failed** (non-zero exit). Composer re-enabled intact, stderr shown in a
pane below the body, in the shape `MailSync`'s log pane already has. The draft
stays. No retry loop.

**Exit 75 has no special meaning here.** `MessageSender` has exactly two
outcomes. Item 125 is open precisely because the *sync* path treats 75 as
neither success nor failure and hangs on it; that exists because `mailsync.sh`
contends for a lock, and there is no lock here. This is recorded so the two
paths are not later "harmonised".

**Command missing or unrunnable** (`QProcess::FailedToStart`). Reported as a
failure naming the command, since a typo'd path is the likely cause.

**Draft write failed.** A **persistent banner** in the composer, not a modal and
not a status-bar line that fades. A modal mid-sentence is hostile while the user
is typing, but the warning must survive until it is dealt with, because the quit
path's honesty depends on it: case 3 below escalates exactly this state to a
dialog on the way out.

**Sent copy write failed after a successful send.** A warning saying exactly
that. Never a send failure, never an offer to resend.

The staged progress display makes this visible rather than confusing: the
failure arrives while the status bar reads "Filing sent copy...", so the user
can see the send stage already passed. The composer still **closes**, because
the message went and holding a composer open for a message already sent invites
sending it twice.

**It is reported with a modal dialog, not a status-bar line.** This is the one
failure in the whole design that produces a silent divergence between what the
recipient received and what the local archive shows, and a status bar does not
catch the eye. Nobody discovers a missing sent copy by noticing a line that
appeared for a few seconds; they discover it months later by looking for a
message that is not there. The dialog names the account and the folder it could
not write to.

**An attachment vanished between attaching and sending.** Send is refused before
the command runs, naming the file. Checked at build time, not at attach time.

**Quitting with composers open.**

1. Every composer clean → quit directly, no dialog.
2. Any composer with unsaved edits → **one** dialog, whatever the count:
   *"N messages are still being composed."* with `[Save drafts and quit]`,
   `[Discard and quit]`, `[Cancel]`. It applies to all of them; there is no
   per-draft choice, because three modals in a row is worse than a coarse
   answer.
3. Any composer whose last autosave **failed** → a dialog that says so, naming
   it, offering a retry. The risk here is different: in case 2 nothing is lost
   by saving, in case 3 saving is what is already not working, so the dialog
   states plainly that quitting loses that text.

"Discard and quit" discards **unsaved edits**, not drafts. A draft already
autosaved stays in the folder. The wording must not read as "delete my three
messages".

**Not handled, deliberately:** network errors, authentication failures, server
rejections. Those belong to `send_command` and its stderr is shown verbatim.

## Testing

**`test_messagebuilder`** carries the bulk, being pure. Fixture-driven like
`test_mimeparser`, asserting on the **generated bytes** rather than on a
round-trip through `MimeParser`, since a builder and a parser that agree can be
wrong together.

Cases: `multipart/alternative` when `sendHtml` is on and `text/plain` alone when
off; `multipart/mixed` nesting with attachments; each enabled extension
rendering, and tables and raw HTML **not** rendering; RFC 2047 encoding of a
non-ASCII subject and display name; quoted-printable for an accented body;
`In-Reply-To` and `References` carried; `Re:` and `Fwd:` not doubling.

**`test_messagesender`** uses stub commands, not msmtp: one exiting 0, one
exiting non-zero with stderr, one that does not exist. The stub writes stdin to
a file the test reads back, proving the message arrived intact. Asserts exactly
two outcomes.

**The send delay needs its own test, and the property that matters is a
negative one**: that Undo during the countdown leaves the stub command **never
run at all**. A test asserting only that the composer reopened would pass
against a design that ran the command and threw the result away, which is the
whole failure the delay exists to prevent. Assert the stub wrote no file.

Also: `send_delay_ms = 0` skips the countdown, and Undo is disabled the instant
the command starts. Drive the countdown with a short delay rather than waiting
five seconds in a test.

**`test_draftstore`**: Maildir filename validity and uniqueness, the previous
revision unlinked, the dirty check skipping a write, and an unwritable directory
reporting rather than throwing.

**`test_composecontext`**: recipient derivation is where the subtle bugs live
and it is pure logic, so it is tested apart from the window. All five of the
user's addresses stripped from a reply-all; every account-resolution rule
including the multi-maildir ambiguity; subject prefixing.

**The formatting toolbar** is tested through its transformations, not through
the widget: wrap with a selection, insert with none, quote applying per line,
and the cursor landing between the tokens in the empty-selection case. That last
one is the property a user notices immediately when it is wrong, and it is
invisible to a test that only compares the resulting text.

**In `test_mainwindow`**: action enablement against a receive-only account, the
ribbon appearing, and `everyActionIsReachableFromAMenu()` covering the six new
actions for free. `WorkerBackedWindow` gains a knob for writing an account
without `send_command` rather than a new fixture class.

**Not tested, and stated so nobody tries.** Composer window geometry: the
offscreen platform returns an identical frame for a correct restore and a broken
one, verified in a standalone program. The actual send: there is no MTA and
there will not be one in CI. How the HTML part renders in any real mail client:
that is a hand test and belongs to the user.

## Follow-up items

Deliberately out of scope here, each worth its own backlog entry.

- **An outbox.** Queue and drain rather than blocking. The seam is
  `MessageSender`. Needs its own indicator story before it is built.
- **Inline images.** `cid:` references from the HTML part, `multipart/related`
  nested inside the alternative. Wanted by the user; the most nesting-heavy part
  of MIME assembly, and markdown offers no natural syntax for it.
- **Save a message as an attachment.** Attach a `message/rfc822` part directly,
  rather than saving to a file and re-attaching it. The manual route through
  `save_message` exists from the first commit.
- **Configurable markdown dialect and extensions**, in the shape Hugo's
  configuration uses.
- **Live markdown syntax highlighting in the composer.** A
  `QSyntaxHighlighter` over the editor, so `**bold**` reads as bold while the
  buffer stays plain markdown. Deliberately after the toolbar: the grammar
  agreement it needs is the expensive part, and the toolbar is what makes the
  feature usable.
- **Review "every action has a shortcut".** `everyActionHasAShortcut` was
  written when the action list was short. Six more actions takes the count past
  the point where a chord for everything is useful, and each new action consumes
  one whether or not anyone would press it. The replacement is the shape
  `everyActionIsReachableFromAMenu()` already has: every action reachable from a
  menu, with shortcuts a chosen subset. Not done here, because changing it while
  adding six actions confuses two changes.
