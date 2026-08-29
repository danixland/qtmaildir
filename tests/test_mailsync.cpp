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

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "mailsync.h"

class TestMailSync : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void unavailableWhenCommandEmpty();
    void unavailableWhenCommandIsOnlyWhitespace();
    void successfulRunEmitsFinished();
    void failedRunReportsExitCode();
    void capturesOutput();
    void capturesStderrToo();
    void emitsStartedSignal();
    void logIsClearedBetweenRuns();
    void refusesConcurrentRuns();
    void canRunAgainAfterFinishing();
    void missingBinaryReportsFailureNotSilence();
    void startDoesNotBlock();
    void argumentsAreNotShellInterpreted();
    void channelsAreAppendedToTheCommand();
    void noChannelsMeansNoExtraArguments();
    void channelNamesAreNotShellInterpreted();

    void phaseStartsAsMbsync();
    void notmuchLineSwitchesPhase();
    void mbsyncSummaryIsReported();
    void noiseLeavesThePhaseAlone();
    void aHostileLineCannotGrowTheStatus();
    void runMarkersAreNotAPhase();
    void theChannelNameIsShown();
    void aChannelNameIsNotLetInVerbatim();

    void lastRunOutcomeReadsAnOkRun();
    void lastRunOutcomeReadsAFailedRun();
    void lastRunOutcomeTakesTheLastMarkerNotTheFirst();
    void lastRunOutcomeOnAMissingLogIsUnknown();
    void lastRunOutcomeOnALogWithNoMarkerIsUnknown();
    void lastRunOutcomeIgnoresATrailingPartialRun();
    void lastRunOutcomeReadsATailOfAHugeLog();
    void lastRunOutcomeReadsABannerTheScriptActuallyWrote();

    void readStatusReadsAnOkRun();
    void readStatusReadsTheChannelsARunCarried();
    void readStatusReadsAFullRunAsEveryAccount();
    void readStatusReadsASkippedRun();
    void readStatusOnAMissingFileIsUnknown();
    void readStatusOnRubbishIsUnknown();
    void readStatusOnATruncatedFileIsUnknown();
    void readStatusOnAnUnknownVersionIsUnknown();
    void readStatusReadsAFileTheScriptActuallyWrote();

private:
    /// Writes an executable shell script into the temp dir, returns its path.
    QString makeScript(const QString &name, const QString &body);

    QTemporaryDir m_dir;
};

void TestMailSync::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

QString TestMailSync::makeScript(const QString &name, const QString &body)
{
    const QString path = m_dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    file.write("#!/bin/sh\n");
    file.write(body.toUtf8());
    file.write("\n");
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

void TestMailSync::unavailableWhenCommandEmpty()
{
    // Braces, not parens: MailSync sync(QString()) is a function declaration.
    MailSync sync{ QString() };
    QVERIFY(!sync.isAvailable());
    QVERIFY(!sync.start());
}

void TestMailSync::unavailableWhenCommandIsOnlyWhitespace()
{
    // A config line like `command =   ` reaches here as spaces, not as empty.
    // splitCommand yields nothing for it, so start() must refuse rather than
    // try to launch an empty program name.
    MailSync sync(QStringLiteral("   "));
    QVERIFY(!sync.start());
    QVERIFY(!sync.isRunning());
}

void TestMailSync::successfulRunEmitsFinished()
{
    MailSync sync(makeScript(QStringLiteral("ok.sh"), QStringLiteral("exit 0")));
    QVERIFY(sync.isAvailable());

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toBool(), true);
    QCOMPARE(spy.first().at(1).toInt(), 0);
}

void TestMailSync::failedRunReportsExitCode()
{
    MailSync sync(makeScript(QStringLiteral("fail.sh"), QStringLiteral("exit 3")));

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QCOMPARE(spy.first().at(0).toBool(), false);
    // The exit code reaches the UI: the status bar shows why sync failed.
    QCOMPARE(spy.first().at(1).toInt(), 3);
}

void TestMailSync::capturesOutput()
{
    MailSync sync(makeScript(QStringLiteral("talk.sh"),
                             QStringLiteral("echo syncing")));

    QSignalSpy spy(&sync, &MailSync::finished);
    QSignalSpy output(&sync, &MailSync::outputReceived);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QVERIFY(sync.log().contains(QStringLiteral("syncing")));
    QVERIFY(!output.isEmpty());
}

