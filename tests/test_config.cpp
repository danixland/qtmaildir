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
#include <QTemporaryDir>
#include <QTranslator>
#include <QSettings>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "config.h"
#include "mailsync.h"

class TestConfig : public QObject
{
    Q_OBJECT
private slots:
    void parsesAccounts();
    void parsesSavedQueries();
    void missingSyncCommandIsEmpty();
    void syncLogDefaultsToTheScriptsOwnPath();
    void syncLogCanBeOverridden();
    void toolbarIconSizeDefaultsTo24();
    void toolbarIconSizeIsActuallyRead();
    void toolbarIconSizeIsClampedAndReported();
    void toolbarIconSizeRejectsGarbage();
    void autoSyncDelayDefaultsTo2000();
    void autoSyncDelayIsActuallyRead();
    void autoSyncDelayKeepsZeroAndNegative();
    void autoSyncDelayRejectsGarbage();
    void accountWithoutMaildirIsRejected();
    void scopedQueryWrapsCorrectly();
    void absentSyncCommandIsNoticeNotProblem();
    void brokenSyncCommandIsAProblem();
    void malformedAccountIsAProblem();
    void validConfigHasNoProblems();
    void startupQueryDefaultsToUnread();
    void startupQueryHonoursTheConfiguredName();
    void unknownStartupQueryFallsBackAndReports();
    void savedQueriesKeepTheirDocumentOrder();
    void savedQueryFieldsAreRead();
    void unknownFieldsSurviveARoundTrip();
    void savedQueriesRoundTripUnchanged();
    void migrationWritesJsonAndLeavesTheIniByteIdentical();
    void migrationPinsEveryEntry();
    void jsonWinsOnceItExists();
    void malformedQueriesFileIsAProblemNotACrash();
    void futureVersionIsRefusedAndReported();
    void startupQueryFallsBackToABuiltinFilter();
    void scopedSavedQueryParenthesisesADisjunction();
    void aGeneratedQueryResolvesFromTheAccounts();
    void aGeneratedQueryTracksAConfigChange();
    void anUnknownGeneratorResolvesToNothingAndReports();
    void migrationAddsSentWhenAnAccountHasOne();
    void migrationAddsNoSentWithoutTheKey();
    void aGeneratedEntryWritesNoRedundantKeys();
    void generalSectionKeysAreActuallyRead();
    void messageZoomDefaultsAndValidates();
    void messageZoomOutOfRangeIsReported();
    void completionOnFocusDefaultsToFalse();
    void completionOnFocusIsActuallyRead();
    void markReadDelayDefaultsToTwoSeconds();
    void markReadDelayIsActuallyRead();
    void markReadDelayAcceptsZeroAndNegative();
    void dateFormatDefaultsToEmpty();
    void dateFormatIsActuallyRead();
    void dateFormatWithoutAFieldIsRejectedAndReported();
    void markReadDelayRejectsGarbage();
    void syncOnExitDefaultsToAsk();
    void syncOnExitReadsAllThreeValues();
    void syncOnExitWarnsOnGarbage();
    void extraMimetypesAppendToBuiltins();
    void extraMimetypeDescriptionMayContainComma();
    void malformedExtraMimetypeIsSkipped();
    void syncChannelDefaultsToTheAccountKey();
    void syncChannelIsActuallyRead();
    void sentQueryIsEmptyWithoutTheKey();
    void sentQueryComposesThePath();
    void sentQuerySurvivesABracketedPath();
    void sentQueryComposesWithScopedQuery();
    void allSentQueryIsEmptyWhenNoAccountHasOne();
    void allSentQuerySkipsAccountsWithoutTheKey();
    void allSentQueryJoinsEveryConfiguredAccount();
    void aStoredGeneratedQueryIsUnpinnedNotDropped();
    void theStartupAccountIsReadAndValidated();
    void theStartupAccountTakesTheKeyNotTheSyncChannel();
    void theStartupQueryCanNameABuiltinFilter();
    void theStartupQuerySurvivesATranslatedFilterName();
    void theLanguageKeyOverridesTheEnvironment();
    void theLanguageKeyRejectsWhatIsNotALocale();
    void theStartupQueryPrefersASavedQueryOverAFilterOfTheSameName();
    void anUnmatchedStartupQueryFallsBackToAFilterNotAStrayQuery();
    void theFlaggedFilterIsCalledImportant();
    void everyBuiltinFilterIsAKnownGenerator();
    void aFilterAcrossAllAccountsIsTheUnscopedQuery();
    void aTagFilterScopedToAnAccountCarriesThatAccountsPath();
    void sentScopedToAnAccountIsThatAccountsSentFolderAlone();
    void sentScopedToAnAccountWithNoSentFolderMatchesNothing();
    void aFilterKeepsItsViewMode();
    void draftsQueryIsEmptyWithoutTheKey();
    void draftsQuerySurvivesABracketedPath();
    void allDraftsQuerySkipsAccountsWithoutTheKey();
    void allDraftsQueryIsIndependentOfSent();
};

static QString writeIni(const QTemporaryDir &dir, const QString &body)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(body.toUtf8());
    f.close();
    return path;
}

void TestConfig::parsesAccounts()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
        "drafts=Drafts\n"
        "\n"
        "[account.personal]\n"
        "name=Test User\n"
        "address=me@example.net\n"
        "maildir=personal\n"
    ));

    Config config;
    config.load(path);

    QCOMPARE(config.accounts().size(), 2);

    const Account work = config.account(QStringLiteral("work"));
    QCOMPARE(work.key, QStringLiteral("work"));
    QCOMPARE(work.name, QStringLiteral("Test User"));
    QCOMPARE(work.address, QStringLiteral("user@example.org"));
    QCOMPARE(work.maildir, QStringLiteral("work-mail"));
    QCOMPARE(work.drafts, QStringLiteral("Drafts"));

    // drafts is optional in v1 (send is v2).
    const Account personal = config.account(QStringLiteral("personal"));
    QVERIFY(personal.drafts.isEmpty());
    QVERIFY(personal.isValid());
}

void TestConfig::parsesSavedQueries()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[queries]\n"
        "Inbox=tag:inbox\n"
        "Unread=tag:unread\n"
    ));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 2);
    // Reached through migration since item 23: [queries] is read once, when
    // queries.json is absent, and childKeys() is alphabetical, so the migrated
    // order is too. From then on the JSON's own order wins, which is what
    // savedQueriesKeepTheirDocumentOrder() covers.
    QCOMPARE(queries.at(0).name, QStringLiteral("Inbox"));
    QCOMPARE(queries.at(0).query, QStringLiteral("tag:inbox"));
}

void TestConfig::missingSyncCommandIsEmpty()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[general]\n"));

    Config config;
    config.load(path);

    QVERIFY(config.syncCommand().isEmpty());
    // The UI uses this to disable the Sync button with a tooltip.
    QVERIFY(!config.warnings().isEmpty());
}

void TestConfig::syncLogDefaultsToTheScriptsOwnPath()
{
    // Item 54 reads this file to learn whether a cron sync succeeded, so an
    // unset key must point where assets/mailsync.sh actually writes, not be
    // empty. Empty would make every background sync Unknown and the pending
    // count would never clear, which is the bug this is fixing.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[general]\n"));

    Config config;
    config.load(path);

    QCOMPARE(config.syncLog(), MailSync::defaultLogPath());
    QVERIFY(config.syncLog().endsWith(QStringLiteral("/.local/state/mailsync.log")));
}

void TestConfig::syncLogCanBeOverridden()
{
    // The script's LOGFILE is editable, and a user who moved it would otherwise
    // get an indicator that never clears with nothing explaining why.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[sync]\n"
        "command=/bin/true\n"
        "log=/var/log/mail/sync.log\n"
    ));

    Config config;
    config.load(path);

    QCOMPARE(config.syncLog(), QStringLiteral("/var/log/mail/sync.log"));
}

void TestConfig::toolbarIconSizeDefaultsTo24()
{
    // The desktop's own metric is the obvious default and was rejected: this
    // style reports PM_ToolBarIconSize as 16, which is a small click target for
    // a toolbar that now shows icons with no text beside them. 24 is a normal
    // toolbar size, and setting the key back to 16 restores the theme's value.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[general]\n"));

    Config config;
    config.load(path);

    QCOMPARE(config.toolbarIconSize(), 24);
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::toolbarIconSizeIsActuallyRead()
{
    // [general] keys are read WITHOUT the general/ prefix, per the note at the
    // top of Config::load(). A key that silently matched nothing would leave
    // the default in place and look exactly like a working default.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\n"
        "toolbar_icon_size = 32\n"
    ));

    Config config;
    config.load(path);

    QCOMPARE(config.toolbarIconSize(), 32);
}

