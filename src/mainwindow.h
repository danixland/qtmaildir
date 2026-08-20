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
#include <QQueue>
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
#include "searchterm.h"
// Complete type, not a forward declaration: showTagRulesDialog() defaults its
// seed to TagRule().
#include "tagrules.h"
#include "syncmonitor.h"
#include "tagcolors.h"
#include "types.h"

class QAction;
class QLineEdit;
class QMenu;
class ThreadListView;
class QLabel;
class QPushButton;
class QComboBox;
class QPlainTextEdit;
class QSplitter;
class QTimer;
class QToolButton;
class QVBoxLayout;

class BusyIndicator;
class ThreadListModel;
class MessageView;
class MailSync;
class NotmuchWorker;
class QueryCompleter;
class TagRulesDialog;

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

    /// Configuration problems worth interrupting startup for, empty when there
    /// are none. A keybinding the user wrote and that is being ignored counts;
    /// a notice such as "no sync command configured" does not.
    ///
    /// **Returned rather than shown, and that is the point.** This used to
    /// raise a `QMessageBox` from the CONSTRUCTOR. A modal cannot be dismissed
    /// under the offscreen platform, so `MainWindow` never finished
    /// constructing and the whole suite hung with no output, which reads as an
    /// infrastructure failure rather than a test one (item 84). The caller
    /// raises the dialog after show(); a test asserts on the list.
    QStringList configProblems() const;

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

    /// Suppresses the delete confirmation.
    ///
    /// A test seam. Deleting a saved query is destructive and not on the undo
    /// stack, so it asks first; a test cannot answer a modal dialog without
    /// hanging, and driving one through QTest would assert the dialog rather
    /// than the deletion.
    void setConfirmDeleteForTesting(bool confirm) { m_confirmDelete = confirm; }

    /// Renames or replaces a stored query, as the edit dialog would on accept.
    ///
    /// A test seam for the rename path specifically: the dialog is modal, and
    /// the property worth asserting is that a rename REPLACES rather than
    /// duplicating, which is decided after the dialog returns.
    void replaceSavedQueryForTesting(const QString &originalName,
                                     const SavedQuery &replacement)
    {
        replaceSavedQuery(originalName, replacement);
    }

    /// How many commands are on the undo stack.
    ///
    /// A test seam. The undo QAction is always enabled and checks canUndo()
    /// when triggered, so its enabled state says nothing about whether a
    /// command was pushed, which is what "this did nothing" has to assert.
    int undoDepthForTesting() const { return m_undoStack.count(); }

    /// The text of the command on top of the undo stack.
    ///
    /// A test seam for the DIRECTION a toggle chose. Delete and Undelete both
    /// push one command and touch the same rows, so a depth or an id says
    /// nothing about which way the toggle went, which is exactly what item 88
    /// got wrong.
    QString undoTextForTesting() const { return m_undoStack.undoText(); }

    /// The tag counts the tag dialog would be built from, for the current
    /// selection. A test seam: the dialog is modal, so the counts cannot be
    /// observed through it.
    QHash<QString, int> selectionTagCountsForTesting() const
    {
        return selectionTagCounts();
    }

    /// The thread the current row belongs to. A test seam for item 88's
    /// resolution itself, reachable when the write it guards is not.
    ThreadSummary threadForCurrentRowForTesting() const;

    /// Sends a message-scoped tag change directly. A test seam for the cases
    /// where driving the action would move the selection, which is sometimes
    /// the very thing under test.
    void sendMessageTagChangeForTesting(const QStringList &messageIds,
                                        const QStringList &add,
                                        const QStringList &remove,
                                        const QString &description)
    {
        sendMessageTagChange(messageIds, add, remove, description);
    }

    /// The ids the last tag change was sent for, and whether they were thread
    /// ids or message ids.
    ///
    /// Exposed because the difference is invisible from outside otherwise: a
    /// message row routed down the thread path produces the same undo depth and
    /// the same status text while tagging every sibling in the thread. A
    /// mutation that made exactly that change passed the whole suite.
    QStringList pendingThreadIdsForTesting() const { return m_pendingThreadIds; }
    QStringList pendingMessageIdsForTesting() const
    {
        return m_pendingChange.messageIds;
    }

    /// The whole change last sent, for tests about WHAT was written rather
    /// than what it was written to. The tags are the same under either scope,
    /// so a test about a tag name should read this instead of a model row.
    TagChange pendingChangeForTesting() const { return m_pendingChange; }

    /// The generation a worker reply must carry to be accepted.
    ///
    /// A test seam: onQueryFinished() discards a reply whose generation is
    /// stale, so a test standing in for the worker has to know the current one.
    quint64 currentGenerationForTesting() const { return m_generation; }

    /// The query the visible list was actually built from.
    ///
    /// A test seam: a refresh re-runs THIS, never the text in the query bar,
    /// and the difference is only observable through the value itself. The
    /// generation counter cannot stand in for it, since a legitimate refresh
    /// bumps the generation too.
    QString lastRunQueryForTesting() const { return m_lastQuery; }

    /// Puts the window into the state refreshCurrentQuery() leaves it in, and
    /// returns the generation the refresh's replies must carry.
    ///
    /// A test seam, because refreshCurrentQuery() returns early without a
    /// worker and a bare window has none. It sets only what decides whether a
    /// batch is a refresh's, not what the real function asks the worker to do.
    quint64 beginRefreshForTesting()
    {
        m_refreshGeneration = ++m_generation;
        m_refreshThreads.clear();
        return m_refreshGeneration;
    }

    /// The query generation as it stood when the held edits were last flushed,
    /// or 0 if they never have been.
    ///
    /// Recorded because the ORDER of the flush and the sync-end refresh is the
    /// whole of one defect and both leave identical end states: a test that
    /// looks afterwards passes whichever ran first. Compared against the
    /// generation the refresh bumps, this says which came first.
    quint64 flushGenerationForTesting() const { return m_flushGeneration; }

    /// The generation a database-stats reply must carry to be accepted.
    ///
    /// A test seam, for the same reason as the one above: onDatabaseStatsReady
    /// discards a reply belonging to a dialog that has since been closed and
    /// reopened, so a test standing in for the worker needs the current value.
    quint64 statsGenerationForTesting() const { return m_statsGeneration; }

    /// Whether a stale-thread recovery is still waiting for its result.
    ///
    /// A test seam. The recovery target is cleared as a matter of course by any
    /// query the user runs, so "is it still set immediately after the button"
    /// is the only way to see that it survived the slot that set it.
    bool hasPendingRecoveryForTesting() const
    {
        return !m_recoverThreadId.isEmpty();
    }

    /// The placeholder's queries, in the order requestCounts() asks for them.
    ///
    /// A test seam. The worker's reply is paired with these POSITIONALLY, so a
    /// test standing in for it has to know the order, and that order now
    /// depends on config rather than on a fixed list.
    QStringList placeholderQueriesForTesting() const
    {
        return placeholderQueries();
    }

    /// The helper lines as the pane would render them.
    QList<HtmlBuilder::PlaceholderHelper> placeholderHelpersForTesting() const
    {
        return placeholderHelpers();
    }

    /// The generation the next counts reply must carry to be accepted.
    quint64 countsGenerationForTesting() const { return m_countsGeneration; }

    /// The query bar's text, and the account the selector is scoped to
    /// (empty for "All accounts"). Both are what a rule preview writes: the
    /// bar so the user can see and edit what ran, and the selector because
    /// runQuery() wraps the text in the selected account's scope, which would
    /// double-scope a rule query that already names its own path.
    QString queryTextForTesting() const;
    QString selectedAccountForTesting() const;

    /// Scopes the view to one account, as choosing it in the selector does.
    /// A test for the rule preview needs this: with no account selected the
    /// box already sits at "All accounts", so asserting that a preview leaves
    /// it there passes whether or not the preview clears it.
    void selectAccountForTesting(const QString &key);

    /// Runs a rule preview without the dialog, which the offscreen platform
    /// cannot click a button in.
    void previewRuleQueryForTesting(const QString &query)
    {
        onRulePreviewRequested(query);
    }

