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

#include <QElapsedTimer>
#include <QFile>
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

QTEST_MAIN(TestMailSync)
#include "test_mailsync.moc"
