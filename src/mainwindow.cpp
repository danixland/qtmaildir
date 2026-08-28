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

#include "mainwindow.h"

#include <algorithm>

#include "maildirname.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScrollBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "composecontext.h"
#include "composewindow.h"
#include "mailsync.h"
#include "messageview.h"
#include "mimeparser.h"
#include "notmuchworker.h"
#include "querycompleter.h"
#include "carddelegate.h"
#include "busyindicator.h"
#include "cardlayout.h"
#include "searchterm.h"
#include "tagchip.h"
#include "tagdialog.h"
#include "pendingchangesdialog.h"
#include "savequerydialog.h"
#include "tagrulesdialog.h"
#include "threadlistmodel.h"
#include "threadlistview.h"
#include "version.h"

QStringList MainWindow::registeredActionNames() const
{
    // Derived from the actions themselves, so it cannot drift from what
    // registerActions() really installed.
    QStringList names = m_actions.keys();
    names.sort();
    return names;
}

QString MainWindow::cidPrefixForIndex(int index)
{
    // "m<index>" is digits only after the 'm', so it cannot contain '!'.
    return QStringLiteral("m%1").arg(index);
}

QString MainWindow::uiStatePath()
{
    // GenericStateLocation, not StateLocation: the latter appends both the
    // organization and the application name, and both are "qtmaildir" here,
    // so it yields ~/.local/state/qtmaildir/qtmaildir. Built the same way
    // Config::defaultPath() builds its own.
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    return base + QStringLiteral("/qtmaildir/uistate.conf");
}

namespace {
/// Overridden only by setLocksPathForTesting(); "/proc/locks" in every real run.
QString g_locksPath = QStringLiteral("/proc/locks");

} // namespace

/// Doc comment on the declaration. Separators and control characters are
/// replaced rather than stripped so a subject carrying one yields a readable
/// name, instead of being truncated to its last segment by the basename
/// reduction Attachment::safeFilename() performs afterwards.
QString MainWindow::defaultMessageFilename(const QString &subject)
{
    QString name = subject.simplified();
    for (QChar &c : name) {
        if (c == QLatin1Char('/') || c == QLatin1Char('\\')
            || c == QLatin1Char(':') || c.category() == QChar::Other_Control) {
            c = QLatin1Char('-');
        }
    }
    // Long subjects exist and many filesystems stop at 255 bytes. Truncated
    // before the extension is added, so the cut cannot eat it.
    name.truncate(120);
    name = name.trimmed();

    // A leading dot makes the file HIDDEN on every Unix desktop, and a subject
    // beginning with one is ordinary ("...and another thing", or a traversal
    // whose separators were just replaced above, leaving "..-..-etc-passwd").
    // The write succeeds and the user cannot see the file they just saved.
    // Measured: QDir::entryList omits it without QDir::Hidden, which is how
    // this was found.
    while (name.startsWith(QLatin1Char('.')))
        name.remove(0, 1);
    name = name.trimmed();

    if (name.isEmpty())
        name = QStringLiteral("message");
    return name + QStringLiteral(".eml");
}

void MainWindow::setLocksPathForTesting(const QString &path)
{
    g_locksPath = path;
}

QString MainWindow::locksPath()
{
    return g_locksPath;
}

/// The thread row containing an index: the index itself when it is already a
/// thread row, its parent when it is a message row.
///
/// Replaces the arithmetic on row numbers that a table permitted. In a tree a
/// row number only identifies a row within one parent, so "current.row() + 1"
/// means the next SIBLING, which under an expanded thread is the next reply.
QModelIndex MainWindow::threadRowOf(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};
    return index.parent().isValid() ? index.parent() : index;
}

/// Selects a whole row, the way QTableView::selectRow did.
///
/// QTreeView has no selectRow, and SelectRows on the selection model is not a
/// substitute: it governs what a click extends to, not what a programmatic
/// select() covers.
void MainWindow::selectRowAt(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    m_threadView->selectionModel()->select(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_threadView->setCurrentIndex(index);
}

/// Selects the top-level thread row at `row`.
void MainWindow::selectThreadRow(int row)
{
    selectRowAt(m_model->index(row, 0, QModelIndex()));
}

void MainWindow::restoreUiState()
{
    QSettings state(uiStatePath(), QSettings::IniFormat);

    // Every restore is conditional: an absent or rejected blob must leave the
    // buildUi() defaults alone rather than produce a zero-size window.
    const QByteArray geometry = state.value(QStringLiteral("window/geometry"))
                                    .toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    const QByteArray windowState = state.value(QStringLiteral("window/state"))
                                       .toByteArray();
    if (!windowState.isEmpty()) {
        restoreState(windowState);
    }

    const QByteArray splitter = state.value(QStringLiteral("window/splitter"))
                                    .toByteArray();
    if (!splitter.isEmpty()) {
        m_splitter->restoreState(splitter);
    }

    // No thread-list header state is read. The pane is one column drawn whole
    // by CardDelegate, so there are no widths to restore; a blob saved by an
    // older version is simply ignored (item 53's Upgrading note).

    // Range-guarded on read: a stale or hand-edited file can hold anything,
    // and setCurrentIndex() on a value with no row silently selects nothing.
    const int sort =
        state.value(QStringLiteral("threadlist/sortOrder"), 0).toInt();
    m_sortOrder->setCurrentIndex(sort == 1 ? 1 : 0);

    // The config value is the starting point for a profile that has never
    // zoomed; once the user does, the state file is what they last had.
    // clampZoom() rejects the garbage a hand-edited file can hold.
    m_messageView->setZoomFactor(
        state.value(QStringLiteral("message/zoom"), m_config.messageZoom())
            .toDouble());
}

void MainWindow::saveUiState() const
{
    QDir().mkpath(QFileInfo(uiStatePath()).absolutePath());
    QSettings state(uiStatePath(), QSettings::IniFormat);
    state.setValue(QStringLiteral("window/geometry"), saveGeometry());
    state.setValue(QStringLiteral("window/state"), saveState());
    state.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
    state.setValue(QStringLiteral("threadlist/sortOrder"),
                   m_sortOrder->currentIndex());
    state.setValue(QStringLiteral("message/zoom"), m_messageView->zoomFactor());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // A sync started for exit is still running: hold the window open. Its
    // finished signal closes us, and asking again here would stack prompts.
    if (m_syncingForExit) {
        event->ignore();
        return;
    }

    // Case 3 FIRST, because it is the one where saving is what is already not
    // working: in case 2 nothing is lost by saving, here quitting loses that
    // text, so the dialog must say so plainly rather than offering a save that
    // will fail again.
    QStringList failedSaves;
    for (const QPointer<ComposeWindow> &composer : m_composers) {
        if (composer && composer->lastSaveFailed())
            failedSaves.append(composer->windowTitle());
    }
    if (!failedSaves.isEmpty()) {
        // The titles, not merely the count. The spec requires the dialog to
        // NAME what could not be saved: "2 messages could not be saved" tells
        // a user with four composers open nothing about which two to rescue.
        //
        // The list is a separate paragraph rather than interpolated into the
        // sentence. The count and the list combine differently across
        // languages, and a translator given "%n message(s) ...: %1" has to
        // keep an English clause order Italian does not share.
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("A draft could not be saved"));
        box.setText(tr("%n message(s) could not be saved to the drafts "
                       "folder. Quitting now loses that text.", "",
                       failedSaves.size()));
        box.setInformativeText(failedSaves.join(QLatin1Char('\n')));
        box.setStandardButtons(QMessageBox::Retry | QMessageBox::Discard
                               | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);
        const int answer = box.exec();

        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Retry) {
            bool allSaved = true;
            for (const QPointer<ComposeWindow> &composer : m_composers) {
                if (composer && composer->lastSaveFailed()
                    && !composer->saveDraftNow()) {
                    allSaved = false;
                }
            }
            if (!allSaved) {
                // Still failing: stay open rather than quitting on a retry
                // that did not work, which would lose exactly the text the
                // user pressed Retry to keep.
                event->ignore();
                return;
            }
        }
    }

    // Case 2: ONE dialog whatever the count. Three modals in a row is worse
    // than a coarse answer, so it applies to all of them and there is no
    // per-draft choice.
    const QList<QPointer<ComposeWindow>> blocking = composersBlockingQuit();
    if (!blocking.isEmpty()) {
        QStringList titles;
        titles.reserve(blocking.size());
        for (const QPointer<ComposeWindow> &composer : blocking)
            titles.append(composer->windowTitle());

        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("Messages still being composed"));
        // "Discard" discards UNSAVED EDITS, not drafts: a draft already
        // autosaved stays in the folder. The wording must not read as
        // "delete my three messages".
        box.setText(tr("%n message(s) are still being composed. Drafts "
                       "already saved stay in the drafts folder either way.",
                       "", blocking.size()));
        box.setInformativeText(titles.join(QLatin1Char('\n')));
        box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard
                               | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Save);
        const int answer = box.exec();

        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Save) {
            // Null-checked per iteration, because `blocking` was computed
            // BEFORE exec() and a nested event loop processes deleteLater().
            // The dialog is window-modal to this window only, so a user can
            // close a composer while it is up; measured in a standalone Qt
            // program, that composer is destroyed before exec() returns.
            // Without this check the save runs on freed memory at the exact
            // moment the application promised to preserve the text, and the
            // remaining composers' drafts are never written because the crash
            // happens mid-loop. Case 3's Retry loop above has always had the
            // equivalent guard; this one had dropped it.
            for (const QPointer<ComposeWindow> &composer : blocking) {
                if (composer)
                    composer->saveDraftNow();
            }
        }
    }

    if (!m_closeApproved && pendingEditCount() > 0
        && m_config.syncOnExit() != Config::SyncOnExit::Never) {

        // Not a destructive-action confirmation, which CLAUDE.md forbids for
        // tag mutations. Those get undo instead. This asks about LOSING work at
        // the one point where undo cannot help, which is the opposite case.
        const bool canSync = m_sync && m_sync->isAvailable();

        if (!canSync) {
            // Degrade to a warning rather than offering a sync that cannot run.
            const auto answer = QMessageBox::warning(
                this, tr("Unsynced changes"),
                tr("%n change(s) have not been synced, and no sync command "
                   "is configured. Quit anyway?", "", pendingEditCount()),
                QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
        } else if (m_config.syncOnExit() == Config::SyncOnExit::Ask) {
            // Three buttons, not two: a user who hit Quit by mistake needs a
            // way back that is not "sync".
            QMessageBox box(this);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(tr("Unsynced changes"));
            box.setText(tr("%n change(s) have not been synced.", "",
                           pendingEditCount()));
            box.setInformativeText(tr("Sync before quitting?"));
            QPushButton *sync =
                box.addButton(tr("Sync and quit"), QMessageBox::AcceptRole);
            QPushButton *quit =
                box.addButton(tr("Quit anyway"), QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(sync);

            // The default is set correctly and Qt agrees (isDefault() and
            // hasFocus() are both true on it), but qt6ct-style draws no
            // visible default-button decoration, so Enter's target is
            // invisible on this desktop. Naming it in the text costs nothing
            // and does not fight the theme.
            // ponytail: text, not a styled button. Restyling the button means
            // overriding the user's theme, which is worse than a sentence.
            sync->setText(tr("Sync and quit (default)"));
            box.exec();

            if (box.clickedButton() == sync) {
                if (m_sync->start(pendingSyncChannels())) {
                    m_syncingForExit = true;
                    m_syncLog->clear();
                    setSyncBusy(true);
                    m_statusLabel->setText(tr("Syncing before quitting..."));
                    event->ignore();
                    return;
                }
                // Could not start after all: say so and stay, rather than
                // quitting as though the sync had happened.
                QMessageBox::warning(this, tr("Sync failed"),
                                     tr("The sync could not be started, so "
                                        "your changes are still unsynced."));
                event->ignore();
                return;
            }
            if (box.clickedButton() != quit) {
                event->ignore();   // Cancel, or the dialog was dismissed.
                return;
            }
        } else if (m_config.syncOnExit() == Config::SyncOnExit::Always) {
            if (m_sync->start(pendingSyncChannels())) {
                m_syncingForExit = true;
                m_syncLog->clear();
                setSyncBusy(true);
                m_statusLabel->setText(tr("Syncing before quitting..."));
                event->ignore();
                return;
            }
            QMessageBox::warning(this, tr("Sync failed"),
                                 tr("The sync could not be started, so your "
                                    "changes are still unsynced."));
            event->ignore();
            return;
        }
    }

    // Every composer goes with the window, and this is the LAST thing before
    // the close is accepted: every route that turns back (Cancel, a failed
    // sync, a refused save) has already returned above, so reaching here means
    // the application really is quitting.
    //
    // A composer is deliberately parentless, so that it appears in the task
    // switcher and stays usable while the main window is. Qt therefore does not
    // take it down with this window, and it kept the process alive: the main
    // window vanished, the composer stayed on screen with nothing behind it,
    // and closing it then raised the unsaved-edits dialog for a session that
    // had already ended.
    //
    // Closing rather than deleting. WA_DeleteOnClose is set on every composer,
    // so close() is what frees them, and it lets ComposeWindow::closeEvent()
    // run its own draft handling on the way out. The drafts have already been
    // saved by the dialogs above, so that pass has nothing left to do; going
    // through it anyway keeps ONE exit path rather than a second one that has
    // to be kept in step.
    //
    // Iterating a COPY: closing a composer runs the `closed` handler, which
    // mutates m_composers, and mutating a container mid-iteration is undefined.
    const QList<QPointer<ComposeWindow>> composers = m_composers;
    for (const QPointer<ComposeWindow> &composer : composers) {
        if (composer)
            composer->close();
    }

    saveUiState();
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Return is bound to open_thread as a WindowShortcut. A shortcut is
    // dispatched before the focused widget sees the key, and Qt's protection
    // for editable widgets covers plain LETTERS only, so from inside the query
    // bar Return triggered the action, focus jumped to the thread list, and the
    // query was never run.
    //
    // Accepting the ShortcutOverride tells Qt the focused widget wants this key
    // as ordinary input, which stops the shortcut from being dispatched at all;
    // QLineEdit then emits returnPressed as usual. Narrow on purpose: one
    // widget, one key, so open_thread keeps working everywhere else.
    if (watched == m_queryEdit && event->type() == QEvent::ShortcutOverride) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter) {
            keyEvent->accept();
            return true;
        }
        // Delete needs NO entry here, and that is worth stating because the
        // reasoning that says it does is nearly right. It is bound bare to
        // `delete`, and Qt's protection for editable widgets covers plain
        // LETTERS only, so by the same argument that made Return a problem it
        // should trigger the action while the user edits a query.
        //
        // It does not, because QLineEdit accepts the ShortcutOverride for
        // Delete itself: Delete is one of its own editing keys, which Return
        // is not. Measured both ways, with this branch present and absent:
        // the action fires 0 times either way and the text is edited either
        // way. Adding a guard here would be dead code carrying a test that
        // cannot fail.
    }

    // The unsynced-changes indicator opens its list on a click (item 119). A
    // QLabel has no clicked signal, so the press is taken here rather than
    // replacing the label with a flat QToolButton: a button would inherit the
    // style's button metrics inside a status bar, and the label already sits
    // correctly.
    if (watched == m_pendingLabel
        && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            showPendingChanges();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

MainWindow::MainWindow(const Config &config, QWidget *parent)
    : QMainWindow(parent), m_config(config)
{
    qRegisterMetaType<ThreadSummary>();
    qRegisterMetaType<MessageRef>();
    qRegisterMetaType<TagChange>();
    qRegisterMetaType<DatabaseStats>();
    qRegisterMetaType<MessageNode>();
    qRegisterMetaType<QVector<ThreadSummary>>();
    qRegisterMetaType<QVector<MessageRef>>();
    qRegisterMetaType<QVector<MessageNode>>();

    m_keyMap.loadDefaults();
    {
        QSettings settings(Config::defaultPath(), QSettings::IniFormat);
        m_keyMap.loadOverrides(settings);
        m_tagColors.load(settings);
    }

    // An account's chip colour comes from its own stanza, since an account tag
    // is a different taxonomy from a functional one.
    for (const Account &account : m_config.accounts()) {
        m_tagColors.setAccountColour(account.key, account.color);
        m_tagColors.setAccountLabel(account.key, account.label);
    }

    buildUi();
    registerActions();

    // Both need the delegate, which buildUi() just created. The list is loaded
    // once at startup and again only on an explicit reload, never per repaint:
    // the painting path runs on every row of every scroll.
    loadBusinessSenders();
    applyCurrentAccountToDelegate();
    // A change takes effect without a restart. Its own connect, not the one in
    // buildSavedQueryRow(), which belongs to the filter-buttons row and is
    // rebuilt with it.
    connect(m_accountBox, &QComboBox::currentIndexChanged, this,
            [this]() { applyCurrentAccountToDelegate(); });

    // After registerActions(), not inside buildUi(): the query bar exists by
    // then but the action does not, so wiring this where the field is built
    // silently connected nothing and left Save query enabled on an empty
    // query. Hung on textChanged rather than textEdited, because the field is
    // also set programmatically, by the saved-query buttons and by
    // recoverStaleThread(), and the action must track those too.
    if (QAction *save = m_actions.value(QStringLiteral("save_query"))) {
        // setDefaultAction, not a second connect: the button then takes the
        // action's text, icon, tooltip and ENABLED state, so it cannot end up
        // offering to save an empty query while the menu entry refuses.
        m_saveQueryButton->setDefaultAction(save);
        // Icon AND text, unlike the toolbar, which follows the desktop's
        // button style. This button sits in a row of text buttons, the saved
        // queries, and an icon on its own next to them reads as a different
        // kind of control than it is. It is also the one action whose meaning
        // an icon alone does not carry: "save" is a shape everyone knows and
        // the question is always "save WHAT".
        m_saveQueryButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        // Its own text, not the action's: "&Save query..." is menu phrasing,
        // and a button rendering the ampersand's accelerator and the ellipsis
        // that promises a dialog reads as a menu entry that escaped. The
        // action keeps both for the menu it lives in.
        m_saveQueryButton->setText(tr("Save"));

        auto updateQueryState = [this, save]() {
            const bool hasQuery = !m_queryEdit->text().trimmed().isEmpty();
            save->setEnabled(hasQuery);
            // The message pane greys "Exclude from search" without it: there
            // would be nothing to exclude FROM. Both widgets exist by now,
            // buildUi() having run before registerActions().
            m_messageView->setHasQuery(hasQuery);
        };
        connect(m_queryEdit, &QLineEdit::textChanged, this, updateQueryState);
        updateQueryState();
    }

    buildMenus();
    // After buildMenus(), which registers the toolbar and menu entries these
    // actions already carry: the bar shows the same objects a second time.
    populateMessageBar();
    // After buildMenus(): QMainWindow::restoreState() matches toolbars by
    // object name, so they must already exist or their position is dropped.
    restoreUiState();
    wireWorker();
    // Sets the status label only. The modal that used to live here is raised
    // by the caller after show(), because a modal in a constructor cannot be
    // dismissed under the offscreen platform and hung the whole suite.
    applyWarnings();

    // No window-wide event filter: QAction shortcuts are dispatched before the
    // focused widget sees the key, so they beat QAbstractItemView's
    // type-to-search without one. Qt also suppresses a plain-letter shortcut
    // while an editable widget has focus, so typing in the query bar stays
    // typing; modifier shortcuts such as Ctrl+Q still work there, which the old
    // filter blocked.
    //
    // That letter rule does NOT cover Return, which is bound to open_thread:
    // it reached the action from inside the query bar and stole the key. The
    // narrow filter buildUi() installs on the query bar claims it back. See
    // eventFilter().

    // Not savedQueries().first(): [queries] is read through childKeys(), which
    // sorts alphabetically, so "first" means whatever happens to sort first
    // rather than anything the user chose. Config resolves the name.
    // BEFORE the startup query runs, so the query below is composed in this
    // scope. That is the whole of `startup_account`: a built-in filter composes
    // with the dropdown, so setting the dropdown is all that is needed and the
    // key never reaches a query builder.
    //
    // Config has already checked the key names a real account and cleared it if
    // not, so findData either matches or this is "All accounts" anyway.
    const QString startupAccount = m_config.startupAccount();
    if (!startupAccount.isEmpty()) {
        const int index = m_accountBox->findData(startupAccount);
        if (index >= 0)
            m_accountBox->setCurrentIndex(index);
    }

    // resolvedQuery(), not startup.query: a generated entry stores no query at
    // all, since its text is composed from the accounts at run time. Reading
    // the field directly meant a startup_query naming a built-in filter opened
    // an empty bar and ran nothing.
    const SavedQuery startup = m_config.startupSavedQuery();
    const QString startupQuery =
        m_config.resolvedQuery(startup, startupAccount);
    if (!startupQuery.isEmpty()) {
        m_queryEdit->setText(startupQuery);

        // Which of the two applies the scope depends on what the startup entry
        // IS, and getting this wrong is silent in both directions.
        //
        // A generated filter came back from resolvedQuery() already scoped to
        // the startup account, so applying the dropdown again gives
        //     path:"work/**" and (path:"work/**" and (tag:inbox))
        // which returns exactly the right rows while being the double scope
        // this item exists to avoid.
        //
        // A saved query did NOT: resolvedQuery() ignores the account key for
        // one, because a saved query states its own scope. Claiming it was
        // already scoped leaves it unscoped for good, with the dropdown sitting
        // on Work and the list showing every account.
        runQuery(FlatResult::No,
                 startup.isGenerated() ? AccountScope::AlreadyScoped
                                       : AccountScope::Apply);
    }
}

MainWindow::~MainWindow()
{
    m_workerThread.quit();
    m_workerThread.wait();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    // The status label is created first: the sync wiring below can report into
    // it before the rest of the UI exists.
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("statusMessage"));
    statusBar()->addWidget(m_statusLabel);

    // Transient messages describe an EVENT and go stale: "Sync complete" reads
    // as the present tense until something else overwrites it. State messages,
    // the selection count above all, describe what is true right now and must
    // not expire while it stays true, so only showTransientStatus() arms this.
    //
    // ponytail: one timer beside the label, not QStatusBar::showMessage().
    // That would mean moving off addWidget() and reworking the permanent
    // widgets beside it, for the same behaviour.
    m_statusTimer = new QTimer(this);
    m_statusTimer->setObjectName(QStringLiteral("statusTimer"));
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(kStatusMessageMs);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        // Only take back a message this timer armed. Anything written since is
        // newer and more relevant than the default.
        if (m_statusLabel->text() == m_transientMessage)
            m_statusLabel->setText(m_defaultStatus);
        m_transientMessage.clear();
    });

    // Beside the sync status rather than as a widget competing with it: the two
    // say related things and reading them apart would be worse than reading
    // them together.
    m_pendingLabel = new QLabel(this);
    m_pendingLabel->setObjectName(QStringLiteral("pendingEdits"));
    // Clickable, opening the list of what it counts (item 119). The cursor is
    // the only affordance a status-bar label can carry, so it is what says
    // this one can be opened.
    m_pendingLabel->setCursor(Qt::PointingHandCursor);
    m_pendingLabel->installEventFilter(this);
    m_pendingLabel->hide();
    statusBar()->addPermanentWidget(m_pendingLabel);

    // Indeterminate, which BusyIndicator starts in: a sync has no measurable
    // progress, since mbsync reports no percentage and the script's output is
    // unstructured, so a bar filling left to right would be inventing a
    // fraction. This one animates to say "working, duration unknown". The
    // determinate half of the widget is the composer's, not this one's.
    m_syncProgress = new BusyIndicator(this);
    m_syncProgress->setObjectName(QStringLiteral("syncProgress"));
    m_syncProgress->setMaximumWidth(120);
    statusBar()->addPermanentWidget(m_syncProgress);

    // Query row.
    auto *queryRow = new QHBoxLayout;
    m_accountBox = new QComboBox(central);
    m_accountBox->setObjectName(QStringLiteral("accountBox"));
    m_accountBox->addItem(tr("All accounts"), QString());
    for (const Account &account : m_config.accounts()) {
        m_accountBox->addItem(account.key, account.key);
        // The RAW account colour here, not CardDelegate's blended line colour:
        // a swatch is a filled patch like a chip, not a thin line, so it wants
        // the colour the account was actually given. Qt renders a
        // DecorationRole colour as a swatch itself, with no delegate.
        //
        // This is what makes the accent bar on a card mean anything: a colour
        // down a card's edge says nothing until something maps it to a name.
        m_accountBox->setItemData(
            m_accountBox->count() - 1,
            m_tagColors.colourFor(TagColors::tagForAccountKey(account.key)),
            Qt::DecorationRole);
    }

    // Sort order. Two entries, straight to notmuch: this ADDS a feature rather
    // than replacing one, since the old column header was decorative and
    // nothing implemented click-to-sort.
    m_sortOrder = new QComboBox(central);
    m_sortOrder->setObjectName(QStringLiteral("sortOrder"));
    // Order matters: the index is what uistate.conf stores.
    m_sortOrder->addItem(tr("Newest first"));
    m_sortOrder->addItem(tr("Oldest first"));
    m_sortOrder->setToolTip(tr("The order threads are listed in"));
    connect(m_sortOrder, &QComboBox::currentIndexChanged,
            this, &MainWindow::runCurrentQuery);

    m_queryEdit = new QLineEdit(central);
    m_queryEdit->setObjectName(QStringLiteral("queryEdit"));
    m_queryEdit->setPlaceholderText(tr("notmuch query, e.g. tag:inbox"));
    // Qt draws the clear button inside the field and shows it only when there
    // is text, themed by the desktop. A hand-rolled button beside the bar would
    // read as "Search" and duplicate Return, which is how item 45 started.
    m_queryEdit->setClearButtonEnabled(true);
    connect(m_queryEdit, &QLineEdit::returnPressed,
            this, &MainWindow::runCurrentQuery);

    // Return is bound to open_thread as a WindowShortcut, and a shortcut is
    // dispatched before the focused widget sees the key. Qt withholds a plain
    // LETTER shortcut from an editable widget, which is why every other binding
    // here is safe, but Return is not a letter and gets no such protection: it
    // reached the action, focus jumped to the thread list, and the query never
    // ran. Accepting the ShortcutOverride is what claims the key back, and it
    // is scoped to the one widget and the one key, so open_thread still works
    // everywhere else in the window.
    m_queryEdit->installEventFilter(this);
    m_queryCompleter = new QueryCompleter(m_queryEdit, m_config, this);

    m_markReadTimer = new QTimer(this);
    // Named so a test can observe whether it is armed without the window
    // having to expose the timer or the decision that armed it.
    m_markReadTimer->setObjectName(QStringLiteral("markReadTimer"));
    m_markReadTimer->setSingleShot(true);
    connect(m_markReadTimer, &QTimer::timeout,
            this, &MainWindow::markCurrentThreadRead);

    m_autoSyncTimer = new QTimer(this);
    // Named for the same reason: a test can assert that an edit armed the
    // debounce without waiting out the delay or starting a real mbsync.
    m_autoSyncTimer->setObjectName(QStringLiteral("autoSyncTimer"));
    m_autoSyncTimer->setSingleShot(true);
    connect(m_autoSyncTimer, &QTimer::timeout, this, &MainWindow::runAutoSync);

    // The pane and its close button travel together: a QPlainTextEdit has
    // nowhere to put one, and a pane that appears on a failed sync and can
    // never be dismissed is worse than one that does not appear at all.
    m_syncLogPane = new QWidget(central);
    m_syncLogPane->setObjectName(QStringLiteral("syncLogPane"));
    auto *syncLogLayout = new QVBoxLayout(m_syncLogPane);
    syncLogLayout->setContentsMargins(0, 0, 0, 0);
    syncLogLayout->setSpacing(2);

    auto *syncLogHeader = new QHBoxLayout;
    syncLogHeader->addWidget(new QLabel(tr("Sync output"), m_syncLogPane));
    syncLogHeader->addStretch();

    auto *closeSyncLog = new QPushButton(tr("Close"), m_syncLogPane);
    closeSyncLog->setObjectName(QStringLiteral("closeSyncLog"));
    closeSyncLog->setToolTip(tr("Hide the sync output until the next failure"));
    connect(closeSyncLog, &QPushButton::clicked,
            m_syncLogPane, &QWidget::hide);
    syncLogHeader->addWidget(closeSyncLog);
    syncLogLayout->addLayout(syncLogHeader);

    m_syncLog = new QPlainTextEdit(m_syncLogPane);
    m_syncLog->setReadOnly(true);
    // 200 rather than 120: mbsync's output is wide and repetitive, and the
    // shorter pane showed too little of it to read.
    m_syncLog->setMaximumHeight(200);
    syncLogLayout->addWidget(m_syncLog);

    m_syncLogPane->hide();

    // Sync is reached from the toolbar, the File menu and the shortcut, all of
    // them one QAction. A second QPushButton sat beside the query bar until
    // 0.9.x, where it read as a Search button given what it stood next to, and
    // carried behaviour the action did not: item 45.
    m_sync = new MailSync(m_config.syncCommand(), this);
    connect(m_sync, &MailSync::finished, this, &MainWindow::onSyncFinished);
    connect(m_sync, &MailSync::outputReceived, this, [this](const QString &chunk) {
        m_syncLog->appendPlainText(chunk.trimmed());
        feedSyncPhase(chunk);
    });

    // Syncs this window did not start. The user's cron runs the same script
    // every ten minutes, so mail arrives and tags change while the window sits
    // idle, and until now nothing here noticed.
    m_syncMonitor = new SyncMonitor(SyncMonitor::defaultLockPath(),
                                    locksPath(), this);
    connect(m_syncMonitor, &SyncMonitor::stateChanged,
            this, &MainWindow::onExternalSyncStateChanged);
    m_syncMonitor->start();

    // The query row proper: account, sort order, the field. The saved queries
    // used to share it and now have a row of their own below, which is what
    // stops an unbounded list squeezing the field (item 23; the ponytail note
    // that stood here predicted exactly this).
    queryRow->addWidget(m_accountBox);
    queryRow->addWidget(m_sortOrder);
    queryRow->addWidget(m_queryEdit, 1);

    // Beside the field, where a user looks for it. The menu entry and Ctrl+S
    // were not enough on their own: saving is a thing you decide on while
    // looking at the results, so it needs to be visible at the query bar
    // rather than remembered. Created here and given its action in the
    // constructor, since registerActions() has not run yet.
    m_saveQueryButton = new QToolButton(central);
    m_saveQueryButton->setObjectName(QStringLiteral("saveQueryButton"));
    queryRow->addWidget(m_saveQueryButton);

    layout->addLayout(queryRow);

    buildSavedQueryRow(central, layout);

    // Thread list and message pane.
    m_model = new ThreadListModel(this);
    m_model->setTagColors(&m_tagColors);
    m_model->setDateFormat(m_config.dateFormat());
    m_model->setForwardPrefixes(m_config.forwardPrefixes());
    // ThreadListView, not a plain QTableView: it paints the row-wide tag
    // strip under each row's cells, which no delegate can do because a
    // delegate is confined to one column's rectangle.
    m_threadView = new ThreadListView(central);
    m_threadView->setModel(m_model);
    m_cardDelegate = new CardDelegate(this);
    m_threadView->setItemDelegate(m_cardDelegate);
    m_threadView->setHeaderHidden(true);
    m_threadView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_threadView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // No style-drawn branch decoration. CardDelegate draws the expander itself,
    // because drawBranches runs BEFORE the row's cells and the delegate's own
    // background paints straight over anything put there: a 60-pixel triangle
    // once survived as 8. Leaving both enabled would draw the theme's dot
    // underneath the delegate's glyph.
    m_threadView->setRootIsDecorated(false);

    // Zero, because CardLayout draws the indent itself. Qt's own indentation
    // would shift the card's rect, and every rect on the card is measured from
    // that rect's left edge, so the two would compound.
    m_threadView->setIndentation(0);

    // One height for every row. A QTreeView has no vertical header to carry a
    // default section size, so the height comes from uniformRowHeights plus
    // CardDelegate::sizeHint.
    m_threadView->setUniformRowHeights(true);

    // Banding, so the eye can follow a card across the pane. The colour comes
    // from the palette's AlternateBase, so it follows the desktop theme.
    m_threadView->setAlternatingRowColors(true);

    // A card is exactly viewport width, so there is nothing to scroll to
    // sideways. Turning the bar off is what closes item 51: a click used to
    // scroll the list horizontally, because the subject column was wider than
    // the viewport and auto-scroll brought the clicked index fully into view.
    m_threadView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Scrolling a whole card at a time rather than a fraction of one, so a
    // card is never left half above the top edge.
    m_threadView->verticalScrollBar()->setSingleStep(
        CardLayout::heightFor(m_threadView->font()));

    // Replies are loaded when a thread is expanded, not with the query.
    // Walking the reply tree of every thread in a 10k-thread result would cost
    // far more than the query itself and almost none of it would be looked at.
    connect(m_threadView, &QTreeView::expanded,
            this, &MainWindow::onThreadExpanded);

    connect(m_threadView->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onThreadSelected);

    // Also on currentRowChanged, and NOT only from onSelectionChanged, which
    // is where the reply family is answered. Both signals fire for an ordinary
    // click, but a selection that does not CHANGE emits only this one: running
    // a query and setting the current index reaches here and never the other,
    // so the enablement was computed against the previously selected row.
    // Measured: Edit draft stayed disabled on a draft selected that way.
    //
    // Safe on currentRowChanged, which CLAUDE.md restricts to "which row is
    // current": that is exactly the question here, and no count is read.
    connect(m_threadView->selectionModel(),
            &QItemSelectionModel::currentRowChanged, this,
            [this]() { updateComposeActions(); });

    // Separate from currentRowChanged: a selection can grow without current
    // moving at all. Ctrl+click adds a row and leaves current where it was, and
    // selectAll() emits no currentRowChanged whatsoever (verified against
    // Qt 6.11). Both are multi-select gestures that have to blank the pane and
    // cancel a pending mark-read, so neither can rely on the current-index
    // signal to notice them.
    connect(m_threadView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);

    // The label describes the SELECTION'S STATE, which a write moves without
    // touching the selection: marking the current row read has to flip the
    // entry to "Mark as unread" with the same row still selected. Keyed on
    // the model rather than on each of the six call sites that apply an
    // optimistic update, so a new one cannot forget.
    connect(m_model, &QAbstractItemModel::dataChanged, this, [this]() {
        refreshUnreadAction();
        refreshTrashActions();
    });

    connect(m_threadView, &QAbstractItemView::doubleClicked,
            this, &MainWindow::onRowDoubleClicked);

    m_messageView = new MessageView(central);
    m_messageView->setTagColors(&m_tagColors);
    connect(m_messageView, &MessageView::statusMessage,
            this, [this](const QString &text) { m_statusLabel->setText(text); });
    connect(m_messageView, &MessageView::queryRequested,
            this, &MainWindow::onPlaceholderQueryRequested);
    connect(m_messageView, &MessageView::staleThreadRecoveryRequested,
            this, &MainWindow::recoverStaleThread);
    connect(m_messageView, &MessageView::searchRequested,
            this, &MainWindow::runSearchFromPane);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->addWidget(m_threadView);
    m_splitter->addWidget(m_messageView);
    m_splitter->setStretchFactor(1, 2);
    // A splitter position is saved in PIXELS, so one saved in a wide window
    // does not fit a narrower one: QSplitter restores the first pane's size
    // verbatim and gives the second whatever is left. A real 1285/1252 split
    // restored into a 1136px window left the message pane 29px wide, a sliver
    // of rendered mail beside a full-width thread list. A floor on the pane
    // covers that and the equivalent drag, and needs no restore-time repair.
    // Only the message pane: a minimum on the thread view as well would leave
    // a narrow window unable to satisfy either, the same fault from the other
    // side.
    m_splitter->setCollapsible(1, false);
    m_messageView->setMinimumWidth(kMinMessagePaneWidth);
    layout->addWidget(m_splitter, 1);

    layout->addWidget(m_syncLogPane);

    setCentralWidget(central);

    resize(1200, 800);
    setWindowTitle(QStringLiteral("qtmaildir %1").arg(QTMAILDIR_VERSION));
}

