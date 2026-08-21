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

#include "messagesender.h"

class TestMessageSender : public QObject
{
    Q_OBJECT

private slots:
    void aSuccessfulCommandReportsSent();
    void theMessageArrivesOnStdinIntact();
    void aLargeMessageArrivesWhole();
    void aFailingCommandReportsItsStderr();
    void aCommandThatDoesNotExistReportsAFailure();
    void aCommandThatIsNotExecutableReportsAFailure();
    void anEmptyCommandIsRefusedWithoutRunning();
    void aCommandOfOnlyWhitespaceIsRefusedWithoutRunning();
    void exitCode75IsAnOrdinaryFailure();
    void aSilentFailureStillReportsAReason();
    void aCommandThatNeverReadsStdinIsStillJudgedByItsExitStatus();
    void aCrashedCommandIsAFailureWithAReason();
    void aSecondSendIsRefusedWhileOneIsRunning();
    void shellMetacharactersReachNoShell();
    void nothingIsEverReportedTwice();
    void destroyingTheSenderLetsAnInFlightSendFinish();
    void destroyingTheSenderEmitsNothing();
    void aPerSendConnectionMustBeSingleShot();

private:
    QString writeStub(const QString &name, const QString &body,
                      bool executable = true);

    QTemporaryDir m_dir;
};

QString TestMessageSender::writeStub(const QString &name, const QString &body,
                                     bool executable)
{
    const QString path = m_dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(QStringLiteral("#!/bin/sh\n%1\n").arg(body).toUtf8());
    file.close();
    QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner;
    if (executable)
        permissions |= QFile::ExeOwner;
    file.setPermissions(permissions);
    return path;
}

void TestMessageSender::aSuccessfulCommandReportsSent()
{
    const QString stub = writeStub(QStringLiteral("ok.sh"), QStringLiteral("cat >/dev/null"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, QByteArray("From: a@example.org\r\n\r\nbody\r\n")));

    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
    QVERIFY2(spy.at(0).at(1).toString().isEmpty(),
             "a successful send carried an error message");
    QVERIFY2(!sender.isRunning(), "the sender still reports a run in progress");
}

void TestMessageSender::theMessageArrivesOnStdinIntact()
{
    // The property that matters most: the bytes the builder produced are the
    // bytes the command receives. A stub that writes stdin to a file is the
    // only way to see it, since there is no MTA to ask.
    const QString captured = m_dir.filePath(QStringLiteral("captured.eml"));
    const QString stub = writeStub(QStringLiteral("capture.sh"),
                                   QStringLiteral("cat > '%1'").arg(captured));
    QVERIFY(!stub.isEmpty());

    const QByteArray bytes(
        "From: a@example.org\r\n"
        "Subject: =?UTF-8?B?UGVyY2jDqQ==?=\r\n"
        "\r\n"
        "Perch=C3=A9 accented body.\r\n");

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, bytes));
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    QFile file(captured);
    QVERIFY2(file.open(QIODevice::ReadOnly), "the stub captured no stdin at all");
    QCOMPARE(file.readAll(), bytes);
}

void TestMessageSender::aLargeMessageArrivesWhole()
{
    // A message with an attachment is megabytes, not bytes, and a pipe holds
    // 64KB. If the write were not driven by the event loop the process would
    // deadlock on a full pipe, or the tail would be silently dropped and a
    // truncated message would be reported as sent. Measured: 1.6MB in one
    // write() call returns the full count only because QProcess buffers it and
    // drains it as the reader consumes; a probe confirmed the payload arrives
    // byte-identical.
    const QString captured = m_dir.filePath(QStringLiteral("big.eml"));
    const QString stub = writeStub(QStringLiteral("bigcapture.sh"),
                                   QStringLiteral("cat > '%1'").arg(captured));
    QVERIFY(!stub.isEmpty());

    QByteArray bytes("From: a@example.org\r\n\r\n");
    // Well past a pipe buffer, and not a repeating single byte, so a partial
    // write cannot accidentally compare equal.
    for (int i = 0; i < 60000; ++i)
        bytes += QByteArray::number(i) + "\r\n";
    QVERIFY(bytes.size() > 300000);

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, bytes));
    QVERIFY(spy.wait(10000));
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    QFile file(captured);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray got = file.readAll();
    QCOMPARE(got.size(), bytes.size());
    QCOMPARE(got, bytes);
}

