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

#include <QProcess>
#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QFocusEvent>
#include <QCloseEvent>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
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
#include "pendingchangesdialog.h"
#include "messageview.h"
#include "mimeparser.h"
#include "notmuchworker.h"
#include "carddelegate.h"
#include "composewindow.h"
#include "senddialog.h"
#include "composecontext.h"
#include "messagebuilder.h"
#include "draftstore.h"
#include "messagesender.h"
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QPointer>
#include <QListWidget>
#include "cardlayout.h"

#include <QImage>
#include <QPainter>
#include <QToolButton>
#include <QHBoxLayout>
#include <QComboBox>
#include <QScrollBar>
#include "tagchip.h"
#include "tagstrip.h"
#include "threaddashboard.h"
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
    /// `accountKey` and `accountMaildir` add one [account.<key>] section, which
    /// is what makes runQuery() scope the bar's text with scopedQuery(). A test
    /// that never selects an account can leave them empty.
    ///
    /// `accountTrash` writes that section's `trash` key, which Delete needs to
    /// know where to move a file to. A DEFAULTED parameter rather than an
    /// overload: an overload would have to repeat the whole body, and every
    /// existing caller passes no account at all and so writes no section and
    /// no trash key either. A caller that names an account and wants Delete to
    /// work has to say where its trash is, which is the same requirement the
    /// real config imposes.
    /// One [account.<key>] section to write.
    ///
    /// `sendCommand` is what makes the account able to send, and its EMPTINESS
    /// is what makes it receive-only: the capability is the key's presence,
    /// not a separate flag, so a receive-only account is written by omitting
    /// it exactly as the real config expresses it.
    struct AccountSpec
    {
        QString key;
        QString maildir;
        QString trash;
        QString sendCommand;
        QString address;
        /// Written only when non-empty, like trash: an account without one
        /// offers no Drafts filter and no Edit draft (items 138 and 153).
        QString drafts;
        /// Where a sent copy is filed. Written only when non-empty; an
        /// account without one sends and files nothing, which is a real
        /// configuration rather than an error.
        QString sent;
    };

    /// Writes several accounts, for the compose cases.
    ///
    /// Beside build() rather than replacing it: every existing caller passes
    /// at most one account and none of them needs a send command, so widening
    /// the three-argument signature further would make ten call sites carry
    /// two empty strings each for one test's benefit.
    bool buildWithAccounts(const QList<AccountSpec> &accounts,
                           const QString &composeKey = QString())
    {
        m_accounts = accounts;
        m_composeKey = composeKey;
        return build();
    }

    bool build(const QString &accountKey = QString(),
               const QString &accountMaildir = QString(),
               const QString &accountTrash = QString())
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
            if (!accountKey.isEmpty()) {
                // QSettings reads `/` in a section name as a group separator,
                // so the section is [account.key], never [account/key].
                out << "\n[account." << accountKey << "]\n"
                    << "maildir=" << accountMaildir << "\n";
                if (!accountTrash.isEmpty())
                    out << "trash=" << accountTrash << "\n";
                // The fixture's folders are lowercase, unlike the Maildir
                // convention Account::inboxFolder() defaults to. Stated rather
                // than assumed, which is the whole point of the key: naming a
                // folder that does not exist would CREATE it.
                out << "inbox=inbox\n";
            }
            if (!m_composeKey.isEmpty())
                out << "\n[compose]\n" << m_composeKey << "\n";
            for (const AccountSpec &account : m_accounts) {
                out << "\n[account." << account.key << "]\n"
                    << "maildir=" << account.maildir << "\n"
                    << "inbox=inbox\n";
                if (!account.trash.isEmpty())
                    out << "trash=" << account.trash << "\n";
                if (!account.address.isEmpty())
                    out << "address=" << account.address << "\n";
                // Written only when non-empty. An account with no
                // send_command is receive-only, which is the shape under test.
                if (!account.sendCommand.isEmpty())
                    out << "send_command=" << account.sendCommand << "\n";
                if (!account.drafts.isEmpty())
                    out << "drafts=" << account.drafts << "\n";
                if (!account.sent.isEmpty())
                    out << "sent=" << account.sent << "\n";
            }
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
    QList<AccountSpec> m_accounts;
    QString m_composeKey;
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
    void aPurgeTakesTheRowsOutOfTheViewWithoutARefresh();
    void theDashboardFollowsAWriteToTheConversationItShows();
    void anEditHeldByASyncSaysSoInsteadOfClaimingItLanded();

    // Compose and send, item 123 task 12.
    void theMailRootComesFromTheConfigNotTheIndex();
    void replyIsDisabledOnAReceiveOnlyAccountsMail();
    void theReceiveOnlyRibbonNamesTheAccount();
    void theReceiveOnlyRibbonGoesWithTheMessageThatRaisedIt();
    void replyIsEnabledOnASendingAccountsMail();
    void composeIsDisabledOnlyWhenNoAccountCanSend();
    void quittingWithACleanComposerAsksNothing();
    void quittingWithUnsavedEditsReportsEveryComposer();
    void closingAComposerCompactsTheRegistry();
    void savingAMessageRefusesToEscapeTheChosenDirectory();
    void aHostileSubjectCannotEscapeTheSaveDirectory();
    void savingTwiceDoesNotOverwriteTheFirstFile();
    void savingAMessageWithAHostileSubjectStaysInTheDirectory();
    void aStuckComposeRequestDoesNotHijackTheNextPaneLoad();
    void theSaveLoopToleratesAComposerClosedUnderTheDialog();
    void quittingClosesEveryComposerRatherThanOrphaningIt();
    void forwardingCarriesTheOriginalsAttachments();
    void forwardSeedsHtmlFromTheConfigNotTheOriginal();
    void aStartupAccountScopesTheStartupQuery();
    void aStartupAccountAlsoScopesASavedStartupQuery();
    void aGeneratedStartupQueryActuallyRuns();
    void everyBuiltinFilterButtonCarriesAnIconAndItsText();
    void theDraftsButtonIsAbsentWithoutADraftsFolder();
    void aQueryInTheMenuCanActuallyBeRun();
    void theFourBuiltinFiltersAreOnTheRowInOrder();
    void aFilterComposesWithTheSelectedAccount();
    void aFilterAcrossAllAccountsIsUnscoped();
    void aFilterDoesNotClearTheAccountSelection();
    void theActiveFilterButtonIsChecked();
    void aHandEditedQueryChecksNoFilterButton();
    void theCheckedFilterFollowsTheAccount();
    void aSavedQueryStillClearsTheAccountSelection();
    void aFilterOffersNoEditOrDeleteActions();
    void changingTheAccountRunsNothing();
    void arrivingBatchesUpdateTheStatusBarWithTheCountSoFar();
    void aRefreshsBatchesLeaveTheStatusBarAlone();
    void selectingAThreadRootShowsItInTheMessagePane();
    void anUnexpandedRootShowsTheDashboardLikeAnExpandedOne();
    void aSingleMessageIdQuerysCardOpensInTheMessagePane();
    void autoSyncIsNotArmedWhenDisabledOrWithNothingPending();
    void autoSyncSkipsWhileABackgroundSyncIsRunning();
    void aSkippedAutoSyncRearmsRatherThanGivingUp();
    void aHeldEditIsSentBeforeTheSyncEndRefreshReadsTheDatabase();
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
    void aCronSyncRefreshesTheLastRunQueryNotTheQueryBar();
    void aRefreshAddsNewMailAndDropsWhatStoppedMatching();
    void theOpenThreadLeavingTheListRaisesTheStaleNotice();
    void aThreadStillMatchingRaisesNoStaleNotice();
    void theStaleNoticeCarriesTheMessageBeingRead();
    void recoveringAStaleThreadQueriesTheWholeThread();
    void recoveryReselectsTheMessageThatWasBeingRead();
    void recoveryOnTheFirstMessageSelectsTheThreadRow();
    void doubleClickingAThreadOpensThatThreadAlone();
    void doubleClickingAReplyOpensItsThreadNotTheReplyAlone();
    void doubleClickingDoesNotLeaveTheMarkReadTimerArmed();
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
    void selectingAConversationArmsNoMarkRead();
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
    void toggleUnreadOnAReplyReadsItsOwnThreadNotTheFirstInTheList();
    void importantOnAnAlreadyImportantThreadRemovesTheTag();
    void importantOnAPlainThreadStillAddsTheTag();
    void importantOnAReplyReadsItsOwnStateNotItsThreads();
    void editTagsOnAReplyCountsItsOwnThreadNotTheFirstInTheList();
    void markCurrentThreadReadResolvesTheThreadThroughTheIndex();
    void deletingAReplyRepaintsThatReplyRow();
    void deleteIsHiddenOnMailAlreadyInTheTrash();
    void aPartlyTrashedConversationIsNotJudgedOnOneMessage();
    void restoreIsHiddenOnMailThatWasNeverDeleted();
    void deleteAlsoMarksTheMessageRead();
    void emptyTrashAsksBeforeDestroyingAnything();
    void theUnreadLabelSaysWhichDirectionItWillGo();
    void theUnreadLabelFollowsAWriteWithoutReselecting();
    void aMixedSelectionIsMarkedReadAndTheActionStaysVisible();
    void aMixedThreadIsMarkedReadAndTheSecondPressIsTheWayBack();
    void toggleUnreadOnAReplyReadsTheReplysOwnState();
    void toggleUnreadOnAReplyRepaintsItInBothDirections();
    void taggingTheOpenReplyUpdatesTheMessagePaneStrip();
    void taggingAnUnrelatedReplyLeavesTheStripAlone();
    void aHeldMessageEditIsSentWhenTheSyncEnds();
    void anActionOnAConversationRowTakesTheConversation();
    void selectingAConversationShowsTheDashboard();
    void selectingALoneMessageShowsTheMessage();
    void autoMarkReadTouchesOnlyTheMessageOnDisplay();
    void autoMarkReadArmsForAReplyToo();
    void taggingTheOpenRootMessageKeepsTheStripPopulated();
    void aLoadedMessageCorrectsTheStripFromTheThreadsUnion();
    void aTransientStatusMessageExpires();
    void theSelectionCountIsStateAndDoesNotExpire();
    void anEditUndoneNettsBackToZero();
    void aDifferentTagOnTheSameMessageStillCounts();
    void everyPendingChangeCanNameItsMessages();
    void theSnapshotGroupsActionsUnderTheirMessage();
    void theSnapshotKeepsAThreadActionThreadScoped();
    void theIndicatorOpensItsListOnAClick();
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
    void everyActionIsReachableFromAMenu();
    void noMenuHasTwoEntriesSharingAMnemonic();
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
    void theMessagePaneCarriesItsOwnActionBar();
    void theMainToolbarKeepsOnlyListWideActions();
    void theMessageBarSitsAboveTheBodyAndBelowTheHeader();
    void theMessageBarIconsAreSmallerThanTheToolbars();

    void everySavedQueryLivesInTheMenu();
    void menuEntriesFollowTheDocumentOrder();
    void aScopedSavedQuerySelectsItsAccount();
    void anUnscopedSavedQueryClearsTheAccount();
    void theSaveQueryActionIsDisabledOnAnEmptyQuery();
    void thereIsASaveButtonBesideTheQueryBar();
    void theMenuIsRightAlignedAwayFromTheButtons();
    void theRowSurvivesWithNothingButAMenu();
    void aStoredGeneratedQueryRunsFlatAndComposed();
    void aRenamedSentEntryKeepsWorking();
    void aGeneratedQueryWithNothingToShowIsSkipped();
    void aSavedQueryEntryOffersRunEditAndDelete();
    void onlyAStoredQueryOffersToBecomeATaggingRule();
    void savingStripsTheRetiredPinnedField();
    void deletingRemovesTheQueryFromTheFile();
    void anEditedQueryKeepsItsUnknownFields();
    void renamingReplacesRatherThanDuplicating();

    void deleteMovesTheMessageToTrash();
    void deleteRecordsWhereTheMessageCameFrom();
    void undoMovesTheMessageBack();
    void deleteOnAReplyMovesThatReplyOnly();
    void deleteWithoutATrashFolderSaysSoRatherThanDoingNothing();
    void undoingADeleteConsumesItsCommandRatherThanPushingAnother();
    void aDeleteHeldDuringASyncCountsAsUnsyncedWork();
    void twoDeletesToOneTrashBothGetTheirTags();
    void deletingTwiceLeavesNoOriginTagBehind();
    void undoOfADeleteRemovesTheOriginTagToo();
    void undoingAMarkReadRestoresOnlyWhatWasUnread();
    void deletingALoneMessageRemovesItFromTheInboxAndUndoReturnsIt();
    void deleteThreadMovesEveryMessageAndRepaintsTheRootCard();
    void aFolderNameWithASpaceSurvivesTheRoundTrip();
    void deleteIsBoundToTheDeleteKey();
    void theDeleteKeyEditsTextInTheQueryBar();
    void restoreIsReachableWithoutTheKeyboard();
    void restoreIsOnlyEnabledInTheTrashView();
    void restoreReturnsAMessageToItsOriginFolder();
    void restoreFallsBackToInboxWithoutAnOriginTag();
    void theCleanupQueryFindsStrandedMail();
    void theCleanupQueryExcludesMailAlreadyInTrash();
    void aMoveThatRelocatesNothingWritesNoTag();
    void restoringFromTheTrashViewRefreshesTheList();
    void theRefreshAfterARestoreLeavesUndoIntact();
    void deletingOutsideTheTrashViewLeavesTheRowInPlace();

    // ComposeWindow, item 123. These need a window but no worker: the composer
    // never touches NotmuchWorker, it reads its context from the value struct
    // MainWindow hands it, so a Config written to a temporary INI is the whole
    // fixture.
    void aComposerOpensClean();
    void ctrlWClosesTheComposer();
    void aDraftReopensWithItsOwnContent();
    void editDraftIsOfferedOnlyForADraft();
    void theMessageBarOffersEditOnADraft();
    void doubleClickingADraftOpensTheComposer();
    void aResumedDraftReplacesItsFileRatherThanAddingOne();
    void aResumedDraftKeepsItsBlindRecipients();
    void aDraftRenamedByASyncStillReopensAndReplacesItsFile();
    void theComposerSplitsItsToolbarByScope();
    void ccAndBccHideBehindADisclosure();
    void ccAndBccAreRevealedWhenTheyCarryAValue();
    void removeAttachmentAppearsOnlyWithAttachments();
    void typingMarksTheComposerDirty();
    void anAutosaveWritesADraftAndClearsTheDirtyFlag();
    void anUnwritableDraftsFolderRaisesThePersistentBanner();
    void aSuccessfulSaveClearsTheBanner();
    void anAccountWithoutADraftsFolderReportsNoFailure();
    void aRewrittenDraftUnlinksThePreviousRevision();
    void theComposerBuildsTheMessageItsWidgetsShow();
    void theFromDropdownDecidesWhichAccountSends();
    void aFormatEditPreservesTheUndoStack();
    void aFormatEditRestoresTheSelectionItAsksFor();
    void aFormatEditOnAnEmptySelectionLandsBetweenTheTokens();
    void theAttachmentWarningRespectsTheConfiguredThreshold();
    void aDisabledAttachmentWarningWarnsAboutNothing();
    void theQuotePositionDecidesWhereTheQuoteLands();
    void theCursorStartsOnBlankSpaceNotOnTheQuote();
    void aReplyOpensWithTheBodyFocused();
    void theSeededQuoteIsNotAnUndoStep();
    void aReplySeedsTheHtmlToggleFromTheOriginal();
    void aNewMessageSeedsTheHtmlToggleFromConfig();
    void disablingInputsCoversEveryFieldAndTheToolbar();
    void aFailedSendCanBeRetriedWithoutFilingTheWrongCopy();
    void anUnchangedMessageIsNotWrittenAgain();
    void closingInsideTheDebounceStillSavesTheDraft();
    void closingAfterASendWritesNoFurtherDraft();
    void aSendRemovesADraftMbsyncHasRenamed();
    void aForwardFlagsTheMessageItForwarded();
    void aReplyFlagsTheMessageItAnswered();
    void aResumedDraftFlagsNothing();
    void aForwardWritesThePassedTagToTheIndex();
    void aCloseDuringTheCountdownIsRefused();
    void aFailedSendKeepsTheTextThatFailedToGo();
    void aSmallSizeLimitIsNotDescribedAsZeroMegabytes();
    void theBusinessSenderListIsLoadedAtStartup();

    // Item 177, task 5: the scope comes from the row, and the labels say so.
    void theUnreadActionNamesTheThreadOnAConversationRow();
    void deleteIsAbsentOnAReplyRow();
    void theWholeThreadSubmenuIsGone();
    void forwardAndSaveAreAbsentOnAConversationRow();
    void replyOnAConversationRowNamesTheThread();
    void replyToAConversationAnswersItsNewestMessage();
    void replyIsUntouchedOnAMessageRow();

    // Item 177, task 6: membership is the union over the conversation.
    void aConversationStaysWhileAnyMessageMatches();
    void aConversationLeavesWhenItsUnionEmpties();

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

    // The message the row displays, which the real worker fills in from the
    // query. Required since item 108: an ordinary tag action resolves a thread
    // row to THIS id, so a summary without one names no message and every
    // action on it does nothing. A fixture missing it fails with "the action
    // did not happen", which reads as a defect in the action.
    thread.firstMessageId = id + QStringLiteral("-first@example.org");
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
    // Child 1, not child 0: since item 177 a conversation lists its FIRST
    // message as a child too, so child 0 is m0 (depth 0) and the reply whose
    // nesting is being measured is the one after it.
    const QModelIndex child = model->index(1, 0, root);
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

    // Guards: the model agrees about which thread has an expander, and only
    // that one is offered one at all. The count is MESSAGES: the three-message
    // thread reads 3, the lone message reads 0.
    QCOMPARE(model->data(first, ThreadListModel::MessageCountRole).toInt(), 3);
    QCOMPARE(model->data(second, ThreadListModel::MessageCountRole).toInt(), 0);

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

    // The plain `archive`, on a CONVERSATION row. Item 108's separate
    // `archive_thread` is gone: since item 177 the row's identity is what
    // makes this thread-scoped, and the fixture's totalCount of 7 is what
    // makes the row a conversation.
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

    // Child 1, not child 0: since item 177 a conversation lists its first
    // message as a child too, so child 0 is m0 and the reply this test acts on
    // is the one after the card's own message.
    const QModelIndex messageRow = model->index(1, 0, threadRow);
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

    // The confirmation the real worker sends back, which since item 176 is
    // what tells the command WHICH messages it moved. There is no worker in
    // this window, so it is delivered by hand; without it the command knows
    // of no change and correctly undoes nothing.
    const TagChange confirmed{ { QStringLiteral("t1-first@example.org"),
                                 QStringLiteral("t2-first@example.org"),
                                 QStringLiteral("t3-first@example.org") },
                               {},
                               { QStringLiteral("unread") },
                               QStringLiteral("Mark all read") };
    QMetaObject::invokeMethod(&window, "onTagsApplied",
                              Q_ARG(TagChange, confirmed));

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
    // A real edit always names the messages it touched: the worker's only
    // emitter of tagsApplied() returns early without them (item 119).
    change.messageIds = { QStringLiteral("pq1@example.org") };
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
    // See item 119: a change with no message ids never reaches this slot.
    change.messageIds = { QStringLiteral("fs1@example.org") };
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
    // See item 119: a change with no message ids never reaches this slot.
    change.messageIds = { QStringLiteral("se1@example.org") };
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

void TestMainWindow::aCronSyncRefreshesTheLastRunQueryNotTheQueryBar()
{
    // The query bar holds text the user has typed but not run, and a refresh
    // must not execute it: that is a search they never asked for. The refresh
    // re-runs the LAST RUN query, so with none there is nothing to do.
    //
    // Since item 93 a default Config DOES run a query at startup: the default
    // startup name resolves to the built-in Unread filter, where before it
    // named nothing and a fresh window had no last-run query at all.
    //
    // The property under test survives that, and is the one that matters on a
    // cron timer: the refresh re-runs the LAST RUN query, never the text
    // sitting in the bar. So the bar is given something the user has typed and
    // not run, and the assertion is that what the refresh runs is still the
    // startup query.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);

    const QString ranAtStartup = window.lastRunQueryForTesting();
    QVERIFY2(!ranAtStartup.isEmpty(),
             "no startup query ran, so a refresh has nothing to re-run and "
             "this test cannot distinguish the two sources");

    queryEdit->setText(QStringLiteral("tag:draft-i-was-typing"));

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));

    QCOMPARE(window.lastRunQueryForTesting(), ranAtStartup);
    QVERIFY2(!window.lastRunQueryForTesting().contains(
                 QStringLiteral("draft-i-was-typing")),
             "the refresh executed the text in the query bar, which is a "
             "search the user never asked for");
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
    // Child 1, not child 0: since item 177 a conversation lists its first
    // message as a child too, so the reply being read is the one after the
    // card's own message.
    const QModelIndex replyIndex = model->index(1, 0, threadIndex);
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

void TestMainWindow::doubleClickingAThreadOpensThatThreadAlone()
{
    // Item 91, the thread case: "double click on a thread loads the whole
    // thread expanded in a view by itself and load the first message in the
    // right pane."
    //
    // The view becomes thread:<id>. Asserted on the query text because that is
    // what makes it a view "by itself"; a gesture that only expanded the row in
    // place would leave every other thread on screen and pass any assertion
    // about the expansion alone.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    ThreadSummary first = makeThread(QStringLiteral("T1"), {});
    first.totalCount = 3;
    model->appendBatch({ first, makeThread(QStringLiteral("T2"), {}) });
    QCOMPARE(model->rowCount(QModelIndex()), 2);

    const QModelIndex thread = model->index(0, 0, QModelIndex());
    QVERIFY(thread.isValid());

    emit view->doubleClicked(thread);

    QCOMPARE(queryEdit->text(), QStringLiteral("thread:T1"));

    // The recovery target is what carries the expansion and the selection
    // across the two round-trips this takes. Without it the query would run and
    // land on a collapsed card with a blank pane.
    QVERIFY2(window.hasPendingRecoveryForTesting(),
             "the double-click ran a query but asked for nothing to be "
             "expanded or selected in the result");
}

void TestMainWindow::doubleClickingAReplyOpensItsThreadNotTheReplyAlone()
{
    // Item 91, the case most likely to be built wrong: "double click on a reply
    // in a thread should still load the whole thread expanded in a view by
    // itself, with the reply I clicked on visible in the right pane."
    //
    // So NOT id:<reply>. The obvious reading of "open it by itself" is a query
    // for that one message, and it is not what was asked for: the view is the
    // thread, the pane is the reply.
    const Config config;
    MainWindow window(config);

    auto *queryEdit = window.findChild<QLineEdit *>();
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);

    ThreadSummary summary = makeThread(QStringLiteral("T1"), {});
    summary.totalCount = 3;
    model->appendBatch({ summary });

    const QModelIndex thread = model->index(0, 0, QModelIndex());
    QVERIFY(thread.isValid());

    // Replies exist only once the tree has loaded, which is what gives this
    // test a child row to double-click at all.
    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("T1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("T1");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("T1"), { root, reply });

    const QModelIndex replyRow = model->index(0, 0, thread);
    QVERIFY2(replyRow.isValid(), "the fixture built no reply row");
    QVERIFY2(model->isMessageRow(replyRow), "that row is not a message row");

    emit view->doubleClicked(replyRow);

    // The THREAD, not the reply. A query of id:m1@example.org here would be the
    // defect this test exists for.
    QCOMPARE(queryEdit->text(), QStringLiteral("thread:T1"));
    QVERIFY2(window.hasPendingRecoveryForTesting(),
             "nothing was remembered to select the reply once it comes back");
}

void TestMainWindow::doubleClickingDoesNotLeaveTheMarkReadTimerArmed()
{
    // A double-click delivers a single click FIRST, which selects the row and
    // arms the mark-read timer. The user is passing through on their way to
    // opening the thread, so the message must not be marked read behind the
    // drill-down: the timer that the first click armed has to be cancelled.
    //
    // This is the same reasoning the multi-row branch of onThreadSelected()
    // uses, where a selection gesture must not mutate mail.
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
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);

    model->appendBatch({ makeThread(QStringLiteral("T1"),
                                    { QStringLiteral("unread") }) });
    const QModelIndex thread = model->index(0, 0, QModelIndex());
    QVERIFY(thread.isValid());

    // The single click a real double-click delivers first. Asserted, so this
    // test cannot pass by the timer never having been armed at all.
    selectThreadRow(view, 0);
    QVERIFY2(timer->isActive(),
             "the selection did not arm the timer, so this proves nothing");

    emit view->doubleClicked(thread);

    QVERIFY2(!timer->isActive(),
             "the drill-down left a mark-read armed for a message the user "
             "only passed through");
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

    // totalCount = 1 since item 177, and that is a retarget rather than a
    // weakening. The stale notice describes the message the pane is DISPLAYING,
    // and a conversation row displays none: it shows the dashboard. A thread of
    // one still renders its message, which is the case the notice is for, and
    // the root-sets-both-ids condition this test was written against is
    // unchanged there.
    ThreadSummary thread = makeThread(QStringLiteral("T1"),
                                      { QStringLiteral("unread") });
    thread.totalCount = 1;
    model->appendBatch({ thread });

    // The root knows its own message once the tree is loaded, which is what
    // makes the pane show one message rather than the conversation. One node,
    // at depth 0: a thread of one has no replies to carry.
    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("T1");
    root.depth = 0;
    model->setThreadMessages(QStringLiteral("T1"), { root });

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
    // All four messages are children since item 177: a conversation lists its
    // first message under itself like every other.
    QCOMPARE(model->rowCount(threadIndex), 4);

    // The fourth message, now at child index 3 behind the first three.
    const QModelIndex fourth = model->index(3, 0, threadIndex);
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
    QCOMPARE(model->rowCount(back), 4);

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

void TestMainWindow::aSkippedAutoSyncRearmsRatherThanGivingUp()
{
    // Item 89, the concrete half. Skipping is correct and must stay, but the
    // skip used to be the END of the attempt: the timer had fired, nothing
    // re-armed it, and the edit waited for a manual sync or the next cron run.
    //
    // The comment defending it said the running sync was "very likely" to carry
    // the edit, since it reached the mail store at edit time. Very likely is not
    // always: an edit made after mbsync has already passed that account's
    // mailbox is not carried by it, and the pending count then sits non-zero
    // with nothing scheduled to clear it.
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
    QVERIFY2(timer->isActive(), "the edit did not arm the debounce at all");

    // Fire it by hand rather than waiting out the delay. A QTimer that has
    // fired is no longer active, so this is also what makes the assertion
    // below mean something: without the re-arm it is inactive here.
    timer->stop();
    QVERIFY(QMetaObject::invokeMethod(&window, "runAutoSync"));

    QVERIFY2(timer->isActive(),
             "a skipped automatic sync left nothing armed to carry the edit");

    // Re-armed at the configured debounce, not at some shorter interval that
    // would spin against a long external sync. SyncMonitor polls /proc/locks,
    // so an m_externalSyncBusy that never clears re-arms at this interval
    // indefinitely, which is cheap only because the interval is the user's own.
    QCOMPARE(timer->interval(), config.autoSyncDelayMs());

    // The edit is still pending throughout: a retry must not look like a
    // completed sync to the indicator.
    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);
    QVERIFY2(!label->isHidden(),
             "the re-armed sync cleared the pending indicator");
}

void TestMainWindow::aHeldEditIsSentBeforeTheSyncEndRefreshReadsTheDatabase()
{
    // Reported by hand, 2026-08-15: read a message while a cron sync is
    // running, watch the unread tag go and the status bar say the change will
    // be applied when the sync finishes, and when it does the message is unread
    // again.
    //
    // The edit is held during a sync because the worker's read-write open
    // BLOCKS on notmuch's exclusive lock. At sync end the handler refreshed the
    // list FIRST and flushed the held edits afterwards, so the refresh read a
    // database that still carried `unread`, reconciled it into the model, and
    // overwrote the optimistic update. The flush then wrote the tag correctly.
    // The database ended up right and the list ended up wrong, with nothing
    // scheduled to re-read it, which is why it looked like the edit was lost.
    //
    // Asserted on the ORDER, not on the tag: this window has no worker to
    // answer either the refresh or the write, so the rows cannot show the
    // outcome. What decides the bug is whether the edit had been sent by the
    // time the refresh was issued.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString locks = dir.filePath(QStringLiteral("locks"));
    {
        QFile f(locks);
        QVERIFY(f.open(QIODevice::WriteOnly));
        // A held lock, so the edit below is held rather than sent.
        f.write("1: FLOCK  ADVISORY  WRITE 1 00:00:0 0\n");
    }
    MainWindow::setLocksPathForTesting(locks);

    Config config;
    config.load(writeSyncConfig(dir));
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
    selectThreadRow(view, 0);

    // The sync is observed as running, which is what makes the edit held.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));

    auto *toggleUnread =
        window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(toggleUnread);
    toggleUnread->trigger();

    QVERIFY2(window.hasEditAwaitingSend(),
             "the edit was not held, so this test proves nothing about the "
             "order the sync-end handler does things in");

    const quint64 before = window.currentGenerationForTesting();

    // The sync ends. Both the refresh and the flush happen in this one call.
    {
        QFile f(locks);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    }
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));

    QVERIFY2(!window.hasEditAwaitingSend(),
             "the sync ended without ever flushing the held edit");

    // The write went out. Nothing else in this handler sends one, so its
    // presence is what proves the flush ran, and the refresh below is what it
    // has to have run BEFORE.
    //
    // Either scope counts. The gesture is Toggle unread on a thread row, which
    // is message-scoped since item 108, so the ids land in the message list;
    // the ORDER this test exists for is the same either way, and pinning the
    // scope here would make it fail for a reason it does not care about.
    QVERIFY2(!window.pendingThreadIdsForTesting().isEmpty()
                 || !window.pendingMessageIdsForTesting().isEmpty(),
             "the held edit was dropped rather than sent");

    const quint64 after = window.currentGenerationForTesting();
    QVERIFY2(after > before, "the sync end did not refresh the list at all");

    // THE ASSERTION THIS TEST EXISTS FOR. Both orders leave identical end
    // state, so everything above passes against the defect; only the
    // generation stamped at flush time separates them.
    //
    // Flushing first means the stamp is the generation from BEFORE the refresh
    // bumped it. Refreshing first means the flush sees the bumped one, and the
    // refresh has already read a database that still carries the old tag.
    QCOMPARE(window.flushGenerationForTesting(), before);
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
    // `delete` on a CONVERSATION row, which is the thread-scoped delete since
    // item 177: the row's identity is what decides the scope, so a summary
    // carrying `deleted` is the right thing to read here.
    auto *action = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(action);

    ThreadSummary deleted = makeThread(QStringLiteral("t1"),
                                       { QStringLiteral("deleted") });
    deleted.totalCount = 3;
    model->appendBatch({ deleted });
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

    // Conversations, so `delete` is thread-scoped on both and the two rows
    // really are in opposite THREAD states.
    ThreadSummary first = makeThread(QStringLiteral("t1"),
                                     { QStringLiteral("deleted") });
    first.totalCount = 2;
    ThreadSummary second = makeThread(QStringLiteral("t2"), {});
    second.totalCount = 2;
    model->appendBatch({ first, second });

    view->selectAll();
    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);

    action->trigger();

    QVERIFY2(model->threadAt(0).isDeleted() && model->threadAt(1).isDeleted(),
             "a mixed selection split instead of deleting the whole selection");
}

/// Builds a window whose SECOND thread is expanded and carries one reply, with
/// the two threads deliberately in opposite states.
///
/// Item 88's shape in one place. A tree numbers rows per parent, so the first
/// reply of any thread has row() == 0 and threadAt(0) answers about the FIRST
/// THREAD IN THE LIST. Every test below selects that reply and asserts on
/// behaviour that can only be right if the thread was resolved through the
/// index: with the row number, each one reads t1's state while acting on t2.
///
/// The opposite states are what makes the tests able to fail. Two threads in
/// the same state give the same answer either way, which is how the reverted
/// item 87 fix passed while corrupting mail.
///
/// \p replyTags defaults to the thread's own tags, which is the usual case.
/// Pass it explicitly to make a reply DISAGREE with its thread, which is what
/// separates "reads the right thread" from "reads the right message": a reply
/// can be unread inside a thread that is not, and vice versa.
static QModelIndex expandSecondThreadAndSelectItsReply(
    QTreeView *view, ThreadListModel *model, const QStringList &firstTags,
    const QStringList &secondTags,
    const std::optional<QStringList> &replyTags = std::nullopt)
{
    ThreadSummary first = makeThread(QStringLiteral("t1"), firstTags);
    ThreadSummary second = makeThread(QStringLiteral("t2"), secondTags);
    second.totalCount = 2;
    model->appendBatch({ first, second });

    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("t2");
    root.tags = secondTags;
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m1@example.org");
    reply.threadId = QStringLiteral("t2");
    reply.tags = replyTags.value_or(secondTags);
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t2"), { root, reply });

    const QModelIndex threadRow = model->index(1, 0, QModelIndex());
    view->expand(threadRow);

    // Child 1, not child 0. Since item 177 a conversation lists its FIRST
    // message as a child too, so child 0 is m0 and the reply this helper
    // promises is the one after it. Every caller is about a reply
    // specifically, and handing them the root message instead makes each one
    // assert about the wrong message while still looking correct.
    const QModelIndex replyRow = model->index(1, 0, threadRow);
    if (!replyRow.isValid() || !model->isMessageRow(replyRow))
        return {};

    // A child row number that is also a plausible top-level one, which is the
    // trap: threadAt() cannot tell the difference. Child 1 sits under the
    // thread at top-level row 1, so threadAt() reading it still lands on the
    // wrong object, which is what these tests exist to catch.
    if (replyRow.row() != 1)
        return {};

    view->selectionModel()->select(
        replyRow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(replyRow);
    QApplication::processEvents();
    return replyRow;
}

void TestMainWindow::toggleUnreadOnAReplyReadsItsOwnThreadNotTheFirstInTheList()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(action);

    // t1 unread, t2 read. Reading t1's state marks an already-read message
    // read again, which is a no-op write the user sees as a dead key.
    const QModelIndex reply = expandSecondThreadAndSelectItsReply(
        view, model, { QStringLiteral("unread") }, {});
    QVERIFY2(reply.isValid(), "the fixture did not produce a reply row at row 0");

    action->trigger();

    QCOMPARE(window.undoDepthForTesting(), 1);
    QVERIFY2(window.undoTextForTesting().contains(QStringLiteral("Mark unread")),
             qPrintable(QStringLiteral(
                            "Toggle unread on a reply of a READ thread chose "
                            "the wrong direction: %1. It read the FIRST "
                            "thread's state, which is unread.")
                            .arg(window.undoTextForTesting())));
}

void TestMainWindow::importantOnAnAlreadyImportantThreadRemovesTheTag()
{
    // Item 98. `flag` was a one-way add, so pressing it on a thread that is
    // already important re-sent a tag the thread had: a no-op write, and a
    // no-op repaints nothing, so the key read as dead. Its two neighbours,
    // Delete and Toggle unread, had been toggles for a long time.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    QStringList{ QStringLiteral("inbox"),
                                                 QStringLiteral("flagged") }) });
    QApplication::processEvents();

    view->setCurrentIndex(model->index(0, 0));
    view->selectionModel()->select(model->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect
                                       | QItemSelectionModel::Rows);
    QApplication::processEvents();

    action->trigger();

    QCOMPARE(window.undoDepthForTesting(), 1);
    QVERIFY2(window.undoTextForTesting().contains(
                 QStringLiteral("Unmark important")),
             qPrintable(QStringLiteral(
                            "Important on an already-important thread did not "
                            "remove the tag: %1. A one-way add is a no-op the "
                            "user cannot see.")
                            .arg(window.undoTextForTesting())));
}

void TestMainWindow::importantOnAPlainThreadStillAddsTheTag()
{
    // The other direction, so a mutation inverting the test above cannot pass.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    QStringList{ QStringLiteral("inbox") }) });
    QApplication::processEvents();

    view->setCurrentIndex(model->index(0, 0));
    view->selectionModel()->select(model->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect
                                       | QItemSelectionModel::Rows);
    QApplication::processEvents();

    action->trigger();

    QCOMPARE(window.undoDepthForTesting(), 1);
    QVERIFY2(window.undoTextForTesting().contains(
                 QStringLiteral("Mark important"))
                 && !window.undoTextForTesting().contains(
                     QStringLiteral("Unmark")),
             qPrintable(QStringLiteral(
                            "Important on a plain thread did not add the tag: "
                            "%1")
                            .arg(window.undoTextForTesting())));
}

void TestMainWindow::importantOnAReplyReadsItsOwnStateNotItsThreads()
{
    // The trap items 88 and 105 each fixed once, which is why item 98 says to
    // call everySelectedRowHasTag() rather than copy the then-current Delete
    // loop.
    //
    // THREE states, all different, which is what the test needs to distinguish
    // the two wrong answers from the right one. t1 (the first thread in the
    // list) is unflagged, t2 (the reply's own thread) is flagged, and the
    // REPLY is unflagged. Reading t1 by row number answers "not flagged" and
    // reading the reply's THREAD answers "flagged", so only a read of the
    // message itself gives "not flagged" for the right reason.
    //
    // Leaving the reply's tags defaulted to its thread's is the trap: a reply
    // in the same state as its thread answers identically whichever of the two
    // the code reads, and the mutation putting item 105's bug back stays green.
    // Measured: it did.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);

    const QModelIndex reply = expandSecondThreadAndSelectItsReply(
        view, model, {}, { QStringLiteral("flagged") },
        QStringList{});
    QVERIFY2(reply.isValid(),
             "the fixture did not produce a reply row at row 0, so this test "
             "would assert nothing");

    action->trigger();

    QVERIFY2(window.pendingMessageIdsForTesting().contains(
                 QStringLiteral("m1@example.org")),
             "Important on a reply did not act on that reply");
    QCOMPARE(window.undoDepthForTesting(), 1);
    QVERIFY2(window.undoTextForTesting().contains(
                 QStringLiteral("Mark important"))
                 && !window.undoTextForTesting().contains(
                     QStringLiteral("Unmark")),
             qPrintable(QStringLiteral(
                            "Important on an unflagged reply chose the wrong "
                            "direction: %1. Its own THREAD is flagged, so "
                            "reading the thread gives Unmark.")
                            .arg(window.undoTextForTesting())));
}

void TestMainWindow::editTagsOnAReplyCountsItsOwnThreadNotTheFirstInTheList()
{
    // The tag dialog is modal, so what is tested is the count it is BUILT
    // from. Those counts drive its tri-state checkboxes, so a wrong count
    // offers to remove a tag the message does not carry and shows the ones it
    // does as unset.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    const QModelIndex reply = expandSecondThreadAndSelectItsReply(
        view, model, { QStringLiteral("t1only") }, { QStringLiteral("t2only") });
    QVERIFY2(reply.isValid(), "the fixture did not produce a reply row at row 0");

    const QHash<QString, int> counts = window.selectionTagCountsForTesting();

    QVERIFY2(counts.contains(QStringLiteral("t2only")),
             "the tag dialog would not offer the tag the selected reply "
             "actually carries");
    QVERIFY2(!counts.contains(QStringLiteral("t1only")),
             "the tag dialog counted the FIRST thread's tags for a reply of "
             "the second, so it would offer to remove a tag that is not there");
}

void TestMainWindow::markCurrentThreadReadResolvesTheThreadThroughTheIndex()
{
    // Item 87 is blocked on this and will scope the write to one message. Today
    // an unrelated guard hides the defect: onThreadSelected clears
    // m_currentThreadId for a message row, so markCurrentThreadRead returns
    // before it can read the wrong thread. That guard is not the protection
    // this needs, and item 87 does not remove it, so the assertion here is on
    // the resolution itself rather than on a write that cannot currently
    // happen.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    const QModelIndex reply = expandSecondThreadAndSelectItsReply(
        view, model, { QStringLiteral("unread") }, { QStringLiteral("unread") });
    QVERIFY2(reply.isValid(), "the fixture did not produce a reply row at row 0");

    // The question the timer's handler asks, in isolation: which thread is the
    // current row part of. With the row number this answers "t1" for a reply
    // of t2.
    QCOMPARE(window.threadForCurrentRowForTesting().threadId,
             QStringLiteral("t2"));
}

void TestMainWindow::deletingAReplyRepaintsThatReplyRow()
{
    // `spam`, not `delete`. Since item 103 Delete MOVES the file, so it needs
    // an account with a configured trash folder and a worker to do the move;
    // this bare window has neither, and Delete correctly refuses. What is
    // under test here is unchanged by that: `spam` is the other message-scoped
    // tag-only action, and it paints the same doomed state.
    // The user's report, at the gesture level: "I'm hitting delete on a reply
    // to a thread, I see the edits counter increasing but I have no feedback
    // if that message is being deleted." The model-level test proves
    // applyMessageTagChange works; this proves the action reaches it, which is
    // the half that was actually missing.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("spam"));
    QVERIFY(action);

    const QModelIndex reply =
        expandSecondThreadAndSelectItsReply(view, model, {}, {});
    QVERIFY2(reply.isValid(), "the fixture did not produce a reply row at row 0");

    // Nothing to see before the gesture, so the assertion after it means
    // something.
    QVERIFY(!model->messageAt(reply).isSpam());
    const QVariant before = model->data(reply, Qt::BackgroundRole);

    QSignalSpy spy(model, &QAbstractItemModel::dataChanged);
    action->trigger();

    QVERIFY2(model->messageAt(reply).isSpam(),
             "Delete on a reply left the reply's own row unchanged, so the "
             "pending count moved and the user saw nothing");
    QVERIFY2(spy.count() >= 1, "no repaint was requested for the reply's row");
    QVERIFY2(model->data(reply, Qt::BackgroundRole) != before,
             "the deleted reply paints exactly as it did before");

    // The THREAD row must not follow: it stands for the whole conversation,
    // and one deleted reply does not doom it.
    const QModelIndex threadRow = reply.parent();
    QVERIFY2(!model->threadFor(threadRow).isSpam(),
             "deleting one reply marked its whole thread deleted");
}

/// A window whose one account owns `acct/`, with its trash at `acct/trash`.
///
/// Delete and Restore both ask about a row's PATH, so a test for either needs
/// a config that says which prefix is a trash folder. Bare-window tests carry
/// no account at all and would answer "not in the trash" for every row.
static Config configWithTrash(QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "[account.acct]\n"
            << "maildir = acct\n"
            << "trash = trash\n"
            << "inbox = inbox\n";
    }
    Config config;
    config.load(path);
    return config;
}

/// One thread row whose displayed message sits at `filePath`.
static ThreadSummary threadAtPath(const QString &id, const QString &filePath,
                                  const QStringList &tags = {})
{
    ThreadSummary thread = makeThread(id, tags);
    thread.firstMessagePath = filePath;
    thread.firstMessageTags = tags;
    return thread;
}

/// Item 178. A CONVERSATION is in the trash only when ALL of its messages are.
///
/// The predicate read ThreadSummary::firstMessagePath for any row that is not
/// a message row, which was right while a thread row MEANT that message (item
/// 108) and stopped being right when item 177 made it mean the conversation.
/// So a partly trashed thread answered on whichever message the query returned
/// first: Delete hidden on a conversation that still has mail outside the
/// trash, Restore offered on one that mostly does not.
///
/// qtmaildir cannot itself produce such a thread, since Delete is hidden on a
/// reply row and Restore is thread-scoped. Two things outside it can: another
/// client trashing one message (the user runs Thunderbird, item 104), and a
/// reply arriving after the conversation was trashed.
///
/// The two messages are in DIFFERENT folders deliberately. Two in the same
/// folder answer identically whichever way the code resolves them, which is
/// the trap AGENTS.md records for item 87.
void TestMainWindow::aPartlyTrashedConversationIsNotJudgedOnOneMessage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Config config = configWithTrash(dir);
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *deleteAction =
        window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(deleteAction);
    auto *restoreAction =
        window.findChild<QAction *>(QStringLiteral("restore"));
    QVERIFY(restoreAction);

    // totalCount is what item 177 reads to decide a row is a conversation. A
    // summary left at the default is a MESSAGE row, so a test meaning to
    // exercise a conversation would quietly exercise the other branch and pass
    // for the wrong reason.
    ThreadSummary partly = threadAtPath(QStringLiteral("t1"),
                                        QStringLiteral("acct/trash/cur/1:2,S"));
    partly.totalCount = 2;
    model->appendBatch({ partly });

    const QModelIndex row = model->index(0, 0, {});
    QVERIFY2(model->isConversationRow(row),
             "the fixture is a message row, so this test cannot see item 178 "
             "at all: set totalCount");

    view->setCurrentIndex(row);

    // The paths the worker reports for this conversation: the displayed
    // message is in the trash, the other is not. Delete must survive and
    // Restore must not be offered, because the conversation is NOT wholly
    // trashed however its first message looks.
    window.setConversationPathsForTesting(
        QStringLiteral("t1"),
        { QStringLiteral("acct/trash/cur/1:2,S"),
          QStringLiteral("acct/inbox/cur/2:2,S") });

    QVERIFY2(deleteAction->isVisible(),
             "Delete was hidden on a conversation with mail outside the "
             "trash: it judged the thread on its first message");
    QVERIFY2(!restoreAction->isVisible(),
             "Restore was offered on a conversation that is only partly "
             "trashed");

    // And the whole-conversation case still answers as it always did, which is
    // what says the fix narrowed nothing.
    window.setConversationPathsForTesting(
        QStringLiteral("t1"),
        { QStringLiteral("acct/trash/cur/1:2,S"),
          QStringLiteral("acct/trash/cur/2:2,S") });

    QVERIFY2(!deleteAction->isVisible(),
             "Delete is still offered on a wholly trashed conversation");
    QVERIFY2(restoreAction->isVisible(),
             "Restore vanished on a wholly trashed conversation");
}

void TestMainWindow::deleteIsHiddenOnMailAlreadyInTheTrash()
{
    // Item 168, from the user: "I noticed I can hit delete via context menu on
    // a message already in the trash."
    //
    // It was not dangerous, which is the part that made it survive: the file
    // is already in the destination, so moveMessages() takes its
    // already-there branch, reports the message as moved and counts an
    // unsynced change for a move that never happened. The menu claimed to
    // have done something and nothing had.
    //
    // The question is about the PATH, never the `deleted` TAG: a message
    // trashed by another client carries no such tag, which is why item 103
    // made the trash view path-based, and asking the tag would offer Delete on
    // exactly the mail a trash view is full of.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Config config = configWithTrash(dir);
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *deleteAction =
        window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(deleteAction);

    model->appendBatch({
        threadAtPath(QStringLiteral("t1"),
                     QStringLiteral("acct/inbox/cur/1:2,S")),
        threadAtPath(QStringLiteral("t2"),
                     QStringLiteral("acct/trash/cur/2:2,S")),
    });

    view->setCurrentIndex(model->index(0, 0, {}));
    QVERIFY2(deleteAction->isVisible(),
             "Delete is hidden on mail that is NOT in the trash, so this test "
             "cannot tell the two cases apart");

    view->setCurrentIndex(model->index(1, 0, {}));
    QVERIFY2(!deleteAction->isVisible(),
             "Delete is still offered on a message already in the trash, "
             "where it reports success and does nothing");

    // A folder whose name STARTS with the trash folder's is a different
    // folder. Without the trailing separator `acct/trash-old` matches
    // `acct/trash` and Delete silently disappears from mail that was never
    // trashed, which is the quiet half of the same mistake.
    model->appendBatch({ threadAtPath(QStringLiteral("t3"),
                                      QStringLiteral("acct/trash-old/cur/3:2,S")) });
    view->setCurrentIndex(model->index(2, 0, {}));
    QVERIFY2(deleteAction->isVisible(),
             "Delete vanished on mail in acct/trash-old, which is not the "
             "trash: the prefix was compared without its separator");
}

void TestMainWindow::restoreIsHiddenOnMailThatWasNeverDeleted()
{
    // The mirror, shipped beside it: `restore` was added unconditionally to
    // both menus, so it was offered on mail that was never deleted, where it
    // has as little meaning as Delete has in the trash.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Config config = configWithTrash(dir);
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *restore = window.findChild<QAction *>(QStringLiteral("restore"));
    QVERIFY(restore);

    model->appendBatch({
        threadAtPath(QStringLiteral("t1"),
                     QStringLiteral("acct/inbox/cur/1:2,S")),
        threadAtPath(QStringLiteral("t2"),
                     QStringLiteral("acct/trash/cur/2:2,S")),
    });

    view->setCurrentIndex(model->index(1, 0, {}));
    QVERIFY2(restore->isVisible(), "Restore is hidden on trashed mail");

    view->setCurrentIndex(model->index(0, 0, {}));
    QVERIFY2(!restore->isVisible(),
             "Restore is still offered on mail that was never deleted");
}

void TestMainWindow::deleteAlsoMarksTheMessageRead()
{
    // The user's second request on the same tangent: "messages moved to the
    // trash should be automatically marked -unread". Deleting is a decision
    // about the message, so the unread count must not go on including what
    // the user threw away.
    //
    // Asserted on the undo TEXT and depth rather than on the tags: the write
    // is a move, which a bare window cannot complete, but the tag change it
    // composes is pushed as one command either way. One command, not two, is
    // the property that matters: undo has to return the folder AND the tag
    // together.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Config config = configWithTrash(dir);
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    model->appendBatch({ threadAtPath(QStringLiteral("t1"),
                                      QStringLiteral("acct/inbox/cur/1:2,S"),
                                      { QStringLiteral("unread") }) });
    const QModelIndex row = model->index(0, 0, {});
    view->setCurrentIndex(row);

    QVERIFY2(model->threadFor(row).isUnread(),
             "the fixture is already read, so this test cannot see the tag go");

    auto *deleteAction = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(deleteAction);
    deleteAction->trigger();

    QVERIFY2(!model->threadFor(row).isUnread(),
             "Delete left the message unread in the trash");
}

void TestMainWindow::emptyTrashAsksBeforeDestroyingAnything()
{
    // Item 118, and the one place this application asks. CLAUDE.md rules out
    // confirmation dialogs for mutations because every mutation pushes its
    // inverse onto the undo stack; a purge has no inverse, so the rule does
    // not reach it. What the rule protects is that a user never loses work to
    // a keystroke, and here the dialog is what provides that rather than
    // contradicting it.
    //
    // Asserting the action EXISTS and is wired, not the dialog's buttons: a
    // modal cannot be driven from a test without blocking it (item 84), so
    // the dialog itself is a hand test. What is pinned here is that nothing
    // is destroyed without going through it.
    const Config config;
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("empty_trash"));
    QVERIFY2(action, "empty_trash does not exist");

    // Reachable from a menu, which everyActionIsReachableFromAMenu() also
    // enforces globally. Named here as well because an unreachable purge is
    // worse than an unreachable anything else: the user cannot discover the
    // action, but a stray keybinding still runs it.
    bool found = false;
    const QList<QMenu *> menus = window.findChildren<QMenu *>();
    for (QMenu *menu : menus) {
        if (menu->actions().contains(action)) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "empty_trash is in no menu");

    // No shortcut, deliberately: this is the one irreversible action, and a
    // chord is exactly how it would be run by accident.
    QVERIFY2(action->shortcut().isEmpty(),
             qPrintable(QStringLiteral("empty_trash carries the shortcut %1; "
                                       "the one irreversible action must not "
                                       "be a keystroke away")
                            .arg(action->shortcut().toString())));
}

void TestMainWindow::theUnreadLabelSaysWhichDirectionItWillGo()
{
    // The user's note: "the label for toggle unread should be dynamic. On an
    // unread message it should be Mark as read, on a read message Mark as
    // unread."
    //
    // "Toggle unread" reads the same whichever way it will go, so the only
    // way to learn what it does is to press it and look. The action stays a
    // toggle, because one message has a real two-valued state; what changes
    // is that the label tells the truth about the direction it has chosen.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"), {}) });

    view->setCurrentIndex(model->index(0, 0, {}));
    QVERIFY2(action->text().contains(QStringLiteral("read")),
             qPrintable(action->text()));
    QVERIFY2(!action->text().contains(QStringLiteral("unread")),
             qPrintable(QStringLiteral("an UNREAD row must offer Mark as "
                                       "read, not: %1").arg(action->text())));

    view->setCurrentIndex(model->index(1, 0, {}));
    QVERIFY2(action->text().contains(QStringLiteral("unread")),
             qPrintable(QStringLiteral("a READ row must offer Mark as unread, "
                                       "not: %1").arg(action->text())));
}

void TestMainWindow::theUnreadLabelFollowsAWriteWithoutReselecting()
{
    // The label describes the selection's STATE, and a write moves that state
    // without touching the selection. Marking the current row read has to
    // leave the entry offering "Mark as unread" on the same row, or the menu
    // offers to do again what was just done.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }) });
    view->setCurrentIndex(model->index(0, 0, {}));
    QVERIFY2(action->text().contains(QStringLiteral("read"))
                 && !action->text().contains(QStringLiteral("unread")),
             qPrintable(action->text()));

    action->trigger();

    QVERIFY2(action->text().contains(QStringLiteral("unread")),
             qPrintable(QStringLiteral("the label did not follow the write: "
                                       "still offering %1 on a row it just "
                                       "marked read").arg(action->text())));
}

void TestMainWindow::aMixedSelectionIsMarkedReadAndTheActionStaysVisible()
{
    // This test asserted the opposite until item 177. The note behind it said
    // "on a thread with mixed states it should be hidden, we have a submenu
    // for thread actions", and the second clause was the load-bearing one: the
    // submenu carried two absolute entries that worked whatever the mix, so
    // hiding the toggle cost the user nothing.
    //
    // Item 177 deleted that submenu, which took the route out with it. The
    // rule is a catch-all now: any unread row reads "mark read" and the entry
    // is never hidden, so one key still reaches both states in two presses.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(action);

    model->appendBatch({ makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") }),
                         makeThread(QStringLiteral("t2"), {}) });

    // From a row that is already current, and NOT via selectAll(): a fresh
    // selectAll emits no currentRowChanged at all and leaves the current
    // index invalid, so a test using it passes against a missing guard
    // (CLAUDE.md).
    view->setCurrentIndex(model->index(0, 0, {}));

    view->selectionModel()->select(
        model->index(1, 0, {}),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(view->selectionModel()->selectedRows().size(), 2);

    QVERIFY2(action->isVisible(),
             "the mixed selection hid the unread action, which now leaves no "
             "key at all: the submenu that used to be the way out is gone");
    QVERIFY2(action->text().contains(QStringLiteral("read"), Qt::CaseInsensitive)
                 && !action->text().contains(QStringLiteral("unread"),
                                             Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a mixed selection must promise the "
                                       "read direction, not: %1")
                            .arg(action->text())));

    // And the write goes the way the label promised. A direction computed from
    // "every row unread" would mark this selection UNREAD while the label said
    // read, which is the item 112 report from the other end.
    action->trigger();

    QVERIFY2(window.undoTextForTesting().contains(QStringLiteral("Mark read")),
             qPrintable(QStringLiteral("the write disagreed with the label on "
                                       "a mixed selection: %1")
                            .arg(window.undoTextForTesting())));
    QVERIFY2(!model->threadAt(0).isUnread() && !model->threadAt(1).isUnread(),
             "the mixed selection did not end up uniformly read, so a second "
             "press has no single state to toggle out of");

    // Which is what makes one key enough: the second press is the way back.
    QVERIFY2(action->text().contains(QStringLiteral("unread"),
                                     Qt::CaseInsensitive),
             qPrintable(QStringLiteral("the label did not follow the write, so "
                                       "the second press repeats the first: %1")
                            .arg(action->text())));
}

void TestMainWindow::aMixedThreadIsMarkedReadAndTheSecondPressIsTheWayBack()
{
    // What item 112 became under item 177. That item's report was real: on a
    // thread with two unread replies, asking to mark the whole thread unread
    // marked it READ, because ThreadSummary::tags is notmuch's UNION and a
    // thread holding even one unread message answers "unread". A union is not
    // a state, and a toggle needs a state.
    //
    // Its fix was two fixed-direction thread actions in a submenu. Item 177
    // deleted that submenu: the ROW decides the scope, so a second set of
    // actions was a second answer to a settled question.
    //
    // Nothing is lost with it, which is the point of this test. On a mixed
    // conversation the toggle goes ONE way, and that way is "mark read",
    // which is the direction that RESOLVES the mix: the thread lands in a
    // single state, and the second press toggles out of it. The reverse would
    // have left it mixed and the key still dead.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    // A query with no filter tag, so viewFilterTag() answers empty and the
    // membership sync (item 177, task 6) stays out of this. Under the default
    // startup query the first press correctly EVICTS the row it just marked
    // read, and the assertions below would then be reading a list that no
    // longer holds the thread. What is under test here is the toggle's
    // direction, which membership neither helps nor hinders.
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("thread:T1"));

    // MIXED: the union carries `unread` because some message is unread, while
    // others are not. A thread whose messages are all in one state answers
    // identically whichever way the direction is computed, so a uniform
    // fixture passes against the bug (CLAUDE.md, item 88's opposite-states
    // requirement).
    ThreadSummary mixed = makeThread(QStringLiteral("T1"),
                                     { QStringLiteral("unread") });
    mixed.totalCount = 3;
    model->appendBatch({ mixed });

    const QModelIndex thread = model->index(0, 0, {});
    QVERIFY(thread.isValid());
    QVERIFY2(model->isConversationRow(thread),
             "the fixture's row is not a conversation, so the toggle would "
             "read one message and this test would assert nothing about the "
             "union");
    QVERIFY2(model->threadFor(thread).isUnread(),
             "the fixture's union does not carry unread, so this test cannot "
             "reach the branch the defect lives in");

    view->setCurrentIndex(thread);
    view->selectionModel()->select(
        thread, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QApplication::processEvents();

    auto *toggle = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(toggle);

    // The LABEL is the promise, and it must name the thread and the direction
    // before the key is pressed. A label saying only "Mark as read" on a row
    // that is about to touch three messages is the ambiguity item 177 exists
    // to remove.
    QVERIFY2(toggle->text().contains(QStringLiteral("thread"),
                                     Qt::CaseInsensitive),
             qPrintable(QStringLiteral("the label does not name the thread: %1")
                            .arg(toggle->text())));
    QVERIFY2(toggle->text().contains(QStringLiteral("read"), Qt::CaseInsensitive)
                 && !toggle->text().contains(QStringLiteral("unread"),
                                             Qt::CaseInsensitive),
             qPrintable(QStringLiteral("the label does not promise the read "
                                       "direction: %1").arg(toggle->text())));

    toggle->trigger();

    QVERIFY2(window.undoTextForTesting().contains(QStringLiteral("Mark read")),
             qPrintable(QStringLiteral("the toggle went the other way on a "
                                       "mixed thread: %1")
                            .arg(window.undoTextForTesting())));

    // And it was THREAD-scoped, which is the whole of item 177: one keystroke
    // on a conversation row took the conversation, not the one message its
    // card shows.
    QVERIFY2(!model->threadAt(0).isUnread(),
             "the thread's own tags did not move, so the write was scoped to "
             "one message and the other two are still unread");

    // The route back is the same key, which is what makes one key enough. The
    // label has to follow the write for that to be true: a stale "Mark thread
    // as read" would make the second press repeat the first, and the user
    // would report the key as dead exactly as they did under item 112.
    QVERIFY2(toggle->isVisible(),
             "the toggle vanished after resolving the mix, so there is no way "
             "back to unread");
    QVERIFY2(toggle->text().contains(QStringLiteral("unread"),
                                     Qt::CaseInsensitive),
             qPrintable(QStringLiteral("the label did not follow the write: %1")
                            .arg(toggle->text())));

    toggle->trigger();

    QVERIFY2(model->threadAt(0).isUnread(),
             "the second press did not take the thread back to unread, so one "
             "key does not reach both states and the deleted submenu really "
             "was carrying something");
}

void TestMainWindow::toggleUnreadOnAReplyReadsTheReplysOwnState()
{
    // The user's report: "read/unread still doesn't trigger a repaint of the
    // reply". The write was already message-scoped and the model already
    // repaints a message row, so neither was the fault. The DIRECTION was:
    // the action read threadFor(current).isUnread(), the THREAD's state, even
    // when the selected row is a reply.
    //
    // The consequence is a dead key rather than a wrong write. On a read
    // thread the answer is always "add unread", so pressing it on an
    // already-unread reply re-adds a tag it has, which is a no-op the model
    // correctly declines to repaint. Item 88 fixed WHICH thread this reads;
    // this is about reading a message at all.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(action);

    // A thread that is READ carrying a reply that is UNREAD. That disagreement
    // is the whole test: with the thread's state the answer is "mark unread",
    // with the message's it is "mark read".
    const QModelIndex reply = expandSecondThreadAndSelectItsReply(
        view, model, {}, {}, QStringList{ QStringLiteral("unread") });
    QVERIFY2(reply.isValid(), "the fixture did not produce a reply row at row 0");
    QVERIFY(model->messageAt(reply).isUnread());
    QVERIFY2(!model->threadFor(reply).isUnread(),
             "the fixture's thread is unread too, so this test cannot tell the "
             "two sources apart");

    action->trigger();

    QVERIFY2(!model->messageAt(reply).isUnread(),
             "Toggle unread on an unread reply did not mark it read: the "
             "direction came from the THREAD, which is already read, so it "
             "re-added a tag the reply already had and nothing changed");
    QVERIFY2(window.undoTextForTesting().contains(QStringLiteral("Mark read")),
             qPrintable(QStringLiteral("wrong direction: %1")
                            .arg(window.undoTextForTesting())));
}

void TestMainWindow::toggleUnreadOnAReplyRepaintsItInBothDirections()
{
    // Visible BOTH ways. The user reached the repaint only by deleting and
    // undoing, which is a different write forcing the row to redraw; the
    // unread change itself has to do it on its own, in each direction.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(action);

    const QModelIndex reply = expandSecondThreadAndSelectItsReply(
        view, model, {}, {}, QStringList{ QStringLiteral("unread") });
    QVERIFY(reply.isValid());

    const QVariant unreadForeground = model->data(reply, Qt::ForegroundRole);
    const QVariant unreadFont = model->data(reply, Qt::FontRole);

    QSignalSpy spy(model, &QAbstractItemModel::dataChanged);
    action->trigger();

    QVERIFY2(spy.count() >= 1, "marking a reply read requested no repaint");
    QVERIFY2(model->data(reply, Qt::ForegroundRole) != unreadForeground,
             "a reply marked read paints exactly as it did while unread");
    QVERIFY2(model->data(reply, Qt::FontRole) != unreadFont,
             "a reply marked read keeps the unread font");

    // And back. A toggle that is only visible one way is half a toggle.
    spy.clear();
    action->trigger();
    QVERIFY(model->messageAt(reply).isUnread());
    QVERIFY2(spy.count() >= 1, "marking a reply unread again requested no repaint");
    QCOMPARE(model->data(reply, Qt::ForegroundRole), unreadForeground);
    QCOMPARE(model->data(reply, Qt::FontRole), unreadFont);
}

void TestMainWindow::taggingTheOpenReplyUpdatesTheMessagePaneStrip()
{
    // `spam`, not `delete`. Since item 103 Delete MOVES the file, so it needs
    // an account with a configured trash folder and a worker to do the move;
    // this bare window has neither, and Delete correctly refuses. What is
    // under test here is unchanged by that: `spam` is the other message-scoped
    // tag-only action, and it paints the same doomed state.
    // The user's report: "the right pane chips are not [repainted], for it to
    // sync I have to change message and go back to the edited one".
    //
    // sendThreadTagChange refreshes the strip when the edited thread is the
    // open one. sendMessageTagChange had no equivalent, so a message-scoped
    // write updated the list row and left the pane's chips describing the
    // message as it was before the edit.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    // The MESSAGE pane's strip by name: the pane holds two since item 177,
    // and an unqualified lookup can return the dashboard's instead.
    auto *strip =
        window.findChild<TagStrip *>(QStringLiteral("messageTagStrip"));
    QVERIFY2(strip, "no tag strip in the message pane");

    // visible + hidden: TagStrip collapses what does not fit into a "+N" chip,
    // and an unshown window has no width, so visibleTags() alone measures the
    // layout rather than the data.
    const auto stripTags = [strip]() {
        return strip->visibleTags() + strip->hiddenTags();
    };
    auto *action = window.findChild<QAction *>(QStringLiteral("spam"));
    QVERIFY(action);

    // A tag the strip will actually draw. Account tags are filtered out by the
    // strip, and `unread` and `inbox` are hidden on the card but not here, so
    // the fixture uses a plain functional tag to keep the assertion honest.
    const QModelIndex reply = expandSecondThreadAndSelectItsReply(
        view, model, {}, {}, QStringList{ QStringLiteral("todo") });
    QVERIFY2(reply.isValid(), "the fixture did not produce a reply row at row 0");

    // Selecting the reply is what puts it in the pane, and the strip has to be
    // showing the reply's own tags before the edit or this asserts nothing.
    QVERIFY2(stripTags().contains(QStringLiteral("todo")),
             "the strip does not show the selected reply's tags, so this test "
             "cannot tell a missing refresh from a strip that never had them");
    QVERIFY(!stripTags().contains(QStringLiteral("spam")));

    action->trigger();

    QVERIFY2(stripTags().contains(QStringLiteral("spam")),
             "the message pane's chips still describe the reply as it was "
             "before the edit; the user has to select away and back to see it");
}

void TestMainWindow::taggingAnUnrelatedReplyLeavesTheStripAlone()
{
    // The guard, not the refresh. The strip describes the message ON DISPLAY,
    // so a write to a different message must not repaint it with that
    // message's tags. The thread path has the same guard, keyed on
    // m_currentThreadId.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    // The MESSAGE pane's strip by name: the pane holds two since item 177,
    // and an unqualified lookup can return the dashboard's instead.
    auto *strip =
        window.findChild<TagStrip *>(QStringLiteral("messageTagStrip"));
    QVERIFY(strip);

    // visible + hidden: TagStrip collapses what does not fit into a "+N" chip,
    // and an unshown window has no width, so visibleTags() alone measures the
    // layout rather than the data.
    const auto stripTags = [strip]() {
        return strip->visibleTags() + strip->hiddenTags();
    };

    ThreadSummary thread = makeThread(QStringLiteral("t1"), {});
    thread.totalCount = 3;
    model->appendBatch({ thread });

    MessageNode root;
    root.messageId = QStringLiteral("m0@example.org");
    root.threadId = QStringLiteral("t1");
    root.depth = 0;
    MessageNode first;
    first.messageId = QStringLiteral("m1@example.org");
    first.threadId = QStringLiteral("t1");
    first.tags = QStringList{ QStringLiteral("todo") };
    first.depth = 1;
    MessageNode second;
    second.messageId = QStringLiteral("m2@example.org");
    second.threadId = QStringLiteral("t1");
    second.tags = QStringList{ QStringLiteral("later") };
    second.depth = 1;
    model->setThreadMessages(QStringLiteral("t1"), { root, first, second });

    const QModelIndex threadRow = model->index(0, 0, QModelIndex());
    view->expand(threadRow);

    // The FIRST reply is the one on display. Child 1, not child 0: since item
    // 177 a conversation lists its first message as a child too, so child 0 is
    // m0 (the card's own message) and the reply the strip describes is m1.
    const QModelIndex displayed = model->index(1, 0, threadRow);
    view->selectionModel()->select(
        displayed,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(displayed);
    QApplication::processEvents();
    QVERIFY(stripTags().contains(QStringLiteral("todo")));

    // A write to the OTHER reply, reaching the send path directly: driving it
    // through the action would move the selection and change what is on
    // display, which is the thing being held still.
    window.sendMessageTagChangeForTesting({ QStringLiteral("m2@example.org") },
                                          { QStringLiteral("deleted") }, {},
                                          QStringLiteral("Delete"));

    QVERIFY2(!stripTags().contains(QStringLiteral("deleted")),
             "the strip took on the tags of a message that is not the one in "
             "the pane");
    QVERIFY2(stripTags().contains(QStringLiteral("todo")),
             "the strip stopped describing the message on display");
}

void TestMainWindow::aHeldMessageEditIsSentWhenTheSyncEnds()
{
    // `spam`, not `delete`. Since item 103 Delete MOVES the file, so it needs
    // an account with a configured trash folder and a worker to do the move;
    // this bare window has neither, and Delete correctly refuses. What is
    // under test here is unchanged by that: `spam` is the other message-scoped
    // tag-only action, and it paints the same doomed state.
    // Found by reading while fixing the strip refresh, not reported.
    //
    // flushHeldEdits() looped over edit.threadIds and called
    // sendThreadTagChange() only. A message-scoped edit held during a sync
    // carries no thread ids at all, so the loop did nothing, the send
    // early-returned on an empty list, and the edit was DROPPED: applied
    // optimistically to the row, counted as unsynced, and never written. The
    // user would have seen the change, been told it was pending, and lost it.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *action = window.findChild<QAction *>(QStringLiteral("spam"));
    QVERIFY(action);

    const QModelIndex reply =
        expandSecondThreadAndSelectItsReply(view, model, {}, {});
    QVERIFY(reply.isValid());

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));
    action->trigger();
    QVERIFY2(window.hasEditAwaitingSend(),
             "a message edit made during a sync was not held");

    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Idle));

    QVERIFY2(!window.hasEditAwaitingSend(),
             "the sync ending did not send the held message edit, so it was "
             "silently dropped: shown on the row, counted as pending, never "
             "written");

    // Sent for the MESSAGE, not escalated to its thread. Losing the scope on
    // the way out of the hold would delete every message in the thread.
    QVERIFY2(window.pendingMessageIdsForTesting().contains(
                 QStringLiteral("m1@example.org")),
             "the held edit was not sent with its message scope");
    QVERIFY2(window.pendingThreadIdsForTesting().isEmpty(),
             "a held MESSAGE edit was sent as a thread edit, which would tag "
             "every message in the thread");

    // And the row still shows it: the flush takes the optimistic update back
    // before re-sending, so a bug there leaves the row wrong in the other
    // direction.
    QVERIFY2(model->messageAt(reply).isSpam(),
             "sending the held edit lost the tag from the reply's row");
}

void TestMainWindow::anActionOnAConversationRowTakesTheConversation()
{
    // The inversion item 177 is. Item 108 made a thread row act on the ONE
    // message its card displays, and this test asserted exactly that; the user
    // then reported it as the defect, because a card that stands above a
    // conversation and acts on one message of it is two things at once. A row
    // with replies is now the conversation, and a row without them is still
    // its message.
    //
    // `spam`, not `delete`. Since item 103 Delete MOVES the file, so it needs
    // an account with a configured trash folder and a worker to do the move;
    // this bare window has neither. `spam` is the other tag-only action and
    // resolves its scope through the same tagSelected().
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);

    // A conversation FIRST and a thread of one SECOND, so a wrong answer is
    // visible in both directions rather than accidentally right in one.
    ThreadSummary many = makeThread(QStringLiteral("t1"), {});
    many.totalCount = 7;
    ThreadSummary one = makeThread(QStringLiteral("t2"), {});
    one.totalCount = 1;
    model->appendBatch({ many, one });

    auto *spam = window.findChild<QAction *>(QStringLiteral("spam"));
    QVERIFY(spam);

    selectThreadRow(view, 0);
    QApplication::processEvents();
    spam->trigger();

    QCOMPARE(window.pendingThreadIdsForTesting(),
             QStringList{ QStringLiteral("t1") });
    QVERIFY2(window.pendingMessageIdsForTesting().isEmpty(),
             "a conversation row acted on one message, so six of the seven "
             "messages the card stands above were left untouched");

    // And the other half of the rule, which is what makes it a rule rather
    // than a blanket escalation: a thread of one is still its message.
    selectThreadRow(view, 1);
    QApplication::processEvents();
    spam->trigger();

    QCOMPARE(window.pendingMessageIdsForTesting(),
             QStringList{ QStringLiteral("t2-first@example.org") });
}

void TestMainWindow::selectingAConversationShowsTheDashboard()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(model && view && pane);

    ThreadSummary one = makeThread(QStringLiteral("t1"), {});
    one.totalCount = 1;
    ThreadSummary many = makeThread(QStringLiteral("t2"), {});
    many.totalCount = 4;
    model->appendBatch({ one, many });

    // Before the gesture, so the check after it means something: the pane
    // starts on the placeholder, not on a dashboard.
    QVERIFY2(!pane->showingDashboard(),
             "the pane was already showing a dashboard before anything was "
             "selected, so the assertion below would pass on nothing");

    selectThreadRow(view, 1);
    QApplication::processEvents();

    QVERIFY2(pane->showingDashboard(),
             "selecting a conversation rendered a message: the row stands for "
             "the thread and has no message to show");
}

void TestMainWindow::selectingALoneMessageShowsTheMessage()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(model && view && pane);

    // The conversation FIRST, so a wrong answer cannot be accidentally right:
    // a branch that showed the dashboard for every row would still be wrong
    // here, and one that never showed it would be wrong above.
    ThreadSummary many = makeThread(QStringLiteral("t1"), {});
    many.totalCount = 4;
    ThreadSummary one = makeThread(QStringLiteral("t2"), {});
    one.totalCount = 1;
    model->appendBatch({ many, one });

    selectThreadRow(view, 1);
    QApplication::processEvents();

    QVERIFY2(!pane->showingDashboard(),
             "a thread of one message showed a dashboard: it must open on one "
             "click, which is the case the whole split exists to protect");
}

void TestMainWindow::autoMarkReadTouchesOnlyTheMessageOnDisplay()
{
    // Item 87, reported 2026-08-14: "with the first message in a thread
    // selected (not expanded), the 2s delay that marks it read applies to the
    // whole thread, so all answers are marked read as well."
    //
    // Not cosmetic. maildir.synchronize_flags is on, so removing `unread`
    // rewrites Maildir filenames and the next sync carries it to the server:
    // mail the user never opened stops being unread everywhere, and nothing
    // here can put it back except reading it again by hand.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[general]\nmark_read_delay_ms = 0\n");
    file.close();

    Config config;
    config.load(path);
    QCOMPARE(config.markReadDelayMs(), 0);

    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);

    // totalCount = 1 since item 177, and the retarget is what KEEPS this test
    // able to fail. A thread row means the conversation now and displays no
    // message, so it arms no mark-read at all and there is no write left here
    // to be wrong about. A thread of one still renders its message, which is
    // where the automatic mark-read survives and where the scoping above is
    // still the property that matters.
    //
    // The stronger new rule, that a conversation row arms nothing whatever, is
    // asserted by selectingAConversationArmsNoMarkRead() below. Between them
    // the two cover every row an automatic mark-read can reach.
    ThreadSummary t = makeThread(QStringLiteral("t1"),
                                 { QStringLiteral("unread") });
    t.totalCount = 1;
    model->appendBatch({ t });

    selectThreadRow(view, 0);
    QApplication::processEvents();
    QVERIFY2(timer->isActive() || !window.pendingMessageIdsForTesting().isEmpty(),
             "selecting an unread thread armed no mark-read at all");

    // Fire it. A zero-interval timer still goes through the event loop.
    QTRY_VERIFY_WITH_TIMEOUT(!timer->isActive(), 2000);
    QApplication::processEvents();

    // ONE message, the one the card renders, and NAMED rather than merely
    // counted: a count of one can still be the wrong one, and the id is what
    // says the write landed on the message actually on display.
    QCOMPARE(window.pendingMessageIdsForTesting(),
             QStringList{ QStringLiteral("t1-first@example.org") });
    QVERIFY2(window.pendingThreadIdsForTesting().isEmpty(),
             "the automatic mark-read escalated to the whole thread, so mail "
             "the user never displayed was marked read and the next sync "
             "carries that to the server");

    // Still not on the undo stack. The user never took this action, so
    // hijacking Ctrl+Z to reverse it would undo something they did not do.
    QCOMPARE(window.undoDepthForTesting(), 0);
}

void TestMainWindow::selectingAConversationArmsNoMarkRead()
{
    // Item 177, and it REPLACES selectingARootCardKeepsItsThreadForMarkRead(),
    // which asserted the opposite and is retired. That test opened "A root card
    // is BOTH a message and a thread", which is exactly the identity item 177
    // splits: a row with replies is the conversation and displays no message,
    // so there is nothing on screen the user can be said to have read.
    // Retargeting it would have made it a test about something it was never
    // about; the history is recorded here so the next reader finds it.
    //
    // The data-safety half of item 87 is unchanged and still lives in
    // autoMarkReadTouchesOnlyTheMessageOnDisplay() above, on the row that does
    // display a message. This is the stronger half: arming NOTHING is safe by
    // construction, so the assertions below are about the absence of any write
    // rather than about its scope.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[general]\nmark_read_delay_ms = 0\n");
    file.close();

    Config config;
    config.load(path);
    QCOMPARE(config.markReadDelayMs(), 0);

    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);

    // A thread of ONE first and the conversation SECOND, in opposite states,
    // so a wrong answer is visible rather than accidentally right: code that
    // armed nothing anywhere would fail the lone-message test above, and code
    // that armed for everything fails here.
    ThreadSummary lone = makeThread(QStringLiteral("t1"),
                                    { QStringLiteral("unread") });
    lone.totalCount = 1;
    ThreadSummary conversation = makeThread(QStringLiteral("t2"),
                                            { QStringLiteral("unread") });
    conversation.totalCount = 7;
    model->appendBatch({ lone, conversation });

    // The guard: this fixture really does arm a mark-read for the row that
    // displays a message, so the absence asserted below is the conversation
    // row's doing and not a window that never arms anything.
    selectThreadRow(view, 0);
    QApplication::processEvents();
    QVERIFY2(timer->isActive() || !window.pendingMessageIdsForTesting().isEmpty(),
             "the lone-message row armed nothing either, so this test cannot "
             "tell a conversation row from a broken mark-read");

    QTRY_VERIFY_WITH_TIMEOUT(!timer->isActive(), 2000);
    QApplication::processEvents();

    // A fresh window, so the lone row's own write does not count towards the
    // assertions about the conversation.
    MainWindow second(config);
    auto *secondModel = second.findChild<ThreadListModel *>();
    QVERIFY(secondModel);
    auto *secondView = second.findChild<QTreeView *>();
    QVERIFY(secondView);
    auto *secondTimer =
        second.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(secondTimer);
    secondModel->appendBatch({ lone, conversation });

    selectThreadRow(secondView, 1);
    QApplication::processEvents();

    QVERIFY2(!secondTimer->isActive(),
             "a conversation row armed the mark-read timer, so a row that "
             "displays no message is about to mark one read");

    // Given time to fire, in case it was armed and stopped between the check
    // above and here. Nothing may arrive.
    QTest::qWait(50);
    QApplication::processEvents();

    QVERIFY2(second.pendingMessageIdsForTesting().isEmpty(),
             "a conversation row marked a message read: the row displays "
             "none, so the write named a message the user never saw");
    QVERIFY2(second.pendingThreadIdsForTesting().isEmpty(),
             "a conversation row marked the whole thread read, which is "
             "every message in it and none of them displayed");
    QCOMPARE(second.undoDepthForTesting(), 0);
}

void TestMainWindow::autoMarkReadArmsForAReplyToo()
{
    // Selecting a reply displays that message, so the same rule applies to it.
    // Before item 87 the timer was deliberately not armed for a message row,
    // because the write it would have made was thread-scoped and would have
    // marked the whole conversation read. With the write scoped to one message
    // that objection is gone, and leaving it unarmed would mean the message
    // the user is reading is the one kind that never gets marked read.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[general]\nmark_read_delay_ms = 0\n");
    file.close();

    Config config;
    config.load(path);

    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *timer = window.findChild<QTimer *>(QStringLiteral("markReadTimer"));
    QVERIFY(timer);

    // The reply is unread; its thread is not, so a thread-keyed timer would
    // have declined to arm at all.
    //
    // No "is it still unread" guard before the wait: the delay is 0 and the
    // helper pumps the event loop, so the write has already happened by the
    // time selection returns. The assertions below are on the write itself,
    // which is what this test is about, and the fixture above is what
    // establishes the reply started unread.
    const QModelIndex reply = expandSecondThreadAndSelectItsReply(
        view, model, {}, {}, QStringList{ QStringLiteral("unread") });
    QVERIFY(reply.isValid());

    QTRY_VERIFY_WITH_TIMEOUT(!timer->isActive(), 2000);
    QApplication::processEvents();

    QCOMPARE(window.pendingMessageIdsForTesting(),
             QStringList{ QStringLiteral("m1@example.org") });
    QVERIFY2(window.pendingThreadIdsForTesting().isEmpty(),
             "reading one reply marked its whole thread read");

    // And the row shows it, which is the half item 105 built.
    QVERIFY2(!model->messageAt(reply).isUnread(),
             "the reply was marked read without its row following");
}

void TestMainWindow::taggingTheOpenRootMessageKeepsTheStripPopulated()
{
    // `spam`, not `delete`. Since item 103 Delete MOVES the file, so it needs
    // an account with a configured trash folder and a worker to do the move;
    // this bare window has neither, and Delete correctly refuses. What is
    // under test here is unchanged by that: `spam` is the other message-scoped
    // tag-only action, and it paints the same doomed state.
    // The user, 2026-08-16: "right pane loses the chip row when repainting, it
    // simply disappears".
    //
    // The strip refresh added for item 105 reads the message's tags through
    // messageById(), which searches only the loaded CHILDREN. A root card's
    // message is never among them, so the lookup returned a default-constructed
    // node and the refresh set the strip to that node's empty tag list, wiping
    // a strip that had been correct a moment earlier. Worse than not
    // refreshing: it actively destroyed what was there.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    // The MESSAGE pane's strip by name: the pane holds two since item 177,
    // and an unqualified lookup can return the dashboard's instead.
    auto *strip =
        window.findChild<TagStrip *>(QStringLiteral("messageTagStrip"));
    QVERIFY(strip);

    ThreadSummary t = makeThread(QStringLiteral("t1"),
                                 { QStringLiteral("todo") });
    t.totalCount = 1;
    model->appendBatch({ t });

    selectThreadRow(view, 0);
    QApplication::processEvents();

    // visible + hidden, not visible alone. TagStrip is a single row that
    // collapses whatever does not fit into a trailing "+N" chip, and this
    // window is never shown, so it has no width to lay out with and puts
    // almost everything in the hidden half. Asserting on visibleTags() alone
    // measures the layout, not the data, and fails for a reason this test does
    // not care about.
    const auto stripTags = [strip]() {
        return strip->visibleTags() + strip->hiddenTags();
    };

    // The guard: the strip has to be showing something before the edit, or
    // this cannot tell "wiped" from "never populated".
    QVERIFY2(stripTags().contains(QStringLiteral("todo")),
             "the strip never showed the selected thread's tags");

    auto *action = window.findChild<QAction *>(QStringLiteral("spam"));
    QVERIFY(action);
    action->trigger();

    QVERIFY2(!stripTags().isEmpty(),
             "the chip row was emptied: the refresh looked the message up "
             "among the loaded replies, where a root card's message never is, "
             "and set the strip to the resulting empty tag list");
    QVERIFY2(stripTags().contains(QStringLiteral("todo")),
             "the strip lost the tag the message still carries");
    QVERIFY2(stripTags().contains(QStringLiteral("spam")),
             "the strip did not pick up the tag just written");
}

void TestMainWindow::aLoadedMessageCorrectsTheStripFromTheThreadsUnion()
{
    // Reported by hand, 2026-08-16, against a real four-message thread whose
    // root carried `unread` and whose THIRD message carried `signed`:
    // "the right pane chips update and both signed and unread disappear ...
    // changing message and going back makes them reappear".
    //
    // Neither half was the write's doing. Selecting a thread row sets the strip
    // from ThreadSummary::tags, which is notmuch's UNION over the thread, so
    // the pane claimed the root message was `signed` when a sibling was. The
    // mark-read write then replaced it with the root's real tags, correctly
    // dropping both, and reselecting put the union back. The pane was lying
    // BEFORE the write, not after it.
    //
    // The load is the authority: MessageRef carries the message's own tags.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    // The MESSAGE pane's strip by name: the pane holds two since item 177,
    // and an unqualified lookup can return the dashboard's instead.
    auto *strip =
        window.findChild<TagStrip *>(QStringLiteral("messageTagStrip"));
    QVERIFY(strip);

    const auto stripTags = [strip]() {
        return strip->visibleTags() + strip->hiddenTags();
    };

    // The union carries `signed`; the root message does not.
    //
    // totalCount = 1 since item 177, which is a retarget rather than a
    // weakening. This test is about the message pane's strip being corrected
    // from a LOADED message, and a conversation row displays no message at all
    // now: it shows the dashboard, whose strip is the thread's union on
    // purpose. A thread of one still renders its message, so the union and the
    // message's own tags can still disagree and the correction still has to
    // happen. The tags are unchanged, so the disagreement the test turns on is
    // the same one the user reported.
    ThreadSummary t = makeThread(QStringLiteral("t1"),
                                 { QStringLiteral("inbox"),
                                   QStringLiteral("signed"),
                                   QStringLiteral("unread") });
    t.totalCount = 1;
    model->appendBatch({ t });

    selectThreadRow(view, 0);
    QApplication::processEvents();

    // Before the load the strip can only show the union, which is what the
    // model holds. That is the state the user saw and reported.
    QVERIFY(stripTags().contains(QStringLiteral("signed")));

    // The worker answers with the message's OWN tags.
    MessageRef ref;
    ref.messageId = QStringLiteral("t1-first@example.org");
    ref.tags = QStringList{ QStringLiteral("inbox"), QStringLiteral("unread") };
    QMetaObject::invokeMethod(
        &window, "onMessageLoaded", Qt::DirectConnection,
        Q_ARG(QVector<MessageRef>, QVector<MessageRef>{ ref }),
        Q_ARG(quint64, window.currentGenerationForTesting()));
    QApplication::processEvents();

    QVERIFY2(!stripTags().contains(QStringLiteral("signed")),
             "the pane still claims the root message is signed, which is a "
             "sibling's tag: it is showing the thread's union rather than the "
             "message on display");
    QVERIFY2(stripTags().contains(QStringLiteral("unread")),
             "the pane lost a tag the message really carries");
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

void TestMainWindow::everyPendingChangeCanNameItsMessages()
{
    // Item 119. The count summed a fourth term, a bare int for confirmed
    // changes carrying no message ids, and that term is what made the count
    // impossible to open and list: three groups could name what they held and
    // the remainder could not.
    //
    // The remainder was empty. NotmuchWorker::applyTags() is the only emitter
    // of tagsApplied() and returns early on an empty id list, which is the
    // exact condition the counter required, so the change it existed for
    // cannot reach this window. Measured before removing it: a qFatal in the
    // branch fired in 4 of 70 cases here, all four invoking the slot directly
    // with a hand-built TagChange, and an assertion before the worker's emit
    // never fired across the whole suite.
    //
    // The guard itself is pinned in test_notmuchworker by
    // applyTagsWithNoIdsDoesNothing(), which is where it lives. What this test
    // holds is the consequence: a change that reaches the indicator names its
    // messages, so the indicator can be listed in full.
    const Config config;
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);
    QVERIFY(label->isHidden());

    // A change with ids counts, and the count is the number of (message, tag)
    // pairs it carries rather than one per signal.
    TagChange change;
    change.messageIds = { QStringLiteral("a@example.org"),
                          QStringLiteral("b@example.org") };
    change.added = { QStringLiteral("flagged") };
    change.description = QStringLiteral("Mark important");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));
    QVERIFY(!label->isHidden());

    // And its inverse nets it back to nothing, which is the property the
    // fourth term could never have: an unnettable count only ever grew.
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change.inverted())));
    QVERIFY2(label->isHidden(),
             "an edit and its inverse left the indicator claiming work");
}

void TestMainWindow::theSnapshotGroupsActionsUnderTheirMessage()
{
    // The layout the user asked for: a message appears ONCE with its actions
    // beneath it. That is a property of the row ORDER, so it is asserted on
    // the snapshot rather than on a rendered dialog.
    const Config config;
    MainWindow window(config);

    // Two actions on one message, one on another, interleaved so a snapshot
    // that simply reported insertion order would fail.
    const auto apply = [&window](const QString &id, const QString &tag,
                                 const QString &description) {
        TagChange change;
        change.messageIds = { id };
        change.added = { tag };
        change.description = description;
        QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                          Q_ARG(TagChange, change)));
    };
    apply(QStringLiteral("b@example.org"), QStringLiteral("flagged"),
          QStringLiteral("Mark important"));
    apply(QStringLiteral("a@example.org"), QStringLiteral("deleted"),
          QStringLiteral("Delete"));
    apply(QStringLiteral("b@example.org"), QStringLiteral("spam"),
          QStringLiteral("Mark spam"));

    const QVector<PendingChange> rows = window.pendingChangeSnapshot();
    QCOMPARE(rows.size(), 3);

    // One message per contiguous run: b's two actions are adjacent, so the
    // dialog can draw the subject once and the actions under it.
    QCOMPARE(rows.at(0).id, QStringLiteral("a@example.org"));
    QCOMPARE(rows.at(1).id, QStringLiteral("b@example.org"));
    QCOMPARE(rows.at(2).id, QStringLiteral("b@example.org"));

    // Each row says what the user did, in the words the action itself used.
    QCOMPARE(rows.at(0).action, QStringLiteral("Delete"));
    QVERIFY(rows.at(1).action != rows.at(2).action);

    // And every row here is message-scoped: none of these was a thread action.
    for (const PendingChange &row : rows)
        QVERIFY(!row.isThread);
}

void TestMainWindow::theSnapshotKeepsAThreadActionThreadScoped()
{
    // Scope follows the ACTION, not the storage. A held thread edit stays one
    // thread row: reporting its messages instead would claim the user acted on
    // each one, and the count they clicked would disagree with the list.
    //
    // Driven through the held queue because that is the only thing that
    // carries thread ids; a confirmed edit is message-scoped by construction.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(model && view);
    // A CONVERSATION row, which is what makes `flag` thread-scoped since item
    // 177. A thread of one would produce a message edit and this test would
    // assert nothing about thread ids.
    ThreadSummary many = makeThread(QStringLiteral("t1"), {});
    many.totalCount = 3;
    model->appendBatch({ many });
    selectThreadRow(view, 0);
    QApplication::processEvents();

    // A cron sync takes the lock, which is what makes the edit HELD rather
    // than sent, and a held edit is the only thing that carries thread ids.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));

    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);
    action->trigger();
    QVERIFY(window.hasEditAwaitingSend());

    const QVector<PendingChange> rows = window.pendingChangeSnapshot();
    QCOMPARE(rows.size(), 1);
    QVERIFY2(rows.at(0).isThread,
             "a thread action was reported as a message change");
    QCOMPARE(rows.at(0).id, QStringLiteral("t1"));

    // The count and the list agree, which is the property the whole dialog
    // rests on.
    QCOMPARE(rows.size(), window.pendingEditCount());
}

void TestMainWindow::theIndicatorOpensItsListOnAClick()
{
    // The label is a QLabel and has no clicked signal, so the click is taken
    // by an event filter. A test that called showPendingChanges() directly
    // would pass with that filter never installed, which is the whole gesture.
    const Config config;
    MainWindow window(config);

    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(label);

    TagChange change;
    change.messageIds = { QStringLiteral("click@example.org") };
    change.added = { QStringLiteral("deleted") };
    change.description = QStringLiteral("Delete");
    QVERIFY(QMetaObject::invokeMethod(&window, "onTagsApplied",
                                      Q_ARG(TagChange, change)));
    QVERIFY(!label->isHidden());

    // With no worker the dialog opens directly and modally, so it is closed
    // from a timer rather than by driving exec() to return some other way.
    // Polled rather than checked once: exec() parents the dialog and spins its
    // own event loop, so a single-shot timer can fire before it exists.
    bool sawDialog = false;
    auto *poll = new QTimer(&window);
    poll->setInterval(1);
    QObject::connect(poll, &QTimer::timeout, &window, [&window, &sawDialog]() {
        if (auto *dialog = window.findChild<PendingChangesDialog *>()) {
            sawDialog = true;
            QCOMPARE(dialog->rows().size(), 1);
            QCOMPARE(dialog->rows().at(0).action, QStringLiteral("Delete"));
            dialog->reject();
        }
    });
    poll->start();

    QMouseEvent press(QEvent::MouseButtonRelease, QPointF(1, 1),
                      QPointF(1, 1), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(label, &press);

    // The subjects are resolved on the worker thread, so the dialog appears a
    // round trip after the click rather than inside sendEvent().
    QTRY_VERIFY_WITH_TIMEOUT(sawDialog, 15000);
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
    // A CONVERSATION row, which is what makes `flag` thread-scoped since item
    // 177. This test asserts on the thread ROW, which a message-scoped write
    // deliberately leaves alone. What is under test is the HOLD, which is
    // identical either way.
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY2(action, "no flag action registered");

    ThreadSummary many = makeThread(QStringLiteral("t1"), {});
    many.totalCount = 3;
    model->appendBatch({ many });
    selectThreadRow(view, 0);
    QApplication::processEvents();

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
    // A CONVERSATION row, which is what makes `flag` thread-scoped since item
    // 177. This test asserts on the thread ROW, which a message-scoped write
    // deliberately leaves alone. What is under test is the HOLD, which is
    // identical either way.
    auto *action = window.findChild<QAction *>(QStringLiteral("flag"));
    QVERIFY(action);

    ThreadSummary many = makeThread(QStringLiteral("t1"), {});
    many.totalCount = 3;
    model->appendBatch({ many });
    selectThreadRow(view, 0);
    QApplication::processEvents();

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

void TestMainWindow::everyActionIsReachableFromAMenu()
{
    // The fourth registration site nothing enforced. CLAUDE.md says adding an
    // action is four places: knownActions(), defaultBindings(), the icon table
    // and the action itself. It is FIVE, and the fifth is a menu.
    //
    // Found the hard way on the trash branch: `restore` shipped keyboard-only,
    // reachable by a chord and by nothing a user could see or discover, and no
    // test noticed. The three existing coverage tests each assert a different
    // property and all three pass against an action that appears nowhere in
    // the interface.
    //
    // The MENU rather than the toolbar, since the toolbar is a small
    // deliberate subset and always will be. Every menu is walked, submenus
    // included, because the five whole-thread actions live only in the "Whole
    // thread" submenu.
    const Config config;
    MainWindow window(config);

    auto *bar = window.menuBar();
    QVERIFY(bar);

    QSet<QAction *> reachable;
    QList<QMenu *> pending;
    const auto topLevel = bar->actions();
    for (QAction *action : topLevel) {
        if (action->menu())
            pending.append(action->menu());
    }
    QVERIFY2(!pending.isEmpty(), "the menu bar holds no menus");

    while (!pending.isEmpty()) {
        QMenu *menu = pending.takeFirst();
        const auto entries = menu->actions();
        for (QAction *entry : entries) {
            if (QMenu *sub = entry->menu()) {
                pending.append(sub);
                // An action owning a menu emits no `triggered`, so it is the
                // submenu that makes its children reachable and never the
                // parent entry itself. Not counted as reachable.
                continue;
            }
            reachable.insert(entry);
        }
    }

    // The guard, before anything is asserted about what is missing: a walk
    // that found nothing would report every action as unreachable and read as
    // a catastrophic regression rather than as a broken probe.
    QVERIFY2(reachable.size() > 10,
             qPrintable(QStringLiteral("the menu walk found only %1 entries")
                            .arg(reachable.size())));

    QStringList unreachable;
    for (const QString &name : KeyMap::knownActions()) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action named %1").arg(name)));
        if (!reachable.contains(action))
            unreachable.append(name);
    }

    QVERIFY2(unreachable.isEmpty(),
             qPrintable(QStringLiteral("%1 action(s) reach no menu, so they "
                                       "exist only for whoever already knows "
                                       "the chord: %2")
                            .arg(unreachable.size())
                            .arg(unreachable.join(QStringLiteral(", ")))));
}

void TestMainWindow::noMenuHasTwoEntriesSharingAMnemonic()
{
    // The sibling of everyActionIsReachableFromAMenu(), and it exists because
    // the rule it enforces had lived only in prose and in one other test's
    // COMMENT, and was duly broken the first time a batch of entries was added
    // to a menu (item 123: `&Reply` against the pre-existing `&Restore from
    // trash`, both Alt+R).
    //
    // Qt does not error on a duplicate mnemonic. It CYCLES between the
    // colliding entries instead of activating either, so the key silently
    // stops working and merely moves a highlight. That is worse than it
    // sounds in the Message menu, where `restore` is deliberately greyed
    // outside the trash view: the ordinary case was pressing Alt+R and landing
    // on a disabled entry.
    //
    // Item 57 already decided this is a property rather than a taste. It
    // rejected the label "Starred" for `flag` precisely because it would have
    // collided with `Mark &spam`, and theImportantActionIsLabelledImportant()
    // pins the surviving label with that reasoning in its comment. A decision
    // recorded only in prose is one nobody re-derives.
    //
    // Scoped PER MENU, which is what the collision actually is: a mnemonic is
    // resolved among the entries of the menu that is open, so the same letter
    // in File and in View is not a conflict.
    const Config config;
    MainWindow window(config);

    auto *bar = window.menuBar();
    QVERIFY(bar);

    // The menu bar's own top-level titles are one such scope too, so the walk
    // starts by treating the bar as a menu and then descends.
    QList<QPair<QString, QList<QAction *>>> scopes;
    scopes.append({ QStringLiteral("the menu bar"), bar->actions() });

    QList<QMenu *> pending;
    const auto topLevel = bar->actions();
    for (QAction *action : topLevel) {
        if (action->menu())
            pending.append(action->menu());
    }
    QVERIFY2(!pending.isEmpty(), "the menu bar holds no menus");

    while (!pending.isEmpty()) {
        QMenu *menu = pending.takeFirst();
        const auto entries = menu->actions();
        scopes.append({ menu->title(), entries });
        for (QAction *entry : entries) {
            if (QMenu *sub = entry->menu())
                pending.append(sub);
        }
    }

    // The four collisions that PREDATE this test, measured by running it
    // against the tree before item 123 touched any label. They are listed
    // rather than fixed, and rather than being hidden by narrowing the test,
    // because renaming a shipped menu entry is the user's call and not a
    // test's: three of them are in menus a user has had in their fingers
    // since 0.1.0.
    //
    // Listed as exact pairs, not as "ignore Alt+R", so this is a freeze and
    // not an amnesty: a NEW entry colliding on any of these same keys still
    // fails, because its pair is not on this list. Fixing one is then a
    // one-line deletion here, which is the point of writing them out.
    // Written as the FULL GROUP of labels sharing one key in one menu, not as
    // a pair. A pair is keyed on which entry the walk happened to see first,
    // so adding a colliding entry ABOVE a frozen one silently re-pairs it and
    // the new defect gets reported as "a frozen collision no longer happens",
    // which names the wrong thing entirely. Measured: reinstating `&Reply`
    // did exactly that before this was changed. A group is order-independent,
    // so a new entry grows the group and fails as a new collision.
    static const QStringList knownPreExistingCollisions = {
        QStringLiteral("&Message: Alt+R shared by \"&Restore from trash\", \"Mark all &read\", \"Tagging &rules...\""),
        QStringLiteral("&Message: Alt+S shared by \"Mark &spam\", \"Find &stranded deleted mail\""),
        QStringLiteral("&View: Alt+O shared by \"&Open thread\", \"Zoom &out\""),
    };

    QStringList collisions;
    int compared = 0;

    for (const auto &scope : scopes) {
        // Keyed on the mnemonic Qt itself derives, not on a hand-parsed '&'.
        // The question is which key Qt will dispatch, and only Qt answers it:
        // "&&" is a literal ampersand and carries no mnemonic at all.
        //
        // A QMap rather than a QHash so the groups come out in a stable key
        // order, which is what lets the frozen list above be written once and
        // stay matching.
        QMap<QString, QStringList> byMnemonic;
        for (QAction *entry : scope.second) {
            if (entry->isSeparator())
                continue;
            const QKeySequence mnemonic = QKeySequence::mnemonic(entry->text());
            if (mnemonic.isEmpty())
                continue;
            ++compared;
            byMnemonic[mnemonic.toString(QKeySequence::NativeText)]
                .append(QStringLiteral("\"%1\"").arg(entry->text()));
        }

        for (auto it = byMnemonic.cbegin(); it != byMnemonic.cend(); ++it) {
            if (it.value().size() < 2)
                continue;
            // Names the menu, the key and EVERY label in the group, so a
            // future failure says what to rename without anyone going looking.
            collisions.append(QStringLiteral("%1: %2 shared by %3")
                                  .arg(scope.first, it.key(),
                                       it.value().join(QStringLiteral(", "))));
        }
    }

    // The guard, and it is not ceremonial: every assertion below is a loop
    // that reports success when it runs zero times. A walk that found no
    // mnemonics at all would pass this test against any label whatsoever.
    QVERIFY2(compared > 20,
             qPrintable(QStringLiteral("only %1 menu entries carried a "
                                       "mnemonic, so this probe measured "
                                       "almost nothing")
                            .arg(compared)));

    // Matched on the menu and key only, with the labels compared separately
    // below. Comparing whole strings made a GROWING group read as a frozen one
    // disappearing: adding `&Reply` took Alt+R from three labels to four, the
    // frozen three-label string stopped matching, and the failure said "this
    // collision no longer happens" about the very key that had just got worse.
    // Measured twice, once per attempt, which is why the two questions are
    // asked separately.
    const auto scopeAndKey = [](const QString &collision) {
        return collision.left(collision.indexOf(QStringLiteral(" shared by ")));
    };

    QHash<QString, QString> frozen;
    for (const QString &known : knownPreExistingCollisions)
        frozen.insert(scopeAndKey(known), known);

    QStringList unexpected;
    QSet<QString> stillPresent;
    for (const QString &collision : collisions) {
        const QString key = scopeAndKey(collision);
        const auto known = frozen.constFind(key);
        if (known == frozen.constEnd()) {
            // A collision on a key nothing froze: entirely new.
            unexpected.append(collision);
            continue;
        }
        stillPresent.insert(key);
        if (*known != collision) {
            // The key was already colliding, but the CAST has changed, which
            // for a frozen entry means an entry joined it. Reported as the
            // new collision it is, naming both what was frozen and what is
            // there now.
            unexpected.append(
                QStringLiteral("%1 (frozen as [%2], now [%3])")
                    .arg(key, *known, collision));
        }
    }

    // A frozen entry that has since been FIXED must not stay on the list
    // silently, or the list becomes a place stale claims accumulate.
    QStringList stale;
    for (const QString &known : knownPreExistingCollisions) {
        if (!stillPresent.contains(scopeAndKey(known)))
            stale.append(known);
    }
    QVERIFY2(stale.isEmpty(),
             qPrintable(QStringLiteral("%1 frozen collision(s) no longer "
                                       "happen, so delete them from "
                                       "knownPreExistingCollisions: %2")
                            .arg(stale.size())
                            .arg(stale.join(QStringLiteral("; ")))));

    QVERIFY2(unexpected.isEmpty(),
             qPrintable(QStringLiteral("%1 menu mnemonic collision(s), where "
                                       "Qt cycles the highlight instead of "
                                       "activating: %2")
                            .arg(unexpected.size())
                            .arg(unexpected.join(QStringLiteral("; ")))));
}

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

    // Asserted on the CHANGE that was sent rather than on the thread's row.
    // Since item 108 this action is message-scoped, so it writes to the
    // message the card displays and the thread summary is deliberately left
    // alone. The tag name is what this test is about, and the change carries
    // it whichever scope the action uses.
    const TagChange sent = window.pendingChangeForTesting();
    QVERIFY2(sent.added.contains(QStringLiteral("flagged")),
             "the renamed action no longer writes the `flagged` tag");
    QVERIFY2(!sent.added.contains(QStringLiteral("important")),
             "the rename reached the mail store: an `important` tag was "
             "written, which no other tool reading this Maildir knows");
    QVERIFY2(sent.removed.isEmpty(),
             "marking important removed a tag, which it must not");
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
                        const QList<QPair<QString, QString>> &accounts,
                        const QString &startupQuery = QString())
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QSettings s(path, QSettings::IniFormat);
    // [general] keys are read WITHOUT the prefix: QSettings' INI backend treats
    // a section literally named [general] as its own fallback section and
    // strips it, so setValue("general/startup_query") would write a key nothing
    // reads.
    if (!startupQuery.isEmpty())
        s.setValue(QStringLiteral("startup_query"), startupQuery);
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

void TestMainWindow::theMessagePaneCarriesItsOwnActionBar()
{
    // Items 139, 140 and 141. The main toolbar had grown to mix two scopes:
    // Sync, Archive, Delete, Mark all read and Undo act on the LIST, while
    // Compose, Reply and Forward are about a message. Forward was on no
    // toolbar at all and reachable only from the Message menu, which is
    // item 139.
    const Config config;
    MainWindow window(config);

    auto *bar = window.findChild<QToolBar *>(QStringLiteral("message_toolbar"));
    QVERIFY2(bar, "the message pane has no action bar");

    // Inside the message pane, not merely somewhere in the window: the point
    // of the item is WHERE it sits.
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);
    QVERIFY2(pane->isAncestorOf(bar),
             "the message bar is not inside the message pane");

    // The three message actions, in the user's order, and the same QAction
    // objects the menus use rather than copies: a second QAction would need
    // its own enablement and would drift from the menu entry.
    // Compose is deliberately NOT here: it needs no message, so it stays on
    // the main toolbar with the window-wide actions. The user reconsidered
    // this after seeing the first version, and the split is now by what the
    // action needs rather than by what it is about.
    const QStringList expected = { QStringLiteral("reply"),
                                   QStringLiteral("forward") };
    for (const QString &name : expected) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action %1").arg(name)));
        QVERIFY2(bar->actions().contains(action),
                 qPrintable(QStringLiteral("%1 is not on the message bar")
                                .arg(name)));
    }

    auto *compose = window.findChild<QAction *>(QStringLiteral("compose"));
    QVERIFY(compose);
    QVERIFY2(!bar->actions().contains(compose),
             "compose is on the message bar, where it needs no message");

    // toggle_html is the view control the user named for this bar. It is a
    // different scope from the three above ("change how I am looking at it",
    // not "act on this"), so it sits apart from them, after a stretch.
    auto *toggleHtml =
        window.findChild<QAction *>(QStringLiteral("toggle_html"));
    QVERIFY(toggleHtml);
    QVERIFY2(bar->actions().contains(toggleHtml),
             "toggle_html is not on the message bar");

    const QList<QAction *> actions = bar->actions();
    const int lastMessageAction =
        actions.indexOf(window.findChild<QAction *>(QStringLiteral("forward")));
    const int htmlIndex = actions.indexOf(toggleHtml);
    QVERIFY2(lastMessageAction >= 0 && htmlIndex > lastMessageAction,
             "toggle_html does not sit after the three message actions");

    // Order alone is not the property: the two groups must be SEPARATED, which
    // is an expanding spacer between them, and a test asserting only on the
    // index passes with the spacer deleted (measured). Find the widget the
    // toolbar made for it and check it expands and sits between the groups.
    int spacerIndex = -1;
    for (int i = 0; i < actions.size(); ++i) {
        auto *widget = bar->widgetForAction(actions.at(i));
        if (widget
            && widget->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding) {
            spacerIndex = i;
            break;
        }
    }
    QVERIFY2(spacerIndex > lastMessageAction && spacerIndex < htmlIndex,
             "no expanding spacer separates the message actions from the view "
             "controls, so they read as one group");
}

void TestMainWindow::theMessageBarSitsAboveTheBodyAndBelowTheHeader()
{
    // The user's correction after seeing the first version: the bar belongs
    // immediately above the message it acts on, under the subject and details
    // rows, rather than at the very top of the pane where it read as part of
    // the window chrome.
    const Config config;
    MainWindow window(config);

    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);
    auto *bar = window.findChild<QToolBar *>(QStringLiteral("message_toolbar"));
    auto *header = pane->findChild<QLabel *>(QStringLiteral("messageHeader"));
    QVERIFY(bar);

    // The message PAGE's column, not the pane's. Since item 177 the pane is a
    // stack of two faces, the message and the conversation dashboard, so the
    // pane's own layout holds one item and the ordering this test is about
    // lives one level in.
    auto *page = pane->findChild<QWidget *>(QStringLiteral("messagePage"));
    QVERIFY2(page, "the message pane has no message page to order");
    auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
    QVERIFY2(layout, "the message page is not laid out vertically");

    // Index in the pane's own column, which is what "above" and "below" mean
    // here. Asserting on geometry instead would measure the offscreen
    // platform's idea of an unshown widget, which is nothing.
    int barIndex = -1;
    int viewIndex = -1;
    int headerIndex = -1;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (item->widget() == bar)
            barIndex = i;
        else if (item->widget()
                 && item->widget()->inherits("QWebEngineView"))
            viewIndex = i;
        else if (header && item->layout()
                 && item->layout()->indexOf(header) >= 0)
            headerIndex = i;
    }

    QVERIFY2(barIndex >= 0 && viewIndex >= 0,
             "the bar or the web view is not in the pane's column");
    QVERIFY2(barIndex < viewIndex,
             "the message bar sits below the message body");
    if (headerIndex >= 0) {
        QVERIFY2(headerIndex < barIndex,
                 "the message bar sits above the header row, where it reads as "
                 "window chrome rather than as belonging to the message");
    }
}

void TestMainWindow::theMessageBarIconsAreSmallerThanTheToolbars()
{
    // The bar is subordinate to the main toolbar, so its icons are smaller.
    // Derived from the configured size rather than hardcoded, so the relation
    // survives the user changing toolbar_icon_size.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[general]\ntoolbar_icon_size = 32\n");
    file.close();

    Config config;
    config.load(path);
    QCOMPARE(config.toolbarIconSize(), 32);

    MainWindow window(config);
    auto *toolBar =
        window.findChild<QToolBar *>(QStringLiteral("main_toolbar"));
    auto *bar = window.findChild<QToolBar *>(QStringLiteral("message_toolbar"));
    QVERIFY(toolBar && bar);

    QCOMPARE(toolBar->iconSize(), QSize(32, 32));
    QCOMPARE(bar->iconSize(), QSize(28, 28));

    // The RELATION, not the constant: a fixed 28 would satisfy the line above
    // and stop meaning anything the moment the user set a different size.
    QVERIFY2(bar->iconSize().width() < toolBar->iconSize().width(),
             "the message bar's icons are not smaller than the toolbar's");
}

void TestMainWindow::theMainToolbarKeepsOnlyListWideActions()
{
    // The other half of item 140: the actions do not merely gain a second
    // home, they LEAVE the main toolbar, which is what makes its remaining
    // contents mean one thing.
    const Config config;
    MainWindow window(config);

    auto *toolBar =
        window.findChild<QToolBar *>(QStringLiteral("main_toolbar"));
    QVERIFY(toolBar);

    for (const QString &name : { QStringLiteral("reply"),
                                 QStringLiteral("forward") }) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action %1").arg(name)));
        QVERIFY2(!toolBar->actions().contains(action),
                 qPrintable(QStringLiteral("%1 is still on the main toolbar")
                                .arg(name)));
    }

    // The guard: without it, a change emptying the toolbar entirely would pass
    // every assertion above while deleting the feature.
    for (const QString &name : { QStringLiteral("compose"),
                                 QStringLiteral("sync"),
                                 QStringLiteral("archive"),
                                 QStringLiteral("undo") }) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action && toolBar->actions().contains(action),
                 qPrintable(QStringLiteral("%1 left the main toolbar, which "
                                           "should keep the list-wide actions")
                                .arg(name)));
    }
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
    // Narrowed by item 108 to the actions that can reach the TOOLBAR, which is
    // where the rule comes from: an icon-only toolbar makes the icon the whole
    // control. The exemption was six whole-thread actions sharing their
    // message-scoped twins' icons; item 177 deleted those six, so the list is
    // down to the one entry that earns it on its own terms.
    //
    // reply_no_quote shares reply's icon, is a menu entry that always carries
    // its text, and is not on the toolbar. The list is named for the PROPERTY
    // that earns the exemption rather than for the tier that first needed it,
    // which is why it survives that tier's deletion unchanged.
    //
    // Named as an exception list rather than by asking the toolbar what it
    // holds, so that PUTTING one of these on the toolbar fails this test
    // rather than silently passing it.
    static const QStringList menuOnlySharedIconActions = {
        QStringLiteral("reply_no_quote"),
    };

    const Config config;
    MainWindow window(config);

    // The exception must not become a hiding place: every one of them still
    // has to carry an icon, which everyActionCarriesAnIcon asserts, and none
    // may sit on the toolbar.
    // BY NAME. There are two toolbars since item 140, and an unnamed
    // findChild returns whichever comes first: pointed at the message pane's
    // bar, this loop would assert that a thread action is absent from a bar
    // that never holds any, and pass while the rule went unchecked.
    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("main_toolbar"));
    QVERIFY(toolBar);
    for (const QString &name : menuOnlySharedIconActions) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action named %1").arg(name)));
        QVERIFY2(!toolBar->actions().contains(action),
                 qPrintable(QStringLiteral("%1 is on the toolbar, where a "
                                           "shared icon is ambiguous, so it "
                                           "cannot be exempt from this rule")
                                .arg(name)));
    }

    QHash<qint64, QString> owners;
    QStringList collisions;
    int withIcons = 0;
    int compared = 0;

    for (const QString &name : KeyMap::knownActions()) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action named %1").arg(name)));
        if (!action->icon().isNull())
            ++withIcons;
        if (menuOnlySharedIconActions.contains(name))
            continue;
        if (action->icon().isNull())
            continue;
        ++compared;

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

    // And the exception list did not swallow the comparison itself.
    QCOMPARE(compared, KeyMap::knownActions().size()
                           - menuOnlySharedIconActions.size());

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
/// The submenu carrying one saved query's actions, which since item 94 is where
/// every saved query lives: the row is built-in filters only, so there is no
/// button to right-click and the entry's own submenu is the whole affordance.
static QMenu *savedQueryEntryMenu(MainWindow &window, const QString &label)
{
    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    if (!menuButton || !menuButton->menu())
        return nullptr;
    for (QAction *action : menuButton->menu()->actions()) {
        if (action->text() == label)
            return action->menu();
    }
    return nullptr;
}

/// One action from a saved query's submenu, by object name. The counterpart of
/// contextActionNamed() for the menu, which holds its actions directly rather
/// than as a context menu on a widget.
static QAction *savedQueryActionNamed(MainWindow &window, const QString &label,
                                      const QString &objectName)
{
    QMenu *menu = savedQueryEntryMenu(window, label);
    if (!menu)
        return nullptr;
    for (QAction *action : menu->actions()) {
        if (action->objectName() == objectName)
            return action;
    }
    return nullptr;
}

void TestMainWindow::everySavedQueryLivesInTheMenu()
{
    // Item 94. The row is built-in filters ONLY, and every saved query is in
    // the menu whatever queries.json says: `pinned` is no longer read, so a
    // file still carrying it from before the removal must not put a button
    // back on the row.
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

    // No saved query is a button any more, INCLUDING the one the file pins.
    // That entry is the whole point of the fixture: a test with nothing
    // pinned passes against `pinned` still being honoured.
    const QStringList labels = savedQueryButtonLabels(window);
    QVERIFY2(!labels.contains(QStringLiteral("Inbox")),
             "a saved query marked pinned in the file still became a button");
    QVERIFY2(!labels.contains(QStringLiteral("Buried")),
             "a saved query became a button");

    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    QVERIFY2(menuButton, "the saved queries have no menu to live in");
    QVERIFY(menuButton->menu());

    QStringList entries;
    for (QAction *action : menuButton->menu()->actions())
        entries.append(action->text());

    QVERIFY2(entries.contains(QStringLiteral("Inbox")),
             "the formerly pinned query is missing from the menu");
    QVERIFY2(entries.contains(QStringLiteral("Buried")),
             "the unpinned query is missing from the menu");
}

/// Document order, not alphabetical: "Zebra" is written first and must stay
/// first. The property the storage change was made for, kept from the deleted
/// pinnedButtonsFollowTheDocumentOrder now that the menu is the only home.
void TestMainWindow::menuEntriesFollowTheDocumentOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Zebra", "query": "tag:zebra" },
            { "name": "Apple", "query": "tag:apple" }
        ]
    })"));

    MainWindow window(config);
    auto *menuButton =
        window.findChild<QPushButton *>(QStringLiteral("savedQueryMenuButton"));
    QVERIFY(menuButton && menuButton->menu());

    QStringList entries;
    for (QAction *action : menuButton->menu()->actions())
        entries.append(action->text());

    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0), QStringLiteral("Zebra"));
    QCOMPARE(entries.at(1), QStringLiteral("Apple"));
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

    // Through the menu's Run entry: since item 94 a saved query is never a
    // button, and Qt emits no triggered for the entry that owns the submenu.
    QAction *run = savedQueryActionNamed(window, QStringLiteral("Billing"),
                                         QStringLiteral("runQuery"));
    QVERIFY(run);
    run->trigger();

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
    QAction *run = savedQueryActionNamed(window, QStringLiteral("Everywhere"),
                                        QStringLiteral("runQuery"));
    QVERIFY(run);
    run->trigger();

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

/// The row must not vanish when the only content is the menu, which since item
/// 94 is where every saved query lives: hiding the row buries all of them.
void TestMainWindow::theRowSurvivesWithNothingButAMenu()
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
    }
    QVERIFY2(found, "the renamed entry was DROPPED rather than kept");

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

    // The guard: the menu was built and the other entry is in it, so a missing
    // Sent means it was skipped rather than that nothing was built at all.
    QVERIFY2(savedQueryEntryMenu(window, QStringLiteral("Inbox")),
             "the menu was not built, so the assertion below proves nothing");
    QVERIFY2(!savedQueryEntryMenu(window, QStringLiteral("Sent")),
             "a generated query with nothing to show must not get an entry");
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

void TestMainWindow::aSavedQueryEntryOffersRunEditAndDelete()
{
    // Item 94 removed the pin action along with the row, so the entry offers
    // three things rather than four. Run is among them and is load-bearing:
    // Qt emits no triggered for the action owning the submenu, so without an
    // item INSIDE it the query could not be run at all.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    loadWithQueries(config, dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox" }
        ]
    })"));

    MainWindow window(config);
    QVERIFY2(savedQueryEntryMenu(window, QStringLiteral("Inbox")),
             "the saved query has no menu entry to carry its actions");

    QVERIFY(savedQueryActionNamed(window, QStringLiteral("Inbox"),
                                  QStringLiteral("runQuery")));
    QVERIFY(savedQueryActionNamed(window, QStringLiteral("Inbox"),
                                  QStringLiteral("editQuery")));
    QVERIFY(savedQueryActionNamed(window, QStringLiteral("Inbox"),
                                  QStringLiteral("deleteQuery")));

    // The retired affordance, asserted absent rather than merely unused: item
    // 94 removed pinning, and an action left behind would still be clickable.
    QVERIFY2(!savedQueryActionNamed(window, QStringLiteral("Inbox"),
                                    QStringLiteral("pinQuery")),
             "the retired pin action is still offered");
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

    // Both are menu entries since item 94, so the comparison is between two
    // entries rather than between a button and a built-in filter. The stored
    // one is found by name, which the user can rename, but nothing else
    // distinguishes an entry in the menu.
    QVERIFY2(savedQueryEntryMenu(window, QStringLiteral("Inbox")),
             "no menu entry was built for the stored query");
    QVERIFY2(savedQueryEntryMenu(window, QStringLiteral("Sent")),
             "no menu entry was built for the generated query");

    QVERIFY2(savedQueryActionNamed(window, QStringLiteral("Inbox"),
                                   QStringLiteral("queryToRule")),
             "a stored query must offer Create tagging rule");
    QVERIFY2(!savedQueryActionNamed(window, QStringLiteral("Sent"),
                                    QStringLiteral("queryToRule")),
             "a generated query must not: its query is a snapshot");

    // The guard: without it, an entry carrying NO actions at all would pass the
    // assertion above. Asserted on the generated entry, since that is the one
    // the absence is claimed of.
    QVERIFY2(savedQueryActionNamed(window, QStringLiteral("Sent"),
                                   QStringLiteral("deleteQuery")),
             "the generated entry lost its other actions, so the assertion "
             "above is not about queryToRule in particular");
}

void TestMainWindow::savingStripsTheRetiredPinnedField()
{
    // Item 94, and the half that is NOT just deleting UI: the user chose to
    // strip `pinned` rather than leave it ignored in the file. A key that is
    // no longer read but still written back is the shape that makes a later
    // reader disagree with this one.
    //
    // The fixture pins BOTH entries, so the assertion cannot pass by the field
    // having been absent all along.
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

    // Neither is a button any more, which is what makes the file the only
    // place the retired field could still be observed.
    QVERIFY(savedQueryButtonLabels(window).isEmpty());

    // Any save rewrites the whole array, so editing one entry puts BOTH through
    // the writer, including the one that was not touched. Through the testing
    // seam rather than the Delete action, which raises a confirmation modal
    // with nothing to dismiss it under the offscreen platform.
    SavedQuery edited;
    edited.name = QStringLiteral("Inbox");
    edited.query = QStringLiteral("tag:inbox and not tag:muted");
    window.replaceSavedQueryForTesting(QStringLiteral("Inbox"), edited);

    const QJsonArray stored = storedQueries(dir);
    QCOMPARE(stored.size(), 2);
    for (const QJsonValue &value : stored) {
        const QJsonObject entry = value.toObject();
        QVERIFY2(!entry.contains(QStringLiteral("pinned")),
                 qPrintable(QStringLiteral("the retired pinned field was "
                                           "written back for '%1'")
                                .arg(entry.value(QStringLiteral("name"))
                                         .toString())));
    }

    // The untouched entry is otherwise intact: a strip that took the query with
    // it would pass the loop above and lose the user's data.
    const QJsonObject other = stored.at(1).toObject();
    QCOMPARE(other.value(QStringLiteral("name")).toString(),
             QStringLiteral("Other"));
    QCOMPARE(other.value(QStringLiteral("query")).toString(),
             QStringLiteral("tag:other"));
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
    QVERIFY2(savedQueryEntryMenu(window, QStringLiteral("Doomed")),
             "the query to be deleted is not in the menu");

    QAction *del = savedQueryActionNamed(window, QStringLiteral("Doomed"),
                                         QStringLiteral("deleteQuery"));
    QVERIFY(del);
    // Destructive and not on the undo stack, so it confirms. Suppressed here
    // rather than driven through the modal dialog, which would hang the test.
    window.setConfirmDeleteForTesting(false);
    del->trigger();

    QVERIFY2(!savedQueryEntryMenu(window, QStringLiteral("Doomed")),
             "the deleted query is still in the menu");
    QVERIFY2(savedQueryEntryMenu(window, QStringLiteral("Keeper")),
             "the surviving query was removed too");

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
    window.replaceSavedQueryForTesting(QStringLiteral("Old"), renamed);

    const QJsonArray stored = storedQueries(dir);
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("New"));

    // And the menu followed the rename rather than keeping both, which is the
    // duplication this asserts against.
    QVERIFY2(savedQueryEntryMenu(window, QStringLiteral("New")),
             "the renamed query is missing from the menu");
    QVERIFY2(!savedQueryEntryMenu(window, QStringLiteral("Old")),
             "the old name is still in the menu, so the rename duplicated");
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

/// Item 181, from the user: "the thread dashboard doesn't update live with the
/// modifications applied to the list pane. If I mark the thread as read, the
/// dash still reports N unread".
///
/// The dashboard draws a ThreadDigest, which the worker builds from the index
/// and which arrived only when a conversation was SELECTED. A tag write moved
/// the model and the card beside it and never touched the digest, so the pane
/// went on reporting the unread count the conversation had when it was opened.
///
/// Reachable from the dashboard's own Mark all read button, which is the worst
/// version of it: the user presses a button and the number above it does not
/// move.
/// Item 182, found by hand: a thread marked read DURING a sync reported
/// "<subject>: mark as read", and then reported the same work again when the
/// sync finished, never once saying it was waiting.
///
/// The write is held, correctly: a sync holds notmuch's exclusive lock and the
/// worker's read-write open BLOCKS on it, so sending would freeze the worker
/// for the rest of the run. Every hold branch sets a deliberately NON-transient
/// label explaining that, and every caller then overwrote it a line later with
/// the generic announcement, which claims the write happened.
///
/// Asserted on the LABEL rather than on the write: what the mail does was
/// already right, and what the user was told was not.
void TestMainWindow::anEditHeldByASyncSaysSoInsteadOfClaimingItLanded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Config config = configWithTrash(dir);
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(status);

    ThreadSummary thread = threadAtPath(QStringLiteral("t1"),
                                        QStringLiteral("acct/inbox/cur/1:2,S"),
                                        { QStringLiteral("unread") });
    thread.totalCount = 9;
    model->appendBatch({ thread });
    view->setCurrentIndex(model->index(0, 0, {}));

    // The sync starts. This is the same slot the sync monitor calls, so the
    // window reaches the state a real background sync puts it in.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));

    // The toggle, which is the route the user took: the thread has unread
    // messages, so this reads "Mark thread as read" and sends the write.
    auto *toggle = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(toggle);
    toggle->trigger();

    // The hold has to be what the user is told about. Without the fix the
    // label reads "Mark as read: 1 thread(s)", which claims a write that has
    // not happened and cannot happen until the sync ends.
    QVERIFY2(status->text().contains(QStringLiteral("sync")),
             qPrintable(QStringLiteral("the status bar never mentions the "
                                       "sync that is holding the edit: %1")
                            .arg(status->text())));

    // And it still names the action, which is what stands in for the
    // confirmation dialog this project rules out: the user has to be able to
    // tell that something larger than they meant has just happened.
    QVERIFY2(status->text().contains(QStringLiteral("read"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("the status bar no longer says WHAT is "
                                       "waiting: %1").arg(status->text())));

    // No restore to "/proc/locks": init() points every test at its own
    // table, and handing the real one back would re-expose the next test.
}

void TestMainWindow::theDashboardFollowsAWriteToTheConversationItShows()
{
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("conv0@example.org"),
        QStringLiteral("A conversation"), QStringLiteral("alice@example.org"),
        // Friday, verified with `date -d 2026-08-14 +%A`. Qt::RFC2822Date
        // validates the weekday against the date.
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Root."), true));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("inbox"), QStringLiteral("conv1@example.org"),
        QStringLiteral("Re: A conversation"), QStringLiteral("bob@example.org"),
        QStringLiteral("Sat, 15 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Reply."), true, QStringLiteral("conv0@example.org")));
    QVERIFY2(backed.build(), qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(view);
    auto *messageView = window.findChild<MessageView *>();
    QVERIFY(messageView);
    ThreadDashboard *dashboard = messageView->dashboard();
    QVERIFY(dashboard);

    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);
    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QModelIndex row = model->index(0, 0, {});
    QVERIFY2(model->isConversationRow(row),
             "the fixture is not a conversation, so this test cannot see the "
             "dashboard at all");

    view->setCurrentIndex(row);

    // The digest is a worker round trip, so wait for the pane to actually
    // carry the conversation's state before asserting anything about it. Not a
    // fixed wait: that passes when the digest never arrives.
    QTRY_VERIFY_WITH_TIMEOUT(messageView->showingDashboard(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!dashboard->showingAllCaughtUp(), 15000);
    QVERIFY2(dashboard->unreadCountShown() == 2,
             qPrintable(QStringLiteral("expected 2 unread listed, got %1")
                            .arg(dashboard->unreadCountShown())));

    // Driven through the ACTION rather than the private funnel, which is both
    // the only route from here and the better assertion: it is the path the
    // dashboard's own Mark all read button takes.
    auto *markAllRead =
        window.findChild<QAction *>(QStringLiteral("mark_all_read"));
    QVERIFY(markAllRead);
    markAllRead->trigger();

    // The pane must follow it. Without the refresh the digest is the one built
    // when the row was selected, and this stays at 2 for ever.
    QTRY_VERIFY_WITH_TIMEOUT(dashboard->showingAllCaughtUp(), 15000);
    QCOMPARE(dashboard->unreadCountShown(), 0);
}

void TestMainWindow::aPurgeTakesTheRowsOutOfTheViewWithoutARefresh()
{
    // Found by hand: the mail was destroyed correctly and the list went on
    // showing it until the user re-ran the query themselves.
    //
    // A purge is the one mutation with no optimistic update to apply. Every
    // other one CHANGES a row, so the model can rewrite it in place; this one
    // takes the row away entirely, and the only honest view afterwards is the
    // one the query gives now.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/trash"), QStringLiteral("doomed@example.org"),
        QStringLiteral("A subject"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.buildWithAccounts({ { QStringLiteral("acct"),
                                          QStringLiteral("acct"),
                                          QStringLiteral("trash"),
                                          {}, {}, {} } }),
             qPrintable(backed.error()));

    MainWindow window(backed.config());

    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    queryEdit->setText(QStringLiteral("path:\"acct/trash/**\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    // Straight to the purge, bypassing the confirmation: a modal cannot be
    // driven from a test without blocking it (item 84), and what is under
    // test is what happens AFTER the user has confirmed.
    window.purgeForTesting({ QStringLiteral("doomed@example.org") });

    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 0, 15000);
}

namespace {

/// A worker-backed window with one message in one account's maildir.
///
/// The compose cases all need the same three things: a message on disk, an
/// account owning the folder it landed in, and a selected row. Repeating that
/// in six tests is how one of them ends up subtly different from the rest.
struct WorkerComposeFixture
{
    WorkerBackedWindow backed;

    /// Writes one message into <accountMaildir>/inbox and indexes it.
    /// \p composeKey, when given, is written as one line under [compose].
    bool seed(const QList<WorkerBackedWindow::AccountSpec> &accounts,
              const QString &folder, const QString &composeKey = QString())
    {
        if (!backed.fixture().addMessage(
                folder, QStringLiteral("compose1@example.org"),
                QStringLiteral("A subject"),
                QStringLiteral("sender@example.org"),
                // Friday, verified with `date -d 2026-08-14 +%A`.
                QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
                QStringLiteral("Body text."))) {
            return false;
        }
        return backed.buildWithAccounts(accounts, composeKey);
    }

    /// Runs a query and puts the current index on its one row.
    ///
    /// Waits on the MAIL ROOT as well as on the row. The reply family is gated
    /// on which account owns the message, which needs the root, and that
    /// arrives on its own queued signal: asserting on an action's enabled
    /// state before it lands measures the startup race rather than the rule.
    static bool selectTheMessage(MainWindow &window)
    {
        auto *model = window.findChild<ThreadListModel *>();
        auto *view = window.findChild<ThreadListView *>();
        auto *queryEdit =
            window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
        if (!model || !view || !queryEdit)
            return false;

        queryEdit->setText(QStringLiteral("tag:inbox"));
        queryEdit->returnPressed();

        bool ready = false;
        for (int attempt = 0; attempt < 150 && !ready; ++attempt) {
            ready = model->rowCount(QModelIndex()) == 1
                    && !window.mailRootForTesting().isEmpty();
            if (!ready)
                QTest::qWait(100);
        }
        if (!ready)
            return false;

        view->setCurrentIndex(model->index(0, 0, QModelIndex()));
        return true;
    }
};

}  // namespace

void TestMainWindow::theMailRootComesFromTheConfigNotTheIndex()
{
    // Item 124's rule, for the path the composer composes drafts and sent
    // copies under. splitIndex() is what makes this test able to fail at all:
    // in the ordinary layout notmuch_database_get_path() and
    // NOTMUCH_CONFIG_MAIL_ROOT return the SAME string, so a test written
    // against it passes whichever accessor the code uses.
    WorkerComposeFixture fixture;
    fixture.backed.fixture().splitIndex();
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());

    QTRY_VERIFY_WITH_TIMEOUT(!window.mailRootForTesting().isEmpty(), 15000);

    // The MAIL root, not the index directory. Under the split layout these are
    // different directories, and a draft composed under the index one is
    // written into the Xapian tree.
    QCOMPARE(window.mailRootForTesting(),
             QDir(fixture.backed.fixture().maildirPath()).absolutePath());
    QVERIFY2(window.mailRootForTesting()
                 != QDir(fixture.backed.fixture().indexPath()).absolutePath(),
             "the window took the index directory for the mail root");
}

void TestMainWindow::replyIsDisabledOnAReceiveOnlyAccountsMail()
{
    // The capability IS the send_command's presence, so this account is
    // written without one.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("listsonly"),
                              QStringLiteral("listsonly"), QString(),
                              /*sendCommand=*/QString(),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("listsonly/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QVERIFY(WorkerComposeFixture::selectTheMessage(window));

    for (const QString &name : { QStringLiteral("reply"),
                                 QStringLiteral("reply_all"),
                                 QStringLiteral("reply_no_quote"),
                                 QStringLiteral("forward") }) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action %1").arg(name)));
        QVERIFY2(!action->isEnabled(),
                 qPrintable(QStringLiteral("%1 was live on receive-only mail")
                                .arg(name)));
    }

    // save_message is NEVER disabled, including here. It is the escape hatch
    // for exactly this case: write the raw message out and attach it to a new
    // message from an account that can send.
    auto *save = window.findChild<QAction *>(QStringLiteral("save_message"));
    QVERIFY(save);
    QVERIFY2(save->isEnabled(),
             "save_message was disabled, removing the escape hatch");
}

void TestMainWindow::replyIsEnabledOnASendingAccountsMail()
{
    // The guard for the test above. Without it, a bug disabling the reply
    // family unconditionally would pass every assertion there while removing
    // the feature entirely.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QVERIFY(WorkerComposeFixture::selectTheMessage(window));

    for (const QString &name : { QStringLiteral("reply"),
                                 QStringLiteral("reply_all"),
                                 QStringLiteral("reply_no_quote"),
                                 QStringLiteral("forward") }) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action %1").arg(name)));
        QVERIFY2(action->isEnabled(),
                 qPrintable(QStringLiteral("%1 was disabled on mail from an "
                                           "account that can send").arg(name)));
    }

    // And no ribbon: this account can send, so there is nothing to explain.
    auto *ribbon =
        window.findChild<QLabel *>(QStringLiteral("receiveOnlyRibbon"));
    QVERIFY(ribbon);
    QVERIFY2(ribbon->isHidden(),
             "the receive-only ribbon showed on an account that can send");
}

void TestMainWindow::theReceiveOnlyRibbonNamesTheAccount()
{
    // The ribbon is a WIDGET in MessageView's layout, not markup inside the
    // web view. Composing HTML from configuration into the one document that
    // renders input from strangers is the wrong direction.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("listsonly"),
                              QStringLiteral("listsonly"), QString(),
                              QString(), QStringLiteral("you@example.org") } },
                          QStringLiteral("listsonly/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QVERIFY(WorkerComposeFixture::selectTheMessage(window));

    auto *ribbon =
        window.findChild<QLabel *>(QStringLiteral("receiveOnlyRibbon"));
    QVERIFY2(ribbon, "no ribbon widget exists");

    // isHidden() rather than isVisibleTo(): under the offscreen platform an
    // unshown window's children report not visible whatever the code does, so
    // isVisibleTo would fail against correct code. What is being asserted is
    // that the ribbon was not left explicitly hidden.
    QVERIFY2(!ribbon->isHidden(),
             "the ribbon did not appear on receive-only mail");
    QVERIFY2(ribbon->text().contains(QStringLiteral("listsonly")),
             qPrintable(QStringLiteral("the ribbon does not name the account: %1")
                            .arg(ribbon->text())));

    // PlainText, not AutoText. A QLabel guesses under AutoText, and this is
    // the same protection MessageDetailsDialog states on every value.
    QCOMPARE(ribbon->textFormat(), Qt::PlainText);
}

void TestMainWindow::theReceiveOnlyRibbonGoesWithTheMessageThatRaisedIt()
{
    // The ribbon explains ONE message, so it must not outlive it. Observed in
    // All accounts: receive-only mail raised it, and selecting mail from an
    // account that can send left it on screen contradicting the live Reply
    // button beside it.
    WorkerComposeFixture fixture;
    QVERIFY(fixture.backed.fixture().addMessage(
        QStringLiteral("listsonly/inbox"), QStringLiteral("ro@example.org"),
        QStringLiteral("Receive only"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY(fixture.backed.fixture().addMessage(
        QStringLiteral("work/inbox"), QStringLiteral("rw@example.org"),
        QStringLiteral("Can send"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(fixture.backed.buildWithAccounts(
                 { { QStringLiteral("listsonly"), QStringLiteral("listsonly"),
                     QString(), QString(), QStringLiteral("you@example.org") },
                   { QStringLiteral("work"), QStringLiteral("work"),
                     QString(), QStringLiteral("/bin/true"),
                     QStringLiteral("you@example.org") } }),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *ribbon =
        window.findChild<QLabel *>(QStringLiteral("receiveOnlyRibbon"));
    QVERIFY(model && view && queryEdit && ribbon);

    // Both messages in one list, which is the All accounts view the defect was
    // seen in. The mail root has to have arrived too: the ribbon is decided by
    // which account owns the message, which cannot be answered without it.
    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 2
                                 && !window.mailRootForTesting().isEmpty(),
                             15000);

    // Find each row by subject rather than by position: the sort order is not
    // what is under test, and asserting on it would make this fail for a
    // reason that has nothing to do with the ribbon.
    QModelIndex receiveOnlyRow;
    QModelIndex sendingRow;
    for (int row = 0; row < model->rowCount(QModelIndex()); ++row) {
        const QModelIndex index = model->index(row, 0, QModelIndex());
        const QString subject = model->threadFor(index).subject;
        if (subject == QStringLiteral("Receive only"))
            receiveOnlyRow = index;
        else if (subject == QStringLiteral("Can send"))
            sendingRow = index;
    }
    QVERIFY2(receiveOnlyRow.isValid() && sendingRow.isValid(),
             "the two seeded messages are not both in the list");

    view->setCurrentIndex(receiveOnlyRow);
    QTRY_VERIFY_WITH_TIMEOUT(!ribbon->isHidden(), 15000);

    // Straight from one to the other, with no deselection in between. This
    // half already worked: a selection change reaches updateComposeActions().
    view->setCurrentIndex(sendingRow);
    QTRY_VERIFY_WITH_TIMEOUT(ribbon->isHidden(), 15000);
    QVERIFY2(ribbon->isHidden(),
             "the ribbon stayed up on mail from an account that can send");

    // The half that did not: blanking the pane by any route that is not a
    // selection change. MessageView::clear() resets the blocked-content bar,
    // the stale notice and the attachment bar by hand, and forgot this one, so
    // the ribbon outlived the message it explains.
    view->setCurrentIndex(receiveOnlyRow);
    QTRY_VERIFY_WITH_TIMEOUT(!ribbon->isHidden(), 15000);

    window.findChild<QAction *>(QStringLiteral("clear_pane"))->trigger();
    QVERIFY2(ribbon->isHidden(),
             "the ribbon survived clear_pane, over a blank message pane");

    // Away and back, not straight back: clear_pane leaves the receive-only row
    // CURRENT, so re-selecting it emits no change and the ribbon would never
    // be re-raised. That is the view's behaviour and not the defect under test.
    view->setCurrentIndex(sendingRow);
    QTRY_VERIFY_WITH_TIMEOUT(ribbon->isHidden(), 15000);
    view->setCurrentIndex(receiveOnlyRow);
    QTRY_VERIFY_WITH_TIMEOUT(!ribbon->isHidden(), 15000);

    queryEdit->setText(QStringLiteral("tag:inbox and subject:\"Can send\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    QVERIFY2(ribbon->isHidden(),
             "the ribbon survived a new query that blanked the pane");
}

void TestMainWindow::composeIsDisabledOnlyWhenNoAccountCanSend()
{
    // An installation with no send_command anywhere is a valid read-only
    // installation and is not warned about; compose is simply unavailable.
    {
        WorkerComposeFixture fixture;
        QVERIFY2(fixture.seed({ { QStringLiteral("listsonly"),
                                  QStringLiteral("listsonly"), QString(),
                                  QString(), QStringLiteral("you@example.org") } },
                              QStringLiteral("listsonly/inbox")),
                 qPrintable(fixture.backed.error()));

        MainWindow window(fixture.backed.config());
        auto *compose = window.findChild<QAction *>(QStringLiteral("compose"));
        QVERIFY(compose);
        QVERIFY2(!compose->isEnabled(),
                 "compose was live with no account able to send");
    }
    {
        WorkerComposeFixture fixture;
        QVERIFY2(fixture.seed(
                     { { QStringLiteral("listsonly"),
                         QStringLiteral("listsonly"), QString(), QString(),
                         QStringLiteral("you@example.org") },
                       { QStringLiteral("work"), QStringLiteral("work"),
                         QString(), QStringLiteral("/bin/true"),
                         QStringLiteral("work@example.org") } },
                     QStringLiteral("listsonly/inbox")),
                 qPrintable(fixture.backed.error()));

        MainWindow window(fixture.backed.config());
        auto *compose = window.findChild<QAction *>(QStringLiteral("compose"));
        QVERIFY(compose);
        QVERIFY2(compose->isEnabled(),
                 "compose was disabled although one account can send");
    }
}

void TestMainWindow::quittingWithACleanComposerAsksNothing()
{
    // Case 1: every composer clean, quit directly, no dialog. A dialog here
    // would be the "are you sure" this project deliberately does not do.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QTRY_VERIFY_WITH_TIMEOUT(!window.mailRootForTesting().isEmpty(), 15000);

    QVERIFY2(window.openComposerForTest(), "no composer opened");
    QCOMPARE(window.openComposerCount(), 1);

    QVERIFY2(window.composersBlockingQuit().isEmpty(),
             "a clean composer was reported as blocking quit");

    // Composers are parentless top-level windows and outlive this MainWindow,
    // carrying a MessageSender and a running autosave timer into whatever test
    // runs next. Closed here rather than left for the destructor, which never
    // touches m_composers.
    for (ComposeWindow *composer : window.openComposersForTest()) {
        composer->show();
        composer->close();
    }
}

void TestMainWindow::quittingWithUnsavedEditsReportsEveryComposer()
{
    // Case 2: ONE dialog whatever the count, so the quit path has to see BOTH
    // composers rather than stopping at the first dirty one.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QTRY_VERIFY_WITH_TIMEOUT(!window.mailRootForTesting().isEmpty(), 15000);

    QVERIFY(window.openComposerForTest());
    QVERIFY(window.openComposerForTest());
    QCOMPARE(window.openComposerCount(), 2);

    // Clean until something is typed, which is the case-1 assertion holding
    // here too and the guard that this test can distinguish the two states.
    QVERIFY(window.composersBlockingQuit().isEmpty());

    window.markComposersDirtyForTest();
    QCOMPARE(window.composersBlockingQuit().size(), 2);

    // Left open, these are parentless top-level windows with a live autosave
    // timer, surviving into later tests. See the note in the clean-composer
    // case above.
    for (ComposeWindow *composer : window.openComposersForTest()) {
        composer->show();
        composer->close();
    }
}

void TestMainWindow::closingAComposerCompactsTheRegistry()
{
    // The closed() signal's ONE job. The QPointer alone would keep
    // composersBlockingQuit() correct, since it nulls on destruction, but the
    // entry would stay in the list for the session's lifetime. This asserts
    // the list is compacted, which only the signal can do.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QTRY_VERIFY_WITH_TIMEOUT(!window.mailRootForTesting().isEmpty(), 15000);

    ComposeWindow *composer = window.openComposerForTest();
    QVERIFY(composer);
    QCOMPARE(window.openComposerCount(), 1);

    // A composer that was never shown returns early from close() WITHOUT
    // reaching closeEvent(), so the signal would never fire and this test
    // would assert nothing at all.
    composer->show();
    QVERIFY(composer->close());

    // And the quit path must not see a destroyed window, which is the
    // QPointer's job rather than the signal's.
    QCOMPARE(window.openComposerCount(), 0);
    QVERIFY(window.composersBlockingQuit().isEmpty());
}

void TestMainWindow::savingAMessageRefusesToEscapeTheChosenDirectory()
{
    // A subject is input from a stranger and is what the default filename is
    // derived from, so it may carry separators and "..". Asserted through
    // Attachment's own helpers, which is what saveDisplayedMessage() calls:
    // a second implementation of the check here would prove nothing about the
    // one that runs.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString directory = dir.path();

    Attachment naming;
    naming.filename = QStringLiteral("../../etc/passwd");
    const QString target =
        QDir(directory).absoluteFilePath(naming.safeFilename());

    QVERIFY2(Attachment::isPathInsideDirectory(directory, target),
             "a traversing subject escaped the chosen directory");
    QVERIFY2(!target.contains(QStringLiteral("/etc/passwd")),
             qPrintable(QStringLiteral("the traversal survived: %1").arg(target)));

    // Compared as PATHS, never with startsWith(): a sibling directory whose
    // name merely begins with the chosen one's is not inside it.
    QVERIFY2(!Attachment::isPathInsideDirectory(
                 directory, directory + QStringLiteral("-evil/message.eml")),
             "a sibling directory passed the containment check");
}

void TestMainWindow::aHostileSubjectCannotEscapeTheSaveDirectory()
{
    // Asserted through MainWindow::defaultMessageFilename(), which is what
    // saveDisplayedMessage() actually calls. The previous version of this
    // check built an Attachment by hand and called safeFilename() directly:
    // that proves what Attachment does and nothing about whether save_message
    // asks it anything, and three mutations to the real path left it green.
    // CLAUDE.md: assert through the function the production path calls, not
    // through the one it calls INTO.
    const QString traversal =
        MainWindow::defaultMessageFilename(QStringLiteral("../../etc/passwd"));

    // No separator survives, so the name cannot address another directory.
    QVERIFY2(!traversal.contains(QLatin1Char('/')),
             qPrintable(QStringLiteral("a separator survived: %1").arg(traversal)));
    // NOT asserting the absence of "..": with every separator replaced, a
    // literal ".." inside a filename addresses nothing and is a legitimate
    // part of a name. What matters is that the result is a single path
    // COMPONENT, which is what makes traversal impossible.
    QCOMPARE(QFileInfo(traversal).fileName(), traversal);
    QVERIFY2(traversal != QStringLiteral("..")
                 && traversal != QStringLiteral("."),
             qPrintable(QStringLiteral("the name is a directory reference: %1")
                            .arg(traversal)));

    // And joining it onto a directory really does stay inside.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Attachment naming;
    naming.filename = traversal;
    const QString target =
        QDir(dir.path()).absoluteFilePath(naming.safeFilename());
    QVERIFY2(Attachment::isPathInsideDirectory(dir.path(), target),
             qPrintable(QStringLiteral("escaped the directory: %1").arg(target)));

    // A backslash is a separator too, on a name written by Windows software.
    const QString backslash = MainWindow::defaultMessageFilename(
        QStringLiteral("..\\..\\Windows\\System32\\config"));
    QVERIFY2(!backslash.contains(QLatin1Char('\\')),
             qPrintable(QStringLiteral("a backslash survived: %1").arg(backslash)));

    // A subject with nothing usable still yields a name rather than "" or a
    // bare extension, which would make the write land on a dotfile.
    const QString empty = MainWindow::defaultMessageFilename(QString());
    QVERIFY2(empty.startsWith(QStringLiteral("message")),
             qPrintable(QStringLiteral("empty subject gave: %1").arg(empty)));

    // The extension survives truncation. Truncating AFTER appending it would
    // cut ".eml" off a long subject and write an extensionless file.
    const QString long_ = MainWindow::defaultMessageFilename(
        QString(400, QLatin1Char('a')));
    QVERIFY2(long_.endsWith(QStringLiteral(".eml")),
             qPrintable(QStringLiteral("the extension was truncated away: %1")
                            .arg(long_.right(20))));
}

void TestMainWindow::savingTwiceDoesNotOverwriteTheFirstFile()
{
    // Two messages very often share a subject, and the filename is derived
    // from it, so the second save must not destroy the first. Driven through
    // saveDisplayedMessage() by way of the directory seam, which is the only
    // way to reach the write guard at all: the file dialog is a modal the
    // offscreen platform cannot click.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QVERIFY(WorkerComposeFixture::selectTheMessage(window));

    QTemporaryDir out;
    QVERIFY(out.isValid());

    window.saveDisplayedMessageForTest(out.path());
    window.saveDisplayedMessageForTest(out.path());

    // Two files, not one overwritten. Asserted on the COUNT rather than on the
    // second name, so the disambiguation scheme can change without the test
    // caring what it is called.
    const QStringList written =
        QDir(out.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QCOMPARE(written.size(), 2);

    // And both are real copies rather than one empty placeholder.
    for (const QString &name : written) {
        QVERIFY2(QFileInfo(QDir(out.path()).absoluteFilePath(name)).size() > 0,
                 qPrintable(QStringLiteral("%1 is empty").arg(name)));
    }
}

void TestMainWindow::savingAMessageWithAHostileSubjectStaysInTheDirectory()
{
    // Driven through saveDisplayedMessage() with a real hostile subject, which
    // is the only shape that covers the production write path. An earlier
    // version of this coverage built an Attachment by hand and called
    // safeFilename() and isPathInsideDirectory() directly, which proves what
    // Attachment does and nothing about whether save_message asks it anything.
    //
    // WHAT THIS CAN AND CANNOT CATCH, measured rather than assumed, because
    // the numbers are surprising and the next person will otherwise redo the
    // work. Three independent layers stand between a subject and the write:
    // defaultMessageFilename() replaces separators, Attachment::safeFilename()
    // reduces to a basename, and Attachment::isPathInsideDirectory() refuses
    // the write. EACH ONE ALONE IS SUFFICIENT, so removing any single layer
    // leaves this test green: measured, all three single-layer mutations pass.
    // Removing all three fails it. That is real defence-in-depth rather than a
    // probe pointed at the wrong object, and mimeparser.h:71-77 already says
    // the same of isPathInsideDirectory, but it does mean this test is a guard
    // against the DEFENCES COLLECTIVELY disappearing, not a guard on any one
    // of them. aHostileSubjectCannotEscapeTheSaveDirectory() covers the first
    // layer on its own, and a single-layer mutation there does fail.
    //
    // The subject is ABSOLUTE rather than "../..", and that matters.
    // QDir::absoluteFilePath() does not resolve ".." (measured: it
    // concatenates), but the collision loop below can rename a relative
    // traversal by accident when the target happens to exist, which makes it
    // the weaker probe. An absolute candidate replaces the directory outright.
    WorkerComposeFixture fixture;
    QVERIFY(fixture.backed.fixture().addMessage(
        QStringLiteral("work/inbox"), QStringLiteral("hostile@example.org"),
        // The subject is the attacker's input, and it is what the default
        // filename is derived from.
        // Absolute, not "../..". QDir::absoluteFilePath() does NOT resolve
        // ".." (measured: it concatenates, giving "<dir>/../../x"), but an
        // ABSOLUTE candidate replaces the directory outright, which is the
        // escape that survives every accident. A relative traversal can be
        // neutralised by the collision loop renaming it when the target
        // happens to exist, so it is the weaker probe of the two.
        QStringLiteral("/tmp/qtmaildir-pwned-probe"),
        QStringLiteral("sender@example.org"),
        // Friday, verified with `date -d 2026-08-14 +%A`.
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(fixture.backed.buildWithAccounts(
                 { { QStringLiteral("work"), QStringLiteral("work"), QString(),
                     QStringLiteral("/bin/true"),
                     QStringLiteral("you@example.org") } }),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QVERIFY(WorkerComposeFixture::selectTheMessage(window));

    // A directory INSIDE another, so an escape has somewhere to land that the
    // test can then look at. Escaping "out" writes into parent/, which is what
    // the assertions below check is still empty.
    QTemporaryDir parent;
    QVERIFY(parent.isValid());
    const QString out = parent.filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(out));

    window.saveDisplayedMessageForTest(out);

    // The file landed inside the chosen directory.
    // NOT QDir::Hidden. A file whose name begins with a dot is hidden on every
    // Unix desktop, so the write would succeed while the user could not find
    // what they saved. Listing without Hidden is what makes this assertion
    // notice that, and it is how the leading-dot case was found: a traversing
    // subject reduces to "..-..-etc-passwd" once its separators are replaced,
    // which is a dotfile.
    const QStringList inside =
        QDir(out).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QCOMPARE(inside.size(), 1);
    QVERIFY2(!inside.first().startsWith(QLatin1Char('.')),
             qPrintable(QStringLiteral("the saved message is hidden: %1")
                            .arg(inside.first())));

    // And nothing was written beside it, which is where a traversal would go.
    const QStringList escaped =
        QDir(parent.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QVERIFY2(escaped.isEmpty(),
             qPrintable(QStringLiteral("a file escaped the directory: %1")
                            .arg(escaped.join(QLatin1Char(' ')))));

    // The written path really is contained, compared as PATHS rather than with
    // startsWith(): a sibling directory whose name merely begins with the
    // chosen one's is not inside it.
    const QString written = QDir(out).absoluteFilePath(inside.first());
    QVERIFY2(Attachment::isPathInsideDirectory(out, written),
             qPrintable(QStringLiteral("escaped: %1").arg(written)));
    QVERIFY2(QFileInfo(written).size() > 0, "the saved message is empty");
}

void TestMainWindow::aStuckComposeRequestDoesNotHijackTheNextPaneLoad()
{
    // A compose request for a message that is not in the index used to stay
    // armed for ever, because it was cleared only on the branch that FOUND the
    // id. The delayed symptom is the bad one: the pane's own loads are the
    // traffic being matched against, so merely selecting that message later
    // matched, opened a composer nobody asked for, and returned before
    // renderMessages() leaving the pane blank on the row just clicked.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QVERIFY(WorkerComposeFixture::selectTheMessage(window));

    // Arm a request for an id the database does not hold. loadMessage() emits
    // an empty result for it, which is what must disarm the request.
    window.requestMessageForComposeForTest(
        QStringLiteral("nosuchmessage@example.org"),
        ComposeContext::Kind::Reply, true);

    // No composer, and the request stops being armed.
    QTRY_VERIFY_WITH_TIMEOUT(!window.composeRequestPendingForTest(), 15000);
    QCOMPARE(window.openComposerCount(), 0);

    // Now the delayed half. Select the real message: the pane must render it,
    // and no composer may appear. With the request still armed this failed
    // only if the ids matched, so the request is re-armed for the REAL id to
    // make the hijack reachable at all.
    window.requestMessageForComposeForTest(
        QStringLiteral("compose1@example.org"), ComposeContext::Kind::Reply,
        true);
    QTRY_VERIFY_WITH_TIMEOUT(!window.composeRequestPendingForTest(), 15000);

    // That one DID match, so it opened a composer. Close it and clear the
    // pane, then re-select and assert the pane renders rather than a second
    // composer opening.
    for (ComposeWindow *composer : window.openComposersForTest()) {
        composer->show();
        composer->close();
    }
    QCOMPARE(window.openComposerCount(), 0);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(model && view);
    view->setCurrentIndex(QModelIndex());
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));

    auto *pane = window.findChild<MessageView *>();
    QVERIFY(pane);
    QTRY_VERIFY_WITH_TIMEOUT(!pane->showingPlaceholder(), 15000);
    QCOMPARE(window.openComposerCount(), 0);
}

void TestMainWindow::quittingClosesEveryComposerRatherThanOrphaningIt()
{
    // A composer is a parentless top-level window, deliberately: it must appear
    // in the task switcher and be usable while the main window is. The cost is
    // that closing the main window does NOT take it down, so quitting left a
    // composer on screen with no application behind it, and Qt kept the process
    // alive for it. Reported from a hand test: the main window closed, the
    // orphan stayed, and its own close then raised the unsaved-edits dialog for
    // a session the user had already ended.
    //
    // The quit path already ASKS about those edits and saves them; what it
    // never did was close the windows afterwards.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QTRY_VERIFY_WITH_TIMEOUT(!window.mailRootForTesting().isEmpty(), 15000);

    // Two, so the fix cannot be "close the last one" and pass.
    QVERIFY2(window.openComposerForTest(), "no composer opened");
    QVERIFY2(window.openComposerForTest(), "no second composer opened");
    QCOMPARE(window.openComposerCount(), 2);

    // Clean composers: the point here is the CLOSE, not the unsaved-edits
    // dialog, which has its own tests and would block this one on a modal.
    window.show();
    window.close();

    // deleteLater() is how a composer goes away, so the count settles on the
    // next event-loop pass rather than synchronously.
    QTRY_COMPARE_WITH_TIMEOUT(window.openComposerCount(), 0, 5000);
}

void TestMainWindow::theSaveLoopToleratesAComposerClosedUnderTheDialog()
{
    // The regression for a measured use-after-free. composersBlockingQuit()
    // used to return raw pointers, and the quit path held that list across
    // QMessageBox::exec(). A nested event loop PROCESSES deleteLater(),
    // verified in a standalone Qt program: a parentless WA_DeleteOnClose
    // window closed while a modal is up is destroyed BEFORE exec() returns.
    // The dialog is window-modal to the main window only, so a user really can
    // close a composer from under it, and Save then ran on freed memory.
    //
    // The modal itself cannot be driven under the offscreen platform, so what
    // is asserted is the property that makes the loop safe: the list holds
    // QPointers, and an entry whose window is destroyed reads as null rather
    // than as a dangling pointer. That is exactly what the null check in the
    // Save loop consumes. Stated plainly because it is NOT full coverage of
    // closeEvent(): see the report.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QTRY_VERIFY_WITH_TIMEOUT(!window.mailRootForTesting().isEmpty(), 15000);

    QVERIFY(window.openComposerForTest());
    QVERIFY(window.openComposerForTest());
    window.markComposersDirtyForTest();

    QList<QPointer<ComposeWindow>> blocking = window.composersBlockingQuit();
    QCOMPARE(blocking.size(), 2);

    // Destroy one exactly as closing it under the dialog would, including the
    // deleteLater() a nested exec() would process.
    ComposeWindow *doomed = blocking.first().data();
    QVERIFY(doomed);
    doomed->show();
    doomed->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // The held list reports it as gone rather than handing back a dangling
    // pointer. A raw QList<ComposeWindow *> could not express this at all.
    QVERIFY2(blocking.first().isNull(),
             "the held entry did not null when its window was destroyed");
    QVERIFY2(!blocking.last().isNull(),
             "the surviving composer was lost too");

    // And the loop the quit path runs skips the null and still saves the
    // survivor, which is the behaviour the crash destroyed: the remaining
    // drafts were never written because the crash happened mid-loop.
    int saved = 0;
    for (const QPointer<ComposeWindow> &composer : blocking) {
        if (composer) {
            composer->saveDraftNow();
            ++saved;
        }
    }
    QCOMPARE(saved, 1);
}

namespace {

/// Writes a multipart/mixed message with one named attachment part.
///
/// Hand-written rather than built with MessageBuilder: this is the INPUT to
/// the forward path, and generating it with the same library that consumes it
/// would let an encoding mistake agree with itself.
bool writeMessageWithAttachment(const QString &path, const QString &attachName,
                                const QByteArray &attachBody)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QByteArray raw =
        "From: sender@example.org\n"
        "To: you@example.org\n"
        "Subject: Quarterly report\n"
        "Message-ID: <fwd-1@example.org>\n"
        // Friday, verified with `date -d 2026-08-14 +%A`. Qt::RFC2822Date
        // validates the weekday against the date.
        "Date: Fri, 14 Aug 2026 10:00:00 +0200\n"
        "MIME-Version: 1.0\n"
        "Content-Type: multipart/mixed; boundary=\"MIX\"\n"
        "\n"
        "--MIX\n"
        "Content-Type: text/plain; charset=utf-8\n"
        "\n"
        "See the attached document.\n"
        "--MIX\n"
        "Content-Type: application/octet-stream; name=\"" + attachName.toUtf8() + "\"\n"
        "Content-Disposition: attachment; filename=\"" + attachName.toUtf8() + "\"\n"
        "\n" + attachBody + "\n"
        "--MIX--\n";
    file.write(raw);
    file.close();
    return true;
}

/// Writes a multipart/alternative message that DOES carry a text/html part.
bool writeHtmlMessage(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(
        "From: sender@example.org\n"
        "To: you@example.org\n"
        "Subject: Has HTML\n"
        "Message-ID: <html-1@example.org>\n"
        "Date: Fri, 14 Aug 2026 10:00:00 +0200\n"
        "MIME-Version: 1.0\n"
        "Content-Type: multipart/alternative; boundary=\"ALT\"\n"
        "\n"
        "--ALT\n"
        "Content-Type: text/plain; charset=utf-8\n"
        "\n"
        "plain\n"
        "--ALT\n"
        "Content-Type: text/html; charset=utf-8\n"
        "\n"
        "<p>html</p>\n"
        "--ALT--\n");
    file.close();
    return true;
}

}  // namespace

void TestMainWindow::forwardingCarriesTheOriginalsAttachments()
{
    // The spec requires Forward to carry attachments, twice. The context field
    // existed and was never assigned, so a Forward opened with an empty
    // attachment list: the composer looked entirely correct, and the recipient
    // received a body quoting a document that was not attached, with nothing
    // erroring anywhere.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString original = dir.filePath(QStringLiteral("original.eml"));
    QVERIFY(writeMessageWithAttachment(original, QStringLiteral("report.pdf"),
                                       QByteArray("PDFBYTES")));

    QTemporaryDir confDir;
    QVERIFY(confDir.isValid());
    const QString confPath = confDir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings settings(confPath, QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("account.work"));
        settings.setValue(QStringLiteral("maildir"), QStringLiteral("work"));
        settings.setValue(QStringLiteral("address"),
                          QStringLiteral("you@example.org"));
        settings.setValue(QStringLiteral("send_command"),
                          QStringLiteral("/bin/true"));
        settings.endGroup();
        settings.sync();
    }
    Config config;
    config.load(confPath);

    ComposeContext context;
    context.kind = ComposeContext::Kind::Forward;
    context.accountKey = QStringLiteral("work");
    context.originalPath = original;
    context.subject = QStringLiteral("Fwd: Quarterly report");

    ComposeWindow composer(context, config, dir.path());

    // The attachment is present, and it is a REAL FILE on disk rather than a
    // remembered name: MessageBuilder reads every attachment by path at build
    // time and refuses a build naming one that does not exist.
    const QStringList attached = composer.attachments();
    QCOMPARE(attached.size(), 1);
    QVERIFY2(QFileInfo::exists(attached.first()),
             qPrintable(QStringLiteral("the extracted path does not exist: %1")
                            .arg(attached.first())));
    QCOMPARE(QFileInfo(attached.first()).fileName(),
             QStringLiteral("report.pdf"));

    // And the bytes are the original's, not an empty placeholder.
    QFile written(attached.first());
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), QByteArray("PDFBYTES"));
    written.close();

    // A Reply to the same message carries NOTHING. The spec says attachments
    // are carried "for Forward, empty otherwise", and a reply that re-attached
    // the original's documents would send them back to their own sender.
    ComposeContext replyContext = context;
    replyContext.kind = ComposeContext::Kind::Reply;
    ComposeWindow replyComposer(replyContext, config, dir.path());
    QVERIFY2(replyComposer.attachments().isEmpty(),
             "a reply carried the original's attachments");
}

void TestMainWindow::forwardSeedsHtmlFromTheConfigNotTheOriginal()
{
    // MEASURED, and it revises what the spec review reported. Forward was
    // NEVER seeding from the original: ComposeWindow::seedFields() already
    // implements the split itself (composewindow.cpp, `isReply ?
    // m_context.seedHtml : m_config.compose().sendHtml`), so the context's
    // value is IGNORED for a forward and the config won regardless. The
    // openComposerFor() line this test also covers was therefore cosmetic
    // rather than a live defect: it stopped the context carrying a value that
    // nothing read, which is worth doing but changed no behaviour.
    //
    // The consequence for this test: EITHER layer alone enforces the rule, so
    // neither single-layer mutation fails it, and only mutating both does.
    // Verified in both directions rather than assumed.
    //
    // The spec splits these: New and Forward seed from [compose] send_html,
    // Reply and Reply-all from whether the original carried a text/html part.
    // An HTML part in the original is a fact about the SENDER's software, so
    // it is the right seed when answering them and says nothing about a
    // forward, which is a new message to somebody else.
    //
    // Asserted on the CONTEXT the window is built from rather than through the
    // checkbox, because what is under test is which source the value comes
    // from. The two sources must DISAGREE or the test passes either way: the
    // config says false while the original is plain text, so reading the
    // original would give false as well. Hence send_html=true against a plain
    // original: config true, original false.
    // The two sources must DISAGREE or the test passes whichever one is read,
    // and getting that wrong is why an earlier version of this survived every
    // mutation: config send_html=FALSE against an original that DOES carry a
    // text/html part. Reading the original gives true, reading the config
    // gives false, so the assertion below can only be satisfied one way.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("work"), QStringLiteral("work"),
                              QString(), QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org") } },
                          QStringLiteral("work/inbox"),
                          QStringLiteral("send_html=false")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QTRY_VERIFY_WITH_TIMEOUT(!window.mailRootForTesting().isEmpty(), 15000);
    QCOMPARE(fixture.backed.config().compose().sendHtml, false);

    // The original lives inside the account's maildir so accountForReply()
    // can resolve it; its CONTENT is what matters, not that notmuch indexed it.
    const QString original =
        QDir(window.mailRootForTesting())
            .absoluteFilePath(QStringLiteral("work/inbox/cur/fwd-original"));
    QVERIFY(writeHtmlMessage(original));

    MimeParser parser;
    const ParsedMessage parsed = parser.parse(original);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.hasHtml(), true);

    // Through openComposerFor(), which is the production line that chooses
    // the source. Building the context by hand here and asserting on the
    // checkbox proved only that ComposeWindow honours what it is given: the
    // mutation putting `original.hasHtml()` back stayed green, because the
    // test was setting seedHtml itself.
    MessageRef ref;
    ref.messageId = QStringLiteral("html-1@example.org");
    ref.filePath = original;
    ref.matched = true;

    window.openComposerForTest(ref, ComposeContext::Kind::Forward, true);

    QList<ComposeWindow *> opened = window.openComposersForTest();
    QCOMPARE(opened.size(), 1);
    auto *sendHtml =
        opened.first()->findChild<QAbstractButton *>(QStringLiteral("sendHtml"));
    QVERIFY(sendHtml);
    QVERIFY2(!sendHtml->isChecked(),
             "Forward seeded sendHtml from the original's HTML part rather "
             "than from [compose] send_html");

    // The counterpart, and it is what stops this asserting "always false":
    // a REPLY to the same message seeds from the original, so it is checked
    // where the forward is not. Without this half, disabling the checkbox
    // outright would pass.
    window.openComposerForTest(ref, ComposeContext::Kind::Reply, true);
    const QList<ComposeWindow *> both = window.openComposersForTest();
    QCOMPARE(both.size(), 2);
    auto *replyHtml =
        both.last()->findChild<QAbstractButton *>(QStringLiteral("sendHtml"));
    QVERIFY(replyHtml);
    QVERIFY2(replyHtml->isChecked(),
             "Reply did not seed sendHtml from the original's HTML part");

    for (ComposeWindow *composer : both) {
        composer->show();
        composer->close();
    }
}

void TestMainWindow::aStartupAccountScopesTheStartupQuery()
{
    // "Start me in Work - Inbox rather than All accounts - Inbox." The account
    // dropdown is set before the startup query runs, and because a built-in
    // filter COMPOSES with the dropdown, the query it runs is scoped to that
    // account without the filter knowing anything about startup.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        // [general] keys are read WITHOUT the prefix, see writeSentConfig.
        s.setValue(QStringLiteral("startup_query"), QStringLiteral("Inbox"));
        s.setValue(QStringLiteral("startup_account"), QStringLiteral("work"));
        s.beginGroup(QStringLiteral("account.work"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("work"));
        s.endGroup();
        s.beginGroup(QStringLiteral("account.personal"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("personal"));
        s.endGroup();
        s.sync();
    }
    Config config;
    config.load(path);

    MainWindow window(config);

    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("work"));

    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);
    QCOMPARE(queryEdit->text(),
             QStringLiteral("path:\"work/**\" and (tag:inbox)"));

    // And the query that actually RAN carries the scope, not merely the text in
    // the bar. runQuery() is told the filter is already scoped, so a mistake
    // here would drop the scope rather than double it, and the bar would still
    // look right.
    // The exact string, not contains(): the double-scoped
    //     path:"work/**" and (path:"work/**" and (tag:inbox))
    // contains the scope too, returns exactly the right rows, and passed a
    // contains() assertion while being the very thing item 93 exists to avoid.
    QCOMPARE(window.lastRunQueryForTesting(),
             QStringLiteral("path:\"work/**\" and (tag:inbox)"));
}

void TestMainWindow::aStartupAccountAlsoScopesASavedStartupQuery()
{
    // The other half, and the one the scoping shortcut can silently drop.
    // resolvedQuery(query, accountKey) ignores the account key for a SAVED
    // query, because a saved query states its own scope. So the startup path
    // gets back an unscoped string, and telling runQuery() the query is
    // "already scoped" would leave it unscoped for good, with the dropdown
    // sitting on Work and the list showing everything.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.setValue(QStringLiteral("startup_query"), QStringLiteral("Mine"));
        s.setValue(QStringLiteral("startup_account"), QStringLiteral("work"));
        s.beginGroup(QStringLiteral("account.work"));
        s.setValue(QStringLiteral("maildir"), QStringLiteral("work"));
        s.endGroup();
        s.sync();
    }
    QFile queries(dir.filePath(QStringLiteral("queries.json")));
    QVERIFY(queries.open(QIODevice::WriteOnly | QIODevice::Text));
    queries.write(QStringLiteral(R"({
        "version": 1,
        "queries": [ { "name": "Mine", "query": "tag:todo" } ]
    })").toUtf8());
    queries.close();

    Config config;
    config.load(path);

    MainWindow window(config);

    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("work"));
    QVERIFY2(window.lastRunQueryForTesting()
                 == QStringLiteral("path:\"work/**\" and (tag:todo)"),
             qPrintable(QStringLiteral("the dropdown says Work and the query "
                                       "that ran was: ")
                            + window.lastRunQueryForTesting()));
}

void TestMainWindow::aGeneratedStartupQueryActuallyRuns()
{
    // The second half of the startup defect. The constructor read
    // startup.query directly, and a generated entry stores no query: its text
    // is composed from the accounts at run time. So even once
    // startupSavedQuery() could return a built-in filter, the window opened on
    // an empty bar and ran nothing.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }, QStringLiteral("Sent")));

    MainWindow window(config);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    // Sent, because it is the filter whose query is composed rather than
    // constant: a tag filter would pass against code that only handled the
    // easy half.
    QCOMPARE(queryEdit->text(), config.allSentQuery());
}

void TestMainWindow::theDraftsButtonIsAbsentWithoutADraftsFolder()
{
    // Item 138 follows item 103's rule: a folder filter with no folder to
    // match is left out of the row rather than shown resolving to
    // matchNothingQuery(). A button that can only ever report nothing is worse
    // than no button, since it reads as "you have no drafts".
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    });

    Config config;
    config.load(path);
    MainWindow window(config);

    QVERIFY2(!window.findChild<QAbstractButton *>(
                 QStringLiteral("draftsButton")),
             "a Drafts button appeared for an account with no drafts folder");

    // The guard: Sent IS configured here, so a change that dropped every
    // filter button would otherwise pass the assertion above.
    QVERIFY2(window.findChild<QAbstractButton *>(QStringLiteral("sentButton")),
             "the Sent button is missing, so this test proves nothing");
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
    const QString path = writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    });
    // A trash key too, or the Trash filter finds nothing and is skipped from
    // the row entirely (item 103), leaving no trashButton for this loop to
    // find. Drafts behaves the same way since item 138.
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("account.work"));
        s.setValue(QStringLiteral("trash"), QStringLiteral("Trash"));
        s.setValue(QStringLiteral("drafts"), QStringLiteral("Drafts"));
        s.endGroup();
    }
    Config config;
    config.load(path);

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

void TestMainWindow::theActiveFilterButtonIsChecked()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *inbox =
        window.findChild<QAbstractButton *>(QStringLiteral("inboxButton"));
    auto *unread =
        window.findChild<QAbstractButton *>(QStringLiteral("unreadButton"));
    QVERIFY(inbox);
    QVERIFY(unread);

    // Checkable is what lets the style draw the active look at all. A test that
    // only asserted isChecked() would pass against a button that can hold the
    // state and never shows it.
    QVERIFY2(inbox->isCheckable(), "the filter button cannot show a checked state");

    // Unread is checked before anything is clicked, and that is correct rather
    // than incidental: startup_query defaults to Unread, so the window opens
    // showing it and the highlight describes the view from the first frame. The
    // window having opened on a filter is asserted here so the Inbox
    // assertions below are known to be a CHANGE of state rather than a button
    // that happened to start unchecked.
    QVERIFY2(unread->isChecked(),
             "the default startup view is Unread, so its button should open "
             "highlighted");
    QVERIFY(!inbox->isChecked());

    inbox->click();
    QVERIFY2(inbox->isChecked(), "the filter that ran is not highlighted");
    QVERIFY2(!unread->isChecked(), "a filter that did not run is highlighted");

    // And the highlight MOVES rather than accumulating. Exactly one button can
    // describe the current view.
    unread->click();
    QVERIFY(unread->isChecked());
    QVERIFY2(!inbox->isChecked(), "the previous filter stayed highlighted");
}

void TestMainWindow::aHandEditedQueryChecksNoFilterButton()
{
    // The behaviour the user chose over "remember the last click": the
    // highlight describes what is on screen, so editing the query away from a
    // filter's own query clears it rather than leaving a button lit over a view
    // it no longer describes.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *inbox =
        window.findChild<QAbstractButton *>(QStringLiteral("inboxButton"));
    QVERIFY(queryEdit);
    QVERIFY(inbox);

    inbox->click();
    QVERIFY(inbox->isChecked());

    queryEdit->setText(QStringLiteral("from:someone@example.org"));
    QVERIFY2(!inbox->isChecked(),
             "a hand-edited query left the Inbox button highlighted");

    // And typing a filter's query by hand lights it, since the highlight is a
    // property of the query rather than a record of which button was pressed.
    queryEdit->setText(QStringLiteral("tag:inbox"));
    QVERIFY2(inbox->isChecked(),
             "a query equal to the Inbox filter did not highlight it");

    // An empty bar is not "every filter matches nothing", which a naive
    // comparison against an unresolvable query would make it.
    queryEdit->clear();
    QVERIFY(!inbox->isChecked());
}

void TestMainWindow::theCheckedFilterFollowsTheAccount()
{
    // Changing the account re-resolves the filter to a different query string,
    // and both are still "Inbox". The highlight is recomputed rather than
    // dropped, or switching account would silently un-highlight the view the
    // user is still looking at.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Config config;
    config.load(writeSentConfig(dir, {
        {QStringLiteral("work"), QStringLiteral("Sent")},
        {QStringLiteral("personal"), QStringLiteral("Sent")},
    }));

    MainWindow window(config);
    auto *queryEdit = window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *inbox =
        window.findChild<QAbstractButton *>(QStringLiteral("inboxButton"));
    QVERIFY(queryEdit);
    QVERIFY(inbox);

    window.selectAccountForTesting(QStringLiteral("work"));
    inbox->click();
    QCOMPARE(queryEdit->text(),
             QStringLiteral("path:\"work/**\" and (tag:inbox)"));
    QVERIFY(inbox->isChecked());

    // The query bar still holds work's inbox query, which is NOT personal's, so
    // the button correctly stops describing the view. The state after an
    // account change is asserted rather than assumed: this is the case where a
    // highlight keyed on the last click would go on lying.
    window.selectAccountForTesting(QStringLiteral("personal"));
    QVERIFY2(!inbox->isChecked(),
             "the highlight survived an account change that left a query "
             "belonging to the other account in the bar");

    // Running it again under the new account lights it once more.
    inbox->click();
    QCOMPARE(queryEdit->text(),
             QStringLiteral("path:\"personal/**\" and (tag:inbox)"));
    QVERIFY(inbox->isChecked());
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

    // The migrated [queries] entry lives in the menu since item 94, like every
    // saved query. Run rather than the entry itself: Qt emits no triggered for
    // an action that owns a submenu.
    QAction *run = savedQueryActionNamed(window, QStringLiteral("Mine"),
                                         QStringLiteral("runQuery"));
    QVERIFY2(run, "the user's own query is not in the menu");

    run->trigger();
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

void TestMainWindow::anUnexpandedRootShowsTheDashboardLikeAnExpandedOne()
{
    // Item 66 resolved the other way, by item 177, and this REPLACES
    // anUnexpandedRootRendersOneMessageNotTheConversation().
    //
    // What item 66 was actually about was an INCONSISTENCY across the
    // expansion boundary: clicking a thread root that had never been expanded
    // rendered the whole conversation, and the identical click rendered one
    // message afterwards, because the model learned the thread's first message
    // only when the replies loaded. The user reported the inconsistency. Item
    // 66 removed it by making both clicks render one message; item 177 removes
    // it the other way, by making a row with replies mean the conversation
    // whether or not it has been opened.
    //
    // So the old assertion is now simply the wrong expectation, while item
    // 66's real value, that the same gesture does the same thing on both sides
    // of the expansion, is exactly what this asserts.
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
    QVERIFY2(model->isConversationRow(root),
             "the two-message row is not a conversation, so this measures the "
             "lone-message path instead");

    // NEVER expanded yet. That is the side of the boundary item 66 found the
    // inconsistency on.
    QVERIFY2(!view->isExpanded(root), "the test expanded the thread itself");

    // The guard: the pane is not already showing a dashboard, or the
    // assertion after the click would pass on nothing.
    QVERIFY(!pane->showingDashboard());

    view->setCurrentIndex(root);
    QApplication::processEvents();

    QVERIFY2(pane->showingDashboard(),
             "an unexpanded conversation row rendered a message: the row "
             "stands for the thread whether or not it has been opened");

    // The other side of the boundary, which is the whole point: expanding
    // changes what the LIST shows and must not change what the ROW means.
    view->expand(root);
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(root) > 0, 15000);

    view->setCurrentIndex(root);
    QApplication::processEvents();

    QVERIFY2(pane->showingDashboard(),
             "the same click on the same row did different things either side "
             "of the expansion, which is the inconsistency item 66 reported");
}

void TestMainWindow::aSingleMessageIdQuerysCardOpensInTheMessagePane()
{
    // Item 66's unverified half, reported again against 0.23.0 with a
    // screenshot: an `id:` query in the bar produces exactly one card, the
    // status bar reports "1 thread selected (1 message)", and the pane stays
    // on the placeholder.
    //
    // The id is shaped like the real one that fails: dots, digits and an @.
    //
    // An ACCOUNT is configured and selected, because that is the state the
    // report was made from and it is the only thing that changes the query:
    // runQuery() wraps the bar's text in path:"<maildir>/**" and (...).
    WorkerBackedWindow backed;
    const QString wanted =
        QStringLiteral("1786718040388.1f6b48f1-64d1@mail.example.org");
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/Inbox"), wanted,
        QStringLiteral("A single message"),
        QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("The only message.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct")),
             qPrintable(backed.error()));

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

    auto *accountBox =
        window.findChild<QComboBox *>(QStringLiteral("accountBox"));
    QVERIFY2(accountBox, "no account dropdown");
    const int account = accountBox->findData(QStringLiteral("acct"));
    QVERIFY2(account >= 0, "the configured account is not in the dropdown");
    accountBox->setCurrentIndex(account);

    // THE SAME MESSAGE IS READ FIRST, and that is the whole defect. The id is
    // copied out of the details dialog of a message being read, so the `id:`
    // query is always typed while that very thread is the current one.
    // runQuery() blanks the pane but leaves m_currentThreadId naming it, so
    // when the row comes back onSelectionChanged() compares the two, finds them
    // equal, and never calls onThreadSelected: nothing is ever loaded.
    //
    // A first query returning a DIFFERENT thread passes against the bug, which
    // is why the earlier version of this test was green.
    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    QTRY_VERIFY_WITH_TIMEOUT(!pane->showingPlaceholder(), 15000);
    const QString firstThreadId = window.currentThreadId();
    QVERIFY2(!firstThreadId.isEmpty(), "the first view never opened a thread");

    // Exactly what the user types, `id:` and the bare id, unquoted.
    queryEdit->setText(QStringLiteral("id:%1").arg(wanted));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QModelIndex root = model->index(0, 0, QModelIndex());
    QVERIFY(root.isValid());

    // The row must name the message before the click can be blamed for
    // anything: an empty MessageIdRole is a different defect and would make
    // the assertion below true for the wrong reason.
    QCOMPARE(model->data(root, ThreadListModel::MessageIdRole).toString(),
             wanted);

    QVERIFY2(pane->showingPlaceholder(),
             "the pane was not blank to begin with");
    // The row IS the thread that was showing when the query ran. Asserted so a
    // fixture change that made them different threads could not quietly turn
    // this back into the passing test it was before the cause was found.
    QCOMPARE(model->data(root, ThreadListModel::ThreadIdRole).toString(),
             firstThreadId);

    // The pane was blanked, so nothing is on display and the window must not
    // still claim otherwise. This is the fix's own contract: leave it set and
    // the selection below is read as "already showing" and never loads.
    QVERIFY2(window.currentThreadId().isEmpty(),
             "runQuery blanked the pane but still names a current thread");

    view->setCurrentIndex(root);
    QTRY_VERIFY_WITH_TIMEOUT(!pane->showingPlaceholder(), 15000);
}

/// Whether any file in `dir` belongs to the message whose filename starts with
/// `stem`.
///
/// A Maildir filename is NOT stable across a move, which is the trap this
/// exists to avoid. `maildir.synchronize_flags` is on, so notmuch rewrites the
/// name to carry the read/seen flags: a message that leaves `new/del1.x` lands
/// as `cur/del1.x:2,S`. Asserting on the exact basename therefore fails
/// against a move that worked perfectly, which is how three of these tests
/// first "failed".
/// Counts messages matching `query` in the fixture's database, by running
/// notmuch itself.
///
/// Asked directly rather than through the query bar because the UI's
/// rowCount() reads 0 for the whole interval before the worker answers, so an
/// assertion that a tag is ABSENT is satisfied by the gap before any answer
/// arrives and passes against a database that still carries the tag.
static int notmuchCount(const QString &configPath, const QString &query)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NOTMUCH_CONFIG"), configPath);
    process.setProcessEnvironment(env);
    process.start(QStringLiteral("notmuch"),
                  { QStringLiteral("count"), query });
    if (!process.waitForFinished(15000))
        return -1;
    bool ok = false;
    const int count =
        QString::fromUtf8(process.readAllStandardOutput()).trimmed().toInt(&ok);
    return ok ? count : -1;
}

/// Applies a tag change with the notmuch binary, for the one thing the UI
/// cannot produce any more: a message tagged `deleted` while its file is still
/// in the inbox. That is the state the OLD Delete left mail in, and the state
/// the cleanup action exists to find, so a test for it has to write it
/// directly rather than through an action that now moves the file too.
static bool notmuchTag(const QString &configPath, const QStringList &args)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NOTMUCH_CONFIG"), configPath);
    process.setProcessEnvironment(env);
    process.start(QStringLiteral("notmuch"),
                  QStringList{ QStringLiteral("tag") } + args);
    return process.waitForFinished(15000) && process.exitCode() == 0;
}

/// Whether `dir` holds the message whose id the fixture wrote as `stem`.
///
/// Matches on the MESSAGE-ID INSIDE each file, never on the filename. It used
/// to compare filenames, which worked only while a move carried the name
/// across unchanged. It no longer does: mbsync requires an MUA to rename a
/// file when it moves it between folders, so `NotmuchWorker::moveMessages()`
/// generates a fresh name and this helper could never find a moved message
/// again. Every one of these assertions failed at once, each reporting "the
/// file is not there" about a file that was.
///
/// `stem` stays in the fixture's `<local>.example.org` form so the fifty-odd
/// call sites did not have to change; it is turned back into
/// `<local@example.org>` here.
static bool folderHasMessageFile(const QString &dir, const QString &stem)
{
    QDir directory(dir);
    if (!directory.exists())
        return false;

    // `del1.example.org` is the fixture's rendering of `del1@example.org`:
    // it replaces the `@` to make a filename-safe stem. Only the LAST dot
    // before the domain is the substituted one, so the split is on the first
    // dot, which is where the local part ends for every id these tests use.
    const int dot = stem.indexOf(QLatin1Char('.'));
    if (dot < 0)
        return false;
    const QString messageId = QStringLiteral("<%1@%2>")
                                  .arg(stem.left(dot), stem.mid(dot + 1));

    const QStringList entries = directory.entryList(QDir::Files);
    for (const QString &entry : entries) {
        QFile file(directory.filePath(entry));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        // The header block only: a quoted id in a body must not count.
        while (!file.atEnd()) {
            const QByteArray line = file.readLine();
            if (line.trimmed().isEmpty())
                break;
            if (line.startsWith("Message-ID:") || line.startsWith("Message-Id:")) {
                if (QString::fromUtf8(line).contains(messageId))
                    return true;
                break;
            }
        }
    }
    return false;
}

void TestMainWindow::deleteMovesTheMessageToTrash()
{
    // The whole point of item 103. Before it, Delete added a tag and moved no
    // file, so deleted mail sat in the inbox for good.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("del1@example.org"),
        QStringLiteral("Delete me"), QStringLiteral("sender@example.org"),
        // Friday, verified with `date -d 2026-08-14 +%A`.
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    QVERIFY(model);
    auto *view = window.findChild<ThreadListView *>();
    QVERIFY(view);
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QString root = backed.fixture().maildirPath();
    const QString inbox = root + QStringLiteral("/acct/inbox/new");
    const QString stem = QStringLiteral("del1.example.org");
    QVERIFY(folderHasMessageFile(inbox, stem));

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));

    auto *del = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(del);
    del->trigger();

    // The filesystem half. cur/, never new/: a file in new/ is re-announced as
    // fresh mail by every reader of the Maildir.
    const QString trash = root + QStringLiteral("/acct/Trash/cur");
    QTRY_VERIFY_WITH_TIMEOUT(folderHasMessageFile(trash, stem), 15000);
    QVERIFY2(!folderHasMessageFile(inbox, stem),
             "the file is in the trash and still in the inbox");
    QVERIFY2(!folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"),
                                   stem),
             "the file is in the trash and still in the inbox");

    // The index half, which the filesystem cannot see. A moved file with a
    // stale index entry sits correctly on disk and is invisible to every query.
    queryEdit->setText(QStringLiteral("path:\"acct/Trash/**\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
}

void TestMainWindow::deleteRecordsWhereTheMessageCameFrom()
{
    // A Maildir filename does not record where a message came from, and once
    // the file has moved notmuch cannot know either. The tag is the only
    // record, and Restore needs it days later.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("del2@example.org"),
        QStringLiteral("Delete me too"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    // Asked of the database, not of the model: the model's optimistic update
    // would report the tag whether or not the write ever landed.
    // Re-queried by id and asserted on the TAG LIST the database returns.
    //
    // Not with `tag:"deleted-from:inbox"` in the query: notmuch's parser does
    // not match a quoted tag containing a colon that way, so such a query
    // returns nothing against a perfectly tagged message and reads as the
    // feature being broken. Asking for the message and inspecting its tags
    // cannot fail that way.
    // Re-run per attempt, not once. The tag write is QUEUED behind the move,
    // so a single query can land before the tags do; QTRY_VERIFY on the
    // model's contents would then re-test a result that can never change,
    // because nothing re-asks the database. Asking again each time is what
    // makes this wait for the write rather than for the clock.
    bool tagged = false;
    for (int attempt = 0; attempt < 30 && !tagged; ++attempt) {
        queryEdit->setText(QStringLiteral("id:del2@example.org"));
        queryEdit->returnPressed();
        QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
        const QStringList tags = model->threadAt(0).tags;
        tagged = tags.contains(QStringLiteral("deleted"))
                 && tags.contains(QStringLiteral("deleted-from:inbox"));
        if (!tagged)
            QTest::qWait(200);
    }
    QVERIFY2(tagged,
             qPrintable(QStringLiteral("tags after the delete: %1")
                            .arg(model->threadAt(0).tags.join(
                                QLatin1Char(' ')))));
}

void TestMainWindow::deletingTwiceLeavesNoOriginTagBehind()
{
    // Delete twice is the ordinary way back: the action toggles, so a second
    // press on a deleted message restores it. That path is NOT the undo path
    // and had its own defect.
    //
    // onMessagesMoved() resolved the origin placeholder from the folder the
    // WORKER reported, which is where the message came FROM. On a delete that
    // is the inbox and correct. On a restore it is the TRASH, so the restore
    // asked to remove `deleted-from:Trash`, a tag that had never been written,
    // while the real `deleted-from:inbox` was never named and stayed on the
    // message. It came home still claiming to have been deleted from
    // somewhere, which makes Restore offer to move a message already at home.
    //
    // Reported from a hand test. The undo test passed throughout, because undo
    // carries its tags on the command and never resolves a placeholder.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("twice@example.org"),
        QStringLiteral("Delete me twice"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QString root = backed.fixture().maildirPath();
    const QString stem = QStringLiteral("twice.example.org");
    const QString trash = root + QStringLiteral("/acct/Trash/cur");

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(folderHasMessageFile(trash, stem), 15000);

    // The origin tag really was written, so the assertion after the second
    // delete is about it being REMOVED rather than never having existed.
    //
    // Asked of the DATABASE, not through the query bar. The file arriving in
    // the trash is not the end of the delete: the tag writes land after the
    // rename this test waits for, and a query bar run inside that gap returns
    // zero rows FOREVER, because QTRY_VERIFY re-reads rowCount() and never
    // re-runs the query. Measured 1 failure in 3 runs, each burning the full
    // 15s timeout on a guard that was correct about a database it had asked
    // too early.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(backed.fixture().configPath(),
                     QStringLiteral("id:twice@example.org and "
                                    "tag:\"deleted-from:inbox\"")) == 1,
        15000);

    // Second press on the same message, which restores it.
    queryEdit->setText(QStringLiteral("id:twice@example.org"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"), stem)
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                    stem),
        15000);

    // The file arriving is NOT the end of the restore. The tags are written
    // only once the worker confirms the move, so the writes land after the
    // rename the assertion above waits for. Querying in that gap reads the
    // state before the restore finished tagging, which is how an earlier
    // version of this test passed against the bug it exists to catch.
    //
    // Waited on the `deleted` tag, which the restore removes on every code
    // path, rather than on a fixed sleep.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(backed.fixture().configPath(),
                     QStringLiteral("id:twice@example.org and tag:deleted"))
            == 0,
        15000);

    // BOTH tags gone, asked of the database. `deleted-from:` left behind is
    // the defect this covers, and it survived a green suite before.
    // The origin tag specifically, asserted on its OWN query.
    //
    // A combined `tag:deleted or tag:"deleted-from:inbox"` query is NOT
    // equivalent and passed against the bug: `deleted` is removed correctly
    // and promptly, so the disjunction went to zero on that term alone while
    // the origin tag was still on the message. Split, so the assertion can
    // only be satisfied by the tag it names.
    // Asked of notmuch DIRECTLY, not through the query bar.
    //
    // A UI query cannot answer this reliably: rowCount() is 0 for the whole
    // interval before the worker replies, so QTRY_VERIFY(rowCount() == 0) is
    // satisfied instantly by the empty pre-result and passes against any
    // state of the database. Measured while building this test: 0 right after
    // returnPressed(), 1 once the answer actually landed. The database is the
    // thing under test here, so it is asked directly.
    const QString cfg = backed.fixture().configPath();

    // The message still exists: an assertion that a tag is absent would be
    // satisfied just as well by the message having vanished.
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:twice@example.org")), 1);

    // The origin tag is gone. This is the defect: it used to survive the
    // restore, because the placeholder resolved to `deleted-from:Trash`, the
    // folder the message was coming FROM, and stripped a tag that had never
    // been written.
    QCOMPARE(notmuchCount(cfg,
                          QStringLiteral("id:twice@example.org and "
                                         "tag:\"deleted-from:inbox\"")),
             0);

    // And no tag naming the trash was invented in its place.
    QCOMPARE(notmuchCount(cfg,
                          QStringLiteral("id:twice@example.org and "
                                         "tag:\"deleted-from:Trash\"")),
             0);

    // `deleted` itself, so a fix that dropped this one instead cannot hide.
    QCOMPARE(notmuchCount(
                 cfg, QStringLiteral("id:twice@example.org and tag:deleted")),
             0);

}

void TestMainWindow::undoOfADeleteRemovesTheOriginTagToo()
{
    // Ctrl+Z is a THIRD way back, beside the second Delete, and it had the
    // same defect for a different reason.
    //
    // MoveCommand was constructed with pending.add, which still holds the
    // unresolved origin PLACEHOLDER: onMessagesMoved() resolved the
    // placeholder for the tags it wrote to the database, but handed the undo
    // command the raw list. Undo then asked to remove a tag by the
    // placeholder's literal name, which no message carries, so the removal
    // was a silent no-op and `deleted-from:inbox` survived. The message came
    // home still claiming to have been deleted from somewhere, which makes
    // Restore offer to move a message that is already at home.
    //
    // Reported from a hand test after the second-Delete path was fixed: that
    // fix did not touch this one, and the existing undo test asserted on the
    // file's location rather than on its tags.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("undotag@example.org"),
        QStringLiteral("Undo my tags"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QString root = backed.fixture().maildirPath();
    const QString stem = QStringLiteral("undotag.example.org");
    const QString cfg = backed.fixture().configPath();

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"), stem),
        15000);

    // The origin tag really was written, so the assertion after the undo is
    // about it being REMOVED rather than never having existed.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, QStringLiteral("id:undotag@example.org and "
                                         "tag:\"deleted-from:inbox\"")) == 1,
        15000);

    window.findChild<QAction *>(QStringLiteral("undo"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"), stem)
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                    stem),
        15000);

    // The file arriving is not the end of the undo: the tags are written only
    // once the worker confirms the move, so they land after the rename.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg,
                     QStringLiteral("id:undotag@example.org and tag:deleted"))
            == 0,
        15000);

    // Asked of notmuch directly. A UI query cannot answer this: rowCount() is
    // 0 for the whole interval before the worker replies, so an assertion
    // that a tag is absent is satisfied by the gap before any answer arrives.
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:undotag@example.org")), 1);
    QCOMPARE(notmuchCount(cfg,
                          QStringLiteral("id:undotag@example.org and "
                                         "tag:\"deleted-from:inbox\"")),
             0);
    QCOMPARE(notmuchCount(cfg,
                          QStringLiteral("id:undotag@example.org and "
                                         "tag:\"deleted-from:Trash\"")),
             0);
}

void TestMainWindow::deletingALoneMessageRemovesItFromTheInboxAndUndoReturnsIt()
{
    // Delete's message-scoped half, end to end, on the row where it still
    // lives: a thread of ONE. Since item 177 a row with replies is the
    // conversation and Delete there takes every message, so the only Delete
    // that writes one message is this one.
    //
    // This test used to run on the ROOT of a three-message thread, because
    // that was the message-scoped case then, and it pinned a defect that came
    // from the mismatch: the toggle asked a thread ROW about its thread's
    // tags, which notmuch gives as a UNION, so deleting the root left the
    // union carrying no `deleted` and a second press ran Delete AGAIN,
    // trash-to-trash, producing `deleted-from:inbox` and
    // `deleted-from:Trash` at once with no way back. Item 177 dissolves the
    // mismatch rather than patching it: the row and the write now agree about
    // what they are for. The trash-to-trash assertions stay, because they are
    // what proves a delete cannot run twice on one message.
    //
    // The row must be left ALONE between the presses: a re-query rebuilds it
    // from the database and hides that class of defect, which is why an
    // earlier version of this probe passed.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("tlone@example.org"),
        QStringLiteral("On its own"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QString root = backed.fixture().maildirPath();
    const QString cfg = backed.fixture().configPath();
    const QString stem = QStringLiteral("tlone.example.org");
    const QString trash = root + QStringLiteral("/acct/Trash/cur");

    // The guard that says this is the message-scoped path at all. With a
    // second message the row would be a conversation and Delete would take
    // the thread, which is a different test.
    const QModelIndex row = model->index(0, 0, QModelIndex());
    QVERIFY2(!model->isConversationRow(row),
             "the fixture's row is a conversation, so Delete is thread-scoped "
             "here and this test asserts nothing about a lone message");

    view->setCurrentIndex(row);
    QApplication::processEvents();
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(folderHasMessageFile(trash, stem), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, QStringLiteral("id:tlone@example.org and "
                                         "tag:\"deleted-from:inbox\"")) == 1,
        15000);

    // There is no second press to make any more, and that is the point.
    //
    // Item 16's double-press-to-undelete existed because the deleted row
    // STAYED in the view with nothing else to act on. Since 2026-08-26 Delete
    // strips `inbox` too, so in this `tag:inbox` view the row LEAVES: the
    // mitigation is unreachable here because the thing it mitigated is gone.
    // Confirmed with the user, who chose this over keeping the toggle.
    //
    // Undo is what retracts now, and it must put back BOTH halves: the file
    // and the tag travelled in one TagChange precisely so one Ctrl+Z returns
    // them together.
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 0, 15000);

    window.findChild<QAction *>(QStringLiteral("undo"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"), stem)
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                    stem),
        15000);
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg,
                     QStringLiteral("id:tlone@example.org and tag:deleted"))
            == 0,
        15000);

    // The tag half of the same undo. Asserted separately because the file
    // moving back and `inbox` coming back are two different failures, and a
    // restore that returns the file without the tag is invisible in the view
    // it was returned to.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg,
                     QStringLiteral("id:tlone@example.org and tag:inbox"))
            == 1,
        15000);

    // Asked of notmuch directly: a UI query reads 0 rows for the whole
    // interval before the worker answers, so an absence assertion through the
    // query bar passes against any state of the database.
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:tlone@example.org")), 1);
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:tlone@example.org and "
                                              "tag:\"deleted-from:inbox\"")),
             0);
    // The tag a re-delete would invent. Its presence is the signature of a
    // trash-to-trash move rather than a variation on the origin-tag defects.
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:tlone@example.org and "
                                              "tag:\"deleted-from:Trash\"")),
             0);
    QVERIFY2(!folderHasMessageFile(trash, stem),
             "the message was left in the trash");
}

void TestMainWindow::deleteThreadMovesEveryMessageAndRepaintsTheRootCard()
{
    // Two defects in one gesture, both reported from a hand test.
    //
    // Delete thread never moved anything: it was left calling tagSelected()
    // when Delete became a move, so a whole conversation stayed in the inbox
    // wearing a `deleted` chip, which is the half-deleted state item 103
    // existed to remove. It moves every message now, each carrying its own
    // `deleted-from:` origin so a thread spanning folders reassembles.
    //
    // And the ROOT card did not repaint until it was clicked, while its
    // replies did. A thread-scoped move updated each message's node;
    // applyMessageTagChange() deliberately leaves a multi-message thread's
    // SUMMARY alone, because one message's edit does not describe the
    // conversation. The replies have nodes and repainted; the root card reads
    // the summary and did not. A thread-scoped move DID change every message,
    // so the summary genuinely moves and applyTagChange() is the right update.
    //
    // The stale summary was also why a second press did nothing: the toggle
    // asks the summary for its direction and kept reading "not deleted".
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("dt0@example.org"),
        QStringLiteral("DT root"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Root body.")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("dt1@example.org"),
        QStringLiteral("Re: DT root"), QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Reply one."), true, QStringLiteral("dt0@example.org")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("dt2@example.org"),
        QStringLiteral("Re: DT root"), QStringLiteral("third@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 12:00:00 +0200"),
        QStringLiteral("Reply two."), true, QStringLiteral("dt0@example.org")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QString root = backed.fixture().maildirPath();
    const QString cfg = backed.fixture().configPath();
    const QString trash = root + QStringLiteral("/acct/Trash/cur");
    const QString thread = QStringLiteral("thread:{id:dt0@example.org}");

    // Three messages, so a thread-scoped action is distinguishable from a
    // message-scoped one at all.
    QCOMPARE(notmuchCount(cfg, thread), 3);

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    view->expand(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    // Every message MOVED, not merely tagged. This is the half that was
    // missing entirely: the action tagged and moved nothing.
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(trash, QStringLiteral("dt0.example.org"))
            && folderHasMessageFile(trash, QStringLiteral("dt1.example.org"))
            && folderHasMessageFile(trash, QStringLiteral("dt2.example.org")),
        15000);
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, thread + QStringLiteral(" and tag:deleted")) == 3,
        15000);
    // Each with its own origin, which is what makes the move reversible.
    QCOMPARE(notmuchCount(cfg, thread
                                   + QStringLiteral(" and "
                                                    "tag:\"deleted-from:inbox\"")),
             3);

    // The ROOT CARD's own state, which is what the user watches. Read from the
    // summary because that is what a thread row draws, and it is the value
    // that stayed stale: the replies repainted and the root did not.
    QVERIFY2(model->threadAt(0).tags.contains(QStringLiteral("deleted")),
             "the root card still reads as not deleted, so it paints "
             "undeleted until the row is clicked");

    // Second press restores the whole thread, which only works if the toggle
    // can see the state the first press produced.
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, thread + QStringLiteral(" and tag:deleted")) == 0,
        15000);

    // Home, and nothing left behind in the trash.
    QCOMPARE(notmuchCount(cfg, thread), 3);
    QCOMPARE(notmuchCount(cfg, thread
                                   + QStringLiteral(" and "
                                                    "tag:\"deleted-from:inbox\"")),
             0);
    QVERIFY(!folderHasMessageFile(trash, QStringLiteral("dt0.example.org")));
    QVERIFY(!folderHasMessageFile(trash, QStringLiteral("dt1.example.org")));
    QVERIFY(!folderHasMessageFile(trash, QStringLiteral("dt2.example.org")));
}

void TestMainWindow::aFolderNameWithASpaceSurvivesTheRoundTrip()
{
    // A notmuch tag MAY contain a space, and a Maildir folder name may too.
    // The worker reported each message's tags as one space-joined string, so
    // `deleted-from:Inbox/SlackBuilds users` was split back into
    // "deleted-from:Inbox/SlackBuilds" and "users", and Restore moved the
    // messages to the truncated folder, CREATING it. On the user's real
    // Maildir that put four messages into a directory mbsync does not sync,
    // beside the real folder of 808, and they read as missing.
    //
    // The leftover origin tag was the visible half: the restore stripped the
    // truncated name, which no message carried, so the real tag stayed on.
    //
    // Separator is a TAB now. A tag cannot contain one, since notmuch's own
    // dump format is line-based and whitespace-delimited.
    WorkerBackedWindow backed;
    const QString folder = QStringLiteral("acct/Inbox/SlackBuilds users");
    QVERIFY(backed.fixture().addMessage(
        folder, QStringLiteral("sp0@example.org"), QStringLiteral("SP root"),
        QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Root body.")));
    QVERIFY(backed.fixture().addMessage(
        folder, QStringLiteral("sp1@example.org"),
        QStringLiteral("Re: SP root"), QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Reply."), true, QStringLiteral("sp0@example.org")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QString root = backed.fixture().maildirPath();
    const QString cfg = backed.fixture().configPath();
    const QString thread = QStringLiteral("thread:{id:sp0@example.org}");
    const QString home = root + QLatin1Char('/') + folder;

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, thread + QStringLiteral(" and tag:deleted")) == 2,
        15000);

    // The origin tag carries the WHOLE folder name, space included.
    QCOMPARE(notmuchCount(cfg,
                          thread
                              + QStringLiteral(" and tag:\"deleted-from:"
                                               "Inbox/SlackBuilds users\"")),
             2);

    // Back again.
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, thread + QStringLiteral(" and tag:deleted")) == 0,
        15000);

    // No origin tag left behind. This is the half the user saw: a tag they
    // could see, could not type, and could not remove.
    QCOMPARE(notmuchCount(cfg,
                          thread
                              + QStringLiteral(" and tag:\"deleted-from:"
                                               "Inbox/SlackBuilds users\"")),
             0);
    // Nor a truncated one, which is what a space-split would have written.
    QCOMPARE(notmuchCount(cfg,
                          thread
                              + QStringLiteral(" and tag:\"deleted-from:"
                                               "Inbox/SlackBuilds\"")),
             0);

    // Home, in the folder with the space in its name.
    QVERIFY2(folderHasMessageFile(home + QStringLiteral("/cur"),
                                  QStringLiteral("sp0.example.org")),
             "the root did not come back to the folder it was deleted from");
    QVERIFY2(folderHasMessageFile(home + QStringLiteral("/cur"),
                                  QStringLiteral("sp1.example.org")),
             "the reply did not come back to the folder it was deleted from");

    // And the truncated folder was never created. Its existence is the defect
    // that hid four real messages from the user and from mbsync.
    QVERIFY2(!QDir(root + QStringLiteral("/acct/Inbox/SlackBuilds")).exists(),
             "a folder named after the truncated origin was created, so the "
             "messages are somewhere mbsync will never sync");
}

void TestMainWindow::deleteIsBoundToTheDeleteKey()
{
    // Del is the key a user reaches for, and Ctrl+D is not a guess anyone
    // makes. Both are bound; this asserts the bare one is really there,
    // since setShortcut() keeps only the LAST of several and silently drops
    // the rest, which would leave the documented binding absent.
    const Config config;
    MainWindow window(config);

    auto *action = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(action);

    QVERIFY2(action->shortcuts().contains(QKeySequence(Qt::Key_Delete)),
             qPrintable(QStringLiteral("delete is not on the Del key; it has: %1")
                            .arg(QKeySequence::listToString(action->shortcuts()))));
}

void TestMainWindow::theDeleteKeyEditsTextInTheQueryBar()
{
    // `delete` is bound to bare Del, and a QAction shortcut is dispatched
    // BEFORE the focused widget sees the key. Qt withholds only plain LETTERS
    // from editable widgets, so by the argument that made bare Return break
    // the query bar, Delete should move mail to the trash while the user is
    // editing a query.
    //
    // It does not: QLineEdit accepts the ShortcutOverride for Delete itself,
    // because Delete is one of its own editing keys, which Return is not. That
    // is a property of Qt rather than of this code, which is exactly why it is
    // pinned here: it is the assumption the bare binding rests on, and if a
    // future Qt or a future focus proxy changes it, mail gets deleted while
    // someone types.
    //
    // Asserted on the ACTION not firing, not on the ShortcutOverride phase. A
    // probe on the override reports notify=1 accepted=1 whether or not this
    // window filters the key, since QLineEdit accepts it either way, so it
    // cannot distinguish the two and passes against any implementation.
    // Measured, while trying to write this test the obvious way.
    const Config config;
    MainWindow window(config);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *deleteAction = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(queryEdit && deleteAction);

    int fired = 0;
    QObject::connect(deleteAction, &QAction::triggered,
                     [&fired]() { ++fired; });

    queryEdit->setFocus();
    QTRY_VERIFY(queryEdit->hasFocus());
    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->setCursorPosition(0);

    QTest::keyClick(queryEdit, Qt::Key_Delete);

    QCOMPARE(fired, 0);
    QCOMPARE(queryEdit->text(), QStringLiteral("ag:inbox"));
}

void TestMainWindow::restoreIsReachableWithoutTheKeyboard()
{
    // Restore shipped as a keyboard shortcut and nothing else: registered,
    // iconned, enabled correctly, and present in no menu at all. A user who
    // does not read the changelog would never learn it exists, and Ctrl+R is
    // not a guess anyone makes.
    //
    // The four places an action must touch are enforced by tests
    // (knownActions, defaultBindings, the icon table); being REACHABLE is a
    // fifth that nothing checked, which is why the gap survived a green suite.
    const Config config;
    MainWindow window(config);

    auto *restore = window.findChild<QAction *>(QStringLiteral("restore"));
    QVERIFY(restore);

    const auto menuContains = [](const QMenu *menu, const QAction *action) {
        return menu && menu->actions().contains(action);
    };

    // A menu on the MENU BAR, beside Delete whose inverse it is. The context
    // menu is excluded here so this assertion cannot be satisfied by the one
    // the next assertion checks: findChildren finds both.
    auto *context =
        window.findChild<QMenu *>(QStringLiteral("threadContextMenu"));
    QVERIFY(context);

    bool inAMenuBarMenu = false;
    for (const QMenu *menu : window.findChildren<QMenu *>()) {
        if (menu != context && menuContains(menu, restore)) {
            inAMenuBarMenu = true;
            break;
        }
    }
    QVERIFY2(inAMenuBarMenu,
             "Restore is in no menu-bar menu, so a user browsing the menus "
             "would never learn it exists");

    // And the thread list's context menu, which is where the other
    // message-scoped actions are reached by mouse.
    QVERIFY2(menuContains(context, restore),
             "Restore is missing from the thread context menu");
}

void TestMainWindow::restoreIsOnlyEnabledInTheTrashView()
{
    // Restore has no meaning outside the trash, and an enabled action that
    // does nothing is worse than an absent one.
    //
    // Enabled from the QUERY rather than from the selection's tags: a message
    // trashed by another client carries no tag of ours and must still be
    // restorable, which is the whole reason the trash view is path-based.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("re1@example.org"),
        QStringLiteral("In the inbox"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/Trash"), QStringLiteral("re2@example.org"),
        QStringLiteral("In the trash"), QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *restore = window.findChild<QAction *>(QStringLiteral("restore"));
    QVERIFY(model && queryEdit);
    QVERIFY2(restore, "there is no restore action");

    // An ordinary view. Both fixture messages carry `inbox`, since the
    // fixture tags all new mail that way regardless of folder, so this is two
    // rows rather than one; the count is not what is under test.
    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 2, 15000);
    QVERIFY2(!restore->isEnabled(),
             "Restore is enabled in an ordinary view, where it means nothing");

    // The trash view, which is the account's own generated trash query.
    queryEdit->setText(QStringLiteral("path:\"acct/Trash/**\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    QVERIFY2(restore->isEnabled(),
             "Restore is disabled in the trash view, where it is the point");
}

void TestMainWindow::restoreReturnsAMessageToItsOriginFolder()
{
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("ro1@example.org"),
        QStringLiteral("Send me back"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    const QString root = backed.fixture().maildirPath();
    const QString cfg = backed.fixture().configPath();
    const QString stem = QStringLiteral("ro1.example.org");

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"), stem),
        15000);

    // Now from the trash view, through Restore rather than through a second
    // Delete: this is the action the user reaches for when browsing trash.
    queryEdit->setText(QStringLiteral("path:\"acct/Trash/**\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("restore"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"), stem)
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                    stem),
        15000);
    // Waited on the ORIGIN tag, not on `deleted`.
    //
    // Both come off in one write, but the file rename and the tag write are
    // separate operations and the assertions below raced the second one:
    // measured 1 failure in 3 runs waiting on `deleted` alone, reporting the
    // origin tag still present. Waiting on the tag this test is actually about
    // removes the race rather than papering over it with a longer timeout.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, QStringLiteral("id:ro1@example.org and "
                                         "tag:\"deleted-from:inbox\"")) == 0,
        15000);
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg,
                     QStringLiteral("id:ro1@example.org and tag:deleted")) == 0,
        15000);

    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:ro1@example.org")), 1);
    QVERIFY(!folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"),
                                  stem));

    // And the `inbox` TAG came back with it. Delete strips that tag so the
    // message leaves the Inbox view, which makes restoring it the other half
    // of the same change: without it the message sits in the inbox FOLDER
    // carrying no `inbox` tag and the Inbox view cannot see it, which reads as
    // "I restored it and it is gone".
    //
    // The file assertions above all passed while this was broken: the folder
    // comparison that decides it was made against the finished TAG rather than
    // against the destination folder, so it was always false. Nothing else
    // here would have noticed.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, QStringLiteral("id:ro1@example.org and tag:inbox"))
            == 1,
        15000);
}

void TestMainWindow::restoreFallsBackToInboxWithoutAnOriginTag()
{
    // A message trashed by ANOTHER client: it sits in the trash folder and
    // carries no `deleted-from:` tag, because nothing here put it there. The
    // real Maildir has such messages, which is why the trash view is path
    // based rather than tag based.
    //
    // Inbox is the documented fallback. Refusing to move it would leave the
    // user with a message they can see in the trash and cannot get out.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/Trash"), QStringLiteral("foreign@example.org"),
        QStringLiteral("Trashed elsewhere"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    const QString root = backed.fixture().maildirPath();
    const QString cfg = backed.fixture().configPath();
    const QString stem = QStringLiteral("foreign.example.org");

    // The guard this test needs: no origin tag, so the fallback is what is
    // under test rather than an ordinary restore.
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:foreign@example.org and "
                                              "tag:\"deleted-from:inbox\"")),
             0);

    queryEdit->setText(QStringLiteral("path:\"acct/Trash/**\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("restore"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"), stem)
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                    stem),
        15000);
    QVERIFY2(!folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"),
                                   stem),
             "the message was copied out of the trash rather than moved");
}

void TestMainWindow::undoMovesTheMessageBack()
{
    // Undo is this project's answer to the confirmation dialog it rules out,
    // so a delete that cannot be undone is a delete with no safety net at all.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("del3@example.org"),
        QStringLiteral("Put me back"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QString root = backed.fixture().maildirPath();
    const QString stem = QStringLiteral("del3.example.org");
    const QString trash = root + QStringLiteral("/acct/Trash/cur");

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(folderHasMessageFile(trash, stem), 15000);

    window.findChild<QAction *>(QStringLiteral("undo"))->trigger();

    // Back in the EXACT folder it came from. A move-back that guessed "inbox"
    // for every account would pass a laxer assertion than this one.
    //
    // cur/, not the new/ it started in: a file coming back from the trash has
    // been read, and re-announcing it as fresh mail is worse than the flag
    // change.
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"), stem)
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                    stem),
        15000);
    QVERIFY2(!folderHasMessageFile(trash, stem),
             "undo restored the file and left a copy in the trash");

    // Both tags gone, asked of the database. `deleted-from:` left behind would
    // make Restore offer to move a message that is already home.
    queryEdit->setText(QStringLiteral(
        "id:del3@example.org and (tag:deleted or tag:\"deleted-from:inbox\")"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 0, 15000);
    // The guard the assertion above needs: a query that matches nothing
    // because the message vanished would pass it too.
    queryEdit->setText(QStringLiteral("id:del3@example.org"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
}

void TestMainWindow::deleteOnAReplyMovesThatReplyOnly()
{
    // The reply case. A test asserting on a root selection is the one case
    // where the wrong resolution is accidentally right, so a mutation on this
    // path stays green without it.
    //
    // Put under the SECOND thread, so the wrong answer is plausible rather
    // than accidentally correct.
    WorkerBackedWindow backed;
    NotmuchFixture &fx = backed.fixture();
    QVERIFY(fx.addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("other@example.org"),
        QStringLiteral("An unrelated thread"),
        QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 09:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY(fx.addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("rootof@example.org"),
        QStringLiteral("A conversation"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY(fx.addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("reply@example.org"),
        QStringLiteral("Re: A conversation"),
        QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Reply body."), true,
        QStringLiteral("rootof@example.org")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 2, 15000);

    // Whichever row holds the conversation. The sort is the model's business,
    // so this asks rather than assuming.
    QModelIndex conversation;
    for (int row = 0; row < model->rowCount(QModelIndex()); ++row) {
        const QModelIndex index = model->index(row, 0, QModelIndex());
        if (model->threadAt(row).totalCount > 1) {
            conversation = index;
            break;
        }
    }
    QVERIFY2(conversation.isValid(), "no multi-message thread in the list");

    view->expand(conversation);
    // Both messages are children since item 177: the conversation lists its
    // first message under itself too.
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(conversation) == 2, 15000);

    // The reply is child 1, behind the thread's first message.
    const QModelIndex replyIndex = model->index(1, 0, conversation);
    QVERIFY(model->isMessageRow(replyIndex));
    QCOMPARE(model->messageAt(replyIndex).messageId,
             QStringLiteral("reply@example.org"));

    view->setCurrentIndex(replyIndex);
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    const QString root = fx.maildirPath();
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"),
                             QStringLiteral("reply.example.org")),
        15000);

    // Only that reply. Escalating a message-scoped delete to its thread would
    // move the root as well, which is the failure worth naming: the user
    // deleted one reply and lost the conversation.
    QVERIFY2(!folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"),
                                   QStringLiteral("rootof.example.org")),
             "deleting a reply moved its thread's root as well");
}

void TestMainWindow::undoingADeleteConsumesItsCommandRatherThanPushingAnother()
{
    // A move is confirmed through onMessagesMoved(), and so is the move an
    // UNDO makes. Pushing a command there unconditionally meant undo left a
    // fresh command on the stack instead of consuming the one it undid, so
    // the stack grew on every press: "Delete", "Undo Delete", "Undo Undo
    // Delete". A user pressing undo twice to be sure re-deleted the mail they
    // had just rescued, which is the opposite of what undo is for here, undo
    // being this project's stand-in for a confirmation dialog.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("undo2@example.org"),
        QStringLiteral("Undo twice"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    const QString root = backed.fixture().maildirPath();
    const QString stem = QStringLiteral("undo2.example.org");
    const QString trash = root + QStringLiteral("/acct/Trash/cur");
    const auto inInbox = [&] {
        return folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"),
                                    stem)
               || folderHasMessageFile(
                   root + QStringLiteral("/acct/inbox/new"), stem);
    };

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(folderHasMessageFile(trash, stem), 15000);

    // The guard the assertions below need: one command, from the delete.
    QTRY_VERIFY_WITH_TIMEOUT(window.undoDepthForTesting() == 1, 15000);

    window.findChild<QAction *>(QStringLiteral("undo"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(inInbox(), 15000);

    // The stack is spent. Asserted on undoText rather than depth alone
    // because a command that is merely marked done still reports its text,
    // and it is the text the user reads off the Edit menu.
    QTRY_VERIFY_WITH_TIMEOUT(window.undoTextForTesting().isEmpty(), 15000);

    // And the real point: pressing undo again must not move the message
    // anywhere. Before the fix this put it straight back in the trash.
    window.findChild<QAction *>(QStringLiteral("undo"))->trigger();
    QTest::qWait(1500);
    QVERIFY2(!folderHasMessageFile(trash, stem),
             "a second undo re-deleted the message the first one restored");
    QVERIFY2(inInbox(), "a second undo moved the message out of the inbox");
}

void TestMainWindow::aDeleteHeldDuringASyncCountsAsUnsyncedWork()
{
    // pendingEditCount() summed the held TAG edits and not the held MOVES, so
    // a Delete pressed during a sync left the count at zero: the indicator
    // stayed hidden and closeEvent()'s `pendingEditCount() > 0` guard never
    // fired, discarding the move on quit with no prompt. That is item 106's
    // data loss with a worse shape, since a dropped move leaves the file in
    // the folder the user asked it out of.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("held1@example.org"),
        QStringLiteral("Held by a sync"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *label = window.findChild<QLabel *>(QStringLiteral("pendingEdits"));
    QVERIFY(model && view && queryEdit && label);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    // Hidden before the gesture, so the assertion after it means something.
    QVERIFY2(label->isHidden(), "the pending indicator was already showing");

    // A sync now holds the write lock, which is what makes the move held
    // rather than sent.
    QMetaObject::invokeMethod(&window, "onExternalSyncStateChanged",
                              Q_ARG(SyncMonitor::State,
                                    SyncMonitor::State::Running));

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    QVERIFY2(!label->isHidden(),
             "a Delete held by a sync did not count as unsynced work, so "
             "quitting would have discarded it with no prompt");

    // The file really is still where it was: this is a HELD move, not a
    // failed one, and the indicator would be meaningless otherwise.
    const QString root = backed.fixture().maildirPath();
    QVERIFY(!folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"),
                                  QStringLiteral("held1.example.org")));
}

void TestMainWindow::twoDeletesToOneTrashBothGetTheirTags()
{
    // The pending-move table was keyed on the destination folder, so two
    // Deletes in one account before the first confirmation arrived both named
    // `acct/Trash`: the second insert overwrote the first and the second
    // confirmation took an empty entry. That file reached the trash carrying
    // neither `deleted` nor `deleted-from:`, which makes it unrestorable by
    // Restore and invisible to a `tag:deleted` query.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("two1@example.org"),
        QStringLiteral("First"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("two2@example.org"),
        QStringLiteral("Second"), QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 2, 15000);

    // Both Deletes issued back to back, WITHOUT waiting for the first to be
    // confirmed. That is the whole point: waiting would serialise them and
    // the keyed table would have coped.
    //
    // Both take row 0, and that is not a typo. Delete strips `inbox`, and in
    // this `tag:inbox` view the row it stripped it from LEAVES the list
    // immediately, so what was row 1 becomes row 0 the moment the first
    // Delete is triggered. Naming index(1, 0) here would select a row that no
    // longer exists and the second message would never be deleted at all,
    // which is exactly how this test failed when the removal was added.
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    const QString root = backed.fixture().maildirPath();
    const QString trash = root + QStringLiteral("/acct/Trash/cur");
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(trash, QStringLiteral("two1.example.org"))
            && folderHasMessageFile(trash, QStringLiteral("two2.example.org")),
        15000);

    // Both carry BOTH tags, asked of the database rather than of the model:
    // the defect was a write that never happened, and the model would have
    // shown the optimistic state either way.
    queryEdit->setText(QStringLiteral(
        "tag:deleted and tag:\"deleted-from:inbox\" and "
        "(id:two1@example.org or id:two2@example.org)"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 2, 15000);
}

void TestMainWindow::deleteWithoutATrashFolderSaysSoRatherThanDoingNothing()
{
    // Task 2 warns at config load. This is the second line of defence: a key
    // the user never fixed must not leave Delete silently inert.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("notrash@example.org"),
        QStringLiteral("Nowhere to go"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    // No trash key, which is what this is about.
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusMessage"));
    QVERIFY(model && view && queryEdit && status);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));

    // Cleared FIRST, so the assertion below cannot be satisfied by whatever
    // the query left behind. Without this the test passes against a Delete
    // that says nothing at all, which is exactly what it exists to catch: it
    // did, before the implementation landed.
    status->clear();
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    QVERIFY2(status->text().contains(QStringLiteral("trash")),
             qPrintable(QStringLiteral(
                            "Delete with no trash folder configured said: '%1'")
                            .arg(status->text())));

    // And it did not tag the message either. A `deleted` tag with the file
    // still in the inbox is exactly the half-done state item 103 removes.
    const QString mail = backed.fixture().maildirPath();
    QVERIFY(folderHasMessageFile(mail + QStringLiteral("/acct/inbox/new"),
                                 QStringLiteral("notrash.example.org"))
            || folderHasMessageFile(mail + QStringLiteral("/acct/inbox/cur"),
                                    QStringLiteral("notrash.example.org")));
}

void TestMainWindow::theCleanupQueryFindsStrandedMail()
{
    // The state 848 real messages are in today: tagged `deleted` by a version
    // of Delete that only ever tagged, with the file still sitting in the
    // inbox. Nothing moves them on their own, so the action reports them and
    // the user decides.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("strand@example.org"),
        QStringLiteral("Tagged but never moved"),
        QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("keep@example.org"),
        QStringLiteral("Perfectly ordinary mail"),
        QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    const QString cfg = backed.fixture().configPath();
    QVERIFY(notmuchTag(cfg, { QStringLiteral("+deleted"),
                              QStringLiteral("--"),
                              QStringLiteral("id:strand@example.org") }));
    // The guard, before anything is asserted about what the action finds: one
    // message is stranded and one is not, so a query that simply returns
    // everything cannot pass.
    QCOMPARE(notmuchCount(cfg, QStringLiteral("tag:deleted")), 1);

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *cleanup =
        window.findChild<QAction *>(QStringLiteral("cleanup_stranded"));
    QVERIFY(model && queryEdit);
    QVERIFY2(cleanup, "there is no cleanup_stranded action");

    cleanup->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    // The query lands in the bar, like every other generated query, so what
    // ran is visible and the user can edit it.
    QVERIFY2(queryEdit->text().contains(QStringLiteral("tag:deleted")),
             qPrintable(QStringLiteral("the bar holds '%1'")
                            .arg(queryEdit->text())));
    QVERIFY2(queryEdit->text().contains(QStringLiteral("not ")),
             qPrintable(QStringLiteral("the bar holds '%1'")
                            .arg(queryEdit->text())));

    // It reports and moves NOTHING. A cleanup that acted on its own would be a
    // bulk delete with no selection behind it, which is the opposite of what
    // the user asked for.
    const QString mail = backed.fixture().maildirPath();
    QVERIFY(folderHasMessageFile(mail + QStringLiteral("/acct/inbox/new"),
                                 QStringLiteral("strand.example.org"))
            || folderHasMessageFile(mail + QStringLiteral("/acct/inbox/cur"),
                                    QStringLiteral("strand.example.org")));
    QCOMPARE(notmuchCount(cfg, QStringLiteral("path:\"acct/Trash/**\"")), 0);
}

void TestMainWindow::theCleanupQueryExcludesMailAlreadyInTrash()
{
    // Properly trashed mail carries the tag AND sits in the folder. Without
    // the exclusion this reports every deleted message ever, which makes the
    // action useless the moment Delete starts working.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("cln1@example.org"),
        QStringLiteral("Going to the trash"),
        QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    const QString cfg = backed.fixture().configPath();
    const QString mail = backed.fixture().maildirPath();

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    // Asked of the database, never of the list: rowCount() reads 0 for the
    // whole interval before the worker answers, so "the cleanup found
    // nothing" would pass against a delete that never happened.
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(mail + QStringLiteral("/acct/Trash/cur"),
                             QStringLiteral("cln1.example.org")),
        15000);
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, QStringLiteral("tag:deleted")) == 1, 15000);

    auto *cleanup =
        window.findChild<QAction *>(QStringLiteral("cleanup_stranded"));
    QVERIFY2(cleanup, "there is no cleanup_stranded action");
    cleanup->trigger();

    // The query the action ran, asked of notmuch directly. The list is the
    // wrong instrument for an emptiness claim, for the reason above.
    QTRY_VERIFY_WITH_TIMEOUT(!queryEdit->text().isEmpty(), 15000);
    QCOMPARE(notmuchCount(cfg, queryEdit->text()), 0);
}

void TestMainWindow::aMoveThatRelocatesNothingWritesNoTag()
{
    // The spec's ordering bullet, at the UI level: a failed rename must leave
    // no tag. The worker half is moveMessagesReportsOnlyWhatMoved(); this is
    // the other half, that the window writes tags only for what the worker
    // reported as actually moved.
    //
    // The failure is provoked by making the destination unwritable, which is
    // the closest thing to a failed rename that a test can arrange without
    // stubbing the worker. A tag written anyway would be the exact half-done
    // state item 103 exists to remove: a message marked deleted, with its file
    // still in the inbox and its origin tag lying about where it went.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("nomove@example.org"),
        QStringLiteral("Cannot be moved"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    const QString root = backed.fixture().maildirPath();
    const QString cfg = backed.fixture().configPath();

    // The trash as a FILE where the folder must be, so creating the Maildir
    // subdirectories under it cannot succeed. A read-only directory would be
    // ignored by a test running as root, which this one must not depend on.
    QFile blocker(root + QStringLiteral("/acct/Trash"));
    QVERIFY2(blocker.open(QIODevice::WriteOnly),
             "could not put a file where the trash folder would go");
    blocker.close();

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    // The guard, so this cannot pass by the delete never having been
    // attempted: the message is still there and still findable afterwards.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, QStringLiteral("id:nomove@example.org")) == 1, 15000);

    // A tag write is a round trip, so an immediate read would pass against a
    // write still in flight. Given time to arrive, then asserted absent.
    QTest::qWait(1500);
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:nomove@example.org and "
                                              "tag:deleted")), 0);
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:nomove@example.org and "
                                              "tag:\"deleted-from:inbox\"")), 0);

    // And the file never left.
    QVERIFY(folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                 QStringLiteral("nomove.example.org"))
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"),
                                    QStringLiteral("nomove.example.org")));
}

void TestMainWindow::restoringFromTheTrashViewRefreshesTheList()
{
    // Reported from a hand test: Restore moved the message correctly and the
    // row it came from sat in the trash list until the Trash filter was
    // clicked again.
    //
    // The trash view is PATH based, so a restored message no longer matches
    // the query the list was built from. That is a state no tag change can
    // express: onMessagesMoved() updates tags and the undo stack and never
    // removes a row, which is right in an ordinary view (a deleted message's
    // card should stay put) and wrong here, where the row is the one thing
    // that is now false.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/Trash"), QStringLiteral("refr1@example.org"),
        QStringLiteral("Restore me"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    const QString root = backed.fixture().maildirPath();
    const QString stem = QStringLiteral("refr1.example.org");

    // The account's own generated trash query, which is what the Trash filter
    // puts in the bar.
    queryEdit->setText(QStringLiteral("path:\"acct/Trash/**\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("restore"))->trigger();

    // The move really happened, waited on the FILE. Without this the row
    // assertion below could pass against a restore that never ran.
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"), stem)
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                    stem),
        15000);

    // And the list no longer shows it, without the user touching anything.
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 0, 15000);
}

void TestMainWindow::theRefreshAfterARestoreLeavesUndoIntact()
{
    // The refresh that fixes the stale trash row runs immediately after the
    // undo entry is pushed, so it must not be the thing that destroys it.
    // runCurrentQuery() clears the undo stack outright, which would make
    // Restore the one mutation in the window with no way back; this asserts
    // the non-destructive refresh was used and stayed non-destructive.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/Trash"), QStringLiteral("undoref@example.org"),
        QStringLiteral("Restore then undo"),
        QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    const QString root = backed.fixture().maildirPath();
    const QString stem = QStringLiteral("undoref.example.org");

    queryEdit->setText(QStringLiteral("path:\"acct/Trash/**\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("restore"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/inbox/cur"), stem)
            || folderHasMessageFile(root + QStringLiteral("/acct/inbox/new"),
                                    stem),
        15000);
    // The refresh has run by now, which is what the row count proves.
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 0, 15000);

    // And the undo entry is still there afterwards.
    auto *undo = window.findChild<QAction *>(QStringLiteral("undo"));
    QVERIFY(undo);
    QVERIFY2(undo->isEnabled(),
             "the refresh after a restore cleared the undo stack");

    undo->trigger();

    // Back in the trash, asserted on the FILE: the undo has to move it, not
    // merely re-tag it.
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"), stem),
        15000);
}

void TestMainWindow::deletingOutsideTheTrashViewLeavesTheRowInPlace()
{
    // The other half of the trash-view refresh, and the reason it is gated.
    //
    // A Delete is a move too and reaches the same confirmation slot. Refreshing
    // on every move would make the row vanish from under the user in every
    // ordinary view, which this project has decided against twice: the card
    // deliberately stays put, because one deleted message does not doom the
    // conversation and a row disappearing mid-gesture loses the user's place.
    //
    // Nothing asserted this, so a mutation dropping the isShowingTrash() gate
    // passed the whole suite.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("stay1@example.org"),
        QStringLiteral("Stay on screen"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body text.")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    const QString root = backed.fixture().maildirPath();
    const QString stem = QStringLiteral("stay1.example.org");

    // An ORDINARY view that the message STOPS MATCHING once the delete lands.
    // Both halves matter and the first draft of this test had only one: a
    // `tag:inbox` view looks ordinary but Delete adds `deleted` and the origin
    // tag and removes nothing, so the message keeps `inbox` and keeps matching.
    // A refresh there is a no-op, and the mutation dropping the
    // isShowingTrash() gate passed against it.
    //
    // A path query on the inbox folder is the honest instrument: the file
    // really leaves that folder, so the row survives only because nothing
    // refreshed.
    queryEdit->setText(QStringLiteral("path:\"acct/inbox/**\""));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    window.findChild<QAction *>(QStringLiteral("delete"))->trigger();

    // The delete really happened, waited on the file rather than on the list.
    QTRY_VERIFY_WITH_TIMEOUT(
        folderHasMessageFile(root + QStringLiteral("/acct/Trash/cur"), stem),
        15000);

    // A refresh is a queued round trip, so an immediate read would pass against
    // one still in flight. Given time to arrive, then asserted not to have
    // taken the row away.
    QTest::qWait(1500);
    QCOMPARE(model->rowCount(QModelIndex()), 1);
}

// ---------------------------------------------------------------------------
// ComposeWindow, item 123.
//
// The composer owns widgets and nothing else here does, which is why its cases
// live in this file. What is asserted is deliberately NOT what it looks like:
// the autosave dirty check, the banner state, the message its widgets produce,
// the format edits and the seeding rules, all of which are observable without
// a painter. CLAUDE.md's "Rendering probes lie" section covers why counting
// pixels here would prove nothing.
// ---------------------------------------------------------------------------

namespace {

/// A Config written to a temporary INI, plus a Maildir root to write into.
///
/// No notmuch database and no worker: the composer never touches
/// NotmuchWorker, so building one would only cost every case a `notmuch new`.
/// The mail root is passed to ComposeWindow explicitly, exactly as MainWindow
/// passes what the worker reported (item 124: it is NOT database.path).
class ComposeFixture
{
public:
    /// `drafts` and `sent` are written only when non-empty, so a test can
    /// build the account-without-a-drafts-folder case by passing an empty
    /// string rather than by needing a second fixture.
    /// `secondAccount` writes a SECOND sending account, which is what makes
    /// the From dropdown have something to choose between. Off by default:
    /// every other case here wants exactly one, so a two-account fixture
    /// everywhere would let a test pass by picking the only entry there is.
    bool build(const QString &drafts = QStringLiteral("Drafts"),
               const QString &sent = QStringLiteral("Sent"),
               const QString &extraCompose = QString(),
               bool secondAccount = false)
    {
        if (!m_confDir.isValid() || !m_mailDir.isValid())
            return false;

        const QString path = m_confDir.filePath(QStringLiteral("qtmaildir.conf"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        {
            QTextStream out(&file);
            // QSettings reads `/` in a section name as a group separator, so
            // the section is [account.acct], never [account/acct].
            out << "[account.acct]\n"
                << "name=Test User\n"
                << "address=user@example.org\n"
                << "maildir=acct\n"
                << "trash=Trash\n";
            if (!drafts.isEmpty())
                out << "drafts=" << drafts << "\n";
            if (!sent.isEmpty())
                out << "sent=" << sent << "\n";
            // A command that exists and does nothing. canSend() is what the
            // From dropdown filters on, so an account without this one line
            // would not appear in it at all.
            out << "send_command=/bin/true\n";
            if (secondAccount) {
                out << "\n[account.other]\n"
                    << "name=Other User\n"
                    << "address=other@example.org\n"
                    << "maildir=other\n"
                    << "trash=Trash\n"
                    << "drafts=Drafts\n"
                    << "sent=Sent\n"
                    << "send_command=/bin/true\n";
            }
            out << "\n[compose]\n";
            if (!extraCompose.isEmpty())
                out << extraCompose << "\n";
        }
        file.close();

        m_config.load(path);
        return true;
    }

    const Config &config() const { return m_config; }
    QString mailRoot() const { return m_mailDir.path(); }

    /// The account's drafts folder, as the composer will resolve it.
    QString draftsCur() const
    {
        return m_mailDir.path() + QStringLiteral("/acct/Drafts/cur");
    }

    /// The second account's drafts folder.
    QString otherDraftsCur() const
    {
        return m_mailDir.path() + QStringLiteral("/other/Drafts/cur");
    }

    /// How many message files sit in the drafts folder.
    int draftCount() const
    {
        return QDir(draftsCur(), {}, QDir::Name, QDir::Files).count();
    }

private:
    QTemporaryDir m_confDir;
    QTemporaryDir m_mailDir;
    Config m_config;
};

/// A minimal New-message context for the fixture's one account.
ComposeContext newContext()
{
    ComposeContext context;
    context.accountKey = QStringLiteral("acct");
    context.kind = ComposeContext::Kind::New;
    return context;
}

}  // namespace

void TestMainWindow::theComposerSplitsItsToolbarByScope()
{
    // Items 142, 143 and 144. One row carried three scopes: text formatting,
    // message composition and the terminal action. The user read it as a menu
    // bar that is not one.
    ComposeFixture fixture;
    QVERIFY(fixture.build());
    ComposeContext context = newContext();
    ComposeWindow window(context, fixture.config(), fixture.mailRoot());

    auto *editorBar =
        window.findChild<QToolBar *>(QStringLiteral("formatToolbar"));
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *subject = window.findChild<QLineEdit *>(QStringLiteral("subject"));
    QVERIFY(editorBar && body && subject);

    // Directly above the text it formats, and below Subject, which belongs to
    // the message rather than to the text.
    auto *central = window.centralWidget();
    QVERIFY(central);
    auto *column = qobject_cast<QVBoxLayout *>(central->layout());
    QVERIFY2(column, "the composer is not laid out in a vertical column");

    // The editor sits inside a QSplitter since item 171, so its position in
    // the column is the SPLITTER's: a forward puts the forwarded message
    // beside the editor, and the toolbar must stay above both. Walking up to
    // whichever child of the column contains the body keeps this test about
    // the toolbar's position rather than about the editor's parentage.
    const auto columnChildOf = [column](QWidget *widget) {
        for (QWidget *w = widget; w; w = w->parentWidget()) {
            for (int i = 0; i < column->count(); ++i) {
                if (column->itemAt(i)->widget() == w)
                    return i;
            }
        }
        return -1;
    };

    const int barIndex = columnChildOf(editorBar);
    const int bodyIndex = columnChildOf(body);
    QVERIFY2(barIndex >= 0 && bodyIndex >= 0,
             "the editor bar or the body is not in the composer's column");
    QVERIFY2(barIndex < bodyIndex, "the editor bar is not above the editor");

    // Send is NOT in that row any more: it is the terminal action and sits by
    // the headers, where its weight belongs.
    auto *send = window.findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(send);
    QVERIFY2(!editorBar->actions().contains(send),
             "Send still shares the editor bar with the formatting buttons");

    auto *sendButton =
        window.findChild<QToolButton *>(QStringLiteral("sendButton"));
    QVERIFY2(sendButton, "there is no Send button beside the headers");
    QCOMPARE(sendButton->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
    QVERIFY2(sendButton->defaultAction() == send,
             "the Send button does not carry the send action itself");

    // A SQUARE, and not a widget that stretches. An Expanding vertical policy
    // grew it to the full height of the header form beside it while the icon
    // and label kept their natural sizes, leaving the two marooned at either
    // end of a tall rectangle with a gap between them. Nothing in the layout
    // or the actions could see that, which is why it is asserted here.
    QCOMPARE(sendButton->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    QCOMPARE(sendButton->width(), sendButton->height());

    // And the icon is the larger half of the button, not a small mark with
    // the label doing the work.
    QVERIFY2(sendButton->iconSize().width() >= 24,
             qPrintable(QStringLiteral("the Send icon is %1px, too small to "
                                       "read as the button's subject")
                            .arg(sendButton->iconSize().width())));

    // Item 143: the formatting buttons carry icons, and keep their words as
    // the tooltip so nothing becomes unnameable in an icon-only row.
    for (const QString &name : { QStringLiteral("format_bold"),
                                 QStringLiteral("format_italic"),
                                 QStringLiteral("format_link") }) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action %1").arg(name)));
        QVERIFY2(!action->toolTip().isEmpty(),
                 qPrintable(QStringLiteral("%1 has no tooltip, so an "
                                           "icon-only button cannot be named")
                                .arg(name)));
    }

    // Item 144: the HTML toggle sits at the right of the same row, apart from
    // the formatting buttons, since it is not one.
    auto *sendHtml =
        window.findChild<QAbstractButton *>(QStringLiteral("sendHtml"));
    QVERIFY(sendHtml);
}

void TestMainWindow::ccAndBccHideBehindADisclosure()
{
    // Item 145. Most messages address neither, so two of four header rows sat
    // empty on every composer.
    ComposeFixture fixture;
    QVERIFY(fixture.build());
    ComposeContext context = newContext();
    context.cc.clear();

    ComposeWindow window(context, fixture.config(), fixture.mailRoot());
    auto *cc = window.findChild<QLineEdit *>(QStringLiteral("cc"));
    auto *bcc = window.findChild<QLineEdit *>(QStringLiteral("bcc"));
    auto *disclosure = window.findChild<QAbstractButton *>(
        QStringLiteral("ccBccDisclosure"));
    QVERIFY(cc && bcc);
    QVERIFY2(disclosure, "there is no Cc/Bcc disclosure beside To:");

    QVERIFY2(cc->isHidden(), "Cc is shown on a composer that carries none");
    QVERIFY2(bcc->isHidden(), "Bcc is shown on a composer that carries none");

    disclosure->click();
    QVERIFY2(!cc->isHidden() && !bcc->isHidden(),
             "the disclosure did not reveal Cc and Bcc");

    disclosure->click();
    QVERIFY2(cc->isHidden() && bcc->isHidden(),
             "the disclosure did not hide Cc and Bcc again");
}

void TestMainWindow::ccAndBccAreRevealedWhenTheyCarryAValue()
{
    // The load-bearing half of item 145, and the reason the disclosure is not
    // simply "start collapsed". A hidden field holding an address is a message
    // going somewhere the sender cannot see, which is worse than the clutter
    // the disclosure removes. A reply carrying Cc, or a reopened draft, must
    // show what it is addressed to.
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    {
        ComposeContext context = newContext();
        context.cc = { QStringLiteral("someone@example.org") };

        ComposeWindow window(context, fixture.config(), fixture.mailRoot());
        auto *cc = window.findChild<QLineEdit *>(QStringLiteral("cc"));
        auto *bcc = window.findChild<QLineEdit *>(QStringLiteral("bcc"));
        QVERIFY(cc && bcc);
        QVERIFY2(!cc->isHidden(),
                 "a seeded Cc is hidden, so the message goes somewhere the "
                 "sender cannot see");
        // Both together: they are one disclosure, and revealing half of it
        // would leave Bcc hidden while Cc is visible for no stated reason.
        QVERIFY2(!bcc->isHidden(),
                 "Cc was revealed without Bcc, though they share a disclosure");
    }

    // ComposeContext carries no bcc at all: a reply never inherits one, so
    // the only way a fresh composer starts with a Bcc is a reopened draft,
    // which fills the widget rather than the context. That is also the case
    // where hiding it matters most, since a Bcc is the value a sender is
    // least able to notice missing.
    {
        ComposeContext context = newContext();
        context.cc.clear();

        ComposeWindow window(context, fixture.config(), fixture.mailRoot());
        auto *bcc = window.findChild<QLineEdit *>(QStringLiteral("bcc"));
        auto *cc = window.findChild<QLineEdit *>(QStringLiteral("cc"));
        QVERIFY(bcc && cc);
        QVERIFY(bcc->isHidden());

        // Setting the text is what a draft load does.
        bcc->setText(QStringLiteral("hidden@example.org"));
        window.revealCcBccIfUsed();
        QVERIFY2(!bcc->isHidden(),
                 "a Bcc filled after construction stayed hidden");
        QVERIFY2(!cc->isHidden(),
                 "Bcc was revealed without Cc, though they share a disclosure");
    }
}

void TestMainWindow::removeAttachmentAppearsOnlyWithAttachments()
{
    // The user's placement: Remove sits with the list it acts on, and a
    // control that can never do anything should not be on screen at all.
    ComposeFixture fixture;
    QVERIFY(fixture.build());
    ComposeContext context = newContext();
    ComposeWindow window(context, fixture.config(), fixture.mailRoot());

    auto *detach = window.findChild<QAction *>(QStringLiteral("compose_detach"));
    auto *list = window.findChild<QListWidget *>(QStringLiteral("attachments"));
    QVERIFY(detach && list);

    // isVisible() on the LIST, not isHidden(): the list is not hidden in its
    // own right, its parent row is, and a child of a hidden parent reports
    // isHidden() false while being just as invisible. Asserting the wrong one
    // fails against correct code.
    QVERIFY2(!list->isVisible(),
             "the attachment list shows with nothing attached");
    QVERIFY2(!detach->isVisible(),
             "Remove attachment is offered with nothing attached");

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("note.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();
    window.attachFile(path);

    QVERIFY2(!list->isHidden(), "the attachment list stayed hidden");
    auto *row = list->parentWidget();
    QVERIFY(row);
    QVERIFY2(detach->isVisible(),
             "Remove attachment is not offered with a file attached");
}

namespace {

/// Writes a draft the way ComposeWindow's autosave does, and returns its path.
///
/// Built through MessageBuilder rather than by hand, so the test resumes the
/// bytes the application really writes: a draft assembled from a string
/// literal could disagree with the builder and the round trip would prove
/// nothing about the real file.
QString writeDraftFile(const QString &folder, const OutgoingMessage &message,
                       const Account &account)
{
    QDir().mkpath(folder + QStringLiteral("/cur"));
    QDir().mkpath(folder + QStringLiteral("/new"));
    QDir().mkpath(folder + QStringLiteral("/tmp"));
    const MessageBuilder::Result built = MessageBuilder::build(message, account);
    if (!built.ok()) {
        qWarning("draft fixture: build failed: %s", qPrintable(built.error));
        return {};
    }
    const DraftStore::Result written =
        DraftStore::write(folder, built.bytes, QStringLiteral("D"));
    if (!written.ok())
        qWarning("draft fixture: write failed: %s", qPrintable(written.error));
    return written.path;
}

}  // namespace

void TestMainWindow::doubleClickingADraftOpensTheComposer()
{
    // The user's own words: "Double clicking on a draft should open the
    // message in the editor window." Every other row opens its thread, which
    // for an unfinished message means looking at it rendered and being unable
    // to touch it.
    WorkerComposeFixture fixture;
    QVERIFY(fixture.backed.fixture().addMessage(
        QStringLiteral("acct/Drafts"), QStringLiteral("draft2@example.org"),
        QStringLiteral("Unfinished"), QStringLiteral("you@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Half a thought.")));
    QVERIFY2(fixture.seed({ { QStringLiteral("acct"), QStringLiteral("acct"),
                              QStringLiteral("Trash"),
                              QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org"),
                              QStringLiteral("Drafts") } },
                          QStringLiteral("acct/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("id:draft2@example.org"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1
                                 && !window.mailRootForTesting().isEmpty(),
                             15000);

    const QModelIndex row = model->index(0, 0, QModelIndex());
    view->setCurrentIndex(row);

    const int before = window.openComposerCount();
    emit view->doubleClicked(row);

    // Through the worker, so the composer arrives on a later turn.
    QTRY_VERIFY_WITH_TIMEOUT(
        window.openComposerCount() == before + 1, 15000);
}

void TestMainWindow::editDraftIsOfferedOnlyForADraft()
{
    // A draft renders like any other message, so the action has to say which
    // rows it applies to. Offered on ordinary mail it would open a composer
    // that owns a file it did not write, and the first autosave would then
    // delete a received message.
    WorkerComposeFixture fixture;
    // The draft goes in BEFORE the window opens: the worker holds the database
    // open, so a message added afterwards is not in the index it queries.
    QVERIFY(fixture.backed.fixture().addMessage(
        QStringLiteral("acct/Drafts"), QStringLiteral("draft1@example.org"),
        QStringLiteral("Half written"), QStringLiteral("you@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body.")));
    QVERIFY2(fixture.seed({ { QStringLiteral("acct"), QStringLiteral("acct"),
                              QStringLiteral("Trash"),
                              QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org"),
                              QStringLiteral("Drafts") } },
                          QStringLiteral("acct/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    auto *edit = window.findChild<QAction *>(QStringLiteral("edit_draft"));
    QVERIFY(model && view && queryEdit && edit);

    // Selected by id, not through selectTheMessage(): there are two messages
    // here, and this test is about which FOLDER each sits in.
    const auto selectById = [&](const QString &id) {
        queryEdit->setText(QStringLiteral("id:") + id);
        queryEdit->returnPressed();
        bool ready = false;
        for (int attempt = 0; attempt < 150 && !ready; ++attempt) {
            ready = model->rowCount(QModelIndex()) == 1
                    && !window.mailRootForTesting().isEmpty();
            if (!ready)
                QTest::qWait(100);
        }
        if (!ready)
            return false;
        view->setCurrentIndex(model->index(0, 0, QModelIndex()));
        return true;
    };

    QVERIFY2(selectById(QStringLiteral("compose1@example.org")),
             "the inbox message was not found");
    QVERIFY2(!edit->isEnabled(),
             "Edit draft is offered on a message in the inbox");

    // The guard, and it is the half that matters: an action disabled
    // everywhere passes the assertion above while the feature does not exist.
    QVERIFY2(selectById(QStringLiteral("draft1@example.org")),
             "the draft was not found");
    QVERIFY2(edit->isEnabled(),
             "Edit draft is not offered on a message in the drafts folder");
}

void TestMainWindow::theMessageBarOffersEditOnADraft()
{
    // Item 157, and the half item 153 did not close. A draft was editable by
    // double-click and by a Message-menu entry, neither of which is where the
    // user looks while reading one. The pane's own bar carries Reply and
    // Forward, which are the two things a draft cannot do: it has no sender to
    // answer and is not finished enough to pass on.
    WorkerComposeFixture fixture;
    QVERIFY(fixture.backed.fixture().addMessage(
        QStringLiteral("acct/Drafts"), QStringLiteral("draft1@example.org"),
        QStringLiteral("Half written"), QStringLiteral("you@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Body.")));
    QVERIFY2(fixture.seed({ { QStringLiteral("acct"), QStringLiteral("acct"),
                              QStringLiteral("Trash"),
                              QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org"),
                              QStringLiteral("Drafts") } },
                          QStringLiteral("acct/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    // Pinned by name: since item 141 the window holds two toolbars, and an
    // unnamed findChild would assert against whichever came first.
    auto *bar = window.findChild<QToolBar *>(QStringLiteral("message_toolbar"));
    auto *pane = window.findChild<MessageView *>();
    QVERIFY(model && view && queryEdit && bar && pane);

    const auto selectById = [&](const QString &id) {
        queryEdit->setText(QStringLiteral("id:") + id);
        queryEdit->returnPressed();
        bool ready = false;
        for (int attempt = 0; attempt < 150 && !ready; ++attempt) {
            ready = model->rowCount(QModelIndex()) == 1
                    && !window.mailRootForTesting().isEmpty();
            if (!ready)
                QTest::qWait(100);
        }
        if (!ready)
            return false;
        view->setCurrentIndex(model->index(0, 0, QModelIndex()));
        return true;
    };

    const auto barHolds = [&](const QString &name) {
        const auto actions = bar->actions();
        return std::any_of(actions.cbegin(), actions.cend(),
                           [&](const QAction *action) {
                               return action && action->objectName() == name;
                           });
    };

    // Ordinary mail first, so the assertions below mean something: a bar that
    // never holds Edit passes the draft check by accident if the reply pair is
    // simply absent everywhere.
    QVERIFY2(selectById(QStringLiteral("compose1@example.org")),
             "the inbox message was not found");
    QVERIFY2(barHolds(QStringLiteral("reply")),
             "the message bar lost Reply on ordinary mail");
    QVERIFY2(barHolds(QStringLiteral("forward")),
             "the message bar lost Forward on ordinary mail");
    QVERIFY2(!barHolds(QStringLiteral("edit_draft")),
             "Edit draft is on the message bar for a message in the inbox");

    // toggle_html is the view-control half, on the far side of the stretch. It
    // describes how the pane renders and not what the message is, so it must
    // survive the swap: a draft can be read as HTML like anything else.
    QVERIFY2(barHolds(QStringLiteral("toggle_html")),
             "the view controls were lost from the message bar");

    QVERIFY2(selectById(QStringLiteral("draft1@example.org")),
             "the draft was not found");
    QVERIFY2(barHolds(QStringLiteral("edit_draft")),
             "Edit draft is missing from the message bar on a draft");
    QVERIFY2(!barHolds(QStringLiteral("reply")),
             "Reply is still offered on a draft, which has nobody to answer");
    QVERIFY2(!barHolds(QStringLiteral("forward")),
             "Forward is still offered on a draft");
    QVERIFY2(barHolds(QStringLiteral("toggle_html")),
             "the view controls were lost when the bar swapped to a draft");

    // And back, because a one-way swap is the plausible defect: the bar is
    // refilled on every selection change, so returning to ordinary mail has to
    // restore the pair rather than leaving Edit behind on mail it must not act
    // on.
    QVERIFY2(selectById(QStringLiteral("compose1@example.org")),
             "the inbox message was not found on the way back");
    QVERIFY2(barHolds(QStringLiteral("reply")),
             "Reply did not come back after leaving a draft");
    QVERIFY2(!barHolds(QStringLiteral("edit_draft")),
             "Edit draft stayed on the bar after leaving the draft");

    // And the gesture the first version of this test could not see. Running a
    // query blanks the pane WITHOUT moving the selection, so currentIndex()
    // stays valid on a row from the previous result and a bar keyed on it
    // describes a message that is no longer displayed. Both directions were
    // reported: Edit draft left over an empty pane after leaving Drafts, and
    // the reply pair left over one after arriving.
    const auto runQuery = [&](const QString &query) {
        queryEdit->setText(query);
        queryEdit->returnPressed();
        QTest::qWait(300);
    };

    QVERIFY2(selectById(QStringLiteral("draft1@example.org")),
             "the draft was not found before the query change");
    QVERIFY2(barHolds(QStringLiteral("edit_draft")), "precondition: on a draft");

    // A query with RESULTS, which is what the user reported and what a query
    // matching nothing cannot reproduce: with no rows the selection goes
    // invalid and the stale-index answer is accidentally right. Here the list
    // repopulates, currentIndex() lands on a row of the NEW result, and the
    // pane is still blank because nothing has been selected by hand.
    runQuery(QStringLiteral("tag:inbox"));
    QVERIFY2(model->rowCount(QModelIndex()) > 0,
             "the blanking query returned nothing, which is the case that "
             "cannot reproduce the defect");

    // The whole bar goes with the pane, which is the shape the user settled on
    // after looking at the greyed-out one: the subject and the details button
    // already vanish when the pane is cleared, and a persisting action bar was
    // the only piece of header furniture that did not.
    QVERIFY2(bar->isHidden(),
             "the message bar is still shown over a blank pane");

    // Enablement is a separate property from visibility and was ALSO stale:
    // updateComposeActions() ran only from the selection handlers, so a query
    // that blanked the pane left Reply enabled. A hidden bar would hide that,
    // but the Message menu shows the same QAction.
    auto *reply = window.findChild<QAction *>(QStringLiteral("reply"));
    QVERIFY(reply);
    QVERIFY2(!reply->isEnabled(), "Reply is enabled over a blank pane");

    // The other direction, and the one the user reported second: arriving at
    // Drafts from the inbox left the reply pair over the blank pane. Selecting
    // a draft after the query must reach Edit draft, which it cannot if the
    // bar is only refilled on a selection change.
    QVERIFY2(selectById(QStringLiteral("draft1@example.org")),
             "the draft was not found after the blanking query");

    // The FIRST message opened after a blanking, which is the gesture the
    // hiding half broke: nothing refills the bar when a message arrives, so a
    // bar hidden by the query stayed hidden until a SECOND selection, where
    // m_items still held the first message and the guard passed one behind.
    // Asserted before the membership checks below, since a bar that is filled
    // correctly and invisible passes every one of them.
    // The pane loads through the worker, so the render lands a turn or more
    // after the selection. Waiting on the pane itself rather than on a fixed
    // delay, which passes when the render never arrives.
    QTRY_VERIFY_WITH_TIMEOUT(!pane->showingPlaceholder(), 15000);
    QVERIFY2(!bar->isHidden(),
             "the message bar stayed hidden for the first message opened "
             "after the pane was blanked");
    QVERIFY2(barHolds(QStringLiteral("edit_draft")),
             "Edit draft did not return after the pane was blanked");
    QVERIFY2(!barHolds(QStringLiteral("reply")),
             "Reply is offered on a draft reached through a blank pane");
}

void TestMainWindow::aDraftReopensWithItsOwnContent()
{
    // Item 153. A draft was write-only: DraftStore had a write() and no
    // reader, and nothing opened a composer from an existing message, so a
    // draft rendered as ordinary mail and could never be finished.
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    OutgoingMessage message;
    message.accountKey = QStringLiteral("acct");
    message.to = { QStringLiteral("someone@example.org") };
    message.cc = { QStringLiteral("copied@example.org") };
    message.subject = QStringLiteral("A half-written note");
    message.markdownBody = QStringLiteral("The first half.\n\nAnd more.");

    const QString folder = fixture.mailRoot() + QStringLiteral("/acct/Drafts");
    const QString path = writeDraftFile(folder, message,
                                        fixture.config().account(
                                            QStringLiteral("acct")));
    QVERIFY2(!path.isEmpty(), "the draft fixture was not written");

    ComposeContext context =
        ComposeContextBuilder::forDraft(fixture.config(), path);
    QVERIFY2(context.kind == ComposeContext::Kind::Draft,
             "forDraft did not produce a Draft context");

    ComposeWindow window(context, fixture.config(), fixture.mailRoot());
    auto *to = window.findChild<QLineEdit *>(QStringLiteral("to"));
    auto *cc = window.findChild<QLineEdit *>(QStringLiteral("cc"));
    auto *subject = window.findChild<QLineEdit *>(QStringLiteral("subject"));
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(to && cc && subject && body);

    QVERIFY2(to->text().contains(QStringLiteral("someone@example.org")),
             qPrintable(QStringLiteral("To reads '%1'").arg(to->text())));
    QVERIFY2(cc->text().contains(QStringLiteral("copied@example.org")),
             qPrintable(QStringLiteral("Cc reads '%1'").arg(cc->text())));
    QCOMPARE(subject->text(), QStringLiteral("A half-written note"));

    // The body VERBATIM: no attribution, no quote markers, and no blank lines
    // added. A draft is the message itself, not something being answered, so
    // seedBody()'s quote framing must not touch it.
    QVERIFY2(body->toPlainText().contains(QStringLiteral("The first half.")),
             qPrintable(QStringLiteral("the body reads '%1'")
                            .arg(body->toPlainText())));
    QVERIFY2(!body->toPlainText().contains(QStringLiteral("wrote:")),
             "the draft body was framed as a quote");
    QVERIFY2(!body->toPlainText().startsWith(QLatin1Char('>')),
             "the draft body was quote-marked");
    QVERIFY2(!body->toPlainText().startsWith(QStringLiteral("\n\n")),
             "blank lines were prepended to a draft, as if it were a reply");
}

void TestMainWindow::aResumedDraftReplacesItsFileRatherThanAddingOne()
{
    // The half that makes resuming safe rather than merely possible. Maildir
    // has no in-place edit, so an autosave writes a new file and unlinks the
    // old one; a resumed draft that did not know its own path would leave the
    // original behind and the user would have two drafts of one message.
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    OutgoingMessage message;
    message.accountKey = QStringLiteral("acct");
    message.to = { QStringLiteral("someone@example.org") };
    message.subject = QStringLiteral("Resumed");
    message.markdownBody = QStringLiteral("Body.");

    const QString folder = fixture.mailRoot() + QStringLiteral("/acct/Drafts");
    const QString path = writeDraftFile(folder, message,
                                        fixture.config().account(
                                            QStringLiteral("acct")));
    QVERIFY(!path.isEmpty());

    const ComposeContext context =
        ComposeContextBuilder::forDraft(fixture.config(), path);
    QCOMPARE(context.draftPath, path);

    ComposeWindow window(context, fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);

    const auto draftCount = [&folder]() {
        return QDir(folder + QStringLiteral("/cur"))
            .entryList(QDir::Files).size();
    };
    QCOMPARE(draftCount(), 1);

    body->setPlainText(QStringLiteral("Body, continued."));

    // Through the real timer, which is what production uses: the edit above
    // starts it, and firing it here runs the same autosave() a pause would.
    auto *timer = window.findChild<QTimer *>(QStringLiteral("autosave"));
    QVERIFY2(timer, "the composer has no autosave timer");
    QVERIFY2(timer->isActive(),
             "editing the body did not arm the autosave timer");
    timer->setInterval(0);
    QTRY_VERIFY_WITH_TIMEOUT(!timer->isActive(), 5000);

    QCOMPARE(draftCount(), 1);
    QVERIFY2(!QFile::exists(path),
             "the original draft file survived the autosave, so the message "
             "now exists twice");
}

void TestMainWindow::aDraftRenamedByASyncStillReopensAndReplacesItsFile()
{
    // Item 163, the composer site, and the one that costs data rather than
    // display. mbsync uploads a draft and renames it to add its `,U=<uid>`
    // infix; the model's path was captured when the query ran, so the reopen
    // is handed a name that no longer exists.
    //
    // The refusal happens BEFORE any composer exists, so the user composes
    // again into a FRESH window whose autosave has no previous path to unlink.
    // The old revision survives, each save mints a new Message-ID, and both
    // files reach the server. Asserted as the file COUNT, which is the shape
    // the fork actually takes.
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    OutgoingMessage message;
    message.accountKey = QStringLiteral("acct");
    message.to = { QStringLiteral("someone@example.org") };
    message.subject = QStringLiteral("Written before a sync");
    message.markdownBody = QStringLiteral("The first half.");

    const QString folder = fixture.mailRoot() + QStringLiteral("/acct/Drafts");
    const QString path = writeDraftFile(folder, message,
                                        fixture.config().account(
                                            QStringLiteral("acct")));
    QVERIFY(!path.isEmpty());

    // mbsync's rename: same directory, same unique stem, `,U=<uid>` inserted
    // before the flag suffix. Nothing reindexes, so the caller below still
    // holds the pre-rename name, which is the whole precondition.
    const QFileInfo before(path);
    const QString base = before.fileName();
    const int suffix = base.indexOf(QStringLiteral(":2,"));
    QVERIFY2(suffix > 0, "the draft fixture has no maildir flag suffix");
    const QString renamed = before.absolutePath() + QLatin1Char('/')
                            + base.left(suffix) + QStringLiteral(",U=7")
                            + base.mid(suffix);
    QVERIFY2(QFile::rename(path, renamed), "could not stage the sync rename");

    // The guard that proves this test can fail: without it, a fixture that
    // quietly left the original in place would pass against the bug.
    QVERIFY2(!QFile::exists(path), "the stale path should no longer exist");

    const auto draftCount = [&folder]() {
        return QDir(folder + QStringLiteral("/cur"))
            .entryList(QDir::Files).size();
    };
    QCOMPARE(draftCount(), 1);

    // The STALE path, exactly as openComposerFor() passes MessageRef::filePath.
    const ComposeContext context =
        ComposeContextBuilder::forDraft(fixture.config(), path);
    QVERIFY2(context.kind == ComposeContext::Kind::Draft,
             "the reopen was refused, so the user would compose a second draft");
    // Resolved, not the caller's: seeding the stale path would let the reopen
    // succeed and the unlink still miss, forking the draft one step later.
    QCOMPARE(context.draftPath, renamed);

    ComposeWindow window(context, fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("The second half."));

    auto *timer = window.findChild<QTimer *>(QStringLiteral("autosave"));
    QVERIFY2(timer, "the composer has no autosave timer");
    QVERIFY2(timer->isActive(), "editing the body did not arm the autosave");
    timer->setInterval(0);
    QTRY_VERIFY_WITH_TIMEOUT(!timer->isActive(), 5000);

    // Still ONE draft: the autosave replaced the renamed file rather than
    // leaving it behind beside a new one.
    QCOMPARE(draftCount(), 1);
    QVERIFY2(!QFile::exists(renamed),
             "the renamed draft survived the autosave, so the draft was forked "
             "into two files and both would reach the server");
}

void TestMainWindow::aResumedDraftKeepsItsBlindRecipients()
{
    // MessageBuilder writes Bcc into the draft file deliberately, and says
    // why. A resumed draft that did not read it back would drop every blind
    // recipient silently: the user finishes the message, sends it, and the
    // people they addressed blindly never receive it and nothing reports so.
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    OutgoingMessage message;
    message.accountKey = QStringLiteral("acct");
    message.to = { QStringLiteral("someone@example.org") };
    message.bcc = { QStringLiteral("blind@example.org") };
    message.subject = QStringLiteral("With a blind copy");
    message.markdownBody = QStringLiteral("Body.");

    const QString folder = fixture.mailRoot() + QStringLiteral("/acct/Drafts");
    const QString path = writeDraftFile(folder, message,
                                        fixture.config().account(
                                            QStringLiteral("acct")));
    QVERIFY(!path.isEmpty());

    const ComposeContext context =
        ComposeContextBuilder::forDraft(fixture.config(), path);
    ComposeWindow window(context, fixture.config(), fixture.mailRoot());

    auto *bcc = window.findChild<QLineEdit *>(QStringLiteral("bcc"));
    QVERIFY(bcc);
    QVERIFY2(bcc->text().contains(QStringLiteral("blind@example.org")),
             qPrintable(QStringLiteral("Bcc reads '%1', so a blind recipient "
                                       "was dropped").arg(bcc->text())));

    // And it is VISIBLE, per item 145: a hidden field holding an address is a
    // message going somewhere the sender cannot see.
    QVERIFY2(!bcc->isHidden(),
             "the resumed draft hid a Bcc it actually carries");
}

void TestMainWindow::ctrlWClosesTheComposer()
{
    // Item 148. Ctrl+W closes a window in every application the user runs, and
    // the composer bound nothing, so the only way out was the title bar.
    //
    // Scoped to the composer, not registered in KeyMap: Qt dispatches a
    // WindowShortcut to the active window only, which is the same reason the
    // formatting shortcuts are parented here rather than to the main window.
    ComposeFixture fixture;
    QVERIFY(fixture.build());
    ComposeContext context = newContext();

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, fixture.config(), fixture.mailRoot());
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));

    auto *close = window->findChild<QAction *>(QStringLiteral("compose_close"));
    QVERIFY2(close, "the composer has no close action");
    QVERIFY2(close->shortcut() == QKeySequence(QStringLiteral("Ctrl+W")),
             qPrintable(QStringLiteral("the close action is bound to '%1', "
                                       "not Ctrl+W")
                            .arg(close->shortcut().toString())));

    close->trigger();

    // WA_DeleteOnClose, so the window really goes rather than merely hiding.
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 5000);
}

void TestMainWindow::aComposerOpensClean()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());

    // Seeding fills every field, which emits every field's change signal. A
    // composer that counted those as edits would autosave a draft nobody
    // asked for, and would tell the quit path there is unsaved work in a
    // window the user opened and closed without typing.
    QVERIFY(!window.hasUnsavedEdits());
    QVERIFY(!window.lastSaveFailed());

    auto *timer = window.findChild<QTimer *>(QStringLiteral("autosave"));
    QVERIFY2(timer, "no autosave timer: the window was never built");
    QVERIFY2(!timer->isActive(),
             "seeding armed the autosave timer, so a untouched composer writes");
}

void TestMainWindow::typingMarksTheComposerDirty()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);

    QVERIFY(!window.hasUnsavedEdits());
    body->setPlainText(QStringLiteral("Some text."));
    QVERIFY(window.hasUnsavedEdits());

    // The subject is part of the message as much as the body is: a draft that
    // saved the body but not the address it was going to would be worse than
    // none.
    ComposeWindow second(newContext(), fixture.config(), fixture.mailRoot());
    auto *subject = second.findChild<QLineEdit *>(QStringLiteral("subject"));
    QVERIFY(subject);
    QVERIFY(!second.hasUnsavedEdits());
    subject->setText(QStringLiteral("A subject"));
    QVERIFY(second.hasUnsavedEdits());
}

void TestMainWindow::anAutosaveWritesADraftAndClearsTheDirtyFlag()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("Draft body."));
    QVERIFY(window.hasUnsavedEdits());

    QVERIFY2(window.saveDraftNow(), "the draft write reported failure");

    QCOMPARE(fixture.draftCount(), 1);
    QVERIFY2(!window.hasUnsavedEdits(),
             "the flag survived a successful save, so the quit path would ask");
    QVERIFY(!window.lastSaveFailed());

    // The bytes really are the message, not an empty file: the draft is
    // byte-identical to what would be sent, which is the property the one
    // built message exists for.
    const QStringList files =
        QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).entryList();
    QCOMPARE(files.size(), 1);
    QFile written(fixture.draftsCur() + QLatin1Char('/') + files.first());
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray bytes = written.readAll();
    QVERIFY2(bytes.contains("Draft body."), "the draft does not carry the body");
    // Written with the Maildir draft flag, not left bare. The flag SET is not
    // pinned here: a draft also carries S, asserted by
    // TestComposeWindow::aSavedDraftIsFlaggedSeen(), and an endsWith(":2,D")
    // here would fail against that correct behaviour.
    QVERIFY2(files.first().section(QStringLiteral(":2,"), 1)
                 .contains(QLatin1Char('D')),
             qPrintable(QStringLiteral("wrong maildir flags: ") + files.first()));
}

void TestMainWindow::anUnwritableDraftsFolderRaisesThePersistentBanner()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("Draft body."));

    // A FILE where the folder must go. mkpath then fails, which is a real
    // failure mode and needs no permission games that root would defeat.
    const QString accountDir = fixture.mailRoot() + QStringLiteral("/acct");
    QVERIFY(QDir().mkpath(accountDir));
    QFile blocker(accountDir + QStringLiteral("/Drafts"));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.write("not a directory");
    blocker.close();

    QVERIFY2(!window.saveDraftNow(), "an unwritable folder reported success");

    auto *banner = window.findChild<QLabel *>(QStringLiteral("draftBanner"));
    QVERIFY2(banner, "no banner widget");
    QVERIFY2(!banner->text().isEmpty(), "the banner says nothing");
    QVERIFY2(window.lastSaveFailed(),
             "lastSaveFailed() is false after a failed write, so the quit "
             "path would let the text go");
    QVERIFY2(window.hasUnsavedEdits(),
             "a failed save cleared the dirty flag, which claims the text is "
             "safe on disk when it is not");
}

void TestMainWindow::aSuccessfulSaveClearsTheBanner()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("First."));

    const QString accountDir = fixture.mailRoot() + QStringLiteral("/acct");
    QVERIFY(QDir().mkpath(accountDir));
    QFile blocker(accountDir + QStringLiteral("/Drafts"));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();

    QVERIFY(!window.saveDraftNow());
    QVERIFY(window.lastSaveFailed());

    // Remove the obstruction and save again. The banner must go: a warning
    // that stays after the thing it warned about is fixed teaches the user to
    // ignore warnings, which is the second lesson in the TagRules entry.
    QVERIFY(QFile::remove(accountDir + QStringLiteral("/Drafts")));
    body->setPlainText(QStringLiteral("Second."));

    QVERIFY2(window.saveDraftNow(), "the retry failed");
    QVERIFY2(!window.lastSaveFailed(), "lastSaveFailed() stayed set");

    auto *banner = window.findChild<QLabel *>(QStringLiteral("draftBanner"));
    QVERIFY(banner);
    QVERIFY2(banner->isHidden(), "the banner is still up after a good save");
}

void TestMainWindow::anAccountWithoutADraftsFolderReportsNoFailure()
{
    ComposeFixture fixture;
    // No drafts key at all: a real configuration, warned about at startup.
    QVERIFY(fixture.build(QString()));

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("Nowhere to save this."));

    // Nothing was written and nothing failed. Reporting a failure here would
    // make the quit path offer a retry for a state no retry can change.
    QVERIFY2(window.saveDraftNow(),
             "a missing drafts folder was reported as a save failure");
    QVERIFY2(!window.lastSaveFailed(), "the banner state was set");

    auto *banner = window.findChild<QLabel *>(QStringLiteral("draftBanner"));
    QVERIFY(banner);
    QVERIFY(banner->isHidden());
}

void TestMainWindow::aRewrittenDraftUnlinksThePreviousRevision()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);

    body->setPlainText(QStringLiteral("Revision one."));
    QVERIFY(window.saveDraftNow());
    QCOMPARE(fixture.draftCount(), 1);

    body->setPlainText(QStringLiteral("Revision two."));
    QVERIFY(window.saveDraftNow());

    // ONE file, not two. Maildir has no in-place edit, so a draft rewritten
    // every thirty seconds would otherwise accumulate one file per pause, and
    // every one of them is a message mbsync uploads.
    QCOMPARE(fixture.draftCount(), 1);

    const QStringList files =
        QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).entryList();
    QFile written(fixture.draftsCur() + QLatin1Char('/') + files.first());
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray bytes = written.readAll();
    QVERIFY2(bytes.contains("Revision two."), "the surviving file is the old one");
}

void TestMainWindow::theComposerBuildsTheMessageItsWidgetsShow()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeContext context = newContext();
    context.kind = ComposeContext::Kind::Reply;
    context.inReplyTo = QStringLiteral("original@example.org");
    context.references = { QStringLiteral("root@example.org"),
                           QStringLiteral("original@example.org") };
    context.to = { QStringLiteral("one@example.org") };
    context.subject = QStringLiteral("Re: a subject");

    ComposeWindow window(context, fixture.config(), fixture.mailRoot());

    auto *cc = window.findChild<QLineEdit *>(QStringLiteral("cc"));
    auto *bcc = window.findChild<QLineEdit *>(QStringLiteral("bcc"));
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(cc && bcc && body);

    // A field the user typed, split on commas. That is wrong for a RAW header
    // and right here: this is the composer's own rendering, which joins with
    // ", ".
    cc->setText(QStringLiteral("two@example.org, three@example.org"));
    bcc->setText(QStringLiteral("  four@example.org  "));
    body->setPlainText(QStringLiteral("The body."));

    const OutgoingMessage message = window.currentMessage();
    QCOMPARE(message.accountKey, QStringLiteral("acct"));
    QCOMPARE(message.to, QStringList{ QStringLiteral("one@example.org") });
    QCOMPARE(message.cc, (QStringList{ QStringLiteral("two@example.org"),
                                       QStringLiteral("three@example.org") }));
    // Trimmed, or the whitespace reaches the wire as part of the address.
    QCOMPARE(message.bcc, QStringList{ QStringLiteral("four@example.org") });
    QCOMPARE(message.subject, QStringLiteral("Re: a subject"));
    QCOMPARE(message.markdownBody, QStringLiteral("The body."));

    // NOT optional. Without them a reply appears as an orphan thread in the
    // sender's own client, which is invisible locally.
    QCOMPARE(message.inReplyTo, QStringLiteral("original@example.org"));
    QCOMPARE(message.references.size(), 2);
    QCOMPARE(message.references.last(), QStringLiteral("original@example.org"));
}

void TestMainWindow::theFromDropdownDecidesWhichAccountSends()
{
    // TWO sending accounts, because a dropdown with one entry cannot be
    // changed and a test against it passes whether the code reads the dropdown
    // or the context. The first revision of this test did exactly that: it
    // asserted count() == 1 and then re-asserted a property another case
    // already covers, and a mutation making currentAccount() read
    // m_context.accountKey survived it.
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QString(), /*secondAccount=*/true));

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *from = window.findChild<QComboBox *>(QStringLiteral("from"));
    QVERIFY2(from, "no From dropdown");

    // Both sending accounts are offered, seeded to the context's.
    QCOMPARE(from->count(), 2);
    QCOMPARE(from->currentData().toString(), QStringLiteral("acct"));
    QCOMPARE(window.currentMessage().accountKey, QStringLiteral("acct"));

    // Now change it. The dropdown is the authority once the window is open:
    // reading the context here would send from the seeded account while the
    // interface said otherwise.
    const int other = from->findData(QStringLiteral("other"));
    QVERIFY2(other >= 0, "the second account is not in the dropdown");
    from->setCurrentIndex(other);

    QCOMPARE(window.currentMessage().accountKey, QStringLiteral("other"));

    // And the choice reaches the DRAFT's destination, not just the value:
    // a draft is written into the sending account's own folder, so a composer
    // that read the context would file it under the wrong account.
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("From the other account."));
    QVERIFY(window.saveDraftNow());

    QCOMPARE(QDir(fixture.otherDraftsCur(), {}, QDir::Name, QDir::Files).count(),
             1u);
    QCOMPARE(QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).count(), 0u);
}

void TestMainWindow::aFormatEditPreservesTheUndoStack()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);

    // Typed through a cursor, which is what makes it an undoable edit;
    // setPlainText() would not be one.
    QTextCursor typing = body->textCursor();
    typing.insertText(QStringLiteral("hello"));
    QVERIFY(body->document()->isUndoAvailable());

    QTextCursor selection = body->textCursor();
    selection.setPosition(0);
    selection.setPosition(5, QTextCursor::KeepAnchor);
    body->setTextCursor(selection);

    auto *bold = window.findChild<QAction *>(QStringLiteral("format_bold"));
    QVERIFY2(bold, "no bold action");
    bold->trigger();

    QCOMPARE(body->toPlainText(), QStringLiteral("**hello**"));

    // The property the plan's setPlainText() draft would have lost. Measured
    // in a standalone probe: setPlainText() takes isUndoAvailable from true to
    // false, so every toolbar press would throw away everything the user could
    // undo.
    QVERIFY2(body->document()->isUndoAvailable(),
             "the format edit destroyed the undo stack");

    // And it is ONE undo step, not one per character: a whole-document
    // replacement inside an edit block collapses to a single entry, so one
    // Ctrl+Z takes the tokens off and leaves the typed word.
    body->undo();
    QCOMPARE(body->toPlainText(), QStringLiteral("hello"));
}

void TestMainWindow::aFormatEditRestoresTheSelectionItAsksFor()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("hello world"));

    // A BACKWARDS selection, anchor after the cursor, which is what a
    // right-to-left drag produces and an ordinary gesture. Measured against a
    // real widget: selectionStart()/selectionEnd() come back normalised even
    // then, so the anchor's side does not reach MarkdownFormat.
    QTextCursor selection = body->textCursor();
    selection.setPosition(5);
    selection.setPosition(0, QTextCursor::KeepAnchor);
    body->setTextCursor(selection);
    QCOMPARE(body->textCursor().selectionStart(), 0);
    QCOMPARE(body->textCursor().selectionEnd(), 5);

    auto *italic = window.findChild<QAction *>(QStringLiteral("format_italic"));
    QVERIFY(italic);
    italic->trigger();

    QCOMPARE(body->toPlainText(), QStringLiteral("*hello* world"));

    // The selection is preserved precisely so a second press can apply a
    // SECOND token to the same words, bold then italic without reselecting.
    QCOMPARE(body->textCursor().selectedText(), QStringLiteral("hello"));

    auto *bold = window.findChild<QAction *>(QStringLiteral("format_bold"));
    QVERIFY(bold);
    bold->trigger();
    QCOMPARE(body->toPlainText(), QStringLiteral("***hello*** world"));
}

void TestMainWindow::aFormatEditOnAnEmptySelectionLandsBetweenTheTokens()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("ab"));

    QTextCursor cursor = body->textCursor();
    cursor.setPosition(1);
    body->setTextCursor(cursor);

    auto *bold = window.findChild<QAction *>(QStringLiteral("format_bold"));
    QVERIFY(bold);
    bold->trigger();

    QCOMPARE(body->toPlainText(), QStringLiteral("a****b"));

    // The property a user notices immediately when it is wrong, and the one
    // invisible to a test that only compares the resulting text: typing must
    // continue INSIDE the pair, not after it.
    QCOMPARE(body->textCursor().position(), 3);
    QVERIFY(!body->textCursor().hasSelection());

    QTextCursor typing = body->textCursor();
    typing.insertText(QStringLiteral("x"));
    QCOMPARE(body->toPlainText(), QStringLiteral("a**x**b"));
}

void TestMainWindow::theAttachmentWarningRespectsTheConfiguredThreshold()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("attachment_warn_bytes=1000")));

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());

    // The threshold, not the modal. The question itself needs a user, so what
    // is asserted is the predicate that decides whether to ask.
    QVERIFY2(!window.attachmentNeedsWarning(999), "warned below the limit");
    QVERIFY2(!window.attachmentNeedsWarning(1000),
             "warned AT the limit, which is not above it");
    QVERIFY2(window.attachmentNeedsWarning(1001), "did not warn above the limit");
}

void TestMainWindow::aDisabledAttachmentWarningWarnsAboutNothing()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("attachment_warn_bytes=0")));

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());

    // Zero means off, not "warn about everything". Read as a threshold it
    // would question an empty file, which is the opposite of what turning a
    // warning off means.
    QVERIFY(!window.attachmentNeedsWarning(0));
    QVERIFY(!window.attachmentNeedsWarning(1));
    QVERIFY(!window.attachmentNeedsWarning(100LL * 1024 * 1024));
}

void TestMainWindow::theQuotePositionDecidesWhereTheQuoteLands()
{
    const QString quote = QStringLiteral("> the original");

    {
        ComposeFixture above;
        QVERIFY(above.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                            QStringLiteral("quote_position=above")));
        ComposeContext context = newContext();
        context.kind = ComposeContext::Kind::Reply;
        context.quotedBody = quote;

        ComposeWindow window(context, above.config(), above.mailRoot());
        auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
        QVERIFY(body);
        QVERIFY2(body->toPlainText().startsWith(quote),
                 "quote_position=above did not put the quote first");
    }

    {
        ComposeFixture below;
        QVERIFY(below.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                            QStringLiteral("quote_position=below")));
        ComposeContext context = newContext();
        context.kind = ComposeContext::Kind::Reply;
        context.quotedBody = quote;

        ComposeWindow window(context, below.config(), below.mailRoot());
        auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
        QVERIFY(body);
        QVERIFY2(body->toPlainText().endsWith(quote),
                 "quote_position=below did not put the quote last");
        QVERIFY2(!body->toPlainText().startsWith(quote),
                 "the quote is at the top under quote_position=below");
    }
}

void TestMainWindow::theCursorStartsOnBlankSpaceNotOnTheQuote()
{
    // The user types their reply where the cursor lands, so that line must be
    // blank under BOTH quote positions. Asserting on the buffer's shape is not
    // enough: theQuotePositionDecidesWhereTheQuoteLands() already does that and
    // passed throughout the defect, because the quote was in the right place
    // and the cursor was on top of it.
    const QString quote = QStringLiteral("> the original");

    const struct {
        const char *position;
        const char *label;
    } cases[] = {
        { "above", "quote_position=above" },
        { "below", "quote_position=below" },
    };

    for (const auto &testCase : cases) {
        ComposeFixture fixture;
        QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                              QStringLiteral("quote_position=%1")
                                  .arg(QLatin1String(testCase.position))));
        ComposeContext context = newContext();
        context.kind = ComposeContext::Kind::Reply;
        context.quotedBody = quote;

        ComposeWindow window(context, fixture.config(), fixture.mailRoot());
        auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
        QVERIFY(body);

        const QTextCursor cursor = body->textCursor();
        QVERIFY2(cursor.block().text().isEmpty(),
                 qPrintable(QStringLiteral("%1: the cursor starts on \"%2\", "
                                           "not on a blank line")
                                .arg(QLatin1String(testCase.label),
                                     cursor.block().text())));

        // Typing must not land inside the quote either. A blank line that is
        // still BELOW the quote would satisfy the check above while leaving the
        // reply underneath what it answers, which is what quote_position
        // decides and must not be silently inverted.
        QTextCursor probe = cursor;
        probe.insertText(QStringLiteral("typed"));
        const QString text = body->toPlainText();
        const bool typedFirst = text.indexOf(QStringLiteral("typed"))
                                < text.indexOf(quote);
        QCOMPARE(typedFirst,
                 QLatin1String(testCase.position) == QLatin1String("above")
                     ? false
                     : true);
    }
}

void TestMainWindow::aReplyOpensWithTheBodyFocused()
{
    // A Reply arrives with To: already filled, so the first widget in the form
    // taking focus means the user has to click into the editor before typing.
    // A New message is the opposite case and keeps the default.
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    {
        ComposeContext context = newContext();
        context.kind = ComposeContext::Kind::Reply;
        context.to = { QStringLiteral("someone@example.org") };
        context.quotedBody = QStringLiteral("> the original");

        ComposeWindow window(context, fixture.config(), fixture.mailRoot());
        auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
        QVERIFY(body);
        // focusWidget(), not hasFocus(): an unshown window is never active, so
        // hasFocus() is false whatever the code does and the assertion would
        // fail against a correct fix. This is the same class of trap CLAUDE.md
        // records for the offscreen platform and window sizing.
        QVERIFY2(window.focusWidget() == body,
                 "a reply did not open with the body focused");
    }

    {
        // The guard: without it, focusing the body unconditionally would pass
        // the assertion above while taking focus off an empty To: field, which
        // is the one thing a new message needs first.
        ComposeContext context = newContext();
        context.kind = ComposeContext::Kind::New;
        context.to.clear();

        ComposeWindow window(context, fixture.config(), fixture.mailRoot());
        auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
        auto *to = window.findChild<QLineEdit *>(QStringLiteral("to"));
        QVERIFY(body && to);
        QVERIFY2(window.focusWidget() != body,
                 "a new message stole focus from the empty To: field");
    }
}

void TestMainWindow::theSeededQuoteIsNotAnUndoStep()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeContext context = newContext();
    context.kind = ComposeContext::Kind::Reply;
    context.quotedBody = QStringLiteral("> the original");

    ComposeWindow window(context, fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    QVERIFY(!body->toPlainText().isEmpty());

    // The seeded quote is not an edit the user made. One Ctrl+Z on a fresh
    // composer must not wipe it, which reads as the buffer losing its content.
    //
    // Worth knowing before judging this test dead weight: removing
    // clearUndoRedoStacks() alone leaves it GREEN, because setPlainText()
    // already leaves undo unavailable. The line it guards becomes load-bearing
    // the moment seedBody() stops using setPlainText, which is a change with
    // reasons to happen: applyEdit() switched to a QTextCursor replacement for
    // exactly the undo-stack property this asserts, and a later revision
    // seeding the quote the same way would put it on the stack. The combined
    // mutation (seed through a cursor AND drop the clear) does kill this.
    QVERIFY2(!body->document()->isUndoAvailable(),
             "the seeded quote is on the undo stack");
}

void TestMainWindow::aReplySeedsTheHtmlToggleFromTheOriginal()
{
    ComposeFixture fixture;
    // Config says yes; the original says no. The original wins for a reply:
    // an HTML part in it is a fact about the sender's software, not a guess
    // about their taste.
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_html=true")));

    ComposeContext context = newContext();
    context.kind = ComposeContext::Kind::Reply;
    context.seedHtml = false;

    ComposeWindow window(context, fixture.config(), fixture.mailRoot());
    auto *toggle = window.findChild<QAbstractButton *>(QStringLiteral("sendHtml"));
    QVERIFY2(toggle, "no send-html toggle");
    QVERIFY2(!toggle->isChecked(),
             "a reply seeded from config rather than from the original");

    // And the other way round, so the test cannot pass by always reading
    // false: a plain-text config with an HTML original still offers HTML.
    ComposeFixture plain;
    QVERIFY(plain.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                        QStringLiteral("send_html=false")));
    ComposeContext htmlReply = newContext();
    htmlReply.kind = ComposeContext::Kind::ReplyAll;
    htmlReply.seedHtml = true;

    ComposeWindow second(htmlReply, plain.config(), plain.mailRoot());
    auto *secondToggle =
        second.findChild<QAbstractButton *>(QStringLiteral("sendHtml"));
    QVERIFY(secondToggle);
    QVERIFY2(secondToggle->isChecked(),
             "a reply-all ignored an HTML original");
}

void TestMainWindow::aNewMessageSeedsTheHtmlToggleFromConfig()
{
    ComposeFixture off;
    QVERIFY(off.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                      QStringLiteral("send_html=false")));

    // seedHtml is deliberately TRUE here and must be ignored: a New message
    // has no original to take evidence from, so a composer reading it would be
    // reading a field nothing filled in.
    ComposeContext context = newContext();
    context.seedHtml = true;

    ComposeWindow window(context, off.config(), off.mailRoot());
    auto *toggle = window.findChild<QAbstractButton *>(QStringLiteral("sendHtml"));
    QVERIFY(toggle);
    QVERIFY2(!toggle->isChecked(), "a New message ignored [compose] send_html");

    ComposeFixture on;
    QVERIFY(on.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                     QStringLiteral("send_html=true")));
    ComposeContext forward = newContext();
    forward.kind = ComposeContext::Kind::Forward;
    forward.seedHtml = false;

    ComposeWindow second(forward, on.config(), on.mailRoot());
    auto *secondToggle =
        second.findChild<QAbstractButton *>(QStringLiteral("sendHtml"));
    QVERIFY(secondToggle);
    QVERIFY2(secondToggle->isChecked(),
             "a Forward seeded from the original rather than from config");
}

void TestMainWindow::disablingInputsCoversEveryFieldAndTheToolbar()
{
    ComposeFixture fixture;
    // Zero delay: the countdown is skipped and the send commits at once, which
    // is the state the inputs must already be disabled in.
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=0")));

    ComposeContext context = newContext();
    context.to = { QStringLiteral("someone@example.org") };

    // Heap-allocated and tracked with a QPointer, because ComposeWindow sets
    // WA_DeleteOnClose and this case really does complete a send: the window
    // deletes itself on the way out, so a stack instance would be destroyed
    // twice. Every other case here stays on the stack, since none of them
    // closes.
    QPointer<ComposeWindow> window =
        new ComposeWindow(context, fixture.config(), fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("Text."));

    auto *toolbar = window->findChild<QToolBar *>(QStringLiteral("formatToolbar"));
    auto *to = window->findChild<QLineEdit *>(QStringLiteral("to"));
    auto *subject = window->findChild<QLineEdit *>(QStringLiteral("subject"));
    auto *from = window->findChild<QComboBox *>(QStringLiteral("from"));
    auto *toggle = window->findChild<QAbstractButton *>(QStringLiteral("sendHtml"));
    QVERIFY(toolbar && to && subject && from && toggle);

    QVERIFY(to->isEnabled());
    QVERIFY(!body->isReadOnly());

    auto *sendAction = window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY2(sendAction, "no send action");
    sendAction->trigger();

    // The message must not change between pressing Send and the bytes being
    // built, so every input goes down for the WHOLE operation, countdown
    // included. The body is made read-only rather than disabled, so its text
    // stays selectable and legible while the send runs.
    QVERIFY2(!to->isEnabled(), "the To field is still editable during a send");
    QVERIFY2(!subject->isEnabled(), "the subject is still editable");
    QVERIFY2(!from->isEnabled(), "the account can still be changed");
    QVERIFY2(!toggle->isEnabled(), "the HTML toggle can still be flipped");
    QVERIFY2(body->isReadOnly(), "the body is still writable during a send");
    QVERIFY2(!toolbar->isEnabled(), "the formatting toolbar is still live");
    auto *attachments =
        window->findChild<QListWidget *>(QStringLiteral("attachments"));
    QVERIFY(attachments);
    QVERIFY2(!attachments->isEnabled(),
             "the attachment list is still live during a send");

    // EVERY control by name, not the toolbar that used to contain them all.
    // Until item 142 split it, one setEnabled() on formatToolbar covered
    // Bold through Send, so asserting on that one widget was the same as
    // asserting on all of them. With the row split four ways, that assertion
    // would pass while Attach sat live beside a message already being built:
    // a file appended to m_attachments after MessageBuilder has run is either
    // silently dropped or added to bytes already handed to the send command,
    // and neither reports anything. Named individually so a control that
    // grows a new home cannot quietly escape the lock.
    for (const QString &name : { QStringLiteral("compose_attach"),
                                 QStringLiteral("compose_detach"),
                                 QStringLiteral("format_bold"),
                                 QStringLiteral("format_italic"),
                                 QStringLiteral("format_link"),
                                 QStringLiteral("format_quote") }) {
        auto *action = window->findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no action %1").arg(name)));
        QVERIFY2(!action->isEnabled(),
                 qPrintable(QStringLiteral("%1 is still live during a send")
                                .arg(name)));
    }

    // The Cc/Bcc disclosure too: revealing a field mid-send is harmless on its
    // own, but the fields it reveals must be as locked as the rest.
    auto *ccBcc = window->findChild<QAbstractButton *>(
        QStringLiteral("ccBccDisclosure"));
    QVERIFY(ccBcc);
    QVERIFY2(!ccBcc->isEnabled(),
             "the Cc/Bcc disclosure is still live during a send");
    auto *cc = window->findChild<QLineEdit *>(QStringLiteral("cc"));
    auto *bcc = window->findChild<QLineEdit *>(QStringLiteral("bcc"));
    QVERIFY(cc && bcc);
    QVERIFY2(!cc->isEnabled() && !bcc->isEnabled(),
             "Cc or Bcc is still editable during a send");

    // /bin/true is the fixture's send command, so the send succeeds and the
    // composer closes itself: the message went, and holding a composer open
    // for a message already sent invites sending it twice. Waited on rather
    // than asserted immediately, since the process is handed to the event loop
    // and nothing here blocks on it. WA_DeleteOnClose then destroys the
    // window, which is what the QPointer observes.
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 15000);

    // And the sent copy really was filed, which is the stage after the send
    // and the one whose failure the design treats as the worst outcome here.
    const QString sentCur =
        fixture.mailRoot() + QStringLiteral("/acct/Sent/cur");
    QCOMPARE(QDir(sentCur, {}, QDir::Name, QDir::Files).count(), 1u);
}

void TestMainWindow::aFailedSendCanBeRetriedWithoutFilingTheWrongCopy()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=0")));

    // A stub whose outcome is switched by a sentinel file, so ONE configured
    // command can fail and then succeed. It appends its stdin to a log, which
    // is what makes the delivery count observable: the defect this guards
    // against files a sent copy of the FIRST message when the second finishes,
    // and a receiver count is the only thing that shows it.
    QTemporaryDir stubDir;
    QVERIFY(stubDir.isValid());
    const QString sentinel = stubDir.filePath(QStringLiteral("succeed"));
    const QString stub = stubDir.filePath(QStringLiteral("send.sh"));
    {
        QFile script(stub);
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&script);
        out << "#!/bin/sh\n"
            << "cat >> " << stubDir.filePath(QStringLiteral("stdin.log")) << "\n"
            << "[ -f " << sentinel << " ] || { echo 'refused' >&2; exit 1; }\n"
            << "exit 0\n";
    }
    QVERIFY(QFile::setPermissions(
        stub, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                  | QFileDevice::ExeOwner));

    // A FRESH Config, not a copy of the fixture's reloaded: Config::load()
    // does not clear what a previous load put there, so a copy keeps the
    // fixture's /bin/true and this test would silently exercise a command that
    // always succeeds. Measured, and it produced a green nothing.
    Config config;
    {
        const QString path = QStringLiteral("%1/retry.conf").arg(stubDir.path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.acct]\n"
            << "name=Test User\n"
            << "address=user@example.org\n"
            << "maildir=acct\n"
            << "trash=Trash\n"
            << "drafts=Drafts\n"
            << "sent=Sent\n"
            << "send_command=" << stub << "\n"
            << "\n[compose]\n"
            << "send_delay_ms=0\n";
        file.close();
        config.load(path);
    }

    ComposeContext context = newContext();
    context.to = { QStringLiteral("someone@example.org") };

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, config, fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction = window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);

    body->setPlainText(QStringLiteral("FIRST attempt."));
    sendAction->trigger();

    // The failure re-enables the composer intact and shows the stderr; the
    // window stays open and the draft stays.
    auto *pane = window->findChild<QWidget *>(QStringLiteral("sendLogPane"));
    QVERIFY(pane);
    QTRY_VERIFY_WITH_TIMEOUT(!pane->isHidden(), 15000);

    QVERIFY2(!window.isNull(), "a failed send closed the composer");
    QVERIFY2(body->isEnabled() && !body->isReadOnly(),
             "a failed send left the composer disabled");

    // Correct the message and send again, this time succeeding. Without
    // Qt::SingleShotConnection on the per-send connect, the first send's
    // lambda is still attached: the second result runs BOTH, and the first
    // still holds the FIRST message's bytes, so it files a sent copy of the
    // wrong message and acts on a dialog it already destroyed.
    QFile marker(sentinel);
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.close();

    body->setPlainText(QStringLiteral("SECOND attempt."));
    sendAction->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 15000);

    // Exactly ONE sent copy, and it is the second message. Two files, or one
    // carrying the first attempt, is the accumulated-receiver defect.
    const QString sentCur = fixture.mailRoot() + QStringLiteral("/acct/Sent/cur");
    const QStringList filed =
        QDir(sentCur, {}, QDir::Name, QDir::Files).entryList();
    QCOMPARE(filed.size(), 1);

    QFile copy(sentCur + QLatin1Char('/') + filed.first());
    QVERIFY(copy.open(QIODevice::ReadOnly));
    const QByteArray bytes = copy.readAll();
    QVERIFY2(bytes.contains("SECOND attempt."),
             "the filed copy is not the message that was sent");
    QVERIFY2(!bytes.contains("FIRST attempt."),
             "the filed copy is the FIRST message, which never went");
}

void TestMainWindow::anUnchangedMessageIsNotWrittenAgain()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build());

    ComposeWindow window(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);

    body->setPlainText(QStringLiteral("Once."));
    QVERIFY(window.saveDraftNow());
    QCOMPARE(fixture.draftCount(), 1);

    const QStringList first =
        QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).entryList();
    QCOMPARE(first.size(), 1);

    // Nothing has changed, so nothing is written. Every autosave produces a
    // Maildir write that mbsync uploads, so this check and the debounce
    // together are what keep a message to a few revisions rather than dozens.
    //
    // The FILENAME is what shows it: DraftStore always generates a fresh name
    // and unlinks the previous one, so a redundant write leaves exactly one
    // file too, and a count alone cannot tell a skipped write from a repeated
    // one. Two runs of this test asserting only on the count would pass
    // against no check at all.
    QVERIFY2(window.saveDraftNow(), "the redundant save reported failure");
    QCOMPARE(fixture.draftCount(), 1);
    const QStringList second =
        QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).entryList();
    QCOMPARE(second, first);

    // And a real change still writes: a check that skipped everything would
    // pass the assertion above and lose the user's text.
    body->setPlainText(QStringLiteral("Twice."));
    QVERIFY(window.saveDraftNow());
    const QStringList third =
        QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).entryList();
    QCOMPARE(third.size(), 1);
    QVERIFY2(third != first, "a changed message was not written");
}

void TestMainWindow::closingInsideTheDebounceStillSavesTheDraft()
{
    ComposeFixture fixture;
    // A debounce far longer than this test, so the timer provably never fires
    // and the only thing that can write is the close itself.
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("autosave_interval_ms=600000")));

    // Heap-allocated: WA_DeleteOnClose destroys the window on the way out, so
    // a stack instance would be destroyed twice.
    QPointer<ComposeWindow> window =
        new ComposeWindow(newContext(), fixture.config(), fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);

    body->setPlainText(QStringLiteral("A paragraph typed and not yet saved."));
    QVERIFY(window->hasUnsavedEdits());

    // The timer has NOT fired. Asserted rather than assumed: if it had, the
    // draft below would prove nothing about the close path.
    auto *timer = window->findChild<QTimer *>(QStringLiteral("autosave"));
    QVERIFY(timer);
    QVERIFY2(timer->isActive(), "the debounce is not running");
    QCOMPARE(fixture.draftCount(), 0);

    // The window manager's X button, which is the route that reaches
    // closeEvent. Typing a paragraph and pressing it inside the debounce
    // interval must not lose the text.
    window->close();
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 5000);

    QCOMPARE(fixture.draftCount(), 1);
    const QStringList files =
        QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).entryList();
    QCOMPARE(files.size(), 1);
    QFile written(fixture.draftsCur() + QLatin1Char('/') + files.first());
    QVERIFY(written.open(QIODevice::ReadOnly));
    QVERIFY2(written.readAll().contains("A paragraph typed and not yet saved."),
             "the close wrote a draft that is not the text that was typed");
}

void TestMainWindow::closingAfterASendWritesNoFurtherDraft()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=0")));

    ComposeContext context = newContext();
    context.to = { QStringLiteral("someone@example.org") };

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, fixture.config(), fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction = window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);

    body->setPlainText(QStringLiteral("Text that is about to be sent."));

    // A draft on disk first, so the send's removal of it is observable and the
    // close-path save has something it could wrongly put back.
    QVERIFY(window->saveDraftNow());
    QCOMPARE(fixture.draftCount(), 1);

    // Now edit again WITHOUT saving, so m_dirty is true at the moment the
    // send completes. This is what makes the m_finished guard load-bearing:
    // without it the close that follows a successful send would write a draft
    // for a message already sent, restoring the file the send just unlinked.
    body->setPlainText(QStringLiteral("Text that is about to be sent, edited."));
    QVERIFY(window->hasUnsavedEdits());

    sendAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 15000);

    // The message went, so the drafts folder is EMPTY. A draft left behind is
    // a message the user sees waiting to be finished when it has already been
    // delivered.
    QCOMPARE(fixture.draftCount(), 0);

    // And the sent copy is there, so this is a completed send rather than a
    // send that never happened leaving nothing behind either way.
    const QString sentCur = fixture.mailRoot() + QStringLiteral("/acct/Sent/cur");
    QCOMPARE(QDir(sentCur, {}, QDir::Name, QDir::Files).count(), 1u);
}

void TestMainWindow::aSendRemovesADraftMbsyncHasRenamed()
{
    // Measured on the user's own mail, 2026-08-26: a forward was sent, the
    // recipient got it, the sent copy was filed, and the draft STAYED in the
    // Drafts view carrying the `D` flag.
    //
    // mbsync renames an uploaded draft to add its `,U=<uid>` infix while
    // m_draftPath still holds the name DraftStore::write() returned, so
    // QFile::remove() ran against a path that no longer existed and failed
    // silently. Item 163 added MaildirName::resolveRenamed() for exactly this
    // rename and wired it into the three READ sites; this is the WRITE site,
    // and it was missed.
    //
    // closingAfterASendWritesNoFurtherDraft() already asserts the drafts
    // folder is empty after a send and passed throughout, because its draft is
    // never renamed. The rename is the whole defect, so it has to be in the
    // fixture.
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=0")));

    ComposeContext context = newContext();
    context.to = { QStringLiteral("someone@example.org") };

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, fixture.config(), fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction =
        window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);

    body->setPlainText(QStringLiteral("Text that is about to be sent."));
    QVERIFY(window->saveDraftNow());
    QCOMPARE(fixture.draftCount(), 1);

    // Renamed exactly as mbsync renames it: the `,U=<uid>` infix goes before
    // the `:2,` flag separator, so the stem the composer remembers is still a
    // prefix of the real name and nothing but a directory scan can find it.
    const QStringList before =
        QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).entryList();
    QCOMPARE(before.size(), 1);
    const QString original = before.first();
    const int sep = original.indexOf(QStringLiteral(":2,"));
    QVERIFY2(sep > 0, "the draft filename carries no :2, flag separator");
    const QString renamed = original.left(sep) + QStringLiteral(",U=7")
                            + original.mid(sep);
    QVERIFY(QFile::rename(fixture.draftsCur() + QLatin1Char('/') + original,
                          fixture.draftsCur() + QLatin1Char('/') + renamed));
    QCOMPARE(fixture.draftCount(), 1);

    sendAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 15000);

    // The sent copy proves the send actually completed, so an empty drafts
    // folder below means the removal worked rather than that nothing ran.
    const QString sentCur =
        fixture.mailRoot() + QStringLiteral("/acct/Sent/cur");
    QCOMPARE(QDir(sentCur, {}, QDir::Name, QDir::Files).count(), 1u);

    QCOMPARE(fixture.draftCount(), 0);
}

void TestMainWindow::aForwardFlagsTheMessageItForwarded()
{
    // Item 68. The signal that carries the P flag back to the source message.
    // Asserted on the SIGNAL rather than on the tag, because the tag write is
    // MainWindow's and needs a worker; what can go wrong here is the composer
    // never emitting, which is what the user observed on 2026-08-26.
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=0")));

    ComposeContext context = newContext();
    context.kind = ComposeContext::Kind::Forward;
    context.to = { QStringLiteral("someone@example.org") };

    // Set for a forward as well as a reply, and deliberately NOT inReplyTo:
    // that header is empty on a forward, so keying the emit on it made this
    // half dead code that compiled and never fired.
    context.sourceMessageId = QStringLiteral("original@example.org");

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, fixture.config(), fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction =
        window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);

    QString flaggedId;
    QString flaggedTag;
    connect(window.data(), &ComposeWindow::sourceMessageAnswered,
            [&](const QString &id, const QString &tag) {
                flaggedId = id;
                flaggedTag = tag;
            });

    body->setPlainText(QStringLiteral("Passing this on."));
    sendAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 15000);

    // The sent copy proves the send completed, so an unset tag below is a
    // missing emit rather than a send that never happened.
    const QString sentCur =
        fixture.mailRoot() + QStringLiteral("/acct/Sent/cur");
    QCOMPARE(QDir(sentCur, {}, QDir::Name, QDir::Files).count(), 1u);

    QCOMPARE(flaggedId, QStringLiteral("original@example.org"));
    QCOMPARE(flaggedTag, QStringLiteral("passed"));
}

void TestMainWindow::aReplyFlagsTheMessageItAnswered()
{
    // The other half of item 68, and the one the measurement said was ALSO
    // missing: all 317 `replied` in the developer's index came from other
    // clients, because nothing here had ever written the R flag.
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=0")));

    ComposeContext context = newContext();
    context.kind = ComposeContext::Kind::Reply;
    context.to = { QStringLiteral("someone@example.org") };
    context.sourceMessageId = QStringLiteral("original@example.org");

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, fixture.config(), fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction =
        window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);

    QString flaggedTag;
    connect(window.data(), &ComposeWindow::sourceMessageAnswered,
            [&](const QString &, const QString &tag) { flaggedTag = tag; });

    body->setPlainText(QStringLiteral("Answering."));
    sendAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 15000);

    QCOMPARE(flaggedTag, QStringLiteral("replied"));
}

void TestMainWindow::aResumedDraftFlagsNothing()
{
    // Kind::Draft records how the FILE was opened, not what the user is
    // doing, so a draft that began as a reply cannot be told from one that
    // began as a new message. Flagging on it would set R from a guess, and
    // maildir.synchronize_flags carries a wrong flag to the server.
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=0")));

    ComposeContext context = newContext();
    context.kind = ComposeContext::Kind::Draft;
    context.to = { QStringLiteral("someone@example.org") };

    // Present, and must still be ignored: this is the case a guard keyed only
    // on the id being non-empty would get wrong.
    context.sourceMessageId = QStringLiteral("original@example.org");

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, fixture.config(), fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction =
        window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);

    bool emitted = false;
    connect(window.data(), &ComposeWindow::sourceMessageAnswered,
            [&](const QString &, const QString &) { emitted = true; });

    body->setPlainText(QStringLiteral("Finishing this off."));
    sendAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(window.isNull(), 15000);

    const QString sentCur =
        fixture.mailRoot() + QStringLiteral("/acct/Sent/cur");
    QCOMPARE(QDir(sentCur, {}, QDir::Name, QDir::Files).count(), 1u);

    QVERIFY2(!emitted, "a resumed draft flagged a message it cannot know it "
                       "was answering");
}

void TestMainWindow::aForwardWritesThePassedTagToTheIndex()
{
    // The END-TO-END half: aForwardFlagsTheMessageItForwarded() proves the
    // composer emits, and this proves the tag actually reaches notmuch. The
    // user forwarded real mail on 2026-08-26, the recipient got it, the sent
    // copy was filed, and `tag:passed` never moved, so the gap is somewhere
    // between the emit and the index and only a real worker can show which.
    WorkerComposeFixture fixture;
    QVERIFY2(fixture.seed({ { QStringLiteral("acct"), QStringLiteral("acct"),
                              QStringLiteral("Trash"),
                              QStringLiteral("/bin/true"),
                              QStringLiteral("you@example.org"),
                              QStringLiteral("Drafts"),
                              QStringLiteral("Sent") } },
                          QStringLiteral("acct/inbox")),
             qPrintable(fixture.backed.error()));

    MainWindow window(fixture.backed.config());
    QVERIFY(WorkerComposeFixture::selectTheMessage(window));

    auto *forward = window.findChild<QAction *>(QStringLiteral("forward"));
    QVERIFY(forward);

    // Forward is gated on a message being DISPLAYED, not merely selected:
    // updateComposeActions() enables it from the pane. A disabled action's
    // trigger() is a silent no-op, so asserting this is what stops the test
    // measuring nothing.
    QTRY_VERIFY_WITH_TIMEOUT(forward->isEnabled(), 15000);
    forward->trigger();

    // Forward is ASYNCHRONOUS: composeReply() asks the worker to load the
    // message and the composer opens when that reply lands. Calling
    // openComposerForTest() straight after the trigger returns before the
    // round trip finishes, and the first version of this test did exactly
    // that, then asserted on a composer whose kind was New and whose
    // sourceMessageId was empty. Waiting on the COUNT is what makes the
    // composer under test the one Forward opened.
    QTRY_VERIFY_WITH_TIMEOUT(window.openComposerCount() == 1, 15000);

    // openComposersForTest(), NOT openComposerForTest(): the singular one
    // OPENS a fresh Kind::New composer rather than returning an existing one,
    // which is what the Compose action's tests want and is a trap here. The
    // first version of this test used it, sent from the composer it had just
    // created, and reported kind=0 with an empty sourceMessageId, reading
    // exactly like the product defect it was written to reproduce.
    const QList<ComposeWindow *> composers = window.openComposersForTest();
    QCOMPARE(composers.size(), 1);
    ComposeWindow *composer = composers.first();
    QVERIFY2(composer, "Forward opened no composer");

    auto *body = composer->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction =
        composer->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);

    auto *to = composer->findChild<QLineEdit *>(QStringLiteral("to"));
    QVERIFY(to);
    to->setText(QStringLiteral("someone@example.org"));
    body->setPlainText(QStringLiteral("Passing this on."));

    sendAction->trigger();

    // The tag lands through the worker, so this waits on the DATABASE rather
    // than on a signal: the whole question is whether the write arrives.
    const QString cfg = fixture.backed.fixture().configPath();
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, QStringLiteral("id:compose1@example.org and "
                                         "tag:passed")) == 1,
        15000);
}

void TestMainWindow::aCloseDuringTheCountdownIsRefused()
{
    ComposeFixture fixture;
    // A countdown long enough to close inside. The default is 5000; this is
    // the window the guard exists for and it must be provably still open when
    // the close is attempted.
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=30000")));

    ComposeContext context = newContext();
    context.to = { QStringLiteral("someone@example.org") };

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, fixture.config(), fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction = window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);
    body->setPlainText(QStringLiteral("Sent after a countdown."));

    sendAction->trigger();

    // Still counting down: the popup is up and nothing has been sent. The
    // sent folder is the evidence, since it is written only after the command
    // succeeds.
    auto *dialog = window->findChild<SendDialog *>();
    QVERIFY2(dialog, "no send popup");
    QVERIFY2(!dialog->isCommitted(), "the countdown already committed");

    // Close during the countdown. Refused: accepting it would destroy this
    // window, take the parented SendDialog down with it, and committed() would
    // never fire. The user pressed Send, watched a countdown, and would
    // believe the mail went.
    window->close();

    // Given a moment for a deletion event to be delivered if one was posted,
    // then asserted still alive. An immediate check would pass against a
    // deleteLater() already queued.
    QTest::qWait(300);
    QVERIFY2(!window.isNull(),
             "the close was accepted during the countdown, so the send was "
             "silently abandoned after the user pressed Send");
    QVERIFY2(window->isVisible() || !window.isNull(), "the window went away");

    // The send never happened, which is the point: nothing was filed.
    const QString sentCur = fixture.mailRoot() + QStringLiteral("/acct/Sent/cur");
    QCOMPARE(QDir(sentCur, {}, QDir::Name, QDir::Files).count(), 0u);

    // Cleaned up by hand, since the window refuses to close while the popup is
    // up and the test must not leak it into the next case.
    delete window;
}

void TestMainWindow::aFailedSendKeepsTheTextThatFailedToGo()
{
    ComposeFixture fixture;
    QVERIFY(fixture.build(QStringLiteral("Drafts"), QStringLiteral("Sent"),
                          QStringLiteral("send_delay_ms=0")));

    QTemporaryDir stubDir;
    QVERIFY(stubDir.isValid());
    const QString stub = stubDir.filePath(QStringLiteral("fail.sh"));
    {
        QFile script(stub);
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&script);
        out << "#!/bin/sh\ncat > /dev/null\necho 'refused' >&2\nexit 1\n";
    }
    QVERIFY(QFile::setPermissions(
        stub, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                  | QFileDevice::ExeOwner));

    Config config;
    {
        const QString path = stubDir.filePath(QStringLiteral("fail.conf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.acct]\n"
            << "name=Test User\naddress=user@example.org\n"
            << "maildir=acct\ntrash=Trash\ndrafts=Drafts\nsent=Sent\n"
            << "send_command=" << stub << "\n"
            << "\n[compose]\nsend_delay_ms=0\n";
        file.close();
        config.load(path);
    }

    ComposeContext context = newContext();
    context.to = { QStringLiteral("someone@example.org") };

    QPointer<ComposeWindow> window =
        new ComposeWindow(context, config, fixture.mailRoot());
    auto *body = window->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *sendAction = window->findChild<QAction *>(QStringLiteral("compose_send"));
    QVERIFY(body && sendAction);

    // An OLD revision on disk, then an edit that is not saved. send() builds
    // from the widgets without saving, so without the fix the file left behind
    // after the failure is the old text: the user watches their correction be
    // sent, sees it fail, and gets the uncorrected version back.
    body->setPlainText(QStringLiteral("The ORIGINAL text."));
    QVERIFY(window->saveDraftNow());
    QCOMPARE(fixture.draftCount(), 1);

    body->setPlainText(QStringLiteral("The CORRECTED text."));
    sendAction->trigger();

    auto *pane = window->findChild<QWidget *>(QStringLiteral("sendLogPane"));
    QVERIFY(pane);
    QTRY_VERIFY_WITH_TIMEOUT(!pane->isHidden(), 15000);
    QVERIFY2(!window.isNull(), "a failed send closed the composer");

    // Exactly one draft, and it is the text that was attempted.
    QCOMPARE(fixture.draftCount(), 1);
    const QStringList files =
        QDir(fixture.draftsCur(), {}, QDir::Name, QDir::Files).entryList();
    QCOMPARE(files.size(), 1);
    QFile written(fixture.draftsCur() + QLatin1Char('/') + files.first());
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray bytes = written.readAll();
    QVERIFY2(bytes.contains("The CORRECTED text."),
             "the draft kept after a failed send is not what was attempted");
    QVERIFY2(!bytes.contains("The ORIGINAL text."),
             "the draft kept after a failed send is the PRE-EDIT revision");

    delete window;
}

void TestMainWindow::aSmallSizeLimitIsNotDescribedAsZeroMegabytes()
{
    // Integer MB division made every figure under a megabyte read as "0 MB",
    // in BOTH halves of the same sentence: "'x' is 0 MB. Many mail servers
    // refuse messages above about 0 MB."
    QVERIFY2(!ComposeWindow::humanSize(500 * 1024).contains(QStringLiteral("0 MB")),
             "half a megabyte is described as 0 MB");
    QVERIFY2(!ComposeWindow::humanSize(1000).contains(QStringLiteral("0 MB")),
             "a kilobyte is described as 0 MB");

    // The unit steps down rather than reporting zero of a larger one.
    QVERIFY(ComposeWindow::humanSize(500 * 1024).contains(QStringLiteral("KB")));
    QVERIFY(ComposeWindow::humanSize(512).contains(QStringLiteral("bytes")));

    // A decimal while the figure is small enough for it to say something, so
    // 26 MB and 26.2 MB are not the same string.
    QVERIFY(ComposeWindow::humanSize(26214400).contains(QStringLiteral("MB")));
    QVERIFY2(ComposeWindow::humanSize(1024 * 1024 * 3 / 2)
                 .contains(QStringLiteral(".")),
             "1.5 MB lost its decimal");
}

void TestMainWindow::theBusinessSenderListIsLoadedAtStartup()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("@cofidis.it\n");
    file.close();

    const Config config;
    MainWindow window(config);
    window.loadBusinessSenders(path);

    QVERIFY(window.businessSendersForTest().domains.contains(
        QStringLiteral("cofidis.it")));
}

void TestMainWindow::theUnreadActionNamesTheThreadOnAConversationRow()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(model && view);

    ThreadSummary one = makeThread(QStringLiteral("t1"),
                                   QStringList{ QStringLiteral("unread") });
    one.totalCount = 1;
    ThreadSummary many = makeThread(QStringLiteral("t2"),
                                    QStringList{ QStringLiteral("unread") });
    many.totalCount = 4;
    model->appendBatch({ one, many });

    auto *action = window.findChild<QAction *>(QStringLiteral("toggle_unread"));
    QVERIFY(action);

    selectThreadRow(view, 0);
    QApplication::processEvents();
    const QString onMessage = action->text();

    selectThreadRow(view, 1);
    QApplication::processEvents();
    const QString onThread = action->text();

    QVERIFY2(onMessage != onThread,
             "the label reads the same on a message and on a conversation, so "
             "nothing tells the user which one the key will act on");
    QVERIFY2(onThread.contains(QStringLiteral("thread"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a conversation row's label does not "
                                       "name the thread: %1").arg(onThread)));
}

void TestMainWindow::deleteIsAbsentOnAReplyRow()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(model && view);

    ThreadSummary first = makeThread(QStringLiteral("t1"), {});
    first.totalCount = 1;
    ThreadSummary many = makeThread(QStringLiteral("t2"), {});
    many.totalCount = 2;
    model->appendBatch({ first, many });

    MessageNode root;
    root.messageId = QStringLiteral("m1");
    root.threadId = QStringLiteral("t2");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m2");
    reply.threadId = QStringLiteral("t2");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t2"), { root, reply });

    const QModelIndex thread = model->index(1, 0, QModelIndex());
    view->expand(thread);
    const QModelIndex replyRow = model->index(0, 0, thread);
    view->selectionModel()->select(
        replyRow, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(replyRow);
    QApplication::processEvents();

    auto *del = window.findChild<QAction *>(QStringLiteral("delete"));
    QVERIFY(del);
    QVERIFY2(!del->isVisible() || !del->isEnabled(),
             "Delete is offered on a reply: deleting is a conversation-level "
             "action and a single reply cannot be removed from a thread");

    auto *archive = window.findChild<QAction *>(QStringLiteral("archive"));
    QVERIFY(archive);
    QVERIFY2(!archive->isVisible() || !archive->isEnabled(),
             "Archive is offered on a reply: it is conversation-level for the "
             "same reason Delete is");

    // And the mirror: on the conversation row itself both are back, so the
    // hide is about what the row IS and not a stuck flag.
    selectThreadRow(view, 1);
    QApplication::processEvents();
    QVERIFY2(del->isVisible() && del->isEnabled(),
             "Delete stayed hidden on a conversation row");
    QVERIFY2(archive->isVisible() && archive->isEnabled(),
             "Archive stayed hidden on a conversation row");
}

void TestMainWindow::theWholeThreadSubmenuIsGone()
{
    const Config config;
    MainWindow window(config);

    for (const QString &name : { QStringLiteral("archive_thread"),
                                 QStringLiteral("delete_thread"),
                                 QStringLiteral("spam_thread"),
                                 QStringLiteral("flag_thread"),
                                 QStringLiteral("mark_thread_read"),
                                 QStringLiteral("mark_thread_unread") }) {
        QVERIFY2(!window.findChild<QAction *>(name),
                 qPrintable(QStringLiteral("%1 still exists; the scope now "
                                           "comes from the row, so a separate "
                                           "action is a second answer to a "
                                           "settled question").arg(name)));
    }

    QVERIFY2(window.findChildren<QMenu *>(
                     QStringLiteral("threadActionsMenu")).isEmpty(),
             "the Whole thread submenu is still built");
}

void TestMainWindow::forwardAndSaveAreAbsentOnAConversationRow()
{
    // A conversation row shows no message, so the three actions that need one
    // cannot mean what they usually do. Forward and Save simply go; Reply
    // becomes "reply to the thread" and is covered by the next test.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(model && view);

    ThreadSummary one = makeThread(QStringLiteral("t1"), {});
    one.totalCount = 1;
    ThreadSummary many = makeThread(QStringLiteral("t2"), {});
    many.totalCount = 3;
    model->appendBatch({ one, many });

    auto *forward = window.findChild<QAction *>(QStringLiteral("forward"));
    auto *save = window.findChild<QAction *>(QStringLiteral("save_message"));
    auto *replyAll = window.findChild<QAction *>(QStringLiteral("reply_all"));
    auto *noQuote =
        window.findChild<QAction *>(QStringLiteral("reply_no_quote"));
    QVERIFY(forward && save && replyAll && noQuote);

    selectThreadRow(view, 0);
    QApplication::processEvents();
    QVERIFY2(forward->isVisible() && save->isVisible(),
             "Forward and Save are hidden on a one-message row, where they "
             "mean exactly what they always did");

    selectThreadRow(view, 1);
    QApplication::processEvents();
    QVERIFY2(!forward->isVisible(),
             "Forward is offered on a conversation row, which shows no "
             "message to forward");
    QVERIFY2(!save->isVisible(),
             "Save is offered on a conversation row, which names no file");
    QVERIFY2(!replyAll->isVisible() && !noQuote->isVisible(),
             "the reply variants are offered on a conversation row, where "
             "there is one reply action and it is the thread's");
}

void TestMainWindow::replyOnAConversationRowNamesTheThread()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(model && view);

    ThreadSummary one = makeThread(QStringLiteral("t1"), {});
    one.totalCount = 1;
    ThreadSummary many = makeThread(QStringLiteral("t2"), {});
    many.totalCount = 3;
    model->appendBatch({ one, many });

    auto *reply = window.findChild<QAction *>(QStringLiteral("reply"));
    QVERIFY(reply);

    selectThreadRow(view, 0);
    QApplication::processEvents();
    const QString onMessage = reply->text();

    selectThreadRow(view, 1);
    QApplication::processEvents();
    const QString onThread = reply->text();

    QVERIFY2(reply->isVisible(),
             "Reply disappeared on a conversation row; one reply action stays, "
             "and it answers the thread");
    QVERIFY2(onMessage != onThread,
             "Reply reads the same on a message and on a conversation, so "
             "nothing says the answer goes to the whole thread");
    QVERIFY2(onThread.contains(QStringLiteral("thread"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a conversation row's Reply does not "
                                       "name the thread: %1").arg(onThread)));
}

void TestMainWindow::replyToAConversationAnswersItsNewestMessage()
{
    // The routing, which the label test does NOT cover: a probe on the action's
    // TEXT passes with the conversation branch of composeReply() deleted
    // outright, measured. This asserts through the composer that opens.
    //
    // The newest message rather than the first is the whole decision. A card
    // stands above a conversation and shows its OPENING post, so answering
    // what the card displays would thread the reply off a message the
    // discussion has moved on from: In-Reply-To and References would fork the
    // thread, and the recipients would be whoever was in it at the start.
    //
    // Three senders, one per message, so "answered the newest" is
    // distinguishable from "answered the first" AND from "answered any of
    // them". Reply-all, so the To and Cc together carry the whole cast and the
    // assertion is about which message supplied the headers, not about which
    // fold the addresses landed in.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("work/inbox"), QStringLiteral("rt0@example.org"),
        QStringLiteral("RT root"), QStringLiteral("first@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Root body."), false));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("work/inbox"), QStringLiteral("rt1@example.org"),
        QStringLiteral("Re: RT root"), QStringLiteral("middle@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Reply one."), false, QStringLiteral("rt0@example.org")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("work/inbox"), QStringLiteral("rt2@example.org"),
        QStringLiteral("Re: RT root"), QStringLiteral("newest@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 12:00:00 +0200"),
        QStringLiteral("Reply two."), false, QStringLiteral("rt0@example.org")));
    QVERIFY2(backed.buildWithAccounts(
                 { { QStringLiteral("work"), QStringLiteral("work"),
                     QString(), QStringLiteral("/bin/true"),
                     QStringLiteral("you@example.org") } }),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(QStringLiteral("tag:inbox"));
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1
                                 && !window.mailRootForTesting().isEmpty(),
                             15000);

    const QModelIndex row = model->index(0, 0, QModelIndex());
    QVERIFY2(model->isConversationRow(row),
             "the fixture's row is not a conversation, so Reply would take the "
             "ordinary message path and this test would assert nothing");

    view->setCurrentIndex(row);
    view->selectionModel()->select(
        row, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QApplication::processEvents();

    auto *reply = window.findChild<QAction *>(QStringLiteral("reply"));
    QVERIFY(reply);
    QVERIFY2(reply->isEnabled(),
             "the account cannot send, so Reply is disabled and the gesture "
             "never reaches the code under test");
    reply->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(window.openComposerCount() == 1, 15000);
    ComposeWindow *composer = window.openComposersForTest().value(0);
    QVERIFY(composer);

    auto *to = composer->findChild<QLineEdit *>(QStringLiteral("to"));
    auto *cc = composer->findChild<QLineEdit *>(QStringLiteral("cc"));
    QVERIFY(to && cc);
    const QString recipients = to->text() + QLatin1Char(' ') + cc->text();

    QVERIFY2(recipients.contains(QStringLiteral("newest@example.org")),
             qPrintable(QStringLiteral("the reply does not answer the "
                                       "conversation's newest message: %1")
                            .arg(recipients)));

    // And it QUOTES NOTHING, per the user: "we just add an answer to the
    // thread". A quoted body would be the newest message's text, which is a
    // second, separate way for this to be wrong.
    auto *body = composer->findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    QVERIFY2(!body->toPlainText().contains(QStringLiteral("Reply two.")),
             qPrintable(QStringLiteral("the reply quoted the message it "
                                       "answers: %1").arg(body->toPlainText())));

    composer->show();
    composer->close();
}

void TestMainWindow::replyIsUntouchedOnAMessageRow()
{
    // The other half of the rule: a reply row is a message like any other, so
    // every compose action behaves exactly as it did before item 177.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    QVERIFY(model && view);

    ThreadSummary many = makeThread(QStringLiteral("t1"), {});
    many.totalCount = 2;
    model->appendBatch({ many });

    MessageNode root;
    root.messageId = QStringLiteral("m1");
    root.threadId = QStringLiteral("t1");
    root.depth = 0;
    MessageNode reply;
    reply.messageId = QStringLiteral("m2");
    reply.threadId = QStringLiteral("t1");
    reply.depth = 1;
    model->setThreadMessages(QStringLiteral("t1"), { root, reply });

    const QModelIndex thread = model->index(0, 0, QModelIndex());
    view->expand(thread);
    const QModelIndex replyRow = model->index(0, 0, thread);
    view->selectionModel()->select(
        replyRow,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->setCurrentIndex(replyRow);
    QApplication::processEvents();

    for (const QString &name : { QStringLiteral("reply"),
                                 QStringLiteral("reply_all"),
                                 QStringLiteral("reply_no_quote"),
                                 QStringLiteral("forward"),
                                 QStringLiteral("save_message") }) {
        auto *action = window.findChild<QAction *>(name);
        QVERIFY(action);
        QVERIFY2(action->isVisible(),
                 qPrintable(QStringLiteral("%1 is hidden on a reply row, which "
                                           "is an ordinary message").arg(name)));
    }

    auto *replyAction = window.findChild<QAction *>(QStringLiteral("reply"));
    QVERIFY2(!replyAction->text().contains(QStringLiteral("thread"),
                                           Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a reply row's Reply claims to answer "
                                       "the thread: %1")
                            .arg(replyAction->text())));
}

void TestMainWindow::aConversationStaysWhileAnyMessageMatches()
{
    // The reported case: a 44-message thread with two replies still unread.
    // Reading one message must not evict the conversation.
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && queryEdit);

    queryEdit->setText(
        config.resolvedQuery(Config::builtinFilter(QStringLiteral("unread")),
                             QString()));

    ThreadSummary other = makeThread(QStringLiteral("t1"),
                                     QStringList{ QStringLiteral("unread") });
    other.totalCount = 1;
    ThreadSummary big = makeThread(QStringLiteral("t2"),
                                   QStringList{ QStringLiteral("unread") });
    big.totalCount = 44;
    model->appendBatch({ other, big });

    // One message of the conversation is read, and specifically the one its
    // ROW displays: naming any other id makes the model answer "no thread" and
    // the test measures nothing, passing against an eviction that judges on
    // the card's own tags. The union still says unread, so the row stays.
    window.sendMessageTagChangeForTesting({ big.firstMessageId }, {},
                                          { QStringLiteral("unread") },
                                          QStringLiteral("Mark read"));

    QCOMPARE(model->rowCount(QModelIndex()), 2);
    QCOMPARE(model->threadAt(1).threadId, QStringLiteral("t2"));

    // The same question put to the judgement ITSELF, which is where the union
    // rule lives. The send path above reaches it only for a thread of one, so
    // asserting through that path alone would let an eviction judging the
    // card's own tags pass: the long thread is filtered out before the model
    // is ever asked. Here the row is named directly, and the only thing
    // keeping it is that its union still carries `unread` while the message
    // its card draws no longer does.
    QVERIFY2(!model->messageById(big.firstMessageId)
                  .tags.contains(QStringLiteral("unread")),
             "the fixture's card message is still unread, so this assertion "
             "cannot tell the union apart from the card's own tags");
    model->removeThreadsWithoutTag(QStringList{ QStringLiteral("t2") },
                                   QStringLiteral("unread"));
    QCOMPARE(model->rowCount(QModelIndex()), 2);

    // And the counterpart, so the test is about the union and not about
    // nothing ever being evicted: a one-message row's union IS its message, so
    // reading it takes the row out at once.
    window.sendMessageTagChangeForTesting({ other.firstMessageId }, {},
                                          { QStringLiteral("unread") },
                                          QStringLiteral("Mark read"));

    QCOMPARE(model->rowCount(QModelIndex()), 1);
    QCOMPARE(model->threadAt(0).threadId, QStringLiteral("t2"));
}

void TestMainWindow::aConversationLeavesWhenItsUnionEmpties()
{
    const Config config;
    MainWindow window(config);

    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<QTreeView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    queryEdit->setText(
        config.resolvedQuery(Config::builtinFilter(QStringLiteral("unread")),
                             QString()));

    ThreadSummary keep = makeThread(QStringLiteral("t1"),
                                    QStringList{ QStringLiteral("unread") });
    keep.totalCount = 1;
    ThreadSummary go = makeThread(QStringLiteral("t2"),
                                  QStringList{ QStringLiteral("unread") });
    go.totalCount = 4;
    model->appendBatch({ keep, go });

    // Selected elsewhere, so the never-evict-the-current-row rule is not what
    // is being measured here.
    selectThreadRow(view, 0);
    QApplication::processEvents();

    window.sendThreadTagChangeForTesting({ QStringLiteral("t2") }, {},
                                         { QStringLiteral("unread") },
                                         QStringLiteral("Mark thread read"));

    QCOMPARE(model->rowCount(QModelIndex()), 1);
    QCOMPARE(model->threadAt(0).threadId, QStringLiteral("t1"));
}

void TestMainWindow::undoingAMarkReadRestoresOnlyWhatWasUnread()
{
    // Item 176, end to end. The thread has messages in DISAGREEING states:
    // two in the same state answer identically whichever way the code
    // resolves them, so a test built on agreement passes against the bug.
    //
    // Measured on the user's real mail before the fix: a thread of 44 with 2
    // unread was marked read, undone, and came back with 43 unread. Because
    // maildir.synchronize_flags is on, that rewrote the files and would have
    // reached the server.
    WorkerBackedWindow backed;
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("ur0@example.org"),
        QStringLiteral("UR root"), QStringLiteral("sender@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 10:00:00 +0200"),
        QStringLiteral("Root body."), false));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("ur1@example.org"),
        QStringLiteral("Re: UR root"), QStringLiteral("other@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 11:00:00 +0200"),
        QStringLiteral("Already read."), false,
        QStringLiteral("ur0@example.org")));
    QVERIFY(backed.fixture().addMessage(
        QStringLiteral("acct/inbox"), QStringLiteral("ur2@example.org"),
        QStringLiteral("Re: UR root"), QStringLiteral("third@example.org"),
        QStringLiteral("Fri, 14 Aug 2026 12:00:00 +0200"),
        QStringLiteral("The only unread one."), true,
        QStringLiteral("ur0@example.org")));
    QVERIFY2(backed.build(QStringLiteral("acct"), QStringLiteral("acct"),
                          QStringLiteral("Trash")),
             qPrintable(backed.error()));

    MainWindow window(backed.config());
    auto *model = window.findChild<ThreadListModel *>();
    auto *view = window.findChild<ThreadListView *>();
    auto *queryEdit =
        window.findChild<QLineEdit *>(QStringLiteral("queryEdit"));
    QVERIFY(model && view && queryEdit);

    const QString cfg = backed.fixture().configPath();
    const QString thread = QStringLiteral("thread:{id:ur0@example.org}");

    // A `thread:` query, not `tag:unread`: the row must survive the write for
    // the undo to be driven through the interface at all.
    queryEdit->setText(thread);
    queryEdit->returnPressed();
    QTRY_VERIFY_WITH_TIMEOUT(model->rowCount(QModelIndex()) == 1, 15000);

    QCOMPARE(notmuchCount(cfg, thread), 3);
    QCOMPARE(notmuchCount(cfg, thread + QStringLiteral(" and tag:unread")), 1);

    // A conversation row, so this is the thread-scoped write.
    view->setCurrentIndex(model->index(0, 0, QModelIndex()));
    QVERIFY(model->isConversationRow(model->index(0, 0, QModelIndex())));
    window.findChild<QAction *>(QStringLiteral("toggle_unread"))->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, thread + QStringLiteral(" and tag:unread")) == 0,
        15000);

    window.findChild<QAction *>(QStringLiteral("undo"))->trigger();

    // ONE message unread again, the one that was. Before the fix this was 3.
    QTRY_VERIFY_WITH_TIMEOUT(
        notmuchCount(cfg, thread + QStringLiteral(" and tag:unread")) == 1,
        15000);
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:ur2@example.org and "
                                              "tag:unread")),
             1);
    QCOMPARE(notmuchCount(cfg, QStringLiteral("id:ur0@example.org and "
                                              "tag:unread")),
             0);
}

#include "test_mainwindow.moc"
