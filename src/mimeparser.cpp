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

#include "mimeparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

namespace {

/// GMime must be initialised exactly once per process.
void ensureGMimeInit()
{
    static bool initialised = false;
    if (!initialised) {
        g_mime_init();
        initialised = true;
    }
}

QString fromGChar(char *owned)
{
    if (!owned)
        return {};
    const QString result = QString::fromUtf8(owned);
    g_free(owned);
    return result;
}

QString headerText(GMimeMessage *message, const char *name)
{
    GMimeHeaderList *headers = g_mime_object_get_header_list(
        GMIME_OBJECT(message));
    if (!headers)
        return {};
    GMimeHeader *header = g_mime_header_list_get_header(headers, name);
    if (!header)
        return {};
    // get_value() returns the RFC 2047-decoded value.
    return QString::fromUtf8(g_mime_header_get_value(header));
}

QByteArray decodePart(GMimePart *part)
{
    GMimeDataWrapper *content = g_mime_part_get_content(part);
    if (!content)
        return {};

    GMimeStream *memStream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(content, memStream);
    g_mime_stream_flush(memStream);

    GByteArray *bytes = g_mime_stream_mem_get_byte_array(
        GMIME_STREAM_MEM(memStream));
    QByteArray result(reinterpret_cast<const char *>(bytes->data), bytes->len);

    g_object_unref(memStream);
    return result;
}

/// Walks the MIME tree, filling the parsed message.
void collectParts(GMimeObject *object, ParsedMessage &out)
{
    if (GMIME_IS_MULTIPART(object)) {
        GMimeMultipart *multipart = GMIME_MULTIPART(object);
        const int count = g_mime_multipart_get_count(multipart);
        for (int i = 0; i < count; ++i)
            collectParts(g_mime_multipart_get_part(multipart, i), out);
        return;
    }

    if (GMIME_IS_MESSAGE_PART(object)) {
        GMimeMessage *sub = g_mime_message_part_get_message(
            GMIME_MESSAGE_PART(object));
        if (sub)
            collectParts(g_mime_message_get_mime_part(sub), out);
        return;
    }

    if (!GMIME_IS_PART(object))
        return;

    GMimePart *part = GMIME_PART(object);
    GMimeContentType *contentType = g_mime_object_get_content_type(object);
    // g_mime_content_type_get_mime_type() returns a newly-allocated string
    // that must be freed; fromGChar() takes ownership of it.
    const QString mimeType = contentType
        ? fromGChar(g_mime_content_type_get_mime_type(contentType))
        : QStringLiteral("application/octet-stream");

    const char *disposition = g_mime_object_get_disposition(object);
    const bool isAttachment =
        disposition && g_ascii_strcasecmp(disposition, "attachment") == 0;

    const char *contentId = g_mime_part_get_content_id(part);

    if (isAttachment) {
        Attachment attachment;
        attachment.mimeType = mimeType;
        attachment.data = decodePart(part);
        const char *filename = g_mime_part_get_filename(part);
        attachment.filename = filename
            ? QString::fromUtf8(filename)
            : QStringLiteral("attachment");
        out.attachments.append(attachment);
        return;
    }

    // A content id makes a part referenceable; it does not make it
    // undisplayable. The two are independent, so register it and then fall
    // through to the body branches: setting a Content-Id on the text/html body
    // is legal and common in bulk-sender output, and returning here left such a
    // message with both body slots empty and a blank pane.
    //
    // Register before assigning, so a part that is both the body and a cid:
    // target stays reachable under its id for any sibling referencing it.
    if (contentId) {
        // Strip the angle brackets so the key matches a cid: URL body.
        QString id = QString::fromUtf8(contentId);
        if (id.startsWith(QLatin1Char('<')) && id.endsWith(QLatin1Char('>')))
            id = id.mid(1, id.size() - 2);
        out.inlineParts.insert(id, InlinePart{ mimeType, decodePart(part) });
    }

    if (mimeType == QLatin1String("text/plain") && out.plainBody.isEmpty()) {
        out.plainBody = QString::fromUtf8(decodePart(part));
    } else if (mimeType == QLatin1String("text/html") && out.htmlBody.isEmpty()) {
        out.htmlBody = QString::fromUtf8(decodePart(part));
    }
}

} // namespace

QString Attachment::safeFilename() const
{
    // Reduce to a basename: QFileInfo handles '/', and backslashes are stripped
    // explicitly because a Windows-authored name can carry them.
    QString name = filename;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    name = QFileInfo(name).fileName();

    // A name of "..", "." or empty leaves nothing usable.
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return QStringLiteral("attachment-%1").arg(
            QUuid::createUuid().toString(QUuid::Id128).left(8));

    return name;
}

QString Attachment::saveWithoutOverwriting(const QString &directory,
                                           QString *error) const
{
    const QString name = safeFilename();
    const QFileInfo info(name);
    const QString base = info.completeBaseName();
    // Kept whole: "archive.tar.gz" must not become "archive (2).gz".
    const QString suffix = info.suffix().isEmpty()
                               ? QString()
                               : QLatin1Char('.') + info.suffix();

    const QDir dir(directory);
    QString candidate = name;
    for (int n = 2; dir.exists(candidate); ++n)
        candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);

    // The containment check still applies: candidate is derived from
    // safeFilename(), but the guarantee belongs at the write, not upstream.
    const QString target = dir.absoluteFilePath(candidate);
    if (!isPathInsideDirectory(directory, target)) {
        if (error) {
            *error = QStringLiteral("Refusing to write outside %1")
                         .arg(QDir::cleanPath(QDir(directory).absolutePath()));
        }
        return {};
    }

    QFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    if (file.write(data) != data.size()) {
        if (error)
            *error = file.errorString();
        return {};
    }
    file.close();
    return target;
}