void TestMailSync::capturesStderrToo()
{
    // mbsync reports failures on stderr. If only stdout were captured, the log
    // pane would be empty for exactly the runs the user needs to read.
    MailSync sync(makeScript(QStringLiteral("noisy.sh"),
                             QStringLiteral("echo boom >&2\nexit 1")));

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QVERIFY(sync.log().contains(QStringLiteral("boom")));
}

void TestMailSync::emitsStartedSignal()
{
    MailSync sync(makeScript(QStringLiteral("started.sh"), QStringLiteral("exit 0")));

    QSignalSpy started(&sync, &MailSync::started);
    QSignalSpy finished(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(finished.wait(5000));

    QCOMPARE(started.count(), 1);
}

void TestMailSync::logIsClearedBetweenRuns()
{
    MailSync sync(makeScript(QStringLiteral("once.sh"), QStringLiteral("echo first")));

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));
    QVERIFY(sync.log().contains(QStringLiteral("first")));

    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));
    // Stale output from the previous run must not accumulate: the log pane is
    // meant to show what this sync did.
    QCOMPARE(sync.log().count(QStringLiteral("first")), 1);
}

void TestMailSync::refusesConcurrentRuns()
{
    MailSync sync(makeScript(QStringLiteral("slow.sh"), QStringLiteral("sleep 2")));
    QVERIFY(sync.start());
    // The cron sync and this one share a flock; starting twice from the GUI is
    // still refused locally so the button cannot queue runs.
    QVERIFY(!sync.start());
    QVERIFY(sync.isRunning());

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(spy.wait(10000));
}

void TestMailSync::canRunAgainAfterFinishing()
{
    MailSync sync(makeScript(QStringLiteral("again.sh"), QStringLiteral("exit 0")));

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));
    QVERIFY(!sync.isRunning());

    // A refusal must be about "currently running", not a one-shot latch.
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 2);
}

void TestMailSync::missingBinaryReportsFailureNotSilence()
{
    // Config checks the path at load time, but the script can be deleted or
    // unmounted afterwards. Failing silently would leave the spinner up
    // forever with no explanation.
    MailSync sync(m_dir.filePath(QStringLiteral("definitely-not-here.sh")));
    QVERIFY(sync.isAvailable());

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QCOMPARE(spy.first().at(0).toBool(), false);
    QVERIFY(!sync.log().isEmpty());
    QVERIFY(!sync.isRunning());
}

void TestMailSync::startDoesNotBlock()
{
    // Spec: "The UI stays usable during sync." start() must hand off to the
    // event loop rather than waiting for the process.
    MailSync sync(makeScript(QStringLiteral("blocker.sh"), QStringLiteral("sleep 2")));

    QElapsedTimer timer;
    timer.start();
    QVERIFY(sync.start());
    const qint64 elapsed = timer.elapsed();

    QVERIFY2(elapsed < 500,
             qPrintable(QStringLiteral("start() blocked for %1ms").arg(elapsed)));

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(spy.wait(10000));
}

void TestMailSync::argumentsAreNotShellInterpreted()
{
    // The command comes from a config file. Running it through a shell would
    // turn that value into an injection point, so the metacharacters below must
    // arrive at the script as literal arguments.
    const QString script = makeScript(QStringLiteral("args.sh"),
                                      QStringLiteral("echo \"$1\""));

    MailSync sync(QStringLiteral("%1 \"; touch %2/pwned\"")
                      .arg(script, m_dir.path()));

    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("pwned"))));
    QVERIFY(sync.log().contains(QStringLiteral("; touch")));
}

void TestMailSync::channelsAreAppendedToTheCommand()
{
    // Item 49: a sync that knows which accounts were touched passes their
    // channel names, and they must reach the script as separate arguments
    // after whatever the config line already carries.
    const QString script = makeScript(QStringLiteral("chan.sh"),
                                      QStringLiteral("echo \"[$@]\""));

    MailSync sync(script);
    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start({ QStringLiteral("work"), QStringLiteral("personal") }));
    QVERIFY(spy.wait(5000));

    QVERIFY(sync.log().contains(QStringLiteral("[work personal]")));
}

void TestMailSync::noChannelsMeansNoExtraArguments()
{
    // Empty means "sync everything", which is the script's own default. It must
    // not become an empty string argument: mbsync would read that as a channel
    // named "" and fail the run.
    const QString script = makeScript(QStringLiteral("nochan.sh"),
                                      QStringLiteral("echo \"count=$#\""));

    MailSync sync(script);
    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start());
    QVERIFY(spy.wait(5000));

    QVERIFY(sync.log().contains(QStringLiteral("count=0")));
}

