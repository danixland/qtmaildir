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

#include "avatar.h"

#include <QCryptographicHash>
#include <QPainter>
#include <QPainterPath>

namespace {

QString twoFrom(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() >= 2)
        return trimmed.left(2).toUpper();
    if (trimmed.size() == 1)
        return (trimmed + trimmed).toUpper();
    return QString();
}

} // namespace

namespace Avatar {

QString initialsFor(const QString &displayName, const QString &address,
                    const QString &accountLabel)
{
    const QStringList words = displayName.split(QLatin1Char(' '),
                                                Qt::SkipEmptyParts);
    if (words.size() >= 2) {
        return (words.at(0).left(1) + words.at(1).left(1)).toUpper();
    }
    if (words.size() == 1) {
        const QString one = twoFrom(words.at(0));
        if (!one.isEmpty())
            return one;
    }

    // No usable name. The local part and the domain each give one letter,
    // which never degrades to a single letter the way the local part alone
    // would, and never reads as a truncated word.
    const int at = address.indexOf(QLatin1Char('@'));
    if (at > 0) {
        const QString local = address.left(at).trimmed();
        const QString domain = address.mid(at + 1).trimmed();
        if (!local.isEmpty() && !domain.isEmpty())
            return (local.left(1) + domain.left(1)).toUpper();
    }
    // An address with no `@` is still something to show.
    const QString bare = twoFrom(address);
    if (!bare.isEmpty())
        return bare;

    const QString account = twoFrom(accountLabel);
    if (!account.isEmpty())
        return account;

    // Nothing at all. Two characters regardless, so the shape never breaks.
    return QStringLiteral("??");
}

} // namespace Avatar
