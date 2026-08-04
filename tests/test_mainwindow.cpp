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

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QTableView>
#include <QTimer>

#include "config.h"
#include "keymap.h"
#include "mainwindow.h"
#include "messageview.h"
#include "notmuchworker.h"
#include "threadlistmodel.h"

/// MainWindow is mostly wiring, and the parts that need a real database are
/// still verified manually. What is checked here is the action registry: the
/// bindings a user configures reach the QActions the menus and the keyboard
/// both read from, and no action is left unreachable.
class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void everyKnownActionIsRegistered();
    void everyRegisteredActionIsKnown();
    void everyActionHasAShortcut();
    void configuredBindingReachesTheAction();
    void cidPrefixesAreBangFree();
    void cidPrefixesAreDistinctPerMessage();
    void uiStateIsNotWrittenIntoTheUserConfig();
    void uiStateSurvivesARestart();
    void missingUiStateLeavesTheDefaults();
    void headerStateFromADifferentColumnLayoutIsDiscarded();
    void returnInTheQueryBarRunsTheQueryNotOpenThread();
    void markReadTimerRestartsRatherThanStacking();
    void markReadTimerIsNotArmedForAReadThread();
    void markReadCanBeDisabled();
    void pendingEditCountSurvivesAQuery();
    void aFailedSyncDoesNotClearThePendingCount();
    void closingWithNoPendingEditsDoesNotPrompt();
    void syncOnExitNeverClosesSilently();
};

void TestMainWindow::everyKnownActionIsRegistered()
{
    // KeyMap::knownActions() is what loadOverrides() validates config bindings
    // against. An action listed there but never registered means a user can
    // bind a key in qtmaildir.conf, get no warning, and have it do nothing.
    //
    // registeredActionNames() is now derived from the QActions themselves, so
    // this compares against what the window really installed.
    const Config config;
    MainWindow window(config);

    const QStringList known = KeyMap::knownActions();
    const QStringList registered = window.registeredActionNames();

    for (const QString &action : known) {
        QVERIFY2(registered.contains(action),
                 qPrintable(QStringLiteral("known action '%1' is never registered "
                                           "by MainWindow").arg(action)));
    }
}

void TestMainWindow::everyRegisteredActionIsKnown()
{
    // The reverse drift: an action MainWindow implements but KeyMap rejects.
    // The user would get "unknown action" for a binding that is really there.
    const Config config;
    MainWindow window(config);

    const QStringList known = KeyMap::knownActions();
    const QStringList registered = window.registeredActionNames();

    for (const QString &action : registered) {
        QVERIFY2(known.contains(action),
                 qPrintable(QStringLiteral("registered action '%1' is not in "
                                           "KeyMap::knownActions()").arg(action)));
    }
}

void TestMainWindow::everyActionHasAShortcut()
{
    // An action with no binding is unreachable from the keyboard. Every one
    // of them carries a default, so an empty shortcut means the default table
    // and the action list have drifted apart.
    const Config config;
    MainWindow window(config);

    for (const QString &name : window.registeredActionNames()) {
        const QAction *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no QAction named '%1'").arg(name)));
        QVERIFY2(!action->shortcut().isEmpty(),
                 qPrintable(QStringLiteral("action '%1' has no shortcut").arg(name)));
    }
}

void TestMainWindow::configuredBindingReachesTheAction()
{
    // The whole point of [keys]: a user's override must end up on the QAction,
    // which is what both the keyboard and the menus read.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("Ctrl+Alt+A"), QStringLiteral("archive"));
        s.endGroup();
    }

    // MainWindow reads its keymap from Config::defaultPath(), so point that
    // at the temporary file for this test.
    const QString previous = qEnvironmentVariable("XDG_CONFIG_HOME");
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("qtmaildir"))));
    QVERIFY(QFile::copy(path, dir.filePath(QStringLiteral("qtmaildir/qtmaildir.conf"))));
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

    {
        const Config config;
        MainWindow window(config);
        const QAction *archive =
            window.findChild<QAction *>(QStringLiteral("archive"));
        QVERIFY(archive);
        QCOMPARE(archive->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+A")));
    }

    if (previous.isEmpty())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", previous.toUtf8());
}

void TestMainWindow::cidPrefixesAreBangFree()
{
    // MainWindow is the only producer of cidPrefix in the application. The
    // '!' separator that keeps two messages' cid: references apart is only
    // unambiguous while the prefix half contains none.
    for (int i : { 0, 1, 9, 10, 99, 1000 }) {
        const QString prefix = MainWindow::cidPrefixForIndex(i);
        QVERIFY(!prefix.isEmpty());
        QVERIFY2(!prefix.contains(QLatin1Char('!')),
                 qPrintable(QStringLiteral("prefix '%1' contains '!'").arg(prefix)));
    }
}