void TestConfig::toolbarIconSizeIsClampedAndReported()
{
    // Out of range is clamped rather than honoured: a 4px icon is invisible and
    // a 4000px one makes the toolbar taller than the window, and neither is
    // recoverable from the UI the value just broke. Reported, because silently
    // ignoring what the user asked for is how message_zoom's documented 0.5-3.0
    // range came to be unenforced without anyone noticing.
    QTemporaryDir dir;
    const QString tooBig = writeIni(dir, QStringLiteral(
        "[general]\n"
        "toolbar_icon_size = 4000\n"
    ));

    Config big;
    big.load(tooBig);
    QCOMPARE(big.toolbarIconSize(), 64);
    QVERIFY(!big.warnings().isEmpty() || !big.problems().isEmpty());

    QTemporaryDir dir2;
    const QString tooSmall = writeIni(dir2, QStringLiteral(
        "[general]\n"
        "toolbar_icon_size = 2\n"
    ));

    Config small;
    small.load(tooSmall);
    QCOMPARE(small.toolbarIconSize(), 16);
}

void TestConfig::toolbarIconSizeRejectsGarbage()
{
    // Unparseable falls back to the default and says so, matching how
    // mark_read_delay_ms treats the same mistake.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\n"
        "toolbar_icon_size = enormous\n"
    ));

    Config config;
    config.load(path);

    QCOMPARE(config.toolbarIconSize(), 24);
    QVERIFY(!config.problems().isEmpty());
}

void TestConfig::autoSyncDelayDefaultsTo2000()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[general]\n"));

    Config config;
    config.load(path);

    QCOMPARE(config.autoSyncDelayMs(), 2000);
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::autoSyncDelayIsActuallyRead()
{
    // [general] keys are read WITHOUT the general/ prefix. A key that silently
    // matched nothing would leave the 2000 default in place and pass every
    // behavioural test in test_mainwindow, since those arm the timer at the
    // default anyway.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\n"
        "auto_sync_delay_ms = 500\n"
    ));

    Config config;
    config.load(path);

    QCOMPARE(config.autoSyncDelayMs(), 500);
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::autoSyncDelayKeepsZeroAndNegative()
{
    // Neither is an error and neither may be clamped: 0 means sync on the next
    // trip through the event loop, and negative disables the automatic sync,
    // which is the only way to get the pre-0.16.0 behaviour back. Clamping
    // either to the default would take that switch away.
    QTemporaryDir dir;
    const QString zero = writeIni(dir, QStringLiteral(
        "[general]\n"
        "auto_sync_delay_ms = 0\n"
    ));

    Config immediate;
    immediate.load(zero);
    QCOMPARE(immediate.autoSyncDelayMs(), 0);
    QVERIFY(immediate.problems().isEmpty());

    QTemporaryDir dir2;
    const QString off = writeIni(dir2, QStringLiteral(
        "[general]\n"
        "auto_sync_delay_ms = -1\n"
    ));

    Config disabled;
    disabled.load(off);
    QCOMPARE(disabled.autoSyncDelayMs(), -1);
    QVERIFY(disabled.problems().isEmpty());
}

void TestConfig::autoSyncDelayRejectsGarbage()
{
    // Falls back to the default and says so, matching mark_read_delay_ms.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\n"
        "auto_sync_delay_ms = soon\n"
    ));

    Config config;
    config.load(path);

    QCOMPARE(config.autoSyncDelayMs(), 2000);
    QVERIFY(!config.problems().isEmpty());
}

void TestConfig::accountWithoutMaildirIsRejected()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[sync]\n"
        "command=/bin/true\n"
        "\n"
        "[account.broken]\n"
        "name=No Maildir\n"
        "address=x@example.org\n"
    ));

    Config config;
    config.load(path);

    // Rejected, reported, and not offered to the user as a scope.
    QCOMPARE(config.accounts().size(), 0);
    QCOMPARE(config.warnings().size(), 1);
    QVERIFY(config.warnings().first().contains(QStringLiteral("broken")));
}

void TestConfig::scopedQueryWrapsCorrectly()
{
    Account account;
    account.key = QStringLiteral("work");
    account.maildir = QStringLiteral("work-mail");

    QCOMPARE(account.scopedQuery(QStringLiteral("tag:inbox")),
             QStringLiteral("path:\"work-mail/**\" and (tag:inbox)"));

    // An empty query still scopes to the account rather than matching nothing.
    QCOMPARE(account.scopedQuery(QString()),
             QStringLiteral("path:\"work-mail/**\""));
}

void TestConfig::absentSyncCommandIsNoticeNotProblem()
{
    // An optional feature simply not being configured must not interrupt
    // startup: the modal would fire on every launch and train the user to
    // dismiss dialogs without reading them.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "maildir=work-mail\n"));

    Config config;
    config.load(path);

    QCOMPARE(config.warnings().size(), 1);
    QVERIFY(config.warnings().first().contains(QStringLiteral("No sync command")));
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::brokenSyncCommandIsAProblem()
{
    // Configured but missing is different: the user asked for sync and is not
    // getting it, so they need telling.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[sync]\n"
        "command=/nonexistent/qtmaildir-test/mailsync.sh\n"
        "\n"
        "[account.work]\n"
        "maildir=work-mail\n"));

    Config config;
    config.load(path);

    QCOMPARE(config.problems().size(), 1);
    QVERIFY(config.problems().first().contains(QStringLiteral("does not exist")));
    // Problems are a subset of warnings, so a caller wanting everything needs
    // only warnings().
    QVERIFY(config.warnings().contains(config.problems().first()));
}

void TestConfig::malformedAccountIsAProblem()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.broken]\n"
        "name=No Maildir Here\n"));

    Config config;
    config.load(path);

    QVERIFY(!config.problems().isEmpty());
    QVERIFY(config.problems().first().contains(QStringLiteral("broken")));
}

void TestConfig::validConfigHasNoProblems()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[sync]\n"
        "command=/bin/true\n"
        "\n"
        "[account.work]\n"
        "maildir=work-mail\n"
        "address=user@example.org\n"));

    Config config;
    config.load(path);

    QVERIFY(config.problems().isEmpty());
    QVERIFY(config.warnings().isEmpty());
}

void TestConfig::startupQueryDefaultsToUnread()
{
    // [queries] is read through childKeys(), which sorts alphabetically, so
    // savedQueries().first() is "Flagged" here. The startup query must be
    // chosen by name, not by sort order.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[queries]\n"
        "Inbox=tag:inbox\n"
        "Unread=tag:unread\n"
        "Flagged=tag:flagged\n")));

    QCOMPARE(config.savedQueries().first().name, QStringLiteral("Flagged"));
    QCOMPARE(config.startupSavedQuery().name, QStringLiteral("Unread"));
    QCOMPARE(config.startupSavedQuery().query, QStringLiteral("tag:unread"));
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::startupQueryHonoursTheConfiguredName()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_query=Flagged\n"
        "\n"
        "[queries]\n"
        "Inbox=tag:inbox\n"
        "Unread=tag:unread\n"
        "Flagged=tag:flagged\n")));

    QCOMPARE(config.startupSavedQuery().name, QStringLiteral("Flagged"));
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::unknownStartupQueryFallsBackAndReports()
{
    // A name the user wrote that matches nothing is a problem: they asked for
    // something and are not getting it. Startup still works, on the fallback.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_query=Nonexistent\n"
        "\n"
        "[queries]\n"
        "Inbox=tag:inbox\n")));

    // The fallback is a BUILT-IN filter, not the first saved query. The old
    // behaviour looked reasonable while every install carried an Inbox entry
    // and became "startup opens a search for one sender" once the duplicated
    // entries were removed.
    QCOMPARE(config.startupSavedQuery().name, QStringLiteral("Unread"));
    QVERIFY(config.startupSavedQuery().isGenerated());
    QCOMPARE(config.problems().size(), 1);

    // The built-in default naming a query the user never created is NOT a
    // problem: they did not get it wrong, they simply have no Unread entry.
    QTemporaryDir quiet;
    Config silent;
    silent.load(writeIni(quiet, QStringLiteral(
        "[queries]\n"
        "Inbox=tag:inbox\n")));

    // And it now RESOLVES rather than falling through. The default has always
    // been "Unread"; before item 93 that named nothing unless the user happened
    // to have such an entry, so an install without one silently opened on
    // whatever came first in the file. The built-in filter of that name is
    // always there.
    QCOMPARE(silent.startupSavedQuery().name, QStringLiteral("Unread"));
    QVERIFY(silent.startupSavedQuery().isGenerated());
    QVERIFY(silent.problems().isEmpty());
}

void TestConfig::generalSectionKeysAreActuallyRead()
{
    // QSettings' INI backend treats a section literally named [general] as its
    // own fallback section and strips the prefix, so a "general/<key>" lookup
    // matches nothing. notmuch_config was read that way and had never worked.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "notmuch_config=/somewhere/notmuch-config\n"
        "\n"
        "[sync]\n"
        "command=/bin/true\n")));

    QCOMPARE(config.notmuchConfig(),
             QStringLiteral("/somewhere/notmuch-config"));
}

