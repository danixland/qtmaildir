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

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
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
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "mailsync.h"
#include "messageview.h"
#include "mimeparser.h"
#include "notmuchworker.h"
#include "querycompleter.h"
#include "tagchip.h"
#include "tagdialog.h"
#include "threadlistmodel.h"
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

void MainWindow::setLocksPathForTesting(const QString &path)
{
    g_locksPath = path;
}

QString MainWindow::locksPath()
{
    return g_locksPath;
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

    // A header blob saved against a different set of columns must be
    // discarded, not restored. QHeaderView::restoreState() returns TRUE for a
    // blob with fewer sections than the model and applies the old widths to
    // the wrong columns: adding the attachment column in front shifted every
    // saved width one place right, silently mangling the layout with no error
    // to detect it by (verified on Qt 6.11). The column count is stored
    // alongside and the blob is only used when it still matches.
    const QByteArray header = state.value(QStringLiteral("threadlist/header"))
                                  .toByteArray();
    const int savedColumns =
        state.value(QStringLiteral("threadlist/columns")).toInt();
    if (!header.isEmpty() && savedColumns == ThreadListModel::ColumnCount) {
        m_threadView->horizontalHeader()->restoreState(header);
    }

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
    state.setValue(QStringLiteral("threadlist/header"),
                   m_threadView->horizontalHeader()->saveState());
    // Guards the blob above: see restoreUiState().
    state.setValue(QStringLiteral("threadlist/columns"),
                   int(ThreadListModel::ColumnCount));
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
                tr("%n tag change(s) have not been synced, and no sync command "
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
            box.setText(tr("%n tag change(s) have not been synced.", "",
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
                if (m_sync->start()) {
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
            if (m_sync->start()) {
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
    }

    return QMainWindow::eventFilter(watched, event);
}

MainWindow::MainWindow(const Config &config, QWidget *parent)
    : QMainWindow(parent), m_config(config)
{
    qRegisterMetaType<ThreadSummary>();
    qRegisterMetaType<MessageRef>();
    qRegisterMetaType<TagChange>();
    qRegisterMetaType<QVector<ThreadSummary>>();
    qRegisterMetaType<QVector<MessageRef>>();

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
    buildMenus();
    // After buildMenus(): QMainWindow::restoreState() matches toolbars by
    // object name, so they must already exist or their position is dropped.
    restoreUiState();
    wireWorker();
    showWarnings();

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
    const SavedQuery startup = m_config.startupSavedQuery();
    if (!startup.query.isEmpty()) {
        m_queryEdit->setText(startup.query);
        runCurrentQuery();
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
    m_pendingLabel->hide();
    statusBar()->addPermanentWidget(m_pendingLabel);

    // Indeterminate: setRange(0, 0). A sync has no measurable progress, since
    // mbsync reports no percentage and the script's output is unstructured, so
    // a bar filling left to right would be inventing a fraction. This one
    // animates to say "working, duration unknown".
    m_syncProgress = new QProgressBar(this);
    m_syncProgress->setObjectName(QStringLiteral("syncProgress"));
    m_syncProgress->setRange(0, 0);
    m_syncProgress->setTextVisible(false);
    m_syncProgress->setMaximumWidth(120);
    m_syncProgress->hide();
    statusBar()->addPermanentWidget(m_syncProgress);

    // Query row.
    auto *queryRow = new QHBoxLayout;
    m_accountBox = new QComboBox(central);
    m_accountBox->addItem(tr("All accounts"), QString());
    for (const Account &account : m_config.accounts())
        m_accountBox->addItem(account.key, account.key);

    m_queryEdit = new QLineEdit(central);
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

    // One row: the account dropdown, the query field, then the saved queries.
    // The field is the only stretching item, so it is framed on both sides
    // rather than running flush to the window edge, which is what the removed
    // Sync button used to terminate.
    //
    // ponytail: no overflow handling. [queries] is unbounded and enough entries
    // would squeeze the field, but three is the real-world case today. Item 23
    // already specifies buttons-plus-menu and is where that belongs.
    queryRow->addWidget(m_accountBox);
    queryRow->addWidget(m_queryEdit, 1);
    for (const SavedQuery &saved : m_config.savedQueries()) {
        auto *button = new QPushButton(saved.name, central);
        connect(button, &QPushButton::clicked, this, [this, saved]() {
            m_queryEdit->setText(saved.query);
            runCurrentQuery();
        });
        queryRow->addWidget(button);
    }
    layout->addLayout(queryRow);

    // Thread list and message pane.
    m_model = new ThreadListModel(this);
    m_model->setTagColors(&m_tagColors);
    m_threadView = new QTableView(central);
    m_threadView->setModel(m_model);
    m_threadView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_threadView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_threadView->verticalHeader()->hide();
    m_threadView->horizontalHeader()->setStretchLastSection(false);
    // Every column Interactive, Subject included: Stretch and ResizeToContents
    // both compute a width and discard the user's drag. Nothing absorbs spare
    // width as a result, so the columns end where they end.
    for (int column = 0; column < ThreadListModel::ColumnCount; ++column) {
        m_threadView->horizontalHeader()->setSectionResizeMode(
            column, QHeaderView::Interactive);
    }

    // The subject cell carries the account chip in front of its text.
    m_threadView->setItemDelegateForColumn(ThreadListModel::SubjectColumn,
                                           new SubjectDelegate(this));
    // Widening a column past the viewport scrolls rather than squeezing the
    // others. Per-pixel so the scroll does not jump a whole column at a time.
    m_threadView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_threadView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Starting widths only; a drag overrides them, and they are what the
    // saved-widths item will persist.
    // Without this the attachment column cannot be narrow at all: the default
    // minimum section size is 58px on this platform, and setColumnWidth()
    // clamps to it silently rather than reporting the smaller value back.
    m_threadView->horizontalHeader()->setMinimumSectionSize(24);
    m_threadView->setColumnWidth(ThreadListModel::AttachmentColumn, 28);
    m_threadView->setColumnWidth(ThreadListModel::DateColumn, 130);
    m_threadView->setColumnWidth(ThreadListModel::AuthorsColumn, 180);
    m_threadView->setColumnWidth(ThreadListModel::SubjectColumn, 520);

    connect(m_threadView->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onThreadSelected);

    // Separate from currentRowChanged: a selection can grow without current
    // moving at all. Ctrl+click adds a row and leaves current where it was, and
    // selectAll() emits no currentRowChanged whatsoever (verified against
    // Qt 6.11). Both are multi-select gestures that have to blank the pane and
    // cancel a pending mark-read, so neither can rely on the current-index
    // signal to notice them.
    connect(m_threadView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSelectionChanged);

    m_messageView = new MessageView(central);
    m_messageView->setTagColors(&m_tagColors);
    connect(m_messageView, &MessageView::statusMessage,
            this, [this](const QString &text) { m_statusLabel->setText(text); });

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->addWidget(m_threadView);
    m_splitter->addWidget(m_messageView);
    m_splitter->setStretchFactor(1, 2);
    layout->addWidget(m_splitter, 1);

    layout->addWidget(m_syncLogPane);

    setCentralWidget(central);

    resize(1200, 800);
    setWindowTitle(QStringLiteral("qtmaildir %1").arg(QTMAILDIR_VERSION));
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
    const QKeySequence sequence = m_keyMap.sequenceFor(name);
    if (!sequence.isEmpty())
        action->setShortcut(sequence);

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
        const QModelIndex current = m_threadView->currentIndex();
        const int row = current.isValid() ? current.row() + 1 : 0;
        if (row < m_model->rowCount())
            m_threadView->selectRow(row);
    });
    addAction(QStringLiteral("prev_thread"), tr("&Previous thread"),
              tr("Select the previous thread"), [this]() {
        const QModelIndex current = m_threadView->currentIndex();
        if (current.isValid() && current.row() > 0)
            m_threadView->selectRow(current.row() - 1);
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
        // A toggle, like toggle_unread: pressing Delete twice is the natural
        // way to say "no, put it back", and adding a tag that is already there
        // is a no-op the user cannot see.
        //
        // One direction for the WHOLE selection. Toggling each thread
        // independently would leave one keystroke with the selection in two
        // states, which is worse than either outcome, so undelete only when
        // every selected thread is already deleted.
        const QModelIndexList rows =
            m_threadView->selectionModel()->selectedRows();
        bool allDeleted = !rows.isEmpty();
        for (const QModelIndex &index : rows) {
            if (!m_model->threadAt(index.row()).isDeleted()) {
                allDeleted = false;
                break;
            }
        }

        if (allDeleted)
            tagSelected({}, { QStringLiteral("deleted") }, tr("Undelete"));
        else
            tagSelected({ QStringLiteral("deleted") }, {}, tr("Delete"));
    });
    addAction(QStringLiteral("spam"), tr("Mark &spam"),
              tr("Add spam and remove inbox"), [this]() {
        tagSelected({ QStringLiteral("spam") }, { QStringLiteral("inbox") },
                    tr("Mark spam"));
    });
    addAction(QStringLiteral("flag"), tr("&Flag"),
              tr("Add the flagged tag"), [this]() {
        tagSelected({ QStringLiteral("flagged") }, {}, tr("Flag"));
    });
    addAction(QStringLiteral("toggle_unread"), tr("Toggle &unread"),
              tr("Toggle the unread tag"), [this]() {
        // The direction comes from the current row, but the change applies to
        // the whole selection, so a mixed selection lands in one consistent
        // state rather than each row flipping its own way.
        const QModelIndex current = m_threadView->currentIndex();
        if (!current.isValid())
            return;
        const ThreadSummary thread = m_model->threadAt(current.row());

        // An explicit toggle overrides the automatic one. Without this, marking
        // a thread unread by hand would be undone a moment later by a timer
        // armed when it was opened, and the key would look broken.
        m_markReadTimer->stop();
        m_markReadThreadId.clear();

        if (thread.isUnread())
            tagSelected({}, { QStringLiteral("unread") }, tr("Mark read"));
        else
            tagSelected({ QStringLiteral("unread") }, {}, tr("Mark unread"));
    });
    addAction(QStringLiteral("edit_tags"), tr("Edit &tags..."),
              tr("Add or remove any tag on the selected threads"), [this]() {
        editTagsOnSelection();
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
        // A threadLoaded still in flight for that id would otherwise paint the
        // thread straight back, which is the queued-reply race documented in
        // CLAUDE.md.
        m_currentThreadId.clear();
        m_messageView->clear();
        m_markReadTimer->stop();
        m_markReadThreadId.clear();
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

    // A binding the user wrote for an action that does not exist would be
    // silently dead. KeyMap warns about unknown names, but only a check here
    // catches the reverse: a known action nothing implements.
    Q_ASSERT(m_actions.size() == KeyMap::knownActions().size());
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
    editMenu->addSeparator();
    editMenu->addAction(m_actions.value(QStringLiteral("select_all")));

    auto *messageMenu = menuBar()->addMenu(tr("&Message"));
    messageMenu->addAction(m_actions.value(QStringLiteral("archive")));
    messageMenu->addAction(m_actions.value(QStringLiteral("delete")));
    messageMenu->addAction(m_actions.value(QStringLiteral("spam")));
    messageMenu->addSeparator();
    messageMenu->addAction(m_actions.value(QStringLiteral("toggle_unread")));
    messageMenu->addAction(m_actions.value(QStringLiteral("edit_tags")));
    messageMenu->addAction(m_actions.value(QStringLiteral("flag")));

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_actions.value(QStringLiteral("prev_thread")));
    viewMenu->addAction(m_actions.value(QStringLiteral("next_thread")));
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
    auto *about = helpMenu->addAction(tr("&About"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);

    // Standard names from the icon theme, so the buttons match the rest of the
    // desktop rather than shipping bespoke art. A theme that lacks one leaves
    // that action with text alone, which still works.
    const QHash<QString, QString> themeIcons = {
        { QStringLiteral("sync"),    QStringLiteral("mail-receive") },
        { QStringLiteral("archive"), QStringLiteral("mail-mark-read") },
        { QStringLiteral("delete"),  QStringLiteral("edit-delete") },
        { QStringLiteral("undo"),    QStringLiteral("edit-undo") },
        { QStringLiteral("spam"),    QStringLiteral("mail-mark-junk") },
        { QStringLiteral("flag"),    QStringLiteral("mail-mark-important") },
        { QStringLiteral("quit"),    QStringLiteral("application-exit") },
        { QStringLiteral("focus_query"), QStringLiteral("edit-find") },
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
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("spam")));
    m_threadContextMenu->addSeparator();
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("toggle_unread")));
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("flag")));
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("edit_tags")));
    m_threadContextMenu->addSeparator();
    m_threadContextMenu->addAction(m_actions.value(QStringLiteral("select_all")));

    m_threadView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_threadView, &QTableView::customContextMenuRequested,
            this, &MainWindow::showThreadContextMenu);

    // The frequent subset only. A toolbar holding every action is as
    // unreadable as no toolbar.
    auto *toolBar = addToolBar(tr("Main"));
    toolBar->setObjectName(QStringLiteral("main_toolbar"));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
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
    toolBar->addSeparator();
    toolBar->addAction(m_actions.value(QStringLiteral("undo")));
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
                      .arg(QStringLiteral(QTMAILDIR_VERSION)));

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
    connect(m_worker, &NotmuchWorker::threadLoaded,
            this, &MainWindow::onThreadLoaded);
    connect(m_worker, &NotmuchWorker::errorOccurred,
            this, &MainWindow::onWorkerError);
    connect(m_worker, &NotmuchWorker::allTagsReady,
            this, &MainWindow::onAllTagsReady);

    // A confirmed write clears the pending revert: without this, a later
    // unrelated error would roll back a change that actually succeeded.
    connect(m_worker, &NotmuchWorker::tagsApplied,
            this, &MainWindow::onTagsApplied);

    m_workerThread.start();

    // Queued behind the thread start, so the completer has real tags as soon
    // as the database can be read. Nothing waits on the answer: requestAllTags
    // stays silent when the database cannot be opened.
    requestAllTags();
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

