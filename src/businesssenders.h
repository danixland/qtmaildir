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

#include <QSet>
#include <QString>
#include <QStringList>

/// The list of senders that read as businesses rather than people.
///
/// `~/.config/qtmaildir/business-senders`, plain text, one entry per line,
/// `#` comments, blank lines ignored. Deliberately NOT in qtmaildir.conf and
/// deliberately not INI: the user's stated workflow is grep-and-edit, QSettings
/// would fight a bare list, and the main config is already large.
///
/// An entry is an exact address (`noreply@cofidis.it`) or a whole domain
/// (`@cofidis.it`). No globs: a pattern language is a rule the user cannot grep
/// for literally, which defeats the file's purpose.
namespace BusinessSenders
{

/// Parsed entries, lower-cased. Two sets rather than one list so a lookup is a
/// hash probe per repaint rather than a walk.
struct List
{
    QSet<QString> addresses;
    QSet<QString> domains;   ///< Stored WITHOUT the leading '@'.
};

List parse(const QString &contents);

/// Reads `path`. A missing or unreadable file yields an empty list rather than
/// an error: the feature is cosmetic and must never block startup.
List load(const QString &path);

bool contains(const List &list, const QString &address);

/// `~/.config/qtmaildir/business-senders`, built from
/// QStandardPaths::GenericConfigLocation.
QString defaultPath();

} // namespace BusinessSenders
