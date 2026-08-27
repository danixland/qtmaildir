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

// gmime BEFORE any Qt header. glib declares a struct field named "signals",
// which Qt #defines to Q_SIGNALS. Needed here for g_log_set_handler(), which is
// how aGroupIsDroppedByTheGuardAndNotByAFailedCast() sees the difference
// between a group dropped by the guard and one dropped by a failed cast.
#include <gmime/gmime.h>

#include <QtTest>
#include <QTemporaryDir>

#include "composecontext.h"
#include "config.h"
#include "mimeparser.h"
#include "types.h"

using ComposeContextBuilder::Recipient;

class TestComposeContext : public QObject
{
    Q_OBJECT

private slots:
    // Own addresses.
    void everyOwnAddressIsCollected();
    void aBlankOwnAddressIsNotCollected();

    // Header parsing, the foundation the recipient rules stand on.
    void aDisplayNameContainingACommaIsOneRecipient();
    void aQuotedDisplayNameSurvivesRoundTripping();
    void aMalformedHeaderYieldsNoRecipients();
    void anEmptyHeaderYieldsNoRecipients();
    void aGroupContributesNoRecipient();
    void aGroupIsDroppedByTheGuardAndNotByAFailedCast();
    void anInjectedHeaderLineIsNotCarriedForward();

    // Reply and reply-all recipient derivation.
    void aPlainReplyGoesToTheSenderOnly();
    void aReplyPrefersReplyToOverFrom();
    void aReplyAllPutsTheSenderInToAndTheRestInCc();
    void aReplyAllStripsEveryOwnAddress();
    void aReplyAllStripsAnOwnAddressRegardlessOfCase();
    void aReplyAllDoesNotListTheSenderTwice();
    void aReplyAllSuppressesDuplicatesAcrossToAndCc();
    void aReplyToOneselfStillAddressesSomeone();
    void aReplyToOwnMessageGoesToItsOriginalRecipients();
    void aReplyAllToOwnMessageDoesNotRepeatToInCc();
    void aCoSenderIsStillRepliedTo();
    void anUnparseableSenderStillProducesARecipient();
    void aReplyAllPrefersReplyToForTheToField();
    void aDisplayNameContainingAnOwnAddressIsNotMistakenForIt();

    // References.
    void referencesCarryTheOriginalChainPlusItsId();
    void referencesDoNotRepeatTheMessageId();
    void aCommaSeparatedReferencesHeaderIsSplitIntoIds();

    // Subjects.
    void aReplySubjectDoesNotDoubleItsPrefix();
    void aForwardSubjectDoesNotDoubleItsPrefix();
    void anEmptySubjectStillGetsAPrefix();
    void aSubjectMentioningReLaterStillGetsAPrefix();
    void aNonEnglishPrefixIsNotDoubled();
    void aCountedPrefixIsNotDoubled();
    void aSingleLetterBeforeAColonIsNotAPrefix();

    // Account resolution.
    void aReceivedForwardIsRecognisedFromItsSubject();
    void configuredForwardPrefixesExtendTheBuiltInTable();
    void theReplyAccountComesFromTheMessagesMaildir();
    void anAccountIsNotMatchedByAPrefixOfItsMaildir();
    void anAmbiguousMessagePrefersTheMatchingRecipient();
    void anAmbiguousMessageWithNoMatchTakesTheFirst();
    void aNewMessagePrefersTheSelectedAccount();
    void aNewMessageFallsThroughASelectedAccountThatCannotSend();
    void aNewMessageUsesDefaultAccountFromAllAccounts();
    void aNewMessageUsesStartupAccountWhenNoDefaultIsSet();
    void aNewMessageFallsBackToTheFirstSendingAccount();
    void aNewMessageReturnsNothingWhenNoAccountCanSend();

    // Quoting.
    void aQuotedBodyPrefixesEveryLine();
    void anHtmlOnlyBodyIsQuotedAsText();

private:
    QString writeConfig(const QString &contents);

    QTemporaryDir m_dir;
};

QString TestComposeContext::writeConfig(const QString &contents)
{
    // A unique name per call: Config caches nothing, but reusing one path
    // across tests in one binary invites a stale read to look like a pass.
    static int counter = 0;
    const QString path =
        m_dir.filePath(QStringLiteral("qtmaildir%1.conf").arg(++counter));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return {};
    file.write(contents.toUtf8());
    file.close();
    return path;
}

// ---------------------------------------------------------------------------
// Own addresses
// ---------------------------------------------------------------------------

void TestComposeContext::everyOwnAddressIsCollected()
{
    // All five of the user's addresses. Missing one means they receive their
    // own reply, and with five accounts that is the likeliest bug here.
    const QString path = writeConfig(QStringLiteral(
        "[account.one]\nmaildir=one\ntrash=Trash\naddress=first@example.org\n"
        "[account.two]\nmaildir=two\ntrash=Trash\naddress=second@example.org\n"
        "[account.three]\nmaildir=three\ntrash=Trash\naddress=third@example.org\n"
        "[account.four]\nmaildir=four\ntrash=Trash\naddress=fourth@example.org\n"
        "[account.five]\nmaildir=five\ntrash=Trash\naddress=fifth@example.org\n"));
    QVERIFY(!path.isEmpty());

    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 5);

    const QStringList own = ComposeContextBuilder::ownAddresses(config);
    QCOMPARE(own.size(), 5);
    for (const QString &address : { QStringLiteral("first@example.org"),
                                    QStringLiteral("second@example.org"),
                                    QStringLiteral("third@example.org"),
                                    QStringLiteral("fourth@example.org"),
                                    QStringLiteral("fifth@example.org") }) {
        QVERIFY2(own.contains(address),
                 qPrintable(QStringLiteral("own address %1 was not collected").arg(address)));
    }
}

void TestComposeContext::aBlankOwnAddressIsNotCollected()
{
    // An account with no address key is legal. An empty string in this list
    // would match nothing usefully and, in a substring filter, everything.
    const QString path = writeConfig(QStringLiteral(
        "[account.one]\nmaildir=one\ntrash=Trash\naddress=first@example.org\n"
        "[account.noaddress]\nmaildir=two\ntrash=Trash\n"));
    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 2);

    const QStringList own = ComposeContextBuilder::ownAddresses(config);
    QCOMPARE(own, QStringList{ QStringLiteral("first@example.org") });
}

// ---------------------------------------------------------------------------
// Header parsing
// ---------------------------------------------------------------------------

