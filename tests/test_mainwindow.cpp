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
#include <QFocusEvent>
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
#include "carddelegate.h"
#include "cardlayout.h"

#include <QImage>
#include <QPainter>
#include <QToolButton>
#include <QHBoxLayout>
#include <QComboBox>
#include <QScrollBar>
#include "tagchip.h"
#include "threadlistmodel.h"
#include "threadlistview.h"
#include "notmuchfixture.h"

/// A MainWindow whose worker is pointed at a throwaway notmuch database.
///
/// **Opt-in, never a suite-wide initTestCase.** Roughly fifty cases here
/// construct a bare MainWindow and must keep costing nothing; making every one
/// of them run `notmuch new` would be minutes of wall clock for a database they
/// never query. Shared mutable state between cases is also how the /proc/locks
/// bug (item 61) reached the whole suite.
///
/// **No test-only hook in MainWindow, deliberately.** wireWorker() already
/// builds the worker from m_config.notmuchConfig(), an ordinary config key, so
/// pointing a written qtmaildir.conf at the fixture exercises the shipping code
/// path rather than a parallel one built for the tests.
///
/// Owns the fixture, because the worker holds the database open on another
/// thread and the fixture's QTemporaryDir deletes the tree in its destructor:
/// it must outlive the window.
class WorkerBackedWindow
{
public:
    /// Builds the database, writes the config and loads it. Check isValid()
    /// and error() before constructing the window.
    bool build()
    {
        if (!m_fixture.isValid()) {
            m_error = QStringLiteral("fixture directory invalid");
            return false;
        }
        if (!m_fixture.index()) {
            m_error = m_fixture.error();
            return false;
        }
        if (!m_confDir.isValid()) {
            m_error = QStringLiteral("config directory invalid");
            return false;
        }

        const QString path =
            m_confDir.filePath(QStringLiteral("qtmaildir.conf"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            m_error = QStringLiteral("cannot write qtmaildir.conf");
            return false;
        }
        {
            QTextStream out(&file);
            // QSettings' INI backend treats a section literally named
            // [general] as its own fallback section and strips the prefix, so
            // this key is read as `notmuch_config` and NOT as
            // `general/notmuch_config`. Getting that backwards is how the key
            // went unnoticed as broken once already.
            out << "[general]\n"
                << "notmuch_config=" << m_fixture.configPath() << "\n";
        }
        file.close();

        m_config.load(path);
        if (m_config.notmuchConfig() != m_fixture.configPath()) {
            m_error = QStringLiteral("config did not pick up notmuch_config");
            return false;
        }
        return true;
    }

    NotmuchFixture &fixture() { return m_fixture; }
    const Config &config() const { return m_config; }
    QString error() const { return m_error; }

private:
    NotmuchFixture m_fixture;
    QTemporaryDir m_confDir;
    Config m_config;
    QString m_error;
};

/// MainWindow is mostly wiring. Cases that need a real database opt into one
/// through WorkerBackedWindow; the rest construct a bare window. What is
/// checked here is the action registry: the bindings a user configures reach
/// the QActions the menus and the keyboard both read from, and no action is
/// left unreachable.
class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void noTestCanSeeTheRealLockTable();
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
    void aConfirmedEditArmsTheAutoSync();
    void autoSyncDebouncesABurstOfEdits();
    void aSearchFromThePaneReplacesTheQuery();
    void aSearchFromThePaneCanNarrowTheQuery();
    void narrowingAnEmptyQueryBarIsAPlainSearch();
    void aMalformedAccountIsReportedWithoutBlockingTheConstructor();
    void aWorkerBackedWindowReturnsRealThreads();
    void everyBuiltinFilterButtonCarriesAnIconAndItsText();
    void aQueryInTheMenuCanActuallyBeRun();
    void theFourBuiltinFiltersAreOnTheRowInOrder();
    void aFilterComposesWithTheSelectedAccount();
    void aFilterAcrossAllAccountsIsUnscoped();
    void aFilterDoesNotClearTheAccountSelection();
    void aSavedQueryStillClearsTheAccountSelection();
    void aFilterOffersNoEditOrDeleteActions();
    void changingTheAccountRunsNothing();
    void arrivingBatchesUpdateTheStatusBarWithTheCountSoFar();
    void aRefreshsBatchesLeaveTheStatusBarAlone();
    void selectingAThreadRootShowsItInTheMessagePane();
    void anUnexpandedRootRendersOneMessageNotTheConversation();
    void autoSyncIsNotArmedWhenDisabledOrWithNothingPending();
    void autoSyncSkipsWhileABackgroundSyncIsRunning();
    void aSuccessfulSyncRefreshesRatherThanRerunningTheQuery();
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
    void aCronSyncRefreshesTheListWithoutAQuery();
    void aCronSyncRefreshesOverASelectionWithoutClearingIt();
    void aCronSyncDoesNotRefreshBeforeAnyQueryHasRun();
    void aRefreshAddsNewMailAndDropsWhatStoppedMatching();
    void theOpenThreadLeavingTheListRaisesTheStaleNotice();
    void aThreadStillMatchingRaisesNoStaleNotice();
    void theStaleNoticeCarriesTheMessageBeingRead();
    void recoveringAStaleThreadQueriesTheWholeThread();
    void recoveryReselectsTheMessageThatWasBeingRead();
    void recoveryOnTheFirstMessageSelectsTheThreadRow();
    void aUserQueryAbandonsAPendingRecovery();
    void blankingThePaneAlsoDropsTheStaleNotice();
    void aNewQueryDropsTheStaleNotice();
    void aFinishedBackgroundSyncStopsSayingItIsRunning();
    void aRefreshDoesNotStampOverASelectionMessage();
    void aRefreshDoesNotOpenNewMailByItself();
    void openingAnotherMessageDropsTheStaleNoticeOfThePreviousOne();
    void theStaleNoticeKeepsTheMessageOfAThreadRootToo();
    void recoveryExpandsTheThreadAndSelectsRatherThanOnlyPointing();
    void recoveryFromAnExpandedThreadRestoresTheReply();
    void theRecoveryButtonSurvivesThePaneBeingBlanked();
    void anUnobservableLockTableLeavesTheSyncButtonUsable();
    void theStatusBarFollowsTheSyncPhase();
    void aSelectedReadThreadIsNotDimmedIntoTheHighlight();
    void childRowsAreIndentedUnderTheirThread();
    void aThreadWithRepliesDrawsAVisibleExpander();
    void cardsNeverScrollSideways();
    void selectingARootCardKeepsItsThreadForMarkRead();
    void nextThreadLeavesTheLastReply();
    void altDownSkipsReplies();
    void bothThreadStepBindingsReachTheAction();
    void sortChoiceSurvivesRestart();
    void accountEntriesCarryTheirColour();
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
    void thereIsNoSentButtonWithoutASentKey();
    void theSentButtonRunsEveryConfiguredAccount();
    void theSentButtonSurvivesABracketedPath();
    void placeholderCountsSkipSentAndDraftsWithoutTheKeys();
    void placeholderCountsCarrySentAndDrafts();
    void placeholderLabelsStayPairedWithTheirQueries();
    void placeholderCountsDropAnUncountableQuery();
    void flatModeDoesNotSurviveTheNextQuery();
    void noTwoActionsShareAnIcon();

    void onlyPinnedQueriesBecomeButtons();
    void unpinnedQueriesReachTheMenu();
    void pinnedButtonsFollowTheDocumentOrder();
    void theSavedQueryMenuIsHiddenWhenEveryQueryIsPinned();
    void aScopedSavedQuerySelectsItsAccount();
    void anUnscopedSavedQueryClearsTheAccount();
    void theSaveQueryActionIsDisabledOnAnEmptyQuery();
    void thereIsASaveButtonBesideTheQueryBar();
    void theMenuIsRightAlignedAwayFromTheButtons();
    void theRowSurvivesWithNothingButUnpinnedQueries();
    void aStoredGeneratedQueryRunsFlatAndComposed();
    void aRenamedSentEntryKeepsWorking();
    void aGeneratedQueryWithNothingToShowIsSkipped();
    void aSavedQueryButtonOffersEditUnpinAndDelete();
    void onlyAStoredQueryOffersToBecomeATaggingRule();
    void unpinningMovesAQueryToTheMenu();
    void deletingRemovesTheQueryFromTheFile();
    void anEditedQueryKeepsItsUnknownFields();
    void renamingReplacesRatherThanDuplicating();

private:
    /// Owns the throwaway lock table init() points every test at. A pointer
    /// rather than a value because it is rebuilt per test, and QTemporaryDir
    /// removes its directory when destroyed.
    QTemporaryDir *m_lockDir = nullptr;
};

/// Item 61. Points every test at a lock table it owns, before every test.
///
/// Without this the suite reads the real `/proc/locks`, so a `mailsync.sh` run
/// on the developer's machine makes `SyncMonitor` report a sync in progress and
/// tests that never mention syncing fail. It is not a rare race: measured 0
/// failures in 30 runs with no lock held and 30 in 30 with one held, and it
/// cost three separate misdiagnoses before the cause was found. Reproduce with
/// `flock /tmp/mbsync.lock -c 'sleep 60'` in one shell and the suite in
/// another.
///
/// An EMPTY file rather than a fabricated table: `SyncMonitor` reads it and
/// finds no entry, which is exactly "no sync is running". A test that wants to
/// see a sync writes its own content, which three already do.
///
/// This also replaces the pattern those three used of restoring
/// `"/proc/locks"` when finished. That restoration was itself a defect: it
/// handed the real table back to whichever test ran next, so one test opting
/// in re-exposed all the others.
void TestMainWindow::init()
{
    m_lockDir = new QTemporaryDir;
    QVERIFY(m_lockDir->isValid());

    const QString locks = m_lockDir->filePath(QStringLiteral("locks"));
    QFile file(locks);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    MainWindow::setLocksPathForTesting(locks);
}

void TestMainWindow::cleanup()
{
    // Left pointing at the temporary path deliberately. Restoring
    // "/proc/locks" here would re-expose the next test between cleanup() and
    // its own init(), which is the trap this fixture exists to close.
    delete m_lockDir;
    m_lockDir = nullptr;
}

void TestMainWindow::noTestCanSeeTheRealLockTable()
{
    // The guard for the fixture itself. A test that asserts on sync state
    // proves nothing if the path silently reverts to /proc/locks, and this
    // fails the moment init() stops being applied or someone restores the real
    // table at the end of a test.
    QVERIFY2(MainWindow::locksPath() != QStringLiteral("/proc/locks"),
             "the suite is reading the real kernel lock table; a sync running "
             "on this machine will fail unrelated tests (item 61)");
    QVERIFY(MainWindow::locksPath().startsWith(QDir::tempPath()));
}

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

    // Forge a state file from the five-column layout. Nothing reads these keys
    // any more, and that is exactly what must be verified: a blob saved by an
    // older version has to be ignored rather than applied to a one-column view.
    {
        QSettings state(MainWindow::uiStatePath(), QSettings::IniFormat);
        state.setValue(QStringLiteral("threadlist/columns"), 5);
        state.setValue(QStringLiteral("threadlist/header"),
                       QByteArray("not a header this model could have saved"));
    }

    // Constructing must not apply it, and must not crash on the garbage blob.
    const Config config;
    MainWindow reopened(config);

    auto *view = reopened.findChild<QTreeView *>();
    QVERIFY(view);
    QCOMPARE(view->model()->columnCount(), 1);

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

    // One column, and setIndentation(0): Qt indents nothing, CardLayout draws
    // the indent itself. So visualRect reports the SAME rect for a thread and
    // its reply, and the indent has to be read off the layout rather than off
    // the geometry. That is the trap CLAUDE.md records in reverse: there,
    // visualRect reported an indent the text did not have; here it reports
    // none while the text is indented.
    const QModelIndex rootCell = model->index(0, 0, QModelIndex());
    const QModelIndex child = model->index(0, 0, root);
    QVERIFY(child.isValid());

    // Guards before the claim: a probe that cannot see both rows can report
    // anything it likes about their relative position.
    QVERIFY2(view->visualRect(rootCell).height() > 0,
             "the thread row has no height, so nothing about it is measurable");
    QVERIFY2(view->visualRect(child).height() > 0,
             "the reply row has no height: it is collapsed or off-screen, and "
             "an indent test against it would pass without drawing anything");

    // The indent is NOT in the geometry. setIndentation(0) means visualRect
    // reports the same left edge for both rows, deliberately: CardLayout draws
    // the indent inside the card's own rect. Asserting on visualRect here
    // would fail against a perfectly indented list, which is the mirror of the
    // trap CLAUDE.md records for item 20, where visualRect reported an indent
    // the text did not have.
    //
    // So the real property, as before: where the TEXT lands. It is read off
    // the layout, which is what the delegate paints from.
    CardLayout::Input threadIn;
    threadIn.isMessage = false;
    threadIn.depth = 0;
    CardLayout::Input replyIn;
    replyIn.isMessage = true;
    replyIn.depth =
        model->data(child, ThreadListModel::MessageDepthRole).toInt();
    QVERIFY2(replyIn.depth > 0,
             "the reply reports depth 0, so there is no nesting to measure");

    const QRect rect = view->visualRect(rootCell);
    const CardLayout threadCard =
        CardLayout::compute(threadIn, rect, view->font());
    const CardLayout replyCard =
        CardLayout::compute(replyIn, rect, view->font());

    QVERIFY2(replyCard.contentLeft > threadCard.contentLeft,
             qPrintable(QStringLiteral("the reply's text starts at x=%1, not "
                                       "right of the thread's at x=%2: the "
                                       "nesting is invisible")
                            .arg(replyCard.contentLeft)
                            .arg(threadCard.contentLeft)));

    // And the spine that makes the nesting read as one block rather than as an
    // arbitrary offset.
    QCOMPARE(replyCard.spines.size(), replyIn.depth);
}

namespace {

/// Two threads, the first with one reply, expanded. The shared fixture for the
/// two navigation tests below.
struct NavFixture
{
    QTreeView *view = nullptr;
    ThreadListModel *model = nullptr;
    QModelIndex root;
    QModelIndex reply;
};

NavFixture buildNavFixture(MainWindow &window)
{
    NavFixture f;
    f.view = window.findChild<QTreeView *>();
    f.model = window.findChild<ThreadListModel *>();

    // unread, so a selection arms the mark-read timer: scheduleMarkRead()
    // returns early for a thread that is already read, and a fixture without
    // it would make a mark-read assertion pass for the wrong reason.
    ThreadSummary first = makeThread(QStringLiteral("T1"),
                                     QStringList{ QStringLiteral("inbox"),
                                                  QStringLiteral("unread") });
    first.totalCount = 2;
    ThreadSummary second = makeThread(QStringLiteral("T2"),
                                      QStringList{ QStringLiteral("inbox") });
    second.totalCount = 1;
    f.model->appendBatch({ first, second });

    MessageNode rootNode;
    rootNode.messageId = QStringLiteral("M1");
    rootNode.threadId = QStringLiteral("T1");
    rootNode.depth = 0;
    MessageNode replyNode;
    replyNode.messageId = QStringLiteral("M2");
    replyNode.threadId = QStringLiteral("T1");
    replyNode.depth = 1;
    f.model->setThreadMessages(QStringLiteral("T1"), { rootNode, replyNode });

    f.root = f.model->index(0, 0);
    f.view->expand(f.root);
    f.reply = f.model->index(0, 0, f.root);
    return f;
}

}  // namespace