void MainWindow::showWarnings()
{
    const QStringList warnings = m_config.warnings() + m_keyMap.warnings();
    if (warnings.isEmpty())
        return;

    // Non-fatal: the app runs degraded rather than refusing to start.
    m_statusLabel->setText(
        tr("%n configuration warning(s)", "", warnings.size()));

    // Interrupt startup only for things that are actually wrong. Every KeyMap
    // warning qualifies (each one means a binding the user wrote is being
    // ignored), but a Config notice such as "no sync command configured" does
    // not: nothing is broken, the feature is simply off, and a modal on every
    // launch teaches the user to dismiss dialogs unread.
    const QStringList problems = m_config.problems() + m_keyMap.warnings();
    if (problems.isEmpty())
        return;

    QMessageBox::warning(this, tr("Configuration problems"),
                         problems.join(QLatin1Char('\n')));
}

void MainWindow::runCurrentQuery()
{
    QString query = m_queryEdit->text().trimmed();

    const QString accountKey = m_accountBox->currentData().toString();
    if (!accountKey.isEmpty())
        query = m_config.account(accountKey).scopedQuery(query);

    if (query.isEmpty())
        return;

    // Kept so loadThread() can work out which messages of a thread matched.
    m_lastQuery = query;

    ++m_generation;
    m_model->clear();
    m_messageView->clear();

    // Undo entries refer to rows that are about to be discarded. The model
    // update they invert would be a no-op against the new result set, leaving
    // undo half-applied: the database would change and the list would not.
    m_undoStack.clear();
    m_pendingChange = {};
    m_pendingThreadIds.clear();

    m_statusLabel->setText(tr("Searching..."));

    QMetaObject::invokeMethod(m_worker, "runQuery", Qt::QueuedConnection,
                              Q_ARG(QString, query),
                              Q_ARG(quint64, m_generation));
}