void TestComposeContext::aDisplayNameContainingACommaIsOneRecipient()
{
    // The single most likely parsing bug: splitting on commas turns one
    // recipient into two, one of which ("Rossi") is not an address at all and
    // would be handed to the send command.
    const QList<Recipient> parsed = ComposeContextBuilder::parseAddressHeader(
        QStringLiteral("\"Rossi, Mario\" <m@example.org>, info@example.net"));

    QCOMPARE(parsed.size(), 2);
    QCOMPARE(parsed.at(0).address, QStringLiteral("m@example.org"));
    QCOMPARE(parsed.at(1).address, QStringLiteral("info@example.net"));
}

void TestComposeContext::aQuotedDisplayNameSurvivesRoundTripping()
{
    // A comma in a display name must come back out QUOTED. Unquoted, the
    // rendered form is not a legal single address: it happens to survive
    // GMime's own lenient re-parse, but it goes into a To: header that other
    // clients and MTAs read, and a bare comma there is a recipient separator.
    //
    // Asserted on the RENDERED TEXT rather than on a re-parse, and that is the
    // point of the test: a round-trip through parseAddressHeader() passes
    // against string-assembled "Rossi, Mario <m@example.org>" because GMime
    // reads it back as one address anyway. Measured 2026-08-21, a mutation
    // replacing the GMime rendering with `name + " <" + addr + ">"` left the
    // whole suite green until this assertion was written this way.
    const QList<Recipient> parsed = ComposeContextBuilder::parseAddressHeader(
        QStringLiteral("\"Rossi, Mario\" <m@example.org>"));
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.at(0).rendered,
             QStringLiteral("\"Rossi, Mario\" <m@example.org>"));

    // And it still re-parses to the same one address.
    const QList<Recipient> again =
        ComposeContextBuilder::parseAddressHeader(parsed.at(0).rendered);
    QCOMPARE(again.size(), 1);
    QCOMPARE(again.at(0).address, QStringLiteral("m@example.org"));
}

void TestComposeContext::aMalformedHeaderYieldsNoRecipients()
{
    // GMime returns NULL rather than an empty list for input it can make
    // nothing of. Measured 2026-08-21: "not an address at all" and "<<<>>>"
    // both return NULL.
    QVERIFY(ComposeContextBuilder::parseAddressHeader(
                QStringLiteral("not an address at all")).isEmpty());
    QVERIFY(ComposeContextBuilder::parseAddressHeader(
                QStringLiteral("<<<>>>")).isEmpty());
}

void TestComposeContext::anEmptyHeaderYieldsNoRecipients()
{
    QVERIFY(ComposeContextBuilder::parseAddressHeader(QString()).isEmpty());
    QVERIFY(ComposeContextBuilder::parseAddressHeader(
                QStringLiteral("   ")).isEmpty());
}

void TestComposeContext::aGroupContributesNoRecipient()
{
    // A group has a name and no mailbox. Carrying its name forward would put
    // "undisclosed-recipients" in a To field as though it were a person.
    const QList<Recipient> parsed = ComposeContextBuilder::parseAddressHeader(
        QStringLiteral("undisclosed-recipients:;"));
    QVERIFY2(parsed.isEmpty(),
             qPrintable(QStringLiteral("a group produced %1 recipient(s)")
                            .arg(parsed.size())));
}

void TestComposeContext::aGroupIsDroppedByTheGuardAndNotByAFailedCast()
{
    // The count alone cannot see this, which is why the guard survived a
    // mutation until 2026-08-21. Removing the INTERNET_ADDRESS_IS_MAILBOX check
    // still yields no recipients, because the invalid cast makes GMime's own
    // assertion return NULL and the address is skipped one line later. The
    // count is therefore right for the wrong reason, and the reason matters: an
    // invalid GObject cast is undefined behaviour papered over by an assertion
    // that G_DISABLE_CHECKS compiles out and that G_DEBUG=fatal-criticals turns
    // into an abort. A security property must not rest on assertions staying
    // enabled.
    //
    // So this asserts on the CRITICAL rather than on the count. glib routes it
    // through the log handler installed here, and a clean parse emits none.
    struct Captured
    {
        static void handler(const gchar *domain, GLogLevelFlags level,
                            const gchar *messageText, gpointer userData)
        {
            Q_UNUSED(domain);
            Q_UNUSED(level);
            auto *messages = static_cast<QStringList *>(userData);
            messages->append(QString::fromUtf8(messageText));
        }
    };

    // Registered per DOMAIN, and the domain is the trap: the two criticals this
    // watches for carry "GLib-GObject" and "gmime", while a NULL domain
    // registers only for the default one. A handler on nullptr alone catches
    // NOTHING here and the test passes against the mutation, measured
    // 2026-08-21.
    QStringList criticals;
    const auto levels = GLogLevelFlags(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING
                                       | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION);
    QList<guint> handlerIds;
    for (const char *domain : { "GLib-GObject", "gmime" })
        handlerIds.append(g_log_set_handler(domain, levels, &Captured::handler, &criticals));

    // A group carrying MEMBERS, not the empty "undisclosed-recipients:;". The
    // empty form has nothing to cast, so it cannot tell the two paths apart.
    const QList<Recipient> parsed = ComposeContextBuilder::parseAddressHeader(
        QStringLiteral("friends: a@example.org, b@example.net;"));

    int i = 0;
    for (const char *domain : { "GLib-GObject", "gmime" })
        g_log_remove_handler(domain, handlerIds.at(i++));

    QVERIFY2(parsed.isEmpty(),
             qPrintable(QStringLiteral("a group with members produced %1 recipient(s)")
                            .arg(parsed.size())));
    QVERIFY2(criticals.isEmpty(),
             qPrintable(QStringLiteral("GMime emitted %1 during the parse: %2")
                            .arg(criticals.size())
                            .arg(criticals.join(QLatin1Char('|')))));
}

