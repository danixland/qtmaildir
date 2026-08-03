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

QTEST_MAIN(TestMailSync)
#include "test_mailsync.moc"