void MainWindow::onThreadsReady(const QVector<ThreadSummary> &threads,
                                quint64 generation)
{
    if (generation != m_generation)
        return;  // Superseded by a newer query.
    m_model->appendBatch(threads);
}

void MainWindow::onQueryFinished(int total, quint64 generation)
{
    if (generation != m_generation)
        return;
    // The query's own result is what the bar says when nothing more pressing
    // is happening, so a transient message falls back to it rather than to
    // nothing.
    m_defaultStatus = tr("%n thread(s)", "", total);
    m_statusLabel->setText(m_defaultStatus);
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
    if (!m_threadView->selectionModel()->isRowSelected(index.row()))
        m_threadView->selectRow(index.row());

    m_threadContextMenu->popup(m_threadView->viewport()->mapToGlobal(pos));
}

void MainWindow::onSelectionChanged()
{
    const int selected = m_threadView->selectionModel()->selectedRows().size();
    if (selected <= 1) {
        // Clearing the count here would wipe whatever the last action reported
        // ("Archive: 3 threads"), which is the more useful message once the
        // selection is gone. Only a count this function wrote is taken back.
        if (m_statusLabel->text() == m_selectionMessage)
            m_statusLabel->clear();
        m_selectionMessage.clear();

        // Collapsing a multi-row selection back to one row has to load that
        // row here, and cannot be left to onThreadSelected. currentRowChanged
        // is emitted BEFORE the selection model is updated (verified against
        // Qt 6.11), so when a click collapses three rows to one, that handler
        // still sees three selected, takes the multi-select branch and returns
        // without loading anything. Only this signal sees the real count.
        const QModelIndex current = m_threadView->currentIndex();
        if (current.isValid()
            && m_model->threadAt(current.row()).threadId != m_currentThreadId) {
            onThreadSelected(current, QModelIndex());
        }
        return;
    }

    // The count is the part that actually teaches multi-select: it acknowledges
    // the selection while it is being built, rather than only after an action
    // has already been applied to it.
    m_selectionMessage = tr("%n thread(s) selected", "", selected);
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
    m_markReadThreadId.clear();
    m_currentThreadId.clear();
    m_messageView->clear();
}

