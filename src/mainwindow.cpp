#include "mainwindow.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QVBoxLayout>

#include "mailsync.h"
#include "messageview.h"
#include "mimeparser.h"
#include "notmuchworker.h"
#include "threadlistmodel.h"

QStringList MainWindow::registeredActionNames()
{
    // Keep in sync with registerActions(). Held against KeyMap::knownActions()
    // by a test rather than by hope.
    return {
        QStringLiteral("next_thread"),
        QStringLiteral("prev_thread"),
        QStringLiteral("open_thread"),
        QStringLiteral("archive"),
        QStringLiteral("delete"),
        QStringLiteral("spam"),
        QStringLiteral("toggle_unread"),
        QStringLiteral("flag"),
        QStringLiteral("focus_query"),
        QStringLiteral("toggle_html"),
        QStringLiteral("load_remote"),
        QStringLiteral("undo"),
        QStringLiteral("sync"),
        QStringLiteral("quit"),
    };
}

QString MainWindow::cidPrefixForIndex(int index)
{
    // "m<index>" is digits only after the 'm', so it cannot contain '!'.
    return QStringLiteral("m%1").arg(index);
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
    }

    buildUi();
    registerActions();
    wireWorker();
    showWarnings();

    installEventFilter(this);

    if (!m_config.savedQueries().isEmpty()) {
        m_queryEdit->setText(m_config.savedQueries().first().query);
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
    statusBar()->addWidget(m_statusLabel);

    // Query row.
    auto *queryRow = new QHBoxLayout;
    m_accountBox = new QComboBox(central);
    m_accountBox->addItem(tr("All accounts"), QString());
    for (const Account &account : m_config.accounts())
        m_accountBox->addItem(account.key, account.key);

    m_queryEdit = new QLineEdit(central);
    m_queryEdit->setPlaceholderText(tr("notmuch query, e.g. tag:inbox"));
    connect(m_queryEdit, &QLineEdit::returnPressed,
            this, &MainWindow::runCurrentQuery);

    m_syncLog = new QPlainTextEdit(central);
    m_syncLog->setReadOnly(true);
    m_syncLog->setMaximumHeight(120);
    m_syncLog->hide();

    m_syncButton = new QPushButton(tr("Sync"), central);
    m_sync = new MailSync(m_config.syncCommand(), this);
    m_syncButton->setEnabled(m_sync->isAvailable());
    if (!m_sync->isAvailable()) {
        m_syncButton->setToolTip(
            tr("No sync command configured ([sync] command in qtmaildir.conf)"));
    }
    connect(m_syncButton, &QPushButton::clicked, this, [this]() {
        if (!m_sync->start())
            m_statusLabel->setText(tr("Sync already running"));
    });
    connect(m_sync, &MailSync::finished, this, &MainWindow::onSyncFinished);
    connect(m_sync, &MailSync::outputReceived, this, [this](const QString &chunk) {
        m_syncLog->appendPlainText(chunk.trimmed());
    });

    queryRow->addWidget(m_accountBox);
    queryRow->addWidget(m_queryEdit, 1);
    queryRow->addWidget(m_syncButton);
    layout->addLayout(queryRow);

    // Saved query buttons.
    auto *savedRow = new QHBoxLayout;
    for (const SavedQuery &saved : m_config.savedQueries()) {
        auto *button = new QPushButton(saved.name, central);
        connect(button, &QPushButton::clicked, this, [this, saved]() {
            m_queryEdit->setText(saved.query);
            runCurrentQuery();
        });
        savedRow->addWidget(button);
    }
    savedRow->addStretch();
    layout->addLayout(savedRow);

    // Thread list and message pane.
    m_model = new ThreadListModel(this);
    m_threadView = new QTableView(central);
    m_threadView->setModel(m_model);
    m_threadView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_threadView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_threadView->verticalHeader()->hide();
    m_threadView->horizontalHeader()->setStretchLastSection(false);
    m_threadView->horizontalHeader()->setSectionResizeMode(
        ThreadListModel::SubjectColumn, QHeaderView::Stretch);

    connect(m_threadView->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this, &MainWindow::onThreadSelected);

    m_messageView = new MessageView(central);
    connect(m_messageView, &MessageView::statusMessage,
            this, [this](const QString &text) { m_statusLabel->setText(text); });

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->addWidget(m_threadView);
    splitter->addWidget(m_messageView);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    layout->addWidget(m_syncLog);

    setCentralWidget(central);

    resize(1200, 800);
    setWindowTitle(tr("qtmaildir"));
}