void TestMessageSender::aFailingCommandReportsItsStderr()
{
    // stderr is shown verbatim: network errors, authentication failures and
    // server rejections all belong to send_command, and this application
    // deliberately does not interpret them.
    const QString stub = writeStub(
        QStringLiteral("fail.sh"),
        QStringLiteral("cat >/dev/null; echo 'auth failed: bad password' >&2; exit 1"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, QByteArray("body")));

    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    QVERIFY2(spy.at(0).at(1).toString().contains(QStringLiteral("auth failed")),
             qPrintable(QStringLiteral("stderr was not reported: '%1'")
                            .arg(spy.at(0).at(1).toString())));
}

void TestMessageSender::aCommandThatDoesNotExistReportsAFailure()
{
    // A typo'd path is the likely cause, so the message names the command.
    // QProcess emits errorOccurred(FailedToStart) INSTEAD OF finished(), which
    // is the trap MailSync already documents: without handling it the signal
    // never arrives and the popup waits forever. Measured on Qt 6.11:
    // finCount 0, errCount 1.
    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(QStringLiteral("/nonexistent/msmtp"), QByteArray("body")));

    QVERIFY2(spy.wait(5000), "no result was ever reported for a missing command");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    QVERIFY2(spy.at(0).at(1).toString().contains(QStringLiteral("msmtp")),
             qPrintable(QStringLiteral("the error does not name the command: '%1'")
                            .arg(spy.at(0).at(1).toString())));
}

void TestMessageSender::aCommandThatIsNotExecutableReportsAFailure()
{
    // A separate case from a missing file and reached by an ordinary mistake:
    // a script written by the user and never chmod'd. It also arrives as
    // FailedToStart with no finished(), so the same handler covers it, but a
    // test asserting only the missing-file case would pass against a handler
    // keyed on the errno rather than on the error enum.
    const QString stub = writeStub(QStringLiteral("noexec.sh"),
                                   QStringLiteral("cat >/dev/null"), false);
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, QByteArray("body")));

    QVERIFY2(spy.wait(5000), "no result was ever reported for a non-executable command");
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    QVERIFY(!spy.at(0).at(1).toString().isEmpty());
}

void TestMessageSender::anEmptyCommandIsRefusedWithoutRunning()
{
    // A receive-only account. The compose actions are disabled on its mail, so
    // this should be unreachable; refusing here rather than asserting means a
    // future caller cannot accidentally send from an account that cannot.
    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY2(!sender.send(QString(), QByteArray("body")),
             "an empty command was accepted");
    QCOMPARE(spy.count(), 0);
    QVERIFY(!sender.isRunning());
}

void TestMessageSender::aCommandOfOnlyWhitespaceIsRefusedWithoutRunning()
{
    // A config file with `send_command = ` and a trailing space reaches
    // exactly this, and it must not run anything.
    //
    // MEASURED, and worth stating precisely so this is not mistaken for a
    // sharper test than it is: send() has TWO guards that both catch a blank
    // command, the trimmed()-empty check and the parts.isEmpty() check after
    // QProcess::splitCommand("   ") returns an empty list. Dropping either one
    // alone leaves this test green, because the other still refuses. Dropping
    // BOTH aborts the run outright: QProcess treats an empty program as fatal,
    // and the mutation reports "Received a fatal error" rather than a failed
    // comparison. The pair is what is under test here; the redundancy is
    // deliberate, since the fatal path is the one thing a send must never
    // reach.
    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY2(!sender.send(QStringLiteral("   \t "), QByteArray("body")),
             "a whitespace-only command was accepted");
    QCOMPARE(spy.count(), 0);
    QVERIFY(!sender.isRunning());
}

