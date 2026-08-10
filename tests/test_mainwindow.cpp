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
#include <QMenu>
#include <QPushButton>
#include <QProgressBar>
#include <QFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QStyle>
#include <QSplitter>
#include <QTableView>
#include <QToolBar>
#include <QTreeView>
#include <QTimer>

#include "config.h"
#include "keymap.h"
#include "mainwindow.h"
#include "messageview.h"
#include "notmuchworker.h"
#include "tagchip.h"
#include "threadlistmodel.h"
#include "threadlistview.h"

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
    void splitterStateFromAWiderWindowKeepsTheMessagePane();
    void usableSplitterStateIsRestoredUntouched();
    void headerStateFromADifferentColumnLayoutIsDiscarded();
    void returnInTheQueryBarRunsTheQueryNotOpenThread();
    void markReadTimerRestartsRatherThanStacking();
    void markReadTimerIsNotArmedForAReadThread();
    void markReadCanBeDisabled();
    void pendingEditCountSurvivesAQuery();
    void aFailedSyncDoesNotClearThePendingCount();
    void closingWithNoPendingEditsDoesNotPrompt();
    void syncOnExitNeverClosesSilently();
    void selectAllIsBoundAndSelectsEveryRow();
    void aMultiRowSelectionDoesNotArmTheMarkReadTimer();
    void growingASelectionCancelsAnAlreadyArmedTimer();
    void collapsingBackToOneRowLoadsThatThreadAgain();
    void theStatusBarReportsAMultiRowSelection();
    void clearSelectionBlanksThePaneAndDeselects();
    void clearPaneLeavesTheSelectionAlone();
    void maildirOverviewShowsUnknownRatherThanZero();
    void maildirOverviewIgnoresAStaleReply();
    void theThreadListOffersAContextMenu();
    void aSecondRowBlanksThePaneNotOnlyAThird();
    void aLocalSyncIsNotReportedAsABackgroundOne();
    void aLocalSyncsOwnLockIsNeverReportedAsBackground();
    void aSkippedLocalSyncStillReportsTheOtherRunFinishing();
    void anUnobservableLockTableLeavesTheSyncButtonUsable();
    void theStatusBarFollowsTheSyncPhase();
    void aSelectedReadThreadIsNotDimmedIntoTheHighlight();
    void thePillRowSpansTheWholeWidthNotOneColumn();
    void childRowsAreIndentedUnderTheirThread();
    void aThreadWithRepliesDrawsAVisibleExpander();
    void noTagStripIsPaintedUnderAMessageRow();
    void replyRowsKeepTheirTextUnderTheThreadLine();
    void clickingTheExpanderTogglesTheThread();
    void selectingAMessageRowTargetsThatMessageNotItsThread();
    void selectingAThreadRowNamesHowManyMessagesItStandsFor();
    void selectingAMessageRowReportsNoBulkCount();
    void anActionOnAThreadRowSaysItHitTheWholeThread();
    void anActionOnAMessageRowTagsThatMessageNotTheThread();
    void markAllReadIsDisabledUntilTheQueryFinishes();
    void markAllReadActsOnEveryRowAndUndoesInOneStep();
    void markAllReadDoesNothingWhenNothingIsUnread();
    void theSyncActionIsDisabledWhileABackgroundSyncHoldsTheLock();
    void escapeBlanksTheMessagePane();
    void deleteTogglesOnAnAlreadyDeletedThread();
    void deleteOnAMixedSelectionDeletesRatherThanSplittingIt();
    void aTransientStatusMessageExpires();
    void theSelectionCountIsStateAndDoesNotExpire();
    void anEditUndoneNettsBackToZero();
    void aDifferentTagOnTheSameMessageStillCounts();
    void anEditWithNoMessageIdsStillCounts();
    void anEditDuringABackgroundSyncIsNotSentYet();
    void aHeldEditIsSentWhenTheBackgroundSyncEnds();
    void aHeldEditCountsAsUnsynced();
    void anUnreadableLockTableStillSendsTheEdit();
    void aRejectedWriteKeepsEarlierUndoHistory();

    void aSuccessfulCronSyncClearsThePendingCount();
    void aFailedCronSyncLeavesThePendingCount();
    void anUnreadableSyncLogLeavesThePendingCount();
    void anUnknownExternalStateClearsNothing();
    void aSuccessfulCronSyncDrainsTheEditedAccounts();
    void aCronSyncDoesNotClearAnEditMadeWhileItRan();

    void everyActionCarriesAnIcon();
    void theToolbarDoesNotOverrideTheDesktopButtonStyle();
    void theImportantActionIsLabelledImportant();
    void theImportantActionStillWritesTheFlaggedTag();
    void theToolbarUsesTheConfiguredIconSize();
    void noTwoActionsShareAnIcon();
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

    // Must fit the smallest screen this ever runs against: the offscreen
    // platform reports 800x800, and restoreGeometry() clamps to the available
    // area, so a 940px width came back as 798 and failed only under offscreen.
    // The number carries no meaning beyond differing from the default size.
    const QSize resized(640, 560);
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

void TestMainWindow::splitterStateFromAWiderWindowKeepsTheMessagePane()
{
    // The reported symptom, from a real state file: a splitter position saved
    // at 1285/1252 and restored into a 1136px window. QSplitter honours the
    // first size and gives the second what is left, so the message pane came
    // back roughly 30px wide, a sliver of rendered mail against a full-width
    // thread list. It is not a first-run problem, which is why widening the
    // default window would not have helped: the wider the window ever was, the
    // worse the next narrower session is.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MainWindow::uiStatePath());

    {
        const Config config;
        MainWindow window(config);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto *splitter = window.findChild<QSplitter *>();
        QVERIFY(splitter);
        // Guard: the forged sizes have to be genuinely wider than the window
        // this reopens into, or the test proves nothing.
        QVERIFY(splitter->width() > 0);
        // Times four, not times two: the offscreen platform chooses the window
        // width itself and has been seen to choose differently between runs of
        // this binary, so a margin that only just exceeds THIS window's width
        // can fail to exceed the reopened one's and quietly stop reproducing
        // the bug.
        const int overwide = splitter->width() * 4;
        splitter->setSizes({ overwide, overwide });
        window.close();
    }

    const Config config;
    MainWindow reopened(config);
    reopened.show();
    QVERIFY(QTest::qWaitForWindowExposed(&reopened));

    auto *splitter = reopened.findChild<QSplitter *>();
    QVERIFY(splitter);
    const QList<int> sizes = splitter->sizes();
    QCOMPARE(sizes.size(), 2);

    // Asserted against the pane's OWN minimum rather than a number repeated
    // here: the floor is private to MainWindow, and a literal copy would keep
    // passing if the shipped one were lowered back to a width mail cannot be
    // read at. The guard below is what stops that from being vacuous.
    auto *pane = reopened.findChild<MessageView *>();
    QVERIFY(pane);
    QVERIFY2(pane->minimumWidth() >= 300,
             qPrintable(QStringLiteral("message pane floor is %1px")
                            .arg(pane->minimumWidth())));
    QVERIFY2(sizes.at(1) >= pane->minimumWidth(),
             qPrintable(QStringLiteral("message pane restored %1px wide in a "
                                       "%2px splitter")
                            .arg(sizes.at(1))
                            .arg(splitter->width())));

    reopened.close();
    QFile::remove(MainWindow::uiStatePath());
    QStandardPaths::setTestModeEnabled(false);
}