void TestMailSync::channelNamesAreNotShellInterpreted()
{
    // Channel names are derived from config, same trust boundary as the command
    // itself, and reach the same QProcess argument list. The injection test
    // above covers the command; this covers the half added for item 49.
    const QString script = makeScript(QStringLiteral("chanargs.sh"),
                                      QStringLiteral("echo \"$1\""));

    MailSync sync(script);
    QSignalSpy spy(&sync, &MailSync::finished);
    QVERIFY(sync.start({ QStringLiteral("; touch %1/chanpwned")
                             .arg(m_dir.path()) }));
    QVERIFY(spy.wait(5000));

    QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("chanpwned"))));
}

void TestMailSync::phaseStartsAsMbsync()
{
    // A fresh tracker has nothing to report until it is fed, and a run is
    // mbsync's until notmuch announces itself.
    SyncPhaseTracker tracker;
    QCOMPARE(tracker.phase(), SyncPhase::Starting);

    // A timestamped mbsync line, as the script emits it.
    QVERIFY(tracker.feed(QStringLiteral("10:44:11 Socket error on imap.example.org (192.0.2.1:993): timeout.")));
    QCOMPARE(tracker.phase(), SyncPhase::Mbsync);
    QVERIFY(!tracker.statusText().isEmpty());
}

void TestMailSync::notmuchLineSwitchesPhase()
{
    // "notmuch new" announces itself with its own progress wording. Matching is
    // loose on purpose: the exact phrasing varies by version, and a status that
    // goes blank because a string moved is worse than a fixed one.
    SyncPhaseTracker tracker;
    tracker.feed(QStringLiteral("10:44:32 Channels: 5    Boxes: 39    Far: +0 *15 #0 -0    Near: +1 *0 #0 -0"));
    QCOMPARE(tracker.phase(), SyncPhase::Mbsync);

    QVERIFY(tracker.feed(QStringLiteral("10:44:33 Processed 77 total files in almost no time.")));
    QCOMPARE(tracker.phase(), SyncPhase::Notmuch);

    // Both spellings notmuch uses when it finishes.
    SyncPhaseTracker other;
    other.feed(QStringLiteral("11:00:33 Added 1 new message to the database."));
    QCOMPARE(other.phase(), SyncPhase::Notmuch);

    SyncPhaseTracker third;
    third.feed(QStringLiteral("11:11:33 No new mail."));
    QCOMPARE(third.phase(), SyncPhase::Notmuch);
}

void TestMailSync::mbsyncSummaryIsReported()
{
    // mbsync prints one summary at the end of its run and nothing per channel,
    // so this line is the only concrete thing there is to show. The counts are
    // worth surfacing; the raw "Far: +0 *15 #0 -0" tail is not.
    SyncPhaseTracker tracker;
    QVERIFY(tracker.feed(QStringLiteral("10:44:32 Channels: 5    Boxes: 39    Far: +0 *15 #0 -0    Near: +1 *0 #0 -0")));

    const QString text = tracker.statusText();
    QVERIFY2(text.contains(QStringLiteral("5")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("39")), qPrintable(text));
}

void TestMailSync::noiseLeavesThePhaseAlone()
{
    // The overwhelming majority of a real run is this one line repeated, and it
    // must not be shown or counted as a phase change.
    SyncPhaseTracker tracker;
    tracker.feed(QStringLiteral("10:44:32 Channels: 5    Boxes: 39    Far: +0 *0 #0 -0    Near: +0 *0 #0 -0"));
    const QString before = tracker.statusText();

    QVERIFY(!tracker.feed(QStringLiteral(
        "11:11:33 Note: Ignoring non-mail file: /home/you/Mail/example/Inbox/.uidvalidity")));
    QCOMPARE(tracker.statusText(), before);
    QCOMPARE(tracker.phase(), SyncPhase::Mbsync);
}

void TestMailSync::aHostileLineCannotGrowTheStatus()
{
    // Sync output is local but unstructured, and it lands in a status label.
    // A long line must be truncated rather than resizing the status bar, and
    // control characters must not survive into it.
    SyncPhaseTracker tracker;
    tracker.feed(QStringLiteral("10:00:00 Channels: %1    Boxes: 2")
                     .arg(QString(500, QLatin1Char('9'))));

    const QString text = tracker.statusText();
    QVERIFY2(text.size() <= 120, qPrintable(QString::number(text.size())));
    QVERIFY(!text.contains(QLatin1Char('\n')));
    QVERIFY(!text.contains(QLatin1Char('\r')));
}

