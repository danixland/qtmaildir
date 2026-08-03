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

#include <QHash>
#include <QKeySequence>
#include <QStringList>

class QSettings;

/// Maps key sequences to action names. Action names are plain strings so this
/// class has no dependency on the widgets that implement the actions.
class KeyMap
{
public:
    /// Every action name the application understands. loadOverrides() rejects
    /// anything not in this set, so a typo in the config cannot bind silently.
    static QStringList knownActions();

    void loadDefaults();

    /// Reads the [keys] group. Invalid sequences and unknown action names are
    /// collected into warnings() rather than throwing or aborting.
    void loadOverrides(QSettings &settings);

    /// Empty string when nothing is bound.
    QString actionFor(const QKeySequence &sequence) const;

    QStringList warnings() const { return m_warnings; }

private:
    QHash<QKeySequence, QString> m_bindings;
    QStringList m_warnings;
};
