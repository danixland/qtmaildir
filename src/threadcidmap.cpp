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

#include "threadcidmap.h"

#include "cidschemehandler.h"

namespace {

/// Removes '!' from a prefix without ever mapping two distinct prefixes onto
/// one another.
///
/// A plain replace of '!' with '_' is NOT safe here: "m0!x" and "m0_x" would
/// both become "m0_x", merging two messages into one key space, which is
/// precisely the collision the namespacing exists to prevent. Escaping the
/// escape character first keeps the transform injective: '_' doubles, and '!'
/// becomes "_x", so no output is reachable from two different inputs.
QString sanitizePrefix(const QString &prefix)
{
    if (!prefix.contains(QLatin1Char('!')) && !prefix.contains(QLatin1Char('_')))
        return prefix;

    QString result;
    result.reserve(prefix.size() + 4);
    for (const QChar c : prefix) {
        if (c == QLatin1Char('_'))
            result += QLatin1String("__");
        else if (c == QLatin1Char('!'))
            result += QLatin1String("_x");
        else
            result += c;
    }
    return result;
}

} // namespace

ThreadCidMap buildThreadCidMap(const QList<ThreadRenderItem> &items)
{
    ThreadCidMap map;

    for (const ThreadRenderItem &item : items) {
        const QString prefix = sanitizePrefix(item.cidPrefix);

        for (auto it = item.message.inlineParts.cbegin();
             it != item.message.inlineParts.cend(); ++it) {
            const QString key = CidSchemeHandler::namespacedKey(prefix, it.key());
            map.parts.insert(key, it.value());
            map.allowedCids.insert(key);
        }
    }

    return map;
}