protected:
    void closeEvent(QCloseEvent *event) override;

    /// Claims Return back for the query bar. Return is bound to open_thread as
    /// a WindowShortcut, and a shortcut outranks the focused widget, so without
    /// this the action fires from inside the bar and the query never runs.
    bool eventFilter(QObject *watched, QEvent *event) override;

public:
    /// Whether the result of a query is shown as a flat list of threads rather
    /// than as an expandable tree.
    ///
    /// Only the Sent button asks for Yes. Every other route runs the no-arg
    /// slot, which passes No, so flat mode cannot outlive the view that asked
    /// for it: the same query typed by hand comes back as a tree.
    enum class FlatResult { No, Yes };
    Q_ENUM(FlatResult)

    /// Whether runQuery() applies the selected account's scope to the bar text.
    ///
    /// Apply is right for anything the user typed or a saved query put there.
    /// AlreadyScoped is for a built-in filter, whose text was resolved through
    /// Config::resolvedQuery(query, accountKey) and already carries the scope:
    /// scoping it a second time would wrap path:"work/Sent/**" in
    /// path:"work/**", which is the double scope item 93 exists to avoid.
    enum class AccountScope { Apply, AlreadyScoped };
    Q_ENUM(AccountScope)

private:
    /// The real query runner. Kept off the slot list deliberately: a slot with
    /// a defaulted argument does not satisfy QObject::connect, which matches
    /// signal and slot arity at compile time, so the zero-argument slot below
    /// is what widgets connect to.
    void runQuery(FlatResult flat,
                  AccountScope scope = AccountScope::Apply);

    /// Builds the row of saved-query buttons, the overflow menu and Sent.
    ///
    /// Its own row since item 23: an unbounded list of buttons sharing the
    /// query row squeezed the field, which is the whole reason for the
    /// pinned/unpinned split.
    void buildSavedQueryRow(QWidget *parent, QVBoxLayout *layout);

    /// Runs a saved query, taking its account scope through the dropdown.
    ///
    /// Not by pre-scoping the text: runQuery() already wraps the query in the
    /// selected account's path, so a scope baked in here would be applied
    /// twice. Setting the dropdown also shows the user what scope they are in.
    void runSavedQuery(const SavedQuery &saved);

    /// Runs a built-in filter in whatever account scope is currently selected.
    ///
    /// The opposite of runSavedQuery() in the one way that matters: it does NOT
    /// touch the account box. A filter narrows what the user is already looking
    /// at, so the dropdown is its input rather than something it overwrites,
    /// which is item 90's defect and item 93's design.
    ///
    /// The query text is resolved here rather than left to runQuery()'s own
    /// scoping, because a generator must be asked for the account's own query:
    /// Sent wrapped in a scope double-scopes and works only by accident of
    /// path: being hierarchical. See Config::resolvedQuery(query, accountKey).
    void runFilter(const SavedQuery &filter);

    /// Checks the filter button whose query is what the bar currently holds,
    /// and unchecks the rest.
    ///
    /// Derived from the query TEXT rather than from the last button pressed, so
    /// editing the query by hand clears the highlight and typing a filter's
    /// query lights it. Resolved against the account box, which is why changing
    /// the account keeps the highlight: the same filter resolves to a different
    /// query and both are still "Inbox".
    void updateFilterButtons();

    /// Names the current query and stores it in queries.json.
    void saveCurrentQuery();

    /// Rebuilds the saved-query row in place after the stored list changed.
    void rebuildSavedQueryRow();

    /// Hangs Edit, Pin/Unpin and Delete on a saved query's button or menu
    /// entry. The only route to changing a stored query from the UI.
    void addSavedQueryActions(QWidget *target, const SavedQuery &saved);

    /// Replaces the entry named `originalName`, writes the file and rebuilds
    /// the row. An empty `replacement.name` deletes it instead.
    ///
    /// Matched on the ORIGINAL name, not the replacement's: a rename otherwise
    /// leaves the old entry in place and adds a second one.
    void replaceSavedQuery(const QString &originalName,
                           const SavedQuery &replacement);

    void editSavedQuery(const SavedQuery &saved);
    void deleteSavedQuery(const SavedQuery &saved);