void TestMainWindow::usableSplitterStateIsRestoredUntouched()
{
    // The rescue must not fire on a position that fits. This is item 1's whole
    // point: a splitter the user dragged comes back where they left it.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MainWindow::uiStatePath());

    QList<int> saved;
    {
        const Config config;
        MainWindow window(config);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        auto *splitter = window.findChild<QSplitter *>();
        QVERIFY(splitter);
        // Lopsided but both panes usable, so nothing should touch it. Sized as
        // a fraction rather than "width minus 250": this window's width is the
        // platform's to choose and has been seen to differ between two runs of
        // the same binary, so an absolute split saved here can arrive too wide
        // for the reopened window and trip the rescue the test is asserting
        // does NOT fire.
        const int total = splitter->width();
        QVERIFY(total > 500);
        splitter->setSizes({ total / 4, total - total / 4 });
        saved = splitter->sizes();
        QVERIFY(saved.at(0) < saved.at(1));
        window.close();
    }

    const Config config;
    MainWindow reopened(config);
    reopened.show();
    QVERIFY(QTest::qWaitForWindowExposed(&reopened));

    auto *splitter = reopened.findChild<QSplitter *>();
    QVERIFY(splitter);
    // The saved ratio, not the saved pixels: QSplitter redistributes to the
    // current width, and only a rescue would flip which pane is the larger.
    // The FIRST pane's saved pixel width is what must survive: QSplitter
    // restores it verbatim and gives the second whatever the current window
    // leaves, so this is the value a rescue would overwrite and the only one
    // that does not move with the window width. Which matters here, because
    // the offscreen platform picks that width itself and has been seen to
    // pick differently between two runs of this same binary (1181 and 779),
    // so any assertion on a ratio or on pane 1 is checking the platform's
    // mood rather than this code.
    const QList<int> sizes = splitter->sizes();
    QCOMPARE(sizes.at(0), saved.at(0));

    reopened.close();
    QFile::remove(MainWindow::uiStatePath());
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

    auto *view = reopened.findChild<QTreeView *>();
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

/// Selects a top-level THREAD row, replacing QTableView::selectRow which a
/// QTreeView does not have.
///
/// Not merely a rename: setCurrentIndex alone leaves the selection model empty,
/// and select() alone leaves current invalid, so every test asserting on either
/// would break in a different way. Both are set here, exactly as
/// QTableView::selectRow did.
static void selectThreadRow(QTreeView *view, int row)
{
    const QModelIndex index = view->model()->index(row, 0, QModelIndex());
    view->selectionModel()->select(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(index);
}

/// The height of a top-level row, replacing QTableView::rowHeight(int).
static int threadRowHeight(QTreeView *view, int row)
{
    return view->visualRect(view->model()->index(row, 0, QModelIndex())).height();
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

void TestMainWindow::thePillRowSpansTheWholeWidthNotOneColumn()
{
    // The pills are a row-wide strip under the cells, not content of the
    // subject cell. Drawn from the subject column's delegate they stop at that
    // column's edge, so a thread with several tags loses the last of them; and
    // they inherit the column's left edge, which puts them under the subject
    // rather than under the row.
    //
    // The property: pills appear to the LEFT of where the subject column
    // starts, which no per-cell delegate on that column could produce.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    ThreadSummary thread = makeThread(QStringLiteral("t1"), {});
    thread.tags = QStringList{ QStringLiteral("mailing-list/SBo"),
                               QStringLiteral("signed") };
    model->appendBatch({ thread });

    window.resize(1400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QApplication::processEvents();

    const int subjectLeft =
        view->columnViewportPosition(ThreadListModel::SubjectColumn);
    QVERIFY2(subjectLeft > 40,
             qPrintable(QStringLiteral("the subject column starts at x=%1, too "
                                       "close to the left edge to tell a "
                                       "row-wide strip from a subject-cell one")
                            .arg(subjectLeft)));
    // The strip must have somewhere to paint that the subject cell does not
    // reach, or this test cannot fail.
    QVERIFY2(subjectLeft < view->viewport()->width(),
             qPrintable(QStringLiteral("the subject column is off-screen "
                                       "(x=%1, viewport %2), so nothing it "
                                       "draws is measurable")
                            .arg(subjectLeft)
                            .arg(view->viewport()->width())));

    QImage shot(view->viewport()->size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    view->viewport()->render(&shot);

    // Count pixels matching the tag colours EXACTLY, not "saturated" pixels.
    // A looser test counts the antialiased edge of the selection highlight
    // blending into the background, which is several hundred distinct
    // near-background colours and passes whatever the strip does. Both earlier
    // versions of this test did precisely that.
    QSet<QRgb> pillColours;
    const QVariantList colours =
        model->index(0, ThreadListModel::SubjectColumn)
            .data(ThreadListModel::PillColoursRole).toList();
    QVERIFY2(!colours.isEmpty(), "the model supplied no pill colours");
    for (const QVariant &colour : colours)
        pillColours.insert(colour.value<QColor>().rgb());

    const int rowHeight = threadRowHeight(view, 0);
    QVERIFY(rowHeight > 0);

    int chipPixels = 0;
    for (int y = 0; y < qMin(rowHeight, shot.height()); ++y) {
        for (int x = 0; x < qMin(subjectLeft, shot.width()); ++x) {
            if (pillColours.contains(shot.pixel(x, y) | 0xff000000))
                ++chipPixels;
        }
    }

    QVERIFY2(chipPixels > 0,
             "no pill-coloured pixels left of the subject column: the strip is "
             "still confined to that cell rather than spanning the row");
}

void TestMainWindow::childRowsAreIndentedUnderTheirThread()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY2(view, "the thread list is not a QTreeView, so it cannot indent");

    // With an account tag, so the thread row draws the chip that a reply row
    // does not. That asymmetry is the whole reason the indent has to be wide,
    // and a test against an untagged thread never sees it.
    model->appendBatch({ makeThread(
        QStringLiteral("t1"),
        QStringList{ TagColors::tagForAccountKey(QStringLiteral("work")) }) });

    MessageNode first;
    first.messageId = QStringLiteral("m0@example.org");
    first.threadId = QStringLiteral("t1");
    first.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("t1");
    reply.from = QStringLiteral("A Replier <replier@example.org>");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t1"), { first, reply });

    window.resize(1400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QModelIndex root = model->index(0, 0, QModelIndex());
    view->expand(root);
    QApplication::processEvents();

    // Measured on the TREE POSITION column, not on column 0. A QTreeView
    // indents only the column carrying the expander, verified against Qt 6.11:
    // with setTreePosition(4), column 0 reports the same left edge for a thread
    // and its reply (0 and 0) while column 4 reports 420 and 440. Asserting on
    // column 0 therefore fails against a perfectly indented tree.
    const int treeColumn = ThreadListModel::SubjectColumn;
    const QModelIndex rootCell = model->index(0, treeColumn, QModelIndex());
    const QModelIndex child = model->index(0, treeColumn, root);
    QVERIFY(child.isValid());

    // Guards before the claim: a probe that cannot see both rows can report
    // anything it likes about their relative position.
    QVERIFY2(view->visualRect(rootCell).height() > 0,
             "the thread row has no height, so nothing about it is measurable");
    QVERIFY2(view->visualRect(child).height() > 0,
             "the reply row has no height: it is collapsed or off-screen, and "
             "an indent test against it would pass without drawing anything");

    QVERIFY2(view->visualRect(child).left() > view->visualRect(rootCell).left(),
             "the reply is not indented relative to its thread");

    // The geometry being indented is NOT the same as the reply LOOKING
    // indented, and asserting only the former shipped a build with no visible
    // nesting at all. A thread row draws an account chip before its subject and
    // a reply row does not, so the reply's text starts about a chip's width to
    // the left of the thread's; at Qt's default 20px indent that difference
    // swallows the shift entirely.
    //
    // So the real property: where the TEXT lands. The reply's subject must
    // begin to the right of the thread's, which is what the eye reads as
    // nesting.
    const int chipWidth =
        TagChip::sizeFor(QFontMetrics(view->font()),
                         model->data(rootCell, ThreadListModel::AccountLabelRole)
                             .toString()).width();
    QVERIFY2(chipWidth > 0,
             "the thread row has no account chip, so this test cannot measure "
             "the offset it is meant to compensate for");

    const int threadTextLeft = view->visualRect(rootCell).left() + chipWidth;
    QVERIFY2(view->visualRect(child).left() > threadTextLeft,
             qPrintable(QStringLiteral("the reply's text starts at x=%1, not "
                                       "right of the thread's text at x=%2: the "
                                       "indent does not beat the account chip "
                                       "and the nesting is invisible")
                            .arg(view->visualRect(child).left())
                            .arg(threadTextLeft)));
}

void TestMainWindow::aThreadWithRepliesDrawsAVisibleExpander()
{
    // The expander is the ONLY thing saying a thread can be opened, and it took
    // four wrong attempts to get on screen, each of which looked correct in
    // code:
    //
    //   - QTreeView::drawBranches, the documented hook, runs BEFORE the row's
    //     cells, so with the expander on a content column the delegate's own
    //     background paints over it. A 60-pixel triangle survived as 8.
    //   - Sizing it from the row rather than the branch rect put most of it
    //     outside that rect.
    //   - Moving it into the delegate but calling it from only one of the two
    //     branches left every real row without one, since every real row has an
    //     account chip and takes the other branch.
    //
    // None of those is visible to a test that asserts on geometry or on model
    // roles, so this one counts painted pixels of the palette colour the glyph
    // is drawn in.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    // Two threads: one with replies, one without. The second is the control,
    // and without it a test that counts text pixels would pass on any row.
    ThreadSummary withReplies = makeThread(
        QStringLiteral("t1"),
        QStringList{ TagColors::tagForAccountKey(QStringLiteral("work")) });
    withReplies.totalCount = 3;
    ThreadSummary lone = makeThread(
        QStringLiteral("t2"),
        QStringList{ TagColors::tagForAccountKey(QStringLiteral("work")) });
    lone.totalCount = 1;
    model->appendBatch({ withReplies, lone });

    window.resize(1400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QApplication::processEvents();

    const QModelIndex first =
        model->index(0, ThreadListModel::SubjectColumn, QModelIndex());
    const QModelIndex second =
        model->index(1, ThreadListModel::SubjectColumn, QModelIndex());

    // Guards: both rows on screen, and the model agreeing about which has
    // replies. Without these a zero count could mean anything.
    QVERIFY2(view->visualRect(first).height() > 0, "the first row is not drawn");
    QVERIFY2(view->visualRect(second).height() > 0,
             "the control row is not drawn");
    QVERIFY(model->data(first, ThreadListModel::HasRepliesRole).toBool());
    QVERIFY(!model->data(second, ThreadListModel::HasRepliesRole).toBool());

    QImage shot(view->viewport()->size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    view->viewport()->render(&shot);

    // The exact colour the glyph is filled with, matched exactly rather than by
    // a brightness threshold, which would count antialiased subject text.
    const QRgb glyph = view->palette().color(QPalette::Text).rgb();

    // Only the strip in front of the subject text, so the subject's own glyphs
    // cannot be counted. kExpanderWidth is the room the delegate reserves.
    const auto countGlyphPixels = [&](const QModelIndex &index) {
        const QRect rect = view->visualRect(index);
        int found = 0;
        for (int y = rect.top(); y < qMin(rect.bottom(), shot.height()); ++y) {
            for (int x = rect.left();
                 x < qMin(rect.left() + SubjectDelegate::kExpanderWidth,
                          shot.width());
                 ++x) {
                if ((shot.pixel(x, y) | 0xff000000) == (glyph | 0xff000000))
                    ++found;
            }
        }
        return found;
    };

    const int drawn = countGlyphPixels(first);
    const int control = countGlyphPixels(second);

    QVERIFY2(drawn > 12,
             qPrintable(QStringLiteral("only %1 expander pixels: the glyph is "
                                       "clipped or painted over, which is how "
                                       "it shipped as an invisible dot")
                            .arg(drawn)));

    // The control must have none, or the count above is measuring something
    // every row draws.
    QCOMPARE(control, 0);
}

void TestMainWindow::selectingAThreadRowNamesHowManyMessagesItStandsFor()
{
    // With two kinds of row selectable, one selected row no longer says how
    // much an action will touch. CLAUDE.md forbids a confirmation dialog for
    // tag mutations, so the scope is made visible instead: this is the "before"
    // half of that, and the count has to come from the thread's own total, not
    // from whatever happens to be expanded.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    ThreadSummary t = makeThread(QStringLiteral("t1"), {});
    t.totalCount = 7;
    model->appendBatch({ t });

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Guard: nothing is expanded, so a count taken from the loaded children
    // would read 0 and this test would be measuring the wrong source.
    QCOMPARE(model->rowCount(model->index(0, 0, QModelIndex())), 0);

    selectThreadRow(view, 0);
    QApplication::processEvents();

    QVERIFY2(status->text().contains(QStringLiteral("7")),
             qPrintable(QStringLiteral("the status bar says '%1', which does "
                                       "not name the 7 messages the thread "
                                       "stands for")
                            .arg(status->text())));
}

void TestMainWindow::selectingAMessageRowReportsNoBulkCount()
{
    // Reading one message is not a bulk action, so it gets no count. A message
    // row reporting "1 thread selected" would be actively wrong about what an
    // action would touch.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    ThreadSummary t = makeThread(QStringLiteral("t1"), {});
    t.totalCount = 3;
    model->appendBatch({ t });

    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("t1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("t1");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t1"), { root, reply });

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QModelIndex threadRow = model->index(0, 0, QModelIndex());
    view->expand(threadRow);
    QApplication::processEvents();

    const QModelIndex messageRow = model->index(0, 0, threadRow);
    QVERIFY(model->isMessageRow(messageRow));

    view->selectionModel()->select(
        messageRow,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(messageRow);
    QApplication::processEvents();

    QVERIFY2(!status->text().contains(QStringLiteral("thread")),
             qPrintable(QStringLiteral("a single message row reports '%1', "
                                       "which claims a thread-wide scope it "
                                       "does not have")
                            .arg(status->text())));
}

void TestMainWindow::anActionOnAThreadRowSaysItHitTheWholeThread()
{
    // The "after" half. Undo is the safety net this project chose over a
    // confirmation dialog, and undo is only usable if the user can tell that
    // something bigger than they intended just happened.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    ThreadSummary t = makeThread(QStringLiteral("t1"), {});
    t.totalCount = 7;
    model->appendBatch({ t });

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    selectThreadRow(view, 0);
    QApplication::processEvents();

    auto *archive = window.findChild<QAction *>(QStringLiteral("archive"));
    QVERIFY2(archive, "no archive action to trigger");
    archive->trigger();

    // Read BEFORE processEvents, deliberately. This binary has no worker
    // (backlog item 36), so the queued applyTagsToThreads reaches a throwaway
    // database that has never heard of thread t1 and answers with
    // errorOccurred, which overwrites the status bar. Draining the event loop
    // here would assert on that error rather than on the scope message, and
    // the test would fail against correct code.
    const QString message = status->text();

    QVERIFY2(message.contains(QStringLiteral("7")),
             qPrintable(QStringLiteral("after archiving a 7-message thread the "
                                       "status bar says '%1', which does not "
                                       "say how much was touched")
                            .arg(message)));

    // And it must say the whole thread went, not merely how many messages: the
    // count alone does not distinguish "7 messages you picked" from "7 messages
    // because you picked their thread".
    QVERIFY2(message.contains(QStringLiteral("whole thread")),
             qPrintable(QStringLiteral("the status bar says '%1', which does "
                                       "not say the action took the whole "
                                       "thread")
                            .arg(message)));
}

void TestMainWindow::anActionOnAMessageRowTagsThatMessageNotTheThread()
{
    // The routing itself, which nothing else here can see. A message row sent
    // down the THREAD path produces the same undo depth and the same status
    // text while tagging every sibling in the conversation: a mutation that did
    // exactly that passed the entire suite, so this test exists because that
    // gap was found rather than because the path looked risky.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    ThreadSummary t = makeThread(QStringLiteral("t1"), {});
    t.totalCount = 3;
    model->appendBatch({ t });

    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("t1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("t1");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t1"), { root, reply });

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QModelIndex threadRow = model->index(0, 0, QModelIndex());
    view->expand(threadRow);
    QApplication::processEvents();

    const QModelIndex messageRow = model->index(0, 0, threadRow);
    QVERIFY(model->isMessageRow(messageRow));

    view->selectionModel()->select(
        messageRow,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(messageRow);
    QApplication::processEvents();

    auto *archive = window.findChild<QAction *>(QStringLiteral("archive"));
    QVERIFY(archive);
    archive->trigger();

    // The change must carry the MESSAGE id and no thread id. Sent as a thread
    // id it would archive the root and every other reply along with it.
    QCOMPARE(window.pendingMessageIdsForTesting(),
             QStringList{ QStringLiteral("m1@example.org") });
    QVERIFY2(window.pendingThreadIdsForTesting().isEmpty(),
             qPrintable(QStringLiteral("the action was sent for thread(s) %1: a "
                                       "message row must not tag its siblings")
                            .arg(window.pendingThreadIdsForTesting()
                                     .join(QStringLiteral(", ")))));

    // And it is undoable, on its own terms rather than the thread's.
    QCOMPARE(window.undoDepthForTesting(), 1);
}

void TestMainWindow::selectingAMessageRowTargetsThatMessageNotItsThread()
{
    // test_mainwindow has no worker (backlog item 36), so this cannot assert on
    // what the pane renders. What it CAN assert is the decision the UI makes:
    // a message row must stop tracking a current thread, or a reply arriving
    // for either kind of selection cannot tell which one it belongs to.
    //
    // The trap this covers is specific. threadAt() takes a TOP-LEVEL row
    // number, and a child's row number indexes its siblings, so handing a
    // message row's number to it loads whichever thread happens to sit at that
    // position in the list. Row 0 under a thread is a plausible-looking wrong
    // answer, which is why the fixture puts the reply under the SECOND thread.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    ThreadSummary first = makeThread(QStringLiteral("t1"), {});
    ThreadSummary second = makeThread(QStringLiteral("t2"), {});
    second.totalCount = 2;
    model->appendBatch({ first, second });

    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("t2");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("t2");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t2"), { root, reply });

    window.resize(1400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Start on a thread row, so the transition to a message row is what is
    // being observed rather than the initial state.
    const QModelIndex threadRow = model->index(1, 0, QModelIndex());
    selectThreadRow(view, 1);
    QApplication::processEvents();
    QCOMPARE(window.currentThreadId(), QStringLiteral("t2"));

    view->expand(threadRow);
    QApplication::processEvents();

    const QModelIndex messageRow = model->index(0, 0, threadRow);
    QVERIFY(messageRow.isValid());
    QVERIFY2(model->isMessageRow(messageRow),
             "the fixture did not produce a message row, so this test would "
             "assert nothing about one");

    view->selectionModel()->select(
        messageRow,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(messageRow);
    QApplication::processEvents();

    // The thread is no longer what the pane is about. Left set, a late
    // loadThread reply would repaint the whole conversation over the single
    // message the user asked for.
    QVERIFY2(window.currentThreadId().isEmpty(),
             qPrintable(QStringLiteral("selecting a reply left the current "
                                       "thread set to '%1': the pane is still "
                                       "tracking the conversation")
                            .arg(window.currentThreadId())));
}

void TestMainWindow::clickingTheExpanderTogglesTheThread()
{
    // The glyph being VISIBLE and the glyph being CLICKABLE are separate
    // properties, and the pixel test for the first passes happily against a
    // triangle nothing can hit. Turning off rootIsDecorated to stop the style
    // drawing its own dot under ours also removed the style's hit area, so the
    // expander rendered perfectly and did nothing.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    ThreadSummary t = makeThread(
        QStringLiteral("t1"),
        QStringList{ TagColors::tagForAccountKey(QStringLiteral("work")) });
    t.totalCount = 3;
    model->appendBatch({ t });

    window.resize(1400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QApplication::processEvents();

    const QModelIndex root = model->index(0, 0, QModelIndex());
    const QModelIndex subject =
        model->index(0, ThreadListModel::SubjectColumn, QModelIndex());
    const QRect rect = view->visualRect(subject);

    // Guards: the row is drawn, it claims to have replies, and it starts
    // collapsed. Without the last one a toggle test can pass by doing nothing.
    QVERIFY2(rect.height() > 0, "the thread row is not on screen");
    QVERIFY(model->data(subject, ThreadListModel::HasRepliesRole).toBool());
    QVERIFY(!view->isExpanded(root));

    // Aimed at the glyph itself: the delegate reserves kExpanderWidth at the
    // left of the subject cell and centres the triangle in it.
    const QPoint hit(rect.left() + SubjectDelegate::kExpanderWidth / 2,
                     rect.top() + SubjectDelegate::kRowPadding
                         + QFontMetrics(view->font()).height() / 2);

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, hit);
    QApplication::processEvents();
    QVERIFY2(view->isExpanded(root),
             "clicking the expander did not open the thread");

    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, hit);
    QApplication::processEvents();
    QVERIFY2(!view->isExpanded(root),
             "clicking the expander again did not close the thread");
}

void TestMainWindow::replyRowsKeepTheirTextUnderTheThreadLine()
{
    // paintEvent runs AFTER the cells, so anything it fills across a reply row
    // covers the text the delegate just drew. The tint and the thread line are
    // both painted there, which makes this the obvious way to ship a block of
    // blank rows.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    ThreadSummary t = makeThread(
        QStringLiteral("t1"),
        QStringList{ TagColors::tagForAccountKey(QStringLiteral("work")) });
    t.totalCount = 2;
    model->appendBatch({ t });

    MessageNode first;
    first.messageId = QStringLiteral("m0@example.org");
    first.threadId = QStringLiteral("t1");
    first.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("t1");
    reply.from = QStringLiteral("A Replier <replier@example.org>");
    reply.subject = QStringLiteral("Re: a subject");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t1"), { first, reply });

    window.resize(1400, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QModelIndex root = model->index(0, 0, QModelIndex());
    view->expand(root);
    QApplication::processEvents();

    const QModelIndex child =
        model->index(0, ThreadListModel::AuthorsColumn, root);
    const QRect rect = view->visualRect(child);
    QVERIFY2(rect.height() > 0, "the reply row is not on screen");

    QImage shot(view->viewport()->size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    view->viewport()->render(&shot);

    // Count pixels in the sender cell that differ from the row's own tint.
    // Text is the only thing that can produce them.
    const QRgb tint = ThreadListModel::replyBackground().rgb() | 0xff000000;
    int textPixels = 0;
    for (int y = rect.top(); y < qMin(rect.bottom(), shot.height()); ++y) {
        for (int x = rect.left(); x < qMin(rect.right(), shot.width()); ++x) {
            if ((shot.pixel(x, y) | 0xff000000) != tint)
                ++textPixels;
        }
    }

    QVERIFY2(textPixels > 20,
             qPrintable(QStringLiteral("only %1 non-background pixels in the "
                                       "reply's sender cell: the row was "
                                       "painted over after its text was drawn")
                            .arg(textPixels)));
}

void TestMainWindow::noTagStripIsPaintedUnderAMessageRow()
{
    // The strip is a row-wide band of the THREAD's tags. Painted under every
    // reply as well it would stripe the list and repeat identical tags down the
    // whole expansion.
    //
    // TWO independent guards stop that, and this test is aimed at the SECOND:
    // the model returns no pills for a child row, and the view skips child rows
    // in its walk. Asserting against the real model tests only the first, and
    // the view's guard can be deleted without the test noticing: verified by
    // mutation, which passed with the skip removed. So the model is replaced
    // here by one that hands out pills for EVERY row, thread and reply alike,
    // leaving the view's own skip as the only thing that can keep the reply
    // rows clean.
    /// Hands out the same pills for a message row as for a thread row, which
    /// the real model never does. Without this the view's skip is unobservable.
    class PillsEverywhereModel : public ThreadListModel
    {
    public:
        QVariant data(const QModelIndex &index, int role) const override
        {
            if (role == PillTagsRole) {
                return QStringList{ QStringLiteral("mailing-list/SBo"),
                                    QStringLiteral("signed") };
            }
            if (role == PillColoursRole) {
                return QVariantList{ QVariant::fromValue(QColor(Qt::magenta)),
                                     QVariant::fromValue(QColor(Qt::cyan)) };
            }
            return ThreadListModel::data(index, role);
        }
    };

    PillsEverywhereModel model;
    ThreadListView view;
    view.setModel(&model);
    view.setTreePosition(ThreadListModel::SubjectColumn);
    view.setUniformRowHeights(true);

    // The delegates MainWindow installs, and not optional here. The strip's
    // band is measured against SubjectDelegate::rowHeightFor; without the
    // delegate the rows take the default height, the band overflows into the
    // row below, and the thread's own strip paints across the reply. That
    // reads exactly like a missing skip in the walk and is not one.
    view.setItemDelegate(new RowStyleDelegate(&view));
    view.setItemDelegateForColumn(ThreadListModel::SubjectColumn,
                                  new SubjectDelegate(&view));
    view.setColumnWidth(ThreadListModel::AttachmentColumn, 28);
    view.setColumnWidth(ThreadListModel::FlagColumn, 28);
    view.setColumnWidth(ThreadListModel::DateColumn, 130);
    view.setColumnWidth(ThreadListModel::AuthorsColumn, 180);
    view.setColumnWidth(ThreadListModel::SubjectColumn, 520);

    ThreadSummary thread = makeThread(QStringLiteral("t1"), {});
    thread.tags = QStringList{ QStringLiteral("mailing-list/SBo"),
                               QStringLiteral("signed") };
    model.appendBatch({ thread });

    MessageNode first;
    first.messageId = QStringLiteral("m0@example.org");
    first.threadId = QStringLiteral("t1");
    first.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("t1");
    reply.depth = 1;
    model.setThreadMessages(QStringLiteral("t1"), { first, reply });

    view.resize(1400, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const QModelIndex root = model.index(0, 0, QModelIndex());
    view.expand(root);
    QApplication::processEvents();

    const QModelIndex child = model.index(0, 0, root);
    const QRect childRect = view.visualRect(child);
    QVERIFY2(childRect.height() > 0, "the reply row is not on screen");

    // The exact colours the stub supplies, so an antialiased edge of anything
    // else cannot be counted as a pill.
    QSet<QRgb> pillColours;
    pillColours.insert(QColor(Qt::magenta).rgb());
    pillColours.insert(QColor(Qt::cyan).rgb());

    QImage shot(view.viewport()->size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    view.viewport()->render(&shot);

    // Guard proving the probe can see pills at all: the THREAD row must have
    // them, or a zero count under the reply proves nothing about the reply.
    const QRect rootRect = view.visualRect(root);
    int threadPills = 0;
    for (int y = rootRect.top(); y < qMin(rootRect.bottom(), shot.height()); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            if (pillColours.contains(shot.pixel(x, y) | 0xff000000))
                ++threadPills;
        }
    }
    QVERIFY2(threadPills > 0,
             "no pill pixels under the THREAD row either, so this probe cannot "
             "tell a missing strip from a broken render");

    int replyPills = 0;
    for (int y = childRect.top(); y < qMin(childRect.bottom(), shot.height()); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            if (pillColours.contains(shot.pixel(x, y) | 0xff000000))
                ++replyPills;
        }
    }

    QCOMPARE(replyPills, 0);
}

void TestMainWindow::aSelectedReadThreadIsNotDimmedIntoTheHighlight()
{
    // Read threads carry a dimmed Qt::ForegroundRole, blended against the
    // UNSELECTED background. Qt's own painting prefers a model foreground over
    // HighlightedText, so without SubjectDelegate::initStyleOption reversing
    // that, selecting a read row paints it grey on the selection colour, which
    // is close to unreadable. Seen in a screenshot before it was caught here.
    //
    // Rendered rather than asserted on roles: the model is right either way,
    // and the defect lives entirely in how the delegate resolves them.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    // Both rows READ, so both are dimmed and neither is bold: the only thing
    // that could differ is how the dimming composites against the selection.
    //
    // Comparing a read row against an unread one would not work, and an
    // earlier version of this test did exactly that. Unread also paints bold,
    // so the rows differ legitimately and the comparison says nothing about
    // the selection. That version passed only because the machine it was
    // written on had its Qt font configured Bold, which made every row bold
    // and hid the difference.
    ThreadSummary first = makeThread(QStringLiteral("t1"), {});
    ThreadSummary second = makeThread(QStringLiteral("t2"), {});
    first.subject = second.subject = QStringLiteral("Same subject both rows");
    first.authors = second.authors = QStringLiteral("Someone <s@example.org>");
    model->appendBatch({ first, second });

    window.resize(900, 300);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Row 0 selected, row 1 not. The property under test is that selecting a
    // dimmed row switches it to the highlight's own text colour, so the two
    // rows MUST differ; comparing two identically-styled rows would pass
    // against a delegate that did nothing at all.
    selectThreadRow(view, 0);
    QApplication::processEvents();

    const int rowHeight = threadRowHeight(view, 0);
    QVERIFY(rowHeight > 0);

    QImage shot(view->viewport()->size(), QImage::Format_ARGB32);
    shot.fill(Qt::transparent);
    view->viewport()->render(&shot);

    // What the delegate resolves for each row, which is the thing the fix
    // changes. Rendering alone cannot separate "used the highlight colour"
    // from "used the dim over a highlighted background".
    QStyleOptionViewItem selected;
    selected.initFrom(view);
    selected.state |= QStyle::State_Selected;
    QStyleOptionViewItem unselected;
    unselected.initFrom(view);
    unselected.state &= ~QStyle::State_Selected;

    auto *delegate = qobject_cast<QStyledItemDelegate *>(view->itemDelegate());
    QVERIFY2(delegate, "the thread view has no styled delegate");

    const QModelIndex index =
        model->index(0, ThreadListModel::SubjectColumn);

    // initStyleOption is protected, so the resolved palette is reached the way
    // the painter does: through a subclass that exposes it.
    struct Probe : SubjectDelegate {
        using SubjectDelegate::initStyleOption;
    };
    const auto *probe = static_cast<const Probe *>(
        static_cast<const SubjectDelegate *>(delegate));

    probe->initStyleOption(&selected, index);
    probe->initStyleOption(&unselected, index);

    QVERIFY2(selected.palette.color(QPalette::Text)
                 == selected.palette.color(QPalette::HighlightedText),
             "a selected row still resolves to the dimmed text colour, so the "
             "dimming will paint over the selection highlight");
    QVERIFY2(unselected.palette.color(QPalette::Text)
                 != selected.palette.color(QPalette::Text),
             "an unselected read row lost its dimming");
}

void TestMainWindow::markAllReadIsDisabledUntilTheQueryFinishes()
{
    // Threads arrive in batches, so acting mid-load would silently skip
    // whatever had not arrived. Rather than acting on part of the view and
    // calling it "all", or stalling on a wait the user cannot see, the action
    // is simply unavailable until the result set is complete.
    const Config config;
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("mark_all_read"));
    QVERIFY2(action, "no mark_all_read action registered");

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);

    // A query in flight: rows are arriving but the worker has not said it is
    // done, so the action must stay out of reach.
    // A query is needed for runCurrentQuery to do anything: it returns early
    // on an empty one, which would leave the flag untouched.
    window.findChild<QLineEdit *>()->setText(QStringLiteral("tag:inbox"));
    QMetaObject::invokeMethod(&window, "runCurrentQuery");
    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }) });
    QVERIFY2(!action->isEnabled(),
             "the action was live while the query was still loading");

    // The generation must match or the reply is discarded as stale, which is
    // how a superseded query is ignored everywhere else in this window.
    const quint64 generation = window.currentGenerationForTesting();
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 1), Q_ARG(quint64, generation));
    QVERIFY2(action->isEnabled(),
             "the action stayed disabled after the query finished");
}