void TestConfig::messageZoomDefaultsAndValidates()
{
    // A QTemporaryDir per case, not one shared: writeIni() always uses the
    // same file name, and QSettings caches by path, so a second load of the
    // same path would return the first case's contents.

    // Absent: 1.0, silently. Nothing the user asked for is being ignored.
    {
        QTemporaryDir dir;
        Config config;
        config.load(writeIni(dir, QStringLiteral("[general]\n")));
        QCOMPARE(config.messageZoom(), 1.0);
        QVERIFY(config.problems().isEmpty());
    }

    {
        QTemporaryDir dir;
        Config config;
        config.load(writeIni(dir, QStringLiteral("[general]\n"
                                                 "message_zoom=1.25\n")));
        QCOMPARE(config.messageZoom(), 1.25);
        QVERIFY(config.problems().isEmpty());
    }

    // Present but unparseable is a problem: the user asked for something and
    // is not getting it, which is the line addProblem() draws.
    {
        QTemporaryDir dir;
        Config config;
        config.load(writeIni(dir, QStringLiteral("[general]\n"
                                                 "message_zoom=huge\n")));
        QCOMPARE(config.messageZoom(), 1.0);
        QCOMPARE(config.problems().size(), 1);
    }
}

void TestConfig::messageZoomOutOfRangeIsReported()
{
    // MessageView::clampZoom() already stops an out-of-range value from
    // reaching the web view, so this is not about the render. It is about the
    // silence: the key parses, so nothing ever told the user that the 500 they
    // wrote is not what they are looking at. Not clamped here, because
    // clampZoom() owns the bounds and two copies would drift.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n"
                                             "message_zoom=500\n")));
    QCOMPARE(config.problems().size(), 1);
    QVERIFY(config.problems().first().contains(QStringLiteral("500")));

    QTemporaryDir dir2;
    Config small;
    small.load(writeIni(dir2, QStringLiteral("[general]\n"
                                             "message_zoom=0.1\n")));
    QCOMPARE(small.problems().size(), 1);

    // In range stays silent.
    QTemporaryDir dir3;
    Config ok;
    ok.load(writeIni(dir3, QStringLiteral("[general]\n"
                                          "message_zoom=3.0\n")));
    QVERIFY(ok.problems().isEmpty());
}

void TestConfig::completionOnFocusDefaultsToFalse()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n")));
    QCOMPARE(config.completionOnFocus(), false);
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::completionOnFocusIsActuallyRead()
{
    // The default is false, so a test that only checks the default would pass
    // just as happily against a "general/completion_on_focus" lookup that
    // matches nothing. Round-tripping a true proves the key is really read.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n"
                                             "completion_on_focus=true\n")));
    QCOMPARE(config.completionOnFocus(), true);
}

void TestConfig::markReadDelayDefaultsToTwoSeconds()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n")));
    QCOMPARE(config.markReadDelayMs(), 2000);
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::markReadDelayIsActuallyRead()
{
    // Round-trip a value that is not the default, which is what proves the key
    // is really read: a "general/mark_read_delay_ms" lookup matches nothing and
    // would still pass a test that only checked the default.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n"
                                             "mark_read_delay_ms=500\n")));
    QCOMPARE(config.markReadDelayMs(), 500);
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::markReadDelayAcceptsZeroAndNegative()
{
    // Both are documented settings, not mistakes: 0 marks read immediately and
    // a negative value disables the behaviour entirely. Neither may be
    // clamped away or warned about.
    QTemporaryDir dir;
    Config zero;
    zero.load(writeIni(dir, QStringLiteral("[general]\n"
                                           "mark_read_delay_ms=0\n")));
    QCOMPARE(zero.markReadDelayMs(), 0);
    QVERIFY(zero.problems().isEmpty());

    QTemporaryDir otherDir;
    Config never;
    never.load(writeIni(otherDir, QStringLiteral("[general]\n"
                                                 "mark_read_delay_ms=-1\n")));
    QCOMPARE(never.markReadDelayMs(), -1);
    QVERIFY(never.problems().isEmpty());
}

void TestConfig::dateFormatDefaultsToEmpty()
{
    // Empty is what tells CardLayout to use the system's short format, which is
    // the shipped behaviour and must survive this key existing.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n")));
    QVERIFY(config.dateFormat().isEmpty());
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::dateFormatIsActuallyRead()
{
    // A pattern that is not the default, which is what proves the key is read
    // at all: a "general/date_format" lookup matches nothing and would still
    // pass a test that only checked the empty default.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n"
                                             "date_format=yyyy-MM-dd\n")));
    QCOMPARE(config.dateFormat(), QStringLiteral("yyyy-MM-dd"));
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::dateFormatWithoutAFieldIsRejectedAndReported()
{
    // The specific trap: QDateTime::toString() with a pattern carrying no date
    // or time field returns something fixed rather than failing, so this would
    // print the same string on every card and look like a rendering fault
    // rather than a config one.
    //
    // "xyz" and not a friendlier-looking word, because almost every letter is
    // a field character: "banana" formats as "bpmnpmnpm" (a is AM/PM, n is the
    // minute) and "hello" as "22ello" (h is the hour). Those are nonsense but
    // they do vary with the instant, so they are not what this rejects and the
    // check would fail against them. What it catches is a pattern whose output
    // is CONSTANT, which is the case that silently shows one date forever.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n"
                                             "date_format=xyz\n")));
    QVERIFY2(config.dateFormat().isEmpty(),
             "a pattern with no date field was accepted");
    QCOMPARE(config.problems().size(), 1);
    QVERIFY(config.problems().first().contains(QStringLiteral("xyz")));
}

void TestConfig::markReadDelayRejectsGarbage()
{
    // Absent is silent, but present-and-unparseable means the user asked for
    // something and is not getting it, which warns rather than passing quietly.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n"
                                             "mark_read_delay_ms=soon\n")));
    QCOMPARE(config.markReadDelayMs(), 2000);
    QVERIFY(!config.problems().isEmpty());
}

void TestConfig::syncOnExitDefaultsToAsk()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n")));
    QCOMPARE(config.syncOnExit(), Config::SyncOnExit::Ask);
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::syncOnExitReadsAllThreeValues()
{
    // A bool could only carry two of these. Each is a distinct behaviour at
    // exit, so each has to round-trip.
    const QList<QPair<QString, Config::SyncOnExit>> cases = {
        { QStringLiteral("ask"),    Config::SyncOnExit::Ask },
        { QStringLiteral("always"), Config::SyncOnExit::Always },
        { QStringLiteral("never"),  Config::SyncOnExit::Never },
        // Case and surrounding space are the user's, not the parser's problem.
        { QStringLiteral("  Always  "), Config::SyncOnExit::Always },
    };

    for (const auto &testCase : cases) {
        QTemporaryDir dir;
        Config config;
        config.load(writeIni(dir, QStringLiteral("[general]\nsync_on_exit=%1\n")
                                      .arg(testCase.first)));
        QCOMPARE(config.syncOnExit(), testCase.second);
        QVERIFY2(config.problems().isEmpty(),
                 qPrintable(QStringLiteral("'%1' warned").arg(testCase.first)));
    }
}

void TestConfig::syncOnExitWarnsOnGarbage()
{
    // Silently falling back would change what happens to unsynced work without
    // telling the user, so a typo has to be named.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral("[general]\n"
                                             "sync_on_exit=maybe\n")));
    QCOMPARE(config.syncOnExit(), Config::SyncOnExit::Ask);
    QVERIFY(!config.problems().isEmpty());
}

void TestConfig::extraMimetypesAppendToBuiltins()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[completion]\n"
        "extra_mimetypes = application/epub+zip|EPUB book, message/rfc822\n")));

    const QList<CompletionEntry> extra = config.extraMimetypes();
    QCOMPARE(extra.size(), 2);
    QCOMPARE(extra.at(0).value, QStringLiteral("application/epub+zip"));
    QCOMPARE(extra.at(0).description, QStringLiteral("EPUB book"));
    QCOMPARE(extra.at(1).value, QStringLiteral("message/rfc822"));
    QVERIFY(extra.at(1).description.isEmpty());
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::extraMimetypeDescriptionMayContainComma()
{
    // '|' separates value from description precisely so a description can
    // contain a comma without QSettings tearing the entry in two.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[completion]\n"
        "extra_mimetypes = \"application/epub+zip|EPUB, an ebook format\"\n")));

    const QList<CompletionEntry> extra = config.extraMimetypes();
    QCOMPARE(extra.size(), 1);
    QCOMPARE(extra.at(0).value, QStringLiteral("application/epub+zip"));
    QCOMPARE(extra.at(0).description, QStringLiteral("EPUB, an ebook format"));
}