private slots:
    void runCurrentQuery() { runQuery(FlatResult::No); }

    /// Brings back a thread that stopped matching, and restores the reader's
    /// place inside it.
    ///
    /// Runs `thread:<id>` so the whole conversation is listed rather than the
    /// single message, then expands it and selects `messageId` once the rows
    /// exist. Both steps are queued round-trips to the worker, so the ids are
    /// remembered in m_recoverThreadId / m_recoverMessageId and acted on as the
    /// replies arrive.
    ///
    /// A slot because MessageView's notice connects to it, and because the
    /// sequencing above is only testable by driving it through the same entry
    /// point the button uses.
    void recoverStaleThread(const QString &threadId, const QString &messageId);

    /// Drills into the double-clicked row: the whole thread, expanded, alone in
    /// the view, with that row's own message in the pane.
    void onRowDoubleClicked(const QModelIndex &index);

    /// Selects the remembered message once its thread's rows have loaded.
    void applyPendingRecovery();
    void onThreadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void onQueryFinished(int total, quint64 generation);
    void onThreadSelected(const QModelIndex &current, const QModelIndex &previous);

    /// Keeps the status bar's selection count and the multi-select guard in
    /// step with selections that never move the current index.
    void onSelectionChanged();

    /// Pops up the thread-list context menu, preserving a multi-row selection
    /// the click lands inside.
    void showThreadContextMenu(const QPoint &pos);
    /// Paints `messages` into the message pane. Callers own the guards.
    void renderMessages(const QVector<MessageRef> &messages);

    /// Asks the worker for a thread's reply tree when its row is expanded.
    void onThreadExpanded(const QModelIndex &index);

    /// Fills in the expanded thread's message rows.
    void onThreadTreeLoaded(const QVector<MessageNode> &nodes,
                            quint64 generation);

    /// Renders the single message a message row asked for.
    void onMessageLoaded(const QVector<MessageRef> &messages,
                         quint64 generation);
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

    /// Runs a search asked for from the message pane.
    ///
    /// `mode` says whether to replace the query bar, narrow it, or narrow it
    /// by everything that is not this value. The panes carry a finished query
    /// and no knowledge of the bar; the combining happens here, because only
    /// the window can see what the bar holds.
    void runSearchFromPane(const QString &query, SearchTerm::SearchMode mode);

    /// Runs one tagging rule's query in the thread list, so the user can see
    /// which mail it collects. The rules dialog stays open; the point is to
    /// compare the rule against its results.
    void onRulePreviewRequested(const QString &query);

    /// Opens the auto-tagging rules editor, or raises the one already open.
    /// `seed` is an optional rule to open on, used by Create tagging rule on a
    /// saved query. A default-constructed TagRule (empty query) means none.
    void showTagRulesDialog(const TagRule &seed = TagRule());

    /// Message counts for the rules dialog's queries, in the order it asked
    /// for them. Does nothing if the dialog has since closed, or if a newer
    /// request has superseded this one.
    void onRuleCountsReady(const QVector<int> &counts, quint64 generation);