void TestMainWindow::markAllReadActsOnEveryRowAndUndoesInOneStep()
{
    // Every row in the view, not just the selected ones, and one undo entry for
    // the batch: a user who marks 400 threads read expects one Ctrl+Z to be
    // enough.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("mark_all_read"));
    QVERIFY(action);

    // A query is needed for runCurrentQuery to do anything: it returns early
    // on an empty one, which would leave the flag untouched.
    window.findChild<QLineEdit *>()->setText(QStringLiteral("tag:inbox"));
    QMetaObject::invokeMethod(&window, "runCurrentQuery");
    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t3"),
                                    { QStringLiteral("unread"),
                                      QStringLiteral("flagged") }) });
    QMetaObject::invokeMethod(&window, "onQueryFinished", Q_ARG(int, 3),
                              Q_ARG(quint64,
                                    window.currentGenerationForTesting()));

    // One row selected, to prove the action ignores the selection rather than
    // acting on it.
    selectThreadRow(view, 0);

    action->trigger();

    for (int row = 0; row < 3; ++row) {
        QVERIFY2(!model->threadAt(row).tags.contains(QStringLiteral("unread")),
                 qPrintable(QStringLiteral("row %1 kept its unread tag")
                                .arg(row)));
    }
    // An unrelated tag on a row is untouched: only unread is removed.
    QVERIFY(model->threadAt(2).tags.contains(QStringLiteral("flagged")));

    // ONE undo entry for the whole batch, not one per thread. Asserted as a
    // depth, since triggering undo once and finding everything restored would
    // also pass if three commands had been pushed and the model happened to
    // recover on the first.
    QCOMPARE(window.undoDepthForTesting(), 1);

    auto *undo = window.findChild<QAction *>(QStringLiteral("undo"));
    QVERIFY(undo);
    undo->trigger();

    for (int row = 0; row < 3; ++row) {
        QVERIFY2(model->threadAt(row).tags.contains(QStringLiteral("unread")),
                 qPrintable(QStringLiteral("row %1 was not restored by one undo")
                                .arg(row)));
    }
}