void TestComposeContext::anInjectedHeaderLineIsNotCarriedForward()
{
    // Header injection, from a stranger's message into the user's reply.
    // Measured 2026-08-21: GMime parses the smuggled line as a GROUP named
    // "Bcc", so dropping non-mailboxes drops it. If groups were kept, a reply
    // would silently pre-fill a recipient the user never saw.
    const QList<Recipient> parsed = ComposeContextBuilder::parseAddressHeader(
        QStringLiteral("a@example.org\nBcc: evil@example.net"));

    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.at(0).address, QStringLiteral("a@example.org"));
    for (const Recipient &recipient : parsed) {
        QVERIFY2(!recipient.rendered.contains(QStringLiteral("evil@example.net")),
                 qPrintable(QStringLiteral("injected address survived in: %1")
                                .arg(recipient.rendered)));
    }

    // The other injection shape: the newline hidden INSIDE a quoted display
    // name, where it does not split the header and so is not dropped as a
    // group. It has to come back RFC 2047 encoded, never as a raw newline: a
    // bare CR or LF in a rendered recipient is a header-injection primitive
    // the moment anything writes it into a To: line. Rendering by hand rather
    // than through GMime is what loses the encoding.
    const QList<Recipient> inName = ComposeContextBuilder::parseAddressHeader(
        QStringLiteral("\"foo\nBcc: evil@example.net\" <a@example.org>"));
    QCOMPARE(inName.size(), 1);
    QCOMPARE(inName.at(0).address, QStringLiteral("a@example.org"));
    QVERIFY2(!inName.at(0).rendered.contains(QLatin1Char('\n'))
                 && !inName.at(0).rendered.contains(QLatin1Char('\r')),
             qPrintable(QStringLiteral("a raw newline survived into a rendered "
                                       "recipient: %1")
                            .arg(inName.at(0).rendered)));
}

// ---------------------------------------------------------------------------
// Reply and reply-all
// ---------------------------------------------------------------------------

void TestComposeContext::aPlainReplyGoesToTheSenderOnly()
{
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.to = QStringLiteral("me@example.org, other@example.net");
    message.cc = QStringLiteral("third@example.com");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/false, { QStringLiteral("me@example.org") }, &to, &cc);

    QCOMPARE(to.size(), 1);
    QVERIFY2(to.at(0).contains(QStringLiteral("sender@example.org")),
             qPrintable(QStringLiteral("To was %1").arg(to.join(QLatin1Char('|')))));
    QVERIFY2(cc.isEmpty(),
             qPrintable(QStringLiteral("a plain reply put %1 in Cc")
                            .arg(cc.join(QLatin1Char('|')))));
}

void TestComposeContext::aReplyPrefersReplyToOverFrom()
{
    // RFC 5322 3.6.2: Reply-To names where the author wants replies sent. This
    // is what makes a list reply land on the list rather than on a person who
    // never asked to be written to directly.
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.replyTo = QStringLiteral("List <list@example.net>");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(message, /*replyAll=*/false, {}, &to, &cc);

    QCOMPARE(to.size(), 1);
    QVERIFY2(to.at(0).contains(QStringLiteral("list@example.net")),
             qPrintable(QStringLiteral("To was %1, expected the Reply-To")
                            .arg(to.join(QLatin1Char('|')))));
    QVERIFY2(!to.at(0).contains(QStringLiteral("sender@example.org")),
             "From was used despite a Reply-To being present");
}

void TestComposeContext::aReplyAllPutsTheSenderInToAndTheRestInCc()
{
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.to = QStringLiteral("first@example.net");
    message.cc = QStringLiteral("second@example.com");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(message, /*replyAll=*/true, {}, &to, &cc);

    QCOMPARE(to.size(), 1);
    QVERIFY(to.at(0).contains(QStringLiteral("sender@example.org")));

    QCOMPARE(cc.size(), 2);
    QVERIFY2(cc.join(QLatin1Char('|')).contains(QStringLiteral("first@example.net")),
             qPrintable(QStringLiteral("Cc was %1").arg(cc.join(QLatin1Char('|')))));
    QVERIFY2(cc.join(QLatin1Char('|')).contains(QStringLiteral("second@example.com")),
             qPrintable(QStringLiteral("Cc was %1").arg(cc.join(QLatin1Char('|')))));
}

void TestComposeContext::aReplyAllStripsEveryOwnAddress()
{
    // Five accounts, and the user's address appears in the original's To under
    // THREE of them. Stripping only the first is the exact failure this guards:
    // the reply-all would then be addressed to the user twice over.
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.to = QStringLiteral(
        "first@example.org, stranger@example.net, third@example.org");
    message.cc = QStringLiteral("fifth@example.org, another@example.com");

    const QStringList own = { QStringLiteral("first@example.org"),
                              QStringLiteral("second@example.org"),
                              QStringLiteral("third@example.org"),
                              QStringLiteral("fourth@example.org"),
                              QStringLiteral("fifth@example.org") };

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(message, /*replyAll=*/true, own, &to, &cc);

    const QString all = (to + cc).join(QLatin1Char('|'));
    for (const QString &address : own) {
        QVERIFY2(!all.contains(address, Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("own address %1 survived in: %2")
                                .arg(address, all)));
    }
    // And the strangers must all still be there: a filter that removed
    // everything would pass the check above while producing an unsendable reply.
    QVERIFY2(all.contains(QStringLiteral("stranger@example.net")),
             qPrintable(QStringLiteral("a stranger was stripped too: %1").arg(all)));
    QVERIFY2(all.contains(QStringLiteral("another@example.com")),
             qPrintable(QStringLiteral("a stranger was stripped too: %1").arg(all)));
    QVERIFY2(all.contains(QStringLiteral("sender@example.org")),
             qPrintable(QStringLiteral("the sender was stripped: %1").arg(all)));
}

void TestComposeContext::aReplyAllStripsAnOwnAddressRegardlessOfCase()
{
    // A domain is case-insensitive by RFC and real mail varies the local part's
    // case too. A case-sensitive filter lets the user's own address through and
    // they receive their own reply.
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.to = QStringLiteral("Me@Example.ORG, stranger@example.net");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/true, { QStringLiteral("me@example.org") }, &to, &cc);

    const QString all = (to + cc).join(QLatin1Char('|'));
    QVERIFY2(!all.contains(QStringLiteral("Me@Example.ORG"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a differently-cased own address survived: %1")
                            .arg(all)));
    QVERIFY(all.contains(QStringLiteral("stranger@example.net")));
}

void TestComposeContext::aReplyAllDoesNotListTheSenderTwice()
{
    // The sender is very often also in their own message's To (a list posting
    // reflected back). Without cross-field suppression they appear in To AND Cc.
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.to = QStringLiteral("sender@example.org, stranger@example.net");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(message, /*replyAll=*/true, {}, &to, &cc);

    const QString all = (to + cc).join(QLatin1Char('|'));
    QCOMPARE(all.count(QStringLiteral("sender@example.org")), 1);
    QVERIFY(all.contains(QStringLiteral("stranger@example.net")));
}

void TestComposeContext::aReplyAllSuppressesDuplicatesAcrossToAndCc()
{
    // The same address in the original's To and Cc, with different display
    // names so a whole-string comparison would treat them as distinct.
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.to = QStringLiteral("Person One <dup@example.net>");
    message.cc = QStringLiteral("P. One <dup@example.net>, other@example.com");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(message, /*replyAll=*/true, {}, &to, &cc);

    const QString all = (to + cc).join(QLatin1Char('|'));
    QCOMPARE(all.count(QStringLiteral("dup@example.net")), 1);
    QVERIFY(all.contains(QStringLiteral("other@example.com")));
}