void TestMailSync::runMarkersAreNotAPhase()
{
    // The script's own banners bracket the run. RUN START must not read as
    // mbsync output, and RUN END must not leave a phase claiming work is still
    // going: the exit status decides the outcome, deliberately, so nothing here
    // may be parsed into success or failure.
    SyncPhaseTracker tracker;
    QVERIFY(!tracker.feed(QStringLiteral("===== RUN START: 2026-08-07T11:10:47+02:00 =====")));
    QCOMPARE(tracker.phase(), SyncPhase::Starting);

    tracker.feed(QStringLiteral("11:11:33 No new mail."));
    QCOMPARE(tracker.phase(), SyncPhase::Notmuch);

    QVERIFY(!tracker.feed(QStringLiteral(
        "===== RUN END: 2026-08-07T11:11:33+02:00  status=FAILED  mbsync=1 notmuch=0 =====")));
    // Unchanged: the banner says nothing the status bar should repeat, and the
    // caller reports the outcome from the exit code.
    QCOMPARE(tracker.phase(), SyncPhase::Notmuch);
}

void TestMailSync::theChannelNameIsShown()
{
    // What the user actually asked for: which account is being synced right
    // now. mbsync -V announces each channel as it reaches it, and the channel
    // name is the account name.
    SyncPhaseTracker tracker;

    QVERIFY(tracker.feed(QStringLiteral("11:31:16 Channel provider-work")));
    QCOMPARE(tracker.phase(), SyncPhase::Mbsync);
    QVERIFY2(tracker.statusText().contains(QStringLiteral("provider-work")),
             qPrintable(tracker.statusText()));

    // The per-box chatter between channels must not displace it: the account
    // is the useful thing, and a box name changing several times a second
    // would make the status bar unreadable.
    const QString onChannel = tracker.statusText();
    QVERIFY(!tracker.feed(QStringLiteral("11:31:16 Opening far side box INBOX...")));
    QCOMPARE(tracker.statusText(), onChannel);
    QVERIFY(!tracker.feed(QStringLiteral("11:32:20 near side: 14758 messages, 0 recent")));
    QCOMPARE(tracker.statusText(), onChannel);

    // The next channel does replace it.
    QVERIFY(tracker.feed(QStringLiteral("11:33:04 Channel provider-personal")));
    QVERIFY(tracker.statusText().contains(QStringLiteral("provider-personal")));
    QVERIFY(!tracker.statusText().contains(QStringLiteral("provider-work")));
}

void TestMailSync::aChannelNameIsNotLetInVerbatim()
{
    // The channel name comes from a config file this app does not own, and it
    // lands in a status label. A long one must not be able to stretch the
    // status bar, whatever mbsync was told to call it.
    SyncPhaseTracker tracker;
    tracker.feed(QStringLiteral("11:31:16 Channel %1")
                     .arg(QString(400, QLatin1Char('x'))));

    const QString text = tracker.statusText();
    QVERIFY2(text.size() <= 120, qPrintable(QString::number(text.size())));
    QVERIFY(!text.contains(QLatin1Char('\n')));
}

// Item 54. A cron sync clears the pending-edit count only if it succeeded, and
// the only evidence of that available to this process is the RUN END line the
// script writes into its log. These tests pin the parser against the exact
// shape assets/mailsync.sh emits.

// Item 174. The status file is what the application READS, as against the log,
// which is for a human. These pin the reader against the exact shape
// assets/mailsync.sh writes; assets/test_mailsync.py pins the writer against
// the same shape from the other side, and the two agree by test rather than by
// shared code, exactly as the two rules.json readers do.

static QString writeStatus(const QDir &dir, const QString &name,
                           const QByteArray &contents)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(contents);
    file.close();
    return path;
}

void TestMailSync::readStatusReadsAnOkRun()
{
    const QString path = writeStatus(
        QDir(m_dir.path()), QStringLiteral("ok.json"),
        R"({"version": 1, "run_id": "2026-08-29T10:00:00+02:00",
            "started": "2026-08-29T10:00:00+02:00",
            "ended": "2026-08-29T10:00:12+02:00",
            "state": "ok", "channels": ["-a"],
            "mbsync_status": 0, "notmuch_status": 0})");
    QVERIFY(!path.isEmpty());

    const SyncStatus status = MailSync::readStatus(path);
    QCOMPARE(status.state, SyncState::Ok);
    QVERIFY(status.ended.isValid());
}

