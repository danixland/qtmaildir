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

#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

class QSettings;

/// Colours for tag chips.
///
/// Tags fall into two taxonomies. A functional tag says what state a thread is
/// in (flagged, replied, shopping/amazon) and is coloured from built-in
/// defaults or the [tagcolors] config group. An account tag says which mailbox
/// it arrived in, is named account-<key> after the [account.<key>] stanza, and
/// takes its colour from that stanza instead.
///
/// Lookup is by exact tag first, then by top-level prefix, so one entry can
/// colour a whole hierarchy: "shopping" covers shopping/amazon and
/// shopping/nike, while "shopping/amazon" still overrides its own.
class TagColors
{
public:
    /// The prefix marking a tag as naming an account rather than a state.
    static QString accountTagPrefix() { return QStringLiteral("account-"); }

    static bool isAccountTag(const QString &tag);

    /// The [account.<key>] suffix behind an account tag, empty if not one.
    static QString accountKeyForTag(const QString &tag);

    /// The tag notmuch carries for an account key. The mapping is derived,
    /// never configured, so the two cannot drift.
    static QString tagForAccountKey(const QString &key);

    /// Black or white, whichever stays legible on the given fill.
    static QColor textColourOn(const QColor &background);

    /// Reads the [tagcolors] group. An unparseable colour is collected into
    /// warnings() and the previous value kept, so one typo cannot leave a tag
    /// unstyled.
    void load(QSettings &settings);

    /// Registers an account's colour, taken from its own stanza.
    void setAccountColour(const QString &accountKey, const QColor &colour);

    /// Registers the text shown on an account's chip. Empty is ignored: a
    /// blank label would render an unreadable chip. The notmuch tag itself is
    /// never renamed, only what the chip displays.
    void setAccountLabel(const QString &accountKey, const QString &label);

    /// Chip text for an account tag, falling back to the account key. Empty
    /// when the tag does not name an account.
    QString labelForAccountTag(const QString &tag) const;

    /// True when this tag resolves to a colour that was chosen for it, as
    /// opposed to the fallback every unknown tag receives.
    bool hasColour(const QString &tag) const;

    /// Always valid: an unconfigured tag falls back to a colour derived from
    /// its name, stable across calls so a chip never changes as you scroll.
    QColor colourFor(const QString &tag) const;

    QStringList warnings() const { return m_warnings; }

private:
    /// The part before the first '/', which is the whole tag when it has none.
    static QString topLevelPrefix(const QString &tag);

    QHash<QString, QColor> m_colours;         ///< Exact tags and prefixes.
    QHash<QString, QColor> m_accountColours;  ///< Keyed by account key.
    QHash<QString, QString> m_accountLabels;  ///< Keyed by account key.
    QStringList m_warnings;
};