void TestMessageSender::exitCode75IsAnOrdinaryFailure()
{
    // Explicitly asserted so the sync path's special handling of 75 is never
    // copied here. There is no lock to contend for, so 75 means only what the
    // command chose it to mean: not sent.
    const QString stub = writeStub(QStringLiteral("busy.sh"),
                                   QStringLiteral("cat >/dev/null; exit 75"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, QByteArray("body")));

    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toBool(), false);
}

void TestMessageSender::aSilentFailureStillReportsAReason()
{
    // The mailsync.sh lesson in the other direction: a command that fails
    // without saying anything must not produce an empty error string, because
    // the popup would then show a failure with a blank explanation and the
    // user would have nothing to act on.
    const QString stub = writeStub(QStringLiteral("silent.sh"),
                                   QStringLiteral("cat >/dev/null; exit 3"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, QByteArray("body")));

    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    const QString error = spy.at(0).at(1).toString();
    QVERIFY2(!error.isEmpty(), "a silent failure reported no reason at all");
    QVERIFY2(error.contains(QStringLiteral("3")),
             qPrintable(QStringLiteral("the exit status is not named: '%1'").arg(error)));
}

void TestMessageSender::aCommandThatNeverReadsStdinIsStillJudgedByItsExitStatus()
{
    // Measured on Qt 6.11: a command that exits without draining a large stdin
    // emits errorOccurred(WriteError) BEFORE finished(). A handler that treated
    // any error as a failure to start would report the write error and swallow
    // the real exit status; a handler that reported on every error would report
    // twice. The exit status is the only authority, exactly as it is for the
    // sync script, so this asserts the reason the command GAVE.
    const QString stub = writeStub(
        QStringLiteral("nonreading.sh"),
        QStringLiteral("echo 'recipient rejected' >&2; exit 1"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, QByteArray(1600 * 1024, 'x')));

    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    QVERIFY2(spy.at(0).at(1).toString().contains(QStringLiteral("recipient rejected")),
             qPrintable(QStringLiteral("the command's own reason was lost: '%1'")
                            .arg(spy.at(0).at(1).toString())));
}

void TestMessageSender::aCrashedCommandIsAFailureWithAReason()
{
    // A segfaulting MTA is a real failure mode and reaches a DIFFERENT branch
    // from a nonzero exit: status is CrashExit and exitCode carries the signal
    // number, so an error message built from the exit code alone would tell the
    // user the command "exited with status 11", which is not what happened.
    //
    // Measured on Qt 6.11: a crash emits errorOccurred(Crashed) and THEN
    // finished(11, CrashExit). Only finished() reports, because handleError
    // filters to FailedToStart, so the count assertion below also proves that
    // filter is doing work on a path that is not the write-error one.
    const QString stub = writeStub(QStringLiteral("crash.sh"),
                                   QStringLiteral("cat >/dev/null; kill -SEGV $$"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, QByteArray("body")));

    QVERIFY(spy.wait(5000));
    QTest::qWait(300);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);
    const QString error = spy.at(0).at(1).toString();
    QVERIFY2(!error.isEmpty(), "a crashed command reported no reason");
    QVERIFY2(error.contains(QStringLiteral("crash")),
             qPrintable(QStringLiteral("a crash was reported as an ordinary exit: '%1'")
                            .arg(error)));
}