void TestMailSync::readStatusReadsTheChannelsARunCarried()
{
    // The whole reason this file exists rather than the log's banner: the
    // application clears its pending count for the accounts a run carried, and
    // the log could never say which those were.
    const QString path = writeStatus(
        QDir(m_dir.path()), QStringLiteral("channels.json"),
        R"({"version": 1, "run_id": "r", "started": "2026-08-29T10:00:00+02:00",
            "ended": "2026-08-29T10:00:12+02:00", "state": "ok",
            "channels": ["work", "personal"],
            "mbsync_status": 0, "notmuch_status": 0})");
    QVERIFY(!path.isEmpty());

    const SyncStatus status = MailSync::readStatus(path);
    QCOMPARE(status.state, SyncState::Ok);
    QCOMPARE(status.channels,
             (QStringList{ QStringLiteral("work"), QStringLiteral("personal") }));
    QVERIFY(!status.everyChannel);
}

void TestMailSync::readStatusReadsAFullRunAsEveryAccount()
{
    // "-a" is not a channel name and must not be matched against one: a full
    // run carries every account, so a reader treating it as an unknown channel
    // would clear nothing on exactly the run that carried everything.
    const QString path = writeStatus(
        QDir(m_dir.path()), QStringLiteral("full.json"),
        R"({"version": 1, "run_id": "r", "started": "2026-08-29T10:00:00+02:00",
            "ended": "2026-08-29T10:00:12+02:00", "state": "ok",
            "channels": ["-a"], "mbsync_status": 0, "notmuch_status": 0})");
    QVERIFY(!path.isEmpty());

    const SyncStatus status = MailSync::readStatus(path);
    QVERIFY2(status.everyChannel, "a -a run was not read as every account");
}

void TestMailSync::readStatusReadsASkippedRun()
{
    // Item 125. A skipped run releases a lock it never took, so the spinner had
    // nothing to clear on. It is a terminal state, and distinct from a failure:
    // the other run is doing the work.
    const QString path = writeStatus(
        QDir(m_dir.path()), QStringLiteral("skip.json"),
        R"({"version": 1, "run_id": "r", "started": "2026-08-29T10:00:00+02:00",
            "ended": "2026-08-29T10:00:00+02:00", "state": "skipped",
            "channels": ["-a"], "mbsync_status": -1, "notmuch_status": -1})");
    QVERIFY(!path.isEmpty());

    const SyncStatus status = MailSync::readStatus(path);
    QCOMPARE(status.state, SyncState::Skipped);
}

void TestMailSync::readStatusOnAMissingFileIsUnknown()
{
    QCOMPARE(MailSync::readStatus(m_dir.filePath(QStringLiteral("nope.json"))).state,
             SyncState::Unknown);
    QCOMPARE(MailSync::readStatus(QString()).state, SyncState::Unknown);
}

void TestMailSync::readStatusOnRubbishIsUnknown()
{
    const QString path = writeStatus(QDir(m_dir.path()),
                                     QStringLiteral("rubbish.json"),
                                     "this is not json at all\n");
    QVERIFY(!path.isEmpty());
    QCOMPARE(MailSync::readStatus(path).state, SyncState::Unknown);
}

void TestMailSync::readStatusOnATruncatedFileIsUnknown()
{
    // The script writes atomically through a temp file and mv precisely so this
    // cannot happen, but a reader that trusts that is one filesystem away from
    // being wrong. Unknown changes no state, so a torn read is harmless.
    const QString path = writeStatus(QDir(m_dir.path()),
                                     QStringLiteral("torn.json"),
                                     R"({"version": 1, "state": "o)");
    QVERIFY(!path.isEmpty());
    QCOMPARE(MailSync::readStatus(path).state, SyncState::Unknown);
}