void MainWindow::registerActions()
{
    m_actions[QStringLiteral("focus_query")] = [this]() {
        m_queryEdit->setFocus();
        m_queryEdit->selectAll();
    };
    m_actions[QStringLiteral("next_thread")] = [this]() {
        const QModelIndex current = m_threadView->currentIndex();
        const int row = current.isValid() ? current.row() + 1 : 0;
        if (row < m_model->rowCount())
            m_threadView->selectRow(row);
    };
    m_actions[QStringLiteral("prev_thread")] = [this]() {
        const QModelIndex current = m_threadView->currentIndex();
        if (current.isValid() && current.row() > 0)
            m_threadView->selectRow(current.row() - 1);
    };
    m_actions[QStringLiteral("open_thread")] = [this]() {
        m_threadView->setFocus();
    };
    m_actions[QStringLiteral("archive")] = [this]() {
        tagSelected({}, { QStringLiteral("inbox") }, tr("Archive"));
    };
    m_actions[QStringLiteral("delete")] = [this]() {
        tagSelected({ QStringLiteral("deleted") }, {}, tr("Delete"));
    };
    m_actions[QStringLiteral("spam")] = [this]() {
        tagSelected({ QStringLiteral("spam") }, { QStringLiteral("inbox") },
                    tr("Mark spam"));
    };
    m_actions[QStringLiteral("flag")] = [this]() {
        tagSelected({ QStringLiteral("flagged") }, {}, tr("Flag"));
    };
    m_actions[QStringLiteral("toggle_unread")] = [this]() {
        // The direction comes from the current row, but the change applies to
        // the whole selection, so a mixed selection lands in one consistent
        // state rather than each row flipping its own way.
        const QModelIndex current = m_threadView->currentIndex();
        if (!current.isValid())
            return;
        const ThreadSummary thread = m_model->threadAt(current.row());
        if (thread.isUnread())
            tagSelected({}, { QStringLiteral("unread") }, tr("Mark read"));
        else
            tagSelected({ QStringLiteral("unread") }, {}, tr("Mark unread"));
    };
    m_actions[QStringLiteral("toggle_html")] = [this]() {
        m_messageView->toggleHtml();
    };
    m_actions[QStringLiteral("load_remote")] = [this]() {
        m_messageView->loadRemoteContent();
    };
    m_actions[QStringLiteral("undo")] = [this]() {
        if (m_undoStack.canUndo())
            m_undoStack.undo();
        else
            m_statusLabel->setText(tr("Nothing to undo"));
    };
    m_actions[QStringLiteral("sync")] = [this]() {
        if (m_sync->isAvailable())
            m_sync->start();
    };
    m_actions[QStringLiteral("quit")] = [this]() { close(); };

    // The two lists are maintained by hand and a test pins them together; this
    // catches the same drift in a debug run.
    Q_ASSERT(m_actions.size() == registeredActionNames().size());
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

    // A confirmed write clears the pending revert: without this, a later
    // unrelated error would roll back a change that actually succeeded.
    connect(m_worker, &NotmuchWorker::tagsApplied, this, [this](const TagChange &) {
        m_pendingChange = {};
        m_pendingThreadIds.clear();
    });

    m_workerThread.start();
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
    m_statusLabel->setText(tr("%n thread(s)", "", total));
}

void MainWindow::onThreadSelected(const QModelIndex &current,
                                  const QModelIndex &)
{
    if (!current.isValid())
        return;

    m_currentThreadId = m_model->threadAt(current.row()).threadId;
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
    if (m_undoStack.canUndo())
        m_undoStack.undo();
    m_undoStack.clear();

    m_pendingChange = {};
    m_pendingThreadIds.clear();
}

void MainWindow::onWorkerError(const QString &message)
{
    // Spec: the UI updates optimistically and reverts if the write fails.
    // Without this the list would keep showing a tag the database never got.
    revertPendingTagChange();
    m_statusLabel->setText(message);
}

void MainWindow::onSyncFinished(bool success, int exitCode)
{
    if (success) {
        m_statusLabel->setText(tr("Sync complete"));
        runCurrentQuery();
    } else {
        m_statusLabel->setText(tr("Sync failed (exit %1)").arg(exitCode));
        m_syncLog->show();
    }
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

    m_statusLabel->setText(
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress)
        return QMainWindow::eventFilter(watched, event);

    // The query bar must receive ordinary typing, so single-key bindings are
    // suppressed while it has focus.
    if (m_queryEdit->hasFocus())
        return QMainWindow::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    const QKeySequence sequence(keyEvent->keyCombination());

    const QString action = m_keyMap.actionFor(sequence);
    if (action.isEmpty() || !m_actions.contains(action))
        return QMainWindow::eventFilter(watched, event);

    m_actions.value(action)();
    return true;
}
