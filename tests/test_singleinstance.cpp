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

#include <QLocalServer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "launchselectors.h"
#include "singleinstance.h"

/// The socket half of item 200. Every case runs against a socket name of its
/// own inside a QTemporaryDir, so nothing here can reach a real running
/// qtmaildir, and two cases cannot collide.
class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void theFirstInstanceBecomesTheServer();
    void aSecondInstanceHandsOverItsSelectors();
    void aSecondInstanceWithNoSelectorsStillArrives();
    void aStaleSocketFileIsReclaimed();
    void anUncreatableSocketDoesNotStopStartup();

private:
    QTemporaryDir m_dir;
    QString m_socketPath;
};

void TestSingleInstance::init()
{
    QVERIFY(m_dir.isValid());
    // A name per test function, so a socket left behind by one case cannot
    // decide the next one's result.
    m_socketPath = m_dir.filePath(
        QStringLiteral("sock-%1").arg(QTest::currentTestFunction()));
}

void TestSingleInstance::theFirstInstanceBecomesTheServer()
{
    SingleInstance first(m_socketPath);
    QVERIFY(first.tryBecomeServer());
    QVERIFY(first.isServer());
}

void TestSingleInstance::aSecondInstanceHandsOverItsSelectors()
{
    // The whole point of the feature: the second process does not open a
    // window, it hands its request to the first and exits.
    SingleInstance first(m_socketPath);
    QVERIFY(first.tryBecomeServer());

    QSignalSpy arrived(&first, &SingleInstance::selectorsReceived);

    LaunchSelectors selectors;
    selectors.account = QStringLiteral("work");
    selectors.messageId = QStringLiteral("<abc@example.org>");

    SingleInstance second(m_socketPath);
    QVERIFY(!second.tryBecomeServer());
    QVERIFY(second.sendToRunningInstance(selectors));

    QTRY_VERIFY_WITH_TIMEOUT(arrived.count() == 1, 5000);
    const auto received =
        arrived.first().at(0).value<LaunchSelectors>();
    QCOMPARE(received.account, QStringLiteral("work"));
    QCOMPARE(received.messageId, QStringLiteral("<abc@example.org>"));
}

void TestSingleInstance::aSecondInstanceWithNoSelectorsStillArrives()
{
    // A bare `qtmaildir` against a running instance means "raise yourself".
    // That is a real request, so it must arrive rather than being dropped as
    // an empty message.
    SingleInstance first(m_socketPath);
    QVERIFY(first.tryBecomeServer());

    QSignalSpy arrived(&first, &SingleInstance::selectorsReceived);

    SingleInstance second(m_socketPath);
    QVERIFY(!second.tryBecomeServer());
    QVERIFY(second.sendToRunningInstance(LaunchSelectors()));

    QTRY_VERIFY_WITH_TIMEOUT(arrived.count() == 1, 5000);
    QVERIFY(arrived.first().at(0).value<LaunchSelectors>().isEmpty());
}

void TestSingleInstance::aStaleSocketFileIsReclaimed()
{
    // A crash or a kill leaves the socket file behind, and listen() then fails
    // with AddressInUse on a file nothing is serving. Without recovery the
    // application would never start again until someone deleted it by hand.
    //
    // A plain file at the socket path stands in for what a killed process
    // leaves behind: a filesystem entry with no process serving it, so nothing
    // answers a connection.
    QFile stale(m_socketPath);
    QVERIFY(stale.open(QIODevice::WriteOnly));
    stale.close();
    QVERIFY(QFile::exists(m_socketPath));

    SingleInstance fresh(m_socketPath);
    QVERIFY2(fresh.tryBecomeServer(),
             "a stale socket file must not stop the application starting");
    QVERIFY(fresh.isServer());
}

void TestSingleInstance::anUncreatableSocketDoesNotStopStartup()
{
    // A read-only state directory must degrade to today's behaviour, a window
    // that opens and works, rather than to no mail client at all. The caller
    // reads isServer() as false and carries on.
    const QString impossible =
        m_dir.filePath(QStringLiteral("no/such/directory/sock"));

    SingleInstance instance(impossible);
    QVERIFY(!instance.tryBecomeServer());
    QVERIFY(!instance.isServer());
    // And it cannot reach a running instance either, since there is none.
    QVERIFY(!instance.sendToRunningInstance(LaunchSelectors()));
}

QTEST_MAIN(TestSingleInstance)
#include "test_singleinstance.moc"
