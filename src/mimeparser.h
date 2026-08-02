#pragma once

#include <QByteArray>
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
    QString saveTo(const QString &directory, QString *error) const;

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

struct ParsedMessage
{
    bool ok = false;
    QString error;

    QString subject;
    QString from;
    QString to;
    QString cc;
    QString date;
    QString messageId;

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
};