void MainWindow::composeNew()
{
    // m_accountBox->currentData() is how the selected account is read
    // everywhere else in this file; there is no currentAccountKey() accessor.
    // Empty means the All accounts view, which falls through to rule 2.
    const QString accountKey = ComposeContextBuilder::accountForNew(
        m_config, m_accountBox->currentData().toString());
    if (accountKey.isEmpty()) {
        // Unreachable while the action is disabled, which is the only state
        // this can be true in. Reported rather than returning silently: an
        // action that runs and does nothing is the failure mode item 105
        // records as "the key does nothing".
        showTransientStatus(tr("No account is configured to send mail"));
        return;
    }

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = accountKey;
    context.seedHtml = m_config.compose().sendHtml;

    openComposer(context);
}

void MainWindow::composeReply(ComposeContext::Kind kind, bool quote)
{
    const QModelIndex current = m_threadView->currentIndex();

    // A CONVERSATION row answers the thread, not a message (item 177). The
    // old comment here said "replying to a thread is meaningless; a reply
    // answers a message", and half of that survives: this still resolves to
    // one message. What changed is WHICH one. A conversation row's card
    // shows the thread's FIRST message, and answering that would thread the
    // reply off the opening post of a discussion that has moved on, so the
    // newest message is asked for instead. Only `reply` reaches here on such
    // a row; refreshScopedActionLabels() hides the other four.
    if (m_model->isConversationRow(current)) {
        replyToThread(m_model->threadFor(current).threadId);
        return;
    }

    // It takes a QModelIndexList, not a single index, so the current index is
    // wrapped rather than passed bare.
    const ActionScope scope = m_model->scopeForSelection({ current });
    if (scope.messageIds.isEmpty()) {
        showTransientStatus(tr("No message is selected"));
        return;
    }

    // Built from the DATABASE, never from the model. The model's data comes
    // from the query, so a row whose state has not been re-queried carries
    // stale values, and a reply built from a stale row would carry the wrong
    // recipients. This is the rule Restore already follows.
    requestMessageForCompose(scope.messageIds.first(), kind, quote);
}

void MainWindow::replyToThread(const QString &threadId)
{
    if (threadId.isEmpty()) {
        showTransientStatus(tr("No thread is selected"));
        return;
    }

    // A round trip, because the model cannot answer this. The summary carries
    // only the thread's FIRST message, and an unexpanded conversation holds no
    // nodes for its replies at all, so the newest message's id exists only in
    // the database. resolveQuery() sorts newest-first explicitly for this.
    //
    // m_pendingThreadScope is deliberately NOT set. It carries the ids a MOVE
    // is about to apply to, and a reply moves nothing; setting it would leave
    // a delete arriving next reading a scope this gesture left behind.
    QMetaObject::invokeMethod(
        m_worker, "resolveThreadMessages", Qt::QueuedConnection,
        Q_ARG(QStringList, QStringList{ threadId }),
        Q_ARG(QString, QStringLiteral("reply_thread")));
}

void MainWindow::editDraft()
{
    editDraftAt(m_threadView->currentIndex());
}

void MainWindow::editDraftAt(const QModelIndex &index)
{
    // A draft is one message by construction, so a drafts row is never a
    // conversation and this needs no thread branch of its own.
    const ActionScope scope = m_model->scopeForSelection({ index });
    if (scope.messageIds.isEmpty()) {
        showTransientStatus(tr("No message is selected"));
        return;
    }

    // Through the worker for its path, never from the model: the model's path
    // comes from the query, and a draft is rewritten by every autosave, so a
    // row that has not been re-queried names a file that no longer exists.
    requestMessageForCompose(scope.messageIds.first(),
                             ComposeContext::Kind::Draft, false);
}

void MainWindow::requestMessageForCompose(const QString &messageId,
                                          ComposeContext::Kind kind,
                                          bool quote)
{
    if (messageId.isEmpty())
        return;

    m_pendingCompose = { messageId, kind, quote, true };

    // The same generation every other worker request carries, so a reply that
    // arrives after the query moved on is discarded rather than opening a
    // composer on a message the user is no longer looking at.
    QMetaObject::invokeMethod(m_worker, "loadMessage", Qt::QueuedConnection,
                              Q_ARG(QString, messageId),
                              Q_ARG(quint64, m_generation));
}

void MainWindow::openComposerFor(const MessageRef &ref,
                                 ComposeContext::Kind kind, bool quote)
{
    // A draft is RESUMED rather than answered: nothing is derived from it,
    // and the composer takes ownership of its file. Handled before the reply
    // machinery below, none of which applies (item 153).
    if (kind == ComposeContext::Kind::Draft) {
        const ComposeContext draft =
            ComposeContextBuilder::forDraft(m_config, ref.filePath);
        if (draft.kind != ComposeContext::Kind::Draft) {
            showTransientStatus(tr("That draft could not be read"));
            return;
        }
        if (draft.accountKey.isEmpty()) {
            showTransientStatus(tr("No account is configured to send"));
            return;
        }
        openComposer(draft);
        return;
    }

    // Item 163, the same stale path the pane and the draft reopen hit. Here it
    // refuses a Reply or a Forward outright, so the user cannot answer a
    // message that is sitting on disk and readable.
    const QString originalPath = MaildirName::resolveRenamed(ref.filePath);

    MimeParser parser;
    const ParsedMessage original = parser.parse(originalPath);
    if (!original.ok) {
        showTransientStatus(tr("That message could not be read"));
        return;
    }

    ComposeContext context;
    context.kind = kind;
    context.originalPath = originalPath;

    // Item 68. Set for a forward as well as a reply, which is why it is not
    // inReplyTo: that header is deliberately omitted from a forward below, and
    // the P flag still belongs on the message that was forwarded.
    context.sourceMessageId = original.messageId;

    const bool replyAll = kind == ComposeContext::Kind::ReplyAll;
    const bool forwarding = kind == ComposeContext::Kind::Forward;

    if (!forwarding) {
        ComposeContextBuilder::recipientsForReply(
            original, replyAll, ComposeContextBuilder::ownAddresses(m_config),
            &context.to, &context.cc);

        // Threading headers on a reply only. A forward starts a new
        // conversation: carrying In-Reply-To would file it under the thread it
        // was forwarded out of, in the RECIPIENT's client.
        context.inReplyTo = original.messageId;
        context.references = ComposeContextBuilder::referencesForReply(original);
    }

    context.subject = forwarding
                          ? ComposeContextBuilder::forwardSubject(original.subject)
                          : ComposeContextBuilder::replySubject(original.subject);

    if (quote)
        context.quotedBody = ComposeContextBuilder::quoteBody(original);

    // Forward seeds from the CONFIG, Reply from the original. The split is
    // the spec's and Config::ComposeSettings::sendHtml states it too: an HTML
    // part in the original is a fact about the SENDER's software, so it is the
    // right seed when answering them and says nothing about a forward, which
    // is a new message to somebody else. composeNew() already reads the config
    // for the same reason.
    context.seedHtml = forwarding ? m_config.compose().sendHtml
                                  : original.hasHtml();

    // accountForReply() takes messagePaths PLURAL because notmuch can return
    // several filenames for one id, and it disambiguates between them by
    // recipient. That disambiguation is INERT here, and the reason is upstream
    // rather than a decision made at this call site: NotmuchWorker::loadMessage
    // builds its MessageRef from notmuch_message_get_filename(), the SINGULAR
    // accessor, so nothing in the pipeline ever carries more than one path and
    // the list below can never hold more than one element. Backlog item 137
    // carries the fix (MessageRef gains a filePaths list populated from
    // notmuch_message_get_filenames()); until then a message that arrived at
    // two accounts can open its reply from the wrong one.
    const QStringList recipients = context.to + context.cc;
    context.accountKey = ComposeContextBuilder::accountForReply(
        m_config, { ref.filePath }, recipients, m_mailRoot);

    if (context.accountKey.isEmpty()
        || !m_config.account(context.accountKey).canSend()) {
        // The enablement pass should already have stopped this, but it answers
        // from the model's path while this answers from the database's, and
        // the two can disagree on a row that has not been re-queried.
        showTransientStatus(
            tr("That message arrived at an account that cannot send"));
        return;
    }

    openComposer(context);
}

void MainWindow::openComposer(const ComposeContext &context)
{
    if (m_mailRoot.isEmpty()) {
        // Without the root a draft cannot be written anywhere, and a composer
        // that silently cannot autosave is the state the quit path's honesty
        // depends on not being in.
        showTransientStatus(tr("The Maildir root is not known yet"));
        return;
    }

    auto *composer = new ComposeWindow(context, m_config, m_mailRoot);
    composer->setAttribute(Qt::WA_DeleteOnClose);
    m_composers.append(QPointer<ComposeWindow>(composer));

    // Compaction, and ONLY compaction. The QPointer above is what keeps
    // composersBlockingQuit() safe against a destroyed window, since it nulls
    // on destruction; this drops the entry so the list does not accumulate
    // nulls for the session's lifetime. Neither replaces the other: without
    // the signal the list leaks entries, without the QPointer it dangles.
    connect(composer, &ComposeWindow::closed, this,
            [this](ComposeWindow *which) {
                m_composers.removeIf([which](const QPointer<ComposeWindow> &p) {
                    return p.isNull() || p.data() == which;
                });
            });

    // A saved draft is indexed immediately (item 158): the Drafts view is a
    // path query, and without this the draft is invisible until the next sync.
    // The worker lives on another thread, so this is a queued connection and
    // notmuch stays on its own thread.
    connect(composer, &ComposeWindow::draftSaved, m_worker,
            &NotmuchWorker::indexDraftFile);

    // A draft unlinked on send must leave no ghost entry behind.
    connect(composer, &ComposeWindow::draftRemoved, m_worker,
            &NotmuchWorker::removeIndexedFile);

    // Item 68. The R and P Maildir flags, on the message the send answered.
    //
    // sendMessageTagChange, NOT tagSelected: this deliberately does not go on
    // the undo stack, for the reason auto mark-read does not (see
    // markCurrentThreadRead). The flag records a fact the user brought about
    // by sending, and the send itself cannot be undone, so offering Ctrl+Z to
    // retract only the flag would leave the two disagreeing. Removing the tag
    // by hand still works.
    //
    // Message-scoped: the message answered, never its thread.
    connect(composer, &ComposeWindow::sourceMessageAnswered, this,
            [this](const QString &messageId, const QString &tag) {
                if (messageId.isEmpty() || tag.isEmpty())
                    return;
                sendMessageTagChange({ messageId }, { tag }, {},
                                     tag == QStringLiteral("passed")
                                         ? tr("Mark forwarded")
                                         : tr("Mark replied"));
            });

    composer->show();
}

QList<QPointer<ComposeWindow>> MainWindow::composersBlockingQuit() const
{
    QList<QPointer<ComposeWindow>> blocking;
    for (const QPointer<ComposeWindow> &composer : m_composers) {
        if (composer && composer->hasUnsavedEdits())
            blocking.append(composer);
    }
    return blocking;
}

ComposeWindow *MainWindow::openComposerForTest()
{
    const QString accountKey =
        ComposeContextBuilder::accountForNew(m_config, QString());
    if (accountKey.isEmpty())
        return nullptr;

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = accountKey;

    const int before = m_composers.size();
    openComposer(context);
    if (m_composers.size() == before)
        return nullptr;
    return m_composers.constLast().data();
}

QList<ComposeWindow *> MainWindow::openComposersForTest() const
{
    QList<ComposeWindow *> live;
    for (const QPointer<ComposeWindow> &composer : m_composers) {
        if (composer)
            live.append(composer.data());
    }
    return live;
}

int MainWindow::openComposerCount() const
{
    int live = 0;
    for (const QPointer<ComposeWindow> &composer : m_composers) {
        if (composer)
            ++live;
    }
    return live;
}

void MainWindow::markComposersDirtyForTest()
{
    // Through the real edit path: the body editor's own textChanged is what
    // ComposeWindow::markDirty() is connected to, so inserting text here
    // exercises the same route typing does. Setting a dirty flag directly
    // would pass against a composer that never notices an edit at all.
    //
    // QTextCursor rather than QTest::keyClicks, so production code does not
    // have to link QtTest.
    for (const QPointer<ComposeWindow> &composer : m_composers) {
        if (!composer)
            continue;
        if (auto *body = composer->findChild<QPlainTextEdit *>(
                QStringLiteral("body"))) {
            body->textCursor().insertText(QStringLiteral("x"));
        }
    }
}

bool MainWindow::currentMessageIsADraft() const
{
    return indexIsADraft(m_threadView->currentIndex());
}

bool MainWindow::indexIsADraft(const QModelIndex &current) const
{
    if (m_mailRoot.isEmpty())
        return false;

    if (!current.isValid())
        return false;

    QString path;
    if (m_model->isMessageRow(current))
        path = m_model->messageAt(current).filePath;
    else
        path = m_model->threadFor(current).firstMessagePath;
    if (path.isEmpty())
        return false;

    // ThreadSummary::firstMessagePath is RELATIVE to the mail root and
    // MessageNode::filePath is ABSOLUTE, the asymmetry accountForCurrentMessage()
    // documents. Compared as a resolved absolute path against each account's
    // drafts folder.
    const QString absolute = QDir::isAbsolutePath(path)
                                 ? path
                                 : QDir(m_mailRoot).absoluteFilePath(path);

    for (const Account &account : m_config.accounts()) {
        if (account.drafts.isEmpty())
            continue;
        const QString folder = QDir(m_mailRoot).absoluteFilePath(
            account.maildir + QLatin1Char('/') + account.drafts);
        // A path comparison with a separator, never startsWith() on the bare
        // folder: "/mail/acct/Drafts-old/cur/x" starts with "/mail/acct/Drafts"
        // and is a different folder. This is the rule the attachment save path
        // already follows.
        if (absolute.startsWith(folder + QLatin1Char('/')))
            return true;
    }
    return false;
}

QString MainWindow::accountForCurrentMessage() const
{
    if (m_mailRoot.isEmpty())
        return {};

    const QModelIndex current = m_threadView->currentIndex();
    if (!current.isValid())
        return {};

    // The model's path, deliberately. This decides whether a CONTROL is live,
    // which a stale path answers well enough; the context that actually opens
    // a composer resolves the account again from the database. Asking the
    // worker here would make every selection change a round trip.
    //
    // The two sources are in DIFFERENT FORMS and normalising them is not
    // tidying. ThreadSummary::firstMessagePath is RELATIVE to the mail root,
    // because runQuery() reduces it with relativeFilePath() so the UI can
    // compare it against an account's maildir; MessageNode::filePath is
    // ABSOLUTE, because MimeParser opens it. accountOwning() builds an
    // absolute prefix, so handing it the relative one matches no account at
    // all and every thread row reports no account, which disables the reply
    // family on mail from an account that can perfectly well send. Measured:
    // it did exactly that until the guard test caught it.
    QString path;
    if (m_model->isMessageRow(current)) {
        path = m_model->messageAt(current).filePath;
    } else {
        path = m_model->threadFor(current).firstMessagePath;
    }
    if (path.isEmpty())
        return {};

    const QString absolute = QDir::isAbsolutePath(path)
                                 ? path
                                 : QDir(m_mailRoot).absoluteFilePath(path);

    return ComposeContextBuilder::accountForReply(m_config, { absolute },
                                                  QStringList(), m_mailRoot);
}

void MainWindow::updateComposeActions()
{
    // The reply family is disabled on mail that arrived at an account which
    // cannot send. save_message is deliberately NOT in this list: it is the
    // escape hatch for exactly that case, writing the raw message to a file
    // that can be attached to a new message from an account that can send.
    // Everything below describes the DISPLAYED message, and every route that
    // blanks the pane clears these two ids while leaving currentIndex() valid
    // on a row from the previous result. Answering from the index instead
    // would describe a message that is no longer on screen: it left Reply
    // enabled over an empty pane, and, once this function refilled the pane's
    // bar, put the wrong buttons there in both directions.
    const bool showing =
        !m_currentMessageId.isEmpty() || !m_currentThreadId.isEmpty();

    const QString replyAccount =
        showing ? accountForCurrentMessage() : QString();
    const bool canReply = !replyAccount.isEmpty()
                          && m_config.account(replyAccount).canSend();

    static const QStringList kReplyFamily = {
        QStringLiteral("reply"), QStringLiteral("reply_all"),
        QStringLiteral("reply_no_quote"), QStringLiteral("forward")
    };
    for (const QString &name : kReplyFamily) {
        if (QAction *action = m_actions.value(name))
            action->setEnabled(canReply);
    }

    // Edit draft is offered only on a row that IS a draft. On ordinary mail
    // it would open a composer owning a file it did not write, and the first
    // autosave replaces that file: editing a received message would delete it.
    //
    // Answered from the path, like accountForCurrentMessage() above, because
    // the folder is what makes a draft a draft. A `draft` tag is not enough:
    // notmuch surfaces the Maildir D flag as one, and a message flagged by
    // another client sits in the inbox rather than in the drafts folder.
    if (QAction *edit = m_actions.value(QStringLiteral("edit_draft")))
        edit->setEnabled(showing && currentMessageIsADraft());

    // The pane's bar shows Edit draft in place of the reply pair on a draft
    // (item 157), so it is refilled here rather than once at construction:
    // this runs on every selection change, which is the only thing that can
    // move a draft into or out of the pane.
    populateMessageBar();

    // The ribbon appears only when an account was identified AND it cannot
    // send. An unidentified account is not a receive-only one: it is a message
    // whose file no account owns, and naming no account in a ribbon that
    // exists to name one would be worse than staying quiet.
    const bool receiveOnly =
        !replyAccount.isEmpty() && !m_config.account(replyAccount).canSend();
    m_messageView->setReceiveOnlyAccount(receiveOnly ? replyAccount
                                                     : QString());

    // compose is disabled only when NO account can send. A read-only
    // installation is valid and is not warned about.
    if (QAction *compose = m_actions.value(QStringLiteral("compose")))
        compose->setEnabled(!m_config.sendingAccounts().isEmpty());
}

void MainWindow::saveDisplayedMessage(const QString &chosenDirectory)
{
    const QModelIndex current = m_threadView->currentIndex();

    // Never reached on a conversation row: refreshScopedActionLabels() hides
    // Save there, because a conversation names no single file. Guarded anyway,
    // since a hidden QAction still fires from a shortcut.
    if (m_model->isConversationRow(current)) {
        showTransientStatus(tr("Select one message to save"));
        return;
    }

    const ActionScope scope = m_model->scopeForSelection({ current });
    if (scope.messageIds.isEmpty()) {
        showTransientStatus(tr("No message is selected"));
        return;
    }

    // The path from the model, which is what the pane is rendering. Unlike a
    // reply, a copy of the wrong file is visible to the user the moment they
    // open it, so this does not need the database round trip a reply does.
    QString sourcePath;
    QString subject;
    if (m_model->isMessageRow(current)) {
        const MessageNode node = m_model->messageAt(current);
        sourcePath = node.filePath;
        subject = node.subject;
    } else {
        const ThreadSummary thread = m_model->threadFor(current);
        sourcePath = thread.firstMessagePath;
        subject = thread.subject;
    }
    if (sourcePath.isEmpty()) {
        showTransientStatus(tr("That message's file could not be found"));
        return;
    }

    // Relative for a thread row, absolute for a message row. The same
    // asymmetry accountForCurrentMessage() documents at length.
    if (!QDir::isAbsolutePath(sourcePath) && !m_mailRoot.isEmpty())
        sourcePath = QDir(m_mailRoot).absoluteFilePath(sourcePath);

    if (!QFileInfo::exists(sourcePath)) {
        showTransientStatus(tr("That message's file could not be found"));
        return;
    }

    // The dialog only when no directory was supplied. A test supplies one,
    // because the modal cannot be driven under the offscreen platform and the
    // containment check below is the only line guarding the write.
    const QString directory =
        chosenDirectory.isEmpty()
            ? QFileDialog::getExistingDirectory(
                  this, tr("Save message to"),
                  QStandardPaths::writableLocation(
                      QStandardPaths::DownloadLocation))
            : chosenDirectory;
    if (directory.isEmpty())
        return;  // cancelled

    // The default name is derived from the SUBJECT, which is input from a
    // stranger: it may carry path separators, "..", or nothing usable. The
    // same rules the attachment path follows, and the same helpers, rather
    // than a second implementation that has to be kept correct separately.
    Attachment naming;
    naming.filename = defaultMessageFilename(subject);
    const QString safeName = naming.safeFilename();

    // Disambiguated rather than overwritten, matching what the attachment bar
    // does. Attachment::saveWithoutOverwriting() is the same rule and cannot
    // be reused here because it writes an Attachment's own bytes, while this
    // COPIES a file; the naming is duplicated, the behaviour is not.
    //
    // The earlier version deleted an existing same-named file, on the
    // reasoning that a save the user just confirmed a location for should not
    // silently do nothing. That is right about the failure and wrong about the
    // remedy: two messages very often share a subject, so the second save
    // would destroy the first, and QFile::copy's refusal is a reason to pick
    // another name rather than to delete somebody's file.
    const QFileInfo naming_info(safeName);
    const QString base = naming_info.completeBaseName();
    const QString suffix = naming_info.suffix().isEmpty()
                               ? QString()
                               : QLatin1Char('.') + naming_info.suffix();
    const QDir dir(directory);
    QString candidate = safeName;
    for (int n = 2; dir.exists(candidate); ++n)
        candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);

    const QString target = dir.absoluteFilePath(candidate);

    // Compared as PATHS, never with startsWith(): "/tmp/safe-evil" passes a
    // startsWith("/tmp/safe") check while being a sibling directory.
    if (!Attachment::isPathInsideDirectory(directory, target)) {
        showTransientStatus(tr("Refusing to write outside %1")
                                .arg(QDir::cleanPath(
                                    QDir(directory).absolutePath())));
        return;
    }

    if (!QFile::copy(sourcePath, target)) {
        showTransientStatus(tr("Could not write %1").arg(target));
        return;
    }
    showTransientStatus(tr("Saved %1").arg(target));
}

QAction *MainWindow::addAction(const QString &name, const QString &text,
                               const QString &description,
                               const std::function<void()> &handler)
{
    auto *action = new QAction(text, this);
    action->setObjectName(name);
    action->setStatusTip(description);
    m_actionDescriptions.insert(name, description);

    // The binding comes from KeyMap, so a [keys] override reaches the menus
    // and the shortcut reference as well as the keyboard.
    // Plural: an action can carry more than one binding, and setShortcut()
    // keeps only the last one given. next_thread has both Ctrl+J and Alt+Down.
    const QList<QKeySequence> sequences = m_keyMap.sequencesFor(name);
    if (!sequences.isEmpty())
        action->setShortcuts(sequences);

    // Shortcuts must work while focus is in the thread list or the message
    // view, not only on the window itself.
    action->setShortcutContext(Qt::WindowShortcut);

    connect(action, &QAction::triggered, this, handler);

    // Added to the window so the shortcut is live even before the action is
    // put in a menu; the ones that never reach a menu depend on this.
    QMainWindow::addAction(action);
    m_actions.insert(name, action);
    return action;
}