void TestConfig::malformedExtraMimetypeIsSkipped()
{
    // One bad entry must not cost the user the rest of the list.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[completion]\n"
        "extra_mimetypes = |no value here, message/rfc822\n")));

    const QList<CompletionEntry> extra = config.extraMimetypes();
    QCOMPARE(extra.size(), 1);
    QCOMPARE(extra.at(0).value, QStringLiteral("message/rfc822"));
    QVERIFY(!config.problems().isEmpty());
}

void TestConfig::syncChannelDefaultsToTheAccountKey()
{
    // Most accounts name their mbsync channel exactly as their section key, so
    // the common case must need no config edit at all.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "maildir = work\n")));

    QCOMPARE(config.accounts().size(), 1);
    QCOMPARE(config.accounts().at(0).syncChannel(), QStringLiteral("work"));
}

void TestConfig::syncChannelIsActuallyRead()
{
    // The key exists because the two names genuinely diverge: a QSettings
    // section key may carry dots that the mbsync channel does not, and passing
    // the section key to mbsync would name a channel that does not exist.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.mail-first.last]\n"
        "maildir = mail-first.last\n"
        "channel = mail-firstlast\n")));

    QCOMPARE(config.accounts().size(), 1);
    QCOMPARE(config.accounts().at(0).syncChannel(),
             QStringLiteral("mail-firstlast"));
}

void TestConfig::sentQueryIsEmptyWithoutTheKey()
{
    // Optional exactly as drafts is. A real account can legitimately have no
    // sent folder at all, and the Sent view omits it silently rather than
    // reporting a config problem on every launch about nothing.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.provider-c]\n"
        "maildir = provider-c\n")));

    QCOMPARE(config.accounts().size(), 1);
    QVERIFY(config.accounts().at(0).sentQuery().isEmpty());
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::sentQueryComposesThePath()
{
    // Relative to maildir, the same way the account's own scope is, so the two
    // cannot disagree about where the account lives.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.webmail-primary]\n"
        "maildir = webmail-primary\n"
        "sent = Sent\n")));

    QCOMPARE(config.accounts().at(0).sentQuery(),
             QStringLiteral("path:\"webmail-primary/Sent/**\""));
}

void TestConfig::sentQuerySurvivesABracketedPath()
{
    // The load-bearing case, and the reason this is a config key rather than a
    // <maildir>/Sent convention. A real provider nests its sent folder under a
    // BRACKETED parent and localises the name: "[Provider]/Posta inviata".
    //
    // "[" and "]" are Xapian syntax. The quotes around the whole path are what
    // make the query work at all, and an implementation that built this without
    // them returns nothing while looking entirely plausible.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.provider-a]\n"
        "maildir = provider-a\n"
        "sent = [Provider]/Posta inviata\n")));

    const QString query = config.accounts().at(0).sentQuery();
    QCOMPARE(query,
             QStringLiteral("path:\"provider-a/[Provider]/Posta inviata/**\""));

    // Stated separately from the QCOMPARE above: the quoting is the property
    // that matters, and a later change to the surrounding syntax must not be
    // able to drop it while still matching a rewritten expected string.
    QVERIFY2(query.contains(QStringLiteral("\"provider-a/[Provider]")),
             "the composed path is not quoted, so Xapian will read the "
             "brackets as syntax and the query will match nothing");
}

void TestConfig::sentQueryComposesWithScopedQuery()
{
    // A Sent view under one account must not show another account's sent mail.
    // The account selector wraps whatever query runs, so the composed sent
    // query has to survive being scoped rather than bypassing it.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.webmail-primary]\n"
        "maildir = webmail-primary\n"
        "sent = Sent\n")));

    const Account account = config.accounts().at(0);
    const QString scoped = account.scopedQuery(account.sentQuery());

    QCOMPARE(scoped,
             QStringLiteral("path:\"webmail-primary/**\" and "
                            "(path:\"webmail-primary/Sent/**\")"));
}

void TestConfig::allSentQueryIsEmptyWhenNoAccountHasOne()
{
    // Empty rather than a query matching nothing, so the caller can hide the
    // Sent button entirely instead of offering one that finds no mail.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.provider-c]\n"
        "maildir = provider-c\n")));

    QVERIFY(config.allSentQuery().isEmpty());
}

void TestConfig::allSentQuerySkipsAccountsWithoutTheKey()
{
    // Joining an account with no `sent` key would leave a bare "or" in the
    // query, and notmuch does not reject that: it silently returns a DIFFERENT
    // result. Measured directly against a real database, `A or  or B` returns
    // 190 where the correct pair returns 211.
    //
    // A malformed query that still returns plausible mail is the failure that
    // ships, so this asserts the shape of the string rather than a count.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.webmail-primary]\n"
        "maildir = webmail-primary\n"
        "sent = Sent\n"
        "\n"
        "[account.provider-c]\n"
        "maildir = provider-c\n"
        "\n"
        "[account.webmail-secondary]\n"
        "maildir = webmail-secondary\n"
        "sent = Sent\n")));

    const QString all = config.allSentQuery();

    QVERIFY2(!all.contains(QStringLiteral("or  or")),
             "an account without a sent key left a bare 'or' in the query");
    QVERIFY2(!all.trimmed().endsWith(QStringLiteral("or")),
             "the query ends in a dangling 'or'");
    QVERIFY2(!all.trimmed().startsWith(QStringLiteral("or")),
             "the query starts with a dangling 'or'");
    QVERIFY(!all.contains(QStringLiteral("provider-c")));

    // Exactly two terms joined, one per account that configures the key.
    QCOMPARE(all.count(QStringLiteral("path:")), 2);
    QCOMPARE(all.count(QStringLiteral(" or ")), 1);
}

void TestConfig::allSentQueryJoinsEveryConfiguredAccount()
{
    // Including a bracketed provider path, which is the case the quoting
    // exists for and the one most likely to be broken by a later rewrite of
    // this composition.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.webmail-primary]\n"
        "maildir = webmail-primary\n"
        "sent = Sent\n"
        "\n"
        "[account.provider-a]\n"
        "maildir = provider-a\n"
        "sent = [Provider]/Posta inviata\n")));

    const QString all = config.allSentQuery();

    QVERIFY(all.contains(QStringLiteral("path:\"webmail-primary/Sent/**\"")));
    QVERIFY(all.contains(
        QStringLiteral("path:\"provider-a/[Provider]/Posta inviata/**\"")));
    QCOMPARE(all.count(QStringLiteral(" or ")), 1);
}

/// Two accounts, one with a sent folder and one without. The second is the
/// case that matters most: folderQuery() returns empty for an unset folder and
/// an empty query means "match everything" to notmuch, so a filter that falls
/// back to it silently shows the whole Maildir.
static QString writeTwoAccounts(const QTemporaryDir &dir)
{
    return writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "maildir=work\n"
        "sent=Sent\n"
        "\n"
        "[account.personal]\n"
        "maildir=personal\n"));
}

void TestConfig::theStartupAccountIsReadAndValidated()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_account=work\n"
        "\n"
        "[account.work]\n"
        "maildir=work\n"
        "\n"
        "[account.personal]\n"
        "maildir=personal\n")));

    QCOMPARE(config.startupAccount(), QStringLiteral("work"));
    QVERIFY(config.problems().isEmpty());

    // Unset is "All accounts", which is what an empty key means everywhere the
    // account is carried, so no separate sentinel.
    QTemporaryDir plain;
    Config unset;
    unset.load(writeIni(plain, QStringLiteral(
        "[account.work]\n"
        "maildir=work\n")));
    QVERIFY(unset.startupAccount().isEmpty());

    // A name that matches no account is a problem: the user asked for
    // something and is not getting it, exactly as for startup_query. Reported
    // and IGNORED rather than passed on, since setting the dropdown to a key
    // that is not in it would silently leave it on All accounts anyway.
    QTemporaryDir bad;
    Config wrong;
    wrong.load(writeIni(bad, QStringLiteral(
        "[general]\n"
        "startup_account=nosuchaccount\n"
        "\n"
        "[account.work]\n"
        "maildir=work\n")));
    QVERIFY2(wrong.startupAccount().isEmpty(),
             "an unknown startup account was passed through rather than "
             "falling back to All accounts");
    QCOMPARE(wrong.problems().size(), 1);
}

