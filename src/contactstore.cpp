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

#include "contactstore.h"

#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QStringList>

#include <algorithm>

namespace {

/// Index of the colon separating property from value: the first one that is
/// not inside a double-quoted parameter value.
///
/// `EMAIL;TYPE=INTERNET;LABEL="work: main":a@example.org` has a colon inside
/// the quoted label, and splitting on the first colon would hand the parser
/// `main":a@example.org` as the address. A backslash inside the quotes escapes
/// the next character, so `\"` does not close the quote.
int colonOutsideQuotes(const QString &line)
{
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\')) {
            ++i;  // Skip the escaped character; it cannot be a delimiter.
            continue;
        }
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            continue;
        }
        if (c == QLatin1Char(':') && !inQuotes)
            return i;
    }
    return -1;
}

/// Decodes the vCard TEXT escape set, applied to FN: a name written
/// `Rossi\, Mario` is one name with a comma, and left escaped the backslash
/// reaches the completion popup. N carries the same escapes, but the struct
/// holds no N and FN is what a display name is.
QString unescapeText(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (int i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c != QLatin1Char('\\') || i + 1 >= value.size()) {
            out.append(c);
            continue;
        }

        const QChar escaped = value.at(++i);
        switch (escaped.unicode()) {
        case 'n':
        case 'N':
            out.append(QLatin1Char('\n'));
            break;
        case '\\':
        case ',':
        case ';':
            out.append(escaped);
            break;
        default:
            // An unknown escape keeps its character, dropping the backslash
            // rather than guessing at a meaning it does not have.
            out.append(escaped);
            break;
        }
    }
    return out;
}

} // namespace

namespace ContactStore {

QString unfold(const QString &text)
{
    QStringList logical;
    const QStringList lines = text.split(QLatin1Char('\n'));

    for (const QString &rawLine : lines) {
        // The store's own files are LF; a CRLF server is handled here rather
        // than left to reach a value with a stray carriage return in it.
        QString line = rawLine;
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        const bool isContinuation =
            !line.isEmpty()
            && (line.at(0) == QLatin1Char(' ') || line.at(0) == QLatin1Char('\t'));

        if (isContinuation && !logical.isEmpty()) {
            // Join to the line that precedes it, removing only the marker
            // whitespace character. A trailing space on the first line is
            // content and survives.
            logical.last() += line.mid(1);
        } else {
            logical.append(line);
        }
    }

    return logical.join(QLatin1Char('\n'));
}

QList<Contact> parseCard(const QString &unfoldedText)
{
    QString name;
    QStringList emails;

    const QStringList lines = unfoldedText.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.isEmpty())
            continue;

        const int colon = colonOutsideQuotes(line);
        if (colon < 0)
            continue;

        // The property name is everything before the first ';', and the
        // parameters that follow belong to it. Matched case-insensitively per
        // RFC 2426, so `email:` is an EMAIL.
        const QString property =
            line.left(colon).section(QLatin1Char(';'), 0, 0).trimmed();
        const QString value = line.mid(colon + 1).trimmed();

        if (property.compare(QLatin1String("FN"), Qt::CaseInsensitive) == 0) {
            // Last one wins if a malformed card carries two, rather than
            // merging two names into one.
            name = unescapeText(value);
        } else if (property.compare(QLatin1String("EMAIL"), Qt::CaseInsensitive) == 0) {
            // PHOTO, ADR, TEL and the rest are not matched and fall through
            // unread; N is deliberately not used as a fallback name, because
            // FN is what a display name is.
            if (!value.isEmpty())
                emails.append(value);
        }
    }

    QList<Contact> contacts;
    contacts.reserve(emails.size());
    for (const QString &email : emails)
        contacts.append(Contact{ name, email });
    return contacts;
}

QList<Contact> loadDirectory(const QString &directory)
{
    QList<Contact> found;
    if (directory.isEmpty())
        return found;

    // Recursive because a vdir keeps one subdirectory per collection. A
    // missing root simply yields nothing; no warning, since a machine with no
    // vdir is the ordinary case.
    QDirIterator it(directory, QStringList{ QStringLiteral("*.vcf") },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next());
        if (!file.open(QIODevice::ReadOnly))
            continue;  // Unreadable: skip it, do not lose the rest.
        const QString text = QString::fromUtf8(file.readAll());
        found += parseCard(unfold(text));
    }

    // De-duplicated on the address, case-insensitively: two collections
    // holding the same person is the ordinary case, not an error. The first
    // card seen wins.
    QList<Contact> unique;
    QHash<QString, int> seen;
    unique.reserve(found.size());
    for (const Contact &contact : found) {
        const QString key = contact.email.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key, unique.size());
        unique.append(contact);
    }

    // Sorted by name then address, case-insensitively. A locale-aware compare
    // is deliberately not used: the order is a completion list, not prose.
    std::sort(unique.begin(), unique.end(),
              [](const Contact &a, const Contact &b) {
                  const int byName =
                      QString::compare(a.name, b.name, Qt::CaseInsensitive);
                  if (byName != 0)
                      return byName < 0;
                  return QString::compare(a.email, b.email, Qt::CaseInsensitive) < 0;
              });

    return unique;
}

}  // namespace ContactStore
