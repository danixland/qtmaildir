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

    if (contentId) {
        // Strip the angle brackets so the key matches a cid: URL body.
        QString id = QString::fromUtf8(contentId);
        if (id.startsWith(QLatin1Char('<')) && id.endsWith(QLatin1Char('>')))
            id = id.mid(1, id.size() - 2);
        out.inlineParts.insert(id, InlinePart{ mimeType, decodePart(part) });
        return;
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

QString Attachment::saveTo(const QString &directory, QString *error) const
{
    const QDir dir(directory);
    const QString target = dir.absoluteFilePath(safeFilename());

    // Belt and braces: confirm the resolved path really is inside directory,
    // so a future change to safeFilename() cannot silently reintroduce escape.
    const QString canonicalDir = QDir(directory).absolutePath();
    if (!QFileInfo(target).absolutePath().startsWith(canonicalDir)) {
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
