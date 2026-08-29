#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Danilo M. <danix@danix.xyz>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""Checks for the status file mailsync.sh writes (item 174).

Nothing here touches real mail or the network: mbsync and notmuch are stubs on
PATH, HOME points at a temp directory, and the lock file is redirected there
too, so a run of this suite cannot collide with the user's cron sync.

The properties worth proving are the ones qtmaildir depends on and a reader of
the script cannot confirm:

  - a status file is written on success, on failure, and on a SKIP, which is
    the case item 125 is open for
  - it names the channels the run actually synced, which is what lets the
    application clear its pending count per account instead of wholesale
  - "-a" is reported as such, since a full run carries every account and a
    reader that treats it as a channel name clears nothing
  - the file is valid JSON at every moment a watcher could read it

Run: ./test_mailsync.py
"""

import json
import os
import stat
import subprocess
import tempfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "mailsync.sh"

failures = []


def check(name, condition, detail=""):
    if condition:
        print(f"ok   {name}")
    else:
        print(f"FAIL {name}{(': ' + detail) if detail else ''}")
        failures.append(name)


def write_stub(path, exit_code, output="", sleep_for=0):
    """A stub standing in for mbsync or notmuch."""
    body = "#!/bin/bash\n"
    if output:
        body += f"echo {output!r}\n"
    if sleep_for:
        body += f"sleep {sleep_for}\n"
    body += f"exit {exit_code}\n"
    path.write_text(body)
    path.chmod(path.stat().st_mode | stat.S_IEXEC)


def run(tmp, args=(), mbsync_exit=0, notmuch_exit=0, lockfile=None,
        hold_lock=False):
    """Runs the script with stubbed binaries, returns (exit code, status dict)."""
    bindir = tmp / "bin"
    bindir.mkdir(exist_ok=True)
    write_stub(bindir / "mbsync", mbsync_exit, "Channel work")
    write_stub(bindir / "notmuch", notmuch_exit, "No new mail.")

    env = dict(os.environ)
    env["HOME"] = str(tmp)
    env["PATH"] = f"{bindir}:{env['PATH']}"
    # Never the real /tmp/mbsync.lock: a test must not join the mutex the
    # user's cron sync uses, or a run of this suite blocks their mail.
    env["MAILSYNC_LOCKFILE"] = str(lockfile or (tmp / "lock"))

    proc = subprocess.run([str(SCRIPT), *args], env=env, capture_output=True,
                          text=True, timeout=60)

    status_path = tmp / ".local/state/qtmaildir/syncstatus.json"
    status = None
    if status_path.exists():
        status = json.loads(status_path.read_text())
    return proc.returncode, status


def main():
    with tempfile.TemporaryDirectory() as raw:
        tmp = Path(raw)

        # --- a successful full run
        code, st = run(tmp)
        check("a clean run exits 0", code == 0, f"exit {code}")
        check("a clean run writes a status file", st is not None)
        if st:
            check("state is ok", st.get("state") == "ok", str(st.get("state")))
            check("mbsync status recorded", st.get("mbsync_status") == 0)
            check("notmuch status recorded", st.get("notmuch_status") == 0)
            check("a full run reports -a", st.get("channels") == ["-a"],
                  str(st.get("channels")))
            check("version is 1", st.get("version") == 1)
            check("carries a start and an end", bool(st.get("started"))
                  and bool(st.get("ended")))

        # --- named channels are reported, which is the whole point for the
        # application: it clears its pending count for these accounts only.
        code, st = run(tmp, args=("work", "personal"))
        check("named channels exit 0", code == 0)
        if st:
            check("named channels are listed",
                  st.get("channels") == ["work", "personal"],
                  str(st.get("channels")))

        # --- a failing mbsync
        code, st = run(tmp, mbsync_exit=1)
        check("a failed mbsync exits nonzero", code != 0, f"exit {code}")
        if st:
            check("state is failed", st.get("state") == "failed",
                  str(st.get("state")))
            check("the failing status is recorded",
                  st.get("mbsync_status") == 1)

        # --- a failing notmuch, with mbsync fine. Distinguished because the
        # mail DID reach the server: only the index is behind.
        code, st = run(tmp, notmuch_exit=1)
        check("a failed notmuch exits nonzero", code != 0)
        if st:
            check("a notmuch failure is still failed",
                  st.get("state") == "failed")
            check("mbsync is recorded as fine", st.get("mbsync_status") == 0)

        # --- a SKIP, which is item 125: the run exits 75 holding no lock, and
        # before this the application had nothing to clear its spinner on.
        lock = tmp / "held.lock"
        lock.touch()
        with open(lock, "w") as handle:
            holder = subprocess.Popen(
                ["flock", "-x", str(lock), "sleep", "10"])
            try:
                # Give flock a moment to actually take it.
                subprocess.run(["sleep", "0.3"])
                code, st = run(tmp, lockfile=lock)
            finally:
                holder.terminate()
                holder.wait()

        check("a skipped run exits 75", code == 75, f"exit {code}")
        check("a skipped run still writes a status file", st is not None)
        if st:
            check("state is skipped", st.get("state") == "skipped",
                  str(st.get("state")))

    print()
    if failures:
        print(f"{len(failures)} FAILED: {', '.join(failures)}")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