void TestConfig::theStartupAccountTakesTheKeyNotTheSyncChannel()
{
    // Two names exist for one account and they genuinely differ in a real
    // setup: a section key may carry dots that the mbsync channel does not.
    // startup_account is the KEY, matched against the [account.<key>] suffix.
    //
    // The dot is the part worth pinning. QSettings treats "/" as a group
    // separator, which is why account sections use a dot in the first place; a
    // dot INSIDE the key is a different case, and a silent mismatch here would
    // report "not a configured account" and quietly start on All accounts.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_account=provider-work.mailbox\n"
        "\n"
        "[account.provider-work.mailbox]\n"
        "maildir=provider-work.mailbox\n"
        "channel=provider-workmailbox\n")));

    QCOMPARE(config.accounts().size(), 1);
    QCOMPARE(config.accounts().constFirst().key,
             QStringLiteral("provider-work.mailbox"));
    QCOMPARE(config.accounts().constFirst().syncChannel(),
             QStringLiteral("provider-workmailbox"));

    QCOMPARE(config.startupAccount(),
             QStringLiteral("provider-work.mailbox"));
    QVERIFY(config.problems().isEmpty());

    // And the channel is NOT accepted, or the two names would be
    // interchangeable in one direction only, which is worse than either rule.
    QTemporaryDir other;
    Config byChannel;
    byChannel.load(writeIni(other, QStringLiteral(
        "[general]\n"
        "startup_account=provider-workmailbox\n"
        "\n"
        "[account.provider-work.mailbox]\n"
        "maildir=provider-work.mailbox\n"
        "channel=provider-workmailbox\n")));

    QVERIFY2(byChannel.startupAccount().isEmpty(),
             "the sync channel was accepted as an account key");
    QCOMPARE(byChannel.problems().size(), 1);
}

void TestConfig::theStartupQueryCanNameABuiltinFilter()
{
    // The defect: startup_query searched the SAVED queries only. A user whose
    // startup view was "Inbox" had that name in queries.json until item 93
    // shipped Inbox as a built-in filter and the duplicate was removed; the
    // name then matched nothing and the app started on whatever query happened
    // to be first in the file.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_query=Inbox\n"
        "\n"
        "[account.work]\n"
        "maildir=work\n"
        "sent=Sent\n")));

    const SavedQuery startup = config.startupSavedQuery();
    QCOMPARE(startup.name, QStringLiteral("Inbox"));
    QVERIFY2(startup.isGenerated(),
             "the startup query matched something other than the built-in");
    QCOMPARE(config.resolvedQuery(startup, QString()),
             QStringLiteral("tag:inbox"));
}

void TestConfig::theStartupQuerySurvivesATranslatedFilterName()
{
    // Reported by the user running the 0.23.0 Italian translation: the app
    // started on the wrong view and said
    //
    //   La ricerca iniziale 'Inbox' non è una ricerca salvata; verrà aperta
    //   'Non letti'.
    //
    // A filter's NAME is a translated label, so `startup_query = Inbox` matched
    // nothing once the Inbox filter was called "In arrivo": a config file that
    // had always worked broke because the UI language changed, and the warning
    // named the user's own correct config as the fault.
    //
    // The fix matches the GENERATOR too, which is stored in queries.json and
    // identical in every locale. Uses a real QTranslator rather than a stub,
    // because the bug lives in the gap between the stored string and the
    // displayed one, and only an actual translation opens that gap.
    QTranslator translator;
    const QString qm = QStringLiteral(TRANSLATIONS_QM);
    QVERIFY2(QFile::exists(qm),
             qPrintable(QStringLiteral("no compiled translation at %1").arg(qm)));
    QVERIFY2(translator.load(qm), "the Italian translation failed to load");
    QVERIFY(qApp->installTranslator(&translator));

    // Proves the translator is actually in effect. Without this the test passes
    // when the translation silently fails to load, asserting nothing: the names
    // stay English and every comparison below succeeds for the wrong reason.
    const SavedQuery inbox = Config::builtinFilter(QStringLiteral("inbox"));
    QCOMPARE(inbox.name, QStringLiteral("In arrivo"));

    QTemporaryDir dir;
    // A saved query has to exist for the warning to be reachable at all: the
    // check is guarded by !m_savedQueries.isEmpty(). Without this file the
    // branch never runs, and an assertion that no problem was reported passes
    // against a broken check by never reaching it. Measured: with no
    // queries.json, reverting the fix left this test green.
    {
        QFile queries(dir.filePath(QStringLiteral("queries.json")));
        QVERIFY(queries.open(QIODevice::WriteOnly));
        queries.write(QStringLiteral(R"({
            "version": 1,
            "queries": [ { "name": "Mine", "query": "tag:mine" } ]
        })").toUtf8());
    }

    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_query=Inbox\n"
        "\n"
        "[account.work]\n"
        "maildir=work\n"
        "sent=Sent\n")));
    QVERIFY2(!config.savedQueries().isEmpty(),
             "queries.json did not load, so the warning path is unreachable");

    const SavedQuery startup = config.startupSavedQuery();
    QCOMPARE(startup.generated, QStringLiteral("inbox"));
    QCOMPARE(config.resolvedQuery(startup, QString()),
             QStringLiteral("tag:inbox"));

    // And it must not warn about a config that is working. The user saw the
    // warning as well as the wrong view, and a warning they cannot act on is
    // its own defect.
    QVERIFY2(config.problems().isEmpty(),
             qPrintable(QStringLiteral("unexpected problem: %1")
                            .arg(config.problems().join(QLatin1Char(' ')))));

    // The translated name still works, since that is what a user reading their
    // own Italian UI would naturally write.
    Config byLabel;
    byLabel.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_query=In arrivo\n"
        "\n"
        "[account.work]\n"
        "maildir=work\n"
        "sent=Sent\n")));
    QVERIFY(!byLabel.savedQueries().isEmpty());
    QCOMPARE(byLabel.startupSavedQuery().generated, QStringLiteral("inbox"));
    QVERIFY(byLabel.problems().isEmpty());

    qApp->removeTranslator(&translator);
}

void TestConfig::theLanguageKeyOverridesTheEnvironment()
{
    // A directory PER CASE. writeIni() always writes qtmaildir.conf and
    // QSettings caches by path, so five loads from one QTemporaryDir all see
    // whichever file was written first: this test failed reporting "it_IT"
    // where it had just written en_US.
    QTemporaryDir dir, systemDir, shortDir, fullDir, englishDir;

    // Unset means follow the environment, which is what an empty value tells
    // main.cpp to do by default-constructing a QLocale.
    Config unset;
    unset.load(writeIni(dir, QStringLiteral("[general]\n")));
    QVERIFY(unset.language().isEmpty());
    QVERIFY(unset.problems().isEmpty());

    // "system" is the default written down. It must read as unset rather than
    // being passed to QLocale, which would resolve it to C and force English.
    Config system;
    system.load(writeIni(systemDir, QStringLiteral(
        "[general]\n"
        "language = system\n")));
    QVERIFY2(system.language().isEmpty(),
             "'system' must read as unset, not as a locale name");
    QVERIFY(system.problems().isEmpty());

    // A short code is accepted as written. Qt resolves "it" to it_IT when the
    // QLocale is built, and QTranslator::load falls back from qtmaildir_it_IT
    // to qtmaildir_it, so the short form needs no expansion here.
    Config shortCode;
    shortCode.load(writeIni(shortDir, QStringLiteral(
        "[general]\n"
        "language = it\n")));
    QCOMPARE(shortCode.language(), QStringLiteral("it"));
    QVERIFY(shortCode.problems().isEmpty());
    QCOMPARE(QLocale(shortCode.language()).name(), QStringLiteral("it_IT"));

    Config full;
    full.load(writeIni(fullDir, QStringLiteral(
        "[general]\n"
        "language = it_IT\n")));
    QCOMPARE(full.language(), QStringLiteral("it_IT"));
    QVERIFY(full.problems().isEmpty());

    // Forcing English is legitimate and must NOT be reported as a problem, even
    // though it loads no .qm: English is the source language and ships none.
    // This is the case that separates "no translation" from "bad value".
    Config english;
    english.load(writeIni(englishDir, QStringLiteral(
        "[general]\n"
        "language = en_US\n")));
    QCOMPARE(english.language(), QStringLiteral("en_US"));
    QVERIFY2(english.problems().isEmpty(),
             "forcing English is a valid choice, not a configuration error");
}

void TestConfig::theLanguageKeyRejectsWhatIsNotALocale()
{
    // The trap this guards. QLocale accepts any string and degrades an
    // unrecognised one to C rather than failing, so `language = itallian`
    // would load no translation and be indistinguishable from asking for
    // English on purpose: the user's typo would be silent forever. Verified
    // against QLocale directly first, so the test rests on measured behaviour
    // rather than on the assumption that a bad name is rejected somewhere.
    QCOMPARE(QLocale(QStringLiteral("itallian")).language(), QLocale::C);

    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[general]\n"
        "language = itallian\n")));

    QCOMPARE(config.problems().size(), 1);
    QVERIFY2(config.problems().first().contains(QStringLiteral("itallian")),
             "the warning must name the value the user wrote");
    // Cleared, so the caller falls back to the environment rather than being
    // handed a name that resolves to C and forces English.
    QVERIFY(config.language().isEmpty());
}

