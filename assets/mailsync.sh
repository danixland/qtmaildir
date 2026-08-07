#!/bin/bash
# mailsync.sh - fetch mail and reindex it.
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
#
# Runs mbsync followed by notmuch new, under a lock so a cron timer and a
# click in qtmaildir cannot run two mbsync processes over one Maildir.
#
# Two audiences, which is what shapes the output handling below: a cron or
# systemd timer, which wants a log file it can read afterwards, and
# qtmaildir, which runs this as a subprocess and shows what it prints.
# Output therefore goes to BOTH, and the exit status is real.
#
# The script owns the log, so the caller must NOT redirect into it as well. A
# crontab line ending "> mailsync.log 2>&1" writes every line a second time,
# because tee has already put it there. Just call the script.

# Defensive: don't rely on cron/systemd/whatever invokes this to have
# set these correctly. Explicit beats inferred, especially after the
# HOME-not-set failure we hit once already. Fall back to the invoking
# user's home from passwd rather than a hardcoded path.
export HOME="${HOME:-$(getent passwd "$(id -u)" | cut -d: -f6)}"
export GNUPGHOME="${GNUPGHOME:-$HOME/.gnupg}"

LOCKFILE="/tmp/mbsync.lock"
LOGFILE="$HOME/.local/state/mailsync.log"

mkdir -p "$(dirname "$LOGFILE")"

# Rotation is NOT this script's job: /etc/logrotate.d/mailsync owns this file,
# keeping seven compressed days. An earlier version also rotated by size here,
# and the two fought: the script's "mv $LOGFILE $LOGFILE.1" overwrote whatever
# logrotate had just put at .1, losing a day of history and leaving an
# uncompressed file where a compressed one belonged.

exec 200>"$LOCKFILE"
if ! flock -n 200; then
    # Both streams again: a caller that skipped because the cron run holds
    # the lock needs to be told, not left with silence and an error code.
    msg="$(date -Iseconds) === SKIPPED: previous run still in progress ==="
    echo "$msg" | tee -a "$LOGFILE" >&2
    # 75 (EX_TEMPFAIL), not 1. A skip is not a failure: the other run is
    # doing the work. qtmaildir reports 1 as "sync failed" and shows its log
    # pane, which is wrong for a click that landed during the cron run, and
    # cron fires every ten minutes so that overlap is routine.
    exit 75
fi

# Statuses are written to files rather than shell variables because the
# block below is piped into tee, which puts it in a subshell: a variable
# assigned in there is gone by the time the parent reads it.
STATUS_DIR="$(mktemp -d)"
trap 'rm -rf "$STATUS_DIR"' EXIT

START_TS="$(date -Iseconds)"
{
    echo "===== RUN START: $START_TS ====="

    # Timestamp every line of mbsync/notmuch output as it streams,
    # rather than only marking run boundaries, this is what actually
    # lets you tell which errors are from which run at a glance.
    #
    # -V, deliberately. Without it mbsync prints NOTHING until it exits, then
    # one summary line: a 100-second run is silent for all of it, so qtmaildir
    # has nothing to report and its status bar can only say "Syncing...". With
    # it, mbsync announces each channel as it reaches it ("Channel work"),
    # which is both the progress and the account name the status bar shows.
    # This is not a buffering problem and stdbuf does not help: the output
    # streams fine, there simply is none to stream.
    mbsync -V -a 2>&1 | while IFS= read -r line; do
        echo "$(date '+%H:%M:%S') $line"
    done
    echo "${PIPESTATUS[0]}" > "$STATUS_DIR/mbsync"

    notmuch new 2>&1 | while IFS= read -r line; do
        echo "$(date '+%H:%M:%S') $line"
    done
    echo "${PIPESTATUS[0]}" > "$STATUS_DIR/notmuch"

    END_TS="$(date -Iseconds)"
    MBSYNC_STATUS="$(cat "$STATUS_DIR/mbsync")"
    NOTMUCH_STATUS="$(cat "$STATUS_DIR/notmuch")"
    if [ "$MBSYNC_STATUS" -eq 0 ] && [ "$NOTMUCH_STATUS" -eq 0 ]; then
        echo "===== RUN END: $END_TS  status=OK ====="
    else
        echo "===== RUN END: $END_TS  status=FAILED  mbsync=$MBSYNC_STATUS notmuch=$NOTMUCH_STATUS ====="
    fi
# tee, not a plain redirect. Appending only to the log left every caller
# that runs this as a subprocess with nothing to show: qtmaildir's sync
# pane was empty for exactly this reason. Cron still gets its log.
} 2>&1 | tee -a "$LOGFILE"

MBSYNC_STATUS="$(cat "$STATUS_DIR/mbsync" 2>/dev/null || echo 1)"
NOTMUCH_STATUS="$(cat "$STATUS_DIR/notmuch" 2>/dev/null || echo 1)"

# Report the real outcome. The old unconditional "exit 0" meant a caller
# could not distinguish a clean sync from a failed one, so qtmaildir's
# sync-on-exit prompt would report success over a sync that had not
# happened, which is precisely the case that loses work.
if [ "$MBSYNC_STATUS" -ne 0 ]; then
    exit "$MBSYNC_STATUS"
fi
exit "$NOTMUCH_STATUS"