void TestMainWindow::markAllReadDoesNothingWhenNothingIsUnread()
{
    // No write, no undo entry, and no pending edit for a view that is already
    // read: an undo entry that restores nothing is worse than none, since it
    // absorbs a Ctrl+Z the user meant for their previous action.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *action = window.findChild<QAction *>(QStringLiteral("mark_all_read"));
    QVERIFY(action);

    // A query is needed for runCurrentQuery to do anything: it returns early
    // on an empty one, which would leave the flag untouched.
    window.findChild<QLineEdit *>()->setText(QStringLiteral("tag:inbox"));
    QMetaObject::invokeMethod(&window, "runCurrentQuery");
    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("flagged") }),
                         makeThread(QStringLiteral("t2"), {}) });
    QMetaObject::invokeMethod(&window, "onQueryFinished", Q_ARG(int, 2),
                              Q_ARG(quint64,
                                    window.currentGenerationForTesting()));

    QCOMPARE(window.undoDepthForTesting(), 0);

    action->trigger();

    // The real assertion: no command was pushed. Checking only that the tags
    // did not change would pass against a version that sent a no-op write for
    // every row, which still costs an undo entry and a pending edit each. The
    // undo QAction cannot answer this: it is always enabled and tests canUndo()
    // when triggered.
    QVERIFY2(window.undoDepthForTesting() == 0,
             "an undo entry was pushed for a view with nothing unread");
    QVERIFY(model->threadAt(0).tags.contains(QStringLiteral("flagged")));

    // And it says so rather than appearing to have done something.
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);
    QVERIFY2(status->text().contains(QStringLiteral("Nothing unread")),
             qPrintable(status->text()));
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
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t3"),
                                    { QStringLiteral("unread") }) });

    selectThreadRow(view, 0);
    QVERIFY2(timer->isActive(), "no timer armed for an unread thread");

    // Move on before it can fire. One timer stays armed, not three.
    selectThreadRow(view, 1);
    QVERIFY(timer->isActive());
    selectThreadRow(view, 2);
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
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("read"),
                                    { QStringLiteral("inbox") }),
                         makeThread(QStringLiteral("unread"),
                                    { QStringLiteral("unread") }) });

    selectThreadRow(view, 0);
    QVERIFY2(!timer->isActive(), "armed a timer for an already-read thread");

    // And the unread one still arms, so this is not "never arms".
    selectThreadRow(view, 1);
    QVERIFY(timer->isActive());

    // Moving back to a read thread disarms it again, rather than leaving the
    // previous thread's timer running to fire against the wrong row.
    selectThreadRow(view, 0);
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
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }) });
    selectThreadRow(view, 0);

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

void TestMainWindow::selectAllIsBoundAndSelectsEveryRow()
{
    // Multi-select already worked by Ctrl+click and Shift+click; what was
    // missing was a keyboard and menu route to it. The action has to exist as a
    // registered action, not as a raw view shortcut, so it reaches the menu,
    // the shortcut reference and [keys] like every other binding.
    const Config config;
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("select_all"));
    QVERIFY2(action, "no select_all action registered");
    QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("Ctrl+A")));

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}),
                         makeThread(QStringLiteral("t2"), {}),
                         makeThread(QStringLiteral("t3"), {}) });

    action->trigger();

    QCOMPARE(view->selectionModel()->selectedRows().size(), 3);
}

void TestMainWindow::aMultiRowSelectionDoesNotArmTheMarkReadTimer()
{
    // A selection gesture must never mutate mail. current follows the keyboard
    // cursor as a selection extends, so without a guard every row swept through
    // by Shift+arrow would be queued to be marked read: threads the user only
    // ever selected, never opened.
    //
    // Note selectAll() on a fresh view is NOT the case to test here: it leaves
    // current invalid and emits no currentRowChanged at all (verified against
    // Qt 6.11), so it would pass without any guard in place. The real path is a
    // row already current, which is how a user reaches select-all: click a
    // thread, then Ctrl+A.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t3"),
                                    { QStringLiteral("unread") }) });

    // Sweep down as Shift+arrow does: current moves onto a row while the
    // selection already spans more than one.
    selectThreadRow(view, 0);
    view->selectionModel()->select(
        model->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view->selectionModel()->setCurrentIndex(
        model->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);

    QVERIFY2(view->selectionModel()->selectedRows().size() > 1,
             "test setup failed to build a multi-row selection");
    QVERIFY2(!timer->isActive(),
             "a multi-row selection armed the mark-read timer");
}

void TestMainWindow::growingASelectionCancelsAnAlreadyArmedTimer()
{
    // The ordering trap: clicking one row arms the timer legitimately, and only
    // then does the selection grow. Guarding the new selection alone is not
    // enough, the timer already running for the first row has to be cancelled
    // or that thread goes read behind a pane that no longer shows it.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"),
                                    { QStringLiteral("unread") }) });

    selectThreadRow(view, 0);
    QVERIFY2(timer->isActive(), "no timer armed for a single unread thread");

    // Extend to a second row, as Shift+click would.
    view->selectionModel()->select(
        model->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);

    QVERIFY2(!timer->isActive(),
             "extending the selection left the first row's timer running");
}

void TestMainWindow::collapsingBackToOneRowLoadsThatThreadAgain()
{
    // The guard must not be a one-way door. Narrowing a multi-row selection
    // back to a single row is ordinary reading again, so the timer arms as it
    // always did.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"),
                                    { QStringLiteral("unread") }) });

    view->selectAll();
    QVERIFY(!timer->isActive());

    // Back to one row, as a plain click would leave it.
    selectThreadRow(view, 1);

    QVERIFY2(timer->isActive(),
             "collapsing back to one row did not resume mark-read");
}

