#pragma once

#include <QHash>
#include <QMainWindow>
#include <QThread>
#include <QUndoCommand>
#include <QUndoStack>

#include <functional>

#include "config.h"
#include "keymap.h"
#include "types.h"

class QLineEdit;
class QTableView;
class QLabel;
class QPushButton;
class QComboBox;
class QPlainTextEdit;

class ThreadListModel;
class MessageView;
class MailSync;
class NotmuchWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const Config &config, QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Every action name registerActions() installs. Exposed so a test can hold
    /// it against KeyMap::knownActions(): the two lists are maintained by hand,
    /// and a drift either way silently breaks a user's key binding.
    static QStringList registeredActionNames();

    /// The cid: namespace prefix for the nth message of a thread.
    ///
    /// MainWindow is the only producer of this value in the application. It
    /// must never contain '!', which is the separator that keeps one message's
    /// cid: references from resolving to another's.
    static QString cidPrefixForIndex(int index);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void runCurrentQuery();
    void onThreadsReady(const QVector<ThreadSummary> &threads, quint64 generation);
    void onQueryFinished(int total, quint64 generation);
    void onThreadSelected(const QModelIndex &current, const QModelIndex &previous);
    void onThreadLoaded(const QVector<MessageRef> &messages, quint64 generation);
    void onWorkerError(const QString &message);
    void onSyncFinished(bool success, int exitCode);

private:
    void buildUi();
    void registerActions();
    void wireWorker();
    void showWarnings();

    void tagSelected(const QStringList &add, const QStringList &remove,
                     const QString &description);

    /// Sends a tag change for a set of threads without touching the undo stack.
    /// Both tagSelected() and ThreadTagCommand route through this.
    void sendThreadTagChange(const QStringList &threadIds,
                             const QStringList &add,
                             const QStringList &remove,
                             const QString &description);

    /// Undoes the optimistic model update for a write the worker rejected.
    void revertPendingTagChange();

    friend class ThreadTagCommand;

    Config m_config;
    KeyMap m_keyMap;

    QThread m_workerThread;
    NotmuchWorker *m_worker = nullptr;

    ThreadListModel *m_model = nullptr;
    MessageView *m_messageView = nullptr;
    MailSync *m_sync = nullptr;
    QUndoStack m_undoStack;

    QLineEdit *m_queryEdit = nullptr;
    QTableView *m_threadView = nullptr;
    QComboBox *m_accountBox = nullptr;
    QPushButton *m_syncButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_syncLog = nullptr;

    QHash<QString, std::function<void()>> m_actions;
    quint64 m_generation = 0;
    QString m_lastQuery;
    QString m_currentThreadId;

    /// The optimistic update awaiting confirmation, kept so a worker error can
    /// put the model back. Only the most recent one: mutations are sent from
    /// the UI thread one user action at a time.
    TagChange m_pendingChange;
    QStringList m_pendingThreadIds;
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
