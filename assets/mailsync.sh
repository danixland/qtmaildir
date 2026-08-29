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
#
# Usage: mailsync.sh [channel ...]
#
# With no arguments it syncs every channel, which is what a cron timer wants
# and what every existing caller already does. Given channel names it syncs
# only those, which is how qtmaildir syncs just the accounts it edited. The
# names are mbsync CHANNEL names from ~/.mbsyncrc, which are not necessarily
# the account names qtmaildir shows: see the `channel` key in qtmaildir.conf.

# Defensive: don't rely on cron/systemd/whatever invokes this to have
# set these correctly. Explicit beats inferred, especially after the
# HOME-not-set failure we hit once already. Fall back to the invoking
# user's home from passwd rather than a hardcoded path.
export HOME="${HOME:-$(getent passwd "$(id -u)" | cut -d: -f6)}"
export GNUPGHOME="${GNUPGHOME:-$HOME/.gnupg}"

# Overridable for the test suite ONLY. A test must never take the real lock:
# that is the mutex the user's cron sync uses, so a test run holding it would
# block their mail. Production passes nothing and gets the real path.
LOCKFILE="${MAILSYNC_LOCKFILE:-/tmp/mbsync.lock}"
LOGFILE="$HOME/.local/state/mailsync.log"

# What qtmaildir READS, as against the log, which is for a human (item 174).
# The application used to infer a finished run from the log's RUN END banner
# and from this lock's inode in /proc/locks. That made a human-readable line
# into wire format, said nothing about WHICH channels a run carried, and left a
# skipped run reporting nothing at all (item 125). This file is the interface;
# the log stays the log.
STATUSFILE="$HOME/.local/state/qtmaildir/syncstatus.json"

mkdir -p "$(dirname "$LOGFILE")"
mkdir -p "$(dirname "$STATUSFILE")"

# Written atomically, because qtmaildir watches this file and a reader must
# never see a half-written one: mv within a directory is atomic, a redirect
# into the final path is not.
#
# Failure to write it is deliberately NOT fatal. The status file is a
# convenience for the application; the sync itself has already happened, and
# taking the run down over a report of it would turn a cosmetic problem into a
# mail problem.
write_status() {
    local state="$1" mbsync_status="$2" notmuch_status="$3"
    local started="$4" ended="$5"
    shift 5

    local channels="" sep="" channel
    for channel in "$@"; do
        # The two characters JSON requires escaping in a string. A channel name
        # comes from ~/.mbsyncrc and is an identifier rather than free text, so
        # this is belt and braces rather than a real expectation.
        channel="${channel//\\/\\\\}"
        channel="${channel//\"/\\\"}"
        channels="${channels}${sep}\"${channel}\""
        sep=", "
    done

    local tmp
    tmp="$(mktemp "${STATUSFILE}.XXXXXX")" || return 0
    cat > "$tmp" <<JSON
{
  "version": 1,
  "run_id": "$started",
  "started": "$started",
  "ended": "$ended",
  "state": "$state",
  "channels": [$channels],
  "mbsync_status": $mbsync_status,
  "notmuch_status": $notmuch_status
}
JSON
    mv -f "$tmp" "$STATUSFILE" 2>/dev/null || rm -f "$tmp"
}

# Rotation is NOT this script's job: /etc/logrotate.d/mailsync owns this file,
# keeping seven compressed days. An earlier version also rotated by size here,
# and the two fought: the script's "mv $LOGFILE $LOGFILE.1" overwrote whatever
# logrotate had just put at .1, losing a day of history and leaving an
# uncompressed file where a compressed one belonged.

# Resolved HERE, before the lock and before the pipeline below, for two
# reasons. The block below is piped into tee and so runs in a subshell, where
# a "set --" cannot be seen by the parent that writes the status file; and the
# skip branch needs the list too, to report what the run WOULD have carried.
#
# -a is NOT equivalent to naming every channel: with no arguments at all mbsync
# syncs nothing and exits, which would look like a clean sync that moved no
# mail. It is reported to qtmaildir as "-a" rather than expanded, since only
# ~/.mbsyncrc knows what every channel is, and the application reads "-a" as
# "every account".
[ "$#" -gt 0 ] || set -- -a
CHANNELS=("$@")

exec 200>"$LOCKFILE"
if ! flock -n 200; then
    # Both streams again: a caller that skipped because the cron run holds
    # the lock needs to be told, not left with silence and an error code.
    SKIP_TS="$(date -Iseconds)"
    msg="$SKIP_TS === SKIPPED: previous run still in progress ==="
    echo "$msg" | tee -a "$LOGFILE" >&2
    # Item 125. A skip used to report NOTHING a watcher could see: it releases
    # a lock it never took, so qtmaildir's spinner waited for a completion that
    # never came. A skipped run is a terminal state and says so, with -1 for
    # both statuses since neither program ran.
    write_status "skipped" -1 -1 "$SKIP_TS" "$SKIP_TS" "${CHANNELS[@]}"
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
    # "$@" when channels were named, -a otherwise, resolved into CHANNELS
    # before the lock above. Quoted and passed as separate words, never
    # flattened into a string: a channel name is an argument, and mbsync takes
    # an unknown one as a fatal error rather than skipping it, which would fail
    # the whole run.
    mbsync -V "${CHANNELS[@]}" 2>&1 | while IFS= read -r line; do
        echo "$(date '+%H:%M:%S') $line"
    done
    echo "${PIPESTATUS[0]}" > "$STATUS_DIR/mbsync"

    notmuch new 2>&1 | while IFS= read -r line; do
        echo "$(date '+%H:%M:%S') $line"
    done
    echo "${PIPESTATUS[0]}" > "$STATUS_DIR/notmuch"

    END_TS="$(date -Iseconds)"
    # Also to a file, for the same subshell reason the statuses are: the parent
    # writes the status file and cannot see a variable assigned in here.
    echo "$END_TS" > "$STATUS_DIR/end"
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
END_TS="$(cat "$STATUS_DIR/end" 2>/dev/null || date -Iseconds)"

# Item 174. What qtmaildir reads, written before the exit below so it exists
# whichever way this run went. "ok" only when BOTH programs succeeded, matching
# the log banner and the exit status: a caller must not be able to read success
# here and failure there.
#
# The two statuses are reported separately as well as folded into the state,
# because they mean different things to the application: a failed mbsync means
# the edits never reached the server, while a failed notmuch means they did and
# only the local index is behind.
if [ "$MBSYNC_STATUS" -eq 0 ] && [ "$NOTMUCH_STATUS" -eq 0 ]; then
    write_status "ok" "$MBSYNC_STATUS" "$NOTMUCH_STATUS" \
                 "$START_TS" "$END_TS" "${CHANNELS[@]}"
else
    write_status "failed" "$MBSYNC_STATUS" "$NOTMUCH_STATUS" \
                 "$START_TS" "$END_TS" "${CHANNELS[@]}"
fi

# Report the real outcome. The old unconditional "exit 0" meant a caller
# could not distinguish a clean sync from a failed one, so qtmaildir's
# sync-on-exit prompt would report success over a sync that had not
# happened, which is precisely the case that loses work.
if [ "$MBSYNC_STATUS" -ne 0 ]; then
    exit "$MBSYNC_STATUS"
fi
exit "$NOTMUCH_STATUS"