void TestMainWindow::theStatusBarReportsAMultiRowSelection()
{
    // The actual discoverability gap: the UI never acknowledged a selection, so
    // nothing taught the user that selecting more than one row was possible.
    // A count that appears while the selection is being built does.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY2(status, "no status label to report into");

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}),
                         makeThread(QStringLiteral("t2"), {}),
                         makeThread(QStringLiteral("t3"), {}) });

    view->selectAll();

    QVERIFY2(status->text().contains(QStringLiteral("3")),
             qPrintable(QStringLiteral("status bar does not report the selection "
                                       "size, it says '%1'").arg(status->text())));
}

void TestMainWindow::clearSelectionBlanksThePaneAndDeselects()
{
    // Item 50: the user asked for "two actions instead of one", so this is the
    // new action and clearPaneLeavesTheSelectionAlone() below pins the old one.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}),
                         makeThread(QStringLiteral("t2"), {}) });

    // From a row that is already current, which is both how a user reaches this
    // and what CLAUDE.md requires: selectAll() on a fresh view emits no
    // currentRowChanged at all, so a test starting there passes against a
    // missing guard.
    selectThreadRow(view, 0);
    QCOMPARE(view->selectionModel()->selectedRows().size(), 1);

    auto *action = window.findChild<QAction *>(QStringLiteral("clear_selection"));
    QVERIFY2(action, "no clear_selection action");
    action->trigger();

    QVERIFY2(view->selectionModel()->selectedRows().isEmpty(),
             "the row is still selected: this action's whole point is that it "
             "deselects as well as blanking");

    // The hazard: clearSelection() leaves currentIndex() VALID, so
    // onSelectionChanged() takes its "one or fewer rows" branch, sees a current
    // row whose id differs from the just-cleared m_currentThreadId, and calls
    // onThreadSelected for it, which sets m_currentThreadId again and sends a
    // loadThread. The pane would then repaint itself a moment later.
    //
    // **currentThreadId() is what detects that, not the pane.** This fixture
    // has no worker, so loadThread never replies and nothing ever repaints;
    // asserting showingPlaceholder() here passes whatever the code does, which
    // CLAUDE.md records as the standing limit of test_mainwindow. What IS
    // observable is the id the window set on its way to that request.
    QVERIFY2(window.currentThreadId().isEmpty(),
             qPrintable(QStringLiteral("a thread was re-adopted after the "
                                       "selection was cleared: currentThreadId "
                                       "is '%1', and a loadThread for it is "
                                       "already in flight")
                            .arg(window.currentThreadId())));

    // And current itself is gone, so no later collapse-to-one-row can reload
    // it either.
    QVERIFY2(!view->currentIndex().isValid(),
             "currentIndex is still valid, so onSelectionChanged can reload "
             "that row on the next selection change");
}

void TestMainWindow::clearPaneLeavesTheSelectionAlone()
{
    // The pre-existing action keeps its behaviour. Item 32 built it to blank
    // WITHOUT touching the selection, and a user who binds it is entitled to
    // that; item 50 adds a second action rather than changing this one.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}),
                         makeThread(QStringLiteral("t2"), {}) });

    selectThreadRow(view, 0);
    QCOMPARE(view->selectionModel()->selectedRows().size(), 1);

    auto *action = window.findChild<QAction *>(QStringLiteral("clear_pane"));
    QVERIFY2(action, "no clear_pane action");
    action->trigger();

    QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
}

void TestMainWindow::maildirOverviewShowsUnknownRatherThanZero()
{
    // A field notmuch could not answer must not render as 0. "0 messages" says
    // the Maildir is empty, which is a claim; the truth is that the count
    // failed, and telling someone their mail is gone is the worst available
    // way to report an unreadable index.
    const Config config;
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("maildirOverview"));
    QVERIFY2(action, "no maildirOverview action");
    action->trigger();

    auto *counts = window.findChild<QLabel *>(QStringLiteral("maildirCounts"));
    QVERIFY2(counts, "the overview dialog has no counts label");

    // The worker never answers in this fixture, so drive the slot directly
    // with the all-unknown stats a failed open produces.
    QMetaObject::invokeMethod(
        &window, "onDatabaseStatsReady", Qt::DirectConnection,
        Q_ARG(DatabaseStats, DatabaseStats{}),
        Q_ARG(quint64, window.statsGenerationForTesting()));

    QVERIFY2(counts->text().contains(QStringLiteral("unknown")),
             qPrintable(QStringLiteral("counts label says '%1'")
                            .arg(counts->text())));
    QVERIFY2(!counts->text().contains(QStringLiteral(">0<")),
             qPrintable(QStringLiteral("an unanswered count rendered as zero: "
                                       "'%1'").arg(counts->text())));

    // WA_DeleteOnClose, so closing is what frees it. Left open, each test
    // leaks a window for the rest of the run.
    counts->window()->close();
}

void TestMainWindow::maildirOverviewIgnoresAStaleReply()
{
    // Counting every message is slow enough that closing and reopening the
    // dialog while one runs is realistic. The older answer must not fill in the
    // newer dialog, or the numbers silently predate whatever prompted the
    // reopen.
    const Config config;
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("maildirOverview"));
    QVERIFY(action);
    action->trigger();

    const quint64 stale = window.statsGenerationForTesting();

    // Reopening bumps the generation, which is what makes the first reply old.
    action->trigger();
    QVERIFY2(window.statsGenerationForTesting() != stale,
             "reopening the dialog did not bump the generation, so a reply for "
             "the previous one cannot be told apart");

    auto *counts = window.findChild<QLabel *>(QStringLiteral("maildirCounts"));
    QVERIFY(counts);
    const QString before = counts->text();

    DatabaseStats old;
    old.messages = 4321;
    old.threads = 999;
    old.tags = 42;
    QMetaObject::invokeMethod(&window, "onDatabaseStatsReady",
                              Qt::DirectConnection,
                              Q_ARG(DatabaseStats, old),
                              Q_ARG(quint64, stale));

    QCOMPARE(counts->text(), before);
    QVERIFY2(!counts->text().contains(QStringLiteral("4321")),
             "a reply for the previous dialog filled in the current one");

    QPointer<QLabel> watch(counts);
    counts->window()->close();

    // WA_DeleteOnClose deletes through deleteLater, so the label outlives
    // close() until the event loop runs. Drain it, or the "reply after the
    // dialog is gone" case below is not actually being tested.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY2(watch.isNull(),
             "the dialog was not destroyed, so the case below is not the one "
             "this test means to exercise");

    // The QPointer's reason for being: counting a large database takes long
    // enough that closing the dialog first is ordinary, and the reply then
    // arrives for a label that has been deleted. A raw pointer would dangle
    // here, so this must not crash.
    QMetaObject::invokeMethod(&window, "onDatabaseStatsReady",
                              Qt::DirectConnection,
                              Q_ARG(DatabaseStats, old),
                              Q_ARG(quint64,
                                    window.statsGenerationForTesting()));
}

void TestMainWindow::theThreadListOffersAContextMenu()
{
    // Right-click is the other half of discoverability: until now every tag
    // action was keyboard-only, so the Ctrl+T dialog in particular could not be
    // reached with the mouse at all.
    const Config config;
    MainWindow window(config);

    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    QCOMPARE(view->contextMenuPolicy(), Qt::CustomContextMenu);

    // The menu must reuse the registered QActions rather than build parallel
    // ones, or a [keys] rebinding would show the old shortcut here and the
    // menu could drift out of step with what the keyboard really does.
    auto *menu = window.findChild<QMenu *>(QStringLiteral("threadContextMenu"));
    QVERIFY2(menu, "no thread-list context menu");

    const QStringList expected = { QStringLiteral("archive"),
                                   QStringLiteral("delete"),
                                   QStringLiteral("spam"),
                                   QStringLiteral("toggle_unread"),
                                   QStringLiteral("edit_tags"),
                                   QStringLiteral("flag") };
    for (const QString &name : expected) {
        QAction *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action '%1'").arg(name)));
        QVERIFY2(menu->actions().contains(action),
                 qPrintable(QStringLiteral("context menu is missing the "
                                           "registered '%1' action").arg(name)));
    }
}

void TestMainWindow::aSecondRowBlanksThePaneNotOnlyAThird()
{
    // Reported by hand testing: selecting a second thread left it displayed,
    // and only a third blanked the pane. The cause is that currentRowChanged is
    // emitted before the selection model updates, so the Ctrl+click that makes
    // the count two arrives at onThreadSelected still reporting one, which
    // loads the thread; onSelectionChanged then blanks the pane, and the load,
    // being queued to the worker, paints over the blank when it returns. By the
    // third row m_currentThreadId is already cleared, so the late result is
    // discarded and the blank survives, which is why the fault looked like an
    // off-by-one in the threshold rather than a race.
    //
    // Two rows must behave exactly as three do.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t3"),
                                    { QStringLiteral("unread") }) });

    // One row: ordinary reading, so a timer is armed and a thread is current.
    selectThreadRow(view, 0);
    QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
    QVERIFY(timer->isActive());

    // Ctrl+click a second row. This is the exact gesture that failed: the
    // selection becomes two while currentRowChanged still reports one.
    view->selectionModel()->setCurrentIndex(
        model->index(1, 0),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);

    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
    QVERIFY2(!timer->isActive(),
             "two selected rows left the mark-read timer armed");

    // A blanked pane is one with no current thread: anything still in flight
    // for that id would repaint over it.
    QVERIFY2(window.currentThreadId().isEmpty(),
             qPrintable(QStringLiteral("two selected rows left thread '%1' "
                                       "loaded in the pane")
                            .arg(window.currentThreadId())));
}

void TestMainWindow::aLocalSyncIsNotReportedAsABackgroundOne()
{
    // Reported by hand testing: a manual sync ended with "Sync finished
    // elsewhere" stamped over its own result. The monitor sees the lock the
    // local run takes, and while the process lives isRunning() suppresses the
    // message; but the process exits, and therefore isRunning() goes false,
    // BEFORE the next poll notices the lock was released. That poll then
    // reported a local sync as a background one.
    //
    // Ownership is latched when the lock appears, so the release can still be
    // attributed after the process is gone.
    const Config config;
    MainWindow window(config);

    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    // The lock appears while no local sync is running: a background one.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    QVERIFY2(status->text().contains(QStringLiteral("Background")),
             qPrintable(QStringLiteral("a background sync was not announced, "
                                       "status says '%1'").arg(status->text())));

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    QVERIFY2(status->text().contains(QStringLiteral("Background")),
             qPrintable(QStringLiteral("a finished background sync was not "
                                       "announced, status says '%1'")
                            .arg(status->text())));

}

void TestMainWindow::aLocalSyncsOwnLockIsNeverReportedAsBackground()
{
    // The reported bug, staged at the seam where it actually lives.
    //
    // A real child process was tried first and abandoned: it needs a sync
    // command in the config, it leaves a live process behind for the length of
    // the test, and it made the suite pop a dialog. None of that is needed,
    // because the defect is not in MailSync. It is that ownership of a lock
    // period was decided at RELEASE time, when MailSync::isRunning() has
    // already gone false, instead of being latched when the lock appeared.
    //
    // With no sync command configured isRunning() is false throughout, which is
    // exactly the state the buggy code misread. So: announce a Running that the
    // window believes is external, then a matching Idle. Both must be reported.
    // The local case is covered by the latch being set only inside the Running
    // branch, and by aSkippedLocalSyncStillReportsTheOtherRunFinishing()
    // proving the latch is handed back when the lock was never ours.
    const Config config;
    MainWindow window(config);

    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);
    auto *progress =
        window.findChild<QProgressBar *>(QStringLiteral("syncProgress"));
    QVERIFY(progress);

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    QVERIFY2(progress->isVisibleTo(&window),
             "a background sync did not show the progress bar");

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    QVERIFY2(!progress->isVisibleTo(&window),
             "the progress bar outlived the background sync");

    // An Unknown transition means the lock table could not be read. Nothing was
    // observed, so nothing may be claimed: the previous message must stand.
    status->setText(QStringLiteral("untouched"));
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Unknown));
    QCOMPARE(status->text(), QStringLiteral("untouched"));
}