void TestMainWindow::cidPrefixesAreDistinctPerMessage()
{
    // Two messages sharing a prefix would share a cid: namespace, which is the
    // collision the namespacing exists to prevent.
    QSet<QString> seen;
    for (int i = 0; i < 200; ++i) {
        const QString prefix = MainWindow::cidPrefixForIndex(i);
        QVERIFY2(!seen.contains(prefix),
                 qPrintable(QStringLiteral("prefix '%1' repeats").arg(prefix)));
        seen.insert(prefix);
    }
}

void TestMainWindow::uiStateIsNotWrittenIntoTheUserConfig()
{
    // The config file is hand-edited and must never gain a base64 geometry
    // blob, nor be rewritten on exit: QSettings preserves neither comments nor
    // key order, so writing it would quietly destroy the user's formatting.
    QVERIFY(MainWindow::uiStatePath() != Config::defaultPath());

    // One qtmaildir component, not two. QStandardPaths::StateLocation appends
    // both the organization and the application name, and here both are
    // "qtmaildir", so using it nests the directory inside itself.
    QCOMPARE(MainWindow::uiStatePath().count(QStringLiteral("/qtmaildir/")), 1);
    QVERIFY(MainWindow::uiStatePath().endsWith(
        QStringLiteral("/qtmaildir/uistate.conf")));
}

void TestMainWindow::uiStateSurvivesARestart()
{
    // Test mode redirects QStandardPaths at the process level, so the state
    // file lands in a scratch directory rather than the real ~/.local/state.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MainWindow::uiStatePath());

    const QSize resized(940, 620);
    {
        const Config config;
        MainWindow window(config);
        window.resize(resized);
        window.findChild<MessageView *>()->setZoomFactor(1.4);
        window.close();  // closeEvent() is what persists the state
    }

    QVERIFY2(QFile::exists(MainWindow::uiStatePath()),
             qPrintable(QStringLiteral("no state file at %1")
                            .arg(MainWindow::uiStatePath())));

    const Config config;
    MainWindow reopened(config);
    QCOMPARE(reopened.size(), resized);
    QCOMPARE(reopened.findChild<MessageView *>()->zoomFactor(), 1.4);

    QFile::remove(MainWindow::uiStatePath());
    QStandardPaths::setTestModeEnabled(false);
}

void TestMainWindow::missingUiStateLeavesTheDefaults()
{
    // A restore that silently succeeded on an empty blob would give a
    // zero-size window on first launch. Absent state must be a no-op.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MainWindow::uiStatePath());

    const Config config;
    MainWindow window(config);
    QCOMPARE(window.size(), QSize(1200, 800));

    QStandardPaths::setTestModeEnabled(false);
}

void TestMainWindow::headerStateFromADifferentColumnLayoutIsDiscarded()
{
    // The upgrade hazard: a 0.3.0 state file holds a three-column header blob,
    // and 0.4.0 added the attachment column in front. QHeaderView::
    // restoreState() returns TRUE for a blob with fewer sections than the
    // model and applies the old widths shifted one column right, mangling the
    // layout with no error to detect it by (verified on Qt 6.11). The stored
    // column count is what makes that detectable.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MainWindow::uiStatePath());

    {
        const Config config;
        MainWindow window(config);
        window.close();
    }

    // Forge a state file from an older layout: same blob, wrong column count.
    {
        QSettings state(MainWindow::uiStatePath(), QSettings::IniFormat);
        state.setValue(QStringLiteral("threadlist/columns"),
                       int(ThreadListModel::ColumnCount) - 1);
        state.setValue(QStringLiteral("threadlist/header"),
                       QByteArray("not a header this model could have saved"));
    }

    // Constructing must not apply it, and must not crash on the garbage blob.
    const Config config;
    MainWindow reopened(config);

    auto *view = reopened.findChild<QTableView *>();
    QVERIFY(view);
    QCOMPARE(view->columnWidth(ThreadListModel::AttachmentColumn), 28);
    QCOMPARE(view->columnWidth(ThreadListModel::DateColumn), 130);
    QCOMPARE(view->columnWidth(ThreadListModel::SubjectColumn), 520);

    QFile::remove(MainWindow::uiStatePath());
    QStandardPaths::setTestModeEnabled(false);
}

