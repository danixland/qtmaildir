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

// gmime.h pulls in glib's gio headers, which declare a struct field named
// "signals". Qt's <QtCore/qnamespace.h> #defines "signals" to "Q_SIGNALS"
// (unless QT_NO_KEYWORDS is set), so gmime.h must be included before any Qt
// header in this translation unit to avoid a macro collision.
#include <gmime/gmime.h>

#include "messagebuilder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QObject>

#include "config.h"
#include "markdownrenderer.h"

namespace {

/// GMime must be initialised exactly once per process. MimeParser has its own
/// copy of this guard; both are cheap and neither can assume the other ran,
/// since a test may link only one of them.
void ensureGMimeInitialised()
{
    static bool initialised = false;
    if (!initialised) {
        g_mime_init();
        initialised = true;
    }
}

/// A text part carrying \p text as utf-8, quoted-printable.
///
/// Deliberately NOT g_mime_text_part_set_text(). Measured 2026-08-20: that
/// function encodes using the charset set at the moment it is CALLED, so the
/// obvious "set the text, then set the charset" order relabels the part without
/// re-encoding it. The result is a part headed charset=utf-8 whose bytes are
/// latin-1 (`Perch=E9`), which looks correct in every header and arrives as
/// mojibake. Building the content stream from the utf-8 bytes directly was
/// measured to produce `Perch=C3=A9` correctly. This user writes Italian, so an
/// accented character is in every message, not an edge case.
GMimePart *makeTextPart(const char *subtype, const QString &text)
{
    GMimePart *part = g_mime_part_new_with_type("text", subtype);
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(part), "charset", "utf-8");

    const QByteArray utf8 = text.toUtf8();
    GMimeStream *stream = g_mime_stream_mem_new_with_buffer(utf8.constData(),
                                                           static_cast<size_t>(utf8.size()));
    GMimeDataWrapper *wrapper =
        g_mime_data_wrapper_new_with_stream(stream, GMIME_CONTENT_ENCODING_DEFAULT);
    g_mime_part_set_content(part, wrapper);
    g_mime_part_set_content_encoding(part, GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE);

    g_object_unref(wrapper);
    g_object_unref(stream);
    return part;
}

/// Sets \p header on \p message to \p addresses, RFC 2047 encoded as utf-8.
/// Returns false and names the offending entry in \p badEntry if any of them
/// could not be parsed as an address.
///
/// Each entry is passed through internet_address_list_parse() rather than
/// treated as a bare address, because the composer's fields hold whatever the
/// user typed and "Name <addr@example.org>" is the ordinary form. Parsing per
/// entry rather than joining first keeps a comma inside a quoted display name
/// from splitting one recipient into two.
///
/// An entry that does not parse is a FAILURE, never a skip. The previous
/// version returned void, `continue`d past anything unparseable, and then only
/// wrote the header if the assembled list came out non-empty, so
/// `to = {"not an address at all ((("}` built a message with NO To: header at
/// all and reported success. With `msmtp -t` the recipients come FROM the
/// headers, so that is a message handed to the send command with nobody to
/// deliver to, and a copy filed in Sent that looks sent and reached no one.
/// Dropping one bad entry of several is the same defect wearing a smaller hat:
/// the others are delivered and nothing says which was not.
///
/// Both the NULL and the zero-length results are treated as failure. Measured
/// 2026-08-20 on GMime 3.2 with a standalone probe, every garbage input tried
/// (`not an address at all (((`, `((((`, `a b c`, `,`, `;`, `()`, `<>`, `` )
/// returned NULL, and no input was found that produced a non-null empty list.
/// The length check is therefore defensive rather than a path with a fixture
/// behind it: it is kept because the failure it would cover is a silently
/// unaddressed message, and it costs one comparison. Do not read it as
/// documenting observed behaviour, and do not expect a mutation on it to be
/// killed by the suite.
///
/// Worth knowing for anything built on top of this: GMime is LENIENT, not
/// strict. `garbage` and `""` both parse to a one-entry list. This function
/// rejects what GMime cannot parse at all; it is not an address validator, and
/// a typo that happens to be parseable still goes out.
bool setAddressHeader(GMimeMessage *message, const char *header, const QStringList &addresses,
                      QString *badEntry)
{
    if (addresses.isEmpty())
        return true;

    InternetAddressList *list = internet_address_list_new();
    for (const QString &entry : addresses) {
        const QString trimmed = entry.trimmed();
        if (trimmed.isEmpty())
            continue;
        const QByteArray utf8 = trimmed.toUtf8();
        InternetAddressList *parsed = internet_address_list_parse(nullptr, utf8.constData());
        const bool parsedNothing = !parsed || internet_address_list_length(parsed) == 0;
        if (parsedNothing) {
            if (parsed)
                g_object_unref(parsed);
            g_object_unref(list);
            *badEntry = trimmed;
            return false;
        }
        internet_address_list_append(list, parsed);
        g_object_unref(parsed);
    }

    if (internet_address_list_length(list) > 0) {
        GMimeFormatOptions *format = g_mime_format_options_get_default();
        char *rendered = internet_address_list_to_string(list, format, TRUE);
        if (rendered) {
            g_mime_object_set_header(GMIME_OBJECT(message), header, rendered, "utf-8");
            g_free(rendered);
        }
    }
    g_object_unref(list);
    return true;
}

}  // namespace