void MainWindow::registerActions()
{
    addAction(QStringLiteral("focus_query"), tr("&Find"),
              tr("Focus and select the query bar"), [this]() {
        m_queryEdit->setFocus();
        m_queryEdit->selectAll();
    });
    addAction(QStringLiteral("next_thread"), tr("&Next thread"),
              tr("Select the next thread"), [this]() {
        // Walked by INDEX, never by row number. A tree numbers rows per
        // parent, so current.row() + 1 names a SIBLING: from the last reply of
        // an expanded thread it asks for a row that does not exist, and from a
        // thread row it counts top-level threads only by accident (item 60).
        //
        // The skip loop is what keeps this meaning thread-to-thread while the
        // view's own Up/Down still steps message-to-message.
        QModelIndex index = m_threadView->indexBelow(
            m_threadView->currentIndex());
        while (index.isValid()
               && index.data(ThreadListModel::IsMessageRole).toBool()) {
            index = m_threadView->indexBelow(index);
        }
        if (index.isValid())
            selectRowAt(index);
    });
    addAction(QStringLiteral("prev_thread"), tr("&Previous thread"),
              tr("Select the previous thread"), [this]() {
        QModelIndex index = m_threadView->indexAbove(
            m_threadView->currentIndex());
        while (index.isValid()
               && index.data(ThreadListModel::IsMessageRole).toBool()) {
            index = m_threadView->indexAbove(index);
        }
        if (index.isValid())
            selectRowAt(index);
    });
    addAction(QStringLiteral("open_thread"), tr("&Open thread"),
              tr("Focus the thread list"), [this]() {
        m_threadView->setFocus();
    });
    addAction(QStringLiteral("archive"), tr("&Archive"),
              tr("Remove inbox from every selected thread"), [this]() {
        tagSelected({}, { QStringLiteral("inbox") }, tr("Archive"));
    });
    addAction(QStringLiteral("delete"), tr("&Delete"),
              tr("Add or remove the deleted tag"), [this]() {
        // Two directions, and since 2026-08-26 only ONE of them is reachable
        // on ordinary mail.
        //
        // This began as item 16's toggle: pressing Delete twice was how a user
        // said "no, put it back", and it existed because the deleted row
        // STAYED in the view, tinted, with nothing else to press. Delete now
        // strips `inbox` and the row leaves the view immediately, so there is
        // no second press to make and the mitigation is not needed; Ctrl+Z
        // retracts, and Restore in the trash is the deliberate route.
        //
        // The undelete branch survives because it is NOT dead: stranded mail
        // (tagged `deleted`, outside any trash folder, from a version before
        // Delete moved files) is the one place `allDeleted` is still true
        // where Delete is visible at all, since item 168 hides the action
        // whenever every selected row is already in a trash folder. That is
        // what `cleanup_stranded` sends the user to, telling them to select
        // what should go and press Delete.
        //
        // One direction for the WHOLE selection. Toggling each thread
        // independently would leave one keystroke with the selection in two
        // states, which is worse than either outcome, so undelete only when
        // every selected thread is already deleted.
        //
        // Each row's own state, message or thread: a reply row is asked about
        // the MESSAGE it stands for. Asking its thread made Delete one-way on
        // a reply, since a message-scoped write never changes the thread's
        // tags and the answer therefore stayed "not deleted" however many
        // times it was pressed. Item 88 fixed which thread was read here; this
        // is about reading a message at all.
        const bool allDeleted = everySelectedRowHasTag(QStringLiteral("deleted"));

        // Item 103. A MOVE now, not only a tag: Delete used to add `deleted`
        // and leave the file exactly where it was, so deleted mail sat in the
        // inbox indefinitely and only the chip said otherwise.
        if (allDeleted)
            restoreSelected();
        else
            trashSelected();
    });
    addAction(QStringLiteral("restore"), tr("&Restore from trash"),
              tr("Move the selected messages out of the trash"), [this]() {
        restoreSelectedFromTrash();
    });
    addAction(QStringLiteral("cleanup_stranded"),
              tr("Find &stranded deleted mail"),
              tr("Show mail tagged deleted that is not in a trash folder"),
              [this]() {
        showStrandedDeletedMail();
    });
    // The ONE irreversible action in this application, and the only one that
    // asks before it runs (item 118). CLAUDE.md rules out confirmation
    // dialogs for mutations because every mutation pushes its inverse onto
    // the undo stack; a purge has no inverse, so the rule does not reach it.
    // What the rule protects is that the user never loses work to a
    // keystroke, which here is what the dialog provides.
    //
    // No default shortcut, for the same reason: a chord is how this would be
    // run by accident.
    addAction(QStringLiteral("empty_trash"), tr("Empt&y trash..."),
              tr("Permanently delete every message in the trash"), [this]() {
        emptyTrash();
    });
    addAction(QStringLiteral("spam"), tr("Mark &spam"),
              tr("Add spam and remove inbox"), [this]() {
        tagSelected({ QStringLiteral("spam") }, { QStringLiteral("inbox") },
                    tr("Mark spam"));
    });
    // Item 57. The LABEL is "Important"; the action name and the tag are both
    // still `flag`/`flagged`, deliberately. The name is what a user writes in
    // the config's [keys] section, and `flagged` is a notmuch tag that neomutt,
    // the user's saved queries and ThreadSummary::isFlagged() all read. Only
    // the wording the user sees changes.
    //
    // &I rather than &S: the Message menu already has "Mark &spam", so
    // "Starred" would have needed an accelerator from inside the word.
    addAction(QStringLiteral("flag"), tr("&Important"),
              tr("Add or remove the important tag"), [this]() {
        // Item 98. A toggle, like Delete and Toggle unread beside it: adding a
        // tag that is already there is a no-op the user cannot see, so a
        // one-way add read as a dead key on anything already important.
        //
        // everySelectedRowHasTag() rather than a loop of its own. Two separate
        // bugs went into that logic on 2026-08-16 (items 88 and 105), and a
        // copy of the then-current Delete loop would have inherited both:
        // resolving a reply's row number to the wrong thread, and asking a
        // reply's THREAD where the write is message-scoped, which makes a
        // toggle one-way.
        const bool allFlagged = everySelectedRowHasTag(QStringLiteral("flagged"));

        if (allFlagged)
            tagSelected({}, { QStringLiteral("flagged") }, tr("Unmark important"));
        else
            tagSelected({ QStringLiteral("flagged") }, {}, tr("Mark important"));
    });
    addAction(QStringLiteral("toggle_unread"), tr("Toggle &unread"),
              tr("Toggle the unread tag"), [this]() {
        // The state of whatever the rows STAND FOR, which for a reply is the
        // message and not its thread. See everySelectedRowHasTag(): reading
        // the thread here made the key dead on a reply.
        //
        // Item 88 fixed WHICH thread this read. That was necessary and not
        // sufficient: a reply needs a message read, not a better thread.
        //
        // Per selection rather than per current row, matching Delete. The old
        // comment said the direction came from the current row while the
        // change applied to the whole selection, which is the same split that
        // makes a mixed selection land in two states.
        //
        // ANY unread rather than EVERY, which is item 177's catch-all rule and
        // must be the same question refreshUnreadAction() asks for the label:
        // a label promising "Mark thread read" over a write computed from
        // Every would mark a mixed conversation UNREAD, which is the item 112
        // report happening again from the other end.
        const bool unread = selectionTagPresence(QStringLiteral("unread"))
                            != TagPresence::None;

        // An explicit toggle overrides the automatic one. Without this, marking
        // a thread unread by hand would be undone a moment later by a timer
        // armed when it was opened, and the key would look broken.
        m_markReadTimer->stop();
        m_markReadMessageId.clear();

        if (unread)
            tagSelected({}, { QStringLiteral("unread") }, tr("Mark read"));
        else
            tagSelected({ QStringLiteral("unread") }, {}, tr("Mark unread"));
    });
    addAction(QStringLiteral("mark_all_read"), tr("Mark all &read"),
              tr("Remove the unread tag from every thread in this view"),
              [this]() {
        markAllRead();
    });
    addAction(QStringLiteral("edit_tags"), tr("Edit &tags..."),
              tr("Add or remove any tag on the selected threads"), [this]() {
        editTagsOnSelection();
    });

    addAction(QStringLiteral("tag_rules"), tr("Tagging &rules..."),
              tr("Edit the rules that tag mail as it arrives"), [this]() {
        showTagRulesDialog();
    });
    addAction(QStringLiteral("save_query"), tr("&Save query..."),
              tr("Keep the current query as a saved query"), [this]() {
        saveCurrentQuery();
    });
    addAction(QStringLiteral("toggle_html"), tr("Toggle &HTML"),
              tr("Switch the thread between HTML and plain text"), [this]() {
        m_messageView->toggleHtml();
    });
    addAction(QStringLiteral("load_remote"), tr("Load &remote content"),
              tr("Load remote images for the current thread"), [this]() {
        m_messageView->loadRemoteContent();
    });
    addAction(QStringLiteral("message_details"), tr("Message &details"),
              tr("Show the full headers of every message in the thread"),
              [this]() {
        m_messageView->showDetailsDialog();
    });
    addAction(QStringLiteral("zoom_in"), tr("Zoom &in"),
              tr("Enlarge the message text"), [this]() {
        m_messageView->zoomIn();
    });
    addAction(QStringLiteral("zoom_out"), tr("Zoom &out"),
              tr("Shrink the message text"), [this]() {
        m_messageView->zoomOut();
    });
    auto *zoomReset =
        addAction(QStringLiteral("zoom_reset"), tr("&Actual size"),
                  tr("Return the message text to its default size"), [this]() {
        m_messageView->zoomReset();
    });

    // Ctrl+= alongside the configured binding: '=' reads as "back to normal",
    // and on a layout where '+' is Shift+'=' it is the unshifted key next to
    // zoom in. Appended rather than assigned, so a [keys] override of
    // zoom_reset keeps working and simply gains this as a second way in.
    // A user who bound Ctrl+= to something else in [keys] keeps their binding.
    const QKeySequence altReset(QStringLiteral("Ctrl+="));
    if (m_keyMap.actionFor(altReset).isEmpty()) {
        QList<QKeySequence> shortcuts = zoomReset->shortcuts();
        shortcuts.append(altReset);
        zoomReset->setShortcuts(shortcuts);
    }

    addAction(QStringLiteral("undo"), tr("&Undo"),
              tr("Undo the last tag change"), [this]() {
        if (m_undoStack.canUndo())
            m_undoStack.undo();
        else
            showTransientStatus(tr("Nothing to undo"));
    });
    addAction(QStringLiteral("sync"), tr("&Sync"),
              tr("Run the configured sync command"), [this]() {
        startSync();
    });
    addAction(QStringLiteral("complete_query"), tr("&Complete query"),
              tr("Offer completions for the query bar"), [this]() {
        // Focus first: the popup anchors on the line edit, and the binding is
        // reachable from the thread list where the bar has no focus at all.
        m_queryEdit->setFocus();
        m_queryCompleter->triggerCompletion();
    });
    addAction(QStringLiteral("clear_pane"), tr("Clear &message pane"),
              tr("Blank the message pane without changing the selection"),
              [this]() {
        // A view change, not a mail change: the selection, the query and the
        // undo stack are all left alone.
        //
        // m_currentThreadId is cleared with the pane, not merely alongside it.
        // A messageLoaded still in flight for that row would otherwise paint
        // it straight back, which is the queued-reply race documented in
        // CLAUDE.md.
        m_currentThreadId.clear();
        m_currentMessageId.clear();
        m_currentMessageThreadId.clear();
        m_messageView->clear();
        showPlaceholderPane();
        m_markReadTimer->stop();
        m_markReadMessageId.clear();
    });
    addAction(QStringLiteral("clear_selection"), tr("Clear &selection"),
              tr("Blank the message pane and deselect every thread"),
              [this]() {
        // Item 50, and the user's wording was "two actions instead of one":
        // clear_pane above still blanks without touching the selection, this
        // one does both. Esc defaults here, since deselecting is what Esc means
        // nearly everywhere else.
        //
        // BOTH LINES BELOW ARE LOAD-BEARING, AND SO IS THEIR PLACE ABOVE THE
        // BLANKING. clearSelection() leaves currentIndex() VALID, and
        // onSelectionChanged() then takes its "one or fewer rows" branch, finds
        // a current row whose id differs from m_currentThreadId, and calls
        // onThreadSelected for it: the thread is re-adopted and a load sent
        // for the row that was just being cleared.
        //
        // Clearing the selection FIRST means that runs while m_currentThreadId
        // still names the displayed thread, so the ids match and nothing is
        // reloaded; setCurrentIndex() then stops any later collapse-to-one-row
        // reaching the same row again.
        //
        // All four arrangements were tried against
        // clearSelectionBlanksThePaneAndDeselects, and only this one passes:
        // dropping setCurrentIndex() fails, and moving either line after the
        // blanking fails.
        m_threadView->clearSelection();
        m_threadView->setCurrentIndex(QModelIndex());

        m_currentThreadId.clear();
        m_currentMessageId.clear();
        m_currentMessageThreadId.clear();
        m_messageView->clear();
        showPlaceholderPane();
        m_markReadTimer->stop();
        m_markReadMessageId.clear();
    });
    addAction(QStringLiteral("select_all"), tr("Select &all threads"),
              tr("Select every thread in the current result list"), [this]() {
        // A registered action rather than the view's built-in SelectAll key, so
        // it reaches the Edit menu, the shortcut reference and [keys] the same
        // way every other binding does. That is the whole point: multi-select
        // already worked, it was simply invisible.
        m_threadView->selectAll();
    });
    addAction(QStringLiteral("quit"), tr("&Quit"),
              tr("Quit qtmaildir"), [this]() { close(); });

    // Compose and send (item 123). The handlers are empty: this is the
    // registration, so the three coverage tests
    // (everyKnownActionIsRegistered, everyActionCarriesAnIcon and
    // everyActionIsReachableFromAMenu) cover the composer from the first
    // commit rather than being satisfied once it is finished.
    //
    // Reply and reply-without-quoting are the same Kind with and without a
    // seeded body, which is why the quoting is a parameter rather than a
    // fourth Kind: the recipients, the subject prefix and the threading
    // headers are identical, and only the body differs.
    addAction(QStringLiteral("compose"), tr("&New message"),
              tr("Compose a new message"), [this]() { composeNew(); });
    addAction(QStringLiteral("reply"), tr("Re&ply"),
              tr("Reply to the displayed message"),
              [this]() { composeReply(ComposeContext::Kind::Reply, true); });
    addAction(QStringLiteral("reply_all"), tr("Reply to a&ll"),
              tr("Reply to the sender and every other recipient"),
              [this]() { composeReply(ComposeContext::Kind::ReplyAll, true); });
    addAction(QStringLiteral("reply_no_quote"), tr("Reply without &quoting"),
              tr("Reply with an empty body"),
              [this]() { composeReply(ComposeContext::Kind::Reply, false); });
    addAction(QStringLiteral("forward"), tr("&Forward"),
              tr("Forward the displayed message"),
              [this]() { composeReply(ComposeContext::Kind::Forward, true); });
    addAction(QStringLiteral("edit_draft"), tr("&Edit draft"),
              tr("Open the selected draft in a composer to finish it"),
              [this]() { editDraft(); });
    addAction(QStringLiteral("save_message"), tr("Sa&ve message as..."),
              tr("Write the raw message to a file"),
              [this]() { saveDisplayedMessage(); });

    // A binding the user wrote for an action that does not exist would be
    // silently dead. KeyMap warns about unknown names, but only a check here
    // catches the reverse: a known action nothing implements.
    Q_ASSERT(m_actions.size() == KeyMap::knownActions().size());

    // QAction starts enabled, so the view-wide actions have to be put into
    // their real state here rather than waiting for the first query: a window
    // that has not run one yet has an empty model and no complete result set,
    // and offering "Mark all read" against nothing is a live control that does
    // nothing.
    updateViewWideActions();

    // Compose and the reply family, for the same reason: QAction starts
    // enabled, so a window with nothing selected would offer a live Reply.
    updateComposeActions();
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_actions.value(QStringLiteral("sync")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_actions.value(QStringLiteral("quit")));

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_actions.value(QStringLiteral("undo")));
    editMenu->addSeparator();
    editMenu->addAction(m_actions.value(QStringLiteral("focus_query")));
    editMenu->addAction(m_actions.value(QStringLiteral("complete_query")));
    editMenu->addAction(m_actions.value(QStringLiteral("save_query")));
    editMenu->addSeparator();
    editMenu->addAction(m_actions.value(QStringLiteral("select_all")));

    auto *messageMenu = menuBar()->addMenu(tr("&Message"));
    // Composing sits above organising (item 123). The spec called for a new
    // top-level Message menu and this one already existed, so the six join it:
    // two menus named Message would be a defect.
    messageMenu->addAction(m_actions.value(QStringLiteral("compose")));
    messageMenu->addSeparator();
    messageMenu->addAction(m_actions.value(QStringLiteral("reply")));
    messageMenu->addAction(m_actions.value(QStringLiteral("reply_all")));
    messageMenu->addAction(m_actions.value(QStringLiteral("reply_no_quote")));
    messageMenu->addAction(m_actions.value(QStringLiteral("forward")));
    messageMenu->addAction(m_actions.value(QStringLiteral("edit_draft")));
    messageMenu->addSeparator();
    messageMenu->addAction(m_actions.value(QStringLiteral("save_message")));
    messageMenu->addSeparator();
    messageMenu->addAction(m_actions.value(QStringLiteral("archive")));
    messageMenu->addAction(m_actions.value(QStringLiteral("delete")));
    // Beside Delete, whose inverse it is. Greyed outside the trash view
    // rather than hidden: an action that vanishes teaches nothing, while a
    // disabled entry with its shortcut beside it says both that it exists and
    // where it applies.
    messageMenu->addAction(m_actions.value(QStringLiteral("restore")));
    messageMenu->addAction(m_actions.value(QStringLiteral("spam")));
    messageMenu->addSeparator();
    messageMenu->addAction(m_actions.value(QStringLiteral("toggle_unread")));
    messageMenu->addAction(m_actions.value(QStringLiteral("mark_all_read")));
    messageMenu->addAction(m_actions.value(QStringLiteral("edit_tags")));
    messageMenu->addAction(m_actions.value(QStringLiteral("flag")));
    // Separated from the entries above: those act on the selection, this edits
    // a rule store shared with mailctl and changes nothing that is on screen.
    messageMenu->addSeparator();
    // A MENU entry and nothing else, at the user's request: "the cleanup
    // should be a menu entry only, not to be confused with the filter Trash".
    // It replaces the whole view like a filter does, so a sixth button beside
    // the five filters would read as one of them.
    messageMenu->addAction(m_actions.value(QStringLiteral("cleanup_stranded")));
    messageMenu->addAction(m_actions.value(QStringLiteral("empty_trash")));
    messageMenu->addAction(m_actions.value(QStringLiteral("tag_rules")));

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_actions.value(QStringLiteral("prev_thread")));
    viewMenu->addAction(m_actions.value(QStringLiteral("next_thread")));
    viewMenu->addAction(m_actions.value(QStringLiteral("open_thread")));
    viewMenu->addSeparator();
    // The two clears. Both shipped keyboard-only, which is what
    // everyActionIsReachableFromAMenu() exists to stop: an action reachable
    // only by a chord is an action nobody discovers.
    viewMenu->addAction(m_actions.value(QStringLiteral("clear_pane")));
    viewMenu->addAction(m_actions.value(QStringLiteral("clear_selection")));
    viewMenu->addSeparator();
    viewMenu->addAction(m_actions.value(QStringLiteral("toggle_html")));
    viewMenu->addAction(m_actions.value(QStringLiteral("load_remote")));
    viewMenu->addAction(m_actions.value(QStringLiteral("message_details")));
    viewMenu->addSeparator();
    viewMenu->addAction(m_actions.value(QStringLiteral("zoom_in")));
    viewMenu->addAction(m_actions.value(QStringLiteral("zoom_out")));
    viewMenu->addAction(m_actions.value(QStringLiteral("zoom_reset")));

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *shortcuts = helpMenu->addAction(tr("&Keyboard shortcuts"));
    connect(shortcuts, &QAction::triggered,
            this, &MainWindow::showShortcutReference);
    // A dialog the user asks for, per item 34: counting every message is not
    // free on a large database, so this must not be anything that refreshes on
    // its own.
    auto *maildirInfo = helpMenu->addAction(tr("&Maildir overview"));
    maildirInfo->setObjectName(QStringLiteral("maildirOverview"));
    connect(maildirInfo, &QAction::triggered,
            this, &MainWindow::showMaildirOverview);
    auto *about = helpMenu->addAction(tr("&About"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    // Standard names from the icon theme, so the buttons match the rest of the
    // desktop rather than shipping bespoke art. A theme that lacks one leaves
    // that action with text alone, which still works.
    // Item 56: every registered action, not a subset. Eight of these carried an
    // icon and sixteen did not, which reads worse than none having one: two
    // adjacent entries in the same menu disagreed, and the toolbar's
    // TextBesideIcon style laid out an empty slot for each of the sixteen.
    //
    // Names are freedesktop ones, and were probed against a real icon theme
    // rather than taken from the spec on faith. A name the running theme lacks
    // still degrades to text through the null check below.
    const QHash<QString, QString> themeIcons = {
        { QStringLiteral("sync"),    QStringLiteral("view-refresh") },
        // NOT mail-mark-read, which mark_all_read below uses. The two shared it
        // in 0.12.0, and with the toolbar icon-only the icon is the whole
        // control: two buttons with different consequences looked identical.
        { QStringLiteral("archive"), QStringLiteral("mail-archive") },
        { QStringLiteral("delete"),  QStringLiteral("edit-delete") },
        // The inverse of delete, and the theme's own name for it: the icon
        // every desktop uses for taking something back out of the wastebasket.
        { QStringLiteral("restore"), QStringLiteral("edit-undelete") },
        // A SEARCH, not a delete. The action reports what it finds and moves
        // nothing, so an icon from the delete family would promise the one
        // thing it deliberately does not do.
        { QStringLiteral("cleanup_stranded"), QStringLiteral("system-search") },
        { QStringLiteral("empty_trash"), QStringLiteral("edit-delete-shred") },
        { QStringLiteral("undo"),    QStringLiteral("edit-undo") },
        { QStringLiteral("spam"),    QStringLiteral("mail-mark-junk") },
        { QStringLiteral("flag"),    QStringLiteral("mail-mark-important") },
        { QStringLiteral("quit"),    QStringLiteral("application-exit") },
        { QStringLiteral("focus_query"), QStringLiteral("edit-find") },

        { QStringLiteral("next_thread"),     QStringLiteral("go-down") },
        { QStringLiteral("prev_thread"),     QStringLiteral("go-up") },
        { QStringLiteral("open_thread"),     QStringLiteral("document-open") },
        { QStringLiteral("toggle_unread"),   QStringLiteral("mail-mark-unread") },
        { QStringLiteral("mark_all_read"),   QStringLiteral("mail-mark-read") },
        { QStringLiteral("edit_tags"),       QStringLiteral("tag") },
        // NOT "tag", which edit_tags uses: with the toolbar icon-only the icon
        // is the whole control, and editing the standing rules is not editing
        // the selection's tags.
        { QStringLiteral("tag_rules"),       QStringLiteral("configure") },
        { QStringLiteral("complete_query"),  QStringLiteral("edit-find-replace") },
        // NOT "document-save": that is the floppy/disk shape, which reads as
        // "write a file somewhere" and asks the user to guess what is being
        // written. Saving a query is bookmarking a search, and bookmark-new is
        // the icon set every desktop already uses for "keep this for later".
        { QStringLiteral("save_query"),      QStringLiteral("bookmark-new") },
        { QStringLiteral("select_all"),      QStringLiteral("edit-select-all") },
        { QStringLiteral("clear_pane"),      QStringLiteral("edit-clear") },
        { QStringLiteral("clear_selection"), QStringLiteral("edit-clear-all") },
        { QStringLiteral("edit_draft"),      QStringLiteral("document-edit") },
        { QStringLiteral("toggle_html"),     QStringLiteral("text-html") },
        { QStringLiteral("load_remote"),     QStringLiteral("image-loading") },
        { QStringLiteral("message_details"), QStringLiteral("dialog-information") },
        { QStringLiteral("zoom_in"),         QStringLiteral("zoom-in") },
        { QStringLiteral("zoom_out"),        QStringLiteral("zoom-out") },
        { QStringLiteral("zoom_reset"),      QStringLiteral("zoom-original") },

        // Compose and send (item 123). reply_no_quote SHARES reply's icon,
        // which the no-duplicates rule allows because that rule exists for the
        // icon-only TOOLBAR, where the icon is the entire control: it never
        // reaches the toolbar, it is a menu entry that always carries its
        // text, and "Reply without quoting" beside the reply icon is the
        // honest pairing.
        // It is named in the exception list in noTwoActionsShareAnIcon(), so
        // putting it on the toolbar fails that test rather than passing
        // silently.
        { QStringLiteral("compose"),        QStringLiteral("mail-message-new") },
        { QStringLiteral("reply"),          QStringLiteral("mail-reply-sender") },
        { QStringLiteral("reply_all"),      QStringLiteral("mail-reply-all") },
        { QStringLiteral("reply_no_quote"), QStringLiteral("mail-reply-sender") },
        { QStringLiteral("forward"),        QStringLiteral("mail-forward") },
        // NOT bookmark-new, which save_query uses: this really does write a
        // file the user names, which is exactly what the disk shape means.
        { QStringLiteral("save_message"),   QStringLiteral("document-save-as") },
    };
    for (auto it = themeIcons.cbegin(); it != themeIcons.cend(); ++it) {
        QAction *action = m_actions.value(it.key());
        if (!action)
            continue;
        const QIcon icon = QIcon::fromTheme(it.value());
        if (!icon.isNull())
            action->setIcon(icon);
    }

    // Right-click on the thread list. Built from the same registered QActions
    // as the menu bar, never from parallel copies: a [keys] override then shows
    // the right shortcut here too, and an action cannot end up doing one thing
    // from the menu bar and another from the context menu.
    //
    // Every entry applies to the whole selection already, since they all funnel
    // through tagSelected(), so this needs no multi-row special casing.
    m_threadContextMenu = new QMenu(this);
    m_threadContextMenu->setObjectName(QStringLiteral("threadContextMenu"));
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("archive")));
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("delete")));
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("restore")));
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("spam")));
    m_threadContextMenu->addSeparator();
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("toggle_unread")));
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("flag")));
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("edit_tags")));
    m_threadContextMenu->addSeparator();
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("select_all")));

    m_threadView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_threadView, &QWidget::customContextMenuRequested,
            this, &MainWindow::showThreadContextMenu);

    // The frequent subset only. A toolbar holding every action is as
    // unreadable as no toolbar.
    auto *toolBar = addToolBar(tr("Main"));
    toolBar->setObjectName(QStringLiteral("main_toolbar"));
    // Item 56, second half: the user asked that buttons honour the desktop's
    // "Icon only" setting. They cannot while this asserts a style of its own.
    // Qt exposes the desktop's preference as SH_ToolButtonStyle, and a
    // hardcoded setToolButtonStyle() overrides it whatever the user chose.
    //
    // Read rather than dropped entirely: with no call at all a QToolBar
    // defaults to Qt::ToolButtonIconOnly rather than to the platform's hint,
    // which would ignore the setting just as thoroughly in the other direction.
    toolBar->setToolButtonStyle(static_cast<Qt::ToolButtonStyle>(
        style()->styleHint(QStyle::SH_ToolButtonStyle, nullptr, toolBar)));

    // Set explicitly rather than left to the style. With the button style above
    // resolving to icon-only on this desktop, the icon IS the control, and this
    // style's PM_ToolBarIconSize is 16px, which is a small target for it.
    // Configurable because the right answer depends on the display, not on
    // anything this code can see.
    const int iconSize = m_config.toolbarIconSize();
    toolBar->setIconSize(QSize(iconSize, iconSize));

    // Compose stays here, and Reply and Forward do not (item 140). The split
    // is what the action NEEDS: composing a new message requires no message at
    // all, so it belongs with the window-wide operations, while Reply and
    // Forward act on whatever the pane is showing and live on its own bar.
    toolBar->addAction(m_actions.value(QStringLiteral("compose")));
    toolBar->addSeparator();

    QAction *syncAction = m_actions.value(QStringLiteral("sync"));
    // Carried over from the QPushButton this replaced: with no command
    // configured the control is disabled, and the tooltip is the only thing
    // that says why.
    if (syncAction && m_sync && !m_sync->isAvailable()) {
        syncAction->setEnabled(false);
        syncAction->setToolTip(
            tr("No sync command configured ([sync] command in qtmaildir.conf)"));
    }
    toolBar->addAction(syncAction);
    toolBar->addSeparator();
    toolBar->addAction(m_actions.value(QStringLiteral("archive")));
    toolBar->addAction(m_actions.value(QStringLiteral("delete")));
    toolBar->addAction(m_actions.value(QStringLiteral("mark_all_read")));
    toolBar->addSeparator();
    toolBar->addAction(m_actions.value(QStringLiteral("undo")));
}

void MainWindow::populateMessageBar()
{
    // The window's own QActions, shown a second time rather than copied: a
    // duplicate QAction would need its own enablement and would drift from the
    // menu entry that updateComposeActions() keeps in step.
    // Reply and Forward only: Compose needs no message and sits on the main
    // toolbar with the other window-wide actions.
    //
    // A draft swaps that pair for Edit draft (item 157). It is the same rule
    // items 139 to 141 settled, applied one level down: the bar carries what
    // the DISPLAYED message affords, and a draft affords neither answering a
    // sender it does not have nor passing on a message that is not finished.
    // The view controls are unchanged by the swap, since how the pane renders
    // is not a property of what the message is.
    //
    // Called from updateComposeActions() as well as at construction, so it
    // follows the selection. That is also why the actions are looked up fresh
    // rather than cached: the bar is refilled, never rebuilt.
    //
    // Slightly smaller than the main toolbar's icons, deriving from the
    // configured size rather than hardcoding one, so the bar stays subordinate
    // to the chrome above it however the user sets that key.
    const int iconSize = qMax(16, (m_config.toolbarIconSize() * 7) / 8);

    // Gated on whether a message is DISPLAYED, not on which row is current.
    // The two disagree on every route that blanks the pane without moving the
    // selection: running a query leaves currentIndex() valid on a row from the
    // previous result, so a bar keyed on it kept offering Edit draft over an
    // empty pane after leaving the Drafts filter, and the reply pair after
    // arriving at it. This is item 150's trap exactly, one level up, and the
    // test that missed it moved row to row, which is the one gesture that
    // cannot expose it.
    //
    // m_currentMessageId is cleared with the pane by every one of those
    // routes, so it is the only thing that tracks what the bar describes.
    // The bar always carries a message half. Whether it is SEEN is
    // MessageView's question, not this one: it hides the whole bar over an
    // empty pane, alongside the subject and the details button, so this only
    // ever decides what a displayed message affords.
    //
    // Only a displayed DRAFT swaps the pair, and "displayed" is the operative
    // word: keyed on m_currentMessageId rather than on currentIndex(), which
    // stays valid on a row from the previous result after a query and made the
    // bar describe a message that was no longer on screen, in both directions.
    // That is item 150's trap one level up, and the first version of this test
    // could not see it because it moved row to row, the one gesture that
    // always changes both.
    // currentMessageIsADraft() alone, with no displayed-message guard beside
    // it: a query calls m_model->clear(), which invalidates currentIndex(),
    // so the predicate is already false whenever the pane is blank. Measured,
    // after writing that guard and finding no reachable state where it
    // changed the answer. The guard that IS load-bearing sits one level up in
    // updateComposeActions(), where accountForCurrentMessage() would otherwise
    // answer about a row this query is discarding.
    QList<QAction *> messageActions;
    if (currentMessageIsADraft()) {
        messageActions = { m_actions.value(QStringLiteral("edit_draft")) };
    } else {
        messageActions = { m_actions.value(QStringLiteral("reply")),
                           m_actions.value(QStringLiteral("forward")) };
    }

    m_messageView->setBarActions(
        messageActions, { m_actions.value(QStringLiteral("toggle_html")) },
        iconSize);
}

void MainWindow::showShortcutReference()
{
    // Generated from the actions, so it cannot disagree with what the keys
    // really do. A hand-written list would drift the first time a binding
    // changed.
    QStringList rows;
    for (const QString &name : registeredActionNames()) {
        const QAction *action = m_actions.value(name);
        if (!action)
            continue;
        const QString sequence = action->shortcut().toString(QKeySequence::NativeText);
        rows.append(QStringLiteral("<tr><td><tt>%1</tt>&nbsp;&nbsp;</td>"
                                   "<td>%2&nbsp;&nbsp;</td>"
                                   "<td><tt>%3</tt></td></tr>")
                        .arg(sequence.isEmpty() ? tr("(unbound)") : sequence.toHtmlEscaped(),
                             m_actionDescriptions.value(name).toHtmlEscaped(),
                             name.toHtmlEscaped()));
    }

    // Two columns rather than one. Fourteen actions in a single table made a
    // dialog taller than the screen, which cut off its own title bar.
    const int half = (rows.size() + 1) / 2;
    const QString header =
        tr("<tr><th align='left'>Key</th><th align='left'>Does</th>"
           "<th align='left'>Action name</th></tr>");
    const QString left = header + rows.mid(0, half).join(QString());
    const QString right = header + rows.mid(half).join(QString());

    // A QDialog rather than QMessageBox: the message box wraps its text at a
    // narrow default width, which turned every description into a column of
    // single words and made the dialog taller than the screen.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Keyboard shortcuts"));

    auto *label = new QLabel(&dialog);
    label->setTextFormat(Qt::RichText);
    label->setText(tr("<table cellspacing='0'><tr>"
                      "<td valign='top'><table cellpadding='3'>%1</table></td>"
                      "<td width='32'></td>"
                      "<td valign='top'><table cellpadding='3'>%2</table></td>"
                      "</tr></table>")
                       .arg(left, right));

    // Mouse selection is view behaviour, not an action, so it cannot appear in
    // the table above however the table is generated. Said here because it is
    // otherwise undiscoverable: nothing in the UI hints that a thread list
    // takes more than one row at a time.
    auto *selectionNote = new QLabel(
        tr("<b>Thread list:</b> <tt>Ctrl</tt>+click adds or removes a single "
           "row, <tt>Shift</tt>+click extends the selection to a range. Tag, "
           "archive and delete all apply to every selected thread."),
        &dialog);
    selectionNote->setTextFormat(Qt::RichText);
    selectionNote->setWordWrap(true);

    auto *note = new QLabel(
        tr("Rebind any of these in the <tt>[keys]</tt> section of "
           "<tt>qtmaildir.conf</tt>, using the action name."),
        &dialog);
    note->setTextFormat(Qt::RichText);
    note->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(label);
    layout->addWidget(selectionNote);
    layout->addWidget(note);
    layout->addStretch();
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showMaildirOverview()
{
    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Maildir overview"));
    dialog->setObjectName(QStringLiteral("maildirOverviewDialog"));
    // Deleted on close, which is what makes m_overviewCounts a QPointer: the
    // worker's reply can arrive after the user has dismissed it.
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto *counts = new QLabel(dialog);
    counts->setObjectName(QStringLiteral("maildirCounts"));
    counts->setTextFormat(Qt::RichText);
    // Shown as pending rather than as zero. The dialog opens before the answer
    // arrives, and a zero would read as "no mail", which is a claim rather than
    // an absence of one.
    counts->setText(tr("<b>Counting...</b>"));
    m_overviewCounts = counts;

    // From config, never from notmuch, which does not model accounts at all.
    // That is the whole reason per-account subdirectories are configured.
    QString accountText;
    const QList<Account> accounts = m_config.accounts();
    accountText += tr("<b>%n account(s)</b>", "", int(accounts.size()));
    if (!accounts.isEmpty()) {
        accountText += QStringLiteral("<ul>");
        for (const Account &account : accounts) {
            // Account names are user-written config, and this label is rich
            // text, so they are escaped like any other untrusted value.
            const QString label = account.label.isEmpty() ? account.key
                                                          : account.label;
            accountText += QStringLiteral("<li>%1</li>")
                               .arg(label.toHtmlEscaped());
        }
        accountText += QStringLiteral("</ul>");
    }

    auto *accountLabel = new QLabel(accountText, dialog);
    accountLabel->setObjectName(QStringLiteral("maildirAccounts"));
    accountLabel->setTextFormat(Qt::RichText);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(dialog);
    layout->addWidget(counts);
    layout->addWidget(accountLabel);
    layout->addStretch();
    layout->addWidget(buttons);

    // Asked for when the dialog opens and never on a timer: counting every
    // message in a large database is not free, which is the constraint that
    // made this a dialog rather than a status-bar field.
    QMetaObject::invokeMethod(m_worker, "requestDatabaseStats",
                              Qt::QueuedConnection,
                              Q_ARG(quint64, ++m_statsGeneration));

    dialog->show();
}

void MainWindow::onDatabaseStatsReady(const DatabaseStats &stats,
                                      quint64 generation)
{
    // Closed and reopened while the count ran: this answer belongs to the old
    // dialog. The QPointer covers "closed", this covers "closed and reopened".
    if (generation != m_statsGeneration)
        return;

    if (!m_overviewCounts)
        return;

    // A field notmuch could not answer stays unknown. Printing 0 would say the
    // database is empty, which is the opposite of "we could not tell".
    const auto number = [](int value) {
        return value < 0 ? tr("unknown") : QLocale().toString(value);
    };

    m_overviewCounts->setText(
        tr("<b>%1</b> messages in <b>%2</b> threads<br>"
           "<b>%3</b> tags")
            .arg(number(stats.messages), number(stats.threads),
                 number(stats.tags)));
}

void MainWindow::showTagRulesDialog(const TagRule &seed)
{
    // One dialog. A second would edit a stale copy and the last Save would
    // silently win, which is the lost-edit case the atomic write cannot help
    // with because both writers are this process.
    if (m_tagRulesDialog) {
        // Seeded into the dialog already up rather than dropped: the menu item
        // must do something visible, and a second dialog would edit a stale
        // copy whose Save would silently win.
        if (!seed.query.isEmpty())
            m_tagRulesDialog->seedRule(seed);
        m_tagRulesDialog->raise();
        m_tagRulesDialog->activateWindow();
        return;
    }

    auto *dialog = new TagRulesDialog(seed, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    m_tagRulesDialog = dialog;

    // The Folder row's dropdown, filled from the Maildir tree on disk rather
    // than from config. Config names one subtree per account and nothing
    // below it, so the dropdown offered five entries and no way to say Drafts
    // or Sent, which is a folder a rule wants to target as often as a whole
    // account. The answer comes back queued, after the dialog is already up;
    // setFolders refills the rows that exist by then.
    QMetaObject::invokeMethod(m_worker, "requestFolders", Qt::QueuedConnection);

    connect(dialog, &TagRulesDialog::previewRequested,
            this, &MainWindow::onRulePreviewRequested);

    connect(dialog, &TagRulesDialog::countsRequested, this, [this, dialog]() {
        QMetaObject::invokeMethod(
            m_worker, "requestMessageCounts", Qt::QueuedConnection,
            Q_ARG(QStringList, dialog->countQueries()),
            Q_ARG(quint64, ++m_ruleCountGeneration));
    });

    dialog->show();
}

void MainWindow::onRuleCountsReady(const QVector<int> &counts,
                                   quint64 generation)
{
    // Stale reply, or the dialog closed while the count was in flight. Both
    // are ordinary rather than rare: counting every rule against a cold index
    // takes seconds, which is long enough for the user to close the dialog or
    // press the button again.
    if (generation != m_ruleCountGeneration || !m_tagRulesDialog)
        return;

    m_tagRulesDialog->setCounts(counts);
}

void MainWindow::showAbout()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About qtmaildir"));

    auto *icon = new QLabel(&dialog);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/qtmaildir.svg"))
                        .pixmap(QSize(160, 160)));
    icon->setAlignment(Qt::AlignCenter);

    auto *text = new QLabel(&dialog);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignTop);
    text->setText(tr("<h3>qtmaildir %1</h3>"
                     "<p>A Qt6 mail client for notmuch-indexed Maildirs.</p>"
                     "<p>Reads and organizes local mail. Fetching and sending "
                     "are handled by external scripts.</p>"
                     "<p>Copyright &copy; 2026 Danilo M. "
                     "&lt;danix@danix.xyz&gt;<br>"
                     "Licensed under the GNU General Public License "
                     "version 2.</p>"
                     "<p>Developed with AI assistance. All code is reviewed, "
                     "tested and curated by the maintainer.</p>")
                      .arg(QStringLiteral(QTMAILDIR_VERSION_DISPLAY)));

    auto *link = new QLabel(
        QStringLiteral("<a href='https://danix.xyz/qtmaildir'>"
                       "https://danix.xyz/qtmaildir</a>"),
        &dialog);
    link->setTextFormat(Qt::RichText);
    link->setAlignment(Qt::AlignCenter);
    link->setOpenExternalLinks(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);

    auto *columns = new QHBoxLayout;
    columns->addWidget(icon, 40);
    columns->addWidget(text, 60);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addLayout(columns);
    layout->addWidget(link);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::wireWorker()
{
    m_worker = new NotmuchWorker(m_config.notmuchConfig());
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &NotmuchWorker::threadsReady,
            this, &MainWindow::onThreadsReady);
    connect(m_worker, &NotmuchWorker::queryFinished,
            this, &MainWindow::onQueryFinished);
    connect(m_worker, &NotmuchWorker::threadTreeLoaded,
            this, &MainWindow::onThreadTreeLoaded);
    connect(m_worker, &NotmuchWorker::messageLoaded,
            this, &MainWindow::onMessageLoaded);
    connect(m_worker, &NotmuchWorker::errorOccurred,
            this, &MainWindow::onWorkerError);
    connect(m_worker, &NotmuchWorker::allTagsReady,
            this, &MainWindow::onAllTagsReady);
    connect(m_worker, &NotmuchWorker::mailRootReady,
            this, &MainWindow::onMailRootReady);
    connect(m_worker, &NotmuchWorker::countsReady,
            this, &MainWindow::onCountsReady);
    connect(m_worker, &NotmuchWorker::databaseStatsReady,
            this, &MainWindow::onDatabaseStatsReady);
    connect(m_worker, &NotmuchWorker::messageCountsReady,
            this, &MainWindow::onRuleCountsReady);

    // Sender counts feed the business-senders candidate list after a sync.
    // The connection is queued, so the QHash argument must be a registered
    // metatype; notmuchworker.cpp registers it beside SortOrder.
    connect(m_worker, &NotmuchWorker::senderCountsReady, this,
            [this](const QHash<QString, int> &counts) {
                // Never applies anything: appendCandidates writes commented
                // lines only, so nothing on screen changes until the user
                // uncomments one. The list is then reloaded so an entry they
                // uncommented by hand takes effect without a restart.
                BusinessSenders::appendCandidates(
                    BusinessSenders::defaultPath(), counts);
                loadBusinessSenders();
            });

    // The rules dialog is the only consumer, and it may have been closed while
    // the scan was in flight. No generation counter: the tree on disk does not
    // change under a query, so a late answer is still the right one.
    connect(m_worker, &NotmuchWorker::foldersReady, this,
            [this](const QStringList &folders) {
                if (m_tagRulesDialog)
                    m_tagRulesDialog->setFolders(folders);
            });

    // A confirmed write clears the pending revert: without this, a later
    // unrelated error would roll back a change that actually succeeded.
    connect(m_worker, &NotmuchWorker::tagsApplied,
            this, &MainWindow::onTagsApplied);
    connect(m_worker, &NotmuchWorker::pendingSubjectsResolved,
            this, &MainWindow::onPendingSubjectsResolved);

    // messagesMovedFrom rather than messagesMoved: the tags a move carries can
    // only be resolved once the origins are known, and that signal is the one
    // that reports them.
    connect(m_worker, &NotmuchWorker::messagesMovedFrom,
            this, &MainWindow::onMessagesMoved);

    // A purge removes rows rather than changing them, so there is no
    // optimistic update to apply: the only honest view is the one the query
    // gives now. Without this the list went on showing mail that no longer
    // existed until the user refreshed by hand, which is how the user found
    // it.
    connect(m_worker, &NotmuchWorker::messagesPurged, this,
            [this](const QStringList &messageIds) {
        showTransientStatus(
            tr("Deleted %n message(s) permanently", "", messageIds.size()));
        runCurrentQuery();
    });

    connect(m_worker, &NotmuchWorker::threadMessagesResolved,
            this, &MainWindow::onThreadMessagesResolved);

    m_workerThread.start();

    // Queued behind the thread start, so the completer has real tags as soon
    // as the database can be read. Nothing waits on the answer: requestAllTags
    // stays silent when the database cannot be opened.
    requestAllTags();

    // The Maildir root, which this window cannot derive (item 124). Asked once:
    // it does not change while the application runs. Nothing waits on it
    // either; the reply family is gated on send_command, not on this.
    QMetaObject::invokeMethod(m_worker, "requestMailRoot", Qt::QueuedConnection);
}