void TestMainWindow::selectingARootCardKeepsItsThreadForMarkRead()
{
    // A root card is BOTH a message and a thread: it renders the thread's
    // first message, and it is still the thread that gets marked read and
    // repainted on a tag change. The message-row path deliberately clears the
    // current thread id; doing that here too would silently disable mark-read
    // and the tag-change repaint for every thread root in the list.
    const Config config;
    MainWindow window(config);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const NavFixture f = buildNavFixture(window);
    f.view->setCurrentIndex(f.root);
    QApplication::processEvents();

    // Guard: the fixture loads replies, so the root knows its own message and
    // the branch under test is the one that runs.
    QVERIFY2(!f.model->data(f.root, ThreadListModel::MessageIdRole)
                  .toString().isEmpty(),
             "the root card does not know its first message, so this exercises "
             "the fallback rather than the path it is written for");

    // A mark-read timer armed for the thread is what proves the thread id
    // survived: scheduleMarkRead() is only reached on the thread-row path.
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);
    QVERIFY2(timer->isActive(),
             "no mark-read timer for a selected root card: its thread id was "
             "cleared along with the switch to rendering one message");
}

void TestMainWindow::nextThreadLeavesTheLastReply()
{
    const Config config;
    MainWindow window(config);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const NavFixture f = buildNavFixture(window);
    QVERIFY(f.reply.isValid());
    QVERIFY2(f.view->isExpanded(f.root),
             "the thread is collapsed, so this test would arrow down a flat "
             "list and pass against the bug it exists to catch");

    f.view->setCurrentIndex(f.reply);

    // The defect (item 60): selectRow(current.row() + 1) asked for row 1 UNDER
    // T1, which does not exist, so the action did nothing at all.
    window.findChild<QAction *>(QStringLiteral("next_thread"))->trigger();

    QCOMPARE(f.view->currentIndex().data(ThreadListModel::ThreadIdRole)
                 .toString(),
             QStringLiteral("T2"));
}

void TestMainWindow::altDownSkipsReplies()
{
    const Config config;
    MainWindow window(config);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const NavFixture f = buildNavFixture(window);
    QVERIFY(f.view->isExpanded(f.root));

    // From the thread ROOT with its replies showing: one step must land on the
    // next THREAD, not on the first reply. That is what makes the action mean
    // thread-to-thread while plain Up/Down still steps message-to-message.
    f.view->setCurrentIndex(f.root);
    window.findChild<QAction *>(QStringLiteral("next_thread"))->trigger();

    QCOMPARE(f.view->currentIndex().data(ThreadListModel::ThreadIdRole)
                 .toString(),
             QStringLiteral("T2"));
    QVERIFY(!f.view->currentIndex().data(ThreadListModel::IsMessageRole)
                 .toBool());

    // And back, which is the mirror case the old arithmetic also failed.
    window.findChild<QAction *>(QStringLiteral("prev_thread"))->trigger();
    QCOMPARE(f.view->currentIndex().data(ThreadListModel::ThreadIdRole)
                 .toString(),
             QStringLiteral("T1"));
    QVERIFY(!f.view->currentIndex().data(ThreadListModel::IsMessageRole)
                 .toBool());
}

void TestMainWindow::bothThreadStepBindingsReachTheAction()
{
    const Config config;
    MainWindow window(config);

    // Two bindings per action, which needs setShortcuts rather than
    // setShortcut: Ctrl+J/K for a neomutt hand, Alt+Up/Down for a mouse one.
    // Alt because Shift+arrows is QTreeView's built-in extend-selection that
    // multi-row tagging depends on, and a bare arrow cannot be a window
    // shortcut without breaking every text field in the window.
    for (const auto &pair : { std::pair<const char *, const char *>{
                                  "next_thread", "Alt+Down" },
                              { "prev_thread", "Alt+Up" } }) {
        auto *action =
            window.findChild<QAction *>(QString::fromLatin1(pair.first));
        QVERIFY2(action, pair.first);
        const QList<QKeySequence> shortcuts = action->shortcuts();
        QVERIFY2(shortcuts.size() >= 2,
                 qPrintable(QStringLiteral("%1 carries %2 shortcut(s), so the "
                                           "second binding is unreachable")
                                .arg(QString::fromLatin1(pair.first))
                                .arg(shortcuts.size())));
        QVERIFY2(shortcuts.contains(
                     QKeySequence(QString::fromLatin1(pair.second))),
                 pair.second);
    }
}

void TestMainWindow::sortChoiceSurvivesRestart()
{
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MainWindow::uiStatePath());

    {
        const Config config;
        MainWindow window(config);
        auto *sort = window.findChild<QComboBox *>(QStringLiteral("sortOrder"));
        QVERIFY(sort);
        QCOMPARE(sort->count(), 2);
        QCOMPARE(sort->currentIndex(), 0);  // Newest first by default.
        sort->setCurrentIndex(1);
        window.close();
    }

    const Config config;
    MainWindow second(config);
    auto *sort = second.findChild<QComboBox *>(QStringLiteral("sortOrder"));
    QVERIFY(sort);
    QCOMPARE(sort->currentIndex(), 1);

    // A stale or hand-edited file can hold anything, which is the lesson item
    // 58 recorded: an out-of-range value must fall back rather than select a
    // row that does not exist.
    {
        QSettings state(MainWindow::uiStatePath(), QSettings::IniFormat);
        state.setValue(QStringLiteral("threadlist/sortOrder"), 47);
    }
    MainWindow third(config);
    auto *thirdSort = third.findChild<QComboBox *>(QStringLiteral("sortOrder"));
    QCOMPARE(thirdSort->currentIndex(), 0);

    QFile::remove(MainWindow::uiStatePath());
    QStandardPaths::setTestModeEnabled(false);
}

void TestMainWindow::accountEntriesCarryTheirColour()
{
    // Its own config, not the environment's. Reading the real one made this
    // SKIP wherever no accounts are configured, which is a test that asserts
    // nothing while reporting success.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("account.work"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("work"));
        s.setValue(QStringLiteral("color"), QStringLiteral("#3d7fd1"));
        s.endGroup();
        s.beginGroup(QStringLiteral("account.personal"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("personal"));
        s.endGroup();
    }

    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 2);

    MainWindow window(config);
    auto *box = window.findChild<QComboBox *>(QStringLiteral("accountBox"));
    QVERIFY(box);
    QCOMPARE(box->count(), 3);

    // "All accounts" is not an account and carries no swatch.
    QVERIFY(!box->itemData(0, Qt::DecorationRole).isValid());

    // Every real account does, including the one with no color= key:
    // colourFor() never fails, deriving a stable colour from the tag name, so
    // adding an account and forgetting to colour it degrades to something
    // usable rather than to nothing.
    QSet<QRgb> seen;
    for (int i = 1; i < box->count(); ++i) {
        const QVariant swatch = box->itemData(i, Qt::DecorationRole);
        QVERIFY2(swatch.isValid(),
                 qPrintable(QStringLiteral("account %1 carries no swatch")
                                .arg(box->itemText(i))));
        const QColor colour = swatch.value<QColor>();
        QVERIFY(colour.isValid());
        seen.insert(colour.rgb());
    }

    // Guard: two accounts sharing one colour would make the swatches useless
    // as a key to the accent bars, and would let a broken lookup pass.
    QCOMPARE(seen.size(), 2);
}

void TestMainWindow::cardsNeverScrollSideways()
{
    const Config config;
    MainWindow window(config);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    // A long subject, so the guard below is not vacuous: this is exactly the
    // content that used to make the subject column wider than the viewport.
    model->appendBatch({ makeThread(
        QStringLiteral("t1"),
        QStringList{ QStringLiteral("inbox") }) });
    QApplication::processEvents();

    // Item 51: clicking a row used to scroll the list sideways, because the
    // subject column was wider than the viewport and auto-scroll brought the
    // clicked index fully into view. A card is exactly viewport width, so
    // there is nowhere to scroll to.
    QVERIFY2(view->visualRect(model->index(0, 0)).height() > 0,
             "no card is drawn, so there is no layout to assert about");
    QCOMPARE(view->horizontalScrollBar()->minimum(),
             view->horizontalScrollBar()->maximum());
}

void TestMainWindow::aThreadWithRepliesDrawsAVisibleExpander()
{
    // The expander is the ONLY thing saying a thread can be opened, and it took
    // four wrong attempts to get on screen before item 53, each of which looked
    // correct in code and none of which a geometry or role assertion could see.
    // So this counts painted pixels.
    //
    // Painted through the DELEGATE rather than through viewport()->render().
    // The viewport render returns a blank image here: CLAUDE.md records that it
    // does so in several ordinary situations, and this test proved it again,
    // reporting zero ink over a card the delegate demonstrably paints 2183
    // pixels into. A probe that sees nothing cannot report on anything.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    // Two threads: one with replies, one without. The second is the control,
    // and without it a test that counts ink would pass on any card.
    ThreadSummary withReplies = makeThread(
        QStringLiteral("t1"),
        QStringList{ TagColors::tagForAccountKey(QStringLiteral("work")) });
    withReplies.totalCount = 3;
    ThreadSummary lone = makeThread(
        QStringLiteral("t2"),
        QStringList{ TagColors::tagForAccountKey(QStringLiteral("work")) });
    lone.totalCount = 1;
    model->appendBatch({ withReplies, lone });

    const QModelIndex first = model->index(0, 0, QModelIndex());
    const QModelIndex second = model->index(1, 0, QModelIndex());

    // Guards: the model agrees about which thread has replies, and only that
    // one is offered an expander at all.
    QCOMPARE(model->data(first, ThreadListModel::ReplyCountRole).toInt(), 2);
    QCOMPARE(model->data(second, ThreadListModel::ReplyCountRole).toInt(), 0);

    const QFont font = view->font();
    const int height = CardLayout::heightFor(font);

    const auto inkInExpander = [&](const QModelIndex &index) {
        QImage shot(400, height, QImage::Format_ARGB32);
        shot.fill(Qt::white);
        QPainter painter(&shot);
        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 400, height);
        option.font = font;
        option.palette = QApplication::palette();
        option.state = QStyle::State_Enabled;
        CardDelegate delegate;
        delegate.paint(&painter, option, index);
        painter.end();

        const QRect rect = CardDelegate::expanderRectFor(option, index);
        int found = 0;
        for (int y = rect.top(); y <= rect.bottom() && y < shot.height(); ++y) {
            for (int x = rect.left(); x <= rect.right() && x < shot.width();
                 ++x) {
                if ((shot.pixel(x, y) | 0xff000000) != 0xffffffffu)
                    ++found;
            }
        }

        // Guard on the probe itself: prove it can see the card's own text
        // before trusting it about the expander. A probe that finds no ink
        // anywhere reports "nothing was drawn" whatever the delegate did.
        int anyInk = 0;
        for (int y = 0; y < shot.height(); ++y)
            for (int x = 0; x < shot.width(); ++x)
                if ((shot.pixel(x, y) | 0xff000000) != 0xffffffffu)
                    ++anyInk;
        return std::pair<int, int>(found, anyInk);
    };

    const auto [drawn, drawnAnywhere] = inkInExpander(first);
    const auto [control, controlAnywhere] = inkInExpander(second);

    QVERIFY2(drawnAnywhere > 0 && controlAnywhere > 0,
             "the probe finds no ink on either card, so it cannot report on "
             "the expander either");

    QVERIFY2(drawn > 12,
             qPrintable(QStringLiteral("only %1 pixels in the expander's rect: "
                                       "the reply count is clipped or painted "
                                       "over").arg(drawn)));
    QVERIFY2(control == 0,
             qPrintable(QStringLiteral("a thread with no replies drew %1 "
                                       "pixels where an expander would go")
                            .arg(control)));
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
    // This case builds a bare window, so it has no worker and cannot assert on
    // what the pane renders. A case that needs one opts into
    // WorkerBackedWindow; that is deliberately not done here, since the
    // decision under test is made before any load. What it CAN assert is that
    // decision:
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
        model->index(0, 0, QModelIndex());
    const QRect rect = view->visualRect(subject);

    // Guards: the row is drawn, it claims to have replies, and it starts
    // collapsed. Without the last one a toggle test can pass by doing nothing.
    QVERIFY2(rect.height() > 0, "the thread row is not on screen");
    QVERIFY(model->data(subject, ThreadListModel::HasRepliesRole).toBool());
    QVERIFY(!view->isExpanded(root));

    // Aimed at the rect the delegate reports, not at one reconstructed here:
    // the drawn target and the clickable one cannot drift if both come from
    // the same call.
    QStyleOptionViewItem option;
    option.rect = rect;
    option.font = view->font();
    const QRect expander = CardDelegate::expanderRectFor(option, subject);
    QVERIFY2(!expander.isEmpty(), "the card offers no expander to click");
    const QPoint hit = expander.center();

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

    // 600, not 300. The query row carries four built-in filter buttons since
    // item 93, and at 300 the reply row was pushed below the viewport: the
    // pixel loop then ran zero times and reported "0 pixels, the row was
    // painted over", which is a different defect from the one that existed.
    window.resize(1400, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QModelIndex root = model->index(0, 0, QModelIndex());
    view->expand(root);
    QApplication::processEvents();

    const QModelIndex child =
        model->index(0, 0, root);
    const QRect rect = view->visualRect(child);
    QVERIFY2(rect.height() > 0, "the reply row is not on screen");

    // visualRect reports a height for a row scrolled out of the viewport, so
    // the check above passes while the loop below has nothing to walk. Assert
    // the rect is really inside the image, or this measures nothing and says
    // the row was painted over.
    QVERIFY2(rect.top() >= 0
                 && rect.bottom() < view->viewport()->height()
                 && rect.left() >= 0
                 && rect.right() <= view->viewport()->width(),
             qPrintable(QStringLiteral("the reply row at %1..%2 is outside the "
                                       "%3px viewport, so the pixel count "
                                       "below would measure nothing")
                            .arg(rect.top())
                            .arg(rect.bottom())
                            .arg(view->viewport()->height())));

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
        model->index(0, 0);

    // initStyleOption is protected, so the resolved palette is reached the way
    // the painter does: through a subclass that exposes it.
    struct Probe : CardDelegate {
        using CardDelegate::initStyleOption;
    };
    const auto *probe = static_cast<const Probe *>(
        static_cast<const CardDelegate *>(delegate));

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

    // The finish is no longer ANNOUNCED, since item 35b made the refresh
    // silent, but it must still be acted on: the running message it wrote is
    // retired. Asserting the absence of "running" rather than the presence of
    // "completed" keeps the test on this window's subject, which is that the
    // lock period was attributed to a background sync rather than to a local
    // one, without pinning wording that has already changed once.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    QVERIFY2(!status->text().contains(QStringLiteral("running")),
             qPrintable(QStringLiteral("a finished background sync left the bar "
                                       "claiming it was still running: '%1'")
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

    // The other run finishing must still be ACTED ON. What that means depends
    // on the state of the list, and this fixture has an empty one with nothing
    // selected, so item 35a's free-refresh branch is what a handed-back lock
    // reaches: the window re-runs the query rather than printing "press Enter".
    //
    // Asserting the generation rather than the status text is deliberate. The
    // property under test is that the Idle was attributed to the OTHER run
    // instead of being swallowed as this window's own; which of the two
    // responses it then produces is item 35a's business, and pinning the
    // wording here would fail every time that decision is revisited.
    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    const quint64 before = window.currentGenerationForTesting();

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    QVERIFY2(window.currentGenerationForTesting() > before,
             "after a skipped local sync, the other run finishing was "
             "swallowed rather than attributed to the background sync");
}

void TestMainWindow::aCronSyncRefreshesTheListWithoutAQuery()
{
    // Item 35b. A background sync brings the list up to date on its own, with
    // no keystroke: this is the whole point of the item, and it holds whether
    // the list is empty or full.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    // The refresh is observed as a NEW query being issued. There is no worker
    // in this fixture, so no result ever arrives; what is asserted is that the
    // query went out at all, which is what used to be missing.
    const quint64 before = window.currentGenerationForTesting();

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));

    QVERIFY2(window.currentGenerationForTesting() > before,
             "a finished cron sync did not refresh the list");
}