namespace MessageBuilder {

Result build(const OutgoingMessage &message, const Account &account)
{
    Result result;

    // Config::account() returns a DEFAULT-CONSTRUCTED Account for an unknown
    // key rather than reporting an error, so an account reached by a stale or
    // mistyped key arrives here looking like a valid one with empty fields.
    // Building from it would produce a message with an empty From: silently
    // malformed mail handed to the send command as though it were fine.
    if (account.address.trimmed().isEmpty()) {
        result.error = QObject::tr("The account %1 has no address configured, so no message "
                                   "can be sent from it.")
                           .arg(account.key);
        return result;
    }

    // Attachments are checked HERE rather than when the file was attached: a
    // file can vanish in between, and a message missing the thing it was
    // written to carry must never reach the send command. Checked before
    // anything is allocated, so the failure path frees nothing.
    //
    // isFile() is load-bearing and not tidiness. A DIRECTORY reports
    // exists=1 and isReadable=1, opening one read-only is legal, and GMime's
    // base64 encoder then loops on a read() returning EISDIR without ever
    // advancing or erroring: measured 2026-08-20 with strace at 2,169,821
    // failed reads in twenty seconds and still going, so build() never
    // returns. It runs synchronously from autosave on the GUI thread, so
    // dragging a folder into a composer froze the whole application with the
    // draft unrecoverable. Device nodes and FIFOs block or read forever the
    // same way, and isFile() excludes those too.
    for (const QString &path : message.attachments) {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile() || !info.isReadable()) {
            result.error = QObject::tr("The attachment %1 is missing or unreadable.")
                               .arg(info.fileName().isEmpty() ? path : info.fileName());
            return result;
        }
    }

    ensureGMimeInitialised();

    GMimeMessage *mime = g_mime_message_new(TRUE);

    const QByteArray fromName = account.name.toUtf8();
    const QByteArray fromAddress = account.address.toUtf8();
    g_mime_message_add_mailbox(mime, GMIME_ADDRESS_TYPE_FROM,
                               account.name.isEmpty() ? nullptr : fromName.constData(),
                               fromAddress.constData());

    // A recipient the user typed and this cannot understand STOPS the send,
    // exactly as a missing attachment does, rather than quietly not being
    // written. See setAddressHeader for what the silent version cost.
    const struct { const char *header; const QStringList &values; } fields[] = {
        {"To", message.to},
        {"Cc", message.cc},
        // Bcc is written into the bytes deliberately, and this is two separate
        // decisions rather than one.
        //
        // On transmission: the documented send command is `msmtp -t`, which
        // reads its recipients FROM the headers and strips Bcc itself before
        // sending, so recipients never see the list. Omitting it here would
        // mean blind recipients never receive the message at all, silently. If
        // sending ever passes recipients as arguments instead, this entry must
        // go with it.
        //
        // At rest: one built message serves three consumers, so the SENT COPY
        // and any autosaved DRAFT are stored in the Maildir with the Bcc list
        // in plaintext, and mbsync syncs those to the IMAP server where they
        // are visible to anyone with account access. That is a separate
        // exposure from transmission and it is accepted knowingly, not
        // overlooked. Do not "fix" it by stripping Bcc here: that breaks blind
        // delivery silently, which is worse.
        {"Bcc", message.bcc},
    };
    for (const auto &field : fields) {
        QString badEntry;
        if (!setAddressHeader(mime, field.header, field.values, &badEntry)) {
            g_object_unref(mime);
            result.error = QObject::tr("%1 is not an address this can send to.").arg(badEntry);
            return result;
        }
    }

    // The explicit "utf-8". Measured 2026-08-20: with NULL here GMime encodes
    // the subject as iso-8859-1 (=?iso-8859-1?B?...?=).
    const QByteArray subject = message.subject.toUtf8();
    g_mime_message_set_subject(mime, subject.constData(), "utf-8");

    if (!message.inReplyTo.trimmed().isEmpty()) {
        const QByteArray value = message.inReplyTo.trimmed().toUtf8();
        g_mime_object_set_header(GMIME_OBJECT(mime), "In-Reply-To", value.constData(), "utf-8");
    }
    if (!message.references.isEmpty()) {
        const QByteArray value = message.references.join(QLatin1Char(' ')).toUtf8();
        g_mime_object_set_header(GMIME_OBJECT(mime), "References", value.constData(), "utf-8");
    }