void TestConfig::theFlaggedFilterIsCalledImportant()
{
    // Item 57 decided this and item 93 contradicted it. The `flag` ACTION has
    // read "&Important" since 0.14.0, chosen over "Starred" partly because &I
    // was free where &S collided with Mark spam, and the filter shipped as
    // "Flagged" beside it: the same tag under two names in one window.
    //
    // The generator keeps its own name, `flagged`. That string is stored in
    // queries.json and matched against a closed set, so it is wire format and
    // must not follow the label.
    const SavedQuery filter =
        Config::builtinFilter(QStringLiteral("flagged"));

    QCOMPARE(filter.name, QStringLiteral("Important"));
    QCOMPARE(filter.generated, QStringLiteral("flagged"));
}

void TestConfig::everyBuiltinFilterIsAKnownGenerator()
{
    // The guard for every case below. A filter whose generator is not in the
    // closed set loads with a reported problem and resolves to an empty query,
    // which means "match everything": the assertions that follow would then be
    // measuring a typo rather than the design.
    Config config;
    const QList<SavedQuery> filters = config.builtinFilters();

    QCOMPARE(filters.size(), 4);

    QStringList names;
    for (const SavedQuery &filter : filters) {
        QVERIFY2(filter.isGenerated(),
                 qPrintable(QStringLiteral("filter '%1' stores a query instead "
                                           "of naming a generator")
                                .arg(filter.name)));
        QVERIFY2(Config::isKnownGenerator(filter.generated),
                 qPrintable(QStringLiteral("filter '%1' names the unknown "
                                           "generator '%2'")
                                .arg(filter.name, filter.generated)));
        names.append(filter.name);
    }

    // The order is the row's order, left to right, and is fixed rather than
    // configurable: item 94 removes the mixed row entirely, so a settings
    // surface for this would be built and deleted inside two items.
    QCOMPARE(names, (QStringList{ QStringLiteral("Unread"),
                                  QStringLiteral("Inbox"),
                                  QStringLiteral("Important"),
                                  QStringLiteral("Sent") }));
}

void TestConfig::aFilterAcrossAllAccountsIsTheUnscopedQuery()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeTwoAccounts(dir));

    // An empty account key is "All accounts", which is what the dropdown holds
    // by default.
    const SavedQuery unread = config.builtinFilter(QStringLiteral("unread"));
    QCOMPARE(config.resolvedQuery(unread, QString()),
             QStringLiteral("tag:unread"));
}

void TestConfig::aTagFilterScopedToAnAccountCarriesThatAccountsPath()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeTwoAccounts(dir));

    // A tag filter has no path of its own, so scoping it is exactly what
    // Account::scopedQuery() does and nothing more is needed.
    const SavedQuery unread = config.builtinFilter(QStringLiteral("unread"));
    QCOMPARE(config.resolvedQuery(unread, QStringLiteral("work")),
             QStringLiteral("path:\"work/**\" and (tag:unread)"));
}

void TestConfig::sentScopedToAnAccountIsThatAccountsSentFolderAlone()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeTwoAccounts(dir));

    const SavedQuery sent = config.builtinFilter(QStringLiteral("sent"));
    const QString scoped = config.resolvedQuery(sent, QStringLiteral("work"));

    // The whole point of a per-account generator. Wrapping the all-accounts
    // query instead would give
    //     path:"work/**" and (path:"work/Sent/**" or path:"personal/Sent/**")
    // which returns the RIGHT ROWS, because path: is hierarchical and the
    // personal half cannot match inside work. It is still wrong to build: it
    // double-scopes and works by accident of the path syntax rather than by
    // saying what is meant. A row-count assertion passes against it, which is
    // why this asserts on the string.
    QCOMPARE(scoped, QStringLiteral("path:\"work/Sent/**\""));
    QVERIFY2(!scoped.contains(QStringLiteral("personal")),
             "another account's sent folder leaked into a scoped Sent filter");
    QCOMPARE(scoped.count(QStringLiteral("path:")), 1);
}

void TestConfig::sentScopedToAnAccountWithNoSentFolderMatchesNothing()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeTwoAccounts(dir));

    // `personal` configures no sent folder, so folderQuery() gives an empty
    // string. Returned as-is that is "match everything" to notmuch, so Sent
    // under this account would show the entire Maildir: the worst possible
    // answer for a button labelled Sent.
    const SavedQuery sent = config.builtinFilter(QStringLiteral("sent"));
    const QString scoped =
        config.resolvedQuery(sent, QStringLiteral("personal"));

    QVERIFY2(!scoped.isEmpty(),
             "an account with no sent folder resolved to an empty query, "
             "which notmuch reads as 'match everything'");
    QCOMPARE(scoped, Config::matchNothingQuery());
}

void TestConfig::aFilterKeepsItsViewMode()
{
    Config config;

    // Sent lists MESSAGES, the other three list threads. Not a detail to
    // unify: a thread would fold the user's sent message back into the
    // conversation it belongs to, which is item 63's finding.
    QVERIFY(config.builtinFilter(QStringLiteral("sent")).flat);
    QVERIFY(!config.builtinFilter(QStringLiteral("unread")).flat);
    QVERIFY(!config.builtinFilter(QStringLiteral("inbox")).flat);
    QVERIFY(!config.builtinFilter(QStringLiteral("flagged")).flat);
}

void TestConfig::draftsQueryIsEmptyWithoutTheKey()
{
    // Optional for the same reason `sent` is, and more often absent: an
    // account that composes elsewhere keeps no local drafts folder at all.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.provider-c]\n"
        "maildir = provider-c\n")));

    QCOMPARE(config.accounts().size(), 1);
    QVERIFY(config.accounts().at(0).draftsQuery().isEmpty());
    QVERIFY(config.problems().isEmpty());
}

void TestConfig::draftsQuerySurvivesABracketedPath()
{
    // The same quoting trap sentQuery() exists for, and it bites harder here:
    // a real provider's drafts folder is BOTH bracketed and localised
    // ("[Provider]/Bozze"). Unquoted, "[" and "]" are Xapian syntax and the
    // term is parsed rather than matched, so the count reads 0 and looks like
    // an empty drafts folder rather than a broken query.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.provider-a]\n"
        "maildir = provider-a\n"
        "drafts = [Provider]/Bozze\n")));

    QCOMPARE(config.accounts().at(0).draftsQuery(),
             QStringLiteral("path:\"provider-a/[Provider]/Bozze/**\""));
}

void TestConfig::allDraftsQuerySkipsAccountsWithoutTheKey()
{
    // The bare-"or" defect allSentQuerySkipsAccountsWithoutTheKey() records,
    // asserted again rather than assumed to be inherited: the two compositions
    // are separate functions and a rewrite of one does not carry the other.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.webmail-primary]\n"
        "maildir = webmail-primary\n"
        "drafts = Drafts\n"
        "\n"
        "[account.provider-c]\n"
        "maildir = provider-c\n"
        "\n"
        "[account.webmail-secondary]\n"
        "maildir = webmail-secondary\n"
        "drafts = Drafts\n")));

    const QString all = config.allDraftsQuery();

    QVERIFY2(!all.contains(QStringLiteral("or  or")),
             "an account without a drafts key left a bare 'or' in the query");
    QVERIFY2(!all.trimmed().endsWith(QStringLiteral("or")),
             "the query ends in a dangling 'or'");
    QVERIFY2(!all.trimmed().startsWith(QStringLiteral("or")),
             "the query starts with a dangling 'or'");
    QVERIFY(!all.contains(QStringLiteral("provider-c")));

    QCOMPARE(all.count(QStringLiteral("path:")), 2);
    QCOMPARE(all.count(QStringLiteral(" or ")), 1);
}

void TestConfig::allDraftsQueryIsIndependentOfSent()
{
    // The two keys are independent, and one real account proves it: it
    // configures `drafts` and has no `sent` whatsoever. A composition that
    // walked the accounts once and emitted both from the same loop iteration
    // would either drop this account's drafts or invent a sent term for it.
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.webmail-primary]\n"
        "maildir = webmail-primary\n"
        "sent = Sent\n"
        "\n"
        "[account.provider-b]\n"
        "maildir = provider-b\n"
        "drafts = [Provider]/Bozze\n")));

    const QString drafts = config.allDraftsQuery();
    const QString sent = config.allSentQuery();

    // One term each, from DIFFERENT accounts.
    QCOMPARE(drafts, QStringLiteral("path:\"provider-b/[Provider]/Bozze/**\""));
    QCOMPARE(sent, QStringLiteral("path:\"webmail-primary/Sent/**\""));
    QVERIFY(!drafts.contains(QStringLiteral("webmail-primary")));
    QVERIFY(!sent.contains(QStringLiteral("provider-b")));
}

// ---------------------------------------------------------------------------
// queries.json (item 23)
// ---------------------------------------------------------------------------