void MainWindow::requestAllTags()
{
    // The generation is unused by the tag path, see onAllTagsReady().
    QMetaObject::invokeMethod(m_worker, "requestAllTags", Qt::QueuedConnection,
                              Q_ARG(quint64, 0));
}

void MainWindow::onAllTagsReady(const QStringList &tags)
{
    // The signal carries a generation, this slot deliberately does not take
    // it. A tag list is not an ordered query result: a later one is always at
    // least as good as an earlier one, and there is no partial state a stale
    // arrival could corrupt. Discarding on generation would only be able to
    // throw away a good list.
    m_knownTags = tags;
    m_queryCompleter->setTags(tags);
}

void MainWindow::onMailRootReady(const QString &mailRoot)
{
    m_mailRoot = mailRoot;

    // The enablement pass reads m_mailRoot to resolve which account owns the
    // displayed message, so it answers "no account" until this arrives. A
    // window that had already selected a row would otherwise keep the reply
    // family greyed out until the next selection change.
    updateComposeActions();
}

QList<MainWindow::PlaceholderLine> MainWindow::placeholderLines() const
{
    // One list of (query, label-maker) pairs rather than two arrays indexed in
    // parallel. The parallel version is what the fixed array was, and its
    // hazard is that inserting an entry in one and not the other prints a real
    // number against the wrong name, which reads as a plausible pane.
    //
    // The queries are wire format and deliberately untranslated: `tag:` is
    // notmuch syntax, not user-facing prose. Only the labels are translated.
    QList<PlaceholderLine> lines = {
        { QStringLiteral("tag:unread"),
          [this](int n) { return tr("%n unread", "", n); } },
        { QStringLiteral("tag:flagged"),
          [this](int n) { return tr("%n flagged", "", n); } },
        { QStringLiteral("tag:inbox"),
          [this](int n) { return tr("%n in inbox", "", n); } },
    };

    // Sent and drafts are composed from the account folders, not from a tag.
    // `tag:draft` counts 0 against a real database and no draft-ish tag exists
    // in it, so a tag-based line would be a permanent zero.
    //
    // Omitted entirely when no account configures the folder, rather than
    // shown as 0: item 63 established that a missing sent folder is a real
    // configuration, and "0 sent" claims the user has sent nothing.
    const QString sent = m_config.allSentQuery();
    if (!sent.isEmpty()) {
        lines.append({ sent, [this](int n) { return tr("%n sent", "", n); } });
    }

    const QString drafts = m_config.allDraftsQuery();
    if (!drafts.isEmpty()) {
        lines.append({ drafts,
                       [this](int n) { return tr("%n draft(s)", "", n); } });
    }

    return lines;
}

QStringList MainWindow::placeholderQueries() const
{
    QStringList queries;
    for (const PlaceholderLine &line : placeholderLines())
        queries.append(line.query);
    return queries;
}

QList<HtmlBuilder::PlaceholderHelper> MainWindow::placeholderHelpers() const
{
    QList<HtmlBuilder::PlaceholderHelper> helpers;

    const QList<PlaceholderLine> lines = placeholderLines();

    // Empty until the first reply lands. Rendering zeroes meanwhile would be
    // worse than rendering nothing: a zero is a claim.
    //
    // The size check is also what keeps the pairing honest across a config
    // that changed shape between the request and the reply: counts that do not
    // match the current line list are not this list's answers.
    if (m_placeholderCounts.size() == lines.size()) {
        for (int i = 0; i < lines.size(); ++i) {
            // A query notmuch could not count yields -1; skip that line rather
            // than print a negative number at the user.
            if (m_placeholderCounts.at(i) < 0)
                continue;
            helpers.append({ lines.at(i).label(m_placeholderCounts.at(i)),
                             lines.at(i).query });
        }
    }

    // The sync line, and only when something needs attention: a line that is
    // always there becomes wallpaper and stops being read.
    if (m_lastSyncFailed) {
        helpers.append({ tr("last sync failed"), QString() });
    } else if (const int pending = pendingEditCount(); pending > 0) {
        helpers.append({ tr("%n change(s) waiting to sync", "", pending),
                         QString() });
    }

    return helpers;
}

void MainWindow::showPlaceholderPane()
{
    m_messageView->showPlaceholder(placeholderHelpers());

    // Every route that blanks the pane comes through here, which is why the
    // bar is refilled here rather than at each of them: item 150 was the same
    // defect one level down and was fixed by finding the one shared site.
    // The ids this reads are cleared by the callers around the same point;
    // the order between the two was measured and no test can tell it apart,
    // since a query invalidates currentIndex() anyway. Left as it was found.
    //
    // updateComposeActions() rather than populateMessageBar() alone, because
    // the ENABLEMENT was stale here too and had been since before the bar
    // existed: it ran only from the two selection handlers, so a query that
    // blanked the pane left Reply and Forward enabled over nothing. Invisible
    // while they lived on the main toolbar among other always-on actions, and
    // plain once they sat over an empty pane. It calls populateMessageBar()
    // last, so the bar still gets refilled.
    updateComposeActions();

    QMetaObject::invokeMethod(m_worker, "requestCounts", Qt::QueuedConnection,
                              Q_ARG(QStringList, placeholderQueries()),
                              Q_ARG(quint64, ++m_countsGeneration));
}

void MainWindow::onCountsReady(const QVector<int> &counts, quint64 generation)
{
    // A reply for a superseded request carries counts taken before whatever
    // prompted the newer one, so accepting it would repaint the pane with
    // older numbers than it already has.
    if (generation != m_countsGeneration)
        return;

    m_placeholderCounts = counts;

    // Only repaint what is actually on screen. Without this, a reply arriving
    // after the user opened a thread would replace the message with the logo.
    if (m_messageView->showingPlaceholder())
        m_messageView->showPlaceholder(placeholderHelpers());
}

QString MainWindow::queryTextForTesting() const
{
    return m_queryEdit->text();
}

QString MainWindow::selectedAccountForTesting() const
{
    return m_accountBox->currentData().toString();
}

void MainWindow::selectAccountForTesting(const QString &key)
{
    const int index = m_accountBox->findData(key);
    if (index >= 0)
        m_accountBox->setCurrentIndex(index);
}

void MainWindow::loadBusinessSenders(const QString &path)
{
    m_businessSenders = BusinessSenders::load(
        path.isEmpty() ? BusinessSenders::defaultPath() : path);
    m_cardDelegate->setBusinessSenders(m_businessSenders);
}

void MainWindow::applyCurrentAccountToDelegate()
{
    const QString key = m_accountBox->currentData().toString();
    if (key.isEmpty()) {
        m_cardDelegate->setAccountAddress(QString());
        m_cardDelegate->setAccountLabel(QString());
        return;
    }
    const Account account = m_config.account(key);
    m_cardDelegate->setAccountAddress(account.address);
    m_cardDelegate->setAccountLabel(
        account.name.isEmpty() ? account.key : account.name);
}

void MainWindow::onRulePreviewRequested(const QString &query)
{
    // Unscoped, deliberately. runQuery() wraps the bar's text in the selected
    // account's scope, and a rule query usually names its own path already
    // (path:"work/**" is what every account rule looks like), so previewing
    // one with an account selected would scope it twice and match nothing.
    // That reads as "this rule collects no mail", which is the opposite of
    // what the preview is for.
    m_accountBox->setCurrentIndex(0);

    // Through the query bar, like onPlaceholderQueryRequested: the bar then
    // shows what is on screen and the user can edit the rule's query there
    // before deciding to change the rule itself.
    m_queryEdit->setText(query);
    runCurrentQuery();

    // The dialog is a separate window and may be covering this one or sitting
    // beside it. Raising makes the result visible either way, and the dialog
    // stays open so the two can be compared.
    raise();
    activateWindow();
}

void MainWindow::onPlaceholderQueryRequested(const QString &query)
{
    // Through the query bar rather than straight to the worker, so the bar
    // shows what is being displayed and the user can edit it from there.
    m_queryEdit->setText(query);
    runCurrentQuery();
}

void MainWindow::runSearchFromPane(const QString &query,
                                   SearchTerm::SearchMode mode)
{
    if (query.isEmpty())
        return;

    QString next;
    switch (mode) {
    case SearchTerm::SearchMode::Replace:
        next = query;
        break;
    case SearchTerm::SearchMode::Narrow:
        next = SearchTerm::extend(m_queryEdit->text(), query);
        break;
    case SearchTerm::SearchMode::Exclude:
        next = SearchTerm::exclude(m_queryEdit->text(), query);
        break;
    }

    // exclude() returns empty when there is nothing to exclude from, which the
    // greyed menu entry should already have prevented. Running it would clear
    // the query bar and show the whole Maildir, so refuse instead.
    if (next.isEmpty())
        return;

    // Through the query bar and the existing runner, so the account scope, the
    // generation counter and the flat-mode reset all behave exactly as they do
    // for a typed query. Nothing here builds a second query path.
    m_queryEdit->setText(next);
    runCurrentQuery();
}

void MainWindow::applyWarnings()
{
    const QStringList warnings = m_config.warnings() + m_keyMap.warnings();
    if (warnings.isEmpty())
        return;

    // Non-fatal: the app runs degraded rather than refusing to start.
    m_statusLabel->setText(
        tr("%n configuration warning(s)", "", warnings.size()));
}

QStringList MainWindow::configProblems() const
{
    // Interrupt startup only for things that are actually wrong. Every KeyMap
    // warning qualifies (each one means a binding the user wrote is being
    // ignored), but a Config notice such as "no sync command configured" does
    // not: nothing is broken, the feature is simply off, and a modal on every
    // launch teaches the user to dismiss dialogs unread.
    return m_config.problems() + m_keyMap.warnings();
}

void MainWindow::buildSavedQueryRow(QWidget *parent, QVBoxLayout *layout)
{
    auto *row = new QWidget(parent);
    row->setObjectName(QStringLiteral("savedQueryRow"));
    auto *box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);

    // Cleared first: the row is rebuilt wholesale on every saved-query edit, so
    // the buttons this hash points at are deleted and re-created. Keeping the
    // old entries would leave dangling pointers that findChild() cannot save us
    // from, since nothing looks them up by name.
    m_filterButtons.clear();

    // The built-in filters are the whole row: they are shipped, they are not
    // in queries.json, and the user cannot edit or delete them (item 93).
    // Item 94 removed the transitional half, so a saved query is never a
    // button and the menu is its only home.
    for (const SavedQuery &filter : Config::builtinFilters()) {
        // Sent with no account configuring a sent folder finds nothing by
        // construction. Hidden rather than present and empty, which is what the
        // hardcoded Sent button did and is worth keeping: a control that always
        // returns nothing reads as broken rather than as absent.
        if (m_config.resolvedQuery(filter, QString())
            == Config::matchNothingQuery())
            continue;

        // A QToolButton, like the Save button at the other end of the row, so
        // the two shipped controls carry icons the same way. The user's own
        // queries stay plain QPushButtons: they have no icon to carry and
        // nothing to say about which is which.
        auto *button = new QToolButton(row);
        button->setText(filter.name);
        // A stable object name per filter, so a test finds the button without
        // depending on the label, which is translated.
        button->setObjectName(filter.generated + QStringLiteral("Button"));

        // Theme icons, not the shipped SVGs in Marks: item 70's split is that
        // the panes are ours and the chrome is the system's, and the query row
        // is chrome. A name the running theme lacks degrades to text on its
        // own, which is why nothing here checks whether it resolved.
        //
        // A STAR for Important, not mail-mark-important, which the `flag`
        // action uses. Item 57 recorded the user asking for a star when the
        // action was renamed, and on the query row the icon is read as a
        // category rather than as "do this to the selection", so the two can
        // differ. Chosen by the user on sight, 2026-08-15.
        //
        // mail-folder-sent, not mail-sent: the former is the folder shape every
        // theme ships, the latter is the envelope-in-flight some do not.
        static const QHash<QString, QString> filterIcons = {
            { QStringLiteral("unread"),  QStringLiteral("mail-mark-unread") },
            { QStringLiteral("inbox"),   QStringLiteral("mail-inbox") },
            { QStringLiteral("flagged"), QStringLiteral("starred") },
            { QStringLiteral("sent"),    QStringLiteral("mail-folder-sent") },
            { QStringLiteral("drafts"),  QStringLiteral("document-edit") },
            { QStringLiteral("trash"),   QStringLiteral("user-trash") },
        };
        button->setIcon(
            QIcon::fromTheme(filterIcons.value(filter.generated)));

        // Icon AND text, for the reason the Save button records: this row is a
        // row of text buttons, so an icon on its own reads as a different kind
        // of control than it is.
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

        // Checkable so the style draws its own "this is the current view" look,
        // which is why no colour is chosen here: a hand-picked highlight would
        // have to be picked twice, once per theme, and would still be wrong
        // under a third.
        //
        // Not auto-exclusive and never toggled by the click itself. The check
        // state is derived from the query bar in updateFilterButtons(), so a
        // button that runs a filter and then has its query edited away does not
        // stay lit. Letting the click set it would make the highlight a record
        // of what was pressed rather than of what is shown.
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);

        connect(button, &QToolButton::clicked, this,
                [this, filter]() { runFilter(filter); });
        m_filterButtons.insert(filter.generated, button);
        box->addWidget(button);
    }

    // The user's own saved queries, every one of them in the menu (item 94).
    // The row is built-in filters only, so nothing has to decide which saved
    // queries get button real estate.
    QList<SavedQuery> savedQueries;
    for (const SavedQuery &saved : m_config.savedQueries()) {
        // A generator whose accounts configure nothing produces an entry that
        // always finds nothing. Skipped entirely, which is what the hardcoded
        // Sent button did and is worth keeping.
        if (saved.isGenerated() && m_config.resolvedQuery(saved).isEmpty())
            continue;

        savedQueries.append(saved);
    }

    // Everything above is left-aligned; the stretch here pushes what follows
    // to the right edge. The buttons are the row's content and read as a set,
    // while the overflow menu is a control over that set, so it sits apart
    // from them rather than trailing the last one.
    const int contentCount = box->count();
    box->addStretch(1);

    // The overflow menu, and only when something is in it: an empty menu
    // button is a control that always does nothing.
    if (!savedQueries.isEmpty()) {
        auto *menuButton = new QPushButton(tr("More queries"), row);
        menuButton->setObjectName(QStringLiteral("savedQueryMenuButton"));
        auto *menu = new QMenu(menuButton);
        for (const SavedQuery &saved : savedQueries) {
            QAction *action = menu->addAction(saved.name);

            // A menu entry has no context menu of its own, so its own submenu
            // carries the same actions; a saved query would otherwise be the
            // one thing that cannot be edited or deleted.
            auto *entryMenu = new QMenu(menu);

            // Running the query is an item INSIDE that submenu, and must be:
            // Qt does not emit triggered for an action that owns a menu, so a
            // connection on `action` itself never fires and clicking the entry
            // only opens the submenu. That shipped, and went unnoticed while
            // the menu was the rarely-used half and the user's queries were
            // pinned buttons. Item 93 moved every query into the menu, and item
            // 94 made it their only home, so this is now the ONLY way to run
            // one.
            auto *run = new QAction(tr("Run"), entryMenu);
            run->setObjectName(QStringLiteral("runQuery"));
            connect(run, &QAction::triggered, this,
                    [this, saved]() { runSavedQuery(saved); });
            entryMenu->addAction(run);

            auto *runSeparator = new QAction(entryMenu);
            runSeparator->setSeparator(true);
            entryMenu->addAction(runSeparator);

            addSavedQueryActions(entryMenu, saved);
            action->setMenu(entryMenu);
        }
        menuButton->setMenu(menu);
        box->addWidget(menuButton);
    }

    layout->addWidget(row);

    // Nothing on either side of the stretch leaves an empty strip of padding,
    // so the row goes away rather than sitting there as a gap. Counted before
    // the stretch was added, since the stretch is always there: a saved query
    // with no built-in filters shown still needs the row for its menu.
    if (contentCount == 0 && savedQueries.isEmpty())
        row->hide();

    // Connected HERE rather than beside the query bar's other handlers, which
    // run in registerActions() before this row exists. Both connections are
    // owned by `row`, so a rebuild disconnects them with the widgets they
    // update and cannot leave a second copy behind firing at deleted buttons.
    //
    // textChanged rather than editingFinished: the highlight has to clear while
    // the user types, not once they leave the field.
    connect(m_queryEdit, &QLineEdit::textChanged, row,
            [this]() { updateFilterButtons(); });
    // The account is the other half of a filter's resolved query, so switching
    // account re-resolves it and the highlight has to be recomputed against the
    // new scope rather than assumed to survive.
    connect(m_accountBox, &QComboBox::currentIndexChanged, row,
            [this]() { updateFilterButtons(); });
    updateFilterButtons();
}

void MainWindow::addSavedQueryActions(QWidget *target, const SavedQuery &saved)
{
    target->setContextMenuPolicy(Qt::ActionsContextMenu);

    auto *edit = new QAction(tr("Edit..."), target);
    edit->setObjectName(QStringLiteral("editQuery"));
    connect(edit, &QAction::triggered, this,
            [this, saved]() { editSavedQuery(saved); });
    target->addAction(edit);

    auto *separator = new QAction(target);
    separator->setSeparator(true);
    target->addAction(separator);

    auto *remove = new QAction(tr("Delete"), target);
    remove->setObjectName(QStringLiteral("deleteQuery"));
    connect(remove, &QAction::triggered, this,
            [this, saved]() { deleteSavedQuery(saved); });
    target->addAction(remove);

    // Stored queries only. A generated entry composes its query from the
    // accounts at run time, so a rule made from one would freeze a snapshot
    // that goes stale the day an account is added, in a file the post-new hook
    // reads unattended.
    if (saved.isGenerated())
        return;

    auto *ruleSeparator = new QAction(target);
    ruleSeparator->setSeparator(true);
    target->addAction(ruleSeparator);

    auto *toRule = new QAction(tr("Create tagging rule..."), target);
    toRule->setObjectName(QStringLiteral("queryToRule"));
    connect(toRule, &QAction::triggered, this, [this, saved]() {
        TagRule seed;
        seed.id = TagRules::sanitiseId(saved.name);
        seed.query = saved.query;
        showTagRulesDialog(seed);
    });
    target->addAction(toRule);
}