void TestComposeContext::aReplyToOneselfStillAddressesSomeone()
{
    // Replying to a message the user sent themselves. Stripping own addresses
    // from a plain Reply's To would leave a message with no recipient that
    // still looks sendable.
    ParsedMessage message;
    message.from = QStringLiteral("Me <me@example.org>");
    message.to = QStringLiteral("me@example.org");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/false, { QStringLiteral("me@example.org") }, &to, &cc);

    QVERIFY2(!to.isEmpty(), "a reply to oneself produced no recipient at all");
    QVERIFY(to.join(QLatin1Char('|')).contains(QStringLiteral("me@example.org")));
}

void TestComposeContext::aReplyToOwnMessageGoesToItsOriginalRecipients()
{
    // The Sent view, and a follow-up on unanswered mail: the user replies to a
    // message they sent. Addressing the sender there addresses the user, so To
    // comes from the original's own recipients instead. The Cc entry is
    // included because a reply to a conversation the user started belongs to
    // everyone who was on it.
    ParsedMessage message;
    message.from = QStringLiteral("Me <me@example.org>");
    message.to = QStringLiteral("Correspondent <them@example.net>");
    message.cc = QStringLiteral("watcher@example.com");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/false, { QStringLiteral("me@example.org") }, &to, &cc);

    const QString joined = to.join(QLatin1Char('|'));
    QVERIFY2(joined.contains(QStringLiteral("them@example.net")),
             qPrintable(QStringLiteral("To was %1").arg(joined)));
    QVERIFY2(joined.contains(QStringLiteral("watcher@example.com")),
             qPrintable(QStringLiteral("To was %1").arg(joined)));
    // The whole point: the user is not written back to themselves.
    QVERIFY2(!joined.contains(QStringLiteral("me@example.org")),
             qPrintable(QStringLiteral("the reply addressed the user: %1").arg(joined)));
    QVERIFY2(cc.isEmpty(), "a plain reply produced a Cc");
}

void TestComposeContext::aReplyAllToOwnMessageDoesNotRepeatToInCc()
{
    // Reply-all to your own message MIRRORS the original's split: its To
    // becomes To, its Cc becomes Cc. The split is the message's meaning, To
    // being "addressed to you" and Cc "for information", and promoting a Cc'd
    // party to To is visible to every recipient.
    //
    // Asserted per FIELD, not on the union. A test counting each address once
    // across to + cc passes whether the split is preserved or collapsed, which
    // is how the collapse shipped and survived its first mutation check.
    ParsedMessage message;
    message.from = QStringLiteral("Me <me@example.org>");
    message.to = QStringLiteral("them@example.net");
    message.cc = QStringLiteral("watcher@example.com");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/true, { QStringLiteral("me@example.org") }, &to, &cc);

    const QString toJoined = to.join(QLatin1Char('|'));
    const QString ccJoined = cc.join(QLatin1Char('|'));
    QVERIFY2(toJoined.contains(QStringLiteral("them@example.net")),
             qPrintable(QStringLiteral("To was %1").arg(toJoined)));
    QVERIFY2(!toJoined.contains(QStringLiteral("watcher@example.com")),
             qPrintable(QStringLiteral("a Cc recipient was promoted to To: %1").arg(toJoined)));
    QVERIFY2(ccJoined.contains(QStringLiteral("watcher@example.com")),
             qPrintable(QStringLiteral("Cc was %1").arg(ccJoined)));
    QVERIFY2(!ccJoined.contains(QStringLiteral("them@example.net")),
             qPrintable(QStringLiteral("the To address repeated in Cc: %1").arg(ccJoined)));
    // The whole point of the self-reply rule.
    QVERIFY2(!(toJoined + ccJoined).contains(QStringLiteral("me@example.org")),
             "the reply addressed the user");
}

void TestComposeContext::aCoSenderIsStillRepliedTo()
{
    // A message the user sent WITH somebody else is not a message to oneself.
    // Only an all-own sender diverts To to the original recipients; here the
    // co-sender is a real person expecting the reply.
    ParsedMessage message;
    message.from = QStringLiteral("Me <me@example.org>, Other <other@example.net>");
    message.to = QStringLiteral("them@example.com");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/false, { QStringLiteral("me@example.org") }, &to, &cc);

    const QString joined = to.join(QLatin1Char('|'));
    QVERIFY2(joined.contains(QStringLiteral("other@example.net")),
             qPrintable(QStringLiteral("To was %1").arg(joined)));
    QVERIFY2(!joined.contains(QStringLiteral("them@example.com")),
             qPrintable(QStringLiteral("a plain reply reached the original's To: %1")
                            .arg(joined)));
}

void TestComposeContext::anUnparseableSenderStillProducesARecipient()
{
    // "From: Mailer Daemon" is a bare display name with no angle brackets, which
    // is what bounces and some automated senders emit. It parses to ZERO
    // mailboxes, so the sender contributes nothing and To would otherwise come
    // out empty.
    //
    // An empty To is the worst outcome available here, because MessageBuilder
    // treats an empty recipient list as success: the message reaches the send
    // command with nobody to deliver to and a copy is filed in Sent that looks
    // sent and reached no one. The original's own recipients are the remaining
    // candidates.
    ParsedMessage message;
    message.from = QStringLiteral("Mailer Daemon");
    message.to = QStringLiteral("them@example.net");
    message.cc = QStringLiteral("watcher@example.com");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/false, { QStringLiteral("me@example.org") }, &to, &cc);

    QVERIFY2(!to.isEmpty(), "an unparseable sender produced a reply with no recipient");
    QVERIFY2(to.join(QLatin1Char('|')).contains(QStringLiteral("them@example.net")),
             qPrintable(QStringLiteral("To was %1").arg(to.join(QLatin1Char('|')))));

    // Reply-all is the worse half: without the fallback it puts every recipient
    // in Cc and leaves To empty, which is a message addressed to nobody.
    QStringList allTo;
    QStringList allCc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/true, { QStringLiteral("me@example.org") }, &allTo, &allCc);
    QVERIFY2(!allTo.isEmpty(), "a reply-all to an unparseable sender left To empty");
}