void TestMailSync::readStatusOnAnUnknownVersionIsUnknown()
{
    // Refused rather than guessed at, the rule the rules file already follows:
    // a future version may mean something different by the same field names,
    // and acting on it would be worse than observing nothing.
    const QString path = writeStatus(
        QDir(m_dir.path()), QStringLiteral("future.json"),
        R"({"version": 99, "run_id": "r", "started": "2026-08-29T10:00:00+02:00",
            "ended": "2026-08-29T10:00:12+02:00", "state": "ok",
            "channels": ["-a"], "mbsync_status": 0, "notmuch_status": 0})");
    QVERIFY(!path.isEmpty());
    QCOMPARE(MailSync::readStatus(path).state, SyncState::Unknown);
}

void TestMailSync::readStatusReadsAFileTheScriptActuallyWrote()
{
    // The guard against the two sides drifting apart. Every test above writes
    // what this file BELIEVES the script emits; this one runs the real script
    // with stubbed binaries and reads what it actually wrote.
    //
    // Skipped rather than failed where bash or the script is unavailable: a
    // packaging build has no reason to carry either, and a test that cannot run
    // has observed nothing.
    const QString script = QStringLiteral(SOURCE_DIR "/assets/mailsync.sh");
    if (!QFile::exists(script))
        QSKIP("assets/mailsync.sh not found");

    QTemporaryDir home;
    QVERIFY(home.isValid());

    // Stubs, so nothing reaches the network and the real lock is never taken.
    const QString bin = home.filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(bin));
    for (const QString &name : { QStringLiteral("mbsync"),
                                 QStringLiteral("notmuch") }) {
        QFile stub(bin + QLatin1Char('/') + name);
        QVERIFY(stub.open(QIODevice::WriteOnly | QIODevice::Text));
        stub.write("#!/bin/bash\nexit 0\n");
        stub.close();
        QVERIFY(stub.setPermissions(QFile::ReadOwner | QFile::WriteOwner
                                    | QFile::ExeOwner));
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("HOME"), home.path());
    env.insert(QStringLiteral("PATH"),
               bin + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
    // Never /tmp/mbsync.lock: that is the mutex the user's cron sync uses, and
    // a test that took it would block their mail.
    env.insert(QStringLiteral("MAILSYNC_LOCKFILE"),
               home.filePath(QStringLiteral("lock")));

    QProcess proc;
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral("bash"), { script, QStringLiteral("work") });
    if (!proc.waitForStarted(5000))
        QSKIP("bash not available");
    QVERIFY(proc.waitForFinished(30000));

    const SyncStatus status = MailSync::readStatus(
        home.filePath(QStringLiteral(".local/state/qtmaildir/syncstatus.json")));
    QCOMPARE(status.state, SyncState::Ok);
    QCOMPARE(status.channels, QStringList{ QStringLiteral("work") });
    QVERIFY(!status.everyChannel);
}