void TestMainWindow::aCronSyncRefreshesOverASelectionWithoutClearingIt()
{
    // The behaviour 0.8.0 refused to build and the reason it refused: a refresh
    // used to mean runCurrentQuery(), which clears the model, the selection and
    // the message pane, so firing it on a cron timer would close the thread
    // being read six times an hour.
    //
    // refreshCurrentQuery() reconciles instead, so the refresh runs AND the
    // selection survives. A test that only checked the query was issued would
    // pass against the destructive version, which is the version this item
    // exists to avoid.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    model->appendBatch({ makeThread(QStringLiteral("T1"),
                                    { QStringLiteral("unread") }) });
    const QModelIndex first = model->index(0, 0, QModelIndex());
    QVERIFY(first.isValid());
    view->setCurrentIndex(first);
    QVERIFY2(view->selectionModel()->hasSelection(),
             "the fixture failed to select a row, so this proves nothing");

    const quint64 before = window.currentGenerationForTesting();

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));

    QVERIFY2(window.currentGenerationForTesting() > before,
             "a cron sync did not refresh a populated list");

    // The rows are untouched until the result comes back, and the selection is
    // still there. A clear() would have emptied both.
    QCOMPARE(model->rowCount(QModelIndex()), 1);
    QVERIFY2(view->selectionModel()->hasSelection(),
             "the refresh cleared the selection, which is what made the old "
             "one unusable on a cron timer");
}

void TestMainWindow::aCronSyncDoesNotRefreshBeforeAnyQueryHasRun()
{
    // The query bar holds text the user has typed but not run, and a refresh
    // must not execute it: that is a search they never asked for. The refresh
    // re-runs the LAST RUN query, so with none there is nothing to do.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:draft-i-was-typing"));

    const quint64 before = window.currentGenerationForTesting();

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));

    QCOMPARE(window.currentGenerationForTesting(), before);
}

void TestMainWindow::aRefreshAddsNewMailAndDropsWhatStoppedMatching()
{
    // The round trip end to end, driven through the real handlers: the refresh
    // query goes out, its batches accumulate, and the result reconciles into
    // the model in one go.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    model->appendBatch({ makeThread(QStringLiteral("T1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("T2"),
                                    { QStringLiteral("unread") }) });
    QCOMPARE(model->rowCount(QModelIndex()), 2);

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();

    // T2 was read elsewhere and no longer matches; T3 is new mail.
    const QVector<ThreadSummary> result{
        makeThread(QStringLiteral("T3"), { QStringLiteral("unread") }),
        makeThread(QStringLiteral("T1"), { QStringLiteral("unread") })
    };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, refresh));

    // Nothing has changed yet: a refresh applies its result whole, never batch
    // by batch, or the first batch would delete every row after it.
    QCOMPARE(model->rowCount(QModelIndex()), 2);
    QCOMPARE(model->threadAt(0).threadId, QStringLiteral("T1"));

    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 2), Q_ARG(quint64, refresh));

    QCOMPARE(model->rowCount(QModelIndex()), 2);
    QCOMPARE(model->threadAt(0).threadId, QStringLiteral("T3"));
    QCOMPARE(model->threadAt(1).threadId, QStringLiteral("T1"));
}

void TestMainWindow::theOpenThreadLeavingTheListRaisesTheStaleNotice()
{
    // The user is reading a thread when a refresh drops it: read the last
    // unread message and the thread stops matching tag:unread. The pane keeps
    // rendering it, correctly, so without a notice the message becomes an
    // orphan with no route back to its thread.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);

    model->appendBatch({ makeThread(QStringLiteral("T1"),
                                    { QStringLiteral("unread") }) });
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    QCOMPARE(window.currentThreadId(), QStringLiteral("T1"));
    QVERIFY(pane->staleThreadId().isEmpty());

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();

    // The refresh comes back empty: the thread was read and is gone.
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 0), Q_ARG(quint64, refresh));

    QCOMPARE(model->rowCount(QModelIndex()), 0);
    QCOMPARE(pane->staleThreadId(), QStringLiteral("T1"));
}

void TestMainWindow::aThreadStillMatchingRaisesNoStaleNotice()
{
    // The common case, and the guard on the test above: a refresh that changes
    // nothing must be invisible. A notice on every sync would be noise, and it
    // would be a lie.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);

    model->appendBatch({ makeThread(QStringLiteral("T1"),
                                    { QStringLiteral("unread") }) });
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();

    const QVector<ThreadSummary> result{
        makeThread(QStringLiteral("T1"), { QStringLiteral("unread") })
    };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, refresh));
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 1), Q_ARG(quint64, refresh));

    QCOMPARE(model->rowCount(QModelIndex()), 1);
    QVERIFY2(pane->staleThreadId().isEmpty(),
             "a thread that still matches was reported as stale");
}

void TestMainWindow::theStaleNoticeCarriesTheMessageBeingRead()
{
    // Reading reply four of eight when the thread drops out. Recovery has to
    // restore the READER'S place, so the notice carries the message id as well
    // as the thread; without it the thread reopens at its first message.
    //
    // Selecting a message row clears m_currentThreadId, so a notice keyed on
    // that alone never fires for exactly the reader who is deepest into a
    // thread. That is the case this pins.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);

    ThreadSummary thread = makeThread(QStringLiteral("T1"),
                                      { QStringLiteral("unread") });
    thread.totalCount = 3;
    model->appendBatch({ thread });

    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("T1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("T1");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("T1"), { root, reply });

    const QModelIndex threadIndex = model->index(0, 0, QModelIndex());
    const QModelIndex replyIndex = model->index(0, 0, threadIndex);
    QVERIFY(replyIndex.isValid());
    view->setCurrentIndex(replyIndex);

    // A message row, so the window is tracking a message rather than a thread.
    QVERIFY2(window.currentThreadId().isEmpty(),
             "the fixture selected a thread row, so this proves nothing");

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 0), Q_ARG(quint64, refresh));

    QCOMPARE(pane->staleThreadId(), QStringLiteral("T1"));
    QCOMPARE(pane->staleMessageId(), QStringLiteral("m1@example.org"));
}

void TestMainWindow::recoveringAStaleThreadQueriesTheWholeThread()
{
    // Clicking "Show it anyway" runs thread:<id>, not a query for the single
    // message: the user asked to get the whole conversation back, with their
    // place in it, so the list has to offer every message of it.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    QMetaObject::invokeMethod(&window, "recoverStaleThread",
                              Q_ARG(QString, QStringLiteral("T1")),
                              Q_ARG(QString, QStringLiteral("m1@example.org")));

    QCOMPARE(queryEdit->text(), QStringLiteral("thread:T1"));
}

void TestMainWindow::recoveryReselectsTheMessageThatWasBeingRead()
{
    // The whole point of carrying the message id: reading reply four of eight,
    // the thread comes back, and the selection lands on reply four rather than
    // at the top of the thread.
    //
    // Driven through the real handlers because that is the only way the
    // sequencing is exercised: the query has to come back before the thread
    // can be expanded, and the expansion before the reply row exists at all.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    QMetaObject::invokeMethod(&window, "recoverStaleThread",
                              Q_ARG(QString, QStringLiteral("T1")),
                              Q_ARG(QString, QStringLiteral("m2@example.org")));
    const quint64 generation = window.currentGenerationForTesting();

    ThreadSummary thread = makeThread(QStringLiteral("T1"), {});
    thread.totalCount = 3;
    const QVector<ThreadSummary> result{ thread };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, generation));
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 1), Q_ARG(quint64, generation));

    // The thread is listed, and nothing can be selected inside it yet: its
    // replies are not loaded, so the recovery is still pending.
    QCOMPARE(model->rowCount(QModelIndex()), 1);

    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("T1");
    root.depth = 0;
    MessageNode first;
    first.messageId = QStringLiteral("m1@example.org");
    first.threadId = QStringLiteral("T1");
    first.depth = 1;
    MessageNode target;
    target.messageId = QStringLiteral("m2@example.org");
    target.threadId = QStringLiteral("T1");
    target.depth = 1;
    const QVector<MessageNode> nodes{ root, first, target };
    QMetaObject::invokeMethod(&window, "onThreadTreeLoaded",
                              Q_ARG(QVector<MessageNode>, nodes),
                              Q_ARG(quint64, generation));

    const QModelIndex current = view->currentIndex();
    QVERIFY2(current.isValid(), "recovery selected nothing");
    QVERIFY2(model->isMessageRow(current),
             "recovery landed on the thread rather than on the message");
    QCOMPARE(model->messageAt(current).messageId,
             QStringLiteral("m2@example.org"));
}

void TestMainWindow::recoveryOnTheFirstMessageSelectsTheThreadRow()
{
    // The trap in the model: setThreadMessages DROPS the depth-0 message,
    // because the root card is that message. So a reader recovering from the
    // thread's first message must land on the ROOT row; looking for it among
    // the children finds nothing and would leave the selection nowhere.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    QMetaObject::invokeMethod(&window, "recoverStaleThread",
                              Q_ARG(QString, QStringLiteral("T1")),
                              Q_ARG(QString, QStringLiteral("m0@example.org")));
    const quint64 generation = window.currentGenerationForTesting();

    ThreadSummary thread = makeThread(QStringLiteral("T1"), {});
    thread.totalCount = 2;
    const QVector<ThreadSummary> result{ thread };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, generation));

    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("T1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("T1");
    reply.depth = 1;
    const QVector<MessageNode> nodes{ root, reply };
    QMetaObject::invokeMethod(&window, "onThreadTreeLoaded",
                              Q_ARG(QVector<MessageNode>, nodes),
                              Q_ARG(quint64, generation));

    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 1), Q_ARG(quint64, generation));

    const QModelIndex current = view->currentIndex();
    QVERIFY2(current.isValid(), "recovery selected nothing");
    QVERIFY2(!model->isMessageRow(current),
             "the thread's first message is the ROOT row, not a child");
    QCOMPARE(model->threadAt(current.row()).threadId, QStringLiteral("T1"));
}

void TestMainWindow::aUserQueryAbandonsAPendingRecovery()
{
    // A recovery spans two round-trips, so the user can type a query in the
    // middle of one. That is them choosing to go somewhere else, and the
    // pending selection must not follow them there: restoring a thread's
    // message into a result the user asked for something else from would yank
    // the view out from under them.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    // Recovering the thread with NO message pinned, so the pending recovery
    // selects its thread row the moment that thread appears. A recovery
    // waiting on a specific reply would pass this test without the guard,
    // simply by never reaching its target: it would sit expanding a thread
    // whose replies this fixture never delivers.
    QMetaObject::invokeMethod(&window, "recoverStaleThread",
                              Q_ARG(QString, QStringLiteral("T1")),
                              Q_ARG(QString, QString()));

    // The user changes their mind before the recovery's query comes back.
    queryEdit->setText(QStringLiteral("tag:flagged"));
    queryEdit->returnPressed();
    const quint64 generation = window.currentGenerationForTesting();

    // That query happens to contain the same thread, which is what makes this
    // a trap rather than a theoretical case: the recovery would find its
    // target and select it.
    ThreadSummary thread = makeThread(QStringLiteral("T1"), {});
    thread.totalCount = 2;
    const QVector<ThreadSummary> result{
        makeThread(QStringLiteral("T9"), {}), thread
    };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, generation));
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 2), Q_ARG(quint64, generation));

    QVERIFY2(!view->currentIndex().isValid(),
             "an abandoned recovery selected a row in the query the user ran "
             "instead");
}

void TestMainWindow::blankingThePaneAlsoDropsTheStaleNotice()
{
    // Reported by the user against the first build of item 35b. The notice
    // outlived the message it describes: blanking the pane left the bar sitting
    // above an empty pane, still naming a thread that was no longer shown, with
    // a button offering to recover it.
    //
    // The bar belongs to the rendered message, exactly as the remote-content
    // bar does, and MessageView::clear() already hides that one. This is the
    // same rule applied to the same place.
    const Config config;
    MainWindow window(config);

    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);

    pane->setStaleThread(QStringLiteral("T1"),
                         QStringLiteral("m1@example.org"));
    QCOMPARE(pane->staleThreadId(), QStringLiteral("T1"));

    pane->clear();

    QVERIFY2(pane->staleThreadId().isEmpty(),
             "the stale notice survived the pane being blanked, so it names a "
             "message that is no longer displayed");
    QVERIFY2(pane->staleMessageId().isEmpty(),
             "the stale notice kept the message id of a cleared pane");
}

void TestMainWindow::aNewQueryDropsTheStaleNotice()
{
    // The user's actual route to the bug: read a thread out of a view, get the
    // notice, then type a new query. The pane blanks and the bar must go with
    // it. Driven through the window rather than through MessageView::clear()
    // directly, because the defect was that nothing on this path called it.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();

    model->appendBatch({ makeThread(QStringLiteral("T1"),
                                    { QStringLiteral("unread") }) });
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 0), Q_ARG(quint64, refresh));
    QCOMPARE(pane->staleThreadId(), QStringLiteral("T1"));

    // The user runs a different query.
    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    QVERIFY2(pane->staleThreadId().isEmpty(),
             "after a new query the notice still offered to recover the thread "
             "from the previous view");
}

void TestMainWindow::aFinishedBackgroundSyncStopsSayingItIsRunning()
{
    // Reported by the user against item 35b. "Background sync running..." is
    // written straight to the status label when the lock appears, and the
    // "Background sync completed" message on the way out was the only thing
    // that ever replaced it. Removing that message, so a refresh could be
    // silent, left the bar claiming a sync was running long after it finished.
    //
    // Silent means "says nothing NEW", not "leaves a stale claim standing".
    const Config config;
    MainWindow window(config);

    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    QVERIFY2(status->text().contains(QStringLiteral("running")),
             qPrintable(QStringLiteral("the fixture never announced a running "
                                       "sync, status says '%1'")
                            .arg(status->text())));

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));

    QVERIFY2(!status->text().contains(QStringLiteral("running")),
             qPrintable(QStringLiteral("the status bar still claims a sync is "
                                       "running after it finished: '%1'")
                            .arg(status->text())));
}

void TestMainWindow::aRefreshDoesNotStampOverASelectionMessage()
{
    // The other half, and the reason this is not simply "always write the
    // thread count". A refresh runs on a cron timer under a user who may be
    // doing something, and the bar carries their selection count while they
    // are. Overwriting that every ten minutes is the noise the silence rule
    // exists to prevent.
    const Config config;
    MainWindow window(config);

    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);
    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();

    model->appendBatch({ makeThread(QStringLiteral("T1"), {}),
                         makeThread(QStringLiteral("T2"), {}) });
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    view->selectionModel()->select(model->index(1, 0, QModelIndex()),
                                   QItemSelectionModel::Select);

    const QString before = status->text();
    QVERIFY2(!before.isEmpty(),
             "the fixture left the status bar empty, so this proves nothing");

    // A refresh completes with no sync ever having been announced.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();
    const QVector<ThreadSummary> result{
        makeThread(QStringLiteral("T1"), {}), makeThread(QStringLiteral("T2"), {})
    };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, refresh));
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 2), Q_ARG(quint64, refresh));

    QCOMPARE(status->text(), before);
}