void TestComposeContext::aReplyAllPrefersReplyToForTheToField()
{
    // Reply-To precedence is not a plain-Reply-only rule: a list's reply-all
    // must also go to the list rather than to the individual poster.
    ParsedMessage message;
    message.from = QStringLiteral("Poster <poster@example.org>");
    message.replyTo = QStringLiteral("List <list@example.net>");
    message.to = QStringLiteral("list@example.net");
    message.cc = QStringLiteral("watcher@example.com");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(message, /*replyAll=*/true, {}, &to, &cc);

    QCOMPARE(to.size(), 1);
    QVERIFY2(to.at(0).contains(QStringLiteral("list@example.net")),
             qPrintable(QStringLiteral("To was %1").arg(to.join(QLatin1Char('|')))));
    // The list is in To, so it must not repeat in Cc even though the original's
    // To named it.
    QVERIFY2(!cc.join(QLatin1Char('|')).contains(QStringLiteral("list@example.net")),
             qPrintable(QStringLiteral("the To address repeated in Cc: %1")
                            .arg(cc.join(QLatin1Char('|')))));
    QVERIFY(cc.join(QLatin1Char('|')).contains(QStringLiteral("watcher@example.com")));
}

void TestComposeContext::aDisplayNameContainingAnOwnAddressIsNotMistakenForIt()
{
    // A stranger whose DISPLAY NAME quotes the user's address. Comparing the
    // rendered whole rather than the addr-spec would strip a real recipient,
    // and the reply would silently not reach them.
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.to = QStringLiteral("\"about me@example.org\" <stranger@example.net>");

    QStringList to;
    QStringList cc;
    ComposeContextBuilder::recipientsForReply(
        message, /*replyAll=*/true, { QStringLiteral("me@example.org") }, &to, &cc);

    QVERIFY2((to + cc).join(QLatin1Char('|')).contains(QStringLiteral("stranger@example.net")),
             "a stranger was stripped because their display name quoted an own address");
}

// ---------------------------------------------------------------------------
// References
// ---------------------------------------------------------------------------

void TestComposeContext::referencesCarryTheOriginalChainPlusItsId()
{
    ParsedMessage message;
    message.messageId = QStringLiteral("current@example.org");
    message.references =
        QStringLiteral("<first@example.org> <second@example.org>");

    const QStringList refs = ComposeContextBuilder::referencesForReply(message);

    QCOMPARE(refs.size(), 3);
    QCOMPARE(refs.at(0), QStringLiteral("first@example.org"));
    QCOMPARE(refs.at(1), QStringLiteral("second@example.org"));
    QCOMPARE(refs.at(2), QStringLiteral("current@example.org"));
}

void TestComposeContext::referencesDoNotRepeatTheMessageId()
{
    ParsedMessage message;
    message.messageId = QStringLiteral("current@example.org");
    message.references = QStringLiteral("<first@example.org> <current@example.org>");

    const QStringList refs = ComposeContextBuilder::referencesForReply(message);

    QCOMPARE(refs.count(QStringLiteral("current@example.org")), 1);
    QCOMPARE(refs.last(), QStringLiteral("current@example.org"));
}

// ---------------------------------------------------------------------------
// Subjects
// ---------------------------------------------------------------------------

void TestComposeContext::aCommaSeparatedReferencesHeaderIsSplitIntoIds()
{
    // `<a@x>,<b@y>` is not conformant, RFC 5322 has no comma here, but some
    // clients emit it. Splitting on whitespace alone makes that whole header ONE
    // token, and stripping its outer brackets then yields the fabricated id
    // `a@x>,<b@y`, which is sent to the recipient as a Message-ID reference. A
    // comma cannot occur inside a msg-id, so accepting it as a separator is free.
    ParsedMessage message;
    message.references = QStringLiteral("<first@example.org>,<second@example.org>");
    message.messageId = QStringLiteral("current@example.org");

    const QStringList refs = ComposeContextBuilder::referencesForReply(message);

    QCOMPARE(refs.size(), 3);
    QCOMPARE(refs.at(0), QStringLiteral("first@example.org"));
    QCOMPARE(refs.at(1), QStringLiteral("second@example.org"));
    QCOMPARE(refs.at(2), QStringLiteral("current@example.org"));
}

void TestComposeContext::aReplySubjectDoesNotDoubleItsPrefix()
{
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("Hello")),
             QStringLiteral("Re: Hello"));
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("Re: Hello")),
             QStringLiteral("Re: Hello"));
    // Case and spacing vary between clients and neither justifies a second
    // prefix. "RE:" from Outlook is the common one.
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("RE: Hello")),
             QStringLiteral("RE: Hello"));
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("re:Hello")),
             QStringLiteral("re:Hello"));
}

void TestComposeContext::aForwardSubjectDoesNotDoubleItsPrefix()
{
    QCOMPARE(ComposeContextBuilder::forwardSubject(QStringLiteral("Hello")),
             QStringLiteral("Fwd: Hello"));
    QCOMPARE(ComposeContextBuilder::forwardSubject(QStringLiteral("Fwd: Hello")),
             QStringLiteral("Fwd: Hello"));
    // "Fw:" is the other common spelling and means the same thing.
    QCOMPARE(ComposeContextBuilder::forwardSubject(QStringLiteral("Fw: Hello")),
             QStringLiteral("Fw: Hello"));
}

void TestComposeContext::anEmptySubjectStillGetsAPrefix()
{
    // A reply to a subjectless message is still a reply. "Re: " alone is
    // correct and is what every other client produces.
    QCOMPARE(ComposeContextBuilder::replySubject(QString()),
             QStringLiteral("Re: "));
}

void TestComposeContext::aSubjectMentioningReLaterStillGetsAPrefix()
{
    // The prefix test is ANCHORED. An unanchored search would see "re:" inside
    // an ordinary subject and refuse to prefix a genuine first reply, which
    // breaks threading in the recipient's client.
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("Notes re: budget")),
             QStringLiteral("Re: Notes re: budget"));
    QCOMPARE(ComposeContextBuilder::forwardSubject(QStringLiteral("Notes fwd: budget")),
             QStringLiteral("Fwd: Notes fwd: budget"));
}

void TestComposeContext::aNonEnglishPrefixIsNotDoubled()
{
    // A mixed-locale mailbox, which this one is. An English-only pattern turns
    // every one of these into "Re: AW: subject", and the round after that into
    // "Re: Re: AW:".
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("AW: Angebot")),
             QStringLiteral("AW: Angebot"));
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("SV: innkalling")),
             QStringLiteral("SV: innkalling"));
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("RES: pedido")),
             QStringLiteral("RES: pedido"));
    QCOMPARE(ComposeContextBuilder::forwardSubject(QStringLiteral("WG: Angebot")),
             QStringLiteral("WG: Angebot"));
    QCOMPARE(ComposeContextBuilder::forwardSubject(QStringLiteral("TR: document")),
             QStringLiteral("TR: document"));
}