    // Measured 2026-08-20: GMime generates neither Date nor Message-ID unless
    // asked. A message without a Message-ID cannot be threaded by anything that
    // receives it, this application's own index of the sent copy included.
    GDateTime *now = g_date_time_new_now_local();
    g_mime_message_set_date(mime, now);
    g_date_time_unref(now);

    const QString domain = account.address.section(QLatin1Char('@'), 1);
    const QByteArray domainUtf8 = (domain.isEmpty() ? QStringLiteral("localhost") : domain).toUtf8();
    // Held locally rather than written into `result` here. Every failure below
    // would otherwise have to remember to clear it, which is a two-place
    // invariant the next early return forgets; it is assigned once, beside the
    // bytes, on the one path that succeeds.
    QString messageId;
    char *generatedId = g_mime_utils_generate_message_id(domainUtf8.constData());
    if (generatedId) {
        g_mime_message_set_message_id(mime, generatedId);
        messageId = QString::fromUtf8(generatedId);
        g_free(generatedId);
    }

    // The markdown SOURCE is the plain part, never a stripped-of-syntax
    // rewrite: `**bold**` reads as emphasis, and rewriting it would mean a
    // second renderer whose output could disagree with the HTML one.
    GMimeObject *body = GMIME_OBJECT(makeTextPart("plain", message.markdownBody));

    if (message.sendHtml) {
        GMimePart *html = makeTextPart("html", MarkdownRenderer::toHtml(message.markdownBody));
        GMimeMultipart *alternative = g_mime_multipart_new_with_subtype("alternative");
        // Least-rich FIRST. A client renders the LAST alternative it
        // understands, so a reversed order shows the markdown source everywhere
        // and the rendered part is never seen.
        g_mime_multipart_add(alternative, body);
        g_mime_multipart_add(alternative, GMIME_OBJECT(html));
        g_object_unref(body);
        g_object_unref(html);
        body = GMIME_OBJECT(alternative);
    }

    if (!message.attachments.isEmpty()) {
        GMimeMultipart *mixed = g_mime_multipart_new_with_subtype("mixed");
        // The body goes in FIRST, so the wrapper NESTS it rather than standing
        // beside it. Beside it, a client shows the alternatives as attachments
        // and the message reads as empty.
        g_mime_multipart_add(mixed, body);
        g_object_unref(body);

        QMimeDatabase mimeDb;
        for (const QString &path : message.attachments) {
            const QFileInfo info(path);
            const QMimeType type = mimeDb.mimeTypeForFile(info);
            const QByteArray typeName = type.name().toUtf8();

            GMimeContentType *contentType =
                g_mime_content_type_parse(nullptr, typeName.isEmpty()
                                                       ? "application/octet-stream"
                                                       : typeName.constData());
            GMimePart *part = g_mime_part_new();
            if (contentType) {
                g_mime_object_set_content_type(GMIME_OBJECT(part), contentType);
                g_object_unref(contentType);
            }

            GMimeStream *stream = g_mime_stream_file_open(path.toLocal8Bit().constData(),
                                                          "r", nullptr);
            if (!stream) {
                // Existence was checked above, so reaching here means the file
                // went away between the check and the read. Fail rather than
                // send a message with a hole in it.
                g_object_unref(part);
                g_object_unref(mixed);
                g_object_unref(mime);
                result.error = QObject::tr("The attachment %1 could not be read.")
                                   .arg(info.fileName());
                return result;
            }
            GMimeDataWrapper *wrapper =
                g_mime_data_wrapper_new_with_stream(stream, GMIME_CONTENT_ENCODING_DEFAULT);
            g_mime_part_set_content(part, wrapper);
            g_mime_part_set_content_encoding(part, GMIME_CONTENT_ENCODING_BASE64);
            g_object_unref(wrapper);
            g_object_unref(stream);

            const QByteArray filename = info.fileName().toUtf8();
            g_mime_part_set_filename(part, filename.constData());
            g_mime_object_set_disposition(GMIME_OBJECT(part), "attachment");

            g_mime_multipart_add(mixed, GMIME_OBJECT(part));
            g_object_unref(part);
        }
        body = GMIME_OBJECT(mixed);
    }

    g_mime_message_set_mime_part(mime, body);
    g_object_unref(body);

    GMimeFormatOptions *format = g_mime_format_options_get_default();
    char *rendered = g_mime_object_to_string(GMIME_OBJECT(mime), format);
    if (rendered) {
        result.bytes = QByteArray(rendered);
        result.messageId = messageId;
        g_free(rendered);
    } else {
        result.error = QObject::tr("The message could not be assembled.");
    }

    g_object_unref(mime);
    return result;
}

}  // namespace MessageBuilder
