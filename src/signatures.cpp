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

/// The index of the first line of the quote, or -1 when the buffer has none.
///
/// The attribution line ("On Mon, someone wrote:") is deliberately NOT
/// included: it introduces the quote and belongs with it, so a signature
/// inserted above the quote goes above the attribution too. Returning the
/// quoted line itself would strand the signature between the attribution and
/// the text it introduces.
int quoteStart(const QStringList &lines)
{
    for (int i = 0; i < lines.size(); ++i) {
        if (!isQuoted(lines.at(i)))
            continue;
        // Walk back over the attribution and the blank line before it, so the
        // signature lands above the whole block rather than inside it.
        int start = i;
        while (start > 0 && !lines.at(start - 1).trimmed().isEmpty()
               && !isQuoted(lines.at(start - 1)))
            --start;
        return start;
    }
    return -1;
}

}  // namespace

QString replace(const QString &buffer, const QString &signature,
                const QStringList &known, Position position)
{
    Q_UNUSED(known);

    if (signature.isEmpty())
        return buffer;

    const QString block = QStringLiteral("\n") + kDelimiter
                          + QStringLiteral("\n") + signature;

    QStringList lines = buffer.split(QLatin1Char('\n'));
    const int quote =
        position == Position::AboveQuote ? quoteStart(lines) : -1;

    // No quote to sit above is not a special case: it is the End placement,
    // which is why a New message needs no branch of its own.
    if (quote < 0)
        return buffer + block;

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
