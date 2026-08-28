# A thread row is the conversation

**Date:** 2026-08-28
**Status:** designed, not built
**Size:** L

## The problem

A row in the thread list means two different things at once, and every
ambiguity the application has about threads comes from that.

Item 66 removed the conversation view, so a thread row's card displays ONE
message. Item 108 then made actions on that row scope to that one message, on
the sound reasoning that what you see is what you act on. Both changes were
right on their own terms. But the row still exists because a THREAD exists: it
is produced by a thread query, it carries a reply count, it expands into
children, and its subject, authors and date describe the conversation rather
than the message.

So a row is a **message** for display and action, and a **thread** for
existence and membership. The user stated the consequence directly:

> as it is today, I don't know how to address a thread, because on one side it
> is a message which happens to have replies, on the other is a discussion, and
> should be handled as such, as a whole.

Four separate questions in one working session turned out to be this one
question wearing different clothes:

- Does reading the displayed message remove the conversation from the Unread
  view? (Answering "yes" made a 44-message thread with two unread replies
  vanish two seconds after it was selected.)
- Does deleting the displayed message remove the conversation from the Inbox
  view? (A shipped test asserts yes; consistency with the above demands no.)
- Whose tags does the card draw, the message's or the thread's? (Items 110 and
  111 answered "the message's first, the thread's second, in two tiers".)
- What does undoing a thread-scoped action cover? (Item 176: it inverts over
  the whole thread while the write touched one message.)

Answered one at a time, they produced four answers that contradict each other.

## The decision

**A thread row stands for the conversation.** It is not a message and does not
carry one.

This reverses item 108 deliberately. That item fixed "the card shows one
message but acts on all of them" by moving the ACTION to match the card; this
fixes the same defect by moving the CARD to match the action. Same fork, other
branch. Item 108's own analysis is still correct about the state it found, and
its work is not wasted: the thread-scoped action bodies it wrote become the
ordinary path.

## Two row kinds

Decided by `ThreadSummary::totalCount`, which the query already carries.

**`totalCount == 1` — a message row.** No expander. One click shows the
message. Every action scopes to it. This is the majority of mail and its
behaviour does not change at all.

The user was explicit that this case must not regress:

> A single email is a thread by concept, yes, but if it has no replies (yet) it
> should be viewed with a single click. The logic has to be able to see the
> difference.

**`totalCount > 1` — a thread row.** It stands for the conversation. It shows
the subject, the participants, the reply count and the THREAD's tags. One click
shows the dashboard (below), never a message. Every action on it is
thread-scoped. Its messages live underneath it and are reached by expanding.

## What a thread row stops carrying

`ThreadSummary::firstMessageId`, `firstMessageTags` and `firstMessagePath` stop
feeding a thread row's identity. Consequences, each of which is a deletion:

- `nodeFor()` no longer seeds `ThreadNode::first` from the summary, and
  `setRootMessageTags()` goes with it.
- The `first.tags` substitution in the thread branch of `data()` goes. A thread
  row's tags are `summary.tags`, notmuch's union, which is simply correct once
  the row is the conversation.
