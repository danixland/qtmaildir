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

#include "launchselectors.h"

/// The command line and the socket payload, over values. No window, no
/// QApplication: this is the half of item 200 that can be asserted exactly,
/// which is why it is its own unit rather than code inside main.cpp.
class TestLaunchSelectors : public QObject
{
    Q_OBJECT

private slots:
    void anEmptyCommandLineSelectsNothing();
    void eachSelectorIsParsed();
    void theThreeSelectorsCompose();
    void anUnknownOptionIsReportedNotFatal();
    void aPayloadRoundTrips();
    void anEmptyPayloadRoundTripsToNothing();
    void aTruncatedPayloadIsRejected();
    void anOversizedPayloadIsRejected();
};

void TestLaunchSelectors::anEmptyCommandLineSelectsNothing()
{
    QString error;
    const LaunchSelectors selectors =
        LaunchSelectors::parse({ QStringLiteral("qtmaildir") }, &error);

    QVERIFY(error.isEmpty());
    QVERIFY(selectors.isEmpty());
    QVERIFY(selectors.account.isEmpty());
    QVERIFY(selectors.threadId.isEmpty());
    QVERIFY(selectors.messageId.isEmpty());
}

void TestLaunchSelectors::eachSelectorIsParsed()
{
    QString error;

    const LaunchSelectors account = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"), QStringLiteral("--account"),
          QStringLiteral("work") }, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(account.account, QStringLiteral("work"));
    QVERIFY(!account.isEmpty());

    const LaunchSelectors thread = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"), QStringLiteral("--thread"),
          QStringLiteral("0000000000001a2b") }, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(thread.threadId, QStringLiteral("0000000000001a2b"));

    const LaunchSelectors message = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"), QStringLiteral("--message"),
          QStringLiteral("<abc@example.org>") }, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(message.messageId, QStringLiteral("<abc@example.org>"));
}

void TestLaunchSelectors::theThreeSelectorsCompose()
{
    // They are not exclusive: "open this message, in this account's view" is
    // one sensible request, and the spec says they compose.
    QString error;
    const LaunchSelectors selectors = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"),
          QStringLiteral("--account"), QStringLiteral("work"),
          QStringLiteral("--thread"), QStringLiteral("00001a2b"),
          QStringLiteral("--message"), QStringLiteral("<abc@example.org>") },
        &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(selectors.account, QStringLiteral("work"));
    QCOMPARE(selectors.threadId, QStringLiteral("00001a2b"));
    QCOMPARE(selectors.messageId, QStringLiteral("<abc@example.org>"));
}

void TestLaunchSelectors::anUnknownOptionIsReportedNotFatal()
{
    // Reported so main() can print it, and NOT a crash or a silent ignore.
    // Today's strcmp loop ignores everything it does not know, which is how a
    // typo currently produces a normal window and no clue.
    QString error;
    const LaunchSelectors selectors = LaunchSelectors::parse(
        { QStringLiteral("qtmaildir"), QStringLiteral("--nonsense") }, &error);

    QVERIFY(!error.isEmpty());
    QVERIFY(selectors.isEmpty());
}

void TestLaunchSelectors::aPayloadRoundTrips()
{
    // What crosses the socket. A round trip is the whole contract: the values
    // that go in are the values that come out, including one with an embedded
    // newline, which is what defeats a line-based format.
    LaunchSelectors original;
    original.account = QStringLiteral("work");
    original.threadId = QStringLiteral("00001a2b");
    original.messageId = QStringLiteral("<a\nb@example.org>");

    QString error;
    const LaunchSelectors parsed =
        LaunchSelectors::fromPayload(original.toPayload(), &error);

    QVERIFY(error.isEmpty());
    QCOMPARE(parsed.account, original.account);
    QCOMPARE(parsed.threadId, original.threadId);
    QCOMPARE(parsed.messageId, original.messageId);
}

void TestLaunchSelectors::anEmptyPayloadRoundTripsToNothing()
{
    // A bare `qtmaildir` with a running instance still sends a payload: it
    // means "raise yourself", which is a real request and not an error.
    QString error;
    const LaunchSelectors parsed =
        LaunchSelectors::fromPayload(LaunchSelectors().toPayload(), &error);

    QVERIFY(error.isEmpty());
    QVERIFY(parsed.isEmpty());
}

void TestLaunchSelectors::aTruncatedPayloadIsRejected()
{
    // The socket hands over whatever it is given. A half-written payload must
    // be refused rather than half-applied.
    LaunchSelectors original;
    original.account = QStringLiteral("work");
    const QByteArray payload = original.toPayload();
    QVERIFY(payload.size() > 4);

    QString error;
    const LaunchSelectors parsed =
        LaunchSelectors::fromPayload(payload.left(payload.size() - 2), &error);

    QVERIFY(!error.isEmpty());
    QVERIFY(parsed.isEmpty());
}

void TestLaunchSelectors::anOversizedPayloadIsRejected()
{
    // A cap, because a local socket will hand over as much as the peer sends.
    // The peer is the user's own process, so this is not a hostile-input
    // defence; it is what stops a confused writer from being read as a
    // gigabyte of selector.
    QString error;
    const LaunchSelectors parsed = LaunchSelectors::fromPayload(
        QByteArray(LaunchSelectors::kMaxPayloadBytes + 1, 'x'), &error);

    QVERIFY(!error.isEmpty());
    QVERIFY(parsed.isEmpty());
}

QTEST_MAIN(TestLaunchSelectors)
#include "test_launchselectors.moc"