static QString writeQueries(const QTemporaryDir &dir, const QString &body)
{
    const QString path = dir.filePath(QStringLiteral("queries.json"));
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(body.toUtf8());
    f.close();
    return path;
}

/// The property the INI could not provide. "Zebra" is written first and must
/// STAY first: alphabetical order would put it last, so this fails against any
/// implementation that sorts, including the one being replaced.
void TestConfig::savedQueriesKeepTheirDocumentOrder()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QString());
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Zebra",  "query": "tag:zebra" },
            { "name": "Apple",  "query": "tag:apple" },
            { "name": "Middle", "query": "tag:middle" }
        ]
    })"));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 3);
    QCOMPARE(queries.at(0).name, QStringLiteral("Zebra"));
    QCOMPARE(queries.at(1).name, QStringLiteral("Apple"));
    QCOMPARE(queries.at(2).name, QStringLiteral("Middle"));
}

void TestConfig::savedQueryFieldsAreRead()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QString());
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "pinned": true },
            { "name": "Billing", "query": "from:billing", "account": "work" }
        ]
    })"));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 2);

    QCOMPARE(queries.at(0).name, QStringLiteral("Inbox"));
    QCOMPARE(queries.at(0).query, QStringLiteral("tag:inbox"));
    QVERIFY(queries.at(0).pinned);
    QVERIFY(queries.at(0).account.isEmpty());

    // pinned defaults to false, which is what puts a query in the menu rather
    // than on the row.
    QVERIFY(!queries.at(1).pinned);
    QCOMPARE(queries.at(1).account, QStringLiteral("work"));
}

/// A field written by a later build must survive an older build's save, or a
/// downgrade silently strips config the user set.
void TestConfig::unknownFieldsSurviveARoundTrip()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QString());
    const QString queriesPath = writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "colour_scheme": "solarized",
        "queries": [
            { "name": "Inbox", "query": "tag:inbox", "icon": "mail-inbox" }
        ]
    })"));

    Config config;
    config.load(path);
    QVERIFY(config.saveSavedQueries());

    QFile f(queriesPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    QCOMPARE(root.value(QStringLiteral("colour_scheme")).toString(),
             QStringLiteral("solarized"));
    const QJsonObject entry =
        root.value(QStringLiteral("queries")).toArray().at(0).toObject();
    QCOMPARE(entry.value(QStringLiteral("icon")).toString(),
             QStringLiteral("mail-inbox"));
}

void TestConfig::savedQueriesRoundTripUnchanged()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QString());
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Zebra", "query": "tag:zebra", "pinned": true },
            { "name": "Apple", "query": "from:a@example.org", "account": "work" }
        ]
    })"));

    Config first;
    first.load(path);
    QVERIFY(first.saveSavedQueries());

    Config second;
    second.load(path);

    const QList<SavedQuery> a = first.savedQueries();
    const QList<SavedQuery> b = second.savedQueries();
    QCOMPARE(b.size(), a.size());
    for (int i = 0; i < a.size(); ++i) {
        QCOMPARE(b.at(i).name, a.at(i).name);
        QCOMPARE(b.at(i).query, a.at(i).query);
        QCOMPARE(b.at(i).pinned, a.at(i).pinned);
        QCOMPARE(b.at(i).account, a.at(i).account);
    }
}

/// Asserts the INI is byte-identical, NOT that it still parses. Re-reading it
/// through QSettings would pass against a rewrite that kept every value while
/// dropping the comments and key order, which is the loss this design exists
/// to avoid.
void TestConfig::migrationWritesJsonAndLeavesTheIniByteIdentical()
{
    QTemporaryDir dir;
    const QString ini = QStringLiteral(
        "; a comment the user wrote and expects to keep\n"
        "[queries]\n"
        "Unread=tag:unread\n"
        "Inbox=tag:inbox\n"
        "\n"
        "[general]\n"
        "startup_query=Unread\n"
    );
    const QString path = writeIni(dir, ini);

    QFile before(path);
    QVERIFY(before.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = before.readAll();
    before.close();

    Config config;
    config.load(path);

    const QString queriesPath = dir.filePath(QStringLiteral("queries.json"));
    QVERIFY2(QFile::exists(queriesPath), "migration did not write queries.json");

    QFile after(path);
    QVERIFY(after.open(QIODevice::ReadOnly));
    const QByteArray afterBytes = after.readAll();
    after.close();

    QCOMPARE(afterBytes, originalBytes);

    // The [queries] section is left in place, so an older build still works.
    QVERIFY(afterBytes.contains("[queries]"));
    QVERIFY(afterBytes.contains("; a comment the user wrote"));
}

/// A migrated query that was not pinned would vanish from the query row, which
/// on the first launch after an upgrade looks like data loss.
void TestConfig::migrationPinsEveryEntry()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[queries]\n"
        "Inbox=tag:inbox\n"
        "Unread=tag:unread\n"
    ));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 2);
    for (const SavedQuery &query : queries)
        QVERIFY2(query.pinned, qPrintable(
            QStringLiteral("migrated query '%1' is not pinned").arg(query.name)));
}

/// Once the JSON exists, [queries] is dead. Two sources of truth was the
/// option this design rejected.
void TestConfig::jsonWinsOnceItExists()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[queries]\n"
        "FromTheIni=tag:ini\n"
    ));
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [ { "name": "FromTheJson", "query": "tag:json" } ]
    })"));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 1);
    QCOMPARE(queries.at(0).name, QStringLiteral("FromTheJson"));
}

void TestConfig::aStoredGeneratedQueryIsUnpinnedNotDropped()
{
    // An existing install carries a Sent entry in queries.json: 0.19.0 migrated
    // the hardcoded button into one. Item 93 ships Sent as a built-in filter,
    // so that stored entry is now a DUPLICATE and would put two Sent buttons on
    // the row, one editable and one not.
    //
    // Unpinned rather than deleted. This file's whole design is that a reader
    // preserves what it does not own, and the user's instruction for their own
    // redundant queries was the same: fold them into the menu, do not drop
    // them. An unpin is reversible from the UI; a delete is not.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "maildir=work\n"
        "sent=Sent\n"));
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Sent", "generated": "sent", "pinned": true },
            { "name": "Mine", "query": "tag:todo", "pinned": true }
        ]
    })"));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 2);

    bool sawSent = false;
    for (const SavedQuery &query : queries) {
        if (query.generated != QStringLiteral("sent"))
            continue;
        sawSent = true;
        QVERIFY2(!query.pinned,
                 "the stored Sent entry is still a button beside the built-in "
                 "filter of the same name");
    }
    QVERIFY2(sawSent, "the stored Sent entry was DROPPED rather than unpinned");

    // The user's own query is untouched: only the entry duplicating a built-in
    // filter is unpinned.
    for (const SavedQuery &query : queries) {
        if (query.name == QStringLiteral("Mine"))
            QVERIFY2(query.pinned, "an unrelated pinned query was unpinned");
    }
}

void TestConfig::theStartupQueryPrefersASavedQueryOverAFilterOfTheSameName()
{
    // The user's own entry wins a name collision. They named it deliberately;
    // the filter's name is one this application chose for them.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_query=Inbox\n"
        "\n"
        "[account.work]\n"
        "maildir=work\n"));
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [ { "name": "Inbox", "query": "tag:inbox and not tag:muted" } ]
    })"));

    Config config;
    config.load(path);

    const SavedQuery startup = config.startupSavedQuery();
    QCOMPARE(startup.name, QStringLiteral("Inbox"));
    QVERIFY2(!startup.isGenerated(),
             "the built-in filter shadowed the user's own query of that name");
    QCOMPARE(startup.query, QStringLiteral("tag:inbox and not tag:muted"));
}

void TestConfig::anUnmatchedStartupQueryFallsBackToAFilterNotAStrayQuery()
{
    // The old fallback was m_savedQueries.first(), which after item 93 removed
    // the duplicated entries could be any leftover query: the user's startup
    // view became a search for one sender, and an empty queries.json started
    // nothing at all. A built-in filter is always present, so the fallback can
    // be one.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_query=NoSuchThing\n"
        "\n"
        "[account.work]\n"
        "maildir=work\n"));
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [ { "name": "from bu", "query": "from:someone" } ]
    })"));

    Config config;
    config.load(path);

    const SavedQuery startup = config.startupSavedQuery();
    QVERIFY2(startup.isGenerated(),
             "an unmatched startup query fell back to a stray saved query");
    QCOMPARE(startup.name, QStringLiteral("Unread"));
}

void TestConfig::malformedQueriesFileIsAProblemNotACrash()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QString());
    writeQueries(dir, QStringLiteral("{ this is not json at all"));

    Config config;
    config.load(path);

    QVERIFY(config.savedQueries().isEmpty());
    QVERIFY2(!config.problems().isEmpty(),
             "a malformed queries.json must be reported");
}