void MainWindow::editSavedQuery(const SavedQuery &saved)
{
    SaveQueryDialog dialog(m_config, saved, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    // Matched on the name the dialog OPENED with. Using the returned name would
    // leave the original entry in place and add a second one under the new
    // name, which is a duplicate rather than a rename.
    replaceSavedQuery(saved.name, dialog.savedQuery());
}

void MainWindow::deleteSavedQuery(const SavedQuery &saved)
{
    // One of the few places in this application that confirms. The rule against
    // confirmation dialogs covers tag mutations, which are undoable through the
    // undo stack; this writes user config, is not on that stack, and cannot be
    // taken back.
    if (m_confirmDelete) {
        const auto answer = QMessageBox::question(
            this, tr("Delete saved query"),
            tr("Delete the saved query '%1'?").arg(saved.name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    replaceSavedQuery(saved.name, SavedQuery());
}

void MainWindow::replaceSavedQuery(const QString &originalName,
                                   const SavedQuery &replacement)
{
    QList<SavedQuery> queries = m_config.savedQueries();
    const bool removing = replacement.name.isEmpty();

    for (int i = 0; i < queries.size(); ++i) {
        if (queries.at(i).name.compare(originalName, Qt::CaseInsensitive) != 0)
            continue;

        if (removing) {
            queries.removeAt(i);
        } else {
            // The unknown fields belong to the STORED entry: a field written by
            // a later build survives an edit made here rather than being
            // dropped on the next save.
            SavedQuery merged = replacement;
            merged.unknown = queries.at(i).unknown;
            queries[i] = merged;
        }
        break;
    }

    m_config.setSavedQueries(queries);
    if (!m_config.saveSavedQueries()) {
        QMessageBox::warning(this, tr("Saved queries"),
                             tr("Could not write the saved queries file."));
        return;
    }

    rebuildSavedQueryRow();
    statusBar()->showMessage(
        removing ? tr("Deleted saved query '%1'.").arg(originalName)
                 : tr("Updated saved query '%1'.").arg(replacement.name),
        kStatusMessageMs);
}

void MainWindow::runSavedQuery(const SavedQuery &saved)
{
    // Through the dropdown, never by pre-scoping the text: runQuery() applies
    // the selected account's path itself, so a scope baked in here would be
    // applied twice. An unscoped query CLEARS the selection rather than
    // inheriting whatever was there, which is the defect the rules preview hit.
    const int index = saved.account.isEmpty()
                          ? m_accountBox->findData(QString())
                          : m_accountBox->findData(saved.account);
    if (index >= 0)
        m_accountBox->setCurrentIndex(index);

    // A generated entry has no stored query: the text is composed from the
    // accounts now, so what lands in the bar is what actually ran and the user
    // can see and edit it.
    m_queryEdit->setText(saved.isGenerated() ? m_config.resolvedQuery(saved)
                                             : saved.query);

    // Flat for this query only. runQuery() sets the mode on EVERY run, so the
    // flag cannot outlive the entry that asked for it, including for the same
    // query typed by hand afterwards.
    runQuery(saved.flat ? FlatResult::Yes : FlatResult::No);
}

void MainWindow::runFilter(const SavedQuery &filter)
{
    // The account box is READ and never written. That is the whole difference
    // from runSavedQuery(), and it is item 90's defect: a filter narrows what
    // the user is already looking at, so the dropdown is its input rather than
    // something it resets on the way past.
    const QString accountKey = m_accountBox->currentData().toString();

    // Resolved here, in the account's scope, and put in the bar so what ran is
    // visible and editable. runQuery() is told not to scope it again.
    m_queryEdit->setText(m_config.resolvedQuery(filter, accountKey));

    runQuery(filter.flat ? FlatResult::Yes : FlatResult::No,
             AccountScope::AlreadyScoped);
}

void MainWindow::updateFilterButtons()
{
    const QString current = m_queryEdit->text().trimmed();
    const QString accountKey = m_accountBox->currentData().toString();

    for (auto it = m_filterButtons.constBegin();
         it != m_filterButtons.constEnd(); ++it) {
        const SavedQuery filter = Config::builtinFilter(it.key());
        const QString resolved = m_config.resolvedQuery(filter, accountKey);

        // An unresolvable filter must never match, or every filter would light
        // up on an empty query bar. matchNothingQuery() is a real query string
        // and would compare equal to itself.
        const bool matches = !current.isEmpty()
                             && resolved != Config::matchNothingQuery()
                             && resolved == current;

        // Blocked, because setChecked() on a checkable QToolButton emits
        // toggled() and this runs from the query bar's own textChanged: a
        // handler that ran runFilter() would re-enter the query path on every
        // keystroke. Nothing connects toggled() today, so this is a guard
        // against the obvious next edit rather than a fix for a live bug.
        const QSignalBlocker blocker(it.value());
        it.value()->setChecked(matches);
    }
}

void MainWindow::saveCurrentQuery()
{
    const QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty())
        return;

    SaveQueryDialog dialog(m_config, query,
                           m_accountBox->currentData().toString(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QList<SavedQuery> queries = m_config.savedQueries();
    const SavedQuery saved = dialog.savedQuery();

    // Replacing by name keeps the dialog's overwrite offer honest, and keeps
    // the entry where it already sat rather than moving it to the end.
    bool replaced = false;
    for (SavedQuery &existing : queries) {
        if (existing.name.compare(saved.name, Qt::CaseInsensitive) == 0) {
            // The unknown fields belong to the STORED entry, not to the
            // dialog's fresh value, so a field a later build wrote survives
            // being edited here.
            SavedQuery merged = saved;
            merged.unknown = existing.unknown;
            existing = merged;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        queries.append(saved);

    m_config.setSavedQueries(queries);
    if (!m_config.saveSavedQueries()) {
        QMessageBox::warning(this, tr("Save query"),
                             tr("Could not write the saved queries file."));
        return;
    }

    rebuildSavedQueryRow();
    statusBar()->showMessage(tr("Saved query '%1'.").arg(saved.name),
                             kStatusMessageMs);
}

void MainWindow::rebuildSavedQueryRow()
{
    // The row is rebuilt wholesale rather than patched: a query can be added,
    // deleted or replaced, and each moves a different entry. Deleting and
    // rebuilding is a handful of widgets and cannot get the cases wrong.
    auto *old = findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    if (!old)
        return;

    auto *layout = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
    if (!layout)
        return;

    const int index = layout->indexOf(old);
    layout->removeWidget(old);
    // Reparented out NOW, not merely scheduled for deletion. deleteLater()
    // defers destruction to the event loop, so the old row goes on answering
    // findChild() until it runs, and findChild returns the FIRST match: every
    // lookup after a rebuild found the stale row and reported the state from
    // before the edit. Nothing visible was wrong, which is why this only
    // showed up as three tests failing on a row that had in fact been rebuilt.
    old->setParent(nullptr);
    old->deleteLater();

    buildSavedQueryRow(centralWidget(), layout);

    // buildSavedQueryRow appends; move it back to where the old row sat, or it
    // lands under the thread list.
    if (index >= 0) {
        auto *item = layout->takeAt(layout->count() - 1);
        layout->insertItem(index, item);
    }
}

void MainWindow::runQuery(FlatResult flat, AccountScope scope)
{
    // Set on EVERY run, not only when Yes. This is the line that stops flat
    // mode leaking: any query that is not the Sent button restores the tree,
    // so the flag cannot survive into the next view.
    m_sentView = flat == FlatResult::Yes;
    m_model->setFlatMode(m_sentView);

    QString query = m_queryEdit->text().trimmed();

    // Whether this is the trash view, which suppresses the doomed fill: every
    // row there is deleted, so the crimson says nothing and only costs
    // legibility.
    //
    // Derived from the QUERY rather than from which button was clicked, so a
    // hand-typed or edited trash query gets the same treatment as the button,
    // and set on EVERY run for the reason flat mode is: a flag left standing
    // would paint the next view's genuinely doomed rows plain.
    //
    // Compared against the trash filter resolved in the CURRENT account scope,
    // which is what runFilter() put in the bar. matchNothingQuery() is
    // excluded because it is a real string that compares equal to itself, so
    // an account with no trash folder would otherwise match it.
    {
        const QString trashQuery = m_config.resolvedQuery(
            Config::builtinFilter(QStringLiteral("trash")),
            m_accountBox->currentData().toString());
        m_model->setTrashView(!query.isEmpty()
                              && trashQuery != Config::matchNothingQuery()
                              && query == trashQuery);
    }

    // A built-in filter arrives already resolved in the selected account's
    // scope, because a generator has to be asked for the account's own query
    // rather than have its all-accounts query wrapped. Scoping again here would
    // put path:"work/Sent/**" inside path:"work/**".
    const QString accountKey = m_accountBox->currentData().toString();
    if (scope == AccountScope::Apply && !accountKey.isEmpty())
        query = m_config.account(accountKey).scopedQuery(query);

    if (query.isEmpty())
        return;

    // Kept so an expansion (loadThreadTree) and a refresh can be scoped to the
    // query the visible list was built from, rather than to whatever the bar
    // holds by the time they run.
    m_lastQuery = query;

    // A query the user ran abandons any recovery still in flight. Recovery
    // spans two round-trips, so a query typed in the middle of one would
    // otherwise have its result hijacked: the pending selection finds its
    // thread in a result the user asked for something else from, and the view
    // jumps. recoverStaleThread() sets the target AFTER calling this, so its
    // own query does not clear it.
    m_recoverThreadId.clear();
    m_recoverMessageId.clear();

    ++m_generation;
    m_model->clear();

    // Cleared WITH the pane, not merely alongside it. These three name what the
    // pane is showing, and both selection handlers use them to decide whether a
    // newly selected row is already displayed. Left set across a query they
    // describe a pane that was just blanked, so a result containing that same
    // thread is recognised as "already showing" and never loaded.
    //
    // That is not a corner case, it is the ordinary way an `id:` query is run:
    // the id is copied out of the details dialog of the message being read, so
    // the thread is current at the moment the query replaces the view, and its
    // one card opens onto the placeholder. A query returning any OTHER thread
    // hides it, which is why it took a screenshot to find.
    m_messageView->clear();
    showPlaceholderPane();

    m_currentThreadId.clear();
    m_currentMessageId.clear();
    m_currentMessageThreadId.clear();

    // Undo entries refer to rows that are about to be discarded. The model
    // update they invert would be a no-op against the new result set, leaving
    // undo half-applied: the database would change and the list would not.
    m_undoStack.clear();
    m_pendingChange = {};
    m_pendingThreadIds.clear();

    m_statusLabel->setText(tr("Searching..."));

    // The result set is incomplete from here until queryFinished arrives, so
    // anything claiming to act on the whole view must wait.
    m_queryComplete = false;
    updateViewWideActions();

    const auto sort = m_sortOrder->currentIndex() == 1
                          ? NotmuchWorker::OldestFirst
                          : NotmuchWorker::NewestFirst;
    // Recipients only for the Sent view: the fold reads message FILES, which
    // is tens of seconds over an inbox. See ThreadSummary::recipients.
    QMetaObject::invokeMethod(m_worker, "runQuery", Qt::QueuedConnection,
                              Q_ARG(QString, query),
                              Q_ARG(quint64, m_generation),
                              Q_ARG(NotmuchWorker::SortOrder, sort),
                              Q_ARG(bool, m_sentView));
}

void MainWindow::onThreadsReady(const QVector<ThreadSummary> &threads,
                                quint64 generation)
{
    if (generation != m_generation)
        return;  // Superseded by a newer query.

    // A refresh accumulates instead of appending. Its batches must not reach
    // the model one at a time: reconcile() decides what to REMOVE from what the
    // result does not contain, so applying the first batch alone would delete
    // every row after it, then the next batch would put some back. The list
    // would churn and every expanded thread would collapse.
    if (generation == m_refreshGeneration) {
        m_refreshThreads.append(threads);
        return;
    }

    m_model->appendBatch(threads);

    // Item 74. "Searching..." was set once in runQuery() and cleared only on
    // queryFinished, so it went on claiming the query was running for the whole
    // walk while rows were visibly arriving behind it. Measured cold against a
    // 1.1 GB index: first rows at 642 ms, done at 5714 ms, five seconds of a
    // slow query reading as a frozen one.
    //
    // The count comes from the model rather than from a running total, since
    // that is the number of rows the user can actually see. Nothing about the
    // timing changes; this only stops the bar from lying.
    m_statusLabel->setText(tr("Searching... %n thread(s)", "",
                              m_model->rowCount(QModelIndex())));
}

void MainWindow::onQueryFinished(int total, quint64 generation)
{
    if (generation != m_generation)
        return;

    // The refresh's result is complete only now, so this is where it lands.
    // One reconcile for the whole set, not one per batch.
    if (generation == m_refreshGeneration) {
        m_refreshGeneration = 0;
        m_model->reconcile(m_refreshThreads);
        m_refreshThreads.clear();

        // The count in the status bar describes the current view and has just
        // changed, but a refresh is meant to be silent, so it updates the
        // FALLBACK text without stamping over whatever the bar is showing.
        m_defaultStatus = tr("%n thread(s)", "", total);

        // A refresh leaves the view complete exactly as a query does: every
        // matching row is present, so view-wide actions stay honest.
        m_queryComplete = true;
        updateViewWideActions();

        // The open thread may have stopped matching, which the user has to be
        // told about: the pane keeps rendering it while the list no longer
        // offers it anywhere.
        updateStaleThreadNotice();
        return;
    }
    // The query's own result is what the bar says when nothing more pressing
    // is happening, so a transient message falls back to it rather than to
    // nothing.
    m_defaultStatus = tr("%n thread(s)", "", total);
    m_statusLabel->setText(m_defaultStatus);

    // The model now holds every row the query matched, so "the whole view" is
    // a thing that can honestly be acted on.
    m_queryComplete = true;
    updateViewWideActions();

    // A recovery's own thread:<id> query landing. The rows exist now, so the
    // thread can be expanded; the message inside it is selected once its
    // replies arrive.
    applyPendingRecovery();
}

bool MainWindow::isShowingTrash() const
{
    // Compared against the trash GENERATOR's query, not against the word
    // "trash" or against a tag. The trash view is path-based so that mail
    // trashed by another client shows up in it; deciding this from
    // `tag:deleted` instead would disable Restore on exactly the messages
    // that most need it, which is the case Restore's fallback exists for.
    //
    // Both scopes, because the view composes with the account dropdown like
    // every other filter: one account's trash, or all of them.
    const QString query = m_lastQuery.trimmed();
    if (query.isEmpty())
        return false;

    const QString all = m_config.allTrashQuery().trimmed();
    if (!all.isEmpty() && query == all)
        return true;

    for (const Account &account : m_config.accounts()) {
        const QString trash = account.trashQuery().trimmed();
        if (!trash.isEmpty() && query == trash)
            return true;
    }
    return false;
}

void MainWindow::updateViewWideActions()
{
    // Only meaningful on mail that is actually in a trash folder. An enabled
    // action that does nothing is worse than an absent one, and Restore
    // outside the trash has nothing to restore from.
    if (QAction *action = m_actions.value(QStringLiteral("restore")))
        action->setEnabled(isShowingTrash());

    // Threads arrive in batches of kBatchSize, so before the query reports its
    // total the model holds only what has landed. An action that says "all"
    // must not run against a partial set and silently skip the rest, and a
    // disabled control says so without a dialog.
    if (QAction *action = m_actions.value(QStringLiteral("mark_all_read")))
        action->setEnabled(m_queryComplete && m_model->rowCount() > 0);
}

void MainWindow::markAllRead()
{
    // Every row, not the selection: this is the one action in the window that
    // deliberately ignores what is selected.
    QStringList threadIds;
    const int rows = m_model->rowCount();
    threadIds.reserve(rows);
    for (int row = 0; row < rows; ++row) {
        const ThreadSummary thread = m_model->threadAt(row);
        // Only the threads that would actually change. Sending the rest would
        // inflate the pending-edit count with writes that do nothing, and the
        // quit prompt reads that count.
        if (thread.isUnread())
            threadIds.append(thread.threadId);
    }

    if (threadIds.isEmpty()) {
        showTransientStatus(tr("Nothing unread in this view"));
        return;
    }

    // An automatic mark-read armed for the open thread would fire after this
    // and push a second, redundant command onto the stack.
    m_markReadTimer->stop();
    m_markReadMessageId.clear();

    const QString description = tr("Mark all read");
    sendThreadTagChange(threadIds, {}, { QStringLiteral("unread") },
                        description);

    // ONE command for the batch, exactly as tagSelected does: a user who marks
    // 400 threads read expects a single Ctrl+Z to put them back.
    m_undoStack.push(new ThreadTagCommand(this, threadIds, {},
                                          { QStringLiteral("unread") },
                                          description));

    showTransientStatus(
        tr("%1: %n thread(s)", "", threadIds.size()).arg(description));
}

void MainWindow::showThreadContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_threadView->indexAt(pos);
    if (!index.isValid())
        return;   // Right-click on empty space below the rows.

    // Right-clicking a row that is already part of the selection must leave
    // that selection alone: the actions apply to every selected thread, so
    // collapsing to the clicked row here would silently narrow a deliberate
    // multi-row selection to one. Right-clicking outside it selects that row
    // instead, which is what every other list does.
    if (!m_threadView->selectionModel()->isSelected(index))
        selectRowAt(index);

    m_threadContextMenu->popup(m_threadView->viewport()->mapToGlobal(pos));
}

bool MainWindow::everySelectedRowIsInATrashFolder() const
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return false;

    for (const QModelIndex &index : rows) {
        // The row's own file: a reply row's message, a thread row's displayed
        // message. Same rule as everySelectedRowHasTag(), and for the same
        // reason: a thread row acts on the message its card shows.
        const QString path =
            m_model->isMessageRow(index)
                ? m_model->messageAt(index).filePath
                : m_model->threadFor(index).firstMessagePath;
        if (path.isEmpty())
            return false;

        const Account account = accountForMessagePath(path);
        if (account.maildir.isEmpty() || account.trash.isEmpty())
            return false;

        // Compared as a path segment, never with startsWith(): `trash-old`
        // starts with `trash` and is a different folder. The same trap the
        // attachment-save check records.
        const QString prefix = account.maildir + QLatin1Char('/')
                               + account.trash + QLatin1Char('/');
        // accountForMessagePath() accepts both shapes, so this must too: a
        // thread row's path is database-relative and a reply row's absolute.
        if (!path.contains(prefix))
            return false;
    }
    return true;
}

void MainWindow::refreshTrashActions()
{
    const bool inTrash = everySelectedRowIsInATrashFolder();
    const bool haveSelection =
        !m_threadView->selectionModel()->selectedRows().isEmpty();

    // Delete on mail already in the trash reported success and did nothing:
    // moveMessages() finds the file already in the destination and takes its
    // early-return branch, which counts an unsynced change for a move that
    // never happened (item 168).
    // ...and hidden on a reply row as well (item 177): deleting is a
    // conversation-level act, so a reply offers no Delete at all. The two
    // hides are ORed rather than fought over, which is why this reads a flag
    // refreshScopedActionLabels() sets instead of walking the selection twice.
    if (auto *del = m_actions.value(QStringLiteral("delete"))) {
        del->setVisible((!haveSelection || !inTrash)
                        && !m_replySelectionHidesDelete);
    }

    // The mirror, which shipped beside it: Restore was added unconditionally
    // to both menus and so was offered on mail that was never deleted.
    if (auto *restore = m_actions.value(QStringLiteral("restore"))) {
        restore->setVisible((!haveSelection || inTrash)
                            && !m_replySelectionHidesDelete);
    }
}

void MainWindow::refreshUnreadAction()
{
    // Item 112 hid this entry whenever the selection disagreed, because a
    // union is not a state and no honest label existed. The route out was the
    // "Whole thread" submenu, whose two entries were absolute rather than a
    // toggle. Item 177 deleted that submenu: the ROW decides the scope, so a
    // second set of actions was a second answer to a settled question.
    //
    // With one key left, hiding on disagreement leaves the commonest
    // conversation in the mailbox with no key at all, so the rule is a
    // catch-all instead: ANY unread message reads "Mark thread read" and marks
    // every message read; only a fully read selection reads "Mark thread
    // unread". Mixed is not a special case, it is the ordinary one.
    //
    // That keeps one key sufficient, which is what the hidden case cost. Two
    // presses reach either state from anywhere: mark read collapses the mix to
    // a state, and the second press toggles out of it.
    auto *action = m_actions.value(QStringLiteral("toggle_unread"));
    if (!action)
        return;

    // Mixed as well as Conversations: the write really will take whole threads
    // for the rows that are conversations, so the wider claim is the honest
    // one. Same rule as refreshScopedActionLabels(), which must not disagree.
    const SelectionKind kind = selectionKind();
    const bool namesTheThread = kind == SelectionKind::Conversations
                                || kind == SelectionKind::Mixed;

    // Mixed joins Every rather than hiding: both mean "something here is
    // unread", which is the question the direction actually turns on. The
    // three-valued answer is still what is asked, because Every and Mixed
    // differ for other callers; only this label collapses them.
    const bool anyUnread =
        selectionTagPresence(QStringLiteral("unread")) != TagPresence::None;

    action->setVisible(true);
    if (anyUnread) {
        action->setText(namesTheThread ? tr("Mark thread as &read")
                                       : tr("Mark as &read"));
        action->setStatusTip(
            namesTheThread
                ? tr("Remove the unread tag from every message of the "
                     "selected threads")
                : tr("Remove the unread tag from the selection"));
    } else {
        action->setText(namesTheThread ? tr("Mark thread as &unread")
                                       : tr("Mark as &unread"));
        action->setStatusTip(
            namesTheThread
                ? tr("Add the unread tag to every message of the selected "
                     "threads")
                : tr("Add the unread tag to the selection"));
    }
}

MainWindow::SelectionKind MainWindow::selectionKind() const
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return SelectionKind::Empty;

    int conversations = 0;
    for (const QModelIndex &index : rows) {
        if (m_model->isConversationRow(index))
            ++conversations;
    }
    if (conversations == 0)
        return SelectionKind::Messages;
    return conversations == rows.size() ? SelectionKind::Conversations
                                        : SelectionKind::Mixed;
}

void MainWindow::refreshScopedActionLabels()
{
    // One pass, one selection, both jobs. Splitting the label from the
    // visibility would let the two answer from different reads of the
    // selection, and a hidden action wearing the wrong label is worse than
    // either fault alone.
    const SelectionKind kind = selectionKind();
    const bool conversation = kind == SelectionKind::Conversations;

    // Mixed counts as a conversation for the LABEL, because the write really
    // will take whole threads for the rows that are conversations, and the
    // wider claim is the honest one when the selection holds both.
    const bool namesTheThread = conversation || kind == SelectionKind::Mixed;

    const auto relabel = [this](const QString &name, const QString &text,
                                const QString &tip) {
        if (QAction *action = m_actions.value(name)) {
            action->setText(text);
            action->setStatusTip(tip);
        }
    };

    if (namesTheThread) {
        relabel(QStringLiteral("archive"), tr("&Archive thread"),
                tr("Remove inbox from every message of the selected threads"));
        relabel(QStringLiteral("delete"), tr("&Delete thread"),
                tr("Move every message of the selected threads to the trash"));
        relabel(QStringLiteral("restore"), tr("&Restore thread from trash"),
                tr("Move every message of the selected threads out of the "
                   "trash"));
        relabel(QStringLiteral("spam"), tr("Mark thread as &spam"),
                tr("Add spam and remove inbox on the selected threads"));
        relabel(QStringLiteral("flag"), tr("&Important thread"),
                tr("Mark every message of the selected threads as important"));
    } else {
        relabel(QStringLiteral("archive"), tr("&Archive"),
                tr("Remove the inbox tag"));
        relabel(QStringLiteral("delete"), tr("&Delete"),
                tr("Add or remove the deleted tag"));
        relabel(QStringLiteral("restore"), tr("&Restore from trash"),
                tr("Move the selected messages out of the trash"));
        relabel(QStringLiteral("spam"), tr("Mark &spam"),
                tr("Add spam and remove inbox"));
        relabel(QStringLiteral("flag"), tr("&Important"),
                tr("Add or remove the important tag"));
    }

    // Delete and Archive are ABSENT on a reply, not disabled, at the user's
    // decision: "I don't think I'd want to be able to remove a single reply
    // from a thread". A thread of ONE is still a message row and keeps them,
    // because there deleting the message and deleting the conversation are
    // the same act. So the test is not "is this a message row" but "is this a
    // reply", which only a message row can be.
    bool anyReply = false;
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows) {
        if (m_model->isMessageRow(index)) {
            anyReply = true;
            break;
        }
    }

    // Delete's and Restore's visibility is also refreshTrashActions()' job,
    // which runs after this and would overwrite a hide made here. It is told
    // about the reply case rather than asked to repeat the walk.
    m_replySelectionHidesDelete = anyReply;

    if (QAction *archive = m_actions.value(QStringLiteral("archive")))
        archive->setVisible(!anyReply);

    // The mirror, on a conversation row: it shows no message, so the actions
    // that need one cannot mean what they usually do. Reply survives as the
    // thread's own, relabelled above; the other four go.
    for (const QString &name : { QStringLiteral("reply_all"),
                                 QStringLiteral("reply_no_quote"),
                                 QStringLiteral("forward"),
                                 QStringLiteral("save_message") }) {
        if (QAction *action = m_actions.value(name))
            action->setVisible(!conversation);
    }

    if (QAction *reply = m_actions.value(QStringLiteral("reply"))) {
        if (conversation) {
            reply->setText(tr("Reply to this &thread"));
            reply->setStatusTip(
                tr("Add an answer to the end of this conversation"));
        } else {
            reply->setText(tr("Re&ply"));
            reply->setStatusTip(tr("Reply to the displayed message"));
        }
    }
}

void MainWindow::onSelectionChanged()
{
    // Here rather than in the currentRowChanged handler: that signal is
    // emitted BEFORE the selection model is updated, so a handler reading
    // selectedRows() there sees the PREVIOUS selection and would label the
    // action for the rows the user just left (CLAUDE.md, verified Qt 6.11).
    //
    // Before anything else reads the model: the user has moved off whatever
    // row they were on, so a row held back by syncViewMembership() leaves now.
    flushDeferredEviction();

    refreshUnreadAction();
    refreshScopedActionLabels();
    refreshTrashActions();

    const QModelIndexList rows = m_threadView->selectionModel()->selectedRows();
    const int selected = rows.size();
    if (selected == 1) {
        // One row selected. With two kinds of row this is exactly where the
        // scope became ambiguous: a thread root stands for every message in it,
        // a message row for one, and the keypress looks identical. Naming it
        // here is what this project does instead of a confirmation dialog,
        // which CLAUDE.md rules out for tag mutations.
        const ActionScope scope = m_model->scopeForSelection(rows);

        if (scope.wholeThread) {
            m_selectionMessage =
                tr("1 thread selected (%n message(s))", "", scope.messageCount);
            m_statusLabel->setText(m_selectionMessage);
            m_statusTimer->stop();
            m_transientMessage.clear();
        } else {
            // Reading one message is not a bulk action and gets no count.
            if (m_statusLabel->text() == m_selectionMessage)
                m_statusLabel->clear();
            m_selectionMessage.clear();
        }

        // Collapsing a multi-row selection back to one row has to load that row
        // here, and cannot be left to onThreadSelected: currentRowChanged is
        // emitted BEFORE the selection model is updated (verified against
        // Qt 6.11), so that handler still sees the old count and returns
        // without loading anything.
        //
        // Compared per row kind: a message row is identified by its message id
        // and a thread row by its thread id, which are different questions.
        // threadFor() resolves the thread either way, so the row-number trap
        // (item 88) cannot be re-entered here even if this branch changes.
        const QModelIndex current = m_threadView->currentIndex();
        if (current.isValid()) {
            const bool changed =
                m_model->isMessageRow(current)
                    ? m_model->messageAt(current).messageId != m_currentMessageId
                    : m_model->threadFor(current).threadId != m_currentThreadId;
            if (changed)
                onThreadSelected(current, QModelIndex());
        }

        // Which account owns the displayed message decides whether the reply
        // family is live and whether the ribbon shows, so it is re-answered
        // whenever the displayed message can have changed.
        updateComposeActions();
        return;
    }

    if (selected < 1) {
        // Nothing selected. Clearing unconditionally would wipe whatever the
        // last action reported ("Archive: 3 threads"), which is the more useful
        // message once the selection is gone, so only a count this function
        // wrote is taken back.
        if (m_statusLabel->text() == m_selectionMessage)
            m_statusLabel->clear();
        m_selectionMessage.clear();
        updateComposeActions();
        return;
    }

    // The count is the part that actually teaches multi-select: it acknowledges
    // the selection while it is being built, rather than only after an action
    // has already been applied to it.
    //
    // Reported per row kind rather than as a bare row count, so a mixed
    // selection says what it will really touch instead of calling three replies
    // "3 threads".
    const ActionScope scope = m_model->scopeForSelection(rows);
    if (!scope.threadIds.isEmpty() && scope.messageIds.isEmpty()) {
        m_selectionMessage =
            tr("%n thread(s) selected (%1 messages)", "", scope.threadIds.size())
                .arg(scope.messageCount);
    } else if (scope.threadIds.isEmpty()) {
        m_selectionMessage =
            tr("%n message(s) selected", "", scope.messageIds.size());
    } else {
        m_selectionMessage =
            tr("%n thread(s) and %1 message(s) selected", "",
               scope.threadIds.size()).arg(scope.messageIds.size());
    }
    m_statusLabel->setText(m_selectionMessage);

    // State, not an event: it must persist while the selection does. Cancel any
    // transient message still counting down, or that timer fires and replaces a
    // count that is still true.
    m_statusTimer->stop();
    m_transientMessage.clear();

    // Ctrl+click and selectAll() reach a multi-row selection without moving
    // current, so onThreadSelected never runs and its guard never fires. The
    // pane and the pending timer have to be dealt with here as well.
    m_markReadTimer->stop();
    m_markReadMessageId.clear();
    m_currentThreadId.clear();
    m_currentMessageId.clear();
    m_currentMessageThreadId.clear();
    m_messageView->clear();
    showPlaceholderPane();

    // A multi-row selection displays no message, so there is no account to
    // reply from and no ribbon to show.
    updateComposeActions();
}

void MainWindow::onThreadSelected(const QModelIndex &current,
                                  const QModelIndex &)
{
    if (!current.isValid())
        return;

    // A current index the user did not put there. QTreeView gives itself one
    // when it takes FOCUS with none set (verified against Qt 6.11: inserting
    // rows does not do it, focusing the view does), and it sets current WITHOUT
    // selecting. Before item 35b nothing could reach that state, because a
    // populated list always had a current row; now a refresh can drop mail into
    // a view the user read empty, and coming back to the window from another
    // desktop would open the new message and mark it read two seconds later
    // without them ever having looked at it.
    //
    // Every real route here (a click, an arrow key, selectRowAt) selects the
    // row as well, so requiring a selection separates the user's intent from
    // Qt's housekeeping without weakening any of them.
    if (!m_threadView->selectionModel()->isSelected(current))
        return;

    // The notice belongs to whatever the pane is showing, and it is about to
    // show something else. Retired here rather than only in MessageView::clear()
    // because selecting a row RE-RENDERS the pane instead of blanking it, so
    // the bar would otherwise sit over a message it does not describe. That is
    // the second half of the reported defect: the pane had moved on and the
    // notice had not.
    m_messageView->setStaleThread(QString(), QString());

    // A selection spanning more than one row is aimed at a bulk action, not at
    // reading. current follows the keyboard cursor as the selection extends, so
    // without this every row swept through would be rendered and, worse,
    // queued to be marked read: a selection gesture must not mutate mail.
    //
    // The count read here is deliberately not trusted on its own. This signal
    // is emitted BEFORE the selection model is updated (verified against
    // Qt 6.11), so a Ctrl+click that takes the selection from one row to two
    // arrives here still reporting one. onSelectionChanged() always follows and
    // sees the true count, and it is what finally blanks the pane and cancels
    // the timer; this branch only catches the case where the count is already
    // stale in the other direction.
    //
    // The stop() is not redundant with the guard. Clicking one row arms a timer
    // legitimately and only then does the selection grow, so the timer already
    // running for that first row has to be cancelled here or it fires behind a
    // pane that no longer shows the thread.
    if (m_threadView->selectionModel()->selectedRows().size() > 1) {
        m_markReadTimer->stop();
        m_markReadMessageId.clear();
        m_currentThreadId.clear();
        m_currentMessageId.clear();
        m_currentMessageThreadId.clear();
        m_messageView->clear();
        showPlaceholderPane();
        return;
    }

    // A message row renders that message ALONE, so the kind of row still has
    // to be checked here: this is a different render path, not a different way
    // of naming the same thread.
    if (m_model->isMessageRow(current)) {
        const MessageNode node = m_model->messageAt(current);
        if (node.messageId.isEmpty())
            return;

        // Armed for a reply too, since item 87. It deliberately was not
        // before, because the write was thread-scoped and reading one reply
        // would have marked the whole conversation read. With the write scoped
        // to one message that objection is gone, and leaving it unarmed would
        // make the message the user is actually reading the one kind that
        // never gets marked read.
        m_markReadTimer->stop();
        m_markReadMessageId.clear();

        m_currentThreadId.clear();
        m_currentMessageId = node.messageId;
        // Remembered for the stale notice: the pane shows one message, but the
        // thread it came from is what the refreshed list is checked against.
        m_currentMessageThreadId = node.threadId;
        m_messageView->setTags(node.tags);

        // After m_currentMessageId is set: the handler compares against it to
        // tell "still showing this" from "the selection moved on".
        scheduleMarkRead(node.messageId, node.isUnread());

        QMetaObject::invokeMethod(m_worker, "loadMessage", Qt::QueuedConnection,
                                  Q_ARG(QString, node.messageId),
                                  Q_ARG(quint64, m_generation));
        return;
    }

    const ThreadSummary thread = m_model->threadFor(current);
    m_currentThreadId = thread.threadId;
    m_messageView->setTags(thread.tags);

    // The message the card displays, not the thread. The summary's `unread` is
    // a union over the conversation, so this can arm for a thread whose first
    // message is already read; the write is scoped to that message either way,
    // so the cost is a no-op rather than a wrong write. Narrowing it properly
    // needs per-message state in ThreadSummary, which nothing carries yet.
    scheduleMarkRead(thread.firstMessageId, thread.isUnread());

    // The root card IS the thread's first message, so selecting it renders
    // that message. Never the whole conversation: that path is gone (item 66).
    //
    // The id comes from the query now, so it is known on a fresh row and this
    // does not depend on the thread having been expanded. It used to, which
    // made a first click render the conversation and every later click render
    // one message, from the identical gesture.
    //
    // In the Sent view a row stands for what the USER sent, which is not
    // always the thread's opening message. Handled below rather than here,
    // because the id that matters there comes from the query's match set and
    // not from the thread's shape.
    const QString firstId =
        m_model->data(current, ThreadListModel::MessageIdRole).toString();
    if (firstId.isEmpty()) {
        // No id at all: a thread with no toplevel message is not something
        // notmuch produces, but blanking the pane is the honest answer if it
        // ever happens, rather than rendering something the row does not name.
        m_currentMessageId.clear();
        m_currentMessageThreadId.clear();
        m_messageView->clear();
        return;
    }

    m_currentMessageId = firstId;
    QMetaObject::invokeMethod(m_worker, "loadMessage", Qt::QueuedConnection,
                              Q_ARG(QString, firstId),
                              Q_ARG(quint64, m_generation));
}

void MainWindow::onMessageLoaded(const QVector<MessageRef> &messages,
                                 quint64 generation)
{
    // A compose request comes through this same signal rather than through a
    // worker signal of its own, so it is answered before the render guards
    // below: those exist to protect the PANE, and none of them applies to
    // opening a composer.
    //
    // Matched by MESSAGE ID, not merely by a pending flag. The compose request
    // and the pane share one loadMessage slot and one messageLoaded signal, so
    // a pane load already in flight when the user presses Reply arrives FIRST
    // and carries a different message: consuming it on the flag alone would
    // open a composer on whichever message the pane happened to be loading.
    // A non-matching reply falls through to the pane, which is what it is.
    if (m_pendingCompose.active) {
        const auto it = std::find_if(
            messages.cbegin(), messages.cend(),
            [this](const MessageRef &ref) {
                return ref.messageId == m_pendingCompose.messageId;
            });
        if (it != messages.cend()) {
            const PendingCompose request = m_pendingCompose;
            m_pendingCompose = {};

            // The generation guard still applies: a query that moved on means
            // the row the user asked from is gone.
            if (generation == m_generation)
                openComposerFor(*it, request.kind, request.quote);

            // A compose load carries no pane update: m_currentMessageId is
            // untouched by requestMessageForCompose(), so falling through
            // would repaint the pane with a message it did not select.
            return;
        }

        // No match, and the request is DISARMED rather than left waiting.
        //
        // Leaving it armed was a two-stage defect. The immediate half is that
        // Reply silently does nothing when the message is not in the index,
        // which is item 105's "the key does nothing". The delayed half is
        // worse: the request stays armed with a specific message id, and the
        // pane's own loads are the traffic being matched against, so merely
        // SELECTING that message later would match, open a composer nobody
        // asked for, and return before renderMessages() leaving the pane blank
        // on the row just clicked.
        //
        // Only an EMPTY reply disarms it, and that asymmetry is the point.
        // loadMessage() emits an empty list precisely when the id resolved to
        // nothing, so that reply belongs to this request and says it failed.
        // A NON-empty reply naming other messages is the pane's own load
        // crossing ours, which is the race the id match exists to survive;
        // disarming on it would reintroduce that race from the other side.
        if (messages.isEmpty()) {
            m_pendingCompose = {};
            showTransientStatus(tr("That message is no longer indexed"));
        }
    }

    // A stale generation means the query moved on. A reply landing after the
    // selection grew past one row would paint a message back over a pane that
    // was deliberately blanked: loadMessage crosses to the worker on a queued
    // connection, so the answer arrives after onSelectionChanged() has already
    // run. Without this the pane would only look right once a third row made
    // the count stale-proof.
    if (generation != m_generation || messages.isEmpty())
        return;
    if (m_threadView->selectionModel()->selectedRows().size() > 1)
        return;

    // Nothing is currently meant to be on screen: a reply that lands after the
    // pane was cleared must not repaint it.
    if (m_currentMessageId.isEmpty())
        return;

    // The worker's answer is the authority on what THIS message carries, and
    // the pane shows one message. setTags() at selection time can only have
    // used the thread's union.
    if (messages.size() == 1)
        m_messageView->setTags(messages.first().tags);

    renderMessages(messages);
}

void MainWindow::onThreadExpanded(const QModelIndex &index)
{
    if (!index.isValid() || m_model->isMessageRow(index))
        return;

    const QString threadId =
        m_model->data(index, ThreadListModel::ThreadIdRole).toString();
    if (threadId.isEmpty())
        return;

    QMetaObject::invokeMethod(m_worker, "loadThreadTree", Qt::QueuedConnection,
                              Q_ARG(QString, threadId),
                              Q_ARG(QString, m_lastQuery),
                              Q_ARG(quint64, m_generation));
}

void MainWindow::onThreadTreeLoaded(const QVector<MessageNode> &nodes,
                                    quint64 generation)
{
    // The same generation guard every other worker reply carries: an expansion
    // whose query has since been replaced must not insert rows into the new
    // result, where that thread may not even appear.
    if (generation != m_generation || nodes.isEmpty())
        return;

    // Every node in one reply belongs to one thread, so the first one names it.
    // Read from the node rather than remembered from the request: two
    // expansions can be in flight at once, and pairing them by order would
    // attach one thread's replies to the other.
    m_model->setThreadMessages(nodes.first().threadId, nodes);

    // A stale-thread recovery waits for exactly this: the message it wants to
    // select does not exist as a row until the replies land.
    applyPendingRecovery();
}

void MainWindow::renderMessages(const QVector<MessageRef> &messages)
{
    // Guards live in the caller. This paints what it is given.
    //
    // Still takes a LIST, though every caller now passes exactly one message:
    // MessageView renders a list of items, and collapsing that to a single
    // message is a separate change to a class with its own tests. Item 66
    // removed the whole-conversation render; it did not simplify the pane.
    MimeParser parser;
    QList<ThreadRenderItem> items;
    items.reserve(messages.size());

    for (int i = 0; i < messages.size(); ++i) {
        const MessageRef &ref = messages.at(i);

        ThreadRenderItem item;
        // Item 163. The model's path was captured when the query ran, and
        // mbsync renames an uploaded file to add its `,U=<uid>` infix, so a row
        // loaded before that sync names a file that no longer exists. The pane
        // then reported the message unreadable while nothing was wrong with it.
        // Unchanged when nothing was renamed; empty when the file is genuinely
        // gone, which still reports below.
        const QString path = MaildirName::resolveRenamed(ref.filePath);
        item.message = parser.parse(path);

        if (!item.message.ok) {
            // One unreadable message must not lose the rest of the thread, so
            // it becomes an inline note rather than replacing the whole pane.
            //
            // Named by the path the model HOLDS, not by the resolved one: when
            // resolution failed there is no resolved path, and the stale name
            // is what the user can act on.
            item.message = {};
            item.message.ok = true;
            item.message.from = tr("(unreadable message)");
            item.message.subject = ref.filePath;
            item.message.plainBody =
                tr("This message could not be parsed.\n%1").arg(ref.filePath);
        }

        // Namespace prefix keeps cid: references distinct across the thread.
        item.cidPrefix = cidPrefixForIndex(i);

        // For the header's marks (item 70). From the REF's tags, since the
        // parsed message carries only what was in the file.
        item.flagged = ref.isFlagged();

        // Matched messages open; the rest collapse to a stub. The last message
        // always opens, so a thread never renders as nothing but stubs.
        item.expanded = ref.matched || i == messages.size() - 1;

        items.append(item);
    }

    m_messageView->showThread(items);
}

void MainWindow::revertPendingTagChange()
{
    // Either scope can be in flight: a thread-scoped write names threads, a
    // message-scoped one names messages, and both are now applied
    // optimistically. Checking only the thread ids left a failed message write
    // showing its optimistic state for good, with nothing to correct it until
    // the next query.
    if (m_pendingThreadIds.isEmpty() && m_pendingChange.messageIds.isEmpty())
        return;

    // Put the rows back the way they were. Only the model is touched: the
    // worker never applied the change, so there is nothing to undo there.
    for (const QString &threadId : m_pendingThreadIds) {
        m_model->applyTagChange(threadId, m_pendingChange.removed,
                                m_pendingChange.added);
    }
    for (const QString &messageId : m_pendingChange.messageIds) {
        m_model->applyMessageTagChange(messageId, m_pendingChange.removed,
                                       m_pendingChange.added);
    }

    // The undo entry describes a change that never landed, so it would apply a
    // spurious inverse if the user pressed undo.
    //
    // undo() alone, deliberately. This used to clear() the whole stack
    // afterwards, which threw away every earlier step the user had built up
    // because one later write was rejected: undoing an archive of fifty
    // threads became impossible if the flag after it happened to land during a
    // sync. undo() has already taken the failed command off the redo side of
    // the stack, and the commands under it describe changes that did land.
    if (m_undoStack.canUndo())
        m_undoStack.undo();

    m_pendingChange = {};
    m_pendingThreadIds.clear();
}

void MainWindow::onWorkerError(const QString &message)
{
    // Spec: the UI updates optimistically and reverts if the write fails.
    // Without this the list would keep showing a tag the database never got.
    //
    // A running sync does NOT arrive here. The read-write open blocks on the
    // lock and then succeeds rather than failing (measured; see the comment at
    // the open in notmuchworker.cpp), so anything reaching this point is a real
    // failure that waiting cannot fix. The stall a running sync does cause is
    // avoided by not sending the write at all, in sendThreadTagChange().
    revertPendingTagChange();
    updatePendingIndicator();
    m_statusLabel->setText(message);
}

bool MainWindow::aSyncHoldsTheWriteLock() const
{
    // Both sources, exactly as updateSyncControls() reads them. A local sync
    // holds the same exclusive lock a cron one does, so an edit made during it
    // would block on precisely the same open.
    return m_localSyncBusy || m_externalSyncBusy;
}

void MainWindow::flushHeldEdits()
{
    // Moves first, and they are flushed even when no tag edit is waiting: the
    // early return below used to be the whole guard, so a held move with an
    // empty edit queue would never have been sent at all. That is item 106's
    // data loss with a worse shape, since a dropped move leaves the file where
    // the user asked it not to be.
    if (!m_heldMoves.isEmpty()) {
        const QVector<HeldMove> moves = m_heldMoves;
        m_heldMoves.clear();
        for (const HeldMove &move : moves) {
            sendMove(move.messageIds, move.destFolder, move.add, move.remove,
                     move.description, move.fromUndo);
        }
        updatePendingIndicator();
    }

    if (m_heldEdits.isEmpty())
        return;

    // Taken by value and cleared first: sendThreadTagChange() writes
    // m_pendingThreadIds, and re-entering partway through the queue must not
    // find the same edits still waiting.
    const QVector<HeldEdit> edits = m_heldEdits;
    m_heldEdits.clear();

    // Stamped for the ordering test. See flushGenerationForTesting().
    m_flushGeneration = m_generation;

    for (const HeldEdit &edit : edits) {
        // Take the optimistic update back before sending, because the send
        // applies it again. Both apply functions are idempotent per tag so the
        // rows do not visibly flicker; without this the change is applied
        // twice and a later revert undoes only one of them, leaving a row
        // showing a tag the database never got.
        for (const QString &threadId : edit.threadIds) {
            m_model->applyTagChange(threadId, edit.change.removed,
                                    edit.change.added);
        }
        for (const QString &messageId : edit.change.messageIds) {
            m_model->applyMessageTagChange(messageId, edit.change.removed,
                                           edit.change.added);
        }

        // By SCOPE. A held edit is one or the other, never both: a
        // message-scoped edit carries no thread ids, so sending it through
        // sendThreadTagChange() sent an empty list, which returns immediately.
        // The edit was applied to the row, counted as unsynced and then
        // dropped without ever being written, which is data loss with a
        // pending count claiming the opposite.
        //
        // Escalating it to its thread instead would be worse: Delete on one
        // reply would delete every message in the conversation.
        if (!edit.threadIds.isEmpty()) {
            sendThreadTagChange(edit.threadIds, edit.change.added,
                                edit.change.removed, edit.change.description);
        }
        if (!edit.change.messageIds.isEmpty()) {
            sendMessageTagChange(edit.change.messageIds, edit.change.added,
                                 edit.change.removed, edit.change.description);
        }
    }

    // Held edits stop counting as held; what counts now is whatever
    // onTagsApplied() confirms.
    updatePendingIndicator();

    showTransientStatus(
        tr("%n held change(s) sent now that the sync has finished", "",
           int(edits.size())));
}

void MainWindow::onSyncFinished(bool success, int exitCode)
{
    setSyncBusy(false);

    // The local sync no longer holds the write lock, whatever its outcome, so
    // edits held during it can go now.
    //
    // The count below is safe: applyTagsToThreads is a QUEUED call, so the
    // onTagsApplied() that records these edits arrives after this function has
    // returned, and therefore after the success branch has cleared the map.
    // They are counted, not wiped.
    //
    // These edits reach the index after the sync that would have carried them,
    // so they go to the mail store on the NEXT run. That is the same one-run
    // delay any edit made mid-sync gets, bounded by the cron interval.
    const bool sentHeldEdits = !m_heldEdits.isEmpty();

    // Snapshotted BEFORE the flush, and this ordering is load-bearing.
    // flushHeldEdits() calls sendThreadTagChange(), which inserts into
    // m_editedAccounts SYNCHRONOUSLY, unlike the pending-edit map below which
    // is written on the worker's queued reply and so is safely counted rather
    // than wiped. Clearing the whole set after the flush would therefore
    // discard accounts whose edits this run did not carry, and those edits
    // would sync only when some later edit happened to name the same account.
    const QSet<QString> accountsThisRunCarried = m_editedAccounts;

    flushHeldEdits();

    if (success) {
        // Only a SUCCESSFUL sync clears the count. Clearing on failure would
        // assert the edits had reached the mail store when the sync is exactly
        // what failed to put them there.
        m_pendingTagEdits.clear();

        // Only what this run actually carried, per the snapshot above. An
        // account added by flushHeldEdits() stays, because its edit reaches the
        // index after the sync that would have taken it and goes out on the
        // next run.
        m_editedAccounts.subtract(accountsThisRunCarried);
        m_lastSyncFailed = false;
        updatePendingIndicator();

        showTransientStatus(tr("Sync complete"));

        if (m_syncingForExit) {
            // Edits held during THIS sync were only just sent, on a queued
            // connection, so they have not reached the index yet and this sync
            // certainly did not carry them. Quitting here would discard exactly
            // the work the prompt exists to protect. Tell the user and stay
            // open; the indicator shows what is still outstanding.
            if (sentHeldEdits) {
                m_syncingForExit = false;
                QMessageBox::information(
                    this, tr("Changes still to sync"),
                    tr("Changes you made while the sync was running have only "
                       "now been applied, so that sync did not carry them. "
                       "Sync once more before quitting."));
                return;
            }

            // The work is safely across, so finish the quit the user asked for.
            m_syncingForExit = false;
            m_closeApproved = true;
            close();
            return;
        }

        // refreshCurrentQuery(), NOT runCurrentQuery(). A sync this window
        // started is not a query the user asked to re-run: runCurrentQuery()
        // clears the model, the undo stack and the message pane, so a sync
        // landing while a message was open read the user out of it. The cron
        // path has reconciled instead since item 35, and there was never a
        // reason for the two to differ.
        //
        // Item 71 is what made it matter. A local sync used to happen only
        // when the user clicked Sync, where blanking was at least explicable;
        // the automatic one fires two seconds after a tag edit, which is
        // precisely when the user is still reading the message they tagged.
        // Reconciling keeps the pane, and updateStaleThreadNotice() then offers
        // "Show it anyway" for a thread that has stopped matching the query,
        // which is the reported case: reading in Unread, the thread is marked
        // read, and it no longer belongs to the view it was opened from.
        refreshCurrentQuery();
        // A sync is the usual way new tags enter the database.
        requestAllTags();

        // Propose new business-sender candidates from the mail this sync
        // delivered. Scoped by scanQuery: a week of mail once the file
        // exists, everything on the first run.
        QMetaObject::invokeMethod(
            m_worker, "countSenders", Qt::QueuedConnection,
            Q_ARG(QString,
                  BusinessSenders::scanQuery(BusinessSenders::defaultPath())));
    } else if (exitCode == kSyncSkippedExitCode) {
        // Skipped means the lock was never ours: some other run holds it. If
        // both started inside the same poll interval the monitor will have
        // latched this lock period as local, which would swallow the report
        // when that other run finishes. Hand it back.
        m_localSyncHoldsLock = false;

        // Not a failure: another run holds the lock and is doing the work.
        // The user's cron fires every ten minutes, so a click landing inside
        // one is routine and must not raise an error or the log pane.
        showTransientStatus(tr("A sync is already running (started "
                               "elsewhere); this one was skipped"));

        if (m_syncingForExit) {
            // The other run is syncing, but this application cannot see when
            // it finishes, so it cannot promise the changes are across. Leave
            // the window open and say so rather than quitting on a guess.
            m_syncingForExit = false;
            QMessageBox::information(
                this, tr("Sync already running"),
                tr("Another sync was already in progress, so this one was "
                   "skipped. Your changes are most likely being carried over "
                   "by that run, but this window cannot see it finish, so it "
                   "has been left open."));
        }
    } else {
        // Latched until a sync succeeds, so the placeholder's sync line still
        // says so on the next blank pane rather than only in a status message
        // the user may not have been looking at. A skipped run does not set
        // this: it is a branch of its own above, and a skip means another
        // process is doing the work rather than that the work failed.
        m_lastSyncFailed = true;
        m_statusLabel->setText(tr("Sync failed (exit %1)").arg(exitCode));
        m_syncLogPane->show();

        if (m_syncingForExit) {
            // Do NOT quit: the edits are still unsynced and quitting now would
            // discard the user's choice silently, which is the failure the
            // whole prompt exists to prevent. Leave the window open with the
            // log showing, so they can see what went wrong and decide.
            m_syncingForExit = false;
            QMessageBox::warning(
                this, tr("Sync failed"),
                tr("The sync failed (exit %1), so your changes are still "
                   "unsynced. The window has been left open.").arg(exitCode));
        }
    }
}

void MainWindow::onTagsApplied(const TagChange &change)
{
    m_pendingChange = {};
    m_pendingThreadIds.clear();

    // Recorded here, where a write is CONFIRMED, rather than where one is sent:
    // an optimistic update the worker later rejects must not leave the
    // indicator claiming an edit that never landed.
    //
    // NET state, not a count of writes. An edit and its inverse leave the mail
    // store where it started, so they must leave the indicator at zero: the
    // automatic mark-read followed by Ctrl+U used to read as 2 unsynced
    // changes when nothing was outstanding. What the user needs to know is
    // whether quitting now would strand work.
    //
    // Keyed per (message, tag): removing `unread` and adding `flagged` on one
    // message are two independent changes and must not cancel each other.
    for (const QString &messageId : change.messageIds) {
        for (const QString &tag : change.added)
            recordPendingEdit(messageId, tag, true, change.description);
        for (const QString &tag : change.removed)
            recordPendingEdit(messageId, tag, false, change.description);
    }

    updatePendingIndicator();

    // Item 71. Armed here, where a write is CONFIRMED and the pending count is
    // already up to date, for the same reason recordPendingEdit() is called
    // here: a sync scheduled for a write the worker went on to reject would run
    // for nothing.
    scheduleAutoSync();

    // A tag the user has just created is the one they are most likely to type
    // again, so do not wait for the next sync to offer it. A set membership
    // test, not a query.
    for (const QString &tag : change.added) {
        if (!m_knownTags.contains(tag)) {
            requestAllTags();
            break;
        }
    }
}

void MainWindow::refreshCurrentQuery()
{
    // The null guard is not defensive padding, it is a reachable path found by
    // this item's own test crashing the constructor. SyncMonitor::start() polls
    // SYNCHRONOUSLY (src/syncmonitor.cpp:52), so a machine whose lock file is
    // idle at that moment emits stateChanged(Idle) from inside buildUi(), while
    // m_model and the worker are still null. Nothing to refresh at that point
    // anyway: the startup query has not run.
    if (!m_model || !m_worker)
        return;

    // m_lastQuery, not the text in the query bar: the bar holds whatever the
    // user has typed since, which may be a query they never ran. Refreshing to
    // that would execute a search they did not ask for.
    if (m_lastQuery.isEmpty())
        return;

    // Nothing is cleared. No m_model->clear(), no m_undoStack.clear(), no
    // m_messageView->clear(): that list is exactly what runCurrentQuery()
    // destroys and what makes it unusable on a cron timer.
    m_refreshGeneration = ++m_generation;
    m_refreshThreads.clear();

    const auto sort = m_sortOrder->currentIndex() == 1
                          ? NotmuchWorker::OldestFirst
                          : NotmuchWorker::NewestFirst;
    // The SAME recipients flag the visible view was built with. A refresh that
    // dropped it would quietly replace a Sent view's recipients with empty
    // strings on the first background sync, while the user was reading it.
    QMetaObject::invokeMethod(m_worker, "runQuery", Qt::QueuedConnection,
                              Q_ARG(QString, m_lastQuery),
                              Q_ARG(quint64, m_refreshGeneration),
                              Q_ARG(NotmuchWorker::SortOrder, sort),
                              Q_ARG(bool, m_sentView));
}

void MainWindow::updateStaleThreadNotice()
{
    // Which thread the pane is showing depends on what was selected: a thread
    // row sets m_currentThreadId, a message row clears it and sets
    // m_currentMessageId instead, so the message case has to be resolved back
    // to its thread. Reading only m_currentThreadId would leave a reader who is
    // three replies deep with no notice at all, which is the commonest way to
    // be deep in a thread in the first place.
    // The message id is carried whenever there IS one, whichever row kind put
    // it there. A thread ROOT sets both: the root card is the thread's first
    // message and the pane renders that message alone, so treating the message
    // id as the message-row case only threw it away for the commonest way to
    // open a thread, and recovery then had nothing to reopen.
    QString threadId = m_currentThreadId;
    const QString messageId = m_currentMessageId;
    if (threadId.isEmpty())
        threadId = m_currentMessageThreadId;

    if (threadId.isEmpty()) {
        m_messageView->setStaleThread(QString(), QString());
        return;
    }

    // Present means matching: the model holds exactly the query's result after
    // a reconcile.
    for (int row = 0; row < m_model->rowCount(QModelIndex()); ++row) {
        if (m_model->threadAt(row).threadId == threadId) {
            m_messageView->setStaleThread(QString(), QString());
            return;
        }
    }

    m_messageView->setStaleThread(threadId, messageId);
}

void MainWindow::recoverStaleThread(const QString &threadId,
                                    const QString &messageId)
{
    if (threadId.isEmpty())
        return;

    // thread:<id> lists the WHOLE conversation rather than the single message,
    // which is what the user asked for: eight messages, with the fourth
    // selected, matching what the pane already shows.
    m_queryEdit->setText(QStringLiteral("thread:%1").arg(threadId));
    runCurrentQuery();

    // Set AFTER the query, which clears any pending recovery: this one is the
    // query's own reason for running and must survive it.
    //
    // Remembered across the two queued round-trips this takes: the query has to
    // come back before the thread can be expanded, and the expansion before the
    // message row exists to select.
    m_recoverThreadId = threadId;
    m_recoverMessageId = messageId;
}

void MainWindow::onRowDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    // A draft opens in the COMPOSER, not in a thread view of itself: it is an
    // unfinished message, and looking at one rendered is not what the gesture
    // means (item 153). Checked before the thread route below, which is what
    // every other row does.
    // The CLICKED row, which is not necessarily the current one.
    if (indexIsADraft(index)) {
        editDraftAt(index);
        return;
    }

    // The whole thread in every case, and the double-clicked row's own message
    // in the pane. A reply therefore drills to its THREAD with itself selected,
    // never to itself alone: "double click on a reply in a thread should still
    // load the whole thread expanded in a view by itself, with the reply I
    // clicked on visible in the right pane" (item 91). An id: query on the
    // reply is the obvious reading of "open it by itself" and is the wrong one.
    //
    // Reached through the INDEX rather than through index.row(): a tree numbers
    // rows per parent, so a reply's row indexes its siblings and threadAt() on
    // one answers about an unrelated thread.
    QString threadId;
    QString messageId;
    if (m_model->isMessageRow(index)) {
        const MessageNode node = m_model->messageAt(index);
        threadId = node.threadId;
        messageId = node.messageId;
    } else {
        threadId = m_model->data(index, ThreadListModel::ThreadIdRole).toString();
        // The thread's first message, so the pane opens on it rather than on
        // nothing. Empty is fine and means the same thing to the recovery: land
        // on the root, which IS that message.
        messageId = m_model->data(index, ThreadListModel::MessageIdRole).toString();
    }

    if (threadId.isEmpty())
        return;

    // The first click of the double-click already selected this row and armed
    // the mark-read timer. The user is passing through on their way into the
    // thread, and a gesture that navigates must not mutate mail, so the timer
    // goes the same way it does for a multi-row selection.
    //
    // Not a correction of the single click's behaviour: the thread is about to
    // be opened and its message read, which arms the timer again for the row
    // the recovery selects. What is cancelled is the arming for a row the user
    // is leaving.
    m_markReadTimer->stop();
    m_markReadMessageId.clear();

    // Reuses the stale-thread recovery outright, which already runs thread:<id>,
    // expands the thread when the row arrives, selects the target message once
    // the replies land, and falls back to the root when the message has gone.
    // Every one of item 91's three cases is one of those paths.
    recoverStaleThread(threadId, messageId);
}

void MainWindow::applyPendingRecovery()
{
    if (m_recoverThreadId.isEmpty())
        return;

    for (int row = 0; row < m_model->rowCount(QModelIndex()); ++row) {
        const QModelIndex thread = m_model->index(row, 0, QModelIndex());
        if (m_model->threadAt(row).threadId != m_recoverThreadId)
            continue;

        // Expanded in every case, and FIRST. The user was reading a
        // conversation, so bringing it back collapsed hides the thing they
        // asked to get back to, whether their message was the root or a reply.
        // Expanding is also what asks the worker for the replies, so it has to
        // happen before any attempt to find one.
        m_threadView->expand(thread);

        // The thread's first message IS the root card rather than a child row:
        // setThreadMessages drops depth 0 because the root stands for it, so
        // looking for it among the children finds nothing and the selection
        // would silently land nowhere.
        //
        // selectRowAt(), not setCurrentIndex(): a current index without a
        // selection is what QTreeView sets by itself on focus, and
        // onThreadSelected() deliberately ignores that, so pointing at the row
        // renders nothing and leaves the pane blank.
        if (m_recoverMessageId.isEmpty()
            || m_model->data(thread, ThreadListModel::MessageIdRole).toString()
                   == m_recoverMessageId) {
            selectRowAt(thread);
            m_recoverThreadId.clear();
            m_recoverMessageId.clear();
            return;
        }

        // A reply cannot be selected until the replies exist. The expand above
        // asked for them, and this runs again when they arrive.
        //
        // The thread is selected NOW rather than waiting, because a freshly
        // queried row does not know its own first message either: the root's
        // MessageIdRole is empty until the tree loads
        // (`src/threadlistmodel.cpp`), so the root check above cannot match yet
        // and returning here would leave the user looking at a collapsed thread
        // and a blank pane until the replies happen to arrive. Selecting the
        // thread renders its first message immediately, which is the right
        // answer outright when that is what they were reading, and is refined
        // to the correct reply on the next pass when it is not.
        //
        // The target is deliberately NOT cleared: this pass is provisional.
        if (m_model->rowCount(thread) == 0) {
            selectRowAt(thread);
            return;
        }

        for (int child = 0; child < m_model->rowCount(thread); ++child) {
            const QModelIndex reply = m_model->index(child, 0, thread);
            if (m_model->messageAt(reply).messageId != m_recoverMessageId)
                continue;
            selectRowAt(reply);
            m_recoverThreadId.clear();
            m_recoverMessageId.clear();
            return;
        }

        // The thread came back without the message: it was deleted, or moved
        // between accounts. Land on the thread rather than leaving the user
        // with nothing selected.
        selectRowAt(thread);
        m_recoverThreadId.clear();
        m_recoverMessageId.clear();
        return;
    }
}

void MainWindow::onExternalSyncStateChanged(SyncMonitor::State state)
{
    if (state == SyncMonitor::State::Running) {
        // A sync this window started is already reported by setSyncBusy().
        // Remember that this particular lock period is ours, because the
        // release at the end of it must be ignored too: the process exits, and
        // therefore isRunning() goes false, BEFORE the monitor's next poll sees
        // the lock gone. Testing isRunning() again on that poll would report a
        // local sync as an external one, stamping "background sync completed"
        // over the local run's own result up to two seconds later.
        m_localSyncHoldsLock = (m_sync && m_sync->isRunning());
        if (m_localSyncHoldsLock)
            return;

        m_externalSyncBusy = true;
        updateSyncControls();
        m_statusLabel->setText(tr("Background sync running..."));
        m_announcedExternalSync = true;
        return;
    }

    // The release of a lock this window took. onSyncFinished() has already
    // said what happened, including for a failure, so there is nothing to add.
    if (m_localSyncHoldsLock) {
        m_localSyncHoldsLock = false;
        m_externalSyncBusy = false;
        updateSyncControls();

        // A local sync releases the write lock exactly as a background one
        // does, and an edit made during it is held the same way. Without this
        // the held edits would wait for the NEXT sync to come and go.
        flushHeldEdits();
        return;
    }

    // Cleared for Idle AND for Unknown. Unknown means /proc/locks could not be
    // read, so nothing is observed; leaving the button disabled there would
    // strand it permanently on a platform that cannot see the lock at all.
    m_externalSyncBusy = false;
    updateSyncControls();

    // Refreshes, unconditionally, and says nothing about it.
    //
    // 0.8.0 refused to refresh here because runCurrentQuery() clears the undo
    // stack, the selection and the message pane, which is right for a query the
    // user typed and hostile for one fired by a cron timer. The status bar
    // asked the user to press Enter instead. That made the list quietly stale:
    // new mail indexed by cron never appeared, and an Unread view read to the
    // end stayed empty in front of it.
    //
    // The answer is not to weigh the cost, it is to remove it.
    // refreshCurrentQuery() reconciles the result into the model instead of
    // resetting it, so a surviving thread keeps its row, its expansion and its
    // selection, and the message being read stays on screen. Nothing has to be
    // preserved by declining to run.
    //
    // No status message: a refresh that changes nothing must be invisible, and
    // one that adds mail is announced by the mail appearing. Six "sync
    // completed" messages an hour are noise reporting the expected.
    //
    // Unknown is not refreshed. It means the lock table could not be read, so
    // no sync was observed, and refreshing on it would re-query on every failed
    // poll rather than after a sync.
    // Retire our own running message, and only that one. The refresh below says
    // nothing, which is right for a sync that changed nothing, but "says
    // nothing" must not mean "leaves 'Background sync running...' on screen
    // after it stopped". Anything else in the bar belongs to the user (a
    // selection count, a tag result) and is left alone.
    if (m_announcedExternalSync) {
        m_announcedExternalSync = false;
        m_statusLabel->setText(m_defaultStatus);
    }

    // BEFORE the refresh below, and the order is the whole of a defect. An edit
    // made during a sync is held, because the worker's read-write open blocks
    // on notmuch's exclusive lock. Refreshing first meant reading a database
    // that still carried the old tag and reconciling that into the model, which
    // overwrote the optimistic update; the flush then wrote the tag correctly,
    // leaving the database right and the list wrong with nothing scheduled to
    // re-read it. Reported by hand as a message going back to unread at the end
    // of the sync it was read during.
    //
    // Flushing first also costs nothing when there is nothing held: the
    // function returns immediately on an empty queue.
    //
    // OUTSIDE the Idle branch, deliberately, and this predates the reordering.
    // Unknown clears the busy flag above, so writes resume from here on;
    // leaving the flush inside Idle would let a new edit go straight out while
    // the ones already held sat waiting for an Idle that a broken /proc/locks
    // will never report.
    flushHeldEdits();

    if (state == SyncMonitor::State::Idle) {
        refreshCurrentQuery();

        // Item 54. A cron sync carries the edits to the mail store exactly as a
        // local one does, so the count it cleared has to be cleared here too.
        // Without this the indicator kept reporting work that had already
        // shipped, and the exit prompt asked to sync for it.
        //
        // The outcome comes from the RUN END line the script writes, because
        // the process that ran this sync is gone and its exit status with it.
        // Anything other than a definite OK changes nothing: the local path's
        // rule is that only a SUCCESSFUL sync may clear the count, and Unknown
        // is the absence of evidence rather than evidence of success.
        if (MailSync::lastRunOutcome(m_config.syncLog()) == SyncOutcome::Ok) {
            m_pendingTagEdits.clear();

            // Cleared HERE, before flushHeldEdits() below, and the ordering is
            // load-bearing for the reason spelled out on the local path at
            // onSyncFinished(): the flush calls sendThreadTagChange(), which
            // writes m_editedAccounts SYNCHRONOUSLY. Clearing after the flush
            // would discard accounts whose edits this run did not carry, and
            // those edits would then sync only when some later edit happened to
            // name the same account. Running first, everything in the set at
            // this moment is exactly what the finished sync carried, so the
            // local path's snapshot-and-subtract collapses to a clear.
            m_editedAccounts.clear();
            updatePendingIndicator();
        }
    }

}

void MainWindow::showTransientStatus(const QString &text)
{
    m_transientMessage = text;
    m_statusLabel->setText(text);
    m_statusTimer->start();
}

void MainWindow::feedSyncPhase(const QString &chunk)
{
    // readAll() returns whatever happened to be buffered, which splits mid-line
    // as often as not, so lines are reassembled here rather than in the tracker:
    // a half-line fed to it would match nothing and the phase would stall.
    m_syncLineBuffer += chunk;

    int newline;
    bool changed = false;
    while ((newline = m_syncLineBuffer.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_syncLineBuffer.left(newline);
        m_syncLineBuffer.remove(0, newline + 1);
        if (m_syncPhase.feed(line))
            changed = true;
    }

    // The tail without a newline is deliberately left in the buffer: mbsync can
    // sit on a line for a while, and feeding a partial one would report a phase
    // from half a word.

    if (!changed)
        return;

    // Not showTransientStatus(): a phase is state, not an event, and must not
    // expire out from under a sync that is still running. Writing the label
    // directly also leaves m_transientMessage alone, so the timer will not
    // reclaim a phase it did not arm.
    m_statusLabel->setText(m_syncPhase.statusText());
}

void MainWindow::setSyncBusy(bool busy)
{
    m_localSyncBusy = busy;
    updateSyncControls();

    // The phase tracker is reset in startSync(), before the process launches,
    // not here: this runs after start() and a fast run has already produced
    // output by then. Setting the label is still right, since the tracker has
    // nothing to say until a line it recognises arrives.
    if (busy && m_syncPhase.statusText().isEmpty())
        m_statusLabel->setText(tr("Syncing..."));
}

void MainWindow::updateSyncControls()
{
    // ONE function of both states, deliberately. Two independent assignments,
    // one per sync path, means whichever fires second wins: a background sync
    // ending would re-enable the button in the middle of a local run, and a
    // local run ending would re-enable it while cron still holds the lock.
    const bool busy = m_localSyncBusy || m_externalSyncBusy;

    m_syncProgress->setBusy(busy);

    // Disabled rather than left clickable: MailSync::start() already refuses a
    // second run and the script exits 75 when another holds the lock, but a
    // button that looks live and does nothing is worse than one that shows it
    // is unavailable.
    //
    // Note this reads Running specifically, not "not Idle". Unknown means
    // /proc/locks could not be read and nothing was observed, so the button
    // stays usable: permanently disabling it where the lock cannot be seen is
    // worse than occasionally offering a run that gets skipped.
    // The QAction is the only Sync control now, and setEnabled on it reaches
    // the toolbar button, the menu entry and the shortcut at once. Item 29
    // originally set a separate QPushButton and missed the action entirely, so
    // the toolbar stayed clickable through a background sync.
    if (QAction *action = m_actions.value(QStringLiteral("sync")))
        action->setEnabled(!busy && m_sync && m_sync->isAvailable());
}

void MainWindow::startSync()
{
    // One handler for every route in: the toolbar, the menu, the shortcut and
    // the button. They previously had two, and only the button's cleared the
    // log, showed the pane and disabled the control, so a sync started from the
    // toolbar ran with no visible sign it had.
    if (!m_sync->isAvailable()) {
        showTransientStatus(
            tr("No sync command configured ([sync] command in qtmaildir.conf)"));
        return;
    }

    // Fresh run, fresh output: leaving the previous run's lines in place
    // makes a stale failure look like the current one.
    m_syncLog->clear();

    // BEFORE start(), not after. A short run can deliver its whole output
    // before control returns here, and resetting afterwards would wipe the
    // phase those lines had already produced, leaving a fast sync showing
    // nothing between "Syncing..." and "Sync complete".
    m_syncPhase.reset();
    m_syncLineBuffer.clear();

    if (!m_sync->start(pendingSyncChannels())) {
        showTransientStatus(tr("Sync already running"));
        return;
    }
    setSyncBusy(true);
}

void MainWindow::scheduleAutoSync()
{
    // Negative disables the behaviour entirely, per the config key, and that is
    // the pre-0.16.0 behaviour: edits wait for a manual sync or the user's cron
    // job. Checked before anything else so a disabled delay arms nothing.
    const int delay = m_config.autoSyncDelayMs();
    if (delay < 0)
        return;

    // No sync command means the Sync action is already disabled and startSync()
    // would only put "No sync command configured" in the status bar. Arming a
    // timer to say that on a delay, for something the user did not ask for, is
    // worse than staying quiet.
    if (!m_sync || !m_sync->isAvailable())
        return;

    // Nothing outstanding, nothing to carry. An edit netted against its own
    // inverse leaves the count at zero (item 28), and syncing for it would run
    // mbsync over a mail store that is already where the server left it.
    if (pendingEditCount() == 0)
        return;

    // Restart, not stack. Tagging a multi-row selection confirms one write per
    // thread and "mark all read" confirms one per thread in the view, so an
    // armed-per-edit timer would be exactly the storm of syncs a debounce is
    // for. The last edit of a burst decides when the single sync happens.
    m_autoSyncTimer->start(delay);
}

void MainWindow::runAutoSync()
{
    // The user can have synced by hand, or undone the edit, in the delay. Both
    // leave nothing to carry, and re-checking here rather than trusting the arm
    // is what makes the debounce safe to restart freely.
    if (pendingEditCount() == 0)
        return;

    // Skip rather than queue when a sync is already in flight, which item 71
    // requires: the cron job holds the same lock, and mbsync's own answer to a
    // second run is to fail on it. The edits are not lost by skipping. They stay
    // pending, and the sync already running is very likely to carry them, since
    // they reached the mail store at edit time.
    //
    // m_externalSyncBusy covers the cron job SyncMonitor can see. A lock taken
    // between that poll and now is not visible here, and does not need to be:
    // MailSync::start() fails on a second run and startSync() reports it.
    //
    // Re-armed rather than abandoned. Skipping is right; giving up is not. The
    // running sync is only VERY LIKELY to carry the edit, since an edit made
    // after mbsync has already passed that account's mailbox is not carried by
    // it, and before this the timer had fired, nothing re-armed it, and the
    // count sat non-zero until a manual sync or the next cron run.
    //
    // scheduleAutoSync() re-checks the delay, the sync command and the pending
    // count on the way in, so this cannot arm a sync for nothing. Against a
    // long external sync it re-arms once per debounce interval until the lock
    // clears, which is the user's own interval and a timer, not a sync.
    if (m_externalSyncBusy || (m_sync && m_sync->isRunning())) {
        scheduleAutoSync();
        return;
    }

    startSync();
}

void MainWindow::recordPendingEdit(const QString &messageId, const QString &tag,
                                   bool added, const QString &action)
{
    const QString key = messageId + QLatin1Char('\n') + tag;

    // A tag put back the way it was is not an outstanding change. Erase rather
    // than store the new direction, or the ledger grows without bound over a
    // long session of tagging and untagging.
    const auto existing = m_pendingTagEdits.constFind(key);
    if (existing != m_pendingTagEdits.constEnd()) {
        if (existing->added != added)
            m_pendingTagEdits.erase(m_pendingTagEdits.find(key));
        return;
    }

    m_pendingTagEdits.insert(key, PendingEdit{ added, action });
}

QStringList MainWindow::pendingSyncChannels() const
{
    // Nothing pending means this run is a FETCH, and a fetch must cover every
    // account: narrowing it to wherever the last edit happened to be would
    // quietly stop collecting mail everywhere else. Empty is the signal for
    // that, and MailSync::start() appends nothing.
    if (m_editedAccounts.isEmpty())
        return {};

    QStringList channels;
    for (const Account &account : m_config.accounts()) {
        if (m_editedAccounts.contains(account.key))
            channels.append(account.syncChannel());
    }

    // An account tag with no matching [account.<key>] section yields no
    // channel, and syncing a subset that omits it would leave its edits behind
    // with nothing to say so. Fall back to a full sync, which is correct if
    // wasteful; the alternative is silently stranding an edit.
    if (channels.size() != m_editedAccounts.size())
        return {};

    // Stable order so a run is reproducible and the log reads the same way
    // twice. QSet has no order of its own.
    channels.sort();
    return channels;
}

int MainWindow::pendingEditCount() const
{
    // A held edit has NOT reached the index, so onTagsApplied() never counted
    // it. It still has to count here: this is what the exit prompt reads, and
    // an edit waiting on a lock is precisely the work quitting would lose.
    // Each held edit counts as one whatever its size, since it carries thread
    // ids rather than message ids and cannot be netted against the map.
    //
    // There is no fourth term. A counter for confirmed changes carrying no
    // message ids stood here until item 119 looked for what it held and found
    // nothing: NotmuchWorker::applyTags() is the only emitter of tagsApplied()
    // and returns early on an empty id list, so the change that counter
    // existed for cannot reach this window. Every pending change can name the
    // messages it touches, which is what lets the indicator be opened and
    // listed in full.
    const int held = int(m_heldEdits.size());
    // Held MOVES count for exactly the same reason, and were missed. With no
    // tag edit queued the count was 0, so the indicator stayed hidden and
    // closeEvent()'s `pendingEditCount() > 0` guard never fired: a Delete
    // pressed during a sync was discarded on quit with no prompt at all. That
    // is item 106's data loss, and worse here, because a dropped move leaves
    // the file in the folder the user asked it out of.
    const int heldMoves = int(m_heldMoves.size());
    return m_pendingTagEdits.size() + held + heldMoves;
}

QVector<PendingChange> MainWindow::pendingChangeSnapshot() const
{
    QVector<PendingChange> rows;

    // The netted per-(message, tag) edits. The key is `messageId\ntag`, built
    // by recordPendingEdit(), so the id is everything before the first
    // newline: a TAG may contain almost anything, but a message id cannot
    // contain a newline and neither separator can be confused for the other.
    for (auto it = m_pendingTagEdits.cbegin(); it != m_pendingTagEdits.cend();
         ++it) {
        const QString id = it.key().section(QLatin1Char('\n'), 0, 0);
        rows.append(PendingChange{ id, false, it->action, QString(), -1 });
    }

    // Held THREAD edits, which stay thread-scoped: a CONVERSATION row is what
    // made them, and reporting the messages instead would claim the user acted
    // on each one. One row per thread the edit named, since a single edit can
    // cover a multi-row selection.
    for (const HeldEdit &edit : m_heldEdits) {
        for (const QString &threadId : edit.threadIds) {
            rows.append(PendingChange{ threadId, true, edit.change.description,
                                       QString(), -1 });
        }
    }

    // Held MOVES, which are message-scoped. A move is not a tag change and is
    // queued separately for that reason, but it is the same kind of row here:
    // one message, one action the user took.
    for (const HeldMove &move : m_heldMoves) {
        for (const QString &messageId : move.messageIds)
            rows.append(PendingChange{ messageId, false, move.description,
                                       QString(), -1 });
    }

    // Grouped by id so a message with several outstanding actions appears
    // ONCE with its actions beneath it, which is the layout the user asked
    // for. A stable sort, so the actions under one message keep the order
    // they were made in rather than an arbitrary one; QHash has no order of
    // its own, so without this the list reshuffles between openings.
    std::stable_sort(rows.begin(), rows.end(),
                     [](const PendingChange &a, const PendingChange &b) {
                         return a.id < b.id;
                     });
    return rows;
}

void MainWindow::showPendingChanges()
{
    // The snapshot is taken HERE, at the click, and is what the dialog shows
    // however long it stays open. Nothing refreshes it: the count the user
    // clicked is the list they get.
    m_pendingChangeRequest = pendingChangeSnapshot();

    if (m_pendingChangeRequest.isEmpty() || !m_worker) {
        // Nothing to resolve. Shown anyway rather than silently ignoring the
        // click, since a window saying "nothing is waiting" is an answer and a
        // dead click is not.
        PendingChangesDialog(m_pendingChangeRequest, this).exec();
        m_pendingChangeRequest.clear();
        return;
    }

    QStringList ids;
    QList<bool> areThreads;
    ids.reserve(m_pendingChangeRequest.size());
    areThreads.reserve(m_pendingChangeRequest.size());
    for (const PendingChange &change : m_pendingChangeRequest) {
        ids.append(change.id);
        areThreads.append(change.isThread);
    }

    QMetaObject::invokeMethod(m_worker, "resolvePendingSubjects",
                              Qt::QueuedConnection,
                              Q_ARG(QStringList, ids),
                              Q_ARG(QList<bool>, areThreads));
}

void MainWindow::onPendingSubjectsResolved(const QStringList &subjects,
                                           const QList<int> &messageCounts)
{
    // Positional, so the two must line up. A mismatch means the answer is not
    // this request's, which is not something to render half of.
    if (m_pendingChangeRequest.isEmpty()
        || subjects.size() != m_pendingChangeRequest.size()
        || messageCounts.size() != m_pendingChangeRequest.size()) {
        m_pendingChangeRequest.clear();
        return;
    }

    QVector<PendingChange> changes = m_pendingChangeRequest;
    m_pendingChangeRequest.clear();
    for (int i = 0; i < changes.size(); ++i) {
        changes[i].subject = subjects.at(i);
        changes[i].messageCount = messageCounts.at(i);
    }

    PendingChangesDialog(changes, this).exec();
}

void MainWindow::updatePendingIndicator()
{
    const int pending = pendingEditCount();
    if (pending <= 0) {
        m_pendingLabel->hide();
        return;
    }

    // "Changes" and not "mutations": the unit the user thinks in is the tagging
    // they did, not the writes it became.
    m_pendingLabel->setText(tr("%n unsynced change(s)", "", pending));
    m_pendingLabel->setToolTip(
        tr("Changes made here that a sync has not yet carried to the mail "
           "store. An external notmuch run can clear them without this count "
           "noticing."));
    m_pendingLabel->show();
}

void MainWindow::scheduleMarkRead(const QString &messageId, bool unread)
{
    // Any pending timer belongs to a message that is no longer on screen.
    // Stopping unconditionally is what makes this a restart rather than a
    // stack: arrowing down ten rows must mark only the one still selected
    // when the timer finally fires.
    m_markReadTimer->stop();
    m_markReadMessageId.clear();

    // Negative disables the behaviour entirely, per the config key.
    const int delay = m_config.markReadDelayMs();
    if (delay < 0)
        return;

    // A row the model cannot name a message for. Marking its thread instead
    // would be a silent escalation: the automatic mark-read is about the
    // message on display, never about the conversation around it.
    if (messageId.isEmpty())
        return;

    // Nothing to do for a message that is already read. Checked here rather
    // than in the handler so no timer is even armed, which keeps a read
    // message from arming one that would fire into a no-op write.
    if (!unread)
        return;

    m_markReadMessageId = messageId;

    // Zero means immediately, and a zero-interval timer still fires through
    // the event loop rather than reentering the selection handler.
    m_markReadTimer->start(delay);
}

void MainWindow::markCurrentThreadRead()
{
    if (m_markReadMessageId.isEmpty())
        return;

    // The selection can have moved on between the timer being armed and it
    // firing, and the message can have been marked read by hand in that
    // window. Both mean this timer has nothing left to do.
    //
    // Compared against what the PANE is showing rather than against the
    // selection: those are the same thing for both kinds of row, and the pane
    // is what "the message the user is reading" means.
    const QString showing = m_currentMessageId.isEmpty()
                                ? currentThreadFirstMessageId()
                                : m_currentMessageId;
    if (m_markReadMessageId != showing) {
        m_markReadMessageId.clear();
        return;
    }

    const QStringList messageIds = { m_markReadMessageId };
    m_markReadMessageId.clear();

    // sendMessageTagChange, NOT tagSelected: this deliberately does not go on
    // the undo stack. The user never took this action, so hijacking Ctrl+Z to
    // reverse it would undo something they did not do, and toggle_unread
    // already gives them a direct way to put it back. Decided 2026-08-03.
    //
    // MESSAGE-scoped since item 87. The thread-wide write was coherent while a
    // root card rendered the whole conversation; item 66 made it render one
    // message and left the write alone, so reading one message marked replies
    // read that had never been displayed. maildir.synchronize_flags is on, so
    // that reached the server and nothing here could put it back.
    //
    // It still funnels through the one applyTags path, per CLAUDE.md; what
    // differs is only whether the inverse is pushed, which is a window-level
    // decision above the worker.
    //
    // Flagged as AUTOMATIC for syncViewMembership(): the user did not ask for
    // this write, so the row it changes must not be taken out from under them.
    // A write they DID ask for evicts at once; the distinction is who
    // initiated it, not what it does (item 177).
    m_automaticWrite = true;
    sendMessageTagChange(messageIds, {}, { QStringLiteral("unread") },
                         tr("Mark read"));
    m_automaticWrite = false;
}

QString MainWindow::currentThreadFirstMessageId() const
{
    // The message a selected THREAD row displays. m_currentThreadId is what
    // the pane was opened from, so this resolves through the model rather than
    // through the selection, which can have moved.
    if (m_currentThreadId.isEmpty())
        return {};

    for (int row = 0; row < m_model->rowCount(QModelIndex()); ++row) {
        const ThreadSummary thread = m_model->threadAt(row);
        if (thread.threadId == m_currentThreadId)
            return thread.firstMessageId;
    }
    return {};
}

bool MainWindow::everySelectedRowHasTag(const QString &tag) const
{
    // Kept as the direction question, which only has two answers to give: a
    // mixed selection has to go one way, and this says which. The LABEL asks
    // selectionTagPresence() instead, because a label can say "these disagree"
    // and a direction cannot.
    return selectionTagPresence(tag) == TagPresence::Every;
}

MainWindow::TagPresence MainWindow::selectionTagPresence(
    const QString &tag) const
{
    // What a toggle asks before choosing its direction, for both Delete and
    // Toggle unread.
    //
    // Per ROW, and each row is asked about what it stands for. The same
    // question ThreadListModel::scopeForSelection() answers for the WRITE, and
    // it has to be the same question: a direction taken from one object while
    // the write lands on another is how a toggle goes one-way.
    //
    // A reply row reports the message's tags. Asking a reply's THREAD is the
    // trap both toggles fell into: the write is message-scoped, so it never
    // changes the thread's tags; the thread's answer therefore never moves
    // however many times the key is pressed. On the second press it re-sends a
    // tag the message already has, which is a no-op, and a no-op repaints
    // nothing.
    //
    // One direction for the WHOLE selection, which is the rule Delete
    // established: toggling each row independently would leave one keystroke
    // with the selection in two states, which is worse than either outcome.
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return TagPresence::None;

    int withTag = 0;
    for (const QModelIndex &index : rows) {
        QStringList tags;
        if (m_model->isConversationRow(index)) {
            // The conversation's own union, which is what a conversation-scoped
            // write is about to change. Reading one message here would make the
            // toggle disagree with itself the moment the thread is mixed.
            tags = m_model->threadFor(index).tags;
        } else if (m_model->isMessageRow(index)) {
            tags = m_model->messageAt(index).tags;
        } else {
            // A thread row that is NOT a conversation: a thread of one, whose
            // union IS its message. Asked about that message anyway rather
            // than about the summary, because the two can diverge in one
            // direction that matters, described below.
            //
            // The union used to be read for EVERY thread row, with a comment
            // calling the imprecision bounded because a tag toggle at worst
            // re-applied a tag the message already had. That stopped being
            // bounded when Delete became a MOVE: deleting the root of a
            // three-message thread left the replies undeleted, so the union
            // carried no `deleted`, so a second press read the row as
            // not-deleted and deleted it AGAIN, trash-to-trash, ending with
            // `deleted-from:inbox` and `deleted-from:Trash` at once and no way
            // back. Item 177 removes the case rather than the symptom: a
            // three-message row is a conversation now and is asked about its
            // conversation, above.
            const ThreadSummary summary = m_model->threadFor(index);
            const MessageNode own =
                m_model->messageById(summary.firstMessageId);
            // messageById() and NOT summary.firstMessageTags, which is the
            // value the QUERY delivered and is not refreshed by an optimistic
            // update: applyMessageTagChange() writes the row's node, so after
            // a delete the node reads `deleted, deleted-from:inbox` while the
            // summary still reads `inbox, unread`. Measured, and preferring
            // the summary left this defect exactly as it was.
            tags = own.messageId.isEmpty() ? summary.firstMessageTags
                                           : own.tags;
        }
        if (tags.contains(tag))
            ++withTag;
    }

    if (withTag == 0)
        return TagPresence::None;
    return withTag == rows.size() ? TagPresence::Every : TagPresence::Mixed;
}

ThreadSummary MainWindow::threadForCurrentRowForTesting() const
{
    return m_model->threadFor(m_threadView->currentIndex());
}

QHash<QString, int> MainWindow::selectionTagCounts() const
{
    // How many of the selected rows carry each tag, which is what tells a tag
    // that is on all of them from one that is on some. The dialog's tri-state
    // checkboxes are built from this, so a wrong count offers to remove a tag
    // the selection does not have.
    //
    // threadFor(index), NOT threadAt(index.row()): a reply's row number named
    // an unrelated thread, so selecting one counted the tags of whichever
    // thread sat at that position in the list (item 88).
    QHash<QString, int> counts;
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows) {
        const ThreadSummary thread = m_model->threadFor(index);
        for (const QString &tag : thread.tags)
            counts[tag] += 1;
    }
    return counts;
}

void MainWindow::editTagsOnSelection()
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        showTransientStatus(tr("Select a thread first"));
        return;
    }

    const QHash<QString, int> counts = selectionTagCounts();

    // m_knownTags is the same list the query completer uses, so the dialog
    // offers every tag in the database without a round trip.
    TagDialog dialog(m_knownTags, counts, rows.size(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QStringList add = dialog.tagsToAdd();
    const QStringList remove = dialog.tagsToRemove();
    if (add.isEmpty() && remove.isEmpty())
        return;   // Applied with nothing changed.

    // Straight through tagSelected(), so this inherits undo, the optimistic
    // model update, the one-query multi-row resolution, and the completer
    // refresh for a tag that did not exist before.
    tagSelected(add, remove, tr("Edit tags"));
}

void MainWindow::tagSelected(const QStringList &add, const QStringList &remove,
                             const QString &description)
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

    // Resolved through the model rather than by mapping rows to threads here.
    // A message row's row number indexes its siblings, so the old
    // threadAt(index.row()) mapping silently acted on whichever thread sat at
    // that position in the list.
    //
    // One resolution, per row, from what the row IS (item 177). There is no
    // scope argument any more: a caller that could choose is what let one
    // gesture mean two things and forced a second set of actions to exist.
    const ActionScope scope = m_model->scopeForSelection(rows);
    if (scope.isEmpty())
        return;

    if (!scope.threadIds.isEmpty()) {
        sendThreadTagChange(scope.threadIds, add, remove, description);

        // Pushed for undo. The inverse re-resolves the same threads, so it
        // works whether or not those rows are still selected.
        m_undoStack.push(new ThreadTagCommand(this, scope.threadIds, add,
                                              remove, description));
    }

    if (!scope.messageIds.isEmpty()) {
        sendMessageTagChange(scope.messageIds, add, remove, description);
        m_undoStack.push(new MessageTagCommand(this, scope.messageIds, add,
                                               remove, description));
    }

    // The scope named after the fact, since the selection may well be gone by
    // the time the user reads it. This is what stands in for the confirmation
    // dialog CLAUDE.md rules out: undo is the safety net, and undo is only
    // usable if the user can tell that something larger than they meant has
    // just happened.
    showTransientStatus(
        scope.wholeThread
            ? tr("%1: %n message(s) (whole thread)", "", scope.messageCount)
                  .arg(description)
            : tr("%1: %n message(s)", "", scope.messageCount).arg(description));
}

void MainWindow::sendMessageTagChange(const QStringList &messageIds,
                                      const QStringList &add,
                                      const QStringList &remove,
                                      const QString &description)
{
    if (messageIds.isEmpty())
        return;

    // Optimistically applied to each MESSAGE's own row. applyTagChange is
    // keyed by thread and would repaint the whole card as though every message
    // in it had changed, which for a one-message edit is a lie; that is why
    // this path had no optimistic update at all, and the cost was that Delete
    // and Toggle unread on a reply moved the pending count and changed nothing
    // the user could see. The reply's own row is where the feedback belongs.
    for (const QString &messageId : messageIds)
        m_model->applyMessageTagChange(messageId, add, remove);

    // The strip shows the tags of the message ON DISPLAY, so it has to follow
    // an edit to that message rather than waiting for the next selection. The
    // thread path has carried this since the strip existed; without it here, a
    // message-scoped edit repainted the list row and left the pane's chips
    // describing the message as it was, until the user selected away and back.
    //
    // Keyed on m_currentMessageId, which is set only for a message row, so a
    // write to some other reply cannot repaint the open one with its tags.
    //
    // Read by ID, not from currentIndex(): the two agree today, and a guard
    // that depends on them agreeing would put the WRONG message's tags in the
    // pane on the day they do not. The id is what the pane is actually
    // showing.
    if (!m_currentMessageId.isEmpty()
        && messageIds.contains(m_currentMessageId)) {
        m_messageView->setTags(
            m_model->messageById(m_currentMessageId).tags);
    }

    // A row that no longer belongs in the view LEAVES it, rather than sitting
    // there repainted until the next query. Beside the repaint above and
    // before the sync hold below, since a held edit is applied optimistically
    // too and its row is just as wrong to keep.
    //
    // The threads the touched messages belong to, with no filter on which
    // message the card draws: a row is the conversation, so it is the UNION
    // that decides, and the union is what removeThreadsWithoutTag() reads. A
    // message the model does not hold names no thread, and is what the refresh
    // inside the sync is for.
    //
    // Named only when this write MOVED the union, which is the one case a
    // message edit can. applyMessageTagChange() keeps the summary in step for
    // a thread of one, where the union IS the message, and deliberately leaves
    // a longer thread's summary alone because one message's edit does not
    // describe the conversation. Putting a longer thread up for eviction here
    // would judge it on a union this write never touched: a stale answer, and
    // wrong in both directions. A conversation leaves the view when a
    // THREAD-scoped write empties its union, which is the other call site.
    QStringList touchedThreads;
    bool aRowIsMissing = false;
    for (const QString &messageId : messageIds) {
        const QString threadId = m_model->threadIdForMessage(messageId);
        if (threadId.isEmpty()) {
            aRowIsMissing = true;
            continue;
        }
        if (m_model->threadCountFor(threadId) > 1)
            continue;
        if (!touchedThreads.contains(threadId))
            touchedThreads.append(threadId);
    }
    syncViewMembership(touchedThreads, aRowIsMissing, add, remove);

    // The accounts this touches, resolved through the containing threads: the
    // account is a property of the thread, and the sync needs the channel
    // whether one message moved or seven.
    for (const QString &messageId : messageIds) {
        const QString threadId = m_model->threadIdForMessage(messageId);
        if (threadId.isEmpty())
            continue;
        for (const QString &key : m_model->accountKeysForThread(threadId))
            m_editedAccounts.insert(key);
    }

    // Held during a sync for exactly the reason the thread path is: the
    // worker's read-write open BLOCKS on notmuch's exclusive lock rather than
    // failing, so sending now would freeze the worker for the rest of the run.
    if (aSyncHoldsTheWriteLock()) {
        m_heldEdits.append(HeldEdit{
            {}, TagChange{ messageIds, add, remove, description } });
        m_statusLabel->setText(
            tr("A sync is running; your change will be applied when it "
               "finishes."));
        updatePendingIndicator();
        return;
    }

    m_pendingThreadIds.clear();
    m_pendingChange = TagChange{ messageIds, add, remove, description };

    QMetaObject::invokeMethod(m_worker, "applyTags", Qt::QueuedConnection,
                              Q_ARG(TagChange, m_pendingChange));
}

const QString &MainWindow::kOriginTagPlaceholder()
{
    // Not wrapped in tr(). It is never displayed: onMessagesMoved() replaces
    // it with a real tag before anything reaches the worker, and a translated
    // placeholder would stop matching in the one locale that translated it,
    // which is the trap CLAUDE.md records for startup_query.
    static const QString placeholder =
        QStringLiteral("\x01qtmaildir-origin-placeholder");
    return placeholder;
}

Account MainWindow::accountForMessagePath(const QString &path) const
{
    // From the PATH, not from the thread's account tag. The tag is optional
    // config, so resolving through it would silently disable Delete for an
    // account that never set one; a message's maildir prefix is what makes it
    // belong to an account at all.
    //
    // Longest maildir wins, so nested account maildirs (`mail` and
    // `mail/work`) resolve to the more specific one rather than to whichever
    // happens to be listed first.
    //
    // BOTH path shapes are accepted, and that is not defensive coding. A
    // thread row's path comes from ThreadSummary::firstMessagePath and is
    // database-RELATIVE; a reply row's comes from MessageNode::filePath and is
    // ABSOLUTE, because MimeParser has to open it. Matching only the relative
    // form resolved every reply to no account, so Delete on a reply reported
    // "no trash folder configured" and moved nothing, which is exactly the
    // thread-row/reply-row asymmetry this file has been bitten by before.
    //
    // A `/` is required after the maildir in both cases, so `acctX` cannot
    // match an account whose maildir is `acct`.
    Account best;
    int bestLength = -1;
    for (const Account &account : m_config.accounts()) {
        if (account.maildir.isEmpty())
            continue;
        const QString segment = QLatin1Char('/') + account.maildir
                                + QLatin1Char('/');
        const bool matches =
            path.startsWith(account.maildir + QLatin1Char('/'))
            || path.contains(segment);
        if (!matches)
            continue;
        if (account.maildir.length() > bestLength) {
            best = account;
            bestLength = account.maildir.length();
        }
    }
    return best;
}

void MainWindow::trashSelected()
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

    // Resolved per row, like every other action since item 177. A conversation
    // row deletes its conversation; a thread of one deletes its message. A
    // REPLY row never reaches here at all, because Delete is hidden on one:
    // the user's rule is that a single reply cannot be removed from a thread.
    const ActionScope scope = m_model->scopeForSelection(rows);

    // Both halves are run, because a selection really can hold one of each and
    // dropping either would silently delete less than the user asked for. They
    // travel different routes: an unexpanded conversation's message ids and
    // paths live only in the database, so the thread half is asynchronous.
    if (!scope.threadIds.isEmpty())
        trashThreads(scope.threadIds);

    if (scope.messageIds.isEmpty())
        return;

    QHash<QString, QString> pathById;
    for (const QString &messageId : scope.messageIds)
        pathById.insert(messageId, m_model->messageById(messageId).filePath);

    trashMessages(scope.messageIds, pathById, scope.messageIds.size());
}