void MainWindow::onThreadSelected(const QModelIndex &current,
                                  const QModelIndex &)
{
    if (!current.isValid())
        return;

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
        m_markReadThreadId.clear();
        m_currentThreadId.clear();
        m_messageView->clear();
        return;
    }

    const ThreadSummary thread = m_model->threadAt(current.row());
    m_currentThreadId = thread.threadId;
    m_messageView->setTags(thread.tags);
    scheduleMarkRead(thread);
    QMetaObject::invokeMethod(m_worker, "loadThread", Qt::QueuedConnection,
                              Q_ARG(QString, m_currentThreadId),
                              Q_ARG(QString, m_lastQuery),
                              Q_ARG(quint64, m_generation));
}

void MainWindow::onThreadLoaded(const QVector<MessageRef> &messages,
                                quint64 generation)
{
    if (generation != m_generation || messages.isEmpty())
        return;

    // A load started while the selection was still a single row can land after
    // it has grown: loadThread crosses to the worker on a queued connection, so
    // the reply arrives after onSelectionChanged() has already blanked the
    // pane. Without this it would paint a thread back over the blank, and the
    // pane would only look right once a third row made the count stale-proof.
    if (m_threadView->selectionModel()->selectedRows().size() > 1)
        return;

    MimeParser parser;
    QList<ThreadRenderItem> items;
    items.reserve(messages.size());

    for (int i = 0; i < messages.size(); ++i) {
        const MessageRef &ref = messages.at(i);

        ThreadRenderItem item;
        item.message = parser.parse(ref.filePath);

        if (!item.message.ok) {
            // One unreadable message must not lose the rest of the thread, so
            // it becomes an inline note rather than replacing the whole pane.
            item.message = {};
            item.message.ok = true;
            item.message.from = tr("(unreadable message)");
            item.message.subject = ref.filePath;
            item.message.plainBody =
                tr("This message could not be parsed.\n%1").arg(ref.filePath);
        }

        // Namespace prefix keeps cid: references distinct across the thread.
        item.cidPrefix = cidPrefixForIndex(i);

        // Matched messages open; the rest collapse to a stub. The last message
        // always opens, so a thread never renders as nothing but stubs.
        item.expanded = ref.matched || i == messages.size() - 1;

        items.append(item);
    }

    m_messageView->showThread(items);
}

