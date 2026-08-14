# Giving `test_mainwindow` a real worker

Resolves backlog item **36**, and produces a reproduction for item **66**.
Depends on item 84, which is already fixed: the constructor no longer raises a
modal, so a `MainWindow` built from a written config file can be constructed at
all.

## What this is

`test_mainwindow` gains the ability to build a `MainWindow` whose
`NotmuchWorker` is pointed at a throwaway notmuch database, so a test can select
a thread and let a real `threadLoaded` arrive. It is then used to write a
failing test for item 66.

**The deliverable is a RED test for item 66.** Fixing that defect is not in
scope. See "Why the fix is excluded" below.

## Why this is smaller than the backlog entry suggests

The entry says the machinery exists and `test_mainwindow` does not use it, which
is true, and implies a hook has to be added to `MainWindow`, which is not.

`MainWindow::wireWorker()` (`src/mainwindow.cpp:1440`) already constructs the
worker from `m_config.notmuchConfig()`, and the constructor already calls it.
`notmuch_config` is an ordinary config key read by `Config::load`
(`src/config.cpp:162`). So a test that writes a `qtmaildir.conf` carrying

```ini
[general]
notmuch_config=/tmp/.../config
```

gets a `MainWindow` whose worker is already pointed at the fixture, through the
shipping code path, with no test-only setter and no `#ifdef`. Nothing in `src/`
changes.

**Note the `[general]` trap** recorded in `CLAUDE.md`: QSettings treats a
section literally named `[general]` as its own fallback section and strips the
prefix, so the key is read as `notmuch_config`, not `general/notmuch_config`.
This is how that key went unnoticed as broken once already. Write the section as
above and read back with `Config::notmuchConfig()` to confirm before building
anything on it.

## The helper

Opt-in, never suite-wide. Roughly fifty existing cases construct a bare
`MainWindow` and must keep costing nothing; a suite-wide `initTestCase` would
make every one of them pay for a `notmuch new`, and shared mutable state between
cases is exactly how the `/proc/locks` bug (item 61) reached the whole suite.

A test that wants a database calls a helper that:

1. builds a `NotmuchFixture` (already exists, `tests/notmuchfixture.h`),
2. adds the messages that test needs and calls `index()`,
3. writes a `qtmaildir.conf` in the same `QTemporaryDir` with `notmuch_config`
   pointing at the fixture's config,
4. loads it into a `Config` and returns both, so the caller constructs the
   `MainWindow` and the fixture outlives it.

**Lifetime is the trap here.** The fixture owns a `QTemporaryDir` which deletes
the tree in its destructor, and the worker holds the database open on another
thread. The fixture must outlive the `MainWindow`, so it cannot be a local in
the helper returned by value unless it is moved or heap-owned. Decide this when
writing it, and assert the database is readable after construction rather than
assuming.

## Waiting for the worker

The worker is on its own thread, so every assertion is asynchronous.
`QSignalSpy::wait()` against the specific signal, never a bare
`QTest::qWait(n)` with a guessed duration: a fixed sleep is a race that passes
on this machine and fails under load, and it also passes when the signal never
arrives at all, since nothing checks.

Wait on `threadsReady` / `queryFinished` for a query, and on `threadLoaded` for
a selection. Assert the spy actually fired (`QVERIFY(spy.wait())`) before
asserting on what it delivered.

## The item 66 reproduction

Item 66: "selecting a thread root leaves the message pane blank until a reply
has been selected." The test:

1. Fixture with one thread of at least two messages, the reply linked by a real
   `In-Reply-To` header, since that is how `NotmuchFixture` builds a thread.
2. Run a query that returns it, wait for `queryFinished`.
3. Select the thread ROOT, the top-level row, without expanding it.
4. Wait for `threadLoaded`.
5. Assert the message pane is showing that thread.

Assert on `MainWindow`'s own notion of what the pane holds, the accessor already
used elsewhere in `test_mainwindow` for "the pane is blanked", not on rendered
pixels. `CLAUDE.md` is explicit that rendering probes lie in repeatable ways and
that `viewport()->render()` returns blank images for several ordinary reasons; a
pixel assertion here would be measuring the probe, not the defect.

**This test is expected to FAIL.** That is the deliverable.

### If it passes

A green test is a real finding, not a failure of the work. It means the defect
needs a condition the entry does not name, and the candidates in rough order of
likelihood are: it needs more than one message loaded before it manifests, it
needs the thread to be already expanded, it depends on the selection arriving
before a previous load completes, or it only appears against a large real
database and not a two-message one.

In that case, record what was tried in the item 66 entry and stop. Do not widen
the test until it goes red: a test twisted until it fails proves nothing about
the defect the user actually sees.

## Why the fix is excluded

The user's decision, and the reason is the ordering. Item 66 has never been
isolated. Designing the fix in the same pass as the reproduction means designing
it while the diagnosis is still a hypothesis, which is where a wrong fix gets
locked in and then defended by the test written alongside it. A red test is a
complete, useful deliverable: it proves the defect is real, locates it, and
makes the fix verifiable when it comes.

## Constraints

- **Every existing `test_mainwindow` case must keep working unchanged.** The
  helper is additive.
- **No test may read the real `/proc/locks`.** `init()` already points every
  test at an empty lock table in its own `QTemporaryDir`, and no test restores
  `"/proc/locks"` afterwards. That protection is suite-wide and this work must
  not weaken it; `noTestCanSeeTheRealLockTable` fails if it is lost.
- **No personal data in fixtures.** Generated messages only, `example.org`
  addresses, generic subjects. This was considered and declined explicitly on
  2026-08-04 and the repository is public.
- **A date fixture's weekday must match its date.** `Qt::RFC2822Date` validates
  the two against each other, so `Thu, 14 Aug 2026` parses as INVALID because
  that day is a Friday. Generate with `date -d <yyyy-mm-dd> +%A`, never from
  memory.
- **A hung `test_mainwindow` leaves a stale binary** that a later `ctest`
  re-runs, so a failure appears to persist after being fixed. Kill it and
  rebuild before concluding anything about a hang.

## Size

**S–M.** The helper is small because the plumbing already exists. The
reproduction is the uncertain half: the first attempt may need several shapes
before it reproduces, and it may not reproduce at all, which is a legitimate
outcome rather than an overrun.