void MainWindow::trashMessages(const QStringList &messageIds,
                               const QHash<QString, QString> &pathById,
                               int messageCount,
                               const QStringList &wholeThreadIds)
{
    if (messageIds.isEmpty())
        return;

    // Grouped by destination, because moveMessages() takes one folder per call
    // and a selection can span accounts with different trash folders.
    //
    // Paths are passed IN rather than read from the model, because the thread
    // path arrives with messages the model has never seen: a thread the user
    // never expanded holds no node for its replies, so a lookup there returns
    // nothing and every message resolves to no account.
    QHash<QString, QStringList> byTrash;
    QStringList unconfigured;
    for (const QString &messageId : messageIds) {
        const Account account =
            accountForMessagePath(pathById.value(messageId));
        if (account.trash.isEmpty()) {
            unconfigured.append(messageId);
            continue;
        }
        byTrash[account.maildir + QLatin1Char('/') + account.trash]
            .append(messageId);
    }

    // Task 2 warns at config load; this is the second line of defence, for a
    // user who never fixed it. Reported rather than silently doing nothing,
    // and NOT tagged either: a `deleted` tag on a file still in the inbox is
    // precisely the half-done state this item removes.
    if (!unconfigured.isEmpty()) {
        m_statusLabel->setText(
            tr("%n message(s) could not be deleted: no trash folder is "
               "configured for their account.", "", int(unconfigured.size())));
    }

    if (byTrash.isEmpty())
        return;

    for (auto it = byTrash.cbegin(); it != byTrash.cend(); ++it) {
        // `unread` goes with it (item 168, the user's request). Deleting is a
        // decision about the message, so the unread count must not go on
        // including what the user threw away.
        //
        // In the SAME change rather than as a second write, so one undo
        // returns the folder and the tag together: TagChange::inverted()
        // gives it back only if it travelled with the move.
        //
        // This rewrites the Maildir filename, because
        // maildir.synchronize_flags is true, and so reaches the server on the
        // next mbsync. That is the same mechanism the post-new hook REFUSES
        // to touch, and the difference is who is acting: the hook tags
        // arriving mail unattended, while this is an explicit gesture on a
        // message in front of the user.
        // `inbox` goes with it too. Without that a message deleted FROM the
        // inbox keeps the tag the Inbox filter matches on, so it stays in that
        // view after being thrown away: measured 2026-08-26 on the user's own
        // mail, where it was the only message ever deleted from an inbox and
        // therefore the only one that could show it. Restore does not depend
        // on it surviving, since `deleted-from:` carries the origin.
        sendMove(it.value(), it.key(),
                 { QStringLiteral("deleted"), kOriginTagPlaceholder() },
                 { QStringLiteral("unread"), QStringLiteral("inbox") },
                 tr("Delete"), false, wholeThreadIds);
    }

    showTransientStatus(
        tr("%1: %n message(s)", "", messageCount).arg(tr("Delete")));
}

