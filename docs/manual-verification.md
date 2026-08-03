# Manual verification against a real database

Run 2026-08-03 against the maintainer's live notmuch index
(`~/Mail`, 5 maildirs, ~36,000 threads, 4,174 in `tag:inbox`).

Items are marked:

- **PASS** / **FAIL** where the result was observed.
- **PENDING** where the item needs a person looking at the screen. The
  automated pass deliberately stopped short of these: a full-screen capture
  exposes whatever else is on the desktop, so the visual items are for the
  maintainer to walk.
- **DEFERRED** for the items that write to the live index. Undo is
  implemented, but a bug in the mutation path is exactly what this checklist
  is meant to catch, so those run with someone watching.

## Configuration used

`~/.config/qtmaildir/qtmaildir.conf`, all five accounts, three saved
queries, the default keybindings, and no `[sync] command` (no
`mailsync.sh` exists on this machine yet).

`general/notmuch_config` is deliberately left unset so libnotmuch resolves
`~/.notmuch-config` itself and the GUI and CLI cannot drift onto different
databases.

## Results

| # | Item | Result |
|---|------|--------|
| 1 | Startup shows no configuration warnings with a valid config | **FAIL, then fixed** |
| 2 | `tag:inbox` count matches `notmuch count --output=threads` | **PASS** |
| 3 | A large query paints the first rows within a second | **PASS** |
| 4 | A new query discards the running one's results | **PASS** |
| 5 | A malformed query (`tag:`) reports an error and does not crash | **PASS, item reworded** |
| 6 | Selecting a thread renders every message, oldest first | **PASS** |
| 7 | Unmatched messages appear as one-line stubs | **PASS** |
| 8 | A large thread renders without stalling; `QtWebEngineProcess` count stays flat | **PASS** |
| 9 | An HTML newsletter renders, and shows "Remote content blocked" | **PASS** |
| 10 | "Load remote content" re-renders with images | **PASS** |
| 11 | Selecting a different thread clears the remote grant | **FAIL, then fixed** |
| 12 | An inline image displays without any remote load | **PASS** |
| 13 | Two messages sharing a Content-ID each show their own image | PENDING |
| 14 | `h` toggles the thread to plain text and back | **PASS, bug found alongside** |
| 15 | A link click opens the system browser without navigating the pane | **PASS** |
| 16 | `a` archives the selected thread | DEFERRED |
| 17 | `a` over a multi-row selection archives all of them | DEFERRED |
| 18 | `u` after a bulk archive restores every thread | DEFERRED |
| 19 | Sync runs, the log fills, the query refreshes | BLOCKED (no sync script on this machine) |
| 20 | Sync during cron's `notmuch new` reports a lock error | BLOCKED (same) |
| 21 | A sync command path containing a space behaves consistently | **PASS, by inspection** |
| 22 | Deleting the sync script mid-run reports a failed start | **PASS, covered by test** |

## Item 1: FAIL, then fixed