void MainWindow::revertPendingTagChange()
{
    if (m_pendingThreadIds.isEmpty())
        return;

    // Put the rows back the way they were. Only the model is touched: the
    // worker never applied the change, so there is nothing to undo there.
    for (const QString &threadId : m_pendingThreadIds) {
        m_model->applyTagChange(threadId, m_pendingChange.removed,
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
    if (m_heldEdits.isEmpty())
        return;

    // Taken by value and cleared first: sendThreadTagChange() writes
    // m_pendingThreadIds, and re-entering partway through the queue must not
    // find the same edits still waiting.
    const QVector<HeldEdit> edits = m_heldEdits;
    m_heldEdits.clear();

    for (const HeldEdit &edit : edits) {
        // Take the optimistic update back before sending, because
        // sendThreadTagChange() applies it again. applyTagChange() is
        // idempotent per tag so the rows do not visibly flicker; without this
        // the change is applied twice and a later revert undoes only one of
        // them, leaving a row showing a tag the database never got.
        for (const QString &threadId : edit.threadIds) {
            m_model->applyTagChange(threadId, edit.change.removed,
                                    edit.change.added);
        }

        sendThreadTagChange(edit.threadIds, edit.change.added,
                            edit.change.removed, edit.change.description);
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
    flushHeldEdits();

    if (success) {
        // Only a SUCCESSFUL sync clears the count. Clearing on failure would
        // assert the edits had reached the mail store when the sync is exactly
        // what failed to put them there.
        m_pendingTagEdits.clear();
        m_unnettablePendingEdits = 0;
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

        runCurrentQuery();
        // A sync is the usual way new tags enter the database.
        requestAllTags();
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
            recordPendingEdit(messageId, tag, true);
        for (const QString &tag : change.removed)
            recordPendingEdit(messageId, tag, false);
    }

    // A change carrying no message ids cannot be netted against anything, and
    // must still register: losing an edit understates the indicator, which is
    // the direction that costs the user work.
    if (change.messageIds.isEmpty()
        && !(change.added.isEmpty() && change.removed.isEmpty())) {
        ++m_unnettablePendingEdits;
    }

    updatePendingIndicator();

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

    // Deliberately reports rather than refreshes. runCurrentQuery() clears the
    // undo stack, the selection and the message pane, which is right for a
    // query the user typed and hostile for one fired by a cron timer: with a
    // sync every ten minutes it would discard undo history and close the thread
    // being read, up to six times an hour, with no action from the user.
    //
    // Unknown is not worth reporting either. It means the lock table could not
    // be read, so nothing was observed, and "sync finished" would be a claim
    // this cannot support.
    if (state == SyncMonitor::State::Idle) {
        showTransientStatus(
            tr("Background sync completed. Press Enter in the query bar to "
               "refresh."));
    }

    // OUTSIDE the Idle branch, deliberately. Unknown clears the busy flag above,
    // so writes resume from here on; leaving the flush inside Idle would let a
    // new edit go straight out while the ones already held sat waiting for an
    // Idle that a broken /proc/locks will never report. After the status
    // message, which flushHeldEdits() overwrites with its own when it sent
    // something.
    flushHeldEdits();
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

    m_syncProgress->setVisible(busy);

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

    if (!m_sync->start()) {
        showTransientStatus(tr("Sync already running"));
        return;
    }
    setSyncBusy(true);
}

void MainWindow::recordPendingEdit(const QString &messageId, const QString &tag,
                                   bool added)
{
    const QString key = messageId + QLatin1Char('\n') + tag;

    // A tag put back the way it was is not an outstanding change. Erase rather
    // than store the new direction, or the ledger grows without bound over a
    // long session of tagging and untagging.
    const auto existing = m_pendingTagEdits.constFind(key);
    if (existing != m_pendingTagEdits.constEnd()) {
        if (*existing != added)
            m_pendingTagEdits.erase(m_pendingTagEdits.find(key));
        return;
    }

    m_pendingTagEdits.insert(key, added);
}

int MainWindow::pendingEditCount() const
{
    // A held edit has NOT reached the index, so onTagsApplied() never counted
    // it. It still has to count here: this is what the exit prompt reads, and
    // an edit waiting on a lock is precisely the work quitting would lose.
    // Each held edit counts as one whatever its size, since it carries thread
    // ids rather than message ids and cannot be netted against the map.
    const int held = int(m_heldEdits.size());
    return m_pendingTagEdits.size() + m_unnettablePendingEdits + held;
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
        tr("Tag changes made here that a sync has not yet carried to the mail "
           "store. An external notmuch run can clear them without this count "
           "noticing."));
    m_pendingLabel->show();
}

void MainWindow::scheduleMarkRead(const ThreadSummary &thread)
{
    // Any pending timer belongs to a thread that is no longer on screen.
    // Stopping unconditionally is what makes this a restart rather than a
    // stack: arrowing down ten threads must mark only the one still selected
    // when the timer finally fires.
    m_markReadTimer->stop();
    m_markReadThreadId.clear();

    // Negative disables the behaviour entirely, per the config key.
    const int delay = m_config.markReadDelayMs();
    if (delay < 0)
        return;

    // Nothing to do for a thread that is already read. Checked here rather
    // than in the handler so no timer is even armed, which keeps a read thread
    // from arming one that would fire into a no-op write.
    if (!thread.tags.contains(QStringLiteral("unread")))
        return;

    m_markReadThreadId = thread.threadId;

    // Zero means immediately, and a zero-interval timer still fires through
    // the event loop rather than reentering the selection handler.
    m_markReadTimer->start(delay);
}

void MainWindow::markCurrentThreadRead()
{
    if (m_markReadThreadId.isEmpty())
        return;

    // The selection can have moved on between the timer being armed and it
    // firing, and the thread can have been marked read by hand in that window.
    // Both mean this timer has nothing left to do.
    if (m_markReadThreadId != m_currentThreadId) {
        m_markReadThreadId.clear();
        return;
    }

    const QModelIndex current = m_threadView->currentIndex();
    if (!current.isValid()) {
        m_markReadThreadId.clear();
        return;
    }

    const ThreadSummary thread = m_model->threadAt(current.row());
    if (thread.threadId != m_markReadThreadId
        || !thread.tags.contains(QStringLiteral("unread"))) {
        m_markReadThreadId.clear();
        return;
    }

    const QStringList threadIds = { m_markReadThreadId };
    m_markReadThreadId.clear();

    // sendThreadTagChange, NOT tagSelected: this deliberately does not go on
    // the undo stack. The user never took this action, so hijacking Ctrl+Z to
    // reverse it would undo something they did not do, and toggle_unread
    // already gives them a direct way to put it back. Decided 2026-08-03.
    //
    // It still funnels through the one applyTags path, per CLAUDE.md; what
    // differs is only whether the inverse is pushed, which is a window-level
    // decision above the worker.
    sendThreadTagChange(threadIds, {}, { QStringLiteral("unread") },
                        tr("Mark read"));
}

void MainWindow::editTagsOnSelection()
{
    const QModelIndexList rows =
        m_threadView->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        showTransientStatus(tr("Select a thread first"));
        return;
    }

    // How many of the selected threads carry each tag, which is what tells a
    // tag that is on all of them from one that is on some.
    QHash<QString, int> counts;
    for (const QModelIndex &index : rows) {
        const ThreadSummary thread = m_model->threadAt(index.row());
        for (const QString &tag : thread.tags)
            counts[tag] += 1;
    }

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

    QStringList threadIds;
    threadIds.reserve(rows.size());
    for (const QModelIndex &index : rows)
        threadIds.append(m_model->threadAt(index.row()).threadId);

    sendThreadTagChange(threadIds, add, remove, description);

    // Pushed for undo. The inverse re-resolves the same threads, so it works
    // whether or not those rows are still selected.
    m_undoStack.push(new ThreadTagCommand(this, threadIds, add, remove,
                                          description));

    showTransientStatus(
        tr("%1: %n thread(s)", "", threadIds.size()).arg(description));
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

    // The strip shows the open thread's tags, so it has to follow a change to
    // that thread rather than waiting for the next selection.
    if (threadIds.contains(m_currentThreadId)) {
        const QModelIndex current = m_threadView->currentIndex();
        if (current.isValid())
            m_messageView->setTags(m_model->threadAt(current.row()).tags);
    }

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