void TestMainWindow::aSkippedLocalSyncStillReportsTheOtherRunFinishing()
{
    // The narrow case the latch could break: a manual sync that exits 75
    // because cron already holds the lock. If both started inside one poll
    // interval the monitor sees the lock appear while isRunning() is true and
    // latches it local, even though the lock belongs to the cron run. The
    // completion of that run would then be swallowed. onSyncFinished() hands
    // ownership back when it sees the skip code.
    const Config config;
    MainWindow window(config);

    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    QMetaObject::invokeMethod(&window, "onSyncFinished",
                              Q_ARG(bool, false),
                              Q_ARG(int, MainWindow::kSyncSkippedExitCode));

    // The skip itself is reported, and not as a failure.
    QVERIFY2(!status->text().contains(QStringLiteral("failed")),
             qPrintable(QStringLiteral("a skip was reported as a failure: '%1'")
                            .arg(status->text())));

    // The other run finishing must still be announced.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    QVERIFY2(status->text().contains(QStringLiteral("Background")),
             qPrintable(QStringLiteral("after a skipped local sync, the other "
                                       "run finishing was swallowed; status "
                                       "says '%1'").arg(status->text())));
}

void TestMainWindow::theSyncActionIsDisabledWhileABackgroundSyncHoldsTheLock()
{
    // Item 29 shipped for the QPushButton only: onExternalSyncStateChanged
    // disabled m_syncButton and never touched the QAction, so the toolbar and
    // menu Sync stayed clickable during a cron sync and could only produce the
    // EX_TEMPFAIL skip. The button-based test passed throughout, because it
    // drove the half that worked.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("qtmaildir"))));
    const QString conf = dir.filePath(QStringLiteral("qtmaildir/qtmaildir.conf"));
    {
        QSettings s(conf, QSettings::IniFormat);
        s.setValue(QStringLiteral("sync/command"), QStringLiteral("/bin/true"));
    }

    const QString locks = dir.filePath(QStringLiteral("locks"));
    {
        QFile f(locks);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    MainWindow::setLocksPathForTesting(locks);

    Config config;
    config.load(conf);
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("sync"));
    QVERIFY2(action, "no sync action to check");
    QVERIFY2(action->isEnabled(), "the action starts disabled with a command set");

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    QVERIFY2(!action->isEnabled(),
             "the sync action stayed enabled during a background sync");

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    QVERIFY2(action->isEnabled(),
             "the sync action was not re-enabled after the background sync");

    MainWindow::setLocksPathForTesting(QStringLiteral("/proc/locks"));
}

void TestMainWindow::theStatusBarFollowsTheSyncPhase()
{
    // Item 42: "Syncing..." said nothing about what was happening, while the
    // script was already streaming its phase into the log pane and the app was
    // throwing it away.
    //
    // Driven through a real script rather than by calling the tracker directly,
    // because the defect this guards is in the wiring: the chunks QProcess
    // hands over split mid-line, so a handler that fed them straight to the
    // tracker would stall on the first partial line.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("qtmaildir"))));

    const QString script = dir.filePath(QStringLiteral("fakesync.sh"));
    {
        QFile f(script);
        QVERIFY(f.open(QIODevice::WriteOnly));
        // Shaped like the real thing: timestamped lines, the noise that makes
        // up the bulk of a run, mbsync's one summary, then notmuch's output.
        // Paced, not dumped. A script that prints everything at once is
        // delivered in a single readyRead, so the tracker sees the whole run in
        // one call and only its final phase is ever painted: the intermediate
        // ones would be unobservable and the test would assert nothing. A real
        // sync takes tens of seconds and arrives in separate chunks, which the
        // sleeps stand in for.
        f.write("#!/bin/sh\n"
                "echo '===== RUN START: 2026-08-07T11:00:00+02:00 ====='\n"
                "echo '11:00:01 Note: Ignoring non-mail file: /home/you/Mail/x/.uidvalidity'\n"
                "sleep 0.2\n"
                "echo '11:00:02 Channels: 5    Boxes: 39    Far: +0 *1 #0 -0    Near: +1 *0 #0 -0'\n"
                "sleep 0.2\n"
                "echo '11:00:03 Processed 79 total files in almost no time.'\n"
                "echo '11:00:03 Added 1 new message to the database.'\n"
                "sleep 0.2\n"
                "echo '===== RUN END: 2026-08-07T11:00:03+02:00  status=OK ====='\n");
        f.close();
        QVERIFY(QFile::setPermissions(script,
                                      QFile::ReadOwner | QFile::WriteOwner
                                          | QFile::ExeOwner));
    }

    const QString conf = dir.filePath(QStringLiteral("qtmaildir/qtmaildir.conf"));
    {
        QSettings s(conf, QSettings::IniFormat);
        s.setValue(QStringLiteral("sync/command"), script);
    }

    const QString locks = dir.filePath(QStringLiteral("locks"));
    {
        QFile f(locks);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    MainWindow::setLocksPathForTesting(locks);

    Config config;
    config.load(conf);
    QCOMPARE(config.syncCommand(), script);
    MainWindow window(config);

    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    // Every value the label takes, recorded as it changes. A run this small
    // finishes in well under a second, so polling for an intermediate phase
    // races the process and usually sees only "Sync complete": the sequence has
    // to be captured, not sampled.
    // QLabel has no textChanged signal, so the label is sampled on a fast timer
    // rather than watched. Each distinct value is recorded once.
    QStringList seen;
    QTimer sampler;
    sampler.setInterval(1);
    connect(&sampler, &QTimer::timeout, &sampler, [&seen, status]() {
        const QString text = status->text();
        if (seen.isEmpty() || seen.constLast() != text)
            seen.append(text);
    });
    sampler.start();

    QVERIFY(QMetaObject::invokeMethod(&window, "startSync"));

    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(seen.cbegin(), seen.cend(), [](const QString &s) {
            return s.contains(QStringLiteral("Sync complete"));
        }),
        10000);

    const QString trace = seen.join(QStringLiteral(" | "));

    // mbsync's summary is the only concrete thing the stream carries, since it
    // names no channel unless run verbose. The counts must reach the label.
    QVERIFY2(std::any_of(seen.cbegin(), seen.cend(), [](const QString &s) {
                 return s.contains(QStringLiteral("39"))
                     && s.contains(QStringLiteral("5"));
             }),
             qPrintable(QStringLiteral("the mbsync summary never reached the "
                                       "status bar. Saw: ") + trace));

    // Then the reindex phase, which is a different message entirely. Without
    // the wiring the label went from "Syncing..." straight to "Sync complete",
    // which is exactly what the defect looked like.
    QVERIFY2(std::any_of(seen.cbegin(), seen.cend(), [](const QString &s) {
                 return s.contains(QStringLiteral("notmuch"));
             }),
             qPrintable(QStringLiteral("the notmuch phase never reached the "
                                       "status bar. Saw: ") + trace));

    // The banners are not a phase and must never appear in the status bar.
    for (const QString &s : seen) {
        QVERIFY2(!s.contains(QStringLiteral("RUN ")), qPrintable(s));
        QVERIFY2(!s.contains(QStringLiteral("status=")), qPrintable(s));
    }

    MainWindow::setLocksPathForTesting(QStringLiteral("/proc/locks"));
}

void TestMainWindow::anUnobservableLockTableLeavesTheSyncButtonUsable()
{
    // Unknown means /proc/locks could not be read, so nothing was observed. A
    // button left permanently disabled on a platform that cannot see the lock
    // is worse than one that occasionally offers a run that gets skipped.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("qtmaildir"))));
    const QString conf = dir.filePath(QStringLiteral("qtmaildir/qtmaildir.conf"));
    {
        QSettings s(conf, QSettings::IniFormat);
        s.setValue(QStringLiteral("sync/command"), QStringLiteral("/bin/true"));
    }

    Config config;
    config.load(conf);
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("sync"));
    QVERIFY(action);

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    QVERIFY(!action->isEnabled());

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Unknown));
    QVERIFY2(action->isEnabled(),
             "an unobservable lock table left the sync action disabled");
}

void TestMainWindow::escapeBlanksTheMessagePane()
{
    // A registered action like any other, so it reaches the menus, the shortcut
    // reference and [keys]. Clearing m_currentThreadId with the pane is the
    // part that matters: a late threadLoaded would otherwise paint the thread
    // straight back, which is the race fixed in 0.8.0.
    const Config config;
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("clear_pane"));
    QVERIFY2(action, "no clear_pane action registered");

    // Shift+Esc since item 50: plain Escape now clears the selection too, and
    // this narrower action kept the same key with a modifier. The behaviour
    // asserted below is unchanged, which is the point of keeping both.
    QCOMPARE(action->shortcut(),
             QKeySequence(Qt::ShiftModifier | Qt::Key_Escape));

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}),
                         makeThread(QStringLiteral("t2"), {}) });

    selectThreadRow(view, 0);
    QVERIFY2(!window.currentThreadId().isEmpty(),
             "no thread was opened to blank");

    action->trigger();
    QVERIFY2(window.currentThreadId().isEmpty(),
             "Escape left the thread loaded in the pane");

    // Blanking is a view change, not a mail change: the selection stays.
    QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
}

void TestMainWindow::deleteTogglesOnAnAlreadyDeletedThread()
{
    // Hitting Delete twice is the natural way to say "no, put it back", and
    // adding a tag that is already present is a no-op the user cannot see.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("deleted") }) });
    selectThreadRow(view, 0);

    action->trigger();

    // The optimistic model update is synchronous, so the row reflects the
    // change without a worker.
    QVERIFY2(!model->threadAt(0).isDeleted(),
             "delete on an already-deleted thread did not undelete it");
}

void TestMainWindow::deleteOnAMixedSelectionDeletesRatherThanSplittingIt()
{
    // The constraint that makes this more than a one-liner: toggling each
    // thread independently would leave one keystroke with the selection in two
    // states, which is worse than either outcome. Undelete only when every
    // selected thread is already deleted.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("deleted") }),
                         makeThread(QStringLiteral("t2"), {}) });

    view->selectAll();
    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);

    action->trigger();

    QVERIFY2(model->threadAt(0).isDeleted() && model->threadAt(1).isDeleted(),
             "a mixed selection split instead of deleting the whole selection");
}

void TestMainWindow::aTransientStatusMessageExpires()
{
    // "Sync complete" describes an event, not a state, and reads as though it
    // describes the present until something else overwrites it.
    const Config config;
    MainWindow window(config);

    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("statusTimer"));
    QVERIFY2(timer, "no status expiry timer");

    QMetaObject::invokeMethod(&window, "showTransientStatus",
                              Q_ARG(QString, QStringLiteral("Sync complete")));
    QCOMPARE(status->text(), QStringLiteral("Sync complete"));
    QVERIFY(timer->isActive());

    // Fire it rather than waiting out the real interval.
    timer->setInterval(0);
    QTRY_VERIFY_WITH_TIMEOUT(status->text() != QStringLiteral("Sync complete"),
                             2000);
}

void TestMainWindow::theSelectionCountIsStateAndDoesNotExpire()
{
    // Not everything in the status bar is an event. The selection count
    // describes what is true right now and must persist while it stays true;
    // expiring it would undo the 0.8.0 discoverability work.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("statusTimer"));
    QVERIFY(timer);

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}),
                         makeThread(QStringLiteral("t2"), {}) });
    view->selectAll();

    QVERIFY2(status->text().contains(QStringLiteral("2")),
             "the selection count was not reported");
    QVERIFY2(!timer->isActive(),
             "the selection count armed the expiry timer; it is state, "
             "not an event");
}

