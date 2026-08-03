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

#include "tagcolors.h"

#include <QCryptographicHash>
#include <QSettings>

namespace {

/// Colours for the tags every notmuch setup has. Chosen to stay legible on a
/// dark theme, which is where the message pane already sits.
QHash<QString, QColor> builtInColours()
{
    return {
        { QStringLiteral("flagged"),      QColor(0xd4, 0x9c, 0x1a) },
        { QStringLiteral("unread"),       QColor(0x2f, 0x6f, 0xa8) },
        { QStringLiteral("deleted"),      QColor(0x8b, 0x2c, 0x2c) },
        { QStringLiteral("spam"),         QColor(0xa8, 0x5c, 0x18) },
        { QStringLiteral("attachment"),   QColor(0x5a, 0x5a, 0x64) },
        { QStringLiteral("replied"),      QColor(0x3d, 0x7a, 0x4a) },
        { QStringLiteral("passed"),       QColor(0x3d, 0x7a, 0x62) },
        { QStringLiteral("draft"),        QColor(0x77, 0x66, 0x33) },
        { QStringLiteral("encrypted"),    QColor(0x6a, 0x4a, 0x8a) },
        { QStringLiteral("signed"),       QColor(0x53, 0x4a, 0x8a) },
        { QStringLiteral("inbox"),        QColor(0x44, 0x4a, 0x52) },
        { QStringLiteral("mailing-list"), QColor(0x36, 0x6a, 0x6a) },
    };
}

}  // namespace

bool TagColors::isAccountTag(const QString &tag)
{
    // The prefix alone, with nothing after it, names no account.
    return tag.startsWith(accountTagPrefix())
           && tag.size() > accountTagPrefix().size();
}

QString TagColors::accountKeyForTag(const QString &tag)
{
    if (!isAccountTag(tag))
        return {};
    return tag.mid(accountTagPrefix().size());
}

QString TagColors::tagForAccountKey(const QString &key)
{
    return accountTagPrefix() + key;
}

QColor TagColors::textColourOn(const QColor &background)
{
    // Perceived luminance: the eye weights green far above blue, so a plain
    // average would call a saturated blue "light" and print black on it.
    const double luminance = (0.299 * background.red()
                              + 0.587 * background.green()
                              + 0.114 * background.blue()) / 255.0;
    return luminance > 0.55 ? QColor(Qt::black) : QColor(Qt::white);
}

QString TagColors::topLevelPrefix(const QString &tag)
{
    const int slash = tag.indexOf(QLatin1Char('/'));
    return slash < 0 ? tag : tag.left(slash);
}

void TagColors::load(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("tagcolors"));
    // allKeys(), not childKeys(): QSettings treats '/' in a key as a group
    // separator, so a hierarchical tag like shopping/amazon becomes a nested
    // key that childKeys() does not return. allKeys() reports both, and the
    // nested one comes back in the "shopping/amazon" form the tag already has.
    // (In the INI file itself it is written as shopping\amazon.)
    const QStringList keys = settings.allKeys();
    for (const QString &key : keys) {
        const QString value = settings.value(key).toString();
        const QColor colour(value);
        if (!colour.isValid()) {
            m_warnings.append(
                QStringLiteral("Unparseable colour '%1' for tag '%2' in "
                               "[tagcolors]").arg(value, key));
            continue;
        }
        m_colours.insert(key, colour);
    }
    settings.endGroup();
}

void TagColors::setAccountColour(const QString &accountKey, const QColor &colour)
{
    if (accountKey.isEmpty() || !colour.isValid())
        return;
    m_accountColours.insert(accountKey, colour);
}

void TagColors::setAccountLabel(const QString &accountKey, const QString &label)
{
    if (accountKey.isEmpty() || label.isEmpty())
        return;
    m_accountLabels.insert(accountKey, label);
}

QString TagColors::labelForAccountTag(const QString &tag) const
{
    const QString key = accountKeyForTag(tag);
    if (key.isEmpty())
        return {};
    return m_accountLabels.value(key, key);
}

bool TagColors::hasColour(const QString &tag) const
{
    if (isAccountTag(tag))
        return m_accountColours.contains(accountKeyForTag(tag));

    const QHash<QString, QColor> builtIn = builtInColours();
    return m_colours.contains(tag) || builtIn.contains(tag)
           || m_colours.contains(topLevelPrefix(tag))
           || builtIn.contains(topLevelPrefix(tag));
}

QColor TagColors::colourFor(const QString &tag) const
{
    // An account's colour lives in its own stanza, not in [tagcolors].
    if (isAccountTag(tag)) {
        const QColor colour = m_accountColours.value(accountKeyForTag(tag));
        if (colour.isValid())
            return colour;
    }

    const QHash<QString, QColor> builtIn = builtInColours();

    // Most specific first: an exact entry must beat the prefix it falls under,
    // or a single child tag could never be singled out.
    if (m_colours.contains(tag))
        return m_colours.value(tag);
    if (builtIn.contains(tag))
        return builtIn.value(tag);

    const QString prefix = topLevelPrefix(tag);
    if (m_colours.contains(prefix))
        return m_colours.value(prefix);
    if (builtIn.contains(prefix))
        return builtIn.value(prefix);

    // Nothing configured: derive a colour from the name so the chip is still
    // readable and distinguishable. Hashing keeps it stable across calls, and
    // the fixed saturation and lightness keep it in the same family as the
    // built-ins rather than producing neon.
    const QByteArray digest =
        QCryptographicHash::hash(tag.toUtf8(), QCryptographicHash::Md5);
    const int hue = static_cast<quint8>(digest.at(0)) * 360 / 256;
    return QColor::fromHsl(hue, 90, 80);
}