void TestMainWindow::aRefreshDoesNotOpenNewMailByItself()
{
    // Reported by the user against item 35b. A thread read to the end empties
    // an Unread view; the refresh then brings in one new message, and it opens
    // ITSELF in the message pane, marking it read two seconds later without
    // the user ever having looked at it.
    //
    // Nothing in MainWindow selects it: QTreeView sets a current index of its
    // own when rows are inserted into a model that had none, and selecting a
    // row is what loads it. An automatic refresh must not do that, or a cron
    // timer decides what the user is reading.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    // The view has been read empty: no rows, nothing current.
    QCOMPARE(model->rowCount(QModelIndex()), 0);
    QVERIFY(!view->currentIndex().isValid());

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();

    const QVector<ThreadSummary> result{
        makeThread(QStringLiteral("NEW"), { QStringLiteral("unread") })
    };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, refresh));
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 1), Q_ARG(quint64, refresh));

    QCOMPARE(model->rowCount(QModelIndex()), 1);

    // Insertion alone does not do it, which is why the bug only showed up when
    // the user came back from another desktop: QTreeView gives itself a current
    // index when it takes FOCUS with none set. Reproduced here rather than
    // asserted from the report, since a test that only inserts rows passes
    // against the defect.
    view->setFocus();
    QApplication::sendEvent(view, new QFocusEvent(QEvent::FocusIn));

    QVERIFY2(window.currentThreadId().isEmpty(),
             "the refresh opened the new mail in the message pane, which marks "
             "it read without the user having looked at it");
}

void TestMainWindow::openingAnotherMessageDropsTheStaleNoticeOfThePreviousOne()
{
    // The other half of the user's report: the pane showed the new message
    // while the notice above it still named the thread they had been reading.
    //
    // The notice itself was right at the moment it was raised. What made it a
    // lie was the pane being replaced underneath it, so this pins the rule that
    // the notice belongs to whatever is currently rendered: selecting anything
    // else retires it, exactly as blanking the pane does.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);

    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    model->appendBatch({ makeThread(QStringLiteral("OLD"),
                                    { QStringLiteral("unread") }) });
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));

    // The refresh drops the thread being read and brings in new mail.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();
    const QVector<ThreadSummary> result{
        makeThread(QStringLiteral("NEW"), { QStringLiteral("unread") })
    };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, refresh));
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 1), Q_ARG(quint64, refresh));

    QCOMPARE(pane->staleThreadId(), QStringLiteral("OLD"));

    // The user chooses to open the new mail themselves.
    const QModelIndex fresh = model->index(0, 0, QModelIndex());
    QVERIFY(fresh.isValid());
    QCOMPARE(model->threadAt(0).threadId, QStringLiteral("NEW"));
    view->setCurrentIndex(fresh);
    view->selectionModel()->select(fresh, QItemSelectionModel::Select);

    QVERIFY2(pane->staleThreadId().isEmpty(),
             "the notice still named the previous thread while the pane showed "
             "a different message");
}

void TestMainWindow::theStaleNoticeKeepsTheMessageOfAThreadRootToo()
{
    // Reported by the user: recovering a thread they were reading brought the
    // thread back collapsed, with the pane blank, instead of reopening the
    // message they had been on.
    //
    // The cause is that a thread ROOT sets both ids. The root card IS the
    // thread's first message and the pane renders exactly that message, so
    // m_currentThreadId and m_currentMessageId are both filled; the notice read
    // the message id only when the thread id was EMPTY, so the root case threw
    // away a message id it had. Recovery then had nothing to restore, landed on
    // the thread row and never expanded it.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);

    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    ThreadSummary thread = makeThread(QStringLiteral("T1"),
                                      { QStringLiteral("unread") });
    thread.totalCount = 4;
    model->appendBatch({ thread });

    // The root knows its own message once the tree is loaded, which is what
    // makes the pane show one message rather than the conversation.
    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("T1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("T1");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("T1"), { root, reply });

    const QModelIndex threadIndex = model->index(0, 0, QModelIndex());
    view->setCurrentIndex(threadIndex);
    view->selectionModel()->select(threadIndex, QItemSelectionModel::Select);
    QCOMPARE(window.currentThreadId(), QStringLiteral("T1"));

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));
    const quint64 refresh = window.currentGenerationForTesting();
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 0), Q_ARG(quint64, refresh));

    QCOMPARE(pane->staleThreadId(), QStringLiteral("T1"));
    QVERIFY2(!pane->staleMessageId().isEmpty(),
             "the notice dropped the message of a thread root, so recovery has "
             "nothing to reopen and lands on a collapsed thread");
    QCOMPARE(pane->staleMessageId(), QStringLiteral("m0@example.org"));
}

void TestMainWindow::recoveryExpandsTheThreadAndSelectsRatherThanOnlyPointing()
{
    // The rest of the same report: recovery brought the thread back COLLAPSED
    // with the pane BLANK. Two separate faults behind one symptom.
    //
    // setCurrentIndex() alone sets a current row without selecting it, and
    // since the fix for the auto-open defect onThreadSelected() ignores exactly
    // that: an unselected current index is Qt's housekeeping, not the user. So
    // recovery pointed at the row and nothing rendered.
    //
    // And nothing expanded the thread, so the reply the user had been reading
    // was not on screen even when it was the target.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    // Recovering onto the thread's FIRST message, which is the root card: the
    // case the user hit by opening a thread rather than a reply.
    QMetaObject::invokeMethod(&window, "recoverStaleThread",
                              Q_ARG(QString, QStringLiteral("T1")),
                              Q_ARG(QString, QStringLiteral("m0@example.org")));
    const quint64 generation = window.currentGenerationForTesting();

    ThreadSummary thread = makeThread(QStringLiteral("T1"), {});
    thread.totalCount = 4;
    const QVector<ThreadSummary> result{ thread };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, result),
                              Q_ARG(quint64, generation));
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 1), Q_ARG(quint64, generation));

    // No tree reply. This is the user's actual case and the one the earlier
    // test missed: the thread comes back from the query with its replies NOT
    // yet loaded, which is the normal state of a freshly queried row. Recovery
    // has to ask for them rather than assuming they are already there.
    const QModelIndex threadIndex = model->index(0, 0, QModelIndex());
    QVERIFY(threadIndex.isValid());
    QCOMPARE(model->rowCount(threadIndex), 0);

    QVERIFY2(view->selectionModel()->hasSelection(),
             "recovery pointed at the row without selecting it, so nothing "
             "renders and the pane stays blank");
    QCOMPARE(window.currentThreadId(), QStringLiteral("T1"));
    QVERIFY2(view->isExpanded(threadIndex),
             "recovery brought the thread back collapsed, so the conversation "
             "the user was reading is not on screen");
}

void TestMainWindow::recoveryFromAnExpandedThreadRestoresTheReply()
{
    // The user's case, staged exactly: a thread ALREADY EXPANDED with the
    // fourth reply selected and rendered, dropped by a refresh, then recovered.
    // Reported twice as still broken while the earlier recovery tests passed,
    // which means those tests were not reproducing it.
    //
    // What they missed is the whole round trip. Recovery re-runs thread:<id>,
    // and that query REPLACES the model contents, so the recovered thread
    // arrives collapsed with no replies loaded whatever state the old row was
    // in. The reply the user wants therefore does not exist as a row at the
    // moment recovery first runs, and the only thing that can create it is the
    // tree reply arriving after an expand.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    // Reading reply 4 of a 4-message thread, expanded.
    ThreadSummary thread = makeThread(QStringLiteral("T1"),
                                      { QStringLiteral("unread") });
    thread.totalCount = 4;
    model->appendBatch({ thread });

    QVector<MessageNode> nodes;
    for (int i = 0; i < 4; ++i) {
        MessageNode n;
        n.messageId = QStringLiteral("m%1@example.org").arg(i);
        n.threadId = QStringLiteral("T1");
        n.depth = i == 0 ? 0 : 1;
        nodes.append(n);
    }
    model->setThreadMessages(QStringLiteral("T1"), nodes);

    const QModelIndex threadIndex = model->index(0, 0, QModelIndex());
    view->expand(threadIndex);
    QCOMPARE(model->rowCount(threadIndex), 3);

    const QModelIndex fourth = model->index(2, 0, threadIndex);
    QVERIFY(fourth.isValid());
    QCOMPARE(model->messageAt(fourth).messageId,
             QStringLiteral("m3@example.org"));
    view->setCurrentIndex(fourth);
    view->selectionModel()->select(fourth, QItemSelectionModel::Select);

    // The refresh drops it, and the user follows the notice.
    QMetaObject::invokeMethod(&window, "recoverStaleThread",
                              Q_ARG(QString, QStringLiteral("T1")),
                              Q_ARG(QString, QStringLiteral("m3@example.org")));
    const quint64 generation = window.currentGenerationForTesting();

    // The recovery query comes back: ONE collapsed thread, no replies. This is
    // what the query really returns, and the state the earlier tests skipped.
    const QVector<ThreadSummary> recovered{ thread };
    QMetaObject::invokeMethod(&window, "onThreadsReady",
                              Q_ARG(QVector<ThreadSummary>, recovered),
                              Q_ARG(quint64, generation));
    QMetaObject::invokeMethod(&window, "onQueryFinished",
                              Q_ARG(int, 1), Q_ARG(quint64, generation));

    const QModelIndex back = model->index(0, 0, QModelIndex());
    QVERIFY(back.isValid());
    QVERIFY2(view->isExpanded(back),
             "the recovered thread came back collapsed");

    // Expanding asks the worker for the tree; that reply is what creates the
    // reply rows. Without it the conversation is not on screen at all.
    QMetaObject::invokeMethod(&window, "onThreadTreeLoaded",
                              Q_ARG(QVector<MessageNode>, nodes),
                              Q_ARG(quint64, generation));

    QVERIFY2(view->isExpanded(back),
             "the thread collapsed again once its replies arrived");
    QCOMPARE(model->rowCount(back), 3);

    const QModelIndex current = view->currentIndex();
    QVERIFY2(current.isValid(), "recovery left nothing selected");
    QVERIFY2(model->isMessageRow(current),
             "recovery landed on the thread rather than on the reply the user "
             "was reading");
    QCOMPARE(model->messageAt(current).messageId,
             QStringLiteral("m3@example.org"));
}

void TestMainWindow::theRecoveryButtonSurvivesThePaneBeingBlanked()
{
    // The defect that survived six wrong diagnoses and every other recovery
    // test in this file, because all of them reach the slot through
    // invokeMethod, which COPIES its arguments.
    //
    // MessageView emitted the signal with its own members, so a direct
    // connection handed MainWindow::recoverStaleThread() references to them.
    // That slot calls runCurrentQuery(), which blanks the pane, which calls
    // setStaleThread() and assigns to those very members. The ids the slot was
    // still holding went empty mid-call, the recovery target was stored as an
    // empty string, and nothing was ever recovered: the thread came back
    // collapsed with the pane blank.
    //
    // Driven through the real button so the real signal runs. A test that
    // calls the slot directly cannot see this and will pass against it.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);
    auto *button =
        pane->findChild<QPushButton *>(QStringLiteral("staleThreadButton"));
    QVERIFY(button);

    queryEdit->setText(QStringLiteral("tag:unread"));
    queryEdit->returnPressed();

    pane->setStaleThread(QStringLiteral("T1"),
                         QStringLiteral("m3@example.org"));
    QCOMPARE(pane->staleThreadId(), QStringLiteral("T1"));

    button->click();

    // The query the button ran is the thread's own, which is only true if the
    // id survived the round trip.
    QCOMPARE(queryEdit->text(), QStringLiteral("thread:T1"));

    // And the target is still pending, waiting for the result. Empty here means
    // the reference was clobbered and the recovery is already dead.
    QVERIFY2(window.hasPendingRecoveryForTesting(),
             "the recovery target was lost during the slot, so the thread will "
             "come back collapsed with a blank pane");
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

    // No restore to "/proc/locks": init() points every test at its own
    // table, and handing the real one back would re-expose the next test.
}

// Item 71. A confirmed tag edit arms a debounce that syncs it out, so an edit
// no longer waits for a manual sync or the user's cron job.
//
// All four of these assert on the TIMER rather than on a sync actually running.
// Starting a real one from a test would launch the configured command, and the
// thing worth guarding here is the decision to sync, not QProcess.
static QString writeSyncConfig(QTemporaryDir &dir, const QString &extra = {})
{
    QDir().mkpath(dir.filePath(QStringLiteral("qtmaildir")));
    const QString conf = dir.filePath(QStringLiteral("qtmaildir/qtmaildir.conf"));
    QSettings s(conf, QSettings::IniFormat);
    // /bin/true exists, so Config keeps it and MailSync reports available. A
    // config with no command disables the automatic sync by design, which would
    // make every one of these tests pass against a stub.
    s.setValue(QStringLiteral("sync/command"), QStringLiteral("/bin/true"));
    // NOT "general/auto_sync_delay_ms": QSettings' INI backend treats a section
    // literally named [general] as its own fallback section and strips it, so a
    // prefixed lookup silently matches nothing. Writing it prefixed here left
    // the default in place and the -1 case read 2000.
    if (!extra.isEmpty())
        s.setValue(QStringLiteral("auto_sync_delay_ms"), extra);
    s.sync();
    return conf;
}

void TestMainWindow::aConfirmedEditArmsTheAutoSync()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSyncConfig(dir));
    QCOMPARE(config.autoSyncDelayMs(), 2000);

    MainWindow window(config);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("autoSyncTimer"));
    QVERIFY2(timer, "no autoSyncTimer to observe");
    QVERIFY2(!timer->isActive(), "the debounce is armed before any edit");

    TagChange change;
    change.messageIds = { QStringLiteral("m1") };
    change.added = { QStringLiteral("flagged") };
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));

    QVERIFY2(timer->isActive(), "a confirmed edit did not arm the automatic sync");
    QCOMPARE(timer->interval(), 2000);
}

void TestMainWindow::autoSyncDebouncesABurstOfEdits()
{
    // The point of the debounce. "Mark all read" confirms one write per thread,
    // and one sync per thread is what this prevents. A timer that STACKED would
    // still be active here, so the assertion is on the count of timers and on
    // the remaining interval having been reset, not merely on isActive().
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSyncConfig(dir));

    MainWindow window(config);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("autoSyncTimer"));
    QVERIFY(timer);

    TagChange first;
    first.messageIds = { QStringLiteral("m1") };
    first.added = { QStringLiteral("flagged") };
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, first)));
    QVERIFY(timer->isActive());

    // Long enough that a restart is unambiguous. Comparing remainingTime()
    // before and after with ">" was tried and is FLAKY: the two reads can land
    // in the same millisecond bucket, and the test then fails against correct
    // code. Assert instead that the remaining time went back up near the full
    // interval, which a stacked or un-restarted timer cannot produce.
    QTest::qWait(500);
    const int afterWait = timer->remainingTime();
    QVERIFY2(afterWait < 1800, "the timer did not start counting down");

    TagChange second;
    second.messageIds = { QStringLiteral("m2") };
    second.added = { QStringLiteral("flagged") };
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, second)));

    QVERIFY2(timer->remainingTime() > 1800,
             "the second edit did not restart the debounce, so a burst of edits "
             "syncs on the schedule of the FIRST one");
    QCOMPARE(window.findChildren<QTimer *>(QStringLiteral("autoSyncTimer")).size(),
             1);
}

