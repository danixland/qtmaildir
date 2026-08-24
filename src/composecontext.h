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

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "types.h"

struct Account;
class Config;
struct ParsedMessage;

/// Builds the ComposeContext that opens a composer.
///
/// Free functions in a namespace: this is pure logic over values, and keeping
/// it apart from ComposeWindow is what lets recipient derivation, subject
/// prefixing and account resolution be tested without a painter.
namespace ComposeContextBuilder {

/// One recipient split out of a header, as both parts and as a rendered whole.
///
/// Kept as a struct rather than a bare string because the two halves answer
/// two different questions and conflating them is how the user's own address
/// escapes a filter. `address` is what a comparison must use: a display name
/// may legitimately CONTAIN an address-looking substring, and a substring test
/// against the whole rendered form matches "not-me@example.org" for "me@example.org".
/// `rendered` is what goes in the field the user sees.
struct Recipient
{
    QString address;   ///< The bare addr-spec, no display name, no angle brackets.
    QString rendered;  ///< "Name <addr>" or the bare address when it has no name.
};

/// The addresses belonging to the user, across every configured account.
///
/// Every one of them is stripped from a reply-all's recipients. Missing one
/// means the user receives their own reply, which is the failure this is most
/// likely to have.
QStringList ownAddresses(const Config &config);

/// Splits a raw address header into individual recipients, using GMime.
///
/// NEVER split on commas. A display name may contain one, so
/// `"Rossi, Mario" <m@example.org>, info@example.net` is TWO addresses and a
/// naive split reports three, one of which ("Rossi") is not an address at all
/// and would be handed to the send command as a recipient. This is the same
/// reason `recipientSummary()` in mimeparser.cpp parses rather than splits.
///
/// Groups (`undisclosed-recipients:;`) contribute NOTHING. A group carries a
/// name and no mailbox, so naming it would put "undisclosed-recipients" in a
/// To field as though it were a person. This also closes a header-injection
/// shape: a raw newline in a header value makes GMime parse the smuggled
/// `Bcc: evil@example.net` as a GROUP, measured 2026-08-21, so dropping
/// non-mailboxes drops the injected recipient rather than carrying it forward.
///
/// An unparseable header yields an empty list rather than a partial guess.
QList<Recipient> parseAddressHeader(const QString &rawHeader);

/// Who a reply goes to, as \p toOut and \p ccOut.
///
/// This is the function the spec calls out as where the subtle bugs live, and
/// the rules are not interchangeable:
///
/// - **Reply** goes to the ORIGINAL SENDER only, and Cc is empty. Reply-To
///   takes precedence over From when the original carries one (RFC 5322
///   §3.6.2: it names where the author wants replies sent), which is what
///   makes a mailing list's reply land on the list rather than on a person who
///   never asked to be written to directly.
/// - **Reply-all** puts the sender in To, and the original's To and Cc in Cc.
///   The user's own addresses are stripped from BOTH, or they receive their
///   own reply. Comparison is case-insensitive: an address's domain is
///   case-insensitive by RFC and real mail varies the local part's case too,
///   so a case-sensitive filter lets `User@Example.org` through against a
///   configured `user@example.org`.
/// - A duplicate is suppressed ACROSS To and Cc, not within each: the sender
///   is very often also in the original's To, and listing them twice is what
///   naive per-field deduplication produces.
///
/// \p replyAll false yields sender-only. \p ownAddresses is what
/// ownAddresses(config) returned.
///
/// **A reply to the user's OWN message goes where that message went**, not
/// back to the user: To comes from the original's recipients instead of from
/// its sender. A plain reply takes its To and Cc together, having no Cc field
/// of its own to mirror into; a reply-all MIRRORS THE SPLIT, the original's To
/// becoming To and its Cc becoming Cc, because To means "addressed to you" and
/// Cc "for information" and promoting a Cc'd party to To is visible to every
/// recipient. This is reached from the Sent view, from a follow-up on
/// unanswered mail, and from any thread whose selected row is the user's own
/// message, so it is an ordinary gesture. "Own" means EVERY parsed sender
/// address is the user's; a co-sender is still someone to reply to.
///
/// Mail the user sent to THEMSELVES alone leaves nothing after that filter, and
/// there the sender is restored: the user is the correct recipient of their own
/// note. Emptying To instead would produce a message with no recipient that
/// still looks sendable, which is why stripping the sender was rejected as the
/// fix. Nothing else strips an own address from a plain Reply's To.
void recipientsForReply(const ParsedMessage &message, bool replyAll,
                        const QStringList &ownAddresses,
                        QStringList *toOut, QStringList *ccOut);

/// The References header for a reply: the original's References plus its
/// Message-ID.
///
/// Not optional. Without it a reply appears as an orphan thread in the
/// sender's own client. A duplicate Message-ID is not appended twice.
///
/// Ids come back BARE, without angle brackets, matching what GMime hands back
/// when MimeParser reads a `Message-ID`. The brackets are wire syntax and
/// `MessageBuilder` adds them when it writes the header, in one place rather
/// than in each caller: GMime writes an EMPTY header for a bare addr-spec
/// rather than complaining, so a caller that forgets them ships a reply that
/// threads nowhere while nothing looks wrong locally.
QStringList referencesForReply(const ParsedMessage &message);

/// Which account replies to a message whose file lives at \p messagePaths.
///
/// The displayed message's own maildir is the strongest available signal and
/// wins outright: mail sent to an address landed in that address's maildir, so
/// replying from it is what the recipient expects. The account dropdown is NOT
/// consulted.
///
/// A message can be in more than one maildir: on a list twice under two
/// addresses, or duplicated across accounts by mbsync, and notmuch returns
/// several filenames for one id. \p recipients disambiguates by preferring the
/// account matching a To or Cc entry; failing that the first is taken. The From
/// field shows the choice, so an arbitrary resolution is visible rather than
/// hidden.
QString accountForReply(const Config &config, const QStringList &messagePaths,
                        const QStringList &recipients, const QString &mailRoot);

/// Which account a NEW message comes from, by the four fallback rules.
///
/// \p selectedAccount is the dropdown's current account, empty for All
/// accounts. Returns empty only when no account can send at all.
QString accountForNew(const Config &config, const QString &selectedAccount);

/// `Re:` or `Fwd:` prefixed, without doubling an existing prefix.
///
/// An existing prefix is recognised in the non-English spellings a mixed-locale
/// mailbox receives (`AW:`, `SV:`, `RES:`, `WG:`, `TR:`, `RV:`, `ENC:`) and in
/// the counted forms Outlook emits (`Re[2]:`, `Re(3):`), or every one of those
/// doubles into `Re: AW: subject`.
///
/// Single-letter spellings are deliberately NOT recognised, though Italian
/// clients send `R:` and `I:`: `R: report on Q3` is an ordinary subject, and
/// treating it as a prefix means a genuine first reply gets no `Re:` and
/// threads nowhere. See the patterns in composecontext.cpp for the measurement.
QString replySubject(const QString &original);

QString forwardSubject(const QString &original);

/// Builds the context that RESUMES a draft from its file.
///
/// Unlike a reply, nothing here is derived: the recipients, the subject and
/// the body are the draft's own, read back verbatim. The account comes from
/// the From header rather than from which account owns the file, because a
/// draft states who it is from and that is the user's own earlier choice.
///
/// The returned context carries `draftPath`, which the composer seeds into
/// the path its autosave replaces. Returns a context whose kind is New, with
/// nothing filled in, when the file cannot be read.
ComposeContext forDraft(const Config &config, const QString &path);

/// The `>`-prefixed original, with an attribution line.
///
/// Takes a ParsedMessage, NOT a MessageNode: the node carries no body and no
/// date (it holds messageId, threadId, from, subject, tags, filePath and
/// depth), so quoting has to come from what MimeParser produced.
QString quoteBody(const ParsedMessage &message);

}  // namespace ComposeContextBuilder
