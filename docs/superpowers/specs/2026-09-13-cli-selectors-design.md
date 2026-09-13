# Launching qtmaildir at an account, thread or message

Date: 2026-09-13
Status: approved, not yet implemented
Backlog: item 200

## Problem

`qtmaildir` accepts no arguments beyond `--version` and `--help`. Another
program that knows which message it cares about, a notification, a script, a
sidecar like item 194's, has no way to say so: it can launch the client and
that is all.

The user's note asks for `--account`, `--thread` and `--message` "so that
another app can launch qtmaildir opening that account's inbox or a certain
message/thread".

## Verified context

Established by reading the code on 2026-09-13, not assumed.

- **Argument handling is a `strcmp` loop.** `main.cpp:38-66` walks `argv` for
  `--version`/`-v` and `--help`/`-h`. Both answer and `return` BEFORE
  `QApplication` is constructed, which is deliberate and documented there:
  `--version` must work on a machine where the GUI cannot open.
- **There is no single-instance mechanism.** No `QLocalServer` or
  `QLocalSocket` appears anywhere in `src/`. Two launches are two processes
  against one notmuch database, and notmuch permits one open handle per
  process.
- **`Qt6::Network` is not linked.** `CMakeLists.txt:20` sets the component list
  to `Widgets Svg WebEngineWidgets` (plus `Test` for the suite), and
  `src/CMakeLists.txt:58` links exactly those. A socket adds a component, which
  is a packaging fact for the SlackBuild in `my-slackbuilds`, not just a CMake
  line.
- **The startup path already composes an account with a query**, at
  `mainwindow.cpp:624-657`, including the distinction this design must not
  break: a generated filter comes back from `resolvedQuery()` ALREADY scoped and
  takes `AccountScope::AlreadyScoped`, while a saved query states its own scope
  and takes `AccountScope::Apply`. Getting it wrong is silent in both
  directions, double-scoping one and leaving the other unscoped.
- **`recoverStaleThread(threadId, messageId)` already does the whole of what
  `--thread` and `--message` need**, at `mainwindow.cpp:5002`. It runs
  `thread:<id>`, remembers the target across the two queued round trips the
  load takes, expands the thread when its row arrives
  (`applyPendingRecovery()`, `mainwindow.cpp:5148`, called from the
  query-complete handler at `mainwindow.cpp:3670`), selects the message once
  the replies land, and falls
  back to the root row when the message has gone. Item 91's double-click reuses
  it outright and says so in a comment. **A CLI selector is a third caller.**
- **`announceAction()` / `showTransientStatus()`** (`mainwindow.cpp:5403`) is
  how the window says something happened, with a timer that falls back to the
  query's own result line.

## Decisions

Taken by the user on 2026-09-13.

1. **A second launch steers the running window.** The second process hands its
   arguments over a socket and exits; the running window applies them and
   raises itself. This is what makes the feature useful to the caller the note
   describes, which will usually find qtmaildir already running, and it fixes
   the two-notmuch-handles problem that exists today as a side effect.
2. **Three selectors: `--account`, `--thread`, `--message`.** `--query` was
   considered and dropped: it is the most general and the cheapest, and it is
   also the one with no caller. The query bar already accepts an arbitrary
   query from the only party who would type one.
3. **A selector that matches nothing opens the window normally and says so in
   the status bar.** Not an empty result, which makes a stale link look like a
   broken client; not a refusal to start, which is right for a script and wrong
   for a desktop launch where the user gets no window and no reason.

## Design

### The command line

`QCommandLineParser` replaces the `strcmp` loop, with one constraint that is
not negotiable: **`--version` and `--help` must keep answering without a
`QApplication`.** `QCommandLineParser` itself needs only `QCoreApplication`'s
argument list, which `QCommandLineParser::process(QStringList)` accepts, so the
parse can happen against `argv` before the GUI object exists. The two
early-exit options keep their current place and their current behaviour; the
parser gains the three selectors and nothing else.

