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
// which Qt #defines to Q_SIGNALS, and the collision is a compile error whose
// message names neither library.
#include <gmime/gmime.h>

#include "composecontext.h"

#include "config.h"
#include "mimeparser.h"

#include <QDir>
#include <QSet>
#include <QRegularExpression>

namespace {

/// GMime must be initialised exactly once per process.
///
/// MimeParser and MessageBuilder each carry their own copy of this guard, and
/// this is a third rather than a shared one for the reason MessageBuilder
/// already records: a test may link only one of them, so neither can assume
/// another ran. Without it the first internet_address_list_parse() call
/// dereferences an uninitialised type registry and SEGVs, which is exactly what
/// this file did before the guard was added.
///
/// A function-local static rather than the `static bool` flag the other two
/// use: C++11 guarantees the initialiser runs exactly once even under
/// concurrent entry, which a bare flag does not.
void ensureGMimeInitialised()
{
    static const bool initialised = [] {
        g_mime_init();
        return true;
    }();
    Q_UNUSED(initialised);
}

/// Matches a reply prefix at the START of a subject, in the spellings clients
/// actually produce.
///
/// Anchored, and that is load-bearing rather than tidy: an unanchored search
/// finds "re:" inside an ordinary subject ("Notes re: budget") and refuses to
/// prefix a genuine first reply, which breaks threading in the recipient's
/// client with nothing to see locally.
///
/// **The non-English spellings are not politeness, they are the doubling bug in
/// a mixed-locale mailbox**, which this one is: the user writes Italian and
/// corresponds beyond it. German and Dutch clients send `AW:`, Scandinavian
/// ones `SV:`, Spanish and Portuguese `RES:`. An English-only pattern turns
/// every one of those into `Re: AW: subject`, and the next round into
/// `Re: Re: AW:`.
///
/// `Re[2]:` and `Re(2):` are the counted forms Outlook and some list managers
/// emit. They mean the same thing and must not be doubled either.
///
/// **Single-letter spellings are deliberately NOT here**, though Italian
/// clients do send `R:`. Measured 2026-08-21: with `r` in the alternation,
/// `R: report on Q3` reads as a reply prefix, so a genuine first reply to that
/// subject gets no `Re:` and threads nowhere in the recipient's client. A
/// one-letter token before a colon is an ordinary subject far more often than
/// it is a prefix, and this is the same failure the anchoring note above
/// describes. The cost of omitting it is one doubled `Re: R:`, which is
/// cosmetic; the cost of including it is broken threading, which is not.
///
/// Ordering inside the alternation matters: `res` before `re` so the longer
/// spelling is not consumed by the shorter one, leaving a stray `S:` unmatched.
const QRegularExpression &replyPrefix()
{
    static const QRegularExpression expression(
        QStringLiteral("^\\s*(res|re|aw|antw|sv|vs)\\s*(\\[\\d+\\]|\\(\\d+\\))?\\s*:"),
        QRegularExpression::CaseInsensitiveOption);
    return expression;
}

/// "Fwd:" and "Fw:" mean the same thing and both are common, as do the
/// non-English spellings a mixed-locale mailbox receives: German `WG:`, Spanish
/// and Portuguese `RV:` and `ENC:`, French `TR:`. Same reasoning as
/// replyPrefix(): an English-only pattern produces `Fwd: WG: subject`.
///
/// Italian `I:` is omitted for the reason replyPrefix() omits `R:`, and it is
/// the worse of the two: `I: notes` is an entirely ordinary subject. The
/// previous pattern was `fwd?`, which likewise matched a bare `F:`; that is not
/// a forward marker in any client and `F: results` must still get a prefix.
const QRegularExpression &forwardPrefix()
{
    static const QRegularExpression expression(
        QStringLiteral("^\\s*(fwd|fw|wg|enc|rv|tr)\\s*(\\[\\d+\\]|\\(\\d+\\))?\\s*:"),
        QRegularExpression::CaseInsensitiveOption);
    return expression;
}

/// The account whose maildir contains \p path, or empty.
QString accountOwning(const Config &config, const QString &path,
                      const QString &mailRoot)
{
    for (const Account &account : config.accounts()) {
        if (account.maildir.isEmpty())
            continue;
        const QString prefix =
            QDir(mailRoot).absoluteFilePath(account.maildir) + QLatin1Char('/');
        // Compared as a path prefix with the separator INCLUDED: without the
        // trailing slash an account "work" would also match a maildir
        // "work-archive", and the reply would be sent from the wrong account.
        if (path.startsWith(prefix))
            return account.key;
    }
    return {};
}

/// True when \p address is one of \p ownAddresses, compared case-insensitively.
///
/// Compared on the addr-spec, never on a rendered "Name <addr>": a display
/// name may legitimately contain an address-looking substring, and a substring
/// test against the whole form strips a real recipient whose name happens to
/// quote one of the user's addresses.
bool isOwn(const QString &address, const QStringList &ownAddresses)
{
    for (const QString &own : ownAddresses) {
        if (own.isEmpty())
            continue;
        if (address.compare(own, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

/// Appends \p recipient to \p out unless its address is already in \p seen or
/// belongs to the user. \p seen is updated.
///
/// Deduplication is keyed on the lowercased ADDRESS, so the same mailbox under
/// two different display names counts once, which is what the original's To
/// and Cc routinely contain.
void appendUnlessSuppressed(const ComposeContextBuilder::Recipient &recipient,
                            const QStringList &ownAddresses,
                            QSet<QString> *seen, QStringList *out)
{
    if (recipient.address.isEmpty())
        return;
    const QString key = recipient.address.toLower();
    if (seen->contains(key))
        return;
    if (isOwn(recipient.address, ownAddresses))
        return;
    seen->insert(key);
    out->append(recipient.rendered);
}

}  // namespace

QList<ComposeContextBuilder::Recipient>
ComposeContextBuilder::parseAddressHeader(const QString &rawHeader)
{
    ensureGMimeInitialised();

    const QByteArray utf8 = rawHeader.trimmed().toUtf8();
    if (utf8.isEmpty())
        return {};

    // Returns NULL rather than an empty list for input it can make nothing of,
    // including the empty string. Guarded above and again here: the header is
    // untrusted and this is the crash if it is not.
    InternetAddressList *list = internet_address_list_parse(nullptr, utf8.constData());
    if (!list)
        return {};

    QList<Recipient> recipients;
    const int count = internet_address_list_length(list);
    for (int i = 0; i < count; ++i) {
        InternetAddress *address = internet_address_list_get_address(list, i);
        if (!address)
            continue;

        // Only MAILBOXES. A group carries a name and no address, so keeping it
        // would put "undisclosed-recipients" in a To field as though it were a
        // person. It is also the injection defence: measured 2026-08-21, a raw
        // newline smuggled into a header makes GMime parse the following
        // "Bcc: evil@example.net" as a GROUP, and dropping non-mailboxes drops
        // it rather than pre-filling a recipient the user never saw.
        if (!INTERNET_ADDRESS_IS_MAILBOX(address))
            continue;

        const char *addr =
            internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(address));
        if (!addr || !*addr)
            continue;

        Recipient recipient;
        recipient.address = QString::fromUtf8(addr).trimmed();
        if (recipient.address.isEmpty())
            continue;

        // Rendered BY GMIME rather than assembled by string. Quoting a display
        // name is not a matter of wrapping it in quotes: a name containing a
        // comma must come back out quoted or it re-parses as two recipients.
        //
        // The final argument is ENCODE, and it is a security parameter rather
        // than a formatting preference. With FALSE a display name carrying a
        // raw newline renders with that newline intact, which is a
        // header-injection primitive: `"foo\nBcc: evil@example.net" <a@...>`
        // comes back out verbatim and anything writing it into a To: line
        // emits a second header the user never saw. With TRUE the same input
        // renders RFC 2047 encoded as `=?iso-8859-1?q?foo=0ABcc=3A?= ...` and
        // the newline can no longer terminate a header. Measured 2026-08-21;
        // this shipped as FALSE and the test caught it.
        char *rendered = internet_address_to_string(
            address, g_mime_format_options_get_default(), TRUE);
        recipient.rendered = rendered ? QString::fromUtf8(rendered).trimmed()
                                      : QString();
        g_free(rendered);
        if (recipient.rendered.isEmpty())
            recipient.rendered = recipient.address;

        recipients.append(recipient);
    }
    g_object_unref(list);

    return recipients;
}

QStringList ComposeContextBuilder::ownAddresses(const Config &config)
{
    QStringList addresses;
    for (const Account &account : config.accounts()) {
        const QString address = account.address.trimmed();
        // An empty address is dropped rather than collected. It would match
        // nothing usefully and, in any substring comparison, everything.
        if (!address.isEmpty() && !addresses.contains(address, Qt::CaseInsensitive))
            addresses.append(address);
    }
    return addresses;
}

void ComposeContextBuilder::recipientsForReply(const ParsedMessage &message,
                                               bool replyAll,
                                               const QStringList &ownAddresses,
                                               QStringList *toOut,
                                               QStringList *ccOut)
{
    if (toOut)
        toOut->clear();
    if (ccOut)
        ccOut->clear();
    if (!toOut)
        return;

    // Reply-To wins over From when present (RFC 5322 3.6.2: it names where the
    // author wants replies sent). Applied to reply-all as well as to a plain
    // reply: a list's reply-all belongs on the list too.
    QList<Recipient> sender = parseAddressHeader(message.replyTo);
    if (sender.isEmpty())
        sender = parseAddressHeader(message.from);

    // A reply to the user's OWN message goes to the people that message was
    // addressed to, not back to the user. Reached from the Sent view, from a
    // follow-up on unanswered mail, and from any thread whose selected row is
    // the user's own message, so it is an ordinary gesture rather than an edge
    // case. The alternative considered and rejected was stripping the sender
    // and leaving To empty, which silently drops every recipient and looks
    // sendable.
    //
    // "Own" means EVERY parsed sender address is the user's. A message with a
    // co-sender is still a reply to that co-sender.
    bool senderIsSelf = !sender.isEmpty();
    for (const Recipient &recipient : sender) {
        if (!isOwn(recipient.address, ownAddresses)) {
            senderIsSelf = false;
            break;
        }
    }

    QSet<QString> seen;
    if (senderIsSelf) {
        // The original's To, with own addresses removed. Its Cc is deliberately
        // NOT taken here for a reply-all: the split is the message's meaning,
        // To being "addressed to you" and Cc "for information", and promoting a
        // Cc'd party to To is visible to every recipient. The Cc pass below
        // carries them across unchanged, so the reply mirrors the original.
        //
        // A PLAIN reply has no Cc field to mirror into, so it takes To and Cc
        // together: everyone who was on the message is still addressed, which
        // is what a reply to a conversation the user started means.
        //
        // Mail the user sent to themselves alone leaves nothing after the own
        // filter, which is the one case where addressing the user IS correct,
        // so the sender is restored below rather than producing an empty To.
        QStringList headers = { message.to };
        if (!replyAll)
            headers.append(message.cc);
        for (const QString &header : headers) {
            const QList<Recipient> parsed = parseAddressHeader(header);
            for (const Recipient &recipient : parsed)
                appendUnlessSuppressed(recipient, ownAddresses, &seen, toOut);
        }
    }

    if (toOut->isEmpty()) {
        for (const Recipient &recipient : sender) {
            if (recipient.address.isEmpty())
                continue;
            const QString key = recipient.address.toLower();
            if (seen.contains(key))
                continue;
            // The sender is NOT filtered against the user's own addresses
            // here. This branch is reached either for an ordinary reply, where
            // the sender is somebody else, or for a note the user sent only to
            // themselves, where they are the correct recipient. Stripping in
            // either case produces a message with no recipient that still
            // looks sendable.
            seen.insert(key);
            toOut->append(recipient.rendered);
        }
    }

    // A From that parses to no mailbox at all leaves To empty, and that is
    // reachable from real mail rather than only from a hostile fixture: a bare
    // display name with no angle brackets ("From: Mailer Daemon") is what
    // bounces and some automated senders emit, and MimeParser hands it over as
    // a header with zero mailboxes. An empty To is the worst outcome available,
    // since MessageBuilder treats it as success: the message is handed to the
    // send command with nobody to deliver to and a copy is filed in Sent that
    // looks sent and reached no one.
    //
    // The original's recipients are the only remaining candidates. Own
    // addresses are stripped, so a message the user sent AND that has an
    // unparseable From still yields nothing here, which is correct: there is
    // genuinely nobody to address, and the composer shows an empty To the user
    // can see and fill rather than a wrong one they will not check.
    if (toOut->isEmpty()) {
        for (const QString &header : { message.to, message.cc }) {
            const QList<Recipient> parsed = parseAddressHeader(header);
            for (const Recipient &recipient : parsed)
                appendUnlessSuppressed(recipient, ownAddresses, &seen, toOut);
        }
    }

    if (!replyAll || !ccOut)
        return;

    // Everyone else goes to Cc, with the user's own addresses removed and
    // duplicates suppressed ACROSS the two fields rather than within each: the
    // sender is very often also in the original's To, and per-field
    // deduplication lists them twice.
    for (const QString &header : { message.to, message.cc }) {
        const QList<Recipient> parsed = parseAddressHeader(header);
        for (const Recipient &recipient : parsed)
            appendUnlessSuppressed(recipient, ownAddresses, &seen, ccOut);
    }
}

QStringList ComposeContextBuilder::referencesForReply(const ParsedMessage &message)
{
    QStringList references;
    QSet<QString> seen;

    const auto append = [&references, &seen](const QString &raw) {
        QString id = raw.trimmed();
        if (id.startsWith(QLatin1Char('<')) && id.endsWith(QLatin1Char('>')))
            id = id.mid(1, id.size() - 2).trimmed();
        if (id.isEmpty() || seen.contains(id))
            return;
        seen.insert(id);
        references.append(id);
    };

    // The header is a whitespace-separated run of <message-ids>, and real mail
    // wraps it across lines, so whitespace is the conformant separator.
    //
    // Commas are accepted BESIDES whitespace because some clients emit
    // `<a@x>,<b@y>`, which RFC 5322 does not allow here. Splitting on
    // whitespace alone turns that whole header into ONE token, and the bracket
    // strip below then yields the garbage id `a@x>,<b@y`: not a threading
    // degradation but a fabricated Message-ID sent to the recipient's client.
    // A comma cannot appear inside a msg-id, so accepting it costs nothing.
    const QStringList existing = message.references.split(
        QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    for (const QString &id : existing)
        append(id);

    // The original's own id goes LAST, which is what makes the chain an order
    // rather than a set. Appended through the same deduplication, so a message
    // whose References already names it does not repeat it.
    append(message.messageId);

    return references;
}

QString ComposeContextBuilder::accountForReply(const Config &config,
                                               const QStringList &messagePaths,
                                               const QStringList &recipients,
                                               const QString &mailRoot)
{
    QStringList candidates;
    for (const QString &path : messagePaths) {
        const QString key = accountOwning(config, path, mailRoot);
        if (!key.isEmpty() && !candidates.contains(key))
            candidates.append(key);
    }

    if (candidates.isEmpty())
        return {};
    if (candidates.size() == 1)
        return candidates.first();

    // Ambiguous: the same message in more than one maildir. Prefer the account
    // whose own address appears among the recipients, which is the reason the
    // copy landed there.
    for (const QString &key : candidates) {
        const Account account = config.account(key);
        if (account.address.isEmpty())
            continue;
        for (const QString &recipient : recipients) {
            if (recipient.contains(account.address, Qt::CaseInsensitive))
                return key;
        }
    }

    // Arbitrary, and visible: the From field shows the choice.
    return candidates.first();
}

QString ComposeContextBuilder::accountForNew(const Config &config,
                                             const QString &selectedAccount)
{
    const auto canSend = [&config](const QString &key) {
        if (key.isEmpty())
            return false;
        for (const Account &account : config.accounts()) {
            if (account.key == key)
                return account.canSend();
        }
        return false;
    };

    // 1. The dropdown's current account, when it is a specific one that can send.
    if (canSend(selectedAccount))
        return selectedAccount;

    // 2. [compose] default_account.
    if (canSend(config.compose().defaultAccount))
        return config.compose().defaultAccount;

    // 3. [general] startup_account, on the same condition.
    if (canSend(config.startupAccount()))
        return config.startupAccount();

    // 4. The first account in configuration order that can send. Arbitrary,
    // which is exactly why rules 2 and 3 exist.
    const QList<Account> sending = config.sendingAccounts();
    if (!sending.isEmpty())
        return sending.first().key;

    // No account can send. A valid read-only installation; the caller's action
    // is disabled and should never have reached this.
    return {};
}

QString ComposeContextBuilder::replySubject(const QString &original)
{
    if (replyPrefix().match(original).hasMatch())
        return original;
    return QStringLiteral("Re: ") + original;
}

QString ComposeContextBuilder::forwardSubject(const QString &original)
{
    if (forwardPrefix().match(original).hasMatch())
        return original;
    return QStringLiteral("Fwd: ") + original;
}

QString ComposeContextBuilder::quoteBody(const ParsedMessage &message)
{
    QStringList quoted;

    // The attribution line. Deliberately NOT translated and NOT reformatted
    // through a locale-dependent date format: this text is sent to a recipient
    // who may not share the user's locale, and the raw Date header is what
    // every other client quotes.
    quoted.append(QStringLiteral("On %1, %2 wrote:")
                      .arg(message.date, message.from));
    quoted.append(QString());

    // Normalised to LF first. A CRLF body split on '\n' alone leaves a
    // carriage return at the end of every line, which survives into the sent
    // message as a stray CR in the middle of a quoted line.
    QString body = message.plainBody;
    body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    body.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        // A blank line still carries the marker. Without it the quote visually
        // ends there in every client that renders quoting.
        quoted.append(line.isEmpty() ? QStringLiteral(">")
                                     : QStringLiteral("> ") + line);
    }

    return quoted.join(QLatin1Char('\n'));
}