void TestMailSync::lastRunOutcomeReadsAnOkRun()
{
    const QString path = m_dir.filePath(QStringLiteral("ok.log"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("===== RUN START: 2026-08-09T10:20:00+02:00 =====\n"
               "10:20:01 Channel one\n"
               "===== RUN END: 2026-08-09T10:20:03+02:00  status=OK =====\n");
    file.close();

    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Ok);
}

void TestMailSync::lastRunOutcomeReadsAFailedRun()
{
    // The failure line carries extra fields after status=, so a parser keyed on
    // the whole line rather than the token would miss it.
    const QString path = m_dir.filePath(QStringLiteral("failed.log"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("===== RUN END: 2026-08-09T10:30:07+02:00  status=FAILED  "
               "mbsync=1 notmuch=0 =====\n");
    file.close();

    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Failed);
}

void TestMailSync::lastRunOutcomeTakesTheLastMarkerNotTheFirst()
{
    // The log accumulates runs and is rotated by logrotate, not by the script,
    // so it normally holds many. Reading the first marker would report an
    // outcome from hours ago, and in the direction that matters: an old OK
    // would clear the count for a run that has just failed.
    const QString path = m_dir.filePath(QStringLiteral("many.log"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("===== RUN END: 2026-08-09T10:00:03+02:00  status=OK =====\n"
               "===== RUN END: 2026-08-09T10:10:03+02:00  status=OK =====\n"
               "===== RUN END: 2026-08-09T10:20:07+02:00  status=FAILED  "
               "mbsync=1 notmuch=0 =====\n");
    file.close();

    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Failed);
}

void TestMailSync::lastRunOutcomeOnAMissingLogIsUnknown()
{
    // Unknown, never Ok. The caller clears the user's pending count on Ok, so
    // an absent log must not be able to assert that edits reached the store.
    QCOMPARE(MailSync::lastRunOutcome(m_dir.filePath(QStringLiteral("nope.log"))),
             SyncOutcome::Unknown);
    QCOMPARE(MailSync::lastRunOutcome(QString()), SyncOutcome::Unknown);
}

void TestMailSync::lastRunOutcomeOnALogWithNoMarkerIsUnknown()
{
    const QString path = m_dir.filePath(QStringLiteral("nomarker.log"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("10:20:01 Channel one\n10:20:02 Channel two\n");
    file.close();

    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Unknown);
}

void TestMailSync::lastRunOutcomeIgnoresATrailingPartialRun()
{
    // The lock is released when the script exits, but the window polls
    // /proc/locks, so it can read the log while a LATER run has already started
    // and written its RUN START. Only END lines carry an outcome.
    const QString path = m_dir.filePath(QStringLiteral("partial.log"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("===== RUN END: 2026-08-09T10:20:03+02:00  status=OK =====\n"
               "===== RUN START: 2026-08-09T10:30:00+02:00 =====\n"
               "10:30:01 Channel one\n");
    file.close();

    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Ok);
}

void TestMailSync::lastRunOutcomeReadsATailOfAHugeLog()
{
    // This runs on the UI thread every time a background sync ends, up to six
    // times an hour, against a file logrotate lets grow all day, so it reads a
    // bounded tail rather than the whole file.
    //
    // Asserted by CONTENT, not by timing. A first version of this test timed
    // the call and required it under 100 ms; it passed with the seek deleted,
    // because reading 10 MB is quick enough either way. The probe measured
    // nothing. A marker reachable only from the head of the file cannot be
    // found by a tail read and cannot be missed by a whole-file read, so the
    // two implementations give different answers here.
    const QString path = m_dir.filePath(QStringLiteral("huge.log"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("===== RUN END: 2026-08-09T09:00:03+02:00  status=OK =====\n");
    const QByteArray filler(200, 'x');
    for (int i = 0; i < 50000; ++i) {
        file.write(filler);
        file.write("\n");
    }
    file.close();
    QVERIFY2(QFileInfo(path).size() > 4L * 1024 * 1024,
             "the fixture must be big enough to matter");

    // Unknown, because the only marker is megabytes above the tail. Reading the
    // whole file would return Ok and fail this.
    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Unknown);

    // The guard the paragraph above demands: prove the tail read finds a marker
    // that IS within reach, so the Unknown above is bounded reading rather than
    // a parser that never matches anything in a large file.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text));
    file.write("===== RUN END: 2026-08-09T10:20:03+02:00  status=FAILED  "
               "mbsync=1 notmuch=0 =====\n");
    file.close();

    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Failed);
}

void TestMailSync::lastRunOutcomeReadsABannerTheScriptActuallyWrote()
{
    // Every other fixture here is a string this test file made up, and the
    // first batch of them was WRONG: they used "2026-08-09 10:20:03" where the
    // script writes `date -Iseconds`, so "2026-08-09T10:20:03+02:00". The
    // parser happened to survive it, because it keys on the "===== RUN END:"
    // prefix and the status= token rather than on the timestamp, but nothing
    // here proved the two formats agreed. A fixture invented to match the code
    // tests the code against itself.
    //
    // So build the banner the way assets/mailsync.sh builds it, with the same
    // command, and parse that. If the script's format changes, or the parser
    // starts depending on the timestamp shape, this fails.
    QProcess date;
    date.start(QStringLiteral("date"), { QStringLiteral("-Iseconds") });
    QVERIFY(date.waitForFinished(5000));
    QCOMPARE(date.exitCode(), 0);
    const QString stamp =
        QString::fromUtf8(date.readAllStandardOutput()).trimmed();
    QVERIFY(!stamp.isEmpty());

    const QString path = m_dir.filePath(QStringLiteral("real.log"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(QStringLiteral("===== RUN END: %1  status=OK =====\n")
                   .arg(stamp).toUtf8());
    file.close();

    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Ok);

    // And the failure banner, whose trailing fields are the part a whole-line
    // match would miss.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text));
    file.write(QStringLiteral("===== RUN END: %1  status=FAILED  mbsync=1 "
                              "notmuch=0 =====\n").arg(stamp).toUtf8());
    file.close();

    QCOMPARE(MailSync::lastRunOutcome(path), SyncOutcome::Failed);
}

QTEST_MAIN(TestMailSync)
#include "test_mailsync.moc"
