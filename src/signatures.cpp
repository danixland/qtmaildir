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

QString replace(const QString &buffer, const QString &signature,
                const QStringList &known, Position position)
{
    Q_UNUSED(signature);
    Q_UNUSED(known);
    Q_UNUSED(position);
    return buffer;
}

}  // namespace Signatures