```
qtmaildir [--account KEY] [--thread ID] [--message ID]
```

The three compose. `--account work --message '<abc@example.org>'` means what it
says, and the account half is applied before the query runs, exactly as
`startup_account` is.

### Single instance

A `QLocalServer` named under `QStandardPaths::GenericStateLocation`, beside the
UI state file, reached through a helper alongside `MainWindow::uiStatePath()`
so the two paths cannot drift. `GenericStateLocation`, not `StateLocation`, for
the reason already recorded: the latter appends both the organization and the
application name and both are `qtmaildir`.

Startup order, and the order is the design:

1. Parse the arguments. `--version`/`--help` answer and exit here, as now.
2. Try to CONNECT to the socket. If a connection succeeds, write the selectors
   as one payload, wait briefly for the write to flush, and exit 0. This
   process never constructs a `MainWindow` and never opens notmuch.
3. If the connection fails, this process becomes the server: listen, then build
   the window and apply the selectors locally.

**A stale socket file is the failure mode to handle, not to hope about.** A
crash or a kill leaves the socket file behind, and `listen()` then fails with
`AddressInUse` on a file nothing is serving. The recovery is to attempt a
connection first, which step 2 already does: a refused connection on an
existing file proves it is stale, and `QLocalServer::removeServer()` clears it
before listening. Doing it in this order means a live instance is never
removed out from under itself.

**If the socket cannot be created at all, start anyway.** A read-only state
directory or a platform without local sockets must degrade to today's
behaviour, a window that opens and works, rather than to no mail client. Log
it; do not fail.

### Applying the selectors

One entry point on `MainWindow`, taking the three values, called from two
places: the local path at startup, and the socket handler when a second launch
arrives. The two must go through the same function or they will drift, which is
the lesson every other pair of paths in this file has already taught.

- **`--account KEY`** sets the account dropdown, through the same
  `findData()`/`setCurrentIndex()` the startup account uses. `Config` already
  validates the key against the configured accounts and clears an invalid one,
  so the miss is detectable rather than silent.
- **`--thread ID`** calls `recoverStaleThread(id, QString())`. The empty
  message id is already meaningful to that function: land on the root row,
  which IS the thread's first message.
- **`--message ID`** resolves the message's thread and calls
  `recoverStaleThread(threadId, messageId)`. The thread is what the user wants
  to see, with that message selected: an `id:` query on the message alone shows
  one card out of its conversation, which item 91 settled is the wrong reading
  of "open this message".

**Resolving a Message-ID to a thread id is the one piece that does not exist.**
It is a worker question, since only the worker touches notmuch, and
`NotmuchWorker` already runs `id:"<...>"` at `notmuchworker.cpp:985`. It is one
more query with a queued reply, and the reply is what calls
`recoverStaleThread()`. Nothing about it is novel; it is listed here because it
is the only new round trip in the design.

**Every id goes through `SearchTerm::quote()`**, like every other query this
application builds. A Message-ID comes from outside the process, so it is
untrusted input in the ordinary sense. Note the existing code at
`mainwindow.cpp:5011` and `:6798` interpolates ids unquoted because they came
from notmuch itself; an id from `argv` did not, and the difference is the whole
reason `SearchTerm` exists.

### Raising the window

`show()`, `raise()` and `activateWindow()`, plus `setWindowState()` clearing
`Qt::WindowMinimized` so a minimized window comes back. **Under Wayland this is
a request, not a command**, and Hyprland may honour it as a focus hint or
ignore it by policy. That is the compositor's decision and not a defect to
chase: the selectors still apply and the window still shows the right thing,
which is the part that must work. Do not add workarounds for focus stealing
prevention.

### When a selector matches nothing

The window opens on its configured startup view and
`showTransientStatus()` names what missed:

- an account key naming no configured account,
- a thread id matching no thread,
- a Message-ID matching no message.