QString MainWindow::originTagFor(const QString &dbRelativeFolder) const
{
    // `acct/inbox` becomes `deleted-from:inbox`. The tag stores the folder
    // relative to the ACCOUNT, never to the database: the account prefix is
    // recomposed from the message's own path when it is read back, so storing
    // it would duplicate it and would go stale the day the user renames a
    // maildir.
    //
    // Shared by the two sites that need the tag, rather than derived twice.
    // They disagreed once already: onMessagesMoved() resolved a placeholder
    // from the folder the worker reported, which on a RESTORE is the trash
    // rather than the origin, so the restore stripped `deleted-from:Trash`
    // and left the real tag in place.
    const Account account =
        accountForMessagePath(dbRelativeFolder + QLatin1Char('/'));
    QString accountRelative = dbRelativeFolder;
    if (!account.maildir.isEmpty()
        && dbRelativeFolder.startsWith(account.maildir + QLatin1Char('/'))) {
        accountRelative = dbRelativeFolder.mid(account.maildir.length() + 1);
    }
    if (accountRelative.isEmpty())
        return QString();
    return QStringLiteral("deleted-from:%1").arg(accountRelative);
}

void MainWindow::trashThreads(const QStringList &threadIds)
{
    if (threadIds.isEmpty())
        return;

    // Asked of the WORKER rather than resolved here. A thread the user never
    // expanded has no nodes in the model for its replies, so the ids and the
    // paths a move needs exist only in the database. applyTagsToThreads()
    // solves the same problem the same way, for the same reason.
    //
    // Repainted HERE, synchronously, before the worker is asked.
    //
    // The move needs message ids and paths that only the database holds for an
    // unexpanded thread, so the move itself is asynchronous. The DISPLAY must
    // not wait for that round trip: the card is what the user watches, and
    // holding it back is what made a deleted thread sit unchanged until it was
    // clicked. It also keeps the toggle's direction readable immediately, so a
    // second press restores rather than deleting again.
    for (const QString &threadId : threadIds)
        m_model->applyTagChange(threadId, { QStringLiteral("deleted") }, {});

    m_pendingThreadScope = threadIds;
    QMetaObject::invokeMethod(m_worker, "resolveThreadMessages",
                              Qt::QueuedConnection,
                              Q_ARG(QStringList, threadIds),
                              Q_ARG(QString, QStringLiteral("delete_thread")));
}

void MainWindow::onThreadMessagesResolved(const QStringList &messageIds,
                                          const QStringList &paths,
                                          const QStringList &tags,
                                          const QString &requestTag)
{
    if (messageIds.size() != paths.size() || messageIds.size() != tags.size())
        return;

    QHash<QString, QString> pathById;
    for (int i = 0; i < messageIds.size(); ++i)
        pathById.insert(messageIds.at(i), paths.at(i));

    const QStringList threadScope = m_pendingThreadScope;
    m_pendingThreadScope.clear();

    if (requestTag == QStringLiteral("reply_thread")) {
        if (messageIds.isEmpty()) {
            showTransientStatus(tr("That thread holds no message to answer"));
            return;
        }
        // The NEWEST message, which resolveQuery() puts first: In-Reply-To and
        // References then land the answer at the END of the conversation, and
        // the recipients are the ones currently in it rather than whoever
        // started it.
        //
        // ReplyAll and no quoting, at the user's decision. A conversation is
        // multi-party by definition, so answering one participant of it is the
        // unusual case and stays available on an individual message; and "we
        // just add an answer to the thread", so there is nothing to quote.
        requestMessageForCompose(messageIds.first(),
                                 ComposeContext::Kind::ReplyAll, false);
        return;
    }

    if (requestTag == QStringLiteral("empty_trash")) {
        confirmAndPurge(messageIds);
        return;
    }

    if (requestTag == QStringLiteral("delete_thread")) {
        trashMessages(messageIds, pathById, messageIds.size(), threadScope);
        return;
    }

    if (requestTag == QStringLiteral("restore_messages")) {
        restoreResolvedMessages(messageIds, paths, tags);
        return;
    }

    if (requestTag != QStringLiteral("undelete_thread"))
        return;

    // Restore, resolved per message: each one goes back to the folder its own
    // `deleted-from:` tag names, so a thread whose messages were deleted from
    // different folders reassembles correctly rather than collapsing into one.
    const QString prefix = QStringLiteral("deleted-from:");
    QHash<QString, QStringList> byOrigin;
    QStringList unknown;
    for (int i = 0; i < messageIds.size(); ++i) {
        // Split on TAB, matching resolveThreadMessages(). A space is not a
        // safe separator: a folder name containing one produces a tag
        // containing one, and splitting there silently truncates the origin
        // to its first word.
        const QStringList messageTags =
            tags.at(i).split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        QString origin;
        for (const QString &tag : messageTags) {
            if (tag.startsWith(prefix)) {
                origin = tag.mid(prefix.length());
                break;
            }
        }
        // A message with no `deleted` tag is not in the trash and has nothing
        // to come back from. A thread-scoped restore reaches every message,
        // including ones the user never deleted, and moving those would drag
        // untouched mail out of whatever folder it legitimately sits in.
        if (!messageTags.contains(QStringLiteral("deleted")))
            continue;
        const Account account =
            accountForMessagePath(paths.at(i));
        if (origin.isEmpty() || account.maildir.isEmpty()) {
            unknown.append(messageIds.at(i));
            continue;
        }
        byOrigin[account.maildir + QLatin1Char('/') + origin]
            .append(messageIds.at(i));
    }

    if (!unknown.isEmpty()) {
        // No origin recorded: deleted by an older version or tagged by hand.
        // The tag comes off so the row stops claiming to be deleted, but no
        // file moves, since guessing a folder would put the message somewhere
        // the user never had it.
        sendMessageTagChange(unknown, {}, { QStringLiteral("deleted") },
                             tr("Undelete thread"));
        m_undoStack.push(new MessageTagCommand(this, unknown, {},
                                               { QStringLiteral("deleted") },
                                               tr("Undelete thread")));
    }

    for (auto it = byOrigin.cbegin(); it != byOrigin.cend(); ++it) {
        // The origin tag is named here, not left as the placeholder: on a
        // restore the placeholder would resolve to the folder the message is
        // coming FROM, which is the trash, and strip a tag never written.
        const QString origin = originTagFor(it.key());
        QStringList remove{ QStringLiteral("deleted") };
        if (!origin.isEmpty())
            remove.append(origin);
        sendMove(it.value(), it.key(), {}, remove, tr("Undelete thread"),
                 false, threadScope);
    }

    showTransientStatus(tr("%1: %n message(s)", "", messageIds.size())
                            .arg(tr("Undelete thread")));
}

void MainWindow::untrashThreads(const QStringList &threadIds)
{
    if (threadIds.isEmpty())
        return;

    // Repainted synchronously, as the delete direction is.
    for (const QString &threadId : threadIds)
        m_model->applyTagChange(threadId, {}, { QStringLiteral("deleted") });

    m_pendingThreadScope = threadIds;
    QMetaObject::invokeMethod(
        m_worker, "resolveThreadMessages", Qt::QueuedConnection,
        Q_ARG(QStringList, threadIds),
        Q_ARG(QString, QStringLiteral("undelete_thread")));
}