/// Refusing an unknown version is the same contract rules.json keeps: a file
/// from a newer build is not silently reinterpreted, and above all is not
/// overwritten with a lossy reading of itself.
void TestConfig::futureVersionIsRefusedAndReported()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QString());
    writeQueries(dir, QStringLiteral(R"({
        "version": 99,
        "queries": [ { "name": "Inbox", "query": "tag:inbox" } ]
    })"));

    Config config;
    config.load(path);

    QVERIFY(config.savedQueries().isEmpty());
    QVERIFY(!config.problems().isEmpty());
}

/// The fallback stops meaning "alphabetically first" and starts meaning "first
/// in the user's own order". "Zebra" first proves it: alphabetical would pick
/// "Apple".
/// Was startupQueryFallsBackToDocumentOrder, asserting the first query in the
/// file. That WAS the defect: the first entry is an arbitrary thing to open on,
/// and once item 93's duplicated entries were removed it became a leftover
/// search for one sender.
void TestConfig::startupQueryFallsBackToABuiltinFilter()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\n"
        "startup_query=NoSuchQuery\n"
    ));
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Zebra", "query": "tag:zebra" },
            { "name": "Apple", "query": "tag:apple" }
        ]
    })"));

    Config config;
    config.load(path);

    const SavedQuery startup = config.startupSavedQuery();
    QVERIFY2(startup.isGenerated(),
             "the fallback picked a saved query out of the file");
    QCOMPARE(startup.name, QStringLiteral("Unread"));
}

/// The parentheses are load-bearing. Without them `path:... and a or b` binds
/// as `(path:... and a) or b`, so a query saved with a disjunction escapes its
/// account scope and matches every account.
void TestConfig::scopedSavedQueryParenthesisesADisjunction()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
    ));
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Either",
              "query": "from:a@example.org or from:b@example.org",
              "account": "work" }
        ]
    })"));

    Config config;
    config.load(path);

    const QString scoped = config.resolvedQuery(config.savedQueries().at(0));
    QCOMPARE(scoped, QStringLiteral(
        "path:\"work-mail/**\" and "
        "(from:a@example.org or from:b@example.org)"));

    // An account key naming nothing resolves to the bare query rather than a
    // scope built from an empty maildir, which would be path:"/**".
    SavedQuery orphan;
    orphan.name = QStringLiteral("Orphan");
    orphan.query = QStringLiteral("tag:inbox");
    orphan.account = QStringLiteral("deleted-account");
    QCOMPARE(config.resolvedQuery(orphan), QStringLiteral("tag:inbox"));
}

// ---------------------------------------------------------------------------
// Generated saved queries
// ---------------------------------------------------------------------------

static QString twoAccountsWithSent()
{
    return QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
        "sent=Sent\n"
        "\n"
        "[account.personal]\n"
        "name=Test User\n"
        "address=me@example.net\n"
        "maildir=personal\n"
        "sent=[Provider]/Posta inviata\n"
    );
}

void TestConfig::aGeneratedQueryResolvesFromTheAccounts()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, twoAccountsWithSent());
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Sent", "generated": "sent", "pinned": true }
        ]
    })"));

    Config config;
    config.load(path);

    const SavedQuery sent = config.savedQueries().at(0);
    QVERIFY(sent.isGenerated());
    // The stored query is empty; the text comes from the accounts.
    QVERIFY(sent.query.isEmpty());
    QCOMPARE(config.resolvedQuery(sent), config.allSentQuery());
    QVERIFY(config.resolvedQuery(sent).contains(
        QStringLiteral("path:\"work-mail/Sent/**\"")));
    // The quotes matter: "[" and "]" are Xapian syntax and an unquoted term
    // is parsed rather than matched.
    QVERIFY(config.resolvedQuery(sent).contains(
        QStringLiteral("path:\"personal/[Provider]/Posta inviata/**\"")));

    // Flat, not threaded: a sent view lists messages, and that property has to
    // travel with the entry or it is lost the moment Sent is a stored row.
    QVERIFY(sent.flat);
}

/// The whole reason Sent is generated rather than stored. A stored copy would
/// keep naming an account that has been renamed or a folder that has moved.
void TestConfig::aGeneratedQueryTracksAConfigChange()
{
    QTemporaryDir dir;
    const QString queries = QStringLiteral(R"({
        "version": 1,
        "queries": [ { "name": "Sent", "generated": "sent" } ]
    })");

    const QString before = writeIni(dir, twoAccountsWithSent());
    writeQueries(dir, queries);
    Config first;
    first.load(before);
    const QString firstResolved = first.resolvedQuery(first.savedQueries().at(0));

    // The user corrects a folder name. Nothing in queries.json changes.
    QTemporaryDir second;
    const QString after = writeIni(second, QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
        "sent=Sent Items\n"
    ));
    writeQueries(second, queries);
    Config later;
    later.load(after);
    const QString laterResolved = later.resolvedQuery(later.savedQueries().at(0));

    QVERIFY(firstResolved != laterResolved);
    QVERIFY(laterResolved.contains(QStringLiteral("Sent Items")));
}

void TestConfig::anUnknownGeneratorResolvesToNothingAndReports()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, twoAccountsWithSent());
    writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [ { "name": "Future", "generated": "not_a_generator" } ]
    })"));

    Config config;
    config.load(path);

    // Kept rather than dropped: a later build may know this generator, and
    // silently deleting the row on save would lose it.
    QCOMPARE(config.savedQueries().size(), 1);
    QVERIFY(config.resolvedQuery(config.savedQueries().at(0)).isEmpty());
    QVERIFY2(!config.problems().isEmpty(),
             "an unknown generator must be reported, not silently inert");
}

void TestConfig::migrationAddsSentWhenAnAccountHasOne()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, twoAccountsWithSent()
                                       + QStringLiteral(
        "\n[queries]\n"
        "Inbox=tag:inbox\n"
    ));

    Config config;
    config.load(path);

    // The migration used to invent a generated Sent entry here, so the
    // hardcoded button could be reordered, renamed or removed like any other
    // row. Item 93 ships Sent as one of four BUILT-IN filters instead, so
    // migrating one as well would put two Sent buttons on the row: one the
    // user's to edit and one not.
    //
    // Nothing is lost. The built-in resolves through the same generator, so it
    // still follows the accounts, and it now composes with the account dropdown
    // rather than resetting it.
    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 1);
    QCOMPARE(queries.at(0).name, QStringLiteral("Inbox"));

    for (const SavedQuery &query : queries) {
        QVERIFY2(!query.isGenerated(),
                 "the migration invented a generated entry that now duplicates "
                 "a built-in filter");
    }
}

/// Today the button is hidden entirely when no account configures a sent
/// folder, rather than offering one that always finds nothing. The migration
/// must not invent a row that would do exactly that.
void TestConfig::migrationAddsNoSentWithoutTheKey()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
        "\n"
        "[queries]\n"
        "Inbox=tag:inbox\n"
    ));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 1);
    QCOMPARE(queries.at(0).name, QStringLiteral("Inbox"));
}

/// The file is meant to be hand-edited, so a key that carries no information
/// is a key the reader has to skip past. `query` says nothing on a generated
/// entry, and `flat` is implied by the sent generator.
void TestConfig::aGeneratedEntryWritesNoRedundantKeys()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, twoAccountsWithSent());
    const QString queriesPath = writeQueries(dir, QStringLiteral(R"({
        "version": 1,
        "queries": [
            { "name": "Sent", "generated": "sent", "pinned": true },
            { "name": "Inbox", "query": "tag:inbox", "pinned": true }
        ]
    })"));

    Config config;
    config.load(path);
    QVERIFY(config.saveSavedQueries());

    QFile f(queriesPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonArray array = QJsonDocument::fromJson(f.readAll())
                                 .object()
                                 .value(QStringLiteral("queries"))
                                 .toArray();
    f.close();

    const QJsonObject sent = array.at(0).toObject();
    QCOMPARE(sent.value(QStringLiteral("generated")).toString(),
             QStringLiteral("sent"));
    QVERIFY2(!sent.contains(QStringLiteral("query")),
             "a generated entry has no query of its own to store");
    QVERIFY2(!sent.contains(QStringLiteral("flat")),
             "the sent generator implies flat; storing it says nothing");

    // The ordinary entry is untouched by any of that.
    const QJsonObject inbox = array.at(1).toObject();
    QCOMPARE(inbox.value(QStringLiteral("query")).toString(),
             QStringLiteral("tag:inbox"));

    // And it all still reads back the same.
    Config reloaded;
    reloaded.load(path);
    QCOMPARE(reloaded.savedQueries().size(), 2);
    QVERIFY(reloaded.savedQueries().at(0).isGenerated());
    QVERIFY2(reloaded.savedQueries().at(0).flat,
             "flat must come back from the generator, not from the file");
}

QTEST_MAIN(TestConfig)
#include "test_config.moc"
