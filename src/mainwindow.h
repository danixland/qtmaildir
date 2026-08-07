/*
 * qtmaildir - a Qt6 mail client for notmuch-indexed Maildirs
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <QHash>
#include <QSet>
#include <QMainWindow>
#include <QPointer>
#include <QThread>
#include <QUndoCommand>
#include <QUndoStack>

#include <functional>

#include "config.h"
#include "htmlbuilder.h"
#include "keymap.h"
// Included rather than forward-declared: SyncPhaseTracker is held by value, so
// its size must be known here. MailSync itself stays a forward declaration.
#include "mailsync.h"
#include "syncmonitor.h"
#include "tagcolors.h"
#include "types.h"

class QAction;
class QLineEdit;
class QMenu;
class QTableView;
class QLabel;
class QPushButton;
class QComboBox;
class QPlainTextEdit;
class QSplitter;
class QProgressBar;
class QTimer;

class ThreadListModel;
class MessageView;
class MailSync;
class NotmuchWorker;
class QueryCompleter;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const Config &config, QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Every action name registerActions() installs. Derived from the actions
    /// themselves rather than hand-maintained, so it cannot drift from what is
    /// really registered.
    QStringList registeredActionNames() const;

    /// The thread currently shown in the message pane, empty when it is blank.
    ///
    /// Empty is what "the pane is blanked" means internally: a late-arriving
    /// load is discarded rather than painted, so no thread can reappear.
    QString currentThreadId() const { return m_currentThreadId; }

    /// True while an edit is held back because a sync holds the write lock.
    /// Exposed for tests: the deferral is otherwise only observable by watching
    /// the worker, which test_mainwindow has no database to drive.
    bool hasEditAwaitingSend() const { return !m_heldEdits.isEmpty(); }

    /// Whether the undo stack still holds anything. Exposed so a test can show
    /// that a rejected write did not take unrelated history down with it.
    bool canUndo() const { return m_undoStack.canUndo(); }

    /// The cid: namespace prefix for the nth message of a thread.
    ///
    /// MainWindow is the only producer of this value in the application. It
    /// must never contain '!', which is the separator that keeps one message's
    /// cid: references from resolving to another's.
    static QString cidPrefixForIndex(int index);

    /// What the sync command returns when another run already holds the lock.
    ///
    /// EX_TEMPFAIL from sysexits.h. A skip is not a failure: the other run is
    /// doing the work, and with a cron timer every ten minutes a click landing
    /// inside one is routine. Reporting it as an error would show a log pane
    /// and an alarming status for a situation that needs neither.
    /// `assets/mailsync.sh` is the reference implementation of this contract.
    static constexpr int kSyncSkippedExitCode = 75;

    /// How long a transient status message stays before the bar falls back to
    /// the thread count. Long enough to read a sentence, short enough that a
    /// stale "Sync complete" does not sit there describing the present.
    static constexpr int kStatusMessageMs = 6000;

    /// Path of the machine-written UI state file. Deliberately not
    /// Config::defaultPath(): the config is hand-edited and must never gain a
    /// base64 geometry blob, nor be rewritten on exit (QSettings does not
    /// preserve comments or key order).
    static QString uiStatePath();

    /// Kernel lock table every MainWindow's SyncMonitor watches, "/proc/locks"
    /// unless a test overrides it.
    ///
    /// A test seam, deliberately NOT a config key: /proc/locks is not something
    /// a user would ever set, and a wrong value silently disables background
    /// sync detection rather than failing loudly. Without this every window a
    /// test builds observes the machine's real sync state, so a test asserting
    /// on the sync button fails whenever the user's cron sync happens to be
    /// running (item 38).
    static void setLocksPathForTesting(const QString &path);
    static QString locksPath();

    /// How many commands are on the undo stack.
    ///
    /// A test seam. The undo QAction is always enabled and checks canUndo()
    /// when triggered, so its enabled state says nothing about whether a
    /// command was pushed, which is what "this did nothing" has to assert.
    int undoDepthForTesting() const { return m_undoStack.count(); }

    /// The generation a worker reply must carry to be accepted.
    ///
    /// A test seam: onQueryFinished() discards a reply whose generation is
    /// stale, so a test standing in for the worker has to know the current one.
    quint64 currentGenerationForTesting() const { return m_generation; }

    /// The generation a database-stats reply must carry to be accepted.
    ///
    /// A test seam, for the same reason as the one above: onDatabaseStatsReady
    /// discards a reply belonging to a dialog that has since been closed and
    /// reopened, so a test standing in for the worker needs the current value.
    quint64 statsGenerationForTesting() const { return m_statsGeneration; }

protected:
    void closeEvent(QCloseEvent *event) override;

    /// Claims Return back for the query bar. Return is bound to open_thread as
    /// a WindowShortcut, and a shortcut outranks the focused widget, so without
    /// this the action fires from inside the bar and the query never runs.
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void runCurrentQuery();
    void onThreadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void onQueryFinished(int total, quint64 generation);
    void onThreadSelected(const QModelIndex &current, const QModelIndex &previous);

    /// Keeps the status bar's selection count and the multi-select guard in
    /// step with selections that never move the current index.
    void onSelectionChanged();

    /// Pops up the thread-list context menu, preserving a multi-row selection
    /// the click lands inside.
    void showThreadContextMenu(const QPoint &pos);
    void onThreadLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void onWorkerError(const QString &message);
    void onSyncFinished(bool success, int exitCode);

    /// Shows a message that describes an event and takes it back after a few
    /// seconds, restoring the last query's thread count.
    ///
    /// Use this for events ("Sync complete"), never for state: the selection
    /// count must persist while the selection does. A private slot so tests can
    /// drive it through the meta-object.
    void showTransientStatus(const QString &text);

    /// Reacts to a sync started outside this window, by cron or by hand.
    ///
    /// A private slot rather than a plain method so tests can drive it through
    /// the meta-object without widening the public API.
    void onExternalSyncStateChanged(SyncMonitor::State state);

    /// Starts a sync and shows that it started. Every route in goes through
    /// here: the toolbar, the menu, the shortcut and the button.
    ///
    /// A private slot for the same reason as the two above: a test needs to
    /// start a real run through the meta-object to exercise the output
    /// handling, without this becoming public API.
    void startSync();

    /// A tag mutation the worker has confirmed reached the database. Counts it
    /// as unsynced, since reaching the index is not reaching the mail store.
    void onTagsApplied(const TagChange &change);
    void onAllTagsReady(const QStringList &tags);

    /// Thread counts for the placeholder's helper lines, in the order
    /// requestPlaceholderCounts() asked for them.
    void onCountsReady(const QVector<int> &counts, quint64 generation);

    /// Fills in the overview dialog's counts when the worker answers. Does
    /// nothing if the dialog has since been closed.
    void onDatabaseStatsReady(const DatabaseStats &stats, quint64 generation);

    /// Runs a query the user clicked on the placeholder pane.
    void onPlaceholderQueryRequested(const QString &query);

private:
    void buildUi();

    /// Restores window geometry, splitter and thread-list header widths.
    /// A missing or rejected blob leaves the buildUi() defaults in place.
    void restoreUiState();
    void saveUiState() const;

    void registerActions();
    void buildMenus();
    void wireWorker();

    /// Asks the worker to re-enumerate the database tags for the completer.
    void requestAllTags();

    /// Shows the placeholder pane and asks the worker to refresh its counts.
    ///
    /// **The single route to a blank pane.** Every site that used to call
    /// MessageView::clear() goes through here, so the pane is never left empty
    /// by accident and the counts are refreshed exactly when they are about to
    /// be looked at. A count goes stale the moment a tag is edited, and one
    /// nobody is looking at is not worth keeping fresh.
    void showPlaceholderPane();

    /// The helper lines, built from the last counts received. Rendered with
    /// whatever the previous answer was until the new one lands, so the pane
    /// never flashes empty while the worker replies.
    QList<HtmlBuilder::PlaceholderHelper> placeholderHelpers() const;

    void showWarnings();
    void showShortcutReference();
    void showAbout();

    /// The Maildir overview (item 34): what notmuch knows about the database,
    /// plus the account list, which comes from config since notmuch does not
    /// model accounts at all.
    ///
    /// Opens immediately showing the counts as pending and fills them in when
    /// the worker answers, rather than blocking: counting every message is not
    /// free on a large database and a dialog that hangs first is worse than one
    /// that populates.
    void showMaildirOverview();

    /// Creates a QAction, binds it to the sequence KeyMap holds for `name`,
    /// and registers it. `name` is the action name used in [keys].
    QAction *addAction(const QString &name, const QString &text,
                       const QString &description,
                       const std::function<void()> &handler);

    void tagSelected(const QStringList &add, const QStringList &remove,
                     const QString &description);

    /// Starts, restarts or cancels the mark-read timer for a newly opened
    /// thread. Cancels outright for a thread that is not unread, so an already
    /// read thread never schedules a write that would change nothing.
    void scheduleMarkRead(const ThreadSummary &thread);

    /// Removes `unread` from the thread the timer was armed for, if it is still
    /// the one on screen.
    void markCurrentThreadRead();

    /// Redraws the unsynced-edits indicator from pendingEditCount().
    void updatePendingIndicator();

    /// Records one confirmed (message, tag) change, cancelling it against an
    /// opposite change already outstanding for the same pair.
    void recordPendingEdit(const QString &messageId, const QString &tag,
                           bool added);

    /// Net changes the index holds that a sync has not carried over.
    int pendingEditCount() const;

    /// Shows or hides the "syncing" state: the progress bar and a disabled
    /// Sync button.
    ///
    /// The bar is INDETERMINATE by design. mbsync reports no percentage and
    /// the script's output is unstructured, so a bar that filled from left to
    /// right would be inventing a fraction nobody knows. An indeterminate one
    /// says "working, duration unknown", which is the truth.
    void setSyncBusy(bool busy);

    /// Reassembles lines from a sync output chunk and updates the status label
    /// when the phase or its detail changes.
    void feedSyncPhase(const QString &chunk);

    /// Removes `unread` from every thread in the current view, as one write and
    /// one undo entry, ignoring the selection.
    void markAllRead();

    /// Enables or disables the actions that claim to act on a whole view,
    /// according to whether the result set is complete.
    void updateViewWideActions();

    /// Applies the sync progress bar and button state from BOTH sync sources.
    ///
    /// One function of both, never two assignments: with a local and a
    /// background sync each writing the widgets independently, whichever
    /// finished second would win and re-enable the button while the other was
    /// still running.
    void updateSyncControls();


    /// Opens the tag dialog on the current selection and applies its result.
    ///
    /// The only route to an arbitrary tag: every other tag action writes a
    /// hardcoded name.
    void editTagsOnSelection();

    /// Set once the user has answered the exit prompt, or once a sync started
    /// for exit has finished. Stops closeEvent asking a second time, and is
    /// what lets the deferred close through.
    bool m_closeApproved = false;

    /// True while a sync started by the exit prompt is running. The window
    /// stays open until it finishes: killing the process mid-sync is exactly
    /// the loss the prompt exists to prevent.
    bool m_syncingForExit = false;

    /// Sends a tag change for a set of threads without touching the undo stack.
    /// Both tagSelected() and ThreadTagCommand route through this.
    void sendThreadTagChange(const QStringList &threadIds,
                             const QStringList &add,
                             const QStringList &remove,
                             const QString &description);

    /// Undoes the optimistic model update for a write the worker rejected.
    void revertPendingTagChange();

    /// Whether a write sent now would block the worker on notmuch's write lock.
    ///
    /// True only for a sync KNOWN to be running. `SyncMonitor::State::Unknown`
    /// deliberately does not count: it means `/proc/locks` could not be read,
    /// and holding every edit on a platform that cannot observe the lock at all
    /// would strand them permanently.
    bool aSyncHoldsTheWriteLock() const;

    /// Sends every edit held while the lock was busy, oldest first.
    void flushHeldEdits();

    /// A tag change not yet sent to the worker, because a sync held the write
    /// lock when the user made it.
    ///
    /// Held rather than sent because the read-write open BLOCKS: measured
    /// 9.158s against a 12s lock hold, returning SUCCESS, not an error. Sending
    /// into that freezes the worker thread, so every later query and thread
    /// load queues behind it. The rows show the change meanwhile, which is
    /// honest: it is what the user asked for and it is going to be applied.
    struct HeldEdit {
        QStringList threadIds;
        TagChange change;
    };

    /// FIFO, because a sync lasts ~35s and the user can keep tagging through
    /// it. Order matters: two edits touching one thread must reach the database
    /// in the order they were made, or the later one does not win.
    QVector<HeldEdit> m_heldEdits;

    friend class ThreadTagCommand;

    Config m_config;
    KeyMap m_keyMap;
    TagColors m_tagColors;

    QThread m_workerThread;
    NotmuchWorker *m_worker = nullptr;

    ThreadListModel *m_model = nullptr;
    MessageView *m_messageView = nullptr;
    MailSync *m_sync = nullptr;

    /// Derives "which half of the sync is running" from the output stream, so
    /// the status bar says more than "Syncing...". Reset at the start of each
    /// local run.
    SyncPhaseTracker m_syncPhase;

    /// Holds the tail of a chunk that did not end on a newline, since
    /// QProcess::readAll() splits wherever it happens to.
    QString m_syncLineBuffer;

    /// Watches the sync lock for runs this window did not start.
    SyncMonitor *m_syncMonitor = nullptr;

    /// True while the lock the monitor can see is held by this window's own
    /// sync. Latched when the lock is taken, because by the time it is released
    /// MailSync::isRunning() is already false and can no longer answer "was
    /// that ours?".
    bool m_localSyncHoldsLock = false;

    /// True while a sync this window started is running. Half of the input to
    /// updateSyncControls().
    bool m_localSyncBusy = false;

    /// True while a sync this window did NOT start holds the lock. The other
    /// half. Tracked here rather than read back from SyncMonitor so the state
    /// the UI acted on is the state it was told about.
    bool m_externalSyncBusy = false;
    QUndoStack m_undoStack;

    QLineEdit *m_queryEdit = nullptr;
    QueryCompleter *m_queryCompleter = nullptr;
    QTableView *m_threadView = nullptr;

    /// Right-click menu for the thread list, holding the same QActions the
    /// menu bar does.
    QMenu *m_threadContextMenu = nullptr;
    QSplitter *m_splitter = nullptr;
    QComboBox *m_accountBox = nullptr;
    QLabel *m_statusLabel = nullptr;

    /// Expires a transient status message. See showTransientStatus().
    QTimer *m_statusTimer = nullptr;

    /// The message m_statusTimer armed for, so it takes back only its own.
    QString m_transientMessage;

    /// What the status bar falls back to: the last query's thread count.
    QString m_defaultStatus;

    /// Says how many tag changes have not been seen to reach the mail store.
    /// Hidden entirely at zero rather than reading "0 unsynced", which is noise.
    QLabel *m_pendingLabel = nullptr;

    /// Indeterminate, shown only while a sync runs. See setSyncBusy().
    QProgressBar *m_syncProgress = nullptr;

    /// The last counts the worker answered, one per kPlaceholderQueries entry.
    /// Empty until the first reply, which renders the pane without its helper
    /// lines rather than with three zeroes that would be a lie.
    QVector<int> m_placeholderCounts;

    /// Discriminates a counts reply from a superseded request, the same way the
    /// query generation does. A reply for an older request is dropped rather
    /// than repainting the pane with counts taken before the last edit.
    quint64 m_countsGeneration = 0;

    /// Set when a sync ends in failure, cleared when one succeeds. Drives the
    /// placeholder's sync line, which appears only when something needs
    /// attention, so it must survive until the next successful run.
    bool m_lastSyncFailed = false;

    /// The overview dialog's counts label while that dialog is open, null
    /// otherwise. A QPointer because the dialog is deleted on close and the
    /// worker's reply can arrive afterwards: a raw pointer would dangle for
    /// exactly as long as the count takes on a large database, which is
    /// precisely when the user is most likely to close it first.
    QPointer<QLabel> m_overviewCounts;

    /// Discriminates a stats reply from a dialog that has since been closed
    /// and reopened, so an old answer cannot fill in a newer dialog.
    quint64 m_statsGeneration = 0;

    /// Holds the sync log and its close button, so the pane can be dismissed.
    QWidget *m_syncLogPane = nullptr;
    QPlainTextEdit *m_syncLog = nullptr;

    /// Action name (as used in [keys]) to the QAction implementing it. Owned
    /// by the window through the QObject parent, not by this hash.
    QHash<QString, QAction *> m_actions;

    /// One-line description per action, for the shortcut reference. Kept
    /// beside the actions so the dialog is generated, never hand-written in
    /// parallel with them.
    QHash<QString, QString> m_actionDescriptions;

    /// The tag list last received from the worker. Held here and not only in
    /// the completer so a mutation can ask whether it introduced a tag the
    /// completer does not yet offer, without a round trip.
    QStringList m_knownTags;

    quint64 m_generation = 0;

    /// True once the running query has reported its total, so the model holds
    /// the whole result set rather than the batches that have arrived so far.
    /// Gates mark_all_read, which cannot honestly say "all" before then.
    bool m_queryComplete = false;
    QString m_lastQuery;
    QString m_currentThreadId;

    /// The selection count last written to the status bar, so it can be taken
    /// back without clobbering a message some other action put there.
    QString m_selectionMessage;

    /// Confirmed tag mutations not yet known to have reached the mail store.
    ///
    /// A count of its own rather than QUndoStack::isClean(), which cannot serve
    /// here: the undo stack is CLEARED on every query, since its entries refer
    /// to rows the new result set discards. Tag a thread, run any query, and the
    /// stack is empty while the change is still unsynced.
    ///
    /// A lower bound on what is outstanding, never a guarantee: the user's cron
    /// can run notmuch new without the application noticing.
    ///
    /// NET state rather than a tally of writes. Keyed "<messageId>\n<tag>",
    /// value true for added and false for removed; a pair that reverts is
    /// erased rather than stored, so an edit and its inverse leave nothing
    /// behind and the map cannot grow without bound.
    QHash<QString, bool> m_pendingTagEdits;

    /// Confirmed changes carrying no message ids, which cannot be netted
    /// against anything. Counted separately rather than dropped: understating
    /// the indicator is the direction that costs the user work.
    int m_unnettablePendingEdits = 0;

    /// Marks the open thread read once it has been on screen long enough.
    ///
    /// Single-shot and RESTARTED on every selection change, never stacked:
    /// arrowing down a list must mark only the thread still selected when it
    /// fires, not each one passed through.
    QTimer *m_markReadTimer = nullptr;

    /// The thread m_markReadTimer will mark read. Compared against the current
    /// selection when it fires, so a timer that outlives its thread does
    /// nothing rather than marking the wrong one.
    QString m_markReadThreadId;

    /// The optimistic update awaiting confirmation, kept so a worker error can
    /// put the model back. Only the most recent one: mutations are sent from
    /// the UI thread one user action at a time.
    TagChange m_pendingChange;
    QStringList m_pendingThreadIds;

    /// Account keys whose mail store has edits a sync has not yet carried,
    /// for item 49's per-account sync.
    ///
    /// Deliberately NOT netted the way m_pendingTagEdits is. That map tracks
    /// the INDEX, where removing a tag and re-adding it leaves nothing
    /// outstanding; this tracks the MAIL STORE, where both writes have already
    /// renamed files that mbsync still has to propagate. Netting this to empty
    /// would skip the very account whose files changed.
    ///
    /// Populated where the threads are known, since TagChange carries message
    /// ids and the account is a property of the thread. Cleared only by a
    /// SUCCESSFUL sync, alongside the pending-edit map.
    QSet<QString> m_editedAccounts;

    /// The channel names for m_editedAccounts, resolved through the config.
    /// Empty means sync everything, which is what a fetch with nothing pending
    /// has to do.
    QStringList pendingSyncChannels() const;
};

/// Undo entry for a tag change over a set of threads.
///
/// Stores thread ids rather than message ids, so undo re-resolves them on the
/// worker and stays correct even if the selection has moved on.
class ThreadTagCommand : public QUndoCommand
{
public:
    ThreadTagCommand(MainWindow *window, const QStringList &threadIds,
                     const QStringList &add, const QStringList &remove,
                     const QString &description)
        : QUndoCommand(description), m_window(window), m_threadIds(threadIds),
          m_add(add), m_remove(remove), m_description(description) {}

    /// The stack calls redo() when the command is pushed. The change has
    /// already been sent by that point, so the first call is skipped.
    void redo() override
    {
        if (m_firstRedo) {
            m_firstRedo = false;
            return;
        }
        m_window->sendThreadTagChange(m_threadIds, m_add, m_remove,
                                      m_description);
    }

    void undo() override
    {
        // Inverted: what was added is removed and vice versa.
        m_window->sendThreadTagChange(m_threadIds, m_remove, m_add,
                                      QStringLiteral("Undo %1").arg(m_description));
    }

private:
    MainWindow *m_window;
    QStringList m_threadIds;
    QStringList m_add;
    QStringList m_remove;
    QString m_description;
    bool m_firstRedo = true;
};