void TestMessageSender::aSecondSendIsRefusedWhileOneIsRunning()
{
    // One QProcess, so a second send would overwrite the first's program and
    // arguments mid-flight. Refusing is what makes the popup's Sending stage
    // mean one message.
    const QString stub = writeStub(QStringLiteral("slow.sh"),
                                   QStringLiteral("cat >/dev/null; sleep 1"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(stub, QByteArray("first")));
    QVERIFY2(sender.isRunning(), "the sender does not report the run it just started");
    QVERIFY2(!sender.send(stub, QByteArray("second")),
             "a second send was accepted while one was running");

    QVERIFY(spy.wait(10000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
}

void TestMessageSender::shellMetacharactersReachNoShell()
{
    // The security property, asserted rather than asserted-about-in-a-comment.
    // The command is split into an argument list and handed to execve, so a
    // `;` in it is a literal argument and there is no shell to act on it. If
    // this ever ran through `sh -c` the stub below would be invoked and the
    // marker file would exist.
    //
    // Measured: QProcess::splitCommand("msmtp; rm x") yields ("msmtp;", "rm",
    // "x"), so the semicolon does not even separate arguments.
    const QString marker = m_dir.filePath(QStringLiteral("shell-ran"));
    const QString stub = writeStub(QStringLiteral("args.sh"),
                                   QStringLiteral("cat >/dev/null; exit 0"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;
    QSignalSpy spy(&sender, &MessageSender::finished);
    QVERIFY(sender.send(QStringLiteral("%1 ; touch %2").arg(stub, marker),
                        QByteArray("body")));
    QVERIFY(spy.wait(5000));

    QVERIFY2(!QFile::exists(marker),
             "the send command was interpreted by a shell");

    // And the same string quoted the way a shell would need it also reaches no
    // shell: double quotes are the ONLY quoting splitCommand understands.
    // Measured: single quotes are NOT stripped, so `-a 'my acct'` arrives as
    // three arguments. Recorded here because the plan's comment claimed
    // splitCommand "handles quoted arguments" without that qualification.
    QCOMPARE(QProcess::splitCommand(QStringLiteral("m -a \"my acct\" -t")),
             QStringList({QStringLiteral("m"), QStringLiteral("-a"),
                          QStringLiteral("my acct"), QStringLiteral("-t")}));
    QCOMPARE(QProcess::splitCommand(QStringLiteral("m -a 'my acct' -t")),
             QStringList({QStringLiteral("m"), QStringLiteral("-a"),
                          QStringLiteral("'my"), QStringLiteral("acct'"),
                          QStringLiteral("-t")}));
}

void TestMessageSender::nothingIsEverReportedTwice()
{
    // Reporting twice would close the send popup and then act on a second
    // result, which for a caller that files a sent copy on success means two
    // copies, or a success followed by a failure. Run every outcome through one
    // sender and count.
    const QString ok = writeStub(QStringLiteral("dup-ok.sh"),
                                 QStringLiteral("cat >/dev/null"));
    const QString bad = writeStub(QStringLiteral("dup-bad.sh"),
                                  QStringLiteral("echo boom >&2; exit 1"));
    QVERIFY(!ok.isEmpty() && !bad.isEmpty());

    for (const QString &command :
         {ok, bad, QStringLiteral("/nonexistent/msmtp")}) {
        MessageSender sender;
        QSignalSpy spy(&sender, &MessageSender::finished);
        QVERIFY(sender.send(command, QByteArray(1600 * 1024, 'x')));
        QVERIFY(spy.wait(10000));
        // Give any second signal a chance to arrive before counting.
        QTest::qWait(300);
        QVERIFY2(spy.count() == 1,
                 qPrintable(QStringLiteral("%1 reported %2 times")
                                .arg(command)
                                .arg(spy.count())));
    }
}

void TestMessageSender::destroyingTheSenderLetsAnInFlightSendFinish()
{
    // The composer's X button is reachable mid-send, and abandoning a live
    // SMTP conversation has a genuinely unknown outcome. Measured before the
    // destructor existed: plain destruction 100ms into a one-second command
    // killed the child and the work did NOT complete, announced by nothing but
    // a "QProcess: Destroyed while process is still running" warning.
    //
    // The marker file is the evidence, because it is written by the command
    // itself after its work: if the destructor killed the child, it does not
    // exist.
    const QString marker = m_dir.filePath(QStringLiteral("send-completed"));
    const QString stub = writeStub(
        QStringLiteral("slowfinish.sh"),
        QStringLiteral("cat >/dev/null; sleep 1; touch '%1'").arg(marker));
    QVERIFY(!stub.isEmpty());
    QVERIFY2(!QFile::exists(marker), "the marker existed before the send ran");

    {
        MessageSender sender;
        QVERIFY(sender.send(stub, QByteArray("body")));
        // Destroyed well before the command could finish, which is the case
        // that matters; without the wait this scope kills it.
        QTest::qWait(100);
        QVERIFY2(sender.isRunning(), "the command finished before it was abandoned");
    }

    QVERIFY2(QFile::exists(marker),
             "destroying the sender killed a send that was in flight");
}

void TestMessageSender::destroyingTheSenderEmitsNothing()
{
    // After a kill the outcome is unknown, and this class reports two outcomes
    // only. A finished(false, ...) from the destructor would report "not sent"
    // for a message that may have been delivered, which is the mailsync.sh
    // mistake pointing the other way.
    //
    // A command that outlasts the shutdown wait is what forces the kill
    // branch, so the wait is shortened by pointing the test at a command
    // longer than it rather than by changing the constant.
    const QString stub = writeStub(QStringLiteral("outlast.sh"),
                                   QStringLiteral("cat >/dev/null; sleep 30"));
    QVERIFY(!stub.isEmpty());

    QSignalSpy *spy = nullptr;
    {
        MessageSender sender;
        spy = new QSignalSpy(&sender, &MessageSender::finished);
        QVERIFY(sender.send(stub, QByteArray("body")));
        QTest::qWait(100);
        QVERIFY(sender.isRunning());
        // The destructor runs as this scope ends: it waits kShutdownWaitMs
        // for a command that will not finish, then kills it.
    }
    // The spy outlives the sender deliberately: a signal emitted during
    // destruction would have been recorded before the object went away.
    QCOMPARE(spy->count(), 0);
    delete spy;
}

void TestMessageSender::aPerSendConnectionMustBeSingleShot()
{
    // The header's contract, asserted. m_reported collapses two QProcess
    // signals into one emit, but it cannot stop a caller from accumulating
    // RECEIVERS: a long-lived sender that a caller connects to inside its send
    // path runs every previous lambda on the next result, each still holding
    // the previous message's bytes.
    //
    // This is the plan's own Task 11 shape, and it is why that step now
    // specifies Qt::SingleShotConnection.
    const QString stub = writeStub(QStringLiteral("twice.sh"),
                                   QStringLiteral("cat >/dev/null"));
    QVERIFY(!stub.isEmpty());

    MessageSender sender;   // long-lived, as a ComposeWindow member is

    // The broken shape: a bare connect() beside each send().
    int bareDeliveries = 0;
    for (int i = 0; i < 2; ++i) {
        QSignalSpy spy(&sender, &MessageSender::finished);
        connect(&sender, &MessageSender::finished, this,
                [&bareDeliveries](bool, const QString &) { ++bareDeliveries; });
        QVERIFY(sender.send(stub, QByteArray("body")));
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.count(), 1);   // ONE emit, both times
    }
    QVERIFY2(bareDeliveries == 3,
             qPrintable(QStringLiteral("expected the documented 1+2 accumulation, got %1")
                            .arg(bareDeliveries)));

    // The prescribed shape: the connection disconnects as it fires, so two
    // sends deliver two results rather than three.
    MessageSender clean;
    int singleShotDeliveries = 0;
    for (int i = 0; i < 2; ++i) {
        QSignalSpy spy(&clean, &MessageSender::finished);
        connect(&clean, &MessageSender::finished, this,
                [&singleShotDeliveries](bool, const QString &) { ++singleShotDeliveries; },
                Qt::SingleShotConnection);
        QVERIFY(clean.send(stub, QByteArray("body")));
        QVERIFY(spy.wait(5000));
    }
    QCOMPARE(singleShotDeliveries, 2);
}

QTEST_MAIN(TestMessageSender)
#include "test_messagesender.moc"