private:
    void buildUi();

    /// Restores window geometry, splitter and thread-list header widths.
    /// A missing or rejected blob leaves the buildUi() defaults in place.
    void restoreUiState();

    /// The thread row containing an index: itself for a thread row, its parent
    /// for a message row.
    QModelIndex threadRowOf(const QModelIndex &index) const;

    /// Selects a whole row. QTreeView has no selectRow of its own.
    void selectRowAt(const QModelIndex &index);

    /// Selects the top-level thread row at `row`.
    void selectThreadRow(int row);
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
    /// One placeholder line: the query it counts, and how to label the answer.
    ///
    /// The label is a callable rather than a string because the count is not
    /// known until the worker replies, and `tr("%n ...")` has to be given the
    /// number to pick its plural form.
    ///
    /// Query and label travel together deliberately. The version this replaced
    /// held them in two arrays indexed in parallel, where inserting an entry in
    /// one and not the other put a real number against the wrong name.
    struct PlaceholderLine {
        QString query;
        std::function<QString(int)> label;
    };

    /// The placeholder's lines, in render order.
    ///
    /// Built per call rather than cached: the sent and drafts lines come from
    /// config, and a cache would be a second source of truth for the pairing
    /// the counts reply depends on.
    QList<PlaceholderLine> placeholderLines() const;

    /// Just the queries, in the order requestCounts() asks for them.
    QStringList placeholderQueries() const;

    QList<HtmlBuilder::PlaceholderHelper> placeholderHelpers() const;

    /// Puts the warning count in the status bar. Nothing modal: see
    /// configProblems() for why the dialog is not raised here.
    void applyWarnings();
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

    /// What a tag action acts on.
    ///
    /// Since item 108 a thread ROW means the one message its card displays, so
    /// Message is the default and Thread is the explicit choice the user makes
    /// through the "Whole thread" submenu. Before that there was no choice:
    /// a thread row always meant the conversation.
    enum class TagScope {
        Message,   ///< The message each selected row displays.
        Thread,    ///< Every message of each selected row's thread.
    };

    void tagSelected(const QStringList &add, const QStringList &remove,
                     const QString &description,
                     TagScope scope = TagScope::Message);

    /// Starts, restarts or cancels the mark-read timer for a newly opened
    /// MESSAGE. Cancels outright for one that is not unread, so an already read
    /// message never schedules a write that would change nothing.
    ///
    /// Takes the id and the state separately because the two come from
    /// different places: a reply row has a MessageNode, and a thread row has
    /// only its summary, whose `unread` is a union over the conversation.
    void scheduleMarkRead(const QString &messageId, bool unread);

    /// The message id of the thread the pane was opened from, or empty.
    QString currentThreadFirstMessageId() const;

    /// Removes `unread` from the thread the timer was armed for, if it is still
    /// the one on screen.
    void markCurrentThreadRead();

    /// Redraws the unsynced-edits indicator from pendingEditCount().
    void updatePendingIndicator();

    /// Arms the debounce that syncs a confirmed tag edit out on its own
    /// (item 71). Does nothing when the delay is negative, when no sync command
    /// is configured, or when nothing is actually pending.
    void scheduleAutoSync();

    /// Starts the debounced automatic sync, unless a sync is already running
    /// (local or external) or the edits it would carry are already gone.
    ///
    /// Q_INVOKABLE so a test can fire the debounce without waiting it out.
    Q_INVOKABLE void runAutoSync();

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
    /// The "Whole thread" submenu, built fresh for each parent that needs one.
    ///
    /// A QMenu lives in one menu tree, so the menu bar and the context menu get
    /// their own instance. The actions inside are shared, which is what has to
    /// stay consistent between them.
    QMenu *buildThreadActionsMenu(QWidget *parent);

    /// Per-tag counts across the selected rows, for the tag dialog.
    QHash<QString, int> selectionTagCounts() const;

    /// True when every selected row already carries \p tag, which is what a
    /// toggle asks before choosing its direction.
    ///
    /// Under Message scope each row answers about what it STANDS FOR: a reply
    /// row about its message, a thread row about the message its card
    /// displays. Asking a reply's thread makes a toggle one-way, since the
    /// message-scoped write never changes the thread's tags.
    ///
    /// Under Thread scope a row answers about its whole thread, so the
    /// question matches the write the thread actions are about to make.
    bool everySelectedRowHasTag(const QString &tag,
                                TagScope scope = TagScope::Message) const;

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
    ///
    /// Invokable so a test can record an edit against a known account without a
    /// worker: this is where m_editedAccounts is populated, and item 54's
    /// draining of it cannot be observed otherwise.
    Q_INVOKABLE void sendThreadTagChange(const QStringList &threadIds,
                             const QStringList &add,
                             const QStringList &remove,
                             const QString &description);

    /// The same for individual MESSAGES, without touching the undo stack.
    /// Both tagSelected() and MessageTagCommand route through this.
    void sendMessageTagChange(const QStringList &messageIds,
                              const QStringList &add,
                              const QStringList &remove,
                              const QString &description);

    /// Moves messages into `destFolder` and applies the tags that go with it.
    ///
    /// The counterpart to sendMessageTagChange() for the one action that is
    /// not purely a tag change. Both trashSelected() and MoveCommand route
    /// through this.
    ///
    /// The tags are NOT applied here: they are applied when the worker
    /// confirms the move, in onMessagesMoved(). Tagging first would leave a
    /// message marked `deleted` in a folder it never left if the rename
    /// failed, which is the half-done state item 103 exists to remove.
    ///
    /// `add` may contain the placeholder kOriginTagPlaceholder, which
    /// onMessagesMoved() replaces with `deleted-from:<origin>` per message.
    /// The origin is not known until the worker reports it, and it differs per
    /// message in a multi-row selection.
    /// `fromUndo` marks a move the undo stack itself started, which must NOT
    /// push a command of its own when it is confirmed. See onMessagesMoved().
    /// `wholeThreadIds`, when non-empty, says this move covers every message
    /// of those threads, so the optimistic repaint updates each thread's
    /// SUMMARY rather than each message's node. A thread row's card reads the
    /// summary, so a thread-scoped move that updated only nodes repainted the
    /// replies and left the root card stale until the next query.
    void sendMove(const QStringList &messageIds, const QString &destFolder,
                  const QStringList &add, const QStringList &remove,
                  const QString &description, bool fromUndo = false,
                  const QStringList &wholeThreadIds = {});

    /// Moves each selected row's message to its account's trash, tagging it
    /// `deleted` and recording where it came from.
    void trashSelected();

    /// The half of trashSelected() that does the work, given the messages and
    /// their paths.
    ///
    /// Paths are passed in rather than looked up, because the thread-scoped
    /// caller has messages the MODEL has never seen: an unexpanded thread
    /// holds no node for its replies, so a model lookup resolves them to no
    /// account and the move is silently dropped. The worker supplies them.
    void trashMessages(const QStringList &messageIds,
                       const QHash<QString, QString> &pathById,
                       int messageCount,
                       const QStringList &wholeThreadIds = {});

    /// Moves every message of each selected THREAD to its account's trash.
    ///
    /// Asynchronous, unlike its message-scoped twin: the ids and paths of an
    /// unexpanded thread's messages live only in the database, so this asks
    /// the worker and finishes in onThreadMessagesResolved().
    void trashSelectedThreads();

    /// The thread ids the selection covers, resolving a reply row to its own
    /// thread. scopeFor() reports a reply under messageIds instead, which left
    /// a thread action on a reply row doing nothing at all.
    QStringList selectedThreadIds() const;

    /// The inverse of trashSelectedThreads(): moves every message of each
    /// selected thread back where it came from.
    void restoreSelectedThreads();

    /// Runs the thread-scoped delete once the worker has resolved the
    /// threads to messages.
    void onThreadMessagesResolved(const QStringList &messageIds,
                                  const QStringList &paths,
                                  const QStringList &tags,
                                  const QString &requestTag);

    /// The inverse: moves each selected row's message back to the folder its
    /// `deleted-from:` tag names, stripping both tags.
    ///
    /// `fallbackToInbox` decides what happens to a message with NO origin tag,
    /// and the two callers want opposite things. From the trash view the
    /// message is demonstrably in the trash, trashed by another client, and
    /// must still come out: it goes to the inbox, reported. From a second
    /// press of Delete it is not in the trash at all and merely wears a stale
    /// tag, so the tag comes off and the file stays where it is.
    void restoreSelected(bool fallbackToInbox = false);

    /// Restore as reached from the TRASH VIEW: resolves each selected
    /// message against the database first, then moves it.
    ///
    /// Asynchronous, unlike restoreSelected(), and that is the point. The
    /// model's tags come from the query, so a row whose delete has not been
    /// re-queried still carries its pre-delete tags; reading the origin from
    /// there found none and sent the message to the INBOX instead of the
    /// folder it came from, one run in three.
    void restoreSelectedFromTrash();

    /// Runs the query that finds mail tagged `deleted` whose file never left
    /// its original folder, which is what every version before item 103 left
    /// behind. It REPORTS and moves nothing: acting on its own would be a bulk
    /// delete with no selection behind it, and the user asked for something
    /// they could come back to and review.
    ///
    /// Repeatable rather than a one-time startup migration, for the same
    /// reason: mail reaches this state again whenever another client tags
    /// without moving.
    void showStrandedDeletedMail();

    /// Moves each resolved message home, using the tags and paths the WORKER
    /// reported rather than anything the model holds.
    void restoreResolvedMessages(const QStringList &messageIds,
                                 const QStringList &paths,
                                 const QStringList &tags);

    /// The messages a resolveMessages() request was made for.
    QStringList m_pendingRestoreIds;

    /// The account's inbox FOLDER name, discovered from its inbox query.
    ///
    /// Never hardcoded: the real Maildir has `Inbox` and a fixture has
    /// `inbox`, and assuming either would create a second folder beside the
    /// real one on the side that disagreed.
    QString inboxFolderFor(const Account &account) const;

    /// Whether the current query IS a trash view, for either scope.
    ///
    /// Compared against the trash generator's own query rather than against a
    /// tag: the view is path-based so mail trashed by another client appears
    /// in it, and such a message carries no tag of ours.
    bool isShowingTrash() const;

    /// The `deleted-from:` tag naming `dbRelativeFolder`, or empty when no
    /// account owns it.
    ///
    /// One rule for both sites that need the tag: the delete that writes it
    /// and the restore that strips it. Deriving it twice let them disagree,
    /// and a restore stripped a tag that had never been written.
    QString originTagFor(const QString &dbRelativeFolder) const;

    /// The account whose maildir contains `path`, or an invalid account when
    /// no configured maildir does.
    ///
    /// Resolved from the PATH rather than from the thread's account tag. The
    /// tag is optional config, so an account without one would resolve to
    /// nothing and silently disable Delete; the maildir prefix is what makes
    /// a message belong to an account in the first place.
    Account accountForMessagePath(const QString &path) const;

    /// Confirms a move: applies the tags the move was asked to carry, with the
    /// origin placeholder resolved per message.
    void onMessagesMoved(const QMap<QString, QString> &originByMessageId,
                         const QString &destFolder);

    /// What a move asked to be tagged, held until the worker confirms it.
    ///
    /// A FIFO and not a map keyed on the destination: two Deletes in one
    /// account before the first confirmation arrives name the same folder, so
    /// a keyed map dropped the first entry and left the second confirmation
    /// with nothing to apply. That file reached the trash carrying neither
    /// `deleted` nor `deleted-from:`, unrestorable and invisible to a
    /// `tag:deleted` query. The worker moves one batch at a time and emits in
    /// request order, so position alone matches a confirmation to its request.
    struct PendingMove {
        QStringList add;
        QStringList remove;
        QString description;
        /// Set for a move the undo stack started, which must not push again.
        bool fromUndo = false;
    };
    QQueue<PendingMove> m_pendingMoves;

    /// The threads a resolveThreadMessages() request was made for, held until
    /// the answer arrives so the optimistic repaint knows the move is
    /// thread-scoped.
    QStringList m_pendingThreadScope;

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

    /// Re-runs the current query and reconciles the result into the model.
    ///
    /// The non-destructive counterpart to `runCurrentQuery()`, and what a sync
    /// fires: nothing is cleared, so the selection, the expanded threads, the
    /// undo stack and the message being read all survive. New threads appear
    /// where the sort puts them and threads that stopped matching leave.
    ///
    /// Does nothing when no query has run yet, since there is nothing to
    /// re-run.
    void refreshCurrentQuery();

    /// Shows or hides the message pane's "no longer matches" notice.
    ///
    /// Called after a refresh, which is the only thing that can remove a row
    /// from under a reader. A thread read out of an Unread view is the ordinary
    /// case: the pane keeps rendering it, correctly, while the list no longer
    /// offers it anywhere, and without this the message quietly becomes an
    /// orphan with no route back.
    void updateStaleThreadNotice();


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

    /// A MOVE not yet sent, for the same reason a tag edit is held.
    ///
    /// A separate queue rather than an entry in m_heldEdits, because a move is
    /// not a tag change and cannot be replayed as one: pushing it through the
    /// edit queue would apply `deleted` and never move the file, leaving the
    /// message reading as deleted while still sitting in the inbox. Item 106
    /// recorded what a dropped held edit costs, and a move dropped the same
    /// way is worse: the tag lands and the file does not.
    struct HeldMove {
        QStringList messageIds;
        QString destFolder;
        QStringList add;
        QStringList remove;
        QString description;
        /// Carried through the hold, or a move undone during a sync would
        /// push a command when it is finally flushed.
        bool fromUndo = false;
    };
    QVector<HeldMove> m_heldMoves;

    quint64 m_flushGeneration = 0;

    friend class ThreadTagCommand;
    friend class MessageTagCommand;
    friend class MoveCommand;

    /// Stands in for `deleted-from:<origin>` between asking for a move and
    /// learning where each message actually came from. Not a tag anyone ever
    /// sees: onMessagesMoved() substitutes the real one per message before
    /// anything is written.
    static const QString &kOriginTagPlaceholder();

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
    /// Save query, beside the field. Driven by the save_query action.
    QToolButton *m_saveQueryButton = nullptr;
    /// The built-in filter buttons, by generator, so the one matching the
    /// current view can be shown as checked. Kept because the buttons are built
    /// in a loop and are otherwise unreachable without findChild() on a name.
    QHash<QString, QToolButton *> m_filterButtons;
    /// Whether deleting a saved query asks first. Always true outside tests.
    bool m_confirmDelete = true;
    QueryCompleter *m_queryCompleter = nullptr;
    /// Its own type, not the QTreeView base. The strip painting and the
    /// expander column are ThreadListView's, and holding the base here only
    /// hid that from every reader.
    ThreadListView *m_threadView = nullptr;

    /// Right-click menu for the thread list, holding the same QActions the
    /// menu bar does.
    QMenu *m_threadContextMenu = nullptr;
    QSplitter *m_splitter = nullptr;

    /// Below this the message pane shows a sliver of a rendered mail and is
    /// not worth the space it occupies. This is a floor, reached only when a
    /// restored position does not fit, so it is set at the width mail is
    /// readable at rather than the width it is merely visible at: at 200 the
    /// placeholder's own text wraps every couple of words.
    static constexpr int kMinMessagePaneWidth = 300;
    QComboBox *m_accountBox = nullptr;
    QComboBox *m_sortOrder = nullptr;
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
    BusyIndicator *m_syncProgress = nullptr;

    /// The last counts the worker answered, one per placeholderLines() entry.
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

    /// The open rules dialog, or null. Held so a counts reply can reach it,
    /// and cleared when it closes: a reply that arrives after the dialog is
    /// gone must find nothing rather than a dangling pointer. A QPointer for
    /// the same reason m_overviewCounts is one, and the window is open wider
    /// here, since counting every rule takes seconds.
    QPointer<TagRulesDialog> m_tagRulesDialog;

    /// Generation of the rules dialog's counts request.
    ///
    /// Its OWN counter, deliberately not m_generation. That one is the QUERY
    /// generation, and onThreadsReady, onQueryFinished, onThreadLoaded,
    /// onThreadTreeLoaded and onMessageLoaded all compare against it directly:
    /// bumping it here would discard whatever thread load was in flight when
    /// the user pressed Count matches, blanking the message pane for a reason
    /// that has nothing to do with the query. Not m_countsGeneration either,
    /// though that one is closer: it belongs to the placeholder pane's THREAD
    /// counts, and sharing it would let each cancel the other's reply. The two
    /// arrive on different signals (countsReady against messageCountsReady),
    /// so they can never be confused for one another and need not share a
    /// counter.
    quint64 m_ruleCountGeneration = 0;

    /// The generation of a REFRESH query, run after a sync to bring the list
    /// up to date without disturbing it.
    ///
    /// Numbered from the same counter as an ordinary query, so a refresh and a
    /// user query can never share an id, but tracked separately because the two
    /// consume their results differently: an ordinary query appends into a
    /// cleared model as batches arrive, while a refresh accumulates every batch
    /// and reconciles once at the end. Zero when no refresh is in flight.
    ///
    /// A user query started while a refresh is running silently supersedes it:
    /// the refresh's batches are still collected but its result is dropped, for
    /// the same reason the generation counter exists at all. Reconciling it
    /// would fight the query the user just typed.
    quint64 m_refreshGeneration = 0;

    /// Threads collected from a refresh query, complete only once its
    /// queryFinished arrives.
    ///
    /// Held rather than applied per batch because reconcile() needs the WHOLE
    /// result to tell a thread that stopped matching from one that simply has
    /// not arrived yet. Reconciling batch by batch would delete every row the
    /// first batch did not contain, emptying the list and refilling it, which
    /// is the reset this exists to avoid.
    QVector<ThreadSummary> m_refreshThreads;

    /// The thread and message a stale-thread recovery is waiting to select.
    ///
    /// Recovery spans two queued round-trips (the query, then the reply walk),
    /// so the target cannot be a local variable. Cleared once the selection
    /// lands, or by any query the user runs in the meantime: that is them
    /// choosing to go somewhere else, and restoring a selection into a result
    /// they did not ask for would yank the view.
    QString m_recoverThreadId;
    QString m_recoverMessageId;

    /// The thread the pane's current MESSAGE belongs to.
    ///
    /// Selecting a message row clears m_currentThreadId (the pane shows one
    /// message, not a conversation), so without this a reader three replies
    /// deep has no thread to check against the refreshed list, and the stale
    /// notice never appears for them. Not obtainable from the model after the
    /// fact: ThreadListModel::threadIdForMessage() searches the rows, and by
    /// the time this is needed the thread has left them.
    QString m_currentMessageThreadId;

    /// True while the status bar is showing this window's own "Background sync
    /// running..." message.
    ///
    /// A refresh after a cron sync is deliberately silent, so it writes nothing
    /// to the bar. That left the running message standing after the sync
    /// finished, because the "completed" message it replaced was the only thing
    /// that ever cleared it. Silence means saying nothing NEW, not leaving a
    /// stale claim on screen: this marks the one string the Idle branch is
    /// entitled to retire, so it cannot overwrite a selection count or anything
    /// else the user is actually reading.
    bool m_announcedExternalSync = false;

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

    /// Whether the CURRENT view is the Sent one, and therefore whether it is
    /// flat and carries recipients.
    ///
    /// Held rather than recomputed because a background refresh re-runs the
    /// same query without going through the Sent button: without this, the
    /// first cron sync would silently turn a Sent view back into a tree of
    /// senders while the user was reading it.
    bool m_sentView = false;
    QString m_lastQuery;
    QString m_currentThreadId;

    /// The message a MESSAGE row is showing, empty whenever the pane holds a
    /// whole thread. The two are mutually exclusive and each clears the other,
    /// so a late reply can tell which kind of selection it belongs to.
    QString m_currentMessageId;

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

    /// The MESSAGE m_markReadTimer will mark read. Compared against what the
    /// pane is showing when it fires, so a timer that outlives its message
    /// does nothing rather than marking the wrong one.
    ///
    /// A message id, not a thread id, since item 87. The timer used to mark
    /// the whole thread, which was coherent while a root card rendered the
    /// whole conversation and stopped being so when item 66 made it render
    /// one message: reading one message marked replies read that had never
    /// been displayed, and with maildir.synchronize_flags on that reaches the
    /// server.
    QString m_markReadMessageId;

    /// Debounces the automatic sync that follows a tag edit (item 71).
    ///
    /// Single-shot and RESTARTED by every confirmed edit, for the same reason
    /// m_markReadTimer is: tagging a multi-row selection confirms one write per
    /// thread, and one sync per thread is exactly what a debounce exists to
    /// prevent.
    QTimer *m_autoSyncTimer = nullptr;

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
    /// Invokable for the same reason as sendThreadTagChange(): it is the only
    /// view onto m_editedAccounts, and a count that reaches zero while the set
    /// stays full looks correct and still syncs the wrong channels.
    Q_INVOKABLE QStringList pendingSyncChannels() const;
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