void TestComposeContext::aCountedPrefixIsNotDoubled()
{
    // Outlook and some list managers count the rounds. Same meaning, and
    // prefixing again produces "Re: Re[2]:".
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("Re[2]: thread")),
             QStringLiteral("Re[2]: thread"));
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("Re(3): thread")),
             QStringLiteral("Re(3): thread"));
}

void TestComposeContext::aSingleLetterBeforeAColonIsNotAPrefix()
{
    // Italian clients do send "R:" and "I:", and they are deliberately NOT
    // recognised. Measured 2026-08-21: with them in the pattern, "R: report on
    // Q3" reads as an existing prefix, so a genuine FIRST reply gets no "Re:"
    // and threads nowhere in the recipient's client, with nothing wrong to see
    // locally. A doubled "Re: R:" is cosmetic; broken threading is not.
    //
    // "F:" is here for the same reason: the pattern was once `fwd?`, which
    // matched it.
    QCOMPARE(ComposeContextBuilder::replySubject(QStringLiteral("R: report on Q3")),
             QStringLiteral("Re: R: report on Q3"));
    QCOMPARE(ComposeContextBuilder::forwardSubject(QStringLiteral("I: notes")),
             QStringLiteral("Fwd: I: notes"));
    QCOMPARE(ComposeContextBuilder::forwardSubject(QStringLiteral("F: results")),
             QStringLiteral("Fwd: F: results"));
}

void TestComposeContext::aReceivedForwardIsRecognisedFromItsSubject()
{
    using ComposeContextBuilder::subjectIsForwarded;

    // Item 68. The display predicate behind the received-forward mark. It
    // shares forwardSubject()'s prefix table deliberately, so the two cannot
    // disagree about what a forward looks like.
    QVERIFY(subjectIsForwarded(QStringLiteral("Fwd: budget")));
    QVERIFY(subjectIsForwarded(QStringLiteral("Fw: budget")));
    QVERIFY(subjectIsForwarded(QStringLiteral("FWD: budget")));
    QVERIFY(subjectIsForwarded(QStringLiteral("WG: Angebot")));
    QVERIFY(subjectIsForwarded(QStringLiteral("TR: document")));

    // Anchored. "Fwd:" inside a subject is a quotation, not a marker, and the
    // whole reason item 68's entry insisted on anchoring.
    QVERIFY(!subjectIsForwarded(QStringLiteral("Notes fwd: budget")));
    QVERIFY(!subjectIsForwarded(QStringLiteral("budget")));
    QVERIFY(!subjectIsForwarded(QString()));

    // The single-letter spellings stay unrecognised here for exactly the
    // reason forwardSubject() rejects them: "I: notes" is an ordinary subject.
    QVERIFY(!subjectIsForwarded(QStringLiteral("I: notes")));
    QVERIFY(!subjectIsForwarded(QStringLiteral("F: results")));

    // A reply to a forward is still a forward the user received, so the Re:
    // chain is stripped first. Both orders, and a counted Outlook form.
    QVERIFY(subjectIsForwarded(QStringLiteral("Re: Fwd: budget")));
    QVERIFY(subjectIsForwarded(QStringLiteral("Re: Re: Fwd: budget")));
    QVERIFY(subjectIsForwarded(QStringLiteral("Re[2]: Fwd: budget")));
    QVERIFY(subjectIsForwarded(QStringLiteral("AW: WG: Angebot")));

    // A plain reply is not a forward, however deep the chain.
    QVERIFY(!subjectIsForwarded(QStringLiteral("Re: budget")));
    QVERIFY(!subjectIsForwarded(QStringLiteral("Re: Re: Re: budget")));
}

void TestComposeContext::configuredForwardPrefixesExtendTheBuiltInTable()
{
    using ComposeContextBuilder::subjectIsForwarded;

    // Item 68. [general] forward_prefixes ADDS to the table rather than
    // replacing it: a user adding Dutch must not lose English.
    const QStringList dutch = { QStringLiteral("Doorst") };
    QVERIFY(subjectIsForwarded(QStringLiteral("Doorst: begroting"), dutch));
    QVERIFY(subjectIsForwarded(QStringLiteral("Fwd: budget"), dutch));

    // Case-insensitive and counted forms, like the built-ins.
    QVERIFY(subjectIsForwarded(QStringLiteral("DOORST: begroting"), dutch));
    QVERIFY(subjectIsForwarded(QStringLiteral("Doorst[2]: begroting"), dutch));
    QVERIFY(subjectIsForwarded(QStringLiteral("Re: Doorst: begroting"), dutch));

    // An unconfigured spelling stays unrecognised, which is what makes the
    // key worth having rather than the predicate matching anything.
    QVERIFY(!subjectIsForwarded(QStringLiteral("Doorst: begroting")));

    // Non-word entries are ignored per entry. Measured 2026-08-26: escaping
    // alone already makes punctuation inert, so what the guard actually buys
    // is that a configured "-" does not make "-: x" a forward, and a digit
    // does not make "2: x" one. Neither is a marker any client emits.
    QVERIFY(!subjectIsForwarded(QStringLiteral("-: x"),
                                { QStringLiteral("-") }));
    QVERIFY(!subjectIsForwarded(QStringLiteral("2: x"),
                                { QStringLiteral("2") }));

    // An empty or blank entry contributes nothing rather than matching
    // everything, which is the failure that would be silent and total.
    const QStringList blank = { QString(), QStringLiteral("   ") };
    QVERIFY(!subjectIsForwarded(QStringLiteral("budget"), blank));
    QVERIFY(!subjectIsForwarded(QStringLiteral("anything at all"), blank));

    // A configured "Re" must not turn every reply into a forward.
    QVERIFY(!subjectIsForwarded(QStringLiteral("Re: budget"),
                                { QStringLiteral("Re") }));
}

// ---------------------------------------------------------------------------
// Account resolution
// ---------------------------------------------------------------------------

void TestComposeContext::theReplyAccountComesFromTheMessagesMaildir()
{
    // The dropdown is NOT consulted: replying from the All accounts view to a
    // message that arrived at account B sends from B.
    const QString path = writeConfig(QStringLiteral(
        "[account.work]\nmaildir=work\ntrash=Trash\naddress=work@example.org\n"
        "send_command=/bin/true\n"
        "[account.home]\nmaildir=home\ntrash=Trash\naddress=home@example.org\n"
        "send_command=/bin/true\n"));
    QVERIFY(!path.isEmpty());

    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 2);

    const QString account = ComposeContextBuilder::accountForReply(
        config, { QStringLiteral("/mail/home/INBOX/cur/123") },
        { QStringLiteral("home@example.org") }, QStringLiteral("/mail"));

    QCOMPARE(account, QStringLiteral("home"));
}