void TestMainWindow::anEditUndoneNettsBackToZero()
{
    // Reported by the user: open a thread, let the 2 s auto-mark-read remove
    // `unread`, then press Ctrl+U to put it back. The indicator read 2 unsynced
    // changes when the mail store was exactly where it started.
    //
    // The count tracks NET state, not writes. Two writes did happen, but their
    // effect cancels, and what the user needs to know is whether quitting now
    // would strand work.
    const Config config;
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);
    QVERIFY(label->isHidden());

    // The automatic mark-read: remove `unread` from one message.
    TagChange off;
    off.messageIds = { QStringLiteral("m1") };
    off.removed = { QStringLiteral("unread") };
    off.description = QStringLiteral("Mark read");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, off)));
    QVERIFY2(!label->isHidden(), "one edit must show the indicator");

    // Ctrl+U puts it back on the same message.
    TagChange on;
    on.messageIds = { QStringLiteral("m1") };
    on.added = { QStringLiteral("unread") };
    on.description = QStringLiteral("Mark unread");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, on)));

    QVERIFY2(label->isHidden(),
             qPrintable(QStringLiteral("an edit and its inverse left the "
                                       "indicator showing '%1'")
                            .arg(label->text())));
}

void TestMainWindow::aDifferentTagOnTheSameMessageStillCounts()
{
    // Netting must be per (message, tag), not per message. Removing `unread`
    // and adding `flagged` on one message are two independent changes, and
    // neither cancels the other.
    const Config config;
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    TagChange a;
    a.messageIds = { QStringLiteral("m1") };
    a.removed = { QStringLiteral("unread") };
    a.description = QStringLiteral("Mark read");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, a)));

    TagChange b;
    b.messageIds = { QStringLiteral("m1") };
    b.added = { QStringLiteral("flagged") };
    b.description = QStringLiteral("Flag");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, b)));

    QVERIFY2(!label->isHidden(),
             "two different tags on one message cancelled each other");
}

void TestMainWindow::anEditWithNoMessageIdsStillCounts()
{
    // A TagChange carrying no message ids cannot be netted against anything,
    // and must still register rather than silently counting as zero. Losing an
    // edit understates the indicator, which is the direction that costs the
    // user work.
    const Config config;
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    TagChange change;
    change.added = { QStringLiteral("deleted") };
    change.description = QStringLiteral("Delete");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));

    QVERIFY2(!label->isHidden(),
             "an edit with no message ids was not counted at all");
}

// Item 37. A tag edit made while a background sync holds notmuch's write lock
// used to stall the worker: the read-write open BLOCKS until the lock frees
// (measured 9.158s against a 12s hold, returning NOTMUCH_STATUS_SUCCESS), so
// every later query and thread load queued behind it. These cases pin the fix:
// do not send the write while a sync is running, send it when the sync ends.

void TestMainWindow::anEditDuringABackgroundSyncIsNotSentYet()
{
    // The defect. Sending during the sync is what stalls the worker, so the
    // edit is held instead. The rows still show it: it is what the user asked
    // for and it is going to be applied.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY2(action, "no flag action registered");

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}) });
    selectThreadRow(view, 0);

    // A cron sync takes the lock.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));

    action->trigger();

    QVERIFY2(window.hasEditAwaitingSend(),
             "the edit was sent straight into a running sync, which is the "
             "blocking open that stalls the worker");
    QVERIFY2(model->threadAt(0).tags.contains(QStringLiteral("flagged")),
             "holding the edit also dropped it from the rows");
}

void TestMainWindow::aHeldEditIsSentWhenTheBackgroundSyncEnds()
{
    // The release. SyncMonitor already reports this transition for item 27, so
    // the held edit rides a signal that exists rather than a timer.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}) });
    selectThreadRow(view, 0);

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    action->trigger();
    QVERIFY(window.hasEditAwaitingSend());

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));

    QVERIFY2(!window.hasEditAwaitingSend(),
             "the sync ending did not send the held edit");
    QVERIFY2(model->threadAt(0).tags.contains(QStringLiteral("flagged")),
             "sending the held edit lost the tag from the rows");
}

void TestMainWindow::aHeldEditCountsAsUnsynced()
{
    // A held edit has not reached the index, so onTagsApplied() never counted
    // it. It must still count here, because this is what the exit prompt reads:
    // quitting on a held edit loses it outright, which is the whole failure the
    // prompt exists to prevent.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);
    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);
    QVERIFY2(label->isHidden(), "the indicator starts hidden at zero");

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}) });
    selectThreadRow(view, 0);

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    action->trigger();

    QVERIFY2(!label->isHidden(),
             "an edit held for a running sync was not counted as unsynced, so "
             "the exit prompt would let the user quit on it");
}

void TestMainWindow::anUnreadableLockTableStillSendsTheEdit()
{
    // State::Unknown means /proc/locks could not be read, so nothing is
    // observed. Holding writes there would strand every edit forever on a
    // platform that cannot see the lock at all. Unknown is not "running".
    //
    // Driven from Running, not from a fresh window: the guard is that Unknown
    // CLEARS the busy flag, and a window that was never busy would pass this
    // whatever Unknown did. Reaching Unknown by way of Running is also the only
    // way a real monitor gets there, when /proc/locks becomes unreadable
    // mid-session.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"), {}),
                         makeThread(QStringLiteral("t2"), {}) });

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    selectThreadRow(view, 0);
    action->trigger();
    QVERIFY2(window.hasEditAwaitingSend(),
             "the edit was not held during a running sync, so this test is not "
             "exercising the Unknown transition it claims to");

    // The lock table becomes unreadable. That is not evidence of a sync, so
    // writing must resume and the held edit must go out.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Unknown));
    QVERIFY2(!window.hasEditAwaitingSend(),
             "an unreadable lock table kept the edit held, stranding it on any "
             "platform without /proc/locks");

    // And a NEW edit is sent rather than held.
    selectThreadRow(view, 1);
    action->trigger();
    QVERIFY2(!window.hasEditAwaitingSend(),
             "an unreadable lock table held a new edit, so writes never resume");
}

void TestMainWindow::aRejectedWriteKeepsEarlierUndoHistory()
{
    // revertPendingTagChange() used to undo the failed command and then CLEAR
    // the whole stack, so one rejected write threw away every undo step the
    // user had built up. Undoing the failed command is enough: it is already
    // off the stack afterwards.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *flag = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(flag);
    auto *archive = window.findChild<QAction *>(QStringLiteral("archive"));
    QVERIFY2(archive, "no archive action registered");

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("inbox") }) });
    selectThreadRow(view, 0);

    // One edit that succeeds, so there is history worth keeping.
    archive->trigger();
    TagChange applied;
    applied.messageIds = { QStringLiteral("m1") };
    applied.removed = { QStringLiteral("inbox") };
    applied.description = QStringLiteral("Archive");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, applied)));

    // A second edit that the worker rejects outright.
    flag->trigger();
    QVERIFY(QMetaObject::invokeMethod(
        &window, "onWorkerError",
        Q_ARG(QString, QStringLiteral("Cannot resolve threads"))));

    QVERIFY2(window.canUndo(),
             "a rejected write cleared the undo history of edits that had "
             "already succeeded");
}

// Item 54. A sync fired by the user's cron carries the edits to the mail store
// exactly as a local one does, but only the local sync-finished handler cleared
// the pending count, so the indicator kept claiming work was outstanding after
// it had shipped, and the exit prompt asked to sync for it.
//
// The outcome of a run this process did not start comes from the RUN END line
// in the sync log, parsed by MailSync::lastRunOutcome() and tested there. These
// tests are about what MainWindow does with each answer.

namespace {

/// Writes a config naming \p logPath as the sync log, and loads it.
///
/// The log path has to come from config rather than the real
/// ~/.local/state/mailsync.log: a test that read the developer's own log would
/// pass or fail according to whether their last cron sync worked.
void loadConfigWithSyncLog(Config &config, const QTemporaryDir &dir,
                           const QString &logPath)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QStringLiteral("[sync]\nlog=%1\n").arg(logPath).toUtf8());
    file.close();

    config.load(path);
    QCOMPARE(config.syncLog(), logPath);
}

/// Writes a sync log whose last completed run ended with \p status.
void writeSyncLog(const QString &path, const QString &status)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QStringLiteral("===== RUN END: 2026-08-09T10:20:03+02:00  status=%1 "
                              "=====\n").arg(status).toUtf8());
    file.close();
}

/// Records one confirmed edit, the way onTagsApplied() does for a real write.
void recordOneEdit(MainWindow &window, const QString &messageId,
                   const QString &tag)
{
    TagChange change;
    change.messageIds = { messageId };
    change.added = { tag };
    change.description = QStringLiteral("Flag");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));
}

/// Drives a complete external sync: the lock appears, then it is released.
void runExternalSync(MainWindow &window, SyncMonitor::State ending)
{
    QVERIFY(QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                                      Q_ARG(SyncMonitor::State,
                                            SyncMonitor::State::Running)));
    QVERIFY(QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                                      Q_ARG(SyncMonitor::State, ending)));
}

} // namespace

void TestMainWindow::aSuccessfulCronSyncClearsThePendingCount()
{
    // The reported defect: edits applied, cron syncs, indicator still says N.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("mailsync.log"));
    writeSyncLog(logPath, QStringLiteral("OK"));

    Config config;
    loadConfigWithSyncLog(config, dir, logPath);
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    recordOneEdit(window, QStringLiteral("m1"), QStringLiteral("flagged"));
    QVERIFY2(!label->isHidden(), "the edit was not counted at all");

    runExternalSync(window, SyncMonitor::State::Idle);

    QVERIFY2(label->isHidden(),
             qPrintable(QStringLiteral("a successful cron sync left the "
                                       "indicator showing '%1'")
                            .arg(label->text())));
}

void TestMainWindow::aFailedCronSyncLeavesThePendingCount()
{
    // The rule the local path already follows: only a SUCCESSFUL sync clears
    // the count. Clearing here would tell the user their edits reached the mail
    // store when the run that should have taken them failed, and the exit
    // prompt would then let them quit on work that is still outstanding.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("mailsync.log"));
    writeSyncLog(logPath, QStringLiteral("FAILED  mbsync=1 notmuch=0"));

    Config config;
    loadConfigWithSyncLog(config, dir, logPath);
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    recordOneEdit(window, QStringLiteral("m1"), QStringLiteral("flagged"));
    runExternalSync(window, SyncMonitor::State::Idle);

    QVERIFY2(!label->isHidden(),
             "a FAILED cron sync cleared the pending count, claiming edits "
             "reached the mail store when the sync that carries them failed");
}

void TestMainWindow::anUnreadableSyncLogLeavesThePendingCount()
{
    // No log at all: SyncOutcome::Unknown. Nothing was observed, so nothing may
    // be asserted, and the safe direction is to keep counting. Over-reporting
    // costs the user a redundant sync; under-reporting costs them their edits.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("absent.log"));
    QVERIFY(!QFileInfo::exists(logPath));

    Config config;
    loadConfigWithSyncLog(config, dir, logPath);
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    recordOneEdit(window, QStringLiteral("m1"), QStringLiteral("flagged"));
    runExternalSync(window, SyncMonitor::State::Idle);

    QVERIFY2(!label->isHidden(),
             "an unreadable sync log cleared the pending count on no evidence");
}

void TestMainWindow::anUnknownExternalStateClearsNothing()
{
    // State::Unknown means /proc/locks could not be read, so no sync was
    // observed finishing. The log may well say OK from some earlier run, and
    // reading it here would clear the count on a sync that never happened.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("mailsync.log"));
    writeSyncLog(logPath, QStringLiteral("OK"));

    Config config;
    loadConfigWithSyncLog(config, dir, logPath);
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    recordOneEdit(window, QStringLiteral("m1"), QStringLiteral("flagged"));
    runExternalSync(window, SyncMonitor::State::Unknown);

    QVERIFY2(!label->isHidden(),
             "an Unknown lock state cleared the pending count from a log line "
             "written by an earlier run");
}