/// Undo entry for a tag change over individual MESSAGES.
///
/// Stores message ids, unlike ThreadTagCommand, and that difference is the
/// point rather than an inconsistency: a message row acts on one message, so
/// re-resolving its thread on undo would restore tags across every sibling the
/// action never touched.
class MessageTagCommand : public QUndoCommand
{
public:
    MessageTagCommand(MainWindow *window, const QStringList &messageIds,
                      const QStringList &add, const QStringList &remove,
                      const QString &description)
        : QUndoCommand(description), m_window(window),
          m_messageIds(messageIds), m_add(add), m_remove(remove),
          m_description(description) {}

    /// The stack calls redo() when the command is pushed, by which point the
    /// change has already been sent, so the first call is skipped.
    void redo() override
    {
        if (m_firstRedo) {
            m_firstRedo = false;
            return;
        }
        m_window->sendMessageTagChange(m_messageIds, m_add, m_remove,
                                       m_description);
    }

    void undo() override
    {
        m_window->sendMessageTagChange(
            m_messageIds, m_remove, m_add,
            QStringLiteral("Undo %1").arg(m_description));
    }

private:
    MainWindow *m_window;
    QStringList m_messageIds;
    QStringList m_add;
    QStringList m_remove;
    QString m_description;
    bool m_firstRedo = true;
};

