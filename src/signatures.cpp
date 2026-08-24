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

#include "signatures.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Signatures {

QStringList names(const QString &dir)
{
    QDir directory(dir);
    if (!directory.exists())
        return {};

    QStringList result;
    const QStringList files =
        directory.entryList({ QStringLiteral("*.md") }, QDir::Files, QDir::Name);
    result.reserve(files.size());
    for (const QString &file : files)
        result.append(QFileInfo(file).completeBaseName());
    return result;
}

QString text(const QString &dir, const QString &name)
{
    // A name arriving from the config file is untrusted input reaching a path.
    // Stems from names() never contain a separator, so rejecting one costs
    // nothing and stops a name like `../../.ssh/id_rsa` from being read into a
    // message the user is about to send.
    if (name.isEmpty() || name.contains(QLatin1Char('/'))
        || name.contains(QLatin1Char('\\')))
        return {};

    QFile file(dir + QStringLiteral("/") + name + QStringLiteral(".md"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

namespace {

/// The RFC 3676 signature separator: two hyphens, a space, end of line.
///
/// The trailing space is part of the standard and is what receiving clients
/// match on to fold or strip a signature. It is also why `--` typed by hand
/// does not collide: an editor does not add trailing whitespace on its own.
const QLatin1String kDelimiter("-- ");

bool isQuoted(const QString &line)
{
    return line.startsWith(QLatin1Char('>'));
}

/// \p text with trailing blank lines removed, the same normalisation the block
/// scan below applies. text() returns file content verbatim, so a signature
/// file ends with the newline every editor writes; without this the match
/// compares a block with no trailing newline against a known entry that has
/// one, and the guard silently fails, appending a second signature instead of
/// replacing the first.
QString stripTrailingBlankLines(const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

/// The index of the first line of the quote, or -1 when the buffer has none.
///
/// The attribution line ("On Mon, someone wrote:") is deliberately NOT
/// included: it introduces the quote and belongs with it, so a signature
/// inserted above the quote goes above the attribution too. Returning the
/// quoted line itself would strand the signature between the attribution and
/// the text it introduces.
int quoteStart(const QStringList &lines, int from = 0)
{
    for (int i = from; i < lines.size(); ++i) {
        if (!isQuoted(lines.at(i)))
            continue;
        // Walk back over the attribution and the blank line before it, so the
        // signature lands above the whole block rather than inside it.
        int start = i;
        while (start > from && !lines.at(start - 1).trimmed().isEmpty()
               && !isQuoted(lines.at(start - 1)))
            --start;
        return start;
    }
    return -1;
}

/// Where the block introduced by the delimiter at \p delimiter ends: the start
/// of the quote below it, or the end of the buffer when there is none.
///
/// This must use quoteStart() rather than scanning for the first quoted line,
/// because the ATTRIBUTION is part of the quote. Scanning for `>` alone puts
/// "On Mon, someone wrote:" inside the signature block, which then matches no
/// known signature and, when it did, left the attribution stranded above the
/// removed text. The two boundaries have to be the same one.
int blockEnd(const QStringList &lines, int delimiter)
{
    const int quote = quoteStart(lines, delimiter + 1);
    return quote < 0 ? lines.size() : quote;
}

/// The line index of the delimiter introducing an existing signature, or -1.
///
/// Two conditions, and both are load-bearing. The delimiter must not be
/// QUOTED, since the quoted original carries the other party's signature and
/// it is not this message's to replace. And the block after it must MATCH one
/// of \p known: finding a delimiter is not authority to delete what follows
/// it, because "-- " reaches a buffer pasted in with quoted text.
int existingSignature(const QStringList &lines, const QStringList &known)
{
    for (int i = lines.size() - 1; i >= 0; --i) {
        if (lines.at(i) != kDelimiter)
            continue;

        // The block runs to the end, or to the quote when the signature sits
        // above one.
        const int end = blockEnd(lines, i);
        // A trailing blank line belongs to the separation, not to the text.
        int textEnd = end;
        while (textEnd > i + 1 && lines.at(textEnd - 1).trimmed().isEmpty())
            --textEnd;

        const QString block =
            lines.mid(i + 1, textEnd - (i + 1)).join(QLatin1Char('\n'));
        if (known.contains(block))
            return i;
    }
    return -1;
}

/// \p lines with the signature at \p delimiter removed, blank separator and
/// all. The caller has already established that the block is a known one.
QStringList withoutSignature(const QStringList &lines, int delimiter)
{
    const int end = blockEnd(lines, delimiter);

    QStringList head = lines.mid(0, delimiter);
    while (!head.isEmpty() && head.last().trimmed().isEmpty())
        head.removeLast();

    QStringList result = head;
    if (end < lines.size()) {
        // Something follows (the quote): restore the blank line that
        // separated it from the signature now being removed.
        result.append(QString());
        result.append(lines.mid(end));
    } else {
        // The signature ran to the end of the buffer, and the trailing
        // newline the head lost with its blank line goes back.
        result.append(QString());
    }
    return result;
}

}  // namespace

QString replace(const QString &buffer, const QString &signature,
                const QStringList &known, Position position)
{
    // Normalise known to the same footing the block scan uses, once here rather
    // than per comparison. knownSignatures() passes text() verbatim, trailing
    // newline and all, and the match must be newline-insensitive or the guard
    // treats every on-disk signature as unknown.
    QStringList normalized;
    normalized.reserve(known.size());
    for (const QString &entry : known)
        normalized.append(stripTrailingBlankLines(entry));

    QStringList lines = buffer.split(QLatin1Char('\n'));

    const int existing = existingSignature(lines, normalized);
    if (existing >= 0)
        lines = withoutSignature(lines, existing);

    const QString stripped = lines.join(QLatin1Char('\n'));

    // "None", or nothing to insert: the removal above is the whole operation.
    if (signature.isEmpty())
        return stripped;

    const QString block = QStringLiteral("\n") + kDelimiter
                          + QStringLiteral("\n") + signature;

    const int quote =
        position == Position::AboveQuote ? quoteStart(lines) : -1;

    // No quote to sit above is not a special case: it is the End placement,
    // which is why a New message needs no branch of its own.
    if (quote < 0)
        return stripped + block;

    QStringList head = lines.mid(0, quote);
    const QStringList tail = lines.mid(quote);
    // The head ends in however many blank lines separated the reply from the
    // attribution. Drop them all and let the block supply exactly one, so the
    // spacing is the same whatever the quote was seeded with.
    while (!head.isEmpty() && head.last().trimmed().isEmpty())
        head.removeLast();

    // head.join() has no trailing newline once trimmed, so the terminator for
    // its last line is supplied here; `block` then opens with the blank line,
    // which is the same shape as the End placement over a buffer ending in a
    // newline.
    return head.join(QLatin1Char('\n')) + QStringLiteral("\n") + block
           + QStringLiteral("\n\n") + tail.join(QLatin1Char('\n'));
}

}  // namespace Signatures