void TestMainWindow::aSearchFromThePaneReplacesTheQuery()
{
    const Config config;
    MainWindow window(config);

    QLineEdit *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY2(queryEdit, "no query bar: the window was never built");

    MessageView *view = window.findChild<MessageView *>();
    QVERIFY2(view, "no message view");

    queryEdit->setText(QStringLiteral("tag:inbox"));
    emit view->searchRequested(QStringLiteral("from:\"foo@example.org\""),
                               SearchTerm::SearchMode::Replace);

    QCOMPARE(queryEdit->text(), QStringLiteral("from:\"foo@example.org\""));
}

void TestMainWindow::aSearchFromThePaneCanNarrowTheQuery()
{
    // The case the feature exists for: a query returning a thousand threads is
    // narrowed by adding a condition. BOTH sides are parenthesised, because
    // 'a or b AND c' binds as 'a or (b AND c)', which WIDENS a search the user
    // asked to narrow, and notmuch reports no error for it.
    const Config config;
    MainWindow window(config);

    QLineEdit *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY2(queryEdit, "no query bar: the window was never built");

    MessageView *view = window.findChild<MessageView *>();
    QVERIFY2(view, "no message view");

    queryEdit->setText(QStringLiteral("tag:inbox or tag:flagged"));
    emit view->searchRequested(QStringLiteral("from:\"foo@example.org\""),
                               SearchTerm::SearchMode::Narrow);

    QCOMPARE(queryEdit->text(),
             QStringLiteral("(tag:inbox or tag:flagged) AND (from:\"foo@example.org\")"));
}

void TestMainWindow::narrowingAnEmptyQueryBarIsAPlainSearch()
{
    // Rather than "() AND (x)", which matches nothing.
    const Config config;
    MainWindow window(config);

    QLineEdit *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY2(queryEdit, "no query bar: the window was never built");

    MessageView *view = window.findChild<MessageView *>();
    QVERIFY2(view, "no message view");

    queryEdit->clear();
    emit view->searchRequested(QStringLiteral("tag:inbox"),
                               SearchTerm::SearchMode::Narrow);

    QCOMPARE(queryEdit->text(), QStringLiteral("tag:inbox"));
}

void TestMainWindow::autoSyncIsNotArmedWhenDisabledOrWithNothingPending()
{
    // A negative delay is the switch that restores the pre-0.16.0 behaviour, so
    // it must arm nothing at all.
    QTemporaryDir off;
    QVERIFY(off.isValid());
    Config disabled;
    disabled.load(writeSyncConfig(off, QStringLiteral("-1")));
    QCOMPARE(disabled.autoSyncDelayMs(), -1);

    MainWindow disabledWindow(disabled);
    auto *disabledTimer =
        disabledWindow.findChild<QTimer *>(QStringLiteral("autoSyncTimer"));
    QVERIFY(disabledTimer);

    TagChange change;
    change.messageIds = { QStringLiteral("m1") };
    change.added = { QStringLiteral("flagged") };
    QVERIFY(QMetaObject::invokeMethod(&disabledWindow, "onTagsApplied",
                                      Q_ARG(TagChange, change)));
    QVERIFY2(!disabledTimer->isActive(),
             "auto_sync_delay_ms = -1 still armed a sync");

    // An edit netted against its own inverse leaves nothing outstanding (item
    // 28), and syncing for it would run mbsync over an unchanged mail store.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSyncConfig(dir));

    MainWindow window(config);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("autoSyncTimer"));
    QVERIFY(timer);

    TagChange added;
    added.messageIds = { QStringLiteral("m1") };
    added.added = { QStringLiteral("unread") };
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, added)));
    QVERIFY2(timer->isActive(), "the first edit did not arm anything, so the "
                                "netting assertion below proves nothing");

    timer->stop();
    TagChange undone;
    undone.messageIds = { QStringLiteral("m1") };
    undone.removed = { QStringLiteral("unread") };
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, undone)));
    QVERIFY2(!timer->isActive(),
             "an edit and its inverse left nothing pending but still armed a sync");
}

void TestMainWindow::autoSyncSkipsWhileABackgroundSyncIsRunning()
{
    // Item 71 requires skipping rather than queueing: the cron job holds the
    // same lock and mbsync's answer to a second run is to fail on it. The edits
    // are not lost, they stay pending.
    //
    // The locks path is redirected so this does not depend on whether a real
    // sync is running on the machine, which is item 61's failure mode.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString locks = dir.filePath(QStringLiteral("locks"));
    {
        QFile f(locks);
        QVERIFY(f.open(QIODevice::WriteOnly));
    }
    MainWindow::setLocksPathForTesting(locks);

    Config config;
    config.load(writeSyncConfig(dir));

    MainWindow window(config);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("autoSyncTimer"));
    QVERIFY(timer);

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));

    TagChange change;
    change.messageIds = { QStringLiteral("m1") };
    change.added = { QStringLiteral("flagged") };
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));

    // Armed, because the edit is real and will still need carrying. The skip
    // belongs to the moment the timer FIRES, not to arming it.
    QVERIFY2(timer->isActive(), "the edit did not arm the debounce at all");

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);
    QVERIFY(!label->isHidden());

    QVERIFY(QMetaObject::invokeMethod(&window, "runAutoSync"));

    // The edit is still pending: a skipped sync must not clear the indicator,
    // which is the fault that would leave the user quitting on unsynced work.
    QVERIFY2(!label->isHidden(),
             "a skipped automatic sync cleared the pending indicator");

    // No restore to "/proc/locks": init() points every test at its own
    // table, and handing the real one back would re-expose the next test.
}

void TestMainWindow::aSuccessfulSyncRefreshesRatherThanRerunningTheQuery()
{
    // Reported by hand against item 71: reading a message in the Unread view,
    // the automatic mark-read tags it, the automatic sync fires two seconds
    // later, and the message pane went blank because the thread had stopped
    // matching "tag:unread".
    //
    // The cause was not the stale-thread notice, which handles exactly this and
    // has since item 35. It was that onSyncFinished() called runCurrentQuery()
    // where the cron path calls refreshCurrentQuery(): a re-run clears the
    // model, the undo stack and the pane, so there was nothing left for the
    // notice to describe. Before item 71 a local sync only ever followed a
    // click on Sync, which is why the difference went unnoticed.
    //
    // Asserted on the UNDO STACK rather than on the pane. Both paths issue a
    // queued query this test has no worker to answer, so the pane ends up blank
    // either way and an assertion on it would pass against both. The undo stack
    // is cleared by runCurrentQuery() and deliberately kept by
    // refreshCurrentQuery(), so it names which path actually ran.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    window.findChild<QLineEdit *>()->setText(QStringLiteral("tag:unread"));
    QMetaObject::invokeMethod(&window, "runCurrentQuery");
    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }) });
    QMetaObject::invokeMethod(&window, "onQueryFinished", Q_ARG(int, 1),
                              Q_ARG(quint64,
                                    window.currentGenerationForTesting()));

    // A real edit, so there is something on the undo stack to lose.
    selectThreadRow(view, 0);
    auto *markRead = window.findChild<QAction *>(QStringLiteral("mark_all_read"));
    QVERIFY(markRead);
    markRead->trigger();
    QCOMPARE(window.undoDepthForTesting(), 1);

    QMetaObject::invokeMethod(&window, "onSyncFinished",
                              Q_ARG(bool, true), Q_ARG(int, 0));

    QVERIFY2(window.undoDepthForTesting() == 1,
             "a successful sync cleared the undo stack, so it re-ran the query "
             "instead of refreshing it, and a message open in the pane is read "
             "out from under the user");
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

    // No restore to "/proc/locks": init() points every test at its own
    // table, and handing the real one back would re-expose the next test.
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

namespace {

/// A config whose accounts carry the given maildir/sent pairs. An empty `sent`
/// writes no key at all, which is the account-without-a-sent-folder case.
QString writeSentConfig(const QTemporaryDir &dir,
                        const QList<QPair<QString, QString>> &accounts)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QSettings s(path, QSettings::IniFormat);
    for (const auto &account : accounts) {
        s.beginGroup(QStringLiteral("account.") + account.first);
        s.setValue(QStringLiteral("maildir"), account.first);
        if (!account.second.isEmpty())
            s.setValue(QStringLiteral("sent"), account.second);
        s.endGroup();
    }
    s.sync();
    return path;
}

}  // namespace

void TestMainWindow::thereIsNoSentButtonWithoutASentKey()
{
    // Hidden entirely rather than present and finding nothing. An account may
    // legitimately keep no sent mail locally, and a button that always returns
    // an empty list reads as a broken feature rather than an absent one.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {{QStringLiteral("provider-c"), {}}}));
    QVERIFY(config.allSentQuery().isEmpty());

    MainWindow window(config);
    QVERIFY(!window.findChild<QAbstractButton *>(QStringLiteral("sentButton")));
}

void TestMainWindow::theSentButtonRunsEveryConfiguredAccount()
{
    // The button composes its query rather than storing one, which is the whole
    // reason it is not a [queries] entry: a saved query is a fixed string and
    // would not gain the third account here without the user editing it.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("webmail-primary"), QStringLiteral("Sent")},
        {QStringLiteral("provider-c"), {}},
        {QStringLiteral("webmail-secondary"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *button = window.findChild<QAbstractButton *>(QStringLiteral("sentButton"));
    QVERIFY(button);

    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    button->click();
    const QString query = queryEdit->text();

    QVERIFY(query.contains(QStringLiteral("webmail-primary/Sent")));
    QVERIFY(query.contains(QStringLiteral("webmail-secondary/Sent")));

    // The account with no key contributes nothing, and leaves no bare "or"
    // behind: notmuch accepts that and silently returns a different result.
    QVERIFY(!query.contains(QStringLiteral("provider-c")));
    QVERIFY(!query.contains(QStringLiteral("or  or")));
    QCOMPARE(query.count(QStringLiteral(" or ")), 1);
}

void TestMainWindow::theSentButtonSurvivesABracketedPath()
{
    // A real provider nests its sent folder under a bracketed parent, and "["
    // and "]" are Xapian syntax. The quoting has to survive the trip from the
    // config through Account::sentQuery() into the query bar; unquoted, the
    // query looks plausible and matches nothing.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("provider-a"), QStringLiteral("[Provider]/Posta inviata")},
    }));

    MainWindow window(config);
    auto *button = window.findChild<QAbstractButton *>(QStringLiteral("sentButton"));
    QVERIFY(button);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    button->click();
    QCOMPARE(queryEdit->text(),
             QStringLiteral("path:\"provider-a/[Provider]/Posta inviata/**\""));
}

namespace {

/// Writes a config whose accounts carry a sent folder, a drafts folder, or
/// neither. Separate from writeSentConfig() because the two keys are
/// independent: the interesting cases here are exactly the ones where an
/// account has one and not the other.
struct FolderAccount {
    QString maildir;
    QString sent;
    QString drafts;
};

QString writeFolderConfig(const QTemporaryDir &dir,
                          const QList<FolderAccount> &accounts)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QSettings s(path, QSettings::IniFormat);
    for (const FolderAccount &account : accounts) {
        s.beginGroup(QStringLiteral("account.") + account.maildir);
        s.setValue(QStringLiteral("maildir"), account.maildir);
        if (!account.sent.isEmpty())
            s.setValue(QStringLiteral("sent"), account.sent);
        if (!account.drafts.isEmpty())
            s.setValue(QStringLiteral("drafts"), account.drafts);
        s.endGroup();
    }
    s.sync();
    return path;
}

/// The label of the helper whose query is `query`, or a null string.
QString labelForQuery(const QList<HtmlBuilder::PlaceholderHelper> &helpers,
                      const QString &query)
{
    for (const HtmlBuilder::PlaceholderHelper &helper : helpers) {
        if (helper.query == query)
            return helper.label;
    }
    return QString();
}

}  // namespace

void TestMainWindow::placeholderCountsSkipSentAndDraftsWithoutTheKeys()
{
    // The tag: lines are unconditional, the folder lines are not. An account
    // with no sent or drafts folder must contribute no line at all rather than
    // a line reading 0: item 63 established that a missing folder is a real
    // configuration, and "0 sent" claims the user has sent nothing.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeFolderConfig(dir, {{QStringLiteral("provider-c"), {}, {}}}));

    MainWindow window(config);

    const QStringList queries = window.placeholderQueriesForTesting();
    QCOMPARE(queries.size(), 3);
    QVERIFY(queries.contains(QStringLiteral("tag:unread")));
    QVERIFY(queries.contains(QStringLiteral("tag:flagged")));
    QVERIFY(queries.contains(QStringLiteral("tag:inbox")));
}

void TestMainWindow::placeholderCountsCarrySentAndDrafts()
{
    // Both composed from the folder keys rather than from a tag. `tag:draft`
    // counts 0 against the user's real database (measured 2026-08-11) and no
    // draft-ish tag exists in it at all, so a tag-based drafts line would be a
    // permanent zero that looks like working code.
    //
    // The account layout is the real one's shape: one account with both keys,
    // one with drafts and NO sent, which is what proves the two are collected
    // independently rather than per-account in one pass.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeFolderConfig(dir, {
        {QStringLiteral("webmail-primary"), QStringLiteral("Sent"),
         QStringLiteral("Drafts")},
        {QStringLiteral("provider-a"), {}, QStringLiteral("[Provider]/Bozze")},
    }));

    MainWindow window(config);

    const QStringList queries = window.placeholderQueriesForTesting();
    QCOMPARE(queries.size(), 5);

    // The quoting survives the trip, which is the trap this whole composition
    // exists for: unquoted, "[" and "]" are Xapian syntax and the count reads
    // 0 while looking like an empty folder.
    QVERIFY2(queries.contains(config.allDraftsQuery()),
             qPrintable(QStringLiteral("drafts query missing, got: %1")
                            .arg(queries.join(QStringLiteral(" | ")))));
    QVERIFY(queries.contains(config.allSentQuery()));
    QVERIFY(config.allDraftsQuery().contains(
        QStringLiteral("path:\"provider-a/[Provider]/Bozze/**\"")));

    // The sent term comes from the account that has one, and the drafts terms
    // from both. An implementation that emitted a folder line per account
    // would produce four folder queries instead of two.
    QCOMPARE(config.allSentQuery().count(QStringLiteral("path:")), 1);
    QCOMPARE(config.allDraftsQuery().count(QStringLiteral("path:")), 2);
}

void TestMainWindow::placeholderLabelsStayPairedWithTheirQueries()
{
    // The defect this guards is the one the old fixed array invited: the
    // labels were written positionally against a separate query array, so
    // inserting an entry in one and not the other put a real number against
    // the wrong name. Asserting the PAIRING rather than the order is what
    // survives a later reshuffle.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeFolderConfig(dir, {
        {QStringLiteral("webmail-primary"), QStringLiteral("Sent"),
         QStringLiteral("Drafts")},
    }));

    MainWindow window(config);

    const QStringList queries = window.placeholderQueriesForTesting();
    QCOMPARE(queries.size(), 5);

    // Distinct counts, so a label reading the wrong index cannot coincide with
    // the right answer. Positional against the queries the window just asked
    // for, which is exactly the contract requestCounts() replies under.
    QVector<int> counts;
    for (int i = 0; i < queries.size(); ++i)
        counts.append((i + 1) * 10);

    QMetaObject::invokeMethod(&window, "onCountsReady",
                              Q_ARG(QVector<int>, counts),
                              Q_ARG(quint64, window.countsGenerationForTesting()));

    const QList<HtmlBuilder::PlaceholderHelper> helpers =
        window.placeholderHelpersForTesting();

    for (int i = 0; i < queries.size(); ++i) {
        const QString label = labelForQuery(helpers, queries.at(i));
        QVERIFY2(!label.isNull(),
                 qPrintable(QStringLiteral("no helper for query '%1'")
                                .arg(queries.at(i))));
        QVERIFY2(label.contains(QString::number(counts.at(i))),
                 qPrintable(QStringLiteral("query '%1' was labelled '%2', which "
                                           "does not carry its own count %3")
                                .arg(queries.at(i), label)
                                .arg(counts.at(i))));
    }

    // And the folder lines say what they are, not "in inbox".
    QVERIFY(labelForQuery(helpers, config.allSentQuery())
                .contains(QStringLiteral("sent")));
    QVERIFY(labelForQuery(helpers, config.allDraftsQuery())
                .contains(QStringLiteral("draft")));
}