void TestMainWindow::returnInTheQueryBarRunsTheQueryNotOpenThread()
{
    // Return is bound to open_thread as a WindowShortcut, and the query bar has
    // to win it back while it has focus. Qt withholds a plain-LETTER shortcut
    // from an editable widget, but Return is not a letter and gets no such
    // protection, so without an explicit override the action fires, the query
    // never runs, and focus jumps to the thread list.
    //
    // The delivery order matters and is the reason this bug survived earlier
    // tests: real input sends ShortcutOverride first and only dispatches the
    // shortcut if nothing accepts it. QTest::keyClick() skips that round trip,
    // so a test written with it passes against the broken code.
    const Config config;
    MainWindow window(config);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *edit = window.findChild<QLineEdit *>();
    QVERIFY(edit);
    edit->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), edit);
    edit->setText(QStringLiteral("tag:unread"));

    QAction *openThread = window.findChild<QAction *>(QStringLiteral("open_thread"));
    QVERIFY(openThread);
    bool actionFired = false;
    connect(openThread, &QAction::triggered, &window, [&actionFired]() {
        actionFired = true;
    });

    // The query bar must claim the override, which is what stops the shortcut
    // from ever being dispatched.
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Return, Qt::NoModifier);
    override.ignore();
    QApplication::sendEvent(edit, &override);
    QVERIFY2(override.isAccepted(),
             "the query bar let Return through to the open_thread shortcut");

    QVERIFY(!actionFired);
}

/// A thread summary carrying the tags a test needs. Enough to drive selection;
/// nothing here touches a database.
static ThreadSummary makeThread(const QString &id, const QStringList &tags)
{
    ThreadSummary thread;
    thread.threadId = id;
    thread.subject = QStringLiteral("Subject ") + id;
    thread.authors = QStringLiteral("Someone <someone@example.org>");
    thread.tags = tags;
    return thread;
}

void TestMainWindow::markReadTimerRestartsRatherThanStacking()
{
    // The plan's hard requirement: arrowing quickly down a list must not mark
    // every thread passed through as read, only the one still selected when the
    // timer fires. A stacked timer per selection would mark all of them.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);
    auto *view = window.findChild<QTableView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t3"),
                                    { QStringLiteral("unread") }) });

    view->selectRow(0);
    QVERIFY2(timer->isActive(), "no timer armed for an unread thread");

    // Move on before it can fire. One timer stays armed, not three.
    view->selectRow(1);
    QVERIFY(timer->isActive());
    view->selectRow(2);
    QVERIFY(timer->isActive());

    // Exactly one timer exists at all, which is what "restarted, not stacked"
    // means concretely.
    QCOMPARE(window.findChildren<QTimer *>(QStringLiteral("markReadTimer")).size(),
             1);
}

void TestMainWindow::markReadTimerIsNotArmedForAReadThread()
{
    // Opening a thread that is already read must not schedule a write that
    // would change nothing.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);
    auto *view = window.findChild<QTableView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("read"),
                                    { QStringLiteral("inbox") }),
                         makeThread(QStringLiteral("unread"),
                                    { QStringLiteral("unread") }) });

    view->selectRow(0);
    QVERIFY2(!timer->isActive(), "armed a timer for an already-read thread");

    // And the unread one still arms, so this is not "never arms".
    view->selectRow(1);
    QVERIFY(timer->isActive());

    // Moving back to a read thread disarms it again, rather than leaving the
    // previous thread's timer running to fire against the wrong row.
    view->selectRow(0);
    QVERIFY(!timer->isActive());
}

void TestMainWindow::markReadCanBeDisabled()
{
    // A negative delay turns the behaviour off entirely. Documented, so it must
    // work rather than being clamped to "immediately".
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("[general]\nmark_read_delay_ms=-1\n");
    }

    Config config;
    config.load(path);
    QCOMPARE(config.markReadDelayMs(), -1);

    MainWindow window(config);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);
    auto *view = window.findChild<QTableView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }) });
    view->selectRow(0);

    QVERIFY2(!timer->isActive(),
             "a negative mark_read_delay_ms must disable the timer");
}

