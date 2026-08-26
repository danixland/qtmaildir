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

#include "businesssenders.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace BusinessSenders {

List parse(const QString &contents)
{
    List list;
    const QStringList lines = contents.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        // A commented entry is the reject gesture: it stays in the file so it
        // is never proposed again, and it is not applied.
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QString entry = line.toLower();
        if (entry.startsWith(QLatin1Char('@')))
            list.domains.insert(entry.mid(1));
        else
            list.addresses.insert(entry);
    }
    return list;
}

List load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return List();
    return parse(QString::fromUtf8(file.readAll()));
}

bool contains(const List &list, const QString &address)
{
    const QString lowered = address.trimmed().toLower();
    if (lowered.isEmpty())
        return false;
    if (list.addresses.contains(lowered))
        return true;

    const int at = lowered.indexOf(QLatin1Char('@'));
    if (at < 0)
        return false;
    return list.domains.contains(lowered.mid(at + 1));
}

QString defaultPath()
{
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath(
        QStringLiteral("qtmaildir/business-senders"));
}

bool looksLikeBulk(const QString &address)
{
    static const QStringList kBulkLocalParts {
        QStringLiteral("noreply"),     QStringLiteral("no-reply"),
        QStringLiteral("donotreply"),  QStringLiteral("do-not-reply"),
        QStringLiteral("info"),        QStringLiteral("support"),
        QStringLiteral("billing"),     QStringLiteral("newsletter"),
        QStringLiteral("notifications"), QStringLiteral("mailer-daemon"),
    };
    const int at = address.indexOf(QLatin1Char('@'));
    if (at <= 0)
        return false;
    const QString local = address.left(at).toLower();
    for (const QString &candidate : kBulkLocalParts) {
        if (local == candidate || local.startsWith(candidate))
            return true;
    }
    return false;
}

void appendCandidates(const QString &path, const QHash<QString, int> &counts)
{
    // Every address the file MENTIONS, active or rejected. Parsed separately
    // from parse() above, which deliberately drops comments: here a comment is
    // exactly what must be remembered.
    QSet<QString> mentioned;
    bool needsLeadingNewline = false;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString contents = QString::fromUtf8(existing.readAll());
        // Hand-editing (the documented workflow) can leave the file without a
        // trailing newline; appending then glues the first addition onto the
        // last entry and silently breaks it. Write a newline before the
        // additions in that case.
        if (!contents.isEmpty() && !contents.endsWith(QLatin1Char('\n')))
            needsLeadingNewline = true;
        const QStringList lines = contents.split(QLatin1Char('\n'));
        for (const QString &raw : lines) {
            QString line = raw.trimmed();
            if (line.startsWith(QLatin1Char('#')))
                line = line.mid(1).trimmed();
            if (line.isEmpty())
                continue;
            // "noreply@cofidis.it (47 messages)" mentions the address before
            // its count.
            mentioned.insert(line.section(QLatin1Char(' '), 0, 0).toLower());
        }
        existing.close();
    }

    QStringList additions;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        const QString address = it.key().trimmed().toLower();
        if (address.isEmpty() || mentioned.contains(address))
            continue;
        if (!looksLikeBulk(address))
            continue;
        additions.append(QStringLiteral("# %1 (%2 messages)")
                             .arg(address)
                             .arg(it.value()));
    }
    if (additions.isEmpty())
        return;

    additions.sort();

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&file);
    if (needsLeadingNewline)
        out << '\n';
    for (const QString &line : additions)
        out << line << '\n';
}

QString scanQuery(const QString &path)
{
    // "*" is notmuch's match-everything. An EMPTY string would also match
    // everything, which is why Config::matchNothingQuery() exists elsewhere in
    // this codebase; being explicit here means a reader never has to wonder
    // which of the two an empty return meant.
    const List existing = load(path);
    if (existing.addresses.isEmpty() && existing.domains.isEmpty())
        return QStringLiteral("*");
    return QStringLiteral("date:1week..");
}

} // namespace BusinessSenders