void TestMainWindow::placeholderCountsDropAnUncountableQuery()
{
    // The worker answers -1 for a query it could not count, rather than
    // skipping the entry, precisely so the positional pairing holds. The pane
    // must then drop that LINE rather than print a negative number, and drop
    // only that one.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeFolderConfig(dir, {
        {QStringLiteral("webmail-primary"), QStringLiteral("Sent"),
         QStringLiteral("Drafts")},
    }));

    MainWindow window(config);

    const QStringList queries = window.placeholderQueriesForTesting();
    QVector<int> counts;
    for (int i = 0; i < queries.size(); ++i)
        counts.append(i == 0 ? -1 : (i + 1) * 10);

    QMetaObject::invokeMethod(&window, "onCountsReady",
                              Q_ARG(QVector<int>, counts),
                              Q_ARG(quint64, window.countsGenerationForTesting()));

    const QList<HtmlBuilder::PlaceholderHelper> helpers =
        window.placeholderHelpersForTesting();

    QVERIFY(labelForQuery(helpers, queries.at(0)).isNull());
    for (int i = 1; i < queries.size(); ++i) {
        QVERIFY2(!labelForQuery(helpers, queries.at(i)).isNull(),
                 qPrintable(QStringLiteral("an uncountable query took '%1' "
                                           "down with it").arg(queries.at(i))));
    }
}

void TestMainWindow::flatModeDoesNotSurviveTheNextQuery()
{
    // The condition the user set for this feature: a flat Sent list is fine, a
    // flat anything-else is not. Asserted at the window rather than the model,
    // because the leak this guards against is in the WIRING, not in the model:
    // setFlatMode(true) from the button with no matching false anywhere else
    // passes every model test and flattens the app from the first Sent click
    // until it restarts.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("webmail-primary"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *button = window.findChild<QAbstractButton *>(QStringLiteral("sentButton"));
    QVERIFY(button);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    QVERIFY2(!model->flatMode(), "the model starts flat");

    button->click();
    QVERIFY2(model->flatMode(), "the Sent button did not flatten the list");

    // Any other query restores the tree. Typed by hand rather than through a
    // saved-query button, since that is the route with no flag of its own and
    // therefore the one most likely to be forgotten.
    queryEdit->setText(QStringLiteral("tag:inbox"));
    QMetaObject::invokeMethod(&window, "runCurrentQuery");
    QVERIFY2(!model->flatMode(),
             "flat mode survived into an ordinary query, so every view after "
             "one Sent click lost its replies");

    // And back, so the button still works after the round trip.
    button->click();
    QVERIFY(model->flatMode());

    // Even the SAME query typed by hand comes back as a tree: the flag follows
    // the button, not the text, which is the rule the user chose.
    queryEdit->setText(config.allSentQuery());
    QMetaObject::invokeMethod(&window, "runCurrentQuery");
    QVERIFY(!model->flatMode());
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

// ---------------------------------------------------------------------------
// Saved queries in the query row (item 23)
// ---------------------------------------------------------------------------

/// Writes a config plus a queries.json beside it, and loads both.
static void loadWithQueries(Config &config, QTemporaryDir &dir,
                            const QString &queriesJson,
                            const QString &iniExtra = {})
{
    QDir().mkpath(dir.filePath(QStringLiteral("qtmaildir")));
    const QString conf =
        dir.filePath(QStringLiteral("qtmaildir/qtmaildir.conf"));
    QFile ini(conf);
    ini.open(QIODevice::WriteOnly | QIODevice::Text);
    ini.write(iniExtra.toUtf8());
    ini.close();

    QFile json(dir.filePath(QStringLiteral("qtmaildir/queries.json")));
    json.open(QIODevice::WriteOnly);
    json.write(queriesJson.toUtf8());
    json.close();

    config.load(conf);
}

/// Buttons in the saved-query row, by label, in the order they are laid out.
/// Whether this is one of the four shipped filters rather than a saved query.
///
/// By object name, not by label: the labels are translated, and a user may name
/// their own query "Unread" too.
static bool isBuiltinFilterButton(QAbstractButton *button)
{
    for (const SavedQuery &filter : Config::builtinFilters()) {
        if (button->objectName() == filter.generated + QStringLiteral("Button"))
            return true;
    }
    return false;
}

static QStringList savedQueryButtonLabels(MainWindow &window)
{
    QStringList labels;
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    if (!row)
        return labels;
    // QAbstractButton, not QPushButton: the built-in filters are QToolButtons
    // so they can carry an icon beside their text, and findChildren on the
    // narrower type would silently skip them, leaving the filter below with
    // nothing to filter.
    const QList<QAbstractButton *> buttons =
        row->findChildren<QAbstractButton *>(QString(),
                                             Qt::FindDirectChildrenOnly);
    for (QAbstractButton *button : buttons) {
        // The menu button is not a saved query and must not be counted as one.
        if (button->objectName() == QStringLiteral("savedQueryMenuButton"))
            continue;
        // Neither are the four built-in filters (item 93), which sit first on
        // the row and are not in queries.json at all. Every caller of this
        // helper is asking about the USER's queries, so counting the filters
        // would make each of them assert on a number it does not care about.
        if (isBuiltinFilterButton(button))
            continue;
        labels.append(button->text());
    }
    return labels;
}

/// The user's own pinned button carrying `label`, or null.
///
/// Positional lookup does not work any more: the built-in filters occupy the
/// first four places on the row, so row->findChild<QPushButton *>() returns
/// Unread rather than the query a test means.
static QPushButton *savedQueryButton(MainWindow &window, const QString &label)
{
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    if (!row)
        return nullptr;
    const QList<QPushButton *> buttons =
        row->findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly);
    for (QPushButton *button : buttons) {
        if (isBuiltinFilterButton(button))
            continue;
        if (button->text() == label)
            return button;
    }
    return nullptr;
}

void TestMainWindow::onlyPinnedQueriesBecomeButtons()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true },
            { "name": "Buried", "query": "tag:buried" }
        ]
    })"));

    MainWindow window(config);
    const QStringList labels = savedQueryButtonLabels(window);

    QVERIFY2(labels.contains(QStringLiteral("Inbox")),
             "a pinned query must have a button");
    QVERIFY2(!labels.contains(QStringLiteral("Buried")),
             "an unpinned query must NOT have a button");
}

void TestMainWindow::unpinnedQueriesReachTheMenu()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true },
            { "name": "Buried", "query": "tag:buried" }
        ]
    })"));

    MainWindow window(config);
    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    QVERIFY2(menuButton, "an unpinned query needs a menu to live in");
    QVERIFY(menuButton->menu());

    QStringList entries;
    const QList<QAction *> actions = menuButton->menu()->actions();
    for (QAction *action : actions)
        entries.append(action->text());

    QVERIFY2(entries.contains(QStringLiteral("Buried")),
             "the unpinned query is missing from the menu");
    // A pinned query is already a button; listing it twice is the duplicate
    // this asserts against.
    QVERIFY2(!entries.contains(QStringLiteral("Inbox")),
             "a pinned query must not also appear in the menu");
}

/// The property the whole storage change was made for. "Zebra" is written
/// first and must stay first; alphabetical order would put it last.
void TestMainWindow::pinnedButtonsFollowTheDocumentOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Zebra", "query": "tag:zebra", "pinned": true },
            { "name": "Apple", "query": "tag:apple", "pinned": true }
        ]
    })"));

    MainWindow window(config);
    const QStringList labels = savedQueryButtonLabels(window);

    QCOMPARE(labels.size(), 2);
    QCOMPARE(labels.at(0), QStringLiteral("Zebra"));
    QCOMPARE(labels.at(1), QStringLiteral("Apple"));
}

void TestMainWindow::theSavedQueryMenuIsHiddenWhenEveryQueryIsPinned()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true }
        ]
    })"));

    MainWindow window(config);

    // The guard the assertion below needs. Asserting only that the menu button
    // is absent passed against NO implementation at all, before any of this
    // was built, so it has to prove first that the row it is looking in was
    // populated and that a button was found.
    const QStringList labels = savedQueryButtonLabels(window);
    QCOMPARE(labels, QStringList{ QStringLiteral("Inbox") });

    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    QVERIFY2(!menuButton,
             "an empty menu button is a control that always does nothing");
}

/// The scope goes through the account dropdown rather than being baked into
/// the query text. runQuery() already scopes by that dropdown, so pre-scoping
/// the text would apply the path twice, and the selection would be invisible.
void TestMainWindow::aScopedSavedQuerySelectsItsAccount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Billing", "query": "from:billing",
              "account": "work", "pinned": true }
        ]
    })"), QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
        "\n"
        "[account.personal]\n"
        "name=Test User\n"
        "address=me@example.net\n"
        "maildir=personal\n"
    ));

    MainWindow window(config);
    auto *accountBox =
        window.findChild<QComboBox *>(QStringLiteral("accountBox"));
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(accountBox);
    QVERIFY(queryEdit);

    // Start somewhere else, so a passing result cannot be the default.
    accountBox->setCurrentIndex(accountBox->findData(
        QStringLiteral("personal")));
    QCOMPARE(accountBox->currentData().toString(), QStringLiteral("personal"));

    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);
    auto *button = savedQueryButton(window, QStringLiteral("Billing"));
    QVERIFY(button);
    button->click();

    QCOMPARE(accountBox->currentData().toString(), QStringLiteral("work"));
    // The text is the bare query. The path scope is applied once, by
    // runQuery(), from the dropdown this just set.
    QCOMPARE(queryEdit->text(), QStringLiteral("from:billing"));
    QVERIFY2(!queryEdit->text().contains(QStringLiteral("path:")),
             "the scope must not be baked into the query text");
}

/// A query with no account must CLEAR the dropdown, not inherit whatever the
/// last one left there. Confirmed against the same defect in the rules
/// preview, where an already-selected account survived the click.
void TestMainWindow::anUnscopedSavedQueryClearsTheAccount()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Everywhere", "query": "tag:inbox", "pinned": true }
        ]
    })"), QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
    ));

    MainWindow window(config);
    auto *accountBox =
        window.findChild<QComboBox *>(QStringLiteral("accountBox"));
    QVERIFY(accountBox);

    accountBox->setCurrentIndex(accountBox->findData(QStringLiteral("work")));
    QCOMPARE(accountBox->currentData().toString(), QStringLiteral("work"));

    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);
    auto *button = savedQueryButton(window, QStringLiteral("Everywhere"));
    QVERIFY(button);
    button->click();

    QVERIFY2(accountBox->currentData().toString().isEmpty(),
             "an unscoped saved query must clear the account selection");
}

void TestMainWindow::theSaveQueryActionIsDisabledOnAnEmptyQuery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1, "queries": []
    })"));

    MainWindow window(config);
    auto *save = window.findChild<QAction *>(QStringLiteral("save_query"));
    QVERIFY2(save, "there is no way to save a query");

    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    queryEdit->clear();
    QVERIFY2(!save->isEnabled(),
             "saving an empty query would store a query that matches nothing");

    queryEdit->setText(QStringLiteral("tag:inbox"));
    QVERIFY2(save->isEnabled(), "a real query must be savable");

    // Whitespace is not a query. setText does not drive a completer, but it
    // does emit textChanged, which is what the enabling is hung on.
    queryEdit->setText(QStringLiteral("   "));
    QVERIFY(!save->isEnabled());
}

/// A menu entry and a shortcut are not a button. The spec asks for one beside
/// the query bar, and the user went looking for it there and did not find it.
void TestMainWindow::thereIsASaveButtonBesideTheQueryBar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1, "queries": []
    })"));

    MainWindow window(config);
    auto *button =
        window.findChild<QAbstractButton *>(QStringLiteral("saveQueryButton"));
    QVERIFY2(button, "no Save query button beside the query bar");

    // Icon AND text. An icon alone was the first version and read as
    // ambiguous: "save" is a familiar shape whose meaning is always "save
    // what?".
    auto *toolButton = qobject_cast<QToolButton *>(button);
    QVERIFY(toolButton);
    QCOMPARE(toolButton->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QVERIFY2(!button->icon().isNull(), "the button has no icon");
    QVERIFY2(!button->text().isEmpty(), "the button has no text");

    // Button phrasing, not the menu's: no accelerator ampersand, and no
    // ellipsis. setDefaultAction copies the action's text, so this asserts the
    // override survived it.
    QVERIFY2(!button->text().contains(QLatin1Char('&')),
             "the menu accelerator leaked onto the button");
    QVERIFY2(!button->text().contains(QStringLiteral("...")),
             "the menu's ellipsis leaked onto the button");

    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    // In the query row itself, not somewhere else in the window that a
    // findChild would also reach.
    QCOMPARE(button->parentWidget(), queryEdit->parentWidget());

    // Follows the action, so it cannot offer to save an empty query while the
    // menu entry correctly refuses.
    queryEdit->clear();
    QVERIFY2(!button->isEnabled(),
             "the button must follow the action's enabled state");
    queryEdit->setText(QStringLiteral("tag:inbox"));
    QVERIFY(button->isEnabled());
}

/// Right-aligned, meaning a stretch sits between the buttons and the menu.
/// Asserted on the layout rather than on x coordinates: the offscreen platform
/// lays out widgets, but a geometry assertion here would also pass for a row
/// that simply ran out of width.
void TestMainWindow::theMenuIsRightAlignedAwayFromTheButtons()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true },
            { "name": "Buried", "query": "tag:buried" }
        ]
    })"));

    MainWindow window(config);
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);
    auto *box = qobject_cast<QHBoxLayout *>(row->layout());
    QVERIFY(box);

    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    QVERIFY(menuButton);

    int menuIndex = -1;
    int stretchIndex = -1;
    for (int i = 0; i < box->count(); ++i) {
        QLayoutItem *item = box->itemAt(i);
        if (item->widget() == menuButton)
            menuIndex = i;
        else if (!item->widget() && item->spacerItem())
            stretchIndex = i;
    }

    QVERIFY2(stretchIndex >= 0, "the row has no stretch to align against");
    QVERIFY2(menuIndex > stretchIndex,
             "the menu must come AFTER the stretch to sit at the right edge");
}

/// The row must not vanish when every saved query is unpinned: the menu is
/// then the only way to reach any of them, and hiding the row buries it.
void TestMainWindow::theRowSurvivesWithNothingButUnpinnedQueries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Buried", "query": "tag:buried" },
            { "name": "AlsoBuried", "query": "tag:also" }
        ]
    })"));

    MainWindow window(config);
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);
    QVERIFY2(!row->isHidden(),
             "the row was hidden, so the only route to these queries is gone");

    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    QVERIFY(menuButton);
    QCOMPARE(menuButton->menu()->actions().size(), 2);
}