void TestComposeContext::anAccountIsNotMatchedByAPrefixOfItsMaildir()
{
    // "work" must not claim a message living in "work-archive". Without the
    // separator in the comparison it does, and the reply is sent from the
    // wrong account.
    //
    // The account KEYS are chosen so the wrong answer is reached FIRST.
    // Config builds its list from QSettings::childGroups(), which returns
    // groups ALPHABETICALLY rather than in file order, so the section order
    // here decides nothing and only the keys do. With "archive" before "work"
    // the loop happens upon the correct account before it can mismatch, and
    // the test passes against the bug: measured, a mutation dropping the
    // separator left the suite fully green. "a-work" (maildir "work") sorts
    // before "b-archive" (maildir "work-archive") and puts the prefix
    // candidate first, where a textual comparison matches it.
    const QString path = writeConfig(QStringLiteral(
        "[account.a-work]\nmaildir=work\ntrash=Trash\naddress=work@example.org\n"
        "send_command=/bin/true\n"
        "[account.b-archive]\nmaildir=work-archive\ntrash=Trash\n"
        "address=archive@example.org\nsend_command=/bin/true\n"));
    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 2);
    // The ordering the mutation depends on, asserted rather than assumed: if
    // Config ever sorts differently this test silently stops testing anything.
    QCOMPARE(config.accounts().at(0).key, QStringLiteral("a-work"));

    const QString account = ComposeContextBuilder::accountForReply(
        config, { QStringLiteral("/mail/work-archive/INBOX/cur/1") },
        {}, QStringLiteral("/mail"));

    QCOMPARE(account, QStringLiteral("b-archive"));
}

void TestComposeContext::anAmbiguousMessagePrefersTheMatchingRecipient()
{
    // One message, two maildirs: on a list twice under two addresses. The
    // recipient headers are the tiebreak.
    const QString path = writeConfig(QStringLiteral(
        "[account.work]\nmaildir=work\ntrash=Trash\naddress=work@example.org\n"
        "send_command=/bin/true\n"
        "[account.home]\nmaildir=home\ntrash=Trash\naddress=home@example.org\n"
        "send_command=/bin/true\n"));
    QVERIFY(!path.isEmpty());

    Config config;
    config.load(path);

    const QString account = ComposeContextBuilder::accountForReply(
        config,
        { QStringLiteral("/mail/work/Lists/cur/1"),
          QStringLiteral("/mail/home/Lists/cur/1") },
        { QStringLiteral("home@example.org") }, QStringLiteral("/mail"));

    QCOMPARE(account, QStringLiteral("home"));
}

void TestComposeContext::anAmbiguousMessageWithNoMatchTakesTheFirst()
{
    // Arbitrary, and deliberately so: the From field shows the choice, which
    // makes an arbitrary resolution visible rather than hidden.
    const QString path = writeConfig(QStringLiteral(
        "[account.work]\nmaildir=work\ntrash=Trash\naddress=work@example.org\n"
        "send_command=/bin/true\n"
        "[account.home]\nmaildir=home\ntrash=Trash\naddress=home@example.org\n"
        "send_command=/bin/true\n"));
    Config config;
    config.load(path);

    const QString account = ComposeContextBuilder::accountForReply(
        config,
        { QStringLiteral("/mail/work/Lists/cur/1"),
          QStringLiteral("/mail/home/Lists/cur/1") },
        { QStringLiteral("someone-else@example.org") }, QStringLiteral("/mail"));

    QVERIFY2(!account.isEmpty(), "an ambiguous message resolved to no account");
    QCOMPARE(account, QStringLiteral("work"));
}

void TestComposeContext::aNewMessagePrefersTheSelectedAccount()
{
    const QString path = writeConfig(QStringLiteral(
        "[account.work]\nmaildir=work\ntrash=Trash\nsend_command=/bin/true\n"
        "[account.home]\nmaildir=home\ntrash=Trash\nsend_command=/bin/true\n"));
    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 2);

    QCOMPARE(ComposeContextBuilder::accountForNew(config, QStringLiteral("home")),
             QStringLiteral("home"));
}

void TestComposeContext::aNewMessageFallsThroughASelectedAccountThatCannotSend()
{
    // Rule 1 requires the selected account CAN send. Viewing a receive-only
    // account and pressing compose must produce a working composer from
    // another account, not a broken one from this.
    const QString path = writeConfig(QStringLiteral(
        "[account.listsonly]\nmaildir=listsonly\ntrash=Trash\n"
        "[account.work]\nmaildir=work\ntrash=Trash\nsend_command=/bin/true\n"));
    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 2);

    QCOMPARE(ComposeContextBuilder::accountForNew(config, QStringLiteral("listsonly")),
             QStringLiteral("work"));
}

void TestComposeContext::aNewMessageUsesDefaultAccountFromAllAccounts()
{
    // The All accounts view has no selected account and falls through to rule 2.
    //
    // The named account must NOT also be what rule 4 would answer, or the test
    // passes with rule 2 deleted outright: measured, a mutation removing it
    // left the suite green because the account list is ALPHABETICAL (Config
    // builds it from QSettings::childGroups()) and the section order in this
    // string decides nothing. "zeta" sorts last, so rule 4 would answer
    // "alpha" and only rule 2 can produce "zeta".
    const QString path = writeConfig(QStringLiteral(
        "[account.alpha]\nmaildir=alpha\ntrash=Trash\nsend_command=/bin/true\n"
        "[account.zeta]\nmaildir=zeta\ntrash=Trash\nsend_command=/bin/true\n"
        "[compose]\ndefault_account=zeta\n"));
    Config config;
    config.load(path);
    QCOMPARE(config.compose().defaultAccount, QStringLiteral("zeta"));
    // Asserted rather than assumed, so the test stops silently proving nothing
    // if Config ever changes its ordering.
    QCOMPARE(config.sendingAccounts().first().key, QStringLiteral("alpha"));

    QCOMPARE(ComposeContextBuilder::accountForNew(config, QString()),
             QStringLiteral("zeta"));
}

