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

#include <QCoreApplication>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QPair>
#include <QStringList>

class QSettings;

/// Maps key sequences to action names. Action names are plain strings so this
/// class has no dependency on the widgets that implement the actions.
class KeyMap
{
    // Not a QObject, so tr() comes from here. Its warnings are user-facing:
    // MainWindow joins them with Config's into the status label and the
    // "Configuration problems" modal.
    Q_DECLARE_TR_FUNCTIONS(KeyMap)

public:
    /// Every action name the application understands. loadOverrides() rejects
    /// anything not in this set, so a typo in the config cannot bind silently.
    static QStringList knownActions();

    /// The built-in bindings, in menu order: {sequence, action}. The single
    /// source of truth for the defaults, so the menus, the shortcut reference
    /// and loadDefaults() cannot disagree about them.
    static QList<QPair<QString, QString>> defaultBindings();

    void loadDefaults();

    /// Reads the [keys] group. Invalid sequences and unknown action names are
    /// collected into warnings() rather than throwing or aborting.
    void loadOverrides(QSettings &settings);

    /// Empty string when nothing is bound.
    QString actionFor(const QKeySequence &sequence) const;

    /// The sequence currently bound to an action, empty if none. The reverse
    /// of actionFor(): menus need a shortcut for an action they already know.
    /// When several sequences are bound to one action, returns the shortest
    /// text, so the menu shows a stable choice rather than a hash-order one.
    QKeySequence sequenceFor(const QString &action) const;

    /// EVERY sequence bound to an action, with sequenceFor()'s choice first.
    ///
    /// An action can have more than one binding, and setShortcut() keeps only
    /// the last: next_thread ships with both Ctrl+J and Alt+Down, and with the
    /// singular setter whichever arrived second was silently unreachable.
    /// Ordered rather than hash-ordered, so the menu still advertises the same
    /// binding sequenceFor() chose.
    QList<QKeySequence> sequencesFor(const QString &action) const;

    /// The built-in sequence for an action, ignoring any user override.
    static QKeySequence defaultSequenceFor(const QString &action);

    /// Whether `sequence` is ANY of `action`'s default bindings.
    ///
    /// Not the same question as `sequence == defaultSequenceFor(action)`: an
    /// action can ship several, and comparing against only the first makes the
    /// others look like user overrides.
    static bool isDefaultBinding(const QKeySequence &sequence,
                                 const QString &action);

    /// Every action name carrying a built-in binding.
    static QStringList defaultActions();

    /// Normalizes a configured key string into the sequence a real keypress
    /// produces. QKeySequence::fromString() discards the case of a bare
    /// letter, so "N" parses to plain Key_N, which no keystroke ever emits:
    /// typing a capital sends Shift+N. A bare uppercase letter is therefore
    /// rewritten to Shift+<letter>. Returns an empty sequence for input
    /// fromString() cannot parse.
    static QKeySequence normalizeSequence(const QString &text);

    QStringList warnings() const { return m_warnings; }

private:
    QHash<QKeySequence, QString> m_bindings;
    QStringList m_warnings;
};