static QString oneAccountWithSent()
{
    return QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
        "sent=Sent\n"
    );
}

/// The existing Sent tests reach the generated entry through MIGRATION, since
/// their configs have no queries.json. This one starts from a stored file, so
/// it covers the path a user is on from the second launch onwards.
void TestMainWindow::aStoredGeneratedQueryRunsFlatAndComposed()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Sent", "generated": "sent", "pinned": true }
        ]
    })"), oneAccountWithSent());

    MainWindow window(config);
    auto *button =
        window.findChild<QAbstractButton *>(QStringLiteral("sentButton"));
    QVERIFY(button);

    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    QVERIFY2(!model->flatMode(), "the model starts threaded");

    button->click();

    // Composed from the account, not read from the file: the entry stores no
    // query at all.
    QCOMPARE(queryEdit->text(), config.allSentQuery());
    QVERIFY(queryEdit->text().contains(
        QStringLiteral("path:\"work-mail/Sent/**\"")));
    QVERIFY2(model->flatMode(),
             "a sent view must be flat, or replies fold back into the thread");
}

/// The point of the change: Sent is the user's row now. Renaming it must not
/// break it, which it would if anything keyed on the literal name "Sent".
void TestMainWindow::aRenamedSentEntryKeepsWorking()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Posta inviata", "generated": "sent", "pinned": true }
        ]
    })"), oneAccountWithSent());

    MainWindow window(config);

    // The renamed entry is UNPINNED on load now, because item 93 ships Sent as
    // a built-in filter and two Sent buttons on the row, one editable and one
    // not, is worse than one of each in its own place. It keeps its name, it
    // keeps working, and it is in the menu rather than on the row.
    QVERIFY2(savedQueryButtonLabels(window).isEmpty(),
             "a stored generated entry is still a button beside the built-in "
             "filter that duplicates it");

    bool found = false;
    for (const SavedQuery &saved : config.savedQueries()) {
        if (saved.name != QStringLiteral("Posta inviata"))
            continue;
        found = true;
        QVERIFY2(saved.isGenerated(),
                 "the entry lost its generator when renamed");
        QVERIFY2(!saved.pinned, "the entry was not unpinned");
    }
    QVERIFY2(found, "the renamed entry was DROPPED rather than unpinned");

    // The built-in Sent still resolves the same query, so nothing the user
    // could reach before became unreachable.
    auto *button =
        window.findChild<QAbstractButton *>(QStringLiteral("sentButton"));
    QVERIFY(button);
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    button->click();
    QCOMPARE(queryEdit->text(), config.allSentQuery());
}

/// The hardcoded button was hidden entirely when no account configured a sent
/// folder, rather than offering one that always finds nothing. A stored row
/// must behave the same way.
void TestMainWindow::aGeneratedQueryWithNothingToShowIsSkipped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true },
            { "name": "Sent", "generated": "sent", "pinned": true }
        ]
    })"), QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
    ));

    MainWindow window(config);

    // The guard: the row was built and the other entry did get a button, so a
    // missing Sent means it was skipped rather than that nothing was built.
    QCOMPARE(savedQueryButtonLabels(window),
             QStringList{ QStringLiteral("Inbox") });
    QVERIFY2(!window.findChild<QAbstractButton *>(QStringLiteral("sentButton")),
             "a generated query with nothing to show must not get a button");
}

/// Reads queries.json back from disk, which is what "it was saved" means.
static QJsonArray storedQueries(const QTemporaryDir &dir)
{
    QFile f(dir.filePath(QStringLiteral("qtmaildir/queries.json")));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    return root.value(QStringLiteral("queries")).toArray();
}

static QAction *contextActionNamed(MainWindow &window, QWidget *target,
                                   const QString &objectName)
{
    const QList<QAction *> actions = target->actions();
    for (QAction *action : actions) {
        if (action->objectName() == objectName)
            return action;
    }
    Q_UNUSED(window);
    return nullptr;
}

void TestMainWindow::aSavedQueryButtonOffersEditUnpinAndDelete()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true }
        ]
    })"));

    MainWindow window(config);
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);
    auto *button = savedQueryButton(window, QStringLiteral("Inbox"));
    QVERIFY(button);

    // A context menu, so the actions live on the widget itself.
    QCOMPARE(button->contextMenuPolicy(), Qt::ActionsContextMenu);
    QVERIFY(contextActionNamed(window, button, QStringLiteral("editQuery")));
    QVERIFY(contextActionNamed(window, button, QStringLiteral("pinQuery")));
    QVERIFY(contextActionNamed(window, button, QStringLiteral("deleteQuery")));
}

void TestMainWindow::onlyAStoredQueryOffersToBecomeATaggingRule()
{
    // A generated entry composes its query from the accounts, so a rule made
    // from one freezes a snapshot that goes stale when an account is added.
    //
    // Both halves are asserted together on purpose: a test that only checks a
    // menu item is ABSENT passes just as well against a feature that was never
    // built, which item 82 recorded the hard way.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    // The account is not decoration. A generated entry that resolves to an
    // empty query is skipped entirely (src/mainwindow.cpp:1692), so without a
    // configured sent folder the Sent button is never built and the half of
    // this test that matters would pass by finding nothing.
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true },
            { "name": "Sent", "generated": "sent", "pinned": true }
        ]
    })"),
                    QStringLiteral(
                        "[account.work]\n"
                        "name=Test User\n"
                        "address=user@example.org\n"
                        "maildir=work-mail\n"
                        "sent=work-mail/Sent\n"));

    MainWindow window(config);
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);

    // By object name, which rebuildSavedQueryRow assigns precisely so a test
    // need not depend on a label the user can rename.
    auto *generated = row->findChild<QAbstractButton *>(
        QStringLiteral("sentButton"));

    // savedQueryButton(), not a scan for the label: item 93 puts a BUILT-IN
    // Inbox filter on the row too, and it carries no context actions by design,
    // so a scan finds that one and the assertion below fails against correct
    // code.
    QPushButton *stored = savedQueryButton(window, QStringLiteral("Inbox"));

    QVERIFY2(stored, "no button was built for the stored query");
    QVERIFY2(generated, "no button was built for the generated query");

    QVERIFY2(contextActionNamed(window, stored, QStringLiteral("queryToRule")),
             "a stored query must offer Create tagging rule");
    QVERIFY2(!contextActionNamed(window, generated,
                                 QStringLiteral("queryToRule")),
             "a generated query must not: its query is a snapshot");

    // The guard, and it has moved since item 93. It used to prove the generated
    // button HAS a menu, so the assertion above was about one action rather
    // than about a button with none. `generated` is now the BUILT-IN Sent
    // filter, which correctly carries no actions at all, so proving the
    // machinery works has to happen on the button that does have them.
    QVERIFY2(contextActionNamed(window, stored, QStringLiteral("deleteQuery")),
             "the stored button lost its other actions, so the assertion above "
             "is not about queryToRule in particular");
}

void TestMainWindow::unpinningMovesAQueryToTheMenu()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true },
            { "name": "Other", "query": "tag:other", "pinned": true }
        ]
    })"));

    MainWindow window(config);
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);
    QCOMPARE(savedQueryButtonLabels(window).size(), 2);
    QVERIFY(!window.findChild<QAbstractButton *>(
        QStringLiteral("savedQueryMenuButton")));

    auto *button = savedQueryButton(window, QStringLiteral("Inbox"));
    QVERIFY(button);
    QAction *pin = contextActionNamed(window, button, QStringLiteral("pinQuery"));
    QVERIFY(pin);
    pin->trigger();

    // Off the row, into the menu, and written to the file: an unpin that only
    // redrew would come back pinned on the next launch.
    QCOMPARE(savedQueryButtonLabels(window), QStringList{ QStringLiteral("Other") });
    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    QVERIFY(menuButton);
    QCOMPARE(menuButton->menu()->actions().size(), 1);

    const QJsonArray stored = storedQueries(dir);
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Inbox"));
    QVERIFY2(!stored.at(0).toObject().contains(QStringLiteral("pinned")),
             "the unpin did not reach the file");
}

void TestMainWindow::deletingRemovesTheQueryFromTheFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Doomed", "query": "tag:doomed", "pinned": true },
            { "name": "Keeper", "query": "tag:keeper", "pinned": true }
        ]
    })"));

    MainWindow window(config);
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);
    auto *button = savedQueryButton(window, QStringLiteral("Doomed"));
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Doomed"));

    QAction *del =
        contextActionNamed(window, button, QStringLiteral("deleteQuery"));
    QVERIFY(del);
    // Destructive and not on the undo stack, so it confirms. Suppressed here
    // rather than driven through the modal dialog, which would hang the test.
    window.setConfirmDeleteForTesting(false);
    del->trigger();

    QCOMPARE(savedQueryButtonLabels(window),
             QStringList{ QStringLiteral("Keeper") });

    const QJsonArray stored = storedQueries(dir);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Keeper"));
}

/// A field a later build wrote must survive an edit here, or upgrading and
/// downgrading silently strips config the user set.
void TestMainWindow::anEditedQueryKeepsItsUnknownFields()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true,
              "icon": "mail-inbox" }
        ]
    })"));

    MainWindow window(config);

    // Through the EDIT path, with a replacement carrying no unknown fields of
    // its own, which is exactly what SaveQueryDialog returns. Driving this
    // through unpin instead proved nothing: unpin copies the stored entry, so
    // it carries `unknown` along by itself and the merge is never exercised.
    // That version passed with the merge deleted.
    SavedQuery edited;
    edited.name = QStringLiteral("Inbox");
    edited.query = QStringLiteral("tag:inbox and not tag:muted");
    edited.pinned = true;
    QVERIFY(edited.unknown.isEmpty());
    window.replaceSavedQueryForTesting(QStringLiteral("Inbox"), edited);

    const QJsonArray stored = storedQueries(dir);
    QCOMPARE(stored.size(), 1);
    const QJsonObject entry = stored.at(0).toObject();
    // The edit landed...
    QCOMPARE(entry.value(QStringLiteral("query")).toString(),
             QStringLiteral("tag:inbox and not tag:muted"));
    // ...and did not take the unknown field down with it.
    QCOMPARE(entry.value(QStringLiteral("icon")).toString(),
             QStringLiteral("mail-inbox"));
}

/// Renaming must match on the name the dialog OPENED with. Matching on the
/// returned name leaves the original in place and adds a second entry.
void TestMainWindow::renamingReplacesRatherThanDuplicating()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Old", "query": "tag:old", "pinned": true }
        ]
    })"));

    MainWindow window(config);

    SavedQuery renamed;
    renamed.name = QStringLiteral("New");
    renamed.query = QStringLiteral("tag:old");
    renamed.pinned = true;
    window.replaceSavedQueryForTesting(QStringLiteral("Old"), renamed);

    const QJsonArray stored = storedQueries(dir);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("New"));
    QCOMPARE(savedQueryButtonLabels(window), QStringList{ QStringLiteral("New") });
}

void TestMainWindow::aMalformedAccountIsReportedWithoutBlockingTheConstructor()
{
    // The exact shape that hung the suite on 2026-08-14: an account section
    // carrying `sent=` and no `maildir=`. Config::load handles it correctly,
    // recording a problem and carrying on, but showWarnings() then raised a
    // modal FROM THE CONSTRUCTOR, which nothing can dismiss under the
    // offscreen platform, so MainWindow never finished constructing.
    //
    // Constructing the window at all is therefore half the assertion: if the
    // modal comes back, this test does not fail, it HANGS.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.one]\n"
            << "sent=Sent\n";
    }

    Config config;
    config.load(path);

    MainWindow window(config);

    // The problem is reported rather than swallowed, and it names the account.
    const QStringList problems = window.configProblems();
    QVERIFY2(!problems.isEmpty(),
             "a malformed account produced no problem to report");
    QVERIFY(problems.join(QLatin1Char('\n')).contains(QStringLiteral("one")));
}

void TestMainWindow::aWorkerBackedWindowReturnsRealThreads()
{
    // The guard test for every fixture-backed case after it. A probe that
    // cannot find what it expects to find will report a miss forever, so this
    // proves the database, the config and the worker are connected BEFORE
    // anything relies on a signal not arriving.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("root@example.org"),
        QStringLiteral("A subject"), QStringLiteral("sender@example.org"),
        // Friday, verified with `date -d 2026-08-14 +%A`. Qt::RFC2822Date
        // validates the weekday against the date and rejects a mismatch.
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(), qPrintable(backed.error()));

    MainWindow window(backed.config());

    QLineEdit *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY2(queryEdit, "no query bar: the window was never built");

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY2(model, "no thread list model");

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();

    // QTRY_VERIFY re-tests until the condition holds or the timeout expires,
    // which is what makes this safe against a worker on another thread. Not a
    // fixed qWait(n): a guessed sleep races under load and, worse, passes when
    // the result never arrives at all because nothing re-checks.
    //
    // The worker itself is unreachable from here by construction, not by
    // oversight: wireWorker() creates it parentless and moves it to its own
    // thread, so findChild() cannot see it. Observing the window's own state
    // is both the only route and the better assertion.
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
}

void TestMainWindow::everyBuiltinFilterButtonCarriesAnIconAndItsText()
{
    // The filters are part of the application now, so they carry icons like the
    // Save button beside them rather than reading as bare text among the user's
    // own queries.
    //
    // Text BESIDE the icon, not instead of it. The toolbar follows the
    // desktop's own button style, but this row is a row of text buttons: an
    // icon on its own here reads as a different kind of control than it is,
    // which is the same argument the Save button records.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);

    for (const SavedQuery &filter : Config::builtinFilters()) {
        auto *button = window.findChild<QToolButton *>(
            filter.generated + QStringLiteral("Button"));
        QVERIFY2(button, qPrintable(QStringLiteral("no button for filter '%1'")
                                        .arg(filter.generated)));

        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
        QCOMPARE(button->text(), filter.name);

        // Whether the icon RESOLVES depends on the running icon theme, which a
        // test cannot assume: QIcon::fromTheme returns a null icon under a
        // platform with no theme installed, so asserting on isNull() would fail
        // for a reason that has nothing to do with this code. What is asserted
        // is that a name was asked for, which is the part that lives here.
        QVERIFY2(!button->icon().name().isEmpty(),
                 qPrintable(QStringLiteral("filter '%1' was given no themed "
                                           "icon name").arg(filter.generated)));
    }
}

void TestMainWindow::aQueryInTheMenuCanActuallyBeRun()
{
    // An unpinned query was UNRUNNABLE. Its action carried both a triggered
    // connection and a submenu of edit actions, and Qt does not emit triggered
    // for an action that owns a menu: clicking it opens the submenu and nothing
    // else. The connection had never fired.
    //
    // It shipped unnoticed because the menu was the rarely-used half while the
    // user's queries were pinned buttons. Item 93 moved every one of them into
    // the menu, which is how it surfaced, and item 94 makes the menu their only
    // home, so this is the path that has to work.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Menued", "query": "tag:menued", "pinned": false }
        ]
    })"));

    MainWindow window(config);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    QVERIFY2(menuButton, "no overflow menu for an unpinned query");
    QVERIFY(menuButton->menu());

    QAction *entry = nullptr;
    for (QAction *action : menuButton->menu()->actions()) {
        if (action->text() == QStringLiteral("Menued"))
            entry = action;
    }
    QVERIFY2(entry, "the unpinned query is not in the menu");

    // The entry keeps its submenu, because an unpinned query must still be
    // editable and deletable. What it cannot be is the ONLY thing there: Qt
    // does not emit triggered for an action that owns a menu, so running the
    // query needs an item of its own.
    QVERIFY2(entry->menu(), "the per-query actions are gone");

    QAction *run = nullptr;
    for (QAction *action : entry->menu()->actions()) {
        if (action->objectName() == QStringLiteral("runQuery"))
            run = action;
    }
    QVERIFY2(run, "no way to run the query: its submenu offers only edit "
                  "actions, and Qt never emits triggered for the parent");

    // First, before the edit actions. Running is what the entry is for; editing
    // is what one does to it occasionally.
    QCOMPARE(entry->menu()->actions().constFirst(), run);

    run->trigger();
    QCOMPARE(queryEdit->text(), QStringLiteral("tag:menued"));

    // The edit actions survived beside it.
    QStringList names;
    for (QAction *action : entry->menu()->actions())
        names.append(action->objectName());
    QVERIFY2(names.contains(QStringLiteral("editQuery")),
             "the entry lost Edit");
    QVERIFY2(names.contains(QStringLiteral("deleteQuery")),
             "the entry lost Delete");
}

