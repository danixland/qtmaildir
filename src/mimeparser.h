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

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

/// An inline part referenced by a cid: URL from the HTML body.
struct InlinePart
{
    QString mimeType;
    QByteArray data;
};

struct Attachment
{
    QString filename;   ///< As it appeared in the message. Untrusted.
    QString mimeType;
    QByteArray data;

    /// filename reduced to a basename safe to join onto a directory.
    /// Attacker-controlled input: a filename may contain path separators or
    /// "..", so anything that could escape the target directory is stripped.
    /// Returns a generated name when nothing usable remains.
    QString safeFilename() const;

    /// Writes the attachment into directory. Returns the full path written, or
    /// an empty string on failure with *error set.
    ///
    /// **Overwrites an existing file of the same name.** That is right for a
    /// single save the user just confirmed a location for, and wrong for
    /// saving a batch: several messages in one thread commonly attach the
    /// same filename. Use saveWithoutOverwriting() there.
    QString saveTo(const QString &directory, QString *error) const;

    /// Writes the attachment into directory under a name that is not already
    /// taken, appending " (2)", " (3)" and so on before the extension.
    /// Returns the full path written, or an empty string on failure.
    ///
    /// Saving a thread's attachments with saveTo() silently destroyed files:
    /// six of sixteen were lost to same-name collisions and every write still
    /// reported success.
    QString saveWithoutOverwriting(const QString &directory, QString *error) const;

    /// True if candidatePath (need not exist) is directory itself or strictly
    /// beneath it, by path-boundary comparison after QDir::cleanPath on both
    /// sides (so ".." segments are resolved rather than compared textually).
    /// A bare QString::startsWith() is NOT sufficient here: it would let
    /// "/tmp/safe-evil" pass against "/tmp/safe" since one string is a
    /// textual prefix of the other despite being sibling directories.
    ///
    /// This is defence-in-depth, not currently load-bearing: saveTo() always
    /// sanitises the name with safeFilename() first, which reduces it to a
    /// plain basename, so no path reaching this check via saveTo()'s public
    /// interface can actually fail it today. It exists for a future change
    /// that stops sanitising, or that accepts a caller-supplied subpath.
    /// Exposed as its own function so that guarantee can be tested directly,
    /// independent of safeFilename() — a test driven purely through saveTo()
    /// cannot exercise this comparison at all, since safeFilename() always
    /// runs first and never produces a path that could fail it.
    static bool isPathInsideDirectory(const QString &directory, const QString &candidatePath);
};

/// A directory name for a thread's saved attachments, "<date> <subject>".
///
/// `rfc822Date` is a raw Date: header as ParsedMessage stores it; it is
/// reduced to "yyyy-MM-dd" when it parses and dropped when it does not.
///
/// Both inputs are untrusted: a subject is attacker-controlled and may carry
/// path separators, "..", control characters, or nothing usable at all. The
/// result is always a single plain component, never a path, and never "." or
/// "..". Falls back to the date alone, then to a generated name, so it is
/// never empty.
///
/// Length is capped: many filesystems limit one component to 255 bytes, and a
/// subject can be far longer than that.
QString attachmentFolderName(const QString &rfc822Date, const QString &subject);

/// A one-line summary of a raw To: header, for the sender's place on a card in
/// a Sent view.
///
/// Parsed with GMime rather than split on commas: a display name may CONTAIN a
/// comma, so `"Rossi, Mario" <m@example.org>, info@example.net` is two
/// addresses and naive splitting reports three.
///
/// Each address renders as its display name, falling back to the address when
/// it has none, so a list does not mix "Mario Rossi" with a bare address. With
/// more than `maxNames` recipients the rest collapse into "+N", mirroring the
/// tag strip's overflow chip rather than being elided mid-name.
///
/// The header is untrusted and may be empty, malformed, or a group such as
/// `undisclosed-recipients:;`. Returns an empty string when nothing usable can
/// be read, never a partial parse.
QString recipientSummary(const QString &rawTo, int maxNames = 2);

struct ParsedMessage
{
    bool ok = false;
    QString error;

    QString subject;
    QString from;

    /// Where the author asked for replies to go, raw and undecoded-into-parts.
    ///
    /// Takes precedence over `from` when building a reply (RFC 5322 3.6.2).
    /// Empty on the great majority of mail; a mailing list is the common case
    /// that sets it, and honouring it is what keeps a list reply on the list
    /// rather than on a person who never asked to be written to directly.
    QString replyTo;

    QString to;
    QString cc;

    /// Present only on a message this application wrote: a DRAFT or a sent
    /// copy carries its Bcc list in the file (see messagebuilder.cpp, which
    /// explains why), and a resumed draft has to read it back or a blind
    /// recipient is silently dropped from the message the user finishes.
    /// Received mail does not carry one.
    QString bcc;
    QString date;
    QString messageId;

    /// The raw References header, a whitespace-separated run of <message-ids>.
    ///
    /// Carried so a reply can extend the chain. Without it the reply appears
    /// as an orphan thread in the recipient's client, which is the whole
    /// reason the header exists.
    QString references;

    QString plainBody;
    QString htmlBody;

    QHash<QString, InlinePart> inlineParts;  ///< Keyed by Content-ID, no <>.
    QList<Attachment> attachments;

    bool hasHtml() const { return !htmlBody.isEmpty(); }
};

/// Parses a single message file using GMime.
///
/// Hand-rolling this would mean reimplementing RFC 2047 encoded words, RFC 2231
/// parameter continuations, transfer encodings, and charset conversion, plus
/// tolerance for malformed real-world mail.
class MimeParser
{
public:
    MimeParser();

    ParsedMessage parse(const QString &filePath) const;

    /// Parses an RFC 2822 `Date:` header, returning an invalid QDateTime when
    /// nothing usable is there.
    ///
    /// **Strips comments before parsing**, because `Qt::RFC2822Date` rejects
    /// the entire string when a trailing timezone comment is present, and
    /// `... +0200 (CEST)` is legal per RFC 5322 and common in the wild
    /// (verified on Qt 6.11). A parser without this silently loses the date on
    /// a large share of real mail.
    static QDateTime parseDate(const QString &rfc822Date);
};