- **Items 110 and 111 are reversed.** The two-tier chip display (own tags first,
  siblings' muted behind them) exists only to reconcile "this card shows one
  message" with "this row is a thread". With the row being the thread, one tier
  is right. `PillOwnCountRole`, `CardLayout::siblingFont()` and
  `CardDelegate::mutedChipColour()` are deleted.

A message ROW keeps everything: `MessageOwnTagsRole` already computes "own"
as a set difference against the parent thread's union, which is exactly the
model this design wants, and needs no change.

The fields stay on `ThreadSummary`. The dashboard and the Sent view still want
to know which message a thread query matched, and the query walk that fills
them is free (measured over 36,615 threads under item 66).

## Actions and scope

**Scope comes from what is selected, never from which menu entry was chosen.**

| Selected | Scope | Label |
|---|---|---|
| Thread row | the conversation | `Mark thread read`, `Archive thread`, `Delete thread` |
| Message row (no replies) | that message | `Mark read`, `Archive`, `Delete` |
| Reply row | that message | `Mark read` |

Labels name their scope, per the user: "Mark Read is fine for a single message,
but right clicking on the thread main row must read Mark thread Read or
something similar". There is precedent: items 99 and 112 already made the
unread action's label depend on the selection, so this is an established
pattern rather than a new mechanism.

**The `Whole thread` submenu is deleted**, and with it the five `*_thread`
action names. Nothing is left to disambiguate once the row's identity decides
the scope.

**Delete and Archive are conversation-level only.** On a reply row they are
ABSENT, not disabled. The user's reasoning:

> I don't think I'd want to be able to remove a single reply from a thread, I
> can mark it with tags, important, spam, etc, I can reply/forward a single
> answer, yes, download that single message, yes, but delete should be
> available only as a conversation.

This is Gmail's model rather than Thunderbird's or mutt's, and it is coherent
with the rest: irreversible actions work on the unit the user can see and
reason about, while reversible per-message things (tags, flags, read state,
reply, forward, save) work wherever they are. It also dissolves the "delete one
reply of five" membership question, which can no longer arise.

`markAllRead` is untouched. It ignores the selection by design and is
thread-scoped by nature (item 108 recorded this).

## Membership

**The union, one rule, no exceptions.** A thread belongs to a view while ANY of
its messages match it. Reading one message of five does not remove the
conversation from Unread; reading the last one does.

Two properties decided during the same session survive, and both are user
decisions rather than implementation details:

- **A row is never evicted while it is current.** The automatic mark-read fires
  two seconds after selection, so evicting on it takes the row out from under
  the user, with a context menu possibly open on it, before they can mark it
  spam or important. The row leaves when the user moves off it. The user's
  framing: "if a selected row still is accessible for me to act on it, the view
  has its meaning and utility."
- **A write the user asked for evicts at once; an automatic one does not.** The
  distinction is who initiated it, not what it does.

A row that starts matching cannot be inserted optimistically, since the model
holds no summary for a thread the query never returned; that case refreshes.

## The dashboard

Shown when a thread row is selected. The user asked for "a recap of the thread,
a well designed dashboard", and rejected the old conversation view, which
listed messages as stubs that could not be expanded and rendered only the last
two.

Blocks, top to bottom:

1. **Header** — subject, then `N people · N days`, and the account.
2. **Tag chips** — the thread's tags. One tier.
3. **Counts** — messages and unread, with a read-progress bar beneath.
4. **Waiting for you** — the unread messages, each as sender, relative time and
   subject. When nothing is unread this block is replaced by a single
   **All caught up** line and the rest of the pane is unchanged.
5. **Activity** — a sparkline over the thread's lifetime, with
   `first → last · busiest <day>` beneath it.
6. **Thread actions** — Mark all read, Archive, Delete.

**Not a web view.** The message pane's `QWebEngineView` exists to render mail
from strangers under a locked-down profile; a dashboard is our own widgets over
our own data, so it is a plain widget and none of that security surface applies
to it. `MessageView` gains a second stacked page and switches between them.
A single-message thread shows the message, never the dashboard.

**Sparkline buckets are always 7**, the span divided into seven, with the real
range printed beneath. A fixed bucket count is what keeps the widget's geometry
testable; a thread spanning five days and one spanning two years cannot share a
bucket size, and the label carries the truth.

**An entry under "Waiting for you" is clickable** and selects that message's
row in the list, expanding the thread if needed. Without it the dashboard is a
dead end at the moment it has just told the user what they have not read.

## Data

One worker entry point, `loadThreadDigest(threadId, generation)`, returning a
plain value struct over queued signals like everything else that crosses the
thread boundary:

```
struct ThreadDigest {
    QString threadId;
    QList<QPair<QString, int>> senders;   // display name, message count
    QVector<MessageRef> unread;           // sender, date, subject, id
    QVector<int> buckets;                 // always 7
    qint64 firstTimestamp, lastTimestamp;
    int busiestBucket;
};
```

Everything comes from the index; no message files are opened, so it costs about
what a thread load costs today. It needs its own generation counter, separate
from the query's: bumping `m_generation` for a digest would discard any thread
load in flight, which is the trap item 119's rule-count work already recorded.

## Testing

- **The two row kinds** are a model-level question and test without a painter:
  a summary with `totalCount == 1` produces a row that reports no children and
  resolves actions to its message; one with `totalCount > 1` reports children
  and resolves to its thread.
- **Membership** needs a thread whose messages DISAGREE about the tag. Two
  messages in the same state answer identically whichever way the code
  resolves them, which is the trap CLAUDE.md records for item 87. Put the
  disagreement under the SECOND thread, so a wrong answer is visible rather
  than accidentally right.
- **The deferred eviction** must be driven through the mark-read TIMER, not by
  calling the send path directly: the deferral keys on the write being
  automatic, so a direct call tests the opposite branch. A bare `MainWindow` in
  a test also runs its startup query against the live index, so a test that
  waits long enough for that to land will see the model repopulate; fire the
  timer with a zero interval and one event-loop turn rather than a `QTRY`.
- **The dashboard** is a widget over a value struct, so its content is
  assertable without rendering: build a `ThreadDigest` and assert what the pane
  shows. Do not count pixels, for every reason under "Rendering probes lie".
- **`ctest -R translations`** must stay clean: the new labels are user-facing
  strings, and a label in an array needs `QT_TRANSLATE_NOOP` with the context
  named on the literal.

## Order of work

1. **Model.** Row kind from `totalCount`; thread rows drop the first-message
   identity; membership by union. The largest single piece, and where the tag
   tier tests change.
2. **Actions.** Scope from the selection, dynamic labels, submenu deleted,
   Delete and Archive absent on reply rows.
3. **Worker.** `loadThreadDigest`, the value struct, its own generation.
4. **Pane.** The dashboard widget, stacked with the message view.
5. **Item 176.** The undo fix, which this makes easier: a thread action's undo
   covering the whole thread is now honestly what the user asked for, so the
   remaining exposure is only the multi-row message case.

This is one coherent change rather than a decomposition candidate. Splitting it
across releases would ship a state where rows mean one thing and actions
another, which is the defect being fixed.

## Upgrading

The five `*_thread` action names are removed. A `[keys]` section naming any of
them refers to an action that no longer exists, and `KeyMap` will report it as
unknown. The replacement is that the ordinary action name now does whatever the
selected row means, so a binding on `toggle_unread` covers both cases and the
thread-specific binding should simply be deleted.

## Open, deliberately

- **Per-sender colours in the dashboard** reuse the account-avatar palette from
  item 169. Whether a participant's colour should be stable across threads is
  not decided; nothing depends on it.
- **Item 168** (Delete hidden when every selected row is already in the trash)
  needs restating in thread terms: a conversation is in the trash when all of
  its messages are. Not part of the core change, and it is a defect if left
  unstated.