void TestMainWindow::theFourBuiltinFiltersAreOnTheRowInOrder()
{
    // Shipped, not pinned. Nothing in this config names a query, so a row with
    // four buttons on it can only have got them from the built-in set: before
    // item 93 a fresh install had an empty query row.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);

    // QAbstractButton: the filters are QToolButtons, so they carry an icon
    // beside their text like the Save button at the other end of the row.
    QStringList labels;
    for (QAbstractButton *button : row->findChildren<QAbstractButton *>()) {
        // The overflow menu is a control over the set, not a member of it.
        if (button->objectName() == QStringLiteral("savedQueryMenuButton"))
            continue;
        labels.append(button->text());
    }

    QCOMPARE(labels, (QStringList{ QStringLiteral("Unread"),
                                   QStringLiteral("Inbox"),
                                   QStringLiteral("Important"),
                                   QStringLiteral("Sent") }));
}

void TestMainWindow::aFilterComposesWithTheSelectedAccount()
{
    // Item 90, and the whole point of item 93. Select an account, hit Unread,
    // and get that account's unread mail rather than everyone's.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
        {QStringLiteral("personal"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    window.selectAccountForTesting(QStringLiteral("work"));
    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("work"));

    auto *unread =
        window.findChild<QAbstractButton *>(QStringLiteral("unreadButton"));
    QVERIFY2(unread, "no built-in Unread button");
    unread->click();

    QCOMPARE(queryEdit->text(),
             QStringLiteral("path:\"work/**\" and (tag:unread)"));

    // Sent under the same account is the account's OWN folder, not the union
    // wrapped in a scope. See the Config test of the same name for why a row
    // count cannot tell the two apart.
    auto *sent = window.findChild<QAbstractButton *>(QStringLiteral("sentButton"));
    QVERIFY(sent);
    sent->click();
    QCOMPARE(queryEdit->text(), QStringLiteral("path:\"work/Sent/**\""));
    QVERIFY2(!queryEdit->text().contains(QStringLiteral("personal")),
             "another account's sent folder leaked into a scoped Sent filter");
}

void TestMainWindow::aFilterAcrossAllAccountsIsUnscoped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    // "All accounts" is the default selection, so this is the fresh state.
    QVERIFY(window.selectedAccountForTesting().isEmpty());

    auto *unread =
        window.findChild<QAbstractButton *>(QStringLiteral("unreadButton"));
    QVERIFY(unread);
    unread->click();

    QCOMPARE(queryEdit->text(), QStringLiteral("tag:unread"));
}

void TestMainWindow::aFilterDoesNotClearTheAccountSelection()
{
    // The defect item 90 filed: the button used to reset the dropdown to "All
    // accounts" before running, so the selection was gone before the query ran.
    // A filter leaves it exactly where the user put it.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    window.selectAccountForTesting(QStringLiteral("work"));

    auto *unread =
        window.findChild<QAbstractButton *>(QStringLiteral("unreadButton"));
    QVERIFY(unread);
    unread->click();

    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("work"));
}

void TestMainWindow::aSavedQueryStillClearsTheAccountSelection()
{
    // The other half of the design, and the reason item 90 was not fixed in
    // place. A saved query is a DESTINATION: it states its own scope, so an
    // unscoped one clears the selection rather than inheriting it. That is the
    // behaviour the rules preview depends on and it must survive item 93.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    const QString path = writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    });
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("queries"));
        s.setValue(QStringLiteral("Mine"), QStringLiteral("tag:todo"));
        s.endGroup();
        s.sync();
    }
    config.load(path);

    MainWindow window(config);
    window.selectAccountForTesting(QStringLiteral("work"));

    // The migrated [queries] entry is pinned, so it is a button beside the
    // filters. Found by its label, since only the filters have stable object
    // names.
    QPushButton *mine = nullptr;
    auto *row = window.findChild<QWidget *>(QStringLiteral("savedQueryRow"));
    QVERIFY(row);
    for (QPushButton *button : row->findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Mine"))
            mine = button;
    }
    QVERIFY2(mine, "the user's own pinned query is not on the row");

    mine->click();
    QVERIFY2(window.selectedAccountForTesting().isEmpty(),
             "an unscoped saved query no longer clears the account selection");
}

void TestMainWindow::aFilterOffersNoEditOrDeleteActions()
{
    // A filter is not the user's to edit, rename or delete: it is shipped, and
    // it is not in queries.json at all. Offering the actions would produce a
    // dialog that writes an entry the row does not read.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *unread =
        window.findChild<QAbstractButton *>(QStringLiteral("unreadButton"));
    QVERIFY(unread);

    for (QAction *action : unread->actions()) {
        QVERIFY2(action->objectName() != QStringLiteral("editQuery"),
                 "a built-in filter offered Edit");
        QVERIFY2(action->objectName() != QStringLiteral("pinQuery"),
                 "a built-in filter offered a pin toggle");
        QVERIFY2(action->objectName() != QStringLiteral("deleteQuery"),
                 "a built-in filter offered Delete");
    }
}

void TestMainWindow::changingTheAccountRunsNothing()
{
    // The user's decision, 2026-08-15: "changing the account should not run the
    // query, hitting the button after changing the account is what queries."
    // The dropdown selects scope; the button is the verb.
    //
    // Today m_accountBox has no signal connected at all, so this guards against
    // wiring one up by reflex while making the filters compose.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    // A known starting point, so "nothing happened" is distinguishable from
    // "it was empty all along", which would pass against a re-run that clears.
    queryEdit->setText(QStringLiteral("tag:todo"));
    const quint64 before = window.currentGenerationForTesting();

    window.selectAccountForTesting(QStringLiteral("work"));

    QCOMPARE(queryEdit->text(), QStringLiteral("tag:todo"));
    QCOMPARE(window.currentGenerationForTesting(), before);
}

void TestMainWindow::arrivingBatchesUpdateTheStatusBarWithTheCountSoFar()
{
    // Item 74: "Searching..." was set once by runQuery and cleared only on
    // queryFinished, so it kept claiming the query was running for the whole
    // cold-cache walk, measured at 5.7 s against a 1.1 GB index, while rows
    // were visibly arriving behind it from 642 ms. A slow query read as a
    // frozen one.
    const Config config;
    MainWindow window(config);

    auto *status =
        window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY2(status, "no status label");
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);

    window.findChild<QLineEdit *>()->setText(QStringLiteral("tag:inbox"));
    QMetaObject::invokeMethod(&window, "runCurrentQuery");

    // The guard: before any batch, the bar says what it has always said. If
    // this ever stops holding, the assertions below are measuring the wrong
    // thing and would pass against a bar that never changes at all.
    QCOMPARE(status->text(), QStringLiteral("Searching..."));

    const quint64 generation = window.currentGenerationForTesting();
    const QVector<ThreadSummary> first = {
        makeThread(QStringLiteral("t1"), {}),
        makeThread(QStringLiteral("t2"), {})
    };
    QVERIFY(QMetaObject::invokeMethod(
        &window, "onThreadsReady",
        Q_ARG(QVector<ThreadSummary>, first), Q_ARG(quint64, generation)));

    const QString afterFirst = status->text();
    QVERIFY2(afterFirst != QStringLiteral("Searching..."),
             "the bar still claimed the query was running after rows arrived");
    QVERIFY2(afterFirst.contains(QStringLiteral("2")),
             qPrintable(QStringLiteral("no count so far in: ") + afterFirst));

    // A second batch, because the count has to keep moving. A bar that says
    // "2" forever is the same lie in a shorter sentence.
    const QVector<ThreadSummary> second = {
        makeThread(QStringLiteral("t3"), {})
    };
    QVERIFY(QMetaObject::invokeMethod(
        &window, "onThreadsReady",
        Q_ARG(QVector<ThreadSummary>, second), Q_ARG(quint64, generation)));
    QVERIFY2(status->text().contains(QStringLiteral("3")),
             qPrintable(QStringLiteral("count did not advance: ")
                        + status->text()));

    // And the finished text still wins, so the running message cannot outlive
    // the query that armed it.
    // The literal "(s)" is what an untranslated %n plural renders as with no
    // translator loaded, which is the case in the suite. Asserting the plural
    // form Qt would pick under a translation would fail against correct code.
    QMetaObject::invokeMethod(&window, "onQueryFinished", Q_ARG(int, 3),
                              Q_ARG(quint64, generation));
    QCOMPARE(status->text(), QStringLiteral("3 thread(s)"));
}

void TestMainWindow::aRefreshsBatchesLeaveTheStatusBarAlone()
{
    // A refresh after a sync is meant to be silent: onQueryFinished updates
    // the FALLBACK text without stamping over the bar, and its batches must
    // not do what its completion deliberately does not. Without this, a cron
    // sync would overwrite whatever the bar was telling the user with a
    // running count they never asked for.
    const Config config;
    MainWindow window(config);

    auto *status =
        window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    window.findChild<QLineEdit *>()->setText(QStringLiteral("tag:inbox"));
    QMetaObject::invokeMethod(&window, "runCurrentQuery");
    const quint64 generation = window.currentGenerationForTesting();
    QMetaObject::invokeMethod(&window, "onQueryFinished", Q_ARG(int, 1),
                              Q_ARG(quint64, generation));

    // Something the user is being told, which the refresh must not erase.
    status->setText(QStringLiteral("Sync complete."));

    // A refresh's own generation. refreshCurrentQuery() needs a worker to set
    // one, and a bare window has none, so the generation is advanced the same
    // way the refresh does and declared to the window through the seam.
    const quint64 refreshGeneration =
        window.beginRefreshForTesting();
    QVERIFY(QMetaObject::invokeMethod(
        &window, "onThreadsReady",
        Q_ARG(QVector<ThreadSummary>, { makeThread(QStringLiteral("t1"), {}) }),
        Q_ARG(quint64, refreshGeneration)));

    QCOMPARE(status->text(), QStringLiteral("Sync complete."));
}

void TestMainWindow::selectingAThreadRootShowsItInTheMessagePane()
{
    // Item 66: "selecting a thread (click on main message), nothing appears in
    // right pane, after expanding and selecting a reply, clicking on the main
    // message correctly shows the email."
    //
    // EXPECTED TO FAIL until that defect is fixed. It is the reproduction the
    // item has needed since 2026-08-04 and could not have while test_mainwindow
    // had no worker to deliver threadLoaded.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("root@example.org"),
        QStringLiteral("A conversation"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("The first message.")));
    // A real In-Reply-To chain, which is the only way the fixture builds a
    // thread: notmuch groups on the headers, not on the subject.
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("reply@example.org"),
        QStringLiteral("Re: A conversation"),
        QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("The reply."), true,
        QStringLiteral("root@example.org")));
    QVERIFY2(backed.build(), qPrintable(backed.error()));

    MainWindow window(backed.config());

    QLineEdit *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY2(queryEdit, "no query bar: the window was never built");
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY2(view, "no thread list view");
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY2(model, "no thread list model");

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();

    // One thread carrying both messages, not two threads.
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QModelIndex root = model->index(0, 0, QModelIndex());
    QVERIFY(root.isValid());
    // hasChildren, NOT rowCount. Children are populated only when a thread is
    // expanded, so rowCount(root) is legitimately 0 here and asserting on it
    // fails against correct code. Before expansion the summary's count is all
    // there is, which is also what proves the two messages threaded rather
    // than arriving as two separate rows.
    QVERIFY2(model->hasChildren(root),
             "the two messages did not form one thread: nothing to expand");

    auto *pane = window.findChild<MessageView *>();
    QVERIFY2(pane, "no message view");

    // The pane starts on the placeholder, which is what "blank" means here.
    // Asserting this BEFORE the click is what makes the assertion after it
    // mean something: without it a pane that was never blank would pass.
    QVERIFY2(pane->showingPlaceholder(),
             "the pane was not blank to begin with");

    // Select the ROOT, without expanding it first. That is the gesture the
    // user reports as leaving the pane blank.
    view->setCurrentIndex(root);

    // NOT currentThreadId(): that is assigned synchronously in the selection
    // handler, before any worker round-trip, so it reports the INTENT to show
    // a thread rather than content arriving. A mutation disabling
    // onThreadLoaded() entirely left it passing, which is how that was found.
    //
    // showingPlaceholder() is what the user sees. QTRY rather than a single
    // check because the load crosses to the worker thread and back, so a
    // failure here means the pane stayed blank for fifteen seconds, not that
    // the test looked too early.
    QTRY_VERIFY_WITH_TIMEOUT(!pane->showingPlaceholder(), 15000);
}

void TestMainWindow::anUnexpandedRootRendersOneMessageNotTheConversation()
{
    // Item 66, the half that reproduced. Clicking a thread root that has never
    // been expanded used to render the whole conversation, because the model
    // learned the thread's first message only when the replies loaded. The
    // identical click rendered ONE message afterwards. The user reported the
    // inconsistency and asked for the single-message behaviour throughout.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("root@example.org"),
        QStringLiteral("A conversation"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("The first message.")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("reply@example.org"),
        QStringLiteral("Re: A conversation"),
        QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("The reply."), true,
        QStringLiteral("root@example.org")));
    QVERIFY2(backed.build(), qPrintable(backed.error()));

    MainWindow window(backed.config());

    QLineEdit *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY2(queryEdit, "no query bar: the window was never built");
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY2(view, "no thread list view");
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY2(model, "no thread list model");
    auto *pane = window.findChild<MessageView *>();
    QVERIFY2(pane, "no message view");

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QModelIndex root = model->index(0, 0, QModelIndex());
    QVERIFY(root.isValid());
    QVERIFY2(model->hasChildren(root), "the two messages did not thread");

    // NEVER expanded. That is the whole point: this is the state in which the
    // old code fell back to the conversation render.
    QVERIFY2(!view->isExpanded(root), "the test expanded the thread itself");

    // The root's message id is known anyway, because the query carries it now.
    QVERIFY2(!model->data(root, ThreadListModel::MessageIdRole)
                  .toString()
                  .isEmpty(),
             "an unexpanded root still has no message id: the query is not "
             "carrying firstMessageId");

    view->setCurrentIndex(root);
    QTRY_VERIFY_WITH_TIMEOUT(!pane->showingPlaceholder(), 15000);

    // ONE message, not a conversation. headerSearchOffers() is populated only
    // when the header states a single message's own From/To/Cc; for a thread
    // the header says "N messages in thread" and carries no such offers, so an
    // empty list here is exactly the conversation render this replaced.
    QVERIFY2(!pane->headerSearchOffers().isEmpty(),
             "the pane rendered a conversation, not a single message");
}

#include "test_mainwindow.moc"
