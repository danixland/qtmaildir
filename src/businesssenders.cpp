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

} // namespace BusinessSenders