One message per miss, naming the value, so a caller passing a stale id can be
debugged from the client rather than from the caller. The account case is
detectable before any query runs; the other two are known when the query comes
back empty.

**A second launch that matches nothing must still raise the window.** The user
asked for it and something went wrong with the selector; showing them nothing
at all is the worst of both.

## Testing

What has a right answer and is worth building, per `AGENTS.md`.

- **The parse**: each selector, the three composed, the two early-exit options
  still answering, and an unknown option not crashing. Pure over `QStringList`,
  so it is testable without a window.
- **The quoting**: a Message-ID containing a quote, a backslash, whitespace or
  a `)` produces the query `SearchTerm` promises, not a malformed one. This is
  the security-relevant assertion and notmuch cannot check it for us, since it
  parses garbage happily and matches zero (`searchterm.h:30-35`).
- **Message-ID to thread id**, in `test_notmuchworker`, against the throwaway
  database: a known id resolves to its thread, an unknown one resolves to
  empty.
- **Applying the selectors**, in `test_mainwindow` with `WorkerBackedWindow`: an
  account key moves the dropdown, a thread id lands on that thread's row, a
  message id lands on that message's row inside its thread. Wait on observable
  state with `QTRY_VERIFY_WITH_TIMEOUT`, never on a fixed `qWait`, and remember
  `rowCount()` on a thread row is 0 until expanded.
- **The miss path**: an unknown account key leaves the dropdown on its
  configured value and the window on its startup query rather than on nothing.
- **Stale socket recovery**: a socket file with nothing behind it does not stop
  the window from starting. Testable against a `QTemporaryDir` state path.

**Not tested, handed to the user to look at**: whether the window actually
raises and takes focus under Hyprland. It is compositor policy, the offscreen
platform cannot see it, and a test there would pass whatever the code does.

## Constraints

- **`Qt6::Network` joins the component list**, in the root `CMakeLists.txt` and
  in `src/CMakeLists.txt`. It is already an installed part of Slackware's
  monolithic `qt6` package, so the SlackBuild's dependencies do not change, but
  the `.SlackBuild` lives in `my-slackbuilds` and the addition should be
  verified there rather than assumed here.
- **No network protocol work.** A `QLocalServer` is a unix domain socket
  between two copies of this program, not a network client. The rule
  `AGENTS.md` states is about IMAP and SMTP; this does not touch it.
- **The socket is a trust boundary, and a small one.** It is owned by the user
  and lives in their own state directory, so the payload comes from a process
  running as them. That is not a reason to skip validation: the payload is
  parsed as three optional strings, anything else is ignored, and every id
  still goes through `SearchTerm::quote()` on the way to a query. A payload
  size cap belongs here too, since a local socket will hand over whatever it is
  given.
- **Five places for an action still applies if any of this gains one.** As
  designed it does not: the selectors are startup input, not user-triggered
  actions, and nothing appears in a menu. If a "raise" action is ever added it
  takes the full treatment in `KeyMap::knownActions()`, the icon table and a
  menu.
- **Every user-facing string is translatable**, including the status-bar misses
  and the `--help` text. The option NAMES are wire format and are never
  translated; their descriptions are prose and are.
- **`--help` gains three lines** and must stay accurate, since it is the only
  documentation a caller will read. The README's usage section gains the same.

## Sizing

**M**, and the halves are uneven.

The selectors are the small half, because `recoverStaleThread()` already exists
and is already proven by two callers. The single-instance socket is the real
work: the connect-first ordering, stale socket recovery, the degrade path when
no socket is possible, and one new worker round trip for the Message-ID lookup.

They could ship separately, the parser and startup selectors first and the
socket after, and the spec is written so that split is available. It is not
recommended: the selectors without the socket answer the note's actual use case
("another app can launch qtmaildir") with a second window, which is the
behaviour the user rejected.