QString attachmentFolderName(const QString &rfc822Date, const QString &subject)
{
    // The date prefix sorts chronologically in a file manager. A Date: header
    // that does not parse is simply dropped rather than guessed at.
    // A trailing timezone comment, "... +0200 (CEST)", is legal per RFC 5322
    // and common in the wild, but Qt::RFC2822Date rejects the whole string
    // when one is present (verified on Qt 6.11). Strip comments before
    // parsing, or every such message silently loses its date prefix.
    QString cleaned = rfc822Date;
    cleaned.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    cleaned = cleaned.trimmed();

    QString prefix;
    const QDateTime parsed = QDateTime::fromString(cleaned, Qt::RFC2822Date);
    if (parsed.isValid())
        prefix = parsed.toString(QStringLiteral("yyyy-MM-dd"));

    // The subject is attacker-controlled and is about to become a directory
    // name. Everything that could make it more than one plain component goes:
    // separators, and the control characters that can hide what a name really
    // is when it is displayed.
    QString name = subject.simplified();
    name.remove(QLatin1Char('/'));
    name.remove(QLatin1Char('\\'));
    QString stripped;
    stripped.reserve(name.size());
    for (const QChar c : name) {
        if (!c.isNull() && c.category() != QChar::Other_Control)
            stripped.append(c);
    }
    // Leading dots would make a hidden directory, and a name of "." or ".."
    // would escape or alias the parent; removing them handles every case.
    while (stripped.startsWith(QLatin1Char('.')))
        stripped.remove(0, 1);
    stripped = stripped.trimmed();

    QString combined;
    if (!prefix.isEmpty() && !stripped.isEmpty())
        combined = prefix + QLatin1Char(' ') + stripped;
    else if (!prefix.isEmpty())
        combined = prefix;
    else
        combined = stripped;

    // A subject can be far longer than a filesystem component allows. Cut to
    // a conservative 120 characters, well under the usual 255-byte limit even
    // once multi-byte characters are counted as bytes.
    constexpr int maxLength = 120;
    if (combined.size() > maxLength)
        combined = combined.left(maxLength).trimmed();

    // Nothing usable survived: no parseable date and a subject that was empty,
    // punctuation, or control characters only.
    if (combined.isEmpty()) {
        return QStringLiteral("attachments-%1").arg(
            QUuid::createUuid().toString(QUuid::Id128).left(8));
    }

    return combined;
}

bool Attachment::isPathInsideDirectory(const QString &directory, const QString &candidatePath)
{
    // Compare candidatePath itself, not QFileInfo(candidatePath).absolutePath()
    // (which would be its *parent* directory) -- candidatePath may itself be
    // the directory being tested, as in the "is directory itself" case this
    // function documents.
    const QString canonicalDir = QDir::cleanPath(QDir(directory).absolutePath());
    const QString canonicalTarget =
        QDir::cleanPath(QFileInfo(candidatePath).absoluteFilePath());
    return canonicalTarget == canonicalDir
        || canonicalTarget.startsWith(canonicalDir + QLatin1Char('/'));
}

QString Attachment::saveTo(const QString &directory, QString *error) const
{
    const QDir dir(directory);
    const QString target = dir.absoluteFilePath(safeFilename());

    // Defence-in-depth, not currently load-bearing: safeFilename() always
    // reduces the name to a plain basename before target is built above, so
    // this check cannot actually be failed via saveTo()'s public interface
    // today (dir.absoluteFilePath(basename) can't escape dir). It exists so
    // that a future change which stops sanitising the name, or which starts
    // accepting a caller-supplied subpath instead of a bare filename, still
    // cannot write outside directory. See Attachment::isPathInsideDirectory
    // for the containment logic and its own direct tests.
    if (!isPathInsideDirectory(directory, target)) {
        const QString canonicalDir = QDir::cleanPath(QDir(directory).absolutePath());
        if (error)
            *error = QStringLiteral("Refusing to write outside %1").arg(canonicalDir);
        return {};
    }

    QFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    file.write(data);
    file.close();
    return target;
}

MimeParser::MimeParser()
{
    ensureGMimeInit();
}

ParsedMessage MimeParser::parse(const QString &filePath) const
{
    ParsedMessage out;

    FILE *fp = fopen(filePath.toLocal8Bit().constData(), "r");
    if (!fp) {
        out.error = QStringLiteral("Cannot open %1").arg(filePath);
        return out;
    }

    GMimeStream *stream = g_mime_stream_file_new(fp);
    GMimeParser *parser = g_mime_parser_new_with_stream(stream);
    GMimeMessage *message = g_mime_parser_construct_message(parser, nullptr);

    g_object_unref(parser);
    g_object_unref(stream);

    if (!message) {
        out.error = QStringLiteral("Cannot parse %1").arg(filePath);
        return out;
    }

    out.subject = QString::fromUtf8(
        g_mime_message_get_subject(message) ?: "");
    out.from = headerText(message, "From");
    out.to = headerText(message, "To");
    out.cc = headerText(message, "Cc");
    out.date = headerText(message, "Date");
    out.messageId = QString::fromUtf8(
        g_mime_message_get_message_id(message) ?: "");

    GMimeObject *body = g_mime_message_get_mime_part(message);
    if (body)
        collectParts(body, out);

    g_object_unref(message);

    out.ok = true;
    return out;
}