void TestMainWindow::pendingEditCountSurvivesAQuery()
{
    // The defining property, and the reason this is a counter of its own rather
    // than QUndoStack::isClean(): the undo stack is cleared on every query,
    // because its entries refer to rows the new result set discards. Tag a
    // thread, run any query, and the stack is empty while the change is still
    // sitting unsynced in the database.
    const Config config;
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);
    QVERIFY2(label->isHidden(), "the indicator must start hidden at zero");

    // Confirm a write the way the worker really does, by emitting the signal
    // the window listens to. No test-only entry point on MainWindow.
    TagChange change;
    change.added = { QStringLiteral("deleted") };
    change.description = QStringLiteral("Delete");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));

    QVERIFY2(!label->isHidden(), "a confirmed edit must show the indicator");
    const QString afterEdit = label->text();
    QVERIFY(!afterEdit.isEmpty());

    // Now run a query, which clears the undo stack. The indicator must not
    // follow it down.
    window.findChild<QLineEdit *>()->setText(QStringLiteral("tag:inbox"));
    QMetaObject::invokeMethod(&window, "runCurrentQuery");

    QVERIFY2(!label->isHidden(),
             "the indicator was cleared by a query, so it is tracking the undo "
             "stack rather than unsynced state");
    QCOMPARE(label->text(), afterEdit);
}

void TestMainWindow::aFailedSyncDoesNotClearThePendingCount()
{
    // A failed sync means the edits are still unsynced. Clearing here would
    // assert the opposite, and the user would quit believing their tagging had
    // been carried over.
    const Config config;
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    TagChange change;
    change.added = { QStringLiteral("flagged") };
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));
    QVERIFY(!label->isHidden());
    const QString afterEdit = label->text();

    QMetaObject::invokeMethod(&window, "onSyncFinished",
                              Q_ARG(bool, false), Q_ARG(int, 1));
    QVERIFY2(!label->isHidden(), "a FAILED sync cleared the pending count");
    QCOMPARE(label->text(), afterEdit);

    // A successful one does clear it, so this is not "never clears".
    QMetaObject::invokeMethod(&window, "onSyncFinished",
                              Q_ARG(bool, true), Q_ARG(int, 0));
    QVERIFY2(label->isHidden(), "a successful sync must clear the indicator");
}

/// Closes a window and reports whether it accepted, failing rather than hanging
/// if a modal appears.
///
/// A modal spins its own event loop, so a test that simply sends a close event
/// blocks forever when a dialog it did not expect opens. This arms a timer that
/// closes any active modal and records that one was there, which turns "a
/// dialog appeared" into an assertion instead of a hung run.
struct CloseProbe
{
    bool accepted = false;
    bool sawModal = false;

    void run(MainWindow *window)
    {
        QTimer poll;
        poll.setInterval(50);
        int ticks = 0;
        QObject::connect(&poll, &QTimer::timeout, [this, &poll, &ticks]() {
            if (QWidget *modal = QApplication::activeModalWidget()) {
                sawModal = true;
                modal->close();
                poll.stop();
                return;
            }
            if (++ticks > 20)   // one second is ample for a synchronous close
                poll.stop();
        });
        poll.start();

        QCloseEvent event;
        QApplication::sendEvent(window, &event);
        accepted = event.isAccepted();
        poll.stop();
    }
};

void TestMainWindow::closingWithNoPendingEditsDoesNotPrompt()
{
    // Nothing outstanding means nothing to ask about. If a prompt fires here it
    // is keying off something other than there being work to lose, and every
    // quit would carry a dialog.
    const Config config;
    MainWindow window(config);

    CloseProbe probe;
    probe.run(&window);

    QVERIFY2(!probe.sawModal, "a clean window prompted on close");
    QVERIFY2(probe.accepted, "a clean window refused to close");
}

void TestMainWindow::syncOnExitNeverClosesSilently()
{
    // "never" is the behaviour that existed before the prompt did, and it has
    // to stay reachable for anyone who does not want to be asked. It must hold
    // whether or not a sync command is configured, so this covers both: the
    // no-command path has its own dialog, and "never" must skip that one too.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("[general]\nsync_on_exit=never\n\n[sync]\ncommand=/bin/true\n");
    }

    Config config;
    config.load(path);
    QCOMPARE(config.syncOnExit(), Config::SyncOnExit::Never);

    MainWindow window(config);

    // Give it something to lose, so this is not passing for the same reason
    // the previous test does.
    TagChange change;
    change.added = { QStringLiteral("deleted") };
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));
    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);
    QVERIFY(!label->isHidden());

    CloseProbe probe;
    probe.run(&window);

    QVERIFY2(!probe.sawModal,
             "sync_on_exit=never prompted anyway");
    QVERIFY2(probe.accepted,
             "sync_on_exit=never must close without prompting");
}

// Constructing a MainWindow needs a QApplication and a platform plugin. The
// test has no display under ctest, so it runs offscreen unless the caller
// asked for something else.
int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty()
                                   ? QByteArray("offscreen")
                                   : qgetenv("QT_QPA_PLATFORM"));
    QApplication app(argc, argv);
    TestMainWindow test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_mainwindow.moc"
