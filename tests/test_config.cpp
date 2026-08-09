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
#include <QSettings>
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
    void accountWithoutMaildirIsRejected();
    void scopedQueryWrapsCorrectly();
    void absentSyncCommandIsNoticeNotProblem();
    void brokenSyncCommandIsAProblem();
    void malformedAccountIsAProblem();
    void validConfigHasNoProblems();
    void startupQueryDefaultsToUnread();
    void startupQueryHonoursTheConfiguredName();
    void unknownStartupQueryFallsBackAndReports();
    void generalSectionKeysAreActuallyRead();
    void messageZoomDefaultsAndValidates();
    void completionOnFocusDefaultsToFalse();
    void completionOnFocusIsActuallyRead();
    void markReadDelayDefaultsToTwoSeconds();
    void markReadDelayIsActuallyRead();
    void markReadDelayAcceptsZeroAndNegative();
    void markReadDelayRejectsGarbage();
    void syncOnExitDefaultsToAsk();
    void syncOnExitReadsAllThreeValues();
    void syncOnExitWarnsOnGarbage();
    void extraMimetypesAppendToBuiltins();
    void extraMimetypeDescriptionMayContainComma();
    void malformedExtraMimetypeIsSkipped();
    void syncChannelDefaultsToTheAccountKey();
    void syncChannelIsActuallyRead();
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
    // QSettings::childKeys() returns keys alphabetically, not in file order,
    // so the UI button order is alphabetical. This assertion happens to hold
    // either way since "Inbox" < "Unread", but the ordering guarantee is
    // alphabetical, not "follows the file".
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

    QCOMPARE(config.startupSavedQuery().name, QStringLiteral("Inbox"));
    QCOMPARE(config.problems().size(), 1);

    // The built-in default naming a query the user never created is NOT a
    // problem: they did not get it wrong, they simply have no Unread entry.
    QTemporaryDir quiet;
    Config silent;
    silent.load(writeIni(quiet, QStringLiteral(
        "[queries]\n"
        "Inbox=tag:inbox\n")));

    QCOMPARE(silent.startupSavedQuery().name, QStringLiteral("Inbox"));
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

QTEST_MAIN(TestConfig)
#include "test_config.moc"