QString MainWindow::inboxFolderFor(const Account &account) const
{
    // Discovered from the account's OWN inbox query, never hardcoded.
    //
    // The casing is not ours to assume: the real Maildir has `Inbox` and a
    // test fixture has `inbox`, and picking either would create a SECOND
    // folder beside the real one on whichever side disagreed. That is exactly
    // the failure a truncated origin folder caused on real mail this morning,
    // and under mbsync's `Create Both` such a folder can reach the server.
    //
    // The inbox query is a generated `path:"<maildir>/<folder>/**"`, so the
    // folder name is the part between the account prefix and the glob.
    const QString query = account.inboxQuery();
    const QString prefix =
        QStringLiteral("path:\"") + account.maildir + QLatin1Char('/');
    const QString suffix = QStringLiteral("/**\"");
    if (query.startsWith(prefix) && query.endsWith(suffix)) {
        const int from = prefix.length();
        const int length = query.length() - from - suffix.length();
        if (length > 0)
            return query.mid(from, length);
    }

    // No inbox configured for this account. `Inbox` is the Maildir
    // convention and is what mbsync's own `Inbox` directive defaults to.
    return QStringLiteral("Inbox");
}

void MainWindow::restoreResolvedMessages(const QStringList &messageIds,
                                         const QStringList &paths,
                                         const QStringList &tags)
{
    if (messageIds.size() != paths.size() || messageIds.size() != tags.size())
        return;

    const QString prefix = QStringLiteral("deleted-from:");
    QHash<QString, QStringList> byOrigin;
    QHash<QString, QStringList> byInbox;
    QStringList stranded;

    for (int i = 0; i < messageIds.size(); ++i) {
        const QStringList messageTags =
            tags.at(i).split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        QString origin;
        for (const QString &tag : messageTags) {
            if (tag.startsWith(prefix)) {
                origin = tag.mid(prefix.length());
                break;
            }
        }

        const Account account = accountForMessagePath(paths.at(i));
        if (account.maildir.isEmpty()) {
            stranded.append(messageIds.at(i));
            continue;
        }

        if (origin.isEmpty()) {
            // Trashed by another client, so there is no record of where it
            // belongs. Inbox is the documented fallback, and it is reported:
            // a guess the user is not told about is worse than the guess.
            byInbox[account.maildir + QLatin1Char('/')
                    + account.inboxFolder()]
                .append(messageIds.at(i));
            continue;
        }
        byOrigin[account.maildir + QLatin1Char('/') + origin]
            .append(messageIds.at(i));
    }

    for (auto it = byOrigin.cbegin(); it != byOrigin.cend(); ++it) {
        // The origin tag is named here rather than left as the placeholder,
        // which onMessagesMoved() would resolve to the folder the message is
        // coming FROM, namely the trash.
        const QString origin = originTagFor(it.key());
        QStringList remove{ QStringLiteral("deleted") };
        if (!origin.isEmpty())
            remove.append(origin);

        // `inbox` comes back when, and only when, the message is going back
        // to an inbox. Delete strips it (so a deleted message leaves the
        // Inbox view), which makes restoring it the other half of that
        // change: without this a restored message sits in the inbox FOLDER
        // carrying no `inbox` TAG, invisible to the view it was returned to
        // until the next hook run. Undo is unaffected either way, since
        // TagChange::inverted() gives back exactly what the move removed.
        //
        // Judged on the DESTINATION folder rather than on the origin tag's
        // text, so an account whose inbox is named something else is right for
        // the same reason inboxFolderFor() exists. The key is
        // `<maildir>/<folder>`, and the account is resolved back from it
        // rather than captured above, where it belongs to the per-message loop
        // and is out of scope here.
        //
        // The destination FOLDER, taken from the key rather than from
        // `origin` above: that is the finished TAG, `deleted-from:Inbox`,
        // which never equals `Inbox` however the account spells it. The
        // comparison was therefore always false and the `inbox` tag never came
        // back, so a restored message sat in the inbox folder invisible to the
        // Inbox view until the next hook run. The comment above says what this
        // does; for one release the code did not do it.
        QStringList add;
        const QString destMaildir = it.key().section(QLatin1Char('/'), 0, 0);
        const QString destFolder = it.key().section(QLatin1Char('/'), 1);
        for (const Account &candidate : m_config.accounts()) {
            if (candidate.maildir != destMaildir)
                continue;
            if (destFolder.compare(candidate.inboxFolder(),
                                   Qt::CaseInsensitive) == 0) {
                add.append(QStringLiteral("inbox"));
            }
            break;
        }

        sendMove(it.value(), it.key(), add, remove, tr("Restore"));
    }

    for (auto it = byInbox.cbegin(); it != byInbox.cend(); ++it) {
        // This branch IS the inbox by construction: it is the fallback for a
        // message with no origin tag, and the folder it names is the
        // account's own inbox. So the tag always comes with it.
        sendMove(it.value(), it.key(), { QStringLiteral("inbox") },
                 { QStringLiteral("deleted") }, tr("Restore"));
    }

    if (!byInbox.isEmpty()) {
        m_statusLabel->setText(
            tr("%n message(s) had no record of where they came from and were "
               "moved to the inbox.", "", int(byInbox.size())));
    }
    if (!stranded.isEmpty()) {
        m_statusLabel->setText(
            tr("%n message(s) could not be restored: they belong to no "
               "configured account.", "", int(stranded.size())));
    }
}

void MainWindow::restoreSelectedFromTrash()
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

    const ActionScope scope = m_model->scopeForSelection(rows);

    // A conversation row restores its whole conversation, by the same rule
    // that makes Delete conversation-scoped there: the two are inverses and
    // must agree about what they act on.
    if (!scope.threadIds.isEmpty())
        untrashThreads(scope.threadIds);

    if (scope.messageIds.isEmpty())
        return;

    // Resolved by the WORKER, not read from the model.
    //
    // The model's tags come from the QUERY, and a row whose delete has not yet
    // been re-queried still carries its pre-delete tags: measured
    // `[inbox,unread]` on a message already in the trash, one run in three.
    // The origin tag is then not found, the message falls into the
    // no-origin branch, and Restore sends it to the INBOX instead of the
    // folder it came from, silently and irreversibly.
    //
    // A restore has to be right about the destination or it is worse than
    // doing nothing, so it asks the database rather than trusting a view that
    // may be a moment behind. untrashThreads() already worked this
    // way; this is the same reasoning applied to the message-scoped path.
    m_pendingRestoreIds = scope.messageIds;
    QMetaObject::invokeMethod(
        m_worker, "resolveMessages", Qt::QueuedConnection,
        Q_ARG(QStringList, scope.messageIds),
        Q_ARG(QString, QStringLiteral("restore_messages")));
}

void MainWindow::purgeForTesting(const QStringList &messageIds)
{
    if (!m_worker || messageIds.isEmpty())
        return;
    QMetaObject::invokeMethod(m_worker, "purgeMessages", Qt::QueuedConnection,
                              Q_ARG(QStringList, messageIds));
}

void MainWindow::emptyTrash()
{
    // Scoped to the account selector, like every other account-aware surface:
    // the All accounts view empties every configured trash, a selected
    // account empties only its own. The user sees which in the dialog.
    const QString accountKey = m_accountBox->currentData().toString();
    const QString query = accountKey.isEmpty()
                              ? m_config.allTrashQuery()
                              : m_config.account(accountKey).trashQuery();

    // An account with no trash folder configured produces an EMPTY query, and
    // an empty notmuch query matches EVERYTHING. Refusing here rather than
    // relying on the worker's own guard, so the message names the cause.
    if (query.isEmpty()) {
        showTransientStatus(tr("No trash folder is configured"));
        return;
    }

    if (!m_worker) {
        showTransientStatus(tr("Not connected to the mail index"));
        return;
    }

    // Enumerated before it is counted, and counted from the DATABASE: the
    // number in the dialog has to be the number destroyed, and the model
    // holds whatever the current view is showing, which is usually not the
    // trash at all.
    QMetaObject::invokeMethod(m_worker, "resolveQueryMessages",
                              Qt::QueuedConnection,
                              Q_ARG(QString, query),
                              Q_ARG(QString, QStringLiteral("empty_trash")));
}

void MainWindow::confirmAndPurge(const QStringList &messageIds)
{
    if (messageIds.isEmpty()) {
        showTransientStatus(tr("The trash is already empty"));
        return;
    }

    const QString accountKey = m_accountBox->currentData().toString();
    const QString where = accountKey.isEmpty()
                              ? tr("every account")
                              : m_accountBox->currentText();

    QMessageBox box(this);
    box.setObjectName(QStringLiteral("emptyTrashConfirmation"));
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Empty trash"));
    box.setText(tr("Permanently delete %n message(s) from the trash of %1?",
                   "", messageIds.size())
                    .arg(where));
    // Said plainly, because it is the only place in this application where it
    // is true.
    box.setInformativeText(tr("This cannot be undone."));
    box.addButton(QMessageBox::Cancel);
    QPushButton *confirm =
        box.addButton(tr("Delete permanently"), QMessageBox::DestructiveRole);
    // Cancel is the default, so Return does not destroy mail.
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() != confirm)
        return;

    QMetaObject::invokeMethod(m_worker, "purgeMessages", Qt::QueuedConnection,
                              Q_ARG(QStringList, messageIds));
}

void MainWindow::showStrandedDeletedMail()
{
    // Not scoped to the selected account, deliberately. The stranded mail is
    // an artefact of an old version rather than a view of anything, and the
    // user wants to see all of it at once; the account dropdown is still there
    // to narrow it by hand afterwards.
    const QString trash = m_config.allTrashQuery();

    // No account configures a trash folder: everything tagged `deleted` is by
    // definition stranded, since there is nowhere for it to have gone. An
    // empty exclusion must never be written as `not ()`, which notmuch parses
    // without complaint and matches nothing, reporting a clean database.
    const QString query =
        trash.isEmpty()
            ? QStringLiteral("tag:deleted")
            : QStringLiteral("tag:deleted and not (%1)").arg(trash);

    // Into the bar, like a filter: what ran is visible and editable, and
    // AlreadyScoped stops runQuery() wrapping it in the selected account's
    // path, which would hide every other account's stranded mail.
    m_queryEdit->setText(query);
    runQuery(FlatResult::No, AccountScope::AlreadyScoped);

    // After runQuery(), which sets "Searching...": set before it, this would
    // be overwritten and the user would be told nothing about what they are
    // looking at.
    m_statusLabel->setText(tr("Mail tagged deleted but not in a trash folder. "
                              "Select what should go and press Delete."));
}

void MainWindow::restoreSelected(bool fallbackToInbox)
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

    const ActionScope scope = m_model->scopeForSelection(rows);

    // The conversation half, for the same reason trashSelected() has one: this
    // is the undelete direction of the same toggle.
    if (!scope.threadIds.isEmpty())
        untrashThreads(scope.threadIds);

    if (scope.messageIds.isEmpty())
        return;

    // Where each message came from, read back off its own tag. This is what
    // the tag exists for: the file has moved, so nothing on disk and nothing
    // in notmuch still records the original folder.
    const QString prefix = QStringLiteral("deleted-from:");
    QHash<QString, QStringList> byOrigin;
    QStringList unknown;
    for (const QString &messageId : scope.messageIds) {
        const MessageNode node = m_model->messageById(messageId);
        QString origin;
        for (const QString &tag : node.tags) {
            if (tag.startsWith(prefix)) {
                origin = tag.mid(prefix.length());
                break;
            }
        }
        // An account prefix is needed to name a folder to the worker, which
        // works in database-relative paths. The origin tag stores the folder
        // relative to the ACCOUNT, so the two are recomposed here.
        const Account account = accountForMessagePath(node.filePath);
        if (origin.isEmpty() || account.maildir.isEmpty()) {
            unknown.append(messageId);
            continue;
        }
        byOrigin[account.maildir + QLatin1Char('/') + origin].append(messageId);
    }

    if (!unknown.isEmpty()) {
        // No origin recorded. Two quite different situations reach here and
        // they want opposite things, which is what `fallbackToInbox` selects.
        //
        // From the TRASH VIEW the message is demonstrably in the trash, put
        // there by another client, and refusing to move it leaves the user
        // looking at a message they cannot get out. Inbox is the documented
        // fallback, and it is reported, because a guess the user is not told
        // about is worse than the guess itself.
        //
        // From a second press of Delete the message is NOT in the trash: it is
        // sitting wherever it always was, wearing a stale `deleted` tag from
        // an older version or from a hand-written notmuch command. Moving it
        // to the inbox there would relocate mail the user never asked to move.
        // The tag comes off and the file stays put.
        if (fallbackToInbox) {
            QHash<QString, QStringList> byInbox;
            QStringList stranded;
            for (const QString &messageId : unknown) {
                const Account account =
                    accountForMessagePath(m_model->messageById(messageId).filePath);
                if (account.maildir.isEmpty()) {
                    stranded.append(messageId);
                    continue;
                }
                byInbox[account.maildir + QLatin1Char('/')
                        + inboxFolderFor(account)]
                    .append(messageId);
            }

            for (auto it = byInbox.cbegin(); it != byInbox.cend(); ++it) {
                sendMove(it.value(), it.key(), {},
                         { QStringLiteral("deleted") }, tr("Restore"));
            }

            if (!byInbox.isEmpty()) {
                m_statusLabel->setText(
                    tr("%n message(s) had no record of where they came from "
                       "and were moved to the inbox.", "",
                       int(unknown.size() - stranded.size())));
            }
            if (!stranded.isEmpty()) {
                m_statusLabel->setText(
                    tr("%n message(s) could not be restored: they belong to no "
                       "configured account.", "", int(stranded.size())));
            }
        } else {
            sendMessageTagChange(unknown, {}, { QStringLiteral("deleted") },
                                 tr("Undelete"));
            m_undoStack.push(new MessageTagCommand(
                this, unknown, {}, { QStringLiteral("deleted") },
                tr("Undelete")));
        }
    }

    for (auto it = byOrigin.cbegin(); it != byOrigin.cend(); ++it) {
        // The origin tag is named HERE, not left as the placeholder.
        //
        // onMessagesMoved() resolves the placeholder from the origin the
        // WORKER reports, which is where the message is coming FROM. On a
        // delete that is the inbox and correct; on a restore it is the trash,
        // so the placeholder resolved to `deleted-from:Trash` and asked to
        // remove a tag that never existed, while the real `deleted-from:inbox`
        // was never named. The message came home still claiming to have been
        // deleted from somewhere, which then made Restore offer to move a
        // message that was already back.
        //
        // A restore does not need the placeholder at all: the origin was just
        // read off the message's own tag to decide where to send it, so the
        // exact tag to strip is already known. Recomposed from the same
        // account-relative form it was stored in.
        const QString origin = originTagFor(it.key());
        QStringList remove{ QStringLiteral("deleted") };
        if (!origin.isEmpty())
            remove.append(origin);
        sendMove(it.value(), it.key(), {}, remove, tr("Undelete"));
    }

    showTransientStatus(
        tr("%1: %n message(s)", "", scope.messageCount).arg(tr("Undelete")));
}

void MainWindow::sendMove(const QStringList &messageIds,
                          const QString &destFolder, const QStringList &add,
                          const QStringList &remove,
                          const QString &description, bool fromUndo,
                          const QStringList &wholeThreadIds)
{
    if (messageIds.isEmpty() || destFolder.isEmpty())
        return;

    // Held during a sync for the same reason every tag write is: the worker's
    // read-write open BLOCKS on notmuch's exclusive lock rather than failing,
    // so sending now would freeze the worker for the rest of the run.
    //
    // A move is held as the MOVE it is, not decomposed into a tag edit. The
    // held-edit queue carries tag changes only, so a move pushed through it
    // would apply the tags and never move the file, which is worse than
    // waiting: the message would read as deleted and still be in the inbox.
    if (aSyncHoldsTheWriteLock()) {
        m_heldMoves.append(HeldMove{ messageIds, destFolder, add, remove,
                                     description, fromUndo });
        m_statusLabel->setText(
            tr("A sync is running; your change will be applied when it "
               "finishes."));
        updatePendingIndicator();
        return;
    }

    // Repainted NOW, before the worker is asked.
    //
    // The write itself waits for the move to be confirmed, and must: tagging
    // the database first would leave a message marked deleted in a folder it
    // never left if the rename failed. The DISPLAY has no such constraint, and
    // holding it back until the round trip finished is what made a deleted row
    // sit there unchanged until the user clicked it. The reply rows repainted
    // and the root did not, because the replies were separately tagged while
    // the root's card reads its thread's summary.
    //
    // Reverted by revertPendingTagChange() if the write is rejected, exactly
    // as the tag path's optimistic update is.
    //
    // The placeholder is dropped rather than displayed: the real origin is not
    // known until the worker answers, and a chip reading the placeholder's
    // literal name would be worse than one chip arriving a moment late.
    QStringList displayAdd;
    for (const QString &tag : add) {
        if (tag != kOriginTagPlaceholder())
            displayAdd.append(tag);
    }
    QStringList displayRemove;
    for (const QString &tag : remove) {
        if (tag != kOriginTagPlaceholder())
            displayRemove.append(tag);
    }
    // A thread-scoped move already repainted its rows in
    // trashThreads() / untrashThreads(), synchronously, before
    // the worker was asked to resolve the threads at all. Repeating it here
    // would be harmless but redundant; more importantly the caller there needs
    // the repaint to happen WITHOUT a worker round trip, which is the whole
    // reason it is not done from this function.
    //
    // applyTagChange() is what those callers use, and applyMessageTagChange()
    // is what this one uses, and the difference is not a style choice: the
    // former moves the thread's SUMMARY, which a thread row's card draws from,
    // while the latter deliberately leaves a multi-message thread's summary
    // alone because one message's edit does not describe the conversation.
    if (wholeThreadIds.isEmpty()) {
        for (const QString &messageId : messageIds)
            m_model->applyMessageTagChange(messageId, displayAdd, displayRemove);
    }

    // A row that no longer belongs in the view LEAVES it, rather than sitting
    // there repainted until the next query. Delete strips `inbox`, so in the
    // Inbox view the message it stripped it from stops matching, and leaving
    // it was the defect: a deleted message stayed in the inbox across
    // restarts, since the tag really was gone from the display and really was
    // still what the query asked for.
    //
    // Guarded on the VIEW's own tag, resolved from the query rather than
    // assumed: a plain `tag:<x>` query is the only shape whose membership one
    // tag decides. A path query (Trash, Sent, Drafts) is unaffected by a tag
    // going away, and an arbitrary query the user typed cannot be reasoned
    // about at all, so both are left alone and refresh at the next sync.
    // Without that guard, deleting from an `id:` view would empty the list.
    if (const QString viewTag = viewFilterTag();
        !viewTag.isEmpty() && displayRemove.contains(viewTag)) {
        m_model->removeThreadsWithoutTag(viewTag);
    }

    // What to tag once the move is CONFIRMED. Tagging now would leave a
    // message marked deleted in a folder it never left if the rename failed.
    //
    // A QUEUE, not a map keyed on the destination: two Deletes in the same
    // account before the first confirmation arrives both name `acct/Trash`,
    // so the second insert overwrote the first and the second confirmation
    // took an empty PendingMove. That file landed in the trash carrying
    // neither `deleted` nor `deleted-from:`, which makes it unrestorable and
    // invisible to a `tag:deleted` query. The worker handles one move at a
    // time on its own thread and emits in the order it was asked, so a plain
    // FIFO matches confirmations to requests without needing a key at all.
    m_pendingMoves.enqueue(PendingMove{ add, remove, description, fromUndo });

    QMetaObject::invokeMethod(m_worker, "moveMessages", Qt::QueuedConnection,
                              Q_ARG(QStringList, messageIds),
                              Q_ARG(QString, destFolder));
}

void MainWindow::syncViewMembership(const QStringList &threadIds,
                                    bool aRowIsMissing,
                                    const QStringList &added,
                                    const QStringList &removed)
{
    // Item 177. The optimistic REPAINT has always been universal; the
    // optimistic MEMBERSHIP was not, and lived on the move path alone. So
    // marking a thread read in the Unread view repainted its row and left it
    // in a list defined by `tag:unread` that it no longer matched, until the
    // next query or sync took it away.
    //
    // Membership is the UNION, one rule and no exceptions: a thread belongs
    // to a view while any of its messages match it. The judgement itself is
    // in removeThreadsWithoutTag(), which reads `summary.tags`; what happens
    // here is only deciding WHICH rows to put to it and WHEN.
    //
    // Guarded on the VIEW's own tag, resolved from the query rather than
    // assumed: a plain `tag:<x>` query is the only shape whose membership one
    // tag decides. A path query (Trash, Sent, Drafts) is unaffected by a tag
    // going away, and an arbitrary query the user typed cannot be reasoned
    // about at all, so both are left alone and correct at the next sync.
    // Without that guard, marking read in an `id:` view would empty the list.
    //
    // The exposure this accepts, deliberately and unchanged from the move
    // path: revertPendingTagChange() repaints a rejected write but cannot
    // REINSERT a row, so a write that fails leaves the row gone until the next
    // query. Waiting for confirmation instead would give back exactly the lag
    // this removes, and a rejected tag write is the rare case while the lag
    // was every keystroke.
    const QString viewTag = viewFilterTag();
    if (viewTag.isEmpty())
        return;

    // The INVERSE, which the model cannot do on its own: a row that starts
    // matching cannot be inserted optimistically, since the model holds no
    // summary for a thread the query never returned. A refresh is what
    // expresses it, exactly as the trash view already does after a restore.
    // It matters most for UNDO: undoing a mark-read in the Unread view adds
    // the tag back, and without this the row stayed gone, which would make an
    // undone action invisible in the view it was undone in.
    // refreshCurrentQuery() clears nothing, so the selection, the expansions
    // and the undo stack all survive.
    //
    // Only when the row is genuinely ABSENT, which is the whole cost of the
    // branch. Adding the view's tag to a row still in the list is the ordinary
    // case, and refreshing there re-runs the query on every such keystroke.
    if (added.contains(viewTag)) {
        if (aRowIsMissing)
            refreshCurrentQuery();
        return;
    }

    if (!removed.contains(viewTag))
        return;

    // A row is never evicted while the user is sitting on it. The automatic
    // mark-read fires two seconds after selection, so evicting on it takes the
    // row out from under them, with a context menu possibly open on it, before
    // they can mark it spam or important. The row leaves when the selection
    // moves, which flushDeferredEviction() does, so the view still empties as
    // they work. A write the user ASKED for evicts at once: the distinction is
    // who initiated it, not what it does.
    QStringList onScreen;
    if (m_automaticWrite) {
        const QModelIndexList selectedRows =
            m_threadView->selectionModel()->selectedRows();
        for (const QModelIndex &row : selectedRows)
            onScreen.append(m_model->threadFor(row).threadId);
        const QModelIndex current = m_threadView->currentIndex();
        if (current.isValid())
            onScreen.append(m_model->threadFor(current).threadId);
    }

    QStringList evictable;
    for (const QString &threadId : threadIds) {
        if (onScreen.contains(threadId)) {
            if (!m_deferredEvictions.contains(threadId))
                m_deferredEvictions.append(threadId);
            continue;
        }
        evictable.append(threadId);
    }

    m_model->removeThreadsWithoutTag(evictable, viewTag);
}

void MainWindow::flushDeferredEviction()
{
    // The rows that stopped matching while the user was on them, taken out now
    // that they have moved on. Re-checked against the model rather than
    // trusted: the tag may have come back (an undo, a sync), in which case
    // removeThreadsWithoutTag() correctly keeps the row.
    if (m_deferredEvictions.isEmpty())
        return;

    const QString viewTag = viewFilterTag();
    if (viewTag.isEmpty()) {
        m_deferredEvictions.clear();
        return;
    }

    // Every thread the user is on, from the SELECTION rather than from
    // currentIndex(): a click calls select() before setCurrentIndex(), so at
    // the moment selectionChanged arrives the current index still names the
    // row being left, and reading it would hold the eviction back for ever.
    QStringList onScreen;
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    for (const QModelIndex &row : rows)
        onScreen.append(m_model->threadFor(row).threadId);

    QStringList ready;
    QStringList stillSelected;
    for (const QString &threadId : m_deferredEvictions) {
        if (onScreen.contains(threadId))
            stillSelected.append(threadId);
        else
            ready.append(threadId);
    }
    m_deferredEvictions = stillSelected;

    m_model->removeThreadsWithoutTag(ready, viewTag);
}

QString MainWindow::viewFilterTag() const
{
    const QString query = m_queryEdit->text().trimmed();
    if (query.isEmpty())
        return {};

    const QString accountKey = m_accountBox->currentData().toString();

    // The three tag-backed built-ins, matched against the query RESOLVED in
    // the current account scope, which is what runFilter() put in the bar. The
    // comparison is on the generated string rather than on the button's
    // checked state, so a query the user edited by hand into the same thing
    // behaves identically, and a label translated into another locale cannot
    // change the answer.
    for (const QString &generator : { QStringLiteral("unread"),
                                      QStringLiteral("inbox"),
                                      QStringLiteral("flagged") }) {
        const SavedQuery filter = Config::builtinFilter(generator);
        const QString resolved = m_config.resolvedQuery(filter, accountKey);
        if (resolved == Config::matchNothingQuery())
            continue;
        if (resolved == query) {
            // The TAG, not the generator: "flagged" happens to match its tag
            // and "inbox" and "unread" do too, but the generator is a filter
            // identity and the tag is what a message carries.
            return Config::generatorTagFor(generator);
        }
    }

    return {};
}

void MainWindow::onMessagesMoved(const QMap<QString, QString> &originByMessageId,
                                 const QString &destFolder)
{
    if (m_pendingMoves.isEmpty())
        return;
    const PendingMove pending = m_pendingMoves.dequeue();
    if (originByMessageId.isEmpty())
        return;

    // The origin differs per message, so the tags do too: two messages deleted
    // from different folders get different `deleted-from:` tags out of one
    // gesture. Grouped by the resolved tag list so identical ones still travel
    // as a single write.
    QHash<QString, QStringList> byOrigin;
    for (auto it = originByMessageId.cbegin(); it != originByMessageId.cend();
         ++it) {
        byOrigin[it.value()].append(it.key());
    }

    for (auto it = byOrigin.cbegin(); it != byOrigin.cend(); ++it) {
        // The origin tag names the folder relative to the ACCOUNT, not to the
        // database: `inbox`, never `acct/inbox`. Restore recomposes the
        // account prefix from the message's own path, so storing it here would
        // duplicate it, and a stored account prefix would go stale the day the
        // user renames a maildir.
        //
        // The worker reports `acct/inbox`; the account's own maildir is
        // `acct`, so the stored tag is `inbox`. Resolved through the first
        // message's path, which is still the account's whichever folder it
        // sits in now.
        const QString originTag = originTagFor(it.key());

        auto resolve = [&](const QStringList &tags) {
            QStringList out;
            for (const QString &tag : tags) {
                if (tag != kOriginTagPlaceholder()) {
                    out.append(tag);
                    continue;
                }
                if (!originTag.isEmpty())
                    out.append(originTag);
            }
            return out;
        };

        const QStringList resolvedAdd = resolve(pending.add);
        const QStringList resolvedRemove = resolve(pending.remove);
        sendMessageTagChange(it.value(), resolvedAdd, resolvedRemove,
                             pending.description);

        // The undo entry carries the RESOLVED tags, and is pushed per origin
        // group rather than once for the batch.
        //
        // It used to be handed pending.add straight, which still holds the
        // unresolved placeholder: undo then asked to remove a tag by that
        // literal name, which no message carries, so the removal was a silent
        // no-op and `deleted-from:inbox` survived the undo. The file came home
        // still claiming to have been deleted from somewhere. Same defect as
        // the one the second-Delete path had, reached through Ctrl+Z instead.
        //
        // Per group because the placeholder resolves to a DIFFERENT tag per
        // origin: one command for a batch spanning two folders could only
        // carry one of them, so the other would be the wrong tag rather than
        // merely an unresolved one.
        if (!pending.fromUndo) {
            QMap<QString, QString> groupOrigins;
            for (const QString &messageId : it.value())
                groupOrigins.insert(messageId, originByMessageId.value(messageId));
            m_undoStack.push(new MoveCommand(this, groupOrigins, destFolder,
                                             resolvedAdd, resolvedRemove,
                                             pending.description));
        }
    }

    // A restore out of the TRASH VIEW leaves the row it came from showing a
    // message that is no longer there, and only a refresh can say so.
    //
    // Reported from a hand test: the move was correct and the row sat in the
    // list until the Trash filter was clicked again. The trash view is PATH
    // based, so a restored message stops matching the query the list was built
    // from, which is a state no tag change can express. Nothing else here
    // removes a row, deliberately: in an ordinary view a deleted message's
    // card should stay put, since one deleted reply does not doom the
    // conversation.
    //
    // refreshCurrentQuery() rather than runCurrentQuery(): it clears nothing,
    // so the selection, the expanded threads, the undo stack and the message
    // being read all survive. Re-running the query outright would destroy the
    // undo entry this function just pushed, which is the one thing a restore
    // must leave intact.
    //
    // Gated on isShowingTrash() and not on the destination: a Delete is a move
    // too and reaches this same slot, and refreshing after every delete would
    // make a row vanish from under the user in every other view.
    if (isShowingTrash())
        refreshCurrentQuery();

    // The undo entries are pushed inside the loop above, one per origin
    // group, because the placeholder resolves per origin. Nothing is pushed
    // for a move the undo stack itself started: a MoveCommand is confirmed
    // through this same slot, so pushing unconditionally left the undo of a
    // Delete putting a fresh command on the stack instead of consuming the
    // one it undid, and a second press of undo re-deleted the message. The
    // flag rides on PendingMove because the answer has to survive the queued
    // round trip; a window-wide "am I undoing" flag would long since have
    // been cleared by the time the worker replies.
}

void MainWindow::sendThreadTagChange(const QStringList &threadIds,
                                     const QStringList &add,
                                     const QStringList &remove,
                                     const QString &description)
{
    // Optimistic: the rows change now, so a bulk archive of hundreds of threads
    // feels instant. Recorded so onWorkerError() can put them back.
    for (const QString &threadId : threadIds)
        m_model->applyTagChange(threadId, add, remove);

    // Which accounts this touches, recorded HERE and not in onTagsApplied():
    // TagChange carries message ids, while the account is a property of the
    // thread, and by the time the worker confirms, the rows may be gone. A
    // write that is later rejected leaves an account listed here that needed no
    // sync, which costs one redundant channel on the next run; missing one
    // would strand the user's edits, which is the failure worth avoiding.
    for (const QString &threadId : threadIds) {
        const QStringList keys = m_model->accountKeysForThread(threadId);
        for (const QString &key : keys)
            m_editedAccounts.insert(key);
    }

    // The strip shows the open thread's tags, so it has to follow a change to
    // that thread rather than waiting for the next selection.
    if (threadIds.contains(m_currentThreadId)) {
        const QModelIndex current = m_threadView->currentIndex();
        if (current.isValid())
            m_messageView->setTags(m_model->threadFor(current).tags);
    }

    // Same as the message path: a thread whose union stops matching the view
    // leaves it now rather than at the next query. A thread-scoped write moves
    // the summary, which is what the membership judgement reads, so a thread
    // marked read is emptied of `unread` in one step and correctly evicted.
    bool aRowIsMissing = false;
    for (const QString &threadId : threadIds)
        aRowIsMissing = aRowIsMissing || !m_model->hasThread(threadId);
    syncViewMembership(threadIds, aRowIsMissing, add, remove);

    // A sync holds notmuch's exclusive write lock, and the worker's read-write
    // open BLOCKS on it rather than failing: measured 9.158s against a 12s
    // hold, returning SUCCESS. Sending now would freeze the worker thread for
    // the rest of the sync, queueing every later query and thread load behind
    // it. Hold the edit and send it when the lock frees.
    //
    // The rows keep the optimistic update applied above, which is honest: it is
    // what the user asked for and it is going to be applied.
    if (aSyncHoldsTheWriteLock()) {
        m_heldEdits.append(HeldEdit{
            threadIds, TagChange{ {}, add, remove, description } });

        // NOT transient. This describes state that lasts until the sync ends,
        // and a message that expired would leave the user with rows showing a
        // tag the database has not got and no explanation of why.
        m_statusLabel->setText(
            tr("A sync is running; your change will be applied when it "
               "finishes."));

        // A held edit is outstanding work, so the indicator has to show it.
        updatePendingIndicator();
        return;
    }

    m_pendingThreadIds = threadIds;
    m_pendingChange = TagChange{ {}, add, remove, description };

    // The worker resolves thread ids to message ids: the UI does not hold
    // message ids for rows it never opened.
    QMetaObject::invokeMethod(m_worker, "applyTagsToThreads",
                              Qt::QueuedConnection,
                              Q_ARG(QStringList, threadIds),
                              Q_ARG(QStringList, add),
                              Q_ARG(QStringList, remove),
                              Q_ARG(QString, description));
}