/// Undo entry for a message MOVE, which is a file rename plus a tag change.
///
/// The destination is CARRIED rather than derived, and that is the whole
/// reason `deleted-from:` exists at all. A Maildir filename does not record
/// where a message came from, and once the file has moved notmuch cannot
/// answer either, so an undo that recomputed the origin would have nothing to
/// recompute it from. Each message carries its own, since one selection can
/// span folders and accounts.
///
/// Grouped by destination: undoing a delete of five messages from three
/// folders is three moves, not five, because moveMessages() takes one folder
/// per call.
class MoveCommand : public QUndoCommand
{
public:
    /// `originByMessageId` names where each message came FROM, and
    /// `destFolder` where they all went.
    MoveCommand(MainWindow *window,
                const QMap<QString, QString> &originByMessageId,
                const QString &destFolder, const QStringList &add,
                const QStringList &remove, const QString &description)
        : QUndoCommand(description), m_window(window),
          m_origins(originByMessageId), m_dest(destFolder), m_add(add),
          m_remove(remove), m_description(description) {}

    /// The stack calls redo() when the command is pushed, by which point the
    /// move has already been sent, so the first call is skipped. Same shape as
    /// the two tag commands above.
    void redo() override
    {
        if (m_firstRedo) {
            m_firstRedo = false;
            return;
        }
        // Also fromUndo: a redo replays a command that is ALREADY on the
        // stack, so confirming it must not push a duplicate either.
        m_window->sendMove(m_origins.keys(), m_dest, m_add, m_remove,
                           m_description, true);
    }

    void undo() override
    {
        // Back to each message's OWN folder, one call per distinct
        // destination. The tags invert with the direction: what the delete
        // added, the undo removes.
        QHash<QString, QStringList> byOrigin;
        for (auto it = m_origins.cbegin(); it != m_origins.cend(); ++it) {
            if (!it.value().isEmpty())
                byOrigin[it.value()].append(it.key());
        }
        for (auto it = byOrigin.cbegin(); it != byOrigin.cend(); ++it) {
            // fromUndo: this move is the undo, so its confirmation must not
            // push a command of its own. Without it the stack grew on every
            // press and a second undo re-deleted the message.
            m_window->sendMove(it.value(), it.key(), m_remove, m_add,
                               QStringLiteral("Undo %1").arg(m_description),
                               true);
        }
    }

private:
    MainWindow *m_window;
    QMap<QString, QString> m_origins;
    QString m_dest;
    QStringList m_add;
    QStringList m_remove;
    QString m_description;
    bool m_firstRedo = true;
};