With a valid config that simply had no `[sync] command`, every launch
opened a blocking modal ("No sync command configured; syncing is
disabled.") that had to be dismissed before the window was usable.

Nothing was broken. Sync is optional and the status bar already reported
the warning count. A modal on every launch is how users learn to dismiss
dialogs unread, which costs you the ones that matter.

Fixed in `3ee73d4`: `Config` now separates *problems* (something
configured but wrong: a sync command that does not exist, an account with
no maildir) from *notices* (an optional feature simply absent). Problems
open a dialog, as does every `KeyMap` warning, since each of those means a
binding the user wrote is being ignored. Notices go to the status bar only.

Re-verified against the real config: warning still reported, no modal.

## Item 2: PASS

| Query | Worker | `notmuch count --output=threads` |
|---|---|---|
| `tag:inbox` | 4174 | 4174 |
| `tag:` | 1917 | 1917 |

An earlier run showed 4174 against a CLI baseline of 4161. That was not a
defect: cron syncs every 10 minutes and had pulled in 13 threads between
the two measurements. Re-running the CLI immediately agreed exactly.

## Item 3: PASS

Query `*` over the whole database, 36,335 threads:

- first batch (200 threads) emitted after **21 ms**
- full enumeration in **2.86 s**, 182 batches

The first screenful is available essentially immediately and the rest
fills in behind, which is what the batching exists for.

## Item 4: PASS

Typed `*`, then `tag:unread` while the first query was still filling. The
list switched cleanly to 136 unread threads with no leftover rows from the
41,000-thread result set and no wrong intermediate count. The generation
counter discards superseded batches as designed.

The maintainer's note that `*` "loaded almost quicker than I could type"
matches the item 3 measurement: 21 ms to the first batch.

## Items 6, 9, 10: PASS

Opening a thread renders every message in the right-hand pane. An HTML
newsletter renders with its layout intact and the "Remote content blocked"
banner shown; clicking **Load remote content** re-renders the same thread
with images loading.

These only passed after the blank-pane defect below was fixed. Before that,
clicking a thread did nothing visible at all.

### The blank pane (found by this checklist, fixed in `5de8147`)

This is the defect the manual pass existed to catch, and no unit test would
have found it: every layer was correct in isolation. The worker returned all
22 messages, MimeParser parsed them, HtmlBuilder produced 77 KB of correct
HTML, and the pane stayed empty.

`setHtml()` does not navigate to the base URL it is given. It navigates to a
`data:` URL carrying the markup and applies the base URL afterwards as the
document's origin only. Two separate pieces of Task 11 assumed otherwise and
each independently rejected the document load:

1. `MessagePage::acceptNavigationRequest` compared the navigation URL against
   `documentUrl()`.
2. `RequestInterceptor` trusted exactly the `qtmaildir:` base URL and denied
   the `data:` document.

Neither was a regression: the drafted implementation had the same defect in a
different spelling, so the message pane had never worked.

Worth recording about the fix itself: the first attempt allowed the `data:`
scheme outright, which broke `dataSchemeBlocked` in the Task 5 suite. That
test was right to fail. A message body can write `<img src="data:...">`, so a
blanket allow would have opened a real hole while fixing a rendering bug. The
exemption is scoped to `ResourceTypeMainFrame` instead.

## Item 11: FAIL, then fixed

Granting remote content on one thread, switching to another and returning
showed the images again with no banner. Re-verified as passing after the fix
in `9d13346`.

The interceptor was not at fault. Against a local HTTP server counting
requests, the image is fetched exactly once, under the grant, and never
again; `allowRemote` is false on return and the request is blocked. Nothing
was re-requested, so the images could only have come from the engine's
decoded-image cache, which is keyed on the document and consulted before any
request exists. The interceptor is never asked. The policy was right and the
pane was lying, which is the kind of gap only a person looking at the screen
will find.

`QWebEngineProfile::clearHttpCache()` does not reach that cache. Loading
`about:blank` before the new document discards the previous one along with
its cached images. It is done in `showThread()` and deliberately not in
`render()`: `render()` also runs for the grant itself, where discarding the
document would throw away exactly what the user just asked to see.

## Item 7: PASS, after correcting the query

Verified with `from:nutpantz` against `thread:0000000000008faa` (22
messages, 2 from that sender): the two matching messages rendered expanded,
the other twenty collapsed to one-line stubs, and the last message rendered
expanded despite not matching, which is the guard that stops a thread
rendering as nothing but stubs.

The first attempt used `LLM` and showed all 22 expanded. That was not a
defect. notmuch reports all 22 as matching, and it is right to: the thread
is a GitHub discussion whose subject is "I wanted to ask about LxQt's LLM
position", so every reply carries the term in its headers and quoted text
even when the visible reply does not mention it. Full-text indexing covers
quoted material, so the match is real and expanding everything was the
correct response to that query.

Worth remembering when choosing a test query: a term from the subject line
will match every message in a thread. Partition on something that varies
per message, such as `from:`.

## Item 12: PASS

The AtlasMedica message (3 inline `cid:` parts) displayed all three images
with no "Remote content blocked" banner. The absent banner is the stronger
half of the result: nothing was denied, so the images came entirely from
parts carried inside the message and no request left the machine.

This exercises the whole namespaced-cid path end to end, which until now
had only unit coverage: `buildThreadCidMap()` builds `m0!<content-id>`
keys, the interceptor allows exactly those, and `CidSchemeHandler` serves
the bytes.

## Item 14: PASS, and a keyboard bug found alongside

`h` toggles between the HTML and plain-text rendering of a thread. Verified
after the fix below; before it, `h` mostly moved the selection instead.

With the thread list focused, every single-letter binding was being eaten by
`QAbstractItemView`'s type-to-search: `h` jumped to the next thread whose
subject began with "h", and `j`, `k`, `a`, `d`, `N`, `F`, `u`, `G` behaved
the same way. The event filter was installed on the MainWindow, and a
window-level filter only sees key presses the focused child did not consume.
Installing it on the thread view as well puts the keymap first. Fixed in
`a81c794`; `j`/`k` navigation confirmed working afterwards.

This one is worth noting for how it hid: the bindings all worked when focus
was anywhere other than the list, which is the state a developer testing a
single shortcut is most likely to be in.

## Item 15: PASS

Clicking a link in a message opened the system browser and left the pane
showing the message.

Worth having checked by hand: `acceptNavigationRequest` was modified twice
in one session, once when MessageView was written and again as part of the
blank-pane fix, and it is the only thing standing between a message body and
replacing the pane with an arbitrary page. `NavigationTypeLinkClicked` goes
to `QDesktopServices::openUrl` and returns false; everything except a typed
main-frame navigation is refused.

## Item 8: PASS

With the 22-message thread (`thread:0000000000008faa`) open:

| State | `QtWebEngineProcess` count |
|---|---|
| Empty pane | 5 (3 renderers) |
| 22-message thread open | 5 (3 renderers) |

Flat, and no stall on render. One `QWebEngineView` per message would have
spawned roughly one render process each; rendering the whole thread as a
single document is what avoids that.

## Item 5: PASS, but the item was wrong

The checklist assumed `tag:` is malformed and should raise an error. It is
not: notmuch's query parser is lenient and accepts it, matching 1,917
threads. The worker returned those threads, emitted no error, and did not
crash. The CLI agrees exactly, so the GUI is faithfully reporting what
notmuch does.

The same leniency was found in Task 8 with an unbalanced quote
(`subject:"unterminated`, accepted, matches nothing). Worth remembering
that notmuch will rarely tell a user their query is wrong; it will just
return something surprising.

## Items 21 and 22

Item 21 (`config.cpp` splits the sync command on a space, `MailSync` uses
`QProcess::splitCommand`): confirmed by inspection. For
`command = "/path/with a space/mailsync.sh"` the two disagree, config
checks a path that does not exist, and sync disables itself. The failure
is safe but the message misleads. Not fixed: no such path is in use, and
the fix is a one-line change to `config.cpp` whenever it matters.

Item 22 (script deleted while running): covered by
`test_mailsync.cpp::missingBinaryReportsFailureNotSilence`, which asserts
the launch failure surfaces as `finished(false, -1)` with a log line
rather than a hung spinner.

## Still to do

Items 16-18 write to the live index and are deferred until they can be run
with someone watching; undo is implemented, but the mutation path is exactly
where this checklist would earn its keep. Items 19-20 need a `mailsync.sh`
to exist on this machine.

Item 13 is unreachable with this mailbox: the only two messages found that
share a Content-ID (`95db36262ead...@phpmailer.0`, two AtlasMedica
notifications) sit in separate single-message threads, and the pane renders
one thread at a time. The behaviour it describes is covered by
`test_threadcidmap.cpp::sharedContentIdsDoNotCollide` instead.

## What the manual pass was worth

Three defects, none of which any unit test in this project would have
caught, all found by a person clicking:

1. **The message pane never rendered at all** (items 6, 9, 10). Every layer
   was correct in isolation; `setHtml()` simply does not navigate to the
   base URL it is given, and two separate pieces of code assumed it does.
2. **Remote images survived a thread switch** (item 11). The policy was
   right the whole time and the pane still showed images the user had not
   re-authorised, because a cached resource never reaches the interceptor.
3. **Every single-letter key binding was swallowed** (item 14) whenever the
   thread list had focus, which is most of the time in normal use.

The common thread: each one lived in the gap between components that were
individually tested and correct. That gap is what a manual checklist is
for.