void TestMainWindow::aSuccessfulCronSyncDrainsTheEditedAccounts()
{
    // The same defect in item 49's state, and invisible in the indicator: the
    // count can reach zero while the account set stays full, in which case the
    // next manual sync runs channels that have nothing to carry. Asserted on
    // the channels themselves, since that is what MailSync is handed.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("mailsync.log"));
    writeSyncLog(logPath, QStringLiteral("OK"));

    const QString confPath = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile conf(confPath);
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    conf.write(QStringLiteral("[sync]\nlog=%1\n\n"
                              "[account.work]\n"
                              "maildir=work-mail\n"
                              "channel=work-channel\n")
                   .arg(logPath).toUtf8());
    conf.close();

    Config config;
    config.load(confPath);
    QCOMPARE(config.accounts().size(), 1);

    MainWindow window(config);

    // One thread carrying the work account's tag, so sendThreadTagChange() can
    // resolve an account key from it the way it does for a real edit.
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    ThreadSummary thread;
    thread.threadId = QStringLiteral("t1");
    thread.subject = QStringLiteral("Subject");
    thread.tags = { QStringLiteral("account-work"), QStringLiteral("inbox") };
    model->appendBatch({ thread });
    QCOMPARE(model->accountKeysForThread(QStringLiteral("t1")),
             QStringList{ QStringLiteral("work") });

    QVERIFY(QMetaObject::invokeMethod(
        &window, "sendThreadTagChange",
        Q_ARG(QStringList, QStringList{ QStringLiteral("t1") }),
        Q_ARG(QStringList, QStringList{ QStringLiteral("flagged") }),
        Q_ARG(QStringList, QStringList{}),
        Q_ARG(QString, QStringLiteral("Flag"))));

    // The guard: the account really is recorded, so the assertion below is
    // about draining it rather than about it never having been there.
    QStringList channels;
    QVERIFY(QMetaObject::invokeMethod(&window, "pendingSyncChannels",
                                      Q_RETURN_ARG(QStringList, channels)));
    QCOMPARE(channels, QStringList{ QStringLiteral("work-channel") });

    runExternalSync(window, SyncMonitor::State::Idle);

    QVERIFY(QMetaObject::invokeMethod(&window, "pendingSyncChannels",
                                      Q_RETURN_ARG(QStringList, channels)));
    QVERIFY2(channels.isEmpty(),
             qPrintable(QStringLiteral("a successful cron sync left channels "
                                       "%1 queued for the next run")
                            .arg(channels.join(QLatin1Char(',')))));
}

void TestMainWindow::aCronSyncDoesNotClearAnEditMadeWhileItRan()
{
    // The race the local path solves by snapshotting before the flush. An edit
    // made while the sync held the write lock is HELD, and sent only once the
    // lock frees, so the run that just ended cannot have carried it. Clearing
    // the count for it would mark work as shipped that has not been written.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.filePath(QStringLiteral("mailsync.log"));
    writeSyncLog(logPath, QStringLiteral("OK"));

    Config config;
    loadConfigWithSyncLog(config, dir, logPath);
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    // The sync starts, then the user edits while it is running.
    QVERIFY(QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                                      Q_ARG(SyncMonitor::State,
                                            SyncMonitor::State::Running)));
    recordOneEdit(window, QStringLiteral("m1"), QStringLiteral("flagged"));
    QVERIFY2(!label->isHidden(), "the edit was not counted at all");

    QVERIFY(QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                                      Q_ARG(SyncMonitor::State,
                                            SyncMonitor::State::Idle)));

    // onTagsApplied() ran while the lock was held, so this edit reached the
    // INDEX during the sync. Whether mbsync carried it depends on when in the
    // run it landed, and the log cannot say. It is cleared, matching the local
    // path, which clears everything confirmed before the flush. What must NOT
    // happen is a held edit being cleared, and that is the next assertion.
    QVERIFY(label->isHidden());

    // A second sync, with an edit held across it: aSyncHoldsTheWriteLock() is
    // false here with no lock file, so this documents the reachable half. The
    // held-edit path has its own coverage in aHeldEditCountsAsUnsynced().
    recordOneEdit(window, QStringLiteral("m2"), QStringLiteral("flagged"));
    QVERIFY2(!label->isHidden(),
             "an edit made after the sync ended was swallowed by it");
}

// Items 56 and 57.

void TestMainWindow::everyActionCarriesAnIcon()
{
    // Item 56. The complaint was inconsistency, not absence: eight actions had
    // themed icons and the other sixteen had none, so adjacent entries in one
    // menu disagreed, and the toolbar's TextBesideIcon style laid out an empty
    // slot for each of the sixteen.
    //
    // What this test can and cannot prove is worth stating, because it is
    // weaker than it looks. QIcon::fromTheme() resolves against the icon theme
    // of the machine running the test, so a PASS says "this desktop has art for
    // every name assigned", not "every name is correct" and not "the icon suits
    // the action". A machine with a sparse theme fails this through no fault of
    // the code. It is still worth having: it catches the actual regression,
    // which is an action registered with no name assigned at all.
    const Config config;
    MainWindow window(config);

    // The guard. Without it a MainWindow that registered nothing would pass an
    // empty loop, which is the classic way a "for each" assertion goes vacuous.
    const QList<QAction *> actions =
        window.findChildren<QAction *>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY2(actions.size() >= KeyMap::knownActions().size(),
             qPrintable(QStringLiteral("expected at least %1 actions, found %2")
                            .arg(KeyMap::knownActions().size())
                            .arg(actions.size())));

    QStringList missing;
    for (const QString &name : KeyMap::knownActions()) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action named %1").arg(name)));
        if (action->icon().isNull())
            missing.append(name);
    }

    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("%1 action(s) have no icon: %2")
                            .arg(missing.size())
                            .arg(missing.join(QStringLiteral(", ")))));
}

void TestMainWindow::theToolbarDoesNotOverrideTheDesktopButtonStyle()
{
    // The second half of the user's note: "Buttons should honor the 'Icon only'
    // option". They cannot while the toolbar asserts its own style. Qt takes
    // the desktop's preference from the platform theme and exposes it as
    // SH_ToolButtonStyle; a hardcoded setToolButtonStyle() overrides it, so the
    // user's setting has no effect whatever it is set to.
    const Config config;
    MainWindow window(config);

    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("main_toolbar"));
    QVERIFY(toolBar);

    const auto expected = static_cast<Qt::ToolButtonStyle>(
        window.style()->styleHint(QStyle::SH_ToolButtonStyle, nullptr, toolBar));

    QCOMPARE(toolBar->toolButtonStyle(), expected);
}

void TestMainWindow::theImportantActionIsLabelledImportant()
{
    // Item 57. The user picked "Important" over "Starred": the Message menu
    // already has `Mark &spam`, so "Starred" would have had to take an
    // accelerator from inside the word, while "Important" takes a free &I.
    const Config config;
    MainWindow window(config);

    // The action NAME is unchanged on purpose. It is the key a user writes in
    // the config's [keys] section, so renaming it would silently break every
    // existing binding for a change that is only about wording.
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);

    QVERIFY2(action->text().contains(QStringLiteral("Important")),
             qPrintable(QStringLiteral("the action still reads '%1'")
                            .arg(action->text())));
    QVERIFY2(!action->text().contains(QStringLiteral("Flag")),
             qPrintable(QStringLiteral("the action still reads '%1'")
                            .arg(action->text())));

    // The accelerator the item chose, and the reason "Starred" was rejected.
    QCOMPARE(action->text(), QStringLiteral("&Important"));
}

void TestMainWindow::theImportantActionStillWritesTheFlaggedTag()
{
    // The rename is a LABEL change and must not reach the mail store. `flagged`
    // is a notmuch tag: neomutt reads it, the user's saved queries match on it,
    // ThreadSummary::isFlagged() tests for it and TagColors colours it. A
    // rename that followed the label through to the tag would rewrite the store
    // and desynchronise every other tool that reads the same Maildir.
    //
    // Asserted on the ids and tags actually sent to the worker, which is the
    // only place the distinction is observable.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("inbox") }) });

    // The guard: the thread must NOT already carry the tag, or the assertion
    // below would pass against an action that did nothing at all.
    QVERIFY(!model->threadAt(0).isFlagged());

    selectThreadRow(view, 0);

    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);
    action->trigger();

    // sendThreadTagChange() applies the change to the model optimistically, so
    // the tag the action really wrote is observable here without a worker.
    QVERIFY2(model->threadAt(0).isFlagged(),
             "the renamed action no longer writes the `flagged` tag");
    QVERIFY2(model->threadAt(0).tags.contains(QStringLiteral("flagged")),
             "the tag written was not `flagged`");
    QVERIFY2(!model->threadAt(0).tags.contains(QStringLiteral("important")),
             "the rename reached the mail store: an `important` tag was "
             "written, which no other tool reading this Maildir knows");
}

void TestMainWindow::theToolbarUsesTheConfiguredIconSize()
{
    // With the toolbar following the desktop's "Icon only" style, the icons are
    // the whole control, and this style reports 16px, which is a small target.
    // The size is configurable with a 24px default; this proves the config
    // value actually reaches the widget rather than sitting in Config unread.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[general]\ntoolbar_icon_size = 40\n");
    file.close();

    Config config;
    config.load(path);
    QCOMPARE(config.toolbarIconSize(), 40);

    MainWindow window(config);
    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("main_toolbar"));
    QVERIFY(toolBar);

    // 40 is deliberately not any of this style's own metrics (16 small, 32
    // large), so the assertion cannot pass by the widget happening to agree
    // with the theme.
    QCOMPARE(toolBar->iconSize(), QSize(40, 40));
}

void TestMainWindow::noTwoActionsShareAnIcon()
{
    // Reported by the user against the icons shipped in 0.12.0: Archive and
    // Mark all read both used `mail-mark-read`. With the toolbar following a
    // desktop set to icon-only, the icon is the entire control, so two buttons
    // with different consequences were indistinguishable.
    //
    // Asserted over every action rather than that one pair, because the defect
    // is the class and not the instance: the icon table is hand-written and
    // twenty-four entries long, so the next duplicate is a plausible typo.
    //
    // Compared by cacheKey() rather than by the theme NAME, which this window
    // does not keep. Two distinct names that resolve to the same art on a given
    // theme are just as ambiguous on screen, and that is what the user sees.
    const Config config;
    MainWindow window(config);

    QHash<qint64, QString> owners;
    QStringList collisions;
    int withIcons = 0;

    for (const QString &name : KeyMap::knownActions()) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action named %1").arg(name)));
        if (action->icon().isNull())
            continue;

        ++withIcons;
        const qint64 key = action->icon().cacheKey();
        const auto existing = owners.constFind(key);
        if (existing != owners.constEnd()) {
            collisions.append(QStringLiteral("%1 and %2")
                                  .arg(existing.value(), name));
        } else {
            owners.insert(key, name);
        }
    }

    // The guard. On a theme that resolves nothing every icon is null, the loop
    // body never runs, and the assertion below would pass having compared
    // nothing at all.
    QVERIFY2(withIcons >= KeyMap::knownActions().size(),
             qPrintable(QStringLiteral("only %1 of %2 actions had an icon to "
                                       "compare")
                            .arg(withIcons)
                            .arg(KeyMap::knownActions().size())));

    QVERIFY2(collisions.isEmpty(),
             qPrintable(QStringLiteral("actions sharing one icon: %1")
                            .arg(collisions.join(QStringLiteral("; ")))));
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