void TestComposeContext::aNewMessageUsesStartupAccountWhenNoDefaultIsSet()
{
    // Rule 3. Same ordering trap as rule 2: "zeta" must not be what rule 4
    // would answer, or a test for this rule passes with the rule deleted.
    const QString path = writeConfig(QStringLiteral(
        "[general]\nstartup_account=zeta\n"
        "[account.alpha]\nmaildir=alpha\ntrash=Trash\nsend_command=/bin/true\n"
        "[account.zeta]\nmaildir=zeta\ntrash=Trash\nsend_command=/bin/true\n"));
    Config config;
    config.load(path);
    QCOMPARE(config.startupAccount(), QStringLiteral("zeta"));
    QVERIFY(config.compose().defaultAccount.isEmpty());
    QCOMPARE(config.sendingAccounts().first().key, QStringLiteral("alpha"));

    QCOMPARE(ComposeContextBuilder::accountForNew(config, QString()),
             QStringLiteral("zeta"));
}

void TestComposeContext::aNewMessageFallsBackToTheFirstSendingAccount()
{
    // Rule 4, arbitrary, and the reason rules 2 and 3 exist. The receive-only
    // account is FIRST, so "the first account" and "the first sending account"
    // are different answers and the test distinguishes them.
    const QString path = writeConfig(QStringLiteral(
        "[account.listsonly]\nmaildir=listsonly\ntrash=Trash\n"
        "[account.work]\nmaildir=work\ntrash=Trash\nsend_command=/bin/true\n"));
    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 2);

    QCOMPARE(ComposeContextBuilder::accountForNew(config, QString()),
             QStringLiteral("work"));
}

void TestComposeContext::aNewMessageReturnsNothingWhenNoAccountCanSend()
{
    // A valid read-only installation. The compose action is disabled, so this
    // should be unreachable, and returning empty rather than a random account
    // is what makes a mistake visible instead of silent.
    const QString path = writeConfig(QStringLiteral(
        "[account.listsonly]\nmaildir=listsonly\ntrash=Trash\n"));
    Config config;
    config.load(path);
    QCOMPARE(config.accounts().size(), 1);

    QVERIFY(ComposeContextBuilder::accountForNew(config, QString()).isEmpty());
}

// ---------------------------------------------------------------------------
// Quoting
// ---------------------------------------------------------------------------

void TestComposeContext::aQuotedBodyPrefixesEveryLine()
{
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.date = QStringLiteral("Thu, 20 Aug 2026 10:00:00 +0200");
    message.plainBody = QStringLiteral("first line\nsecond line\n\nafter a blank");

    const QString quoted = ComposeContextBuilder::quoteBody(message);

    QVERIFY2(quoted.contains(QStringLiteral("> first line")),
             qPrintable(QStringLiteral("first line not quoted:\n%1").arg(quoted)));
    QVERIFY2(quoted.contains(QStringLiteral("> second line")),
             "second line not quoted");
    // A blank line inside a quote must still carry the marker, or the quote
    // visually ends there in every client that renders it.
    QVERIFY2(quoted.contains(QStringLiteral("\n>\n")),
             qPrintable(QStringLiteral("a blank line lost its marker:\n%1").arg(quoted)));
    QVERIFY2(quoted.contains(QStringLiteral("sender@example.org")),
             "no attribution line naming the sender");
    // A CRLF body must not leave a stray carriage return before every marker.
    ParsedMessage crlf;
    crlf.plainBody = QStringLiteral("one\r\ntwo");
    const QString quotedCrlf = ComposeContextBuilder::quoteBody(crlf);
    QVERIFY2(!quotedCrlf.contains(QLatin1Char('\r')),
             qPrintable(QStringLiteral("a carriage return survived quoting: %1")
                            .arg(quotedCrlf)));
}

/// Item 171's silent half. An HTML-only original has an EMPTY `plainBody`, so
/// quoting it produced an attribution line and nothing else: the content was
/// gone and nothing said so. Measured on the developer's own inbox 2026-08-27,
/// 30 of 342 sampled messages (~9%) declare text/html with no text/plain, so
/// this is not an edge case.
///
/// The fallback renders the HTML down to text. It does NOT preserve
/// formatting, which is the separate half of item 171 and is answered by the
/// multipart/alternative build; this only guarantees the words survive.
void TestComposeContext::anHtmlOnlyBodyIsQuotedAsText()
{
    ParsedMessage message;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.date = QStringLiteral("Thu, 20 Aug 2026 10:00:00 +0200");
    message.htmlBody = QStringLiteral(
        "<p>Revenue rose <b>12%</b> against forecast.</p><ul><li>Region A</li></ul>");
    // plainBody deliberately empty: this is the shape that lost the content.

    const QString quoted = ComposeContextBuilder::quoteBody(message);

    QVERIFY2(quoted.contains(QStringLiteral("Revenue rose")),
             qPrintable(QStringLiteral("the body was lost:\n%1").arg(quoted)));
    QVERIFY2(quoted.contains(QStringLiteral("Region A")),
             qPrintable(QStringLiteral("list content was lost:\n%1").arg(quoted)));
    QVERIFY2(quoted.contains(QStringLiteral("12%")),
             qPrintable(QStringLiteral("emphasised text was lost:\n%1").arg(quoted)));

    // Quoted like any other body, not dumped raw.
    QVERIFY2(quoted.contains(QStringLiteral("> Revenue rose")),
             qPrintable(QStringLiteral("the fallback is not quoted:\n%1").arg(quoted)));

    // Text, not markup: the plain half of a message must not carry tags.
    QVERIFY2(!quoted.contains(QStringLiteral("<b>")),
             qPrintable(QStringLiteral("markup reached the plain quote:\n%1").arg(quoted)));
    QVERIFY2(!quoted.contains(QStringLiteral("<p>")),
             qPrintable(QStringLiteral("markup reached the plain quote:\n%1").arg(quoted)));

    // A message WITH a plain part must keep using it, untouched: the fallback
    // is for the empty case only, and rendering HTML over a real plain part
    // would change every ordinary reply.
    ParsedMessage both;
    both.plainBody = QStringLiteral("the real plain part");
    both.htmlBody = QStringLiteral("<p>the html part</p>");
    const QString preferred = ComposeContextBuilder::quoteBody(both);
    QVERIFY2(preferred.contains(QStringLiteral("> the real plain part")),
             qPrintable(QStringLiteral("the plain part was not preferred:\n%1")
                            .arg(preferred)));
    QVERIFY2(!preferred.contains(QStringLiteral("the html part")),
             qPrintable(QStringLiteral("the html part was used anyway:\n%1")
                            .arg(preferred)));
}

QTEST_MAIN(TestComposeContext)
#include "test_composecontext.moc"
