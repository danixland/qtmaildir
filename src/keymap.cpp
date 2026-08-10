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

#include "keymap.h"

#include <algorithm>

#include <QSettings>

QStringList KeyMap::knownActions()
{
    // Keep in sync with the actions MainWindow registers.
    return {
        QStringLiteral("next_thread"),
        QStringLiteral("prev_thread"),
        QStringLiteral("open_thread"),
        QStringLiteral("archive"),
        QStringLiteral("delete"),
        QStringLiteral("spam"),
        QStringLiteral("toggle_unread"),
        QStringLiteral("mark_all_read"),
        QStringLiteral("edit_tags"),
        QStringLiteral("flag"),
        QStringLiteral("focus_query"),
        QStringLiteral("complete_query"),
        QStringLiteral("select_all"),
        QStringLiteral("clear_pane"),
        QStringLiteral("clear_selection"),
        QStringLiteral("toggle_html"),
        QStringLiteral("load_remote"),
        QStringLiteral("message_details"),
        QStringLiteral("zoom_in"),
        QStringLiteral("zoom_out"),
        QStringLiteral("zoom_reset"),
        QStringLiteral("undo"),
        QStringLiteral("sync"),
        QStringLiteral("quit"),
    };
}

QList<QPair<QString, QString>> KeyMap::defaultBindings()
{
    // Modifier shortcuts throughout, rather than the bare letters of 0.1.0.
    // Two reasons. A bare capital never worked: "N" parses to plain Key_N
    // while typing a capital emits Shift+N, so toggle_unread, flag and sync
    // were dead keys. And a single letter cannot be a QAction shortcut in a
    // menu without stealing that letter from every text field in the window.
    //
    // Ordered as the menus present them; a QList keeps that order, which a
    // QHash would not.
    return {
        { QStringLiteral("Ctrl+J"),       QStringLiteral("next_thread") },
        { QStringLiteral("Ctrl+K"),       QStringLiteral("prev_thread") },
        // Alt, because Shift+Up/Down is QTreeView's built-in extend-selection,
        // which multi-row tagging depends on, and plain Up/Down is the view's
        // own navigation, which already steps INTO an expanded thread's
        // replies and is what gives message-to-message movement for free.
        //
        // These must stay chords. Every action is a QAction with
        // WindowShortcut, dispatched before the focused widget sees the key,
        // and Qt withholds only plain LETTERS from editable widgets: a bare
        // Up bound here would break the arrow keys in the query bar, the tag
        // dialog and the web view at once, exactly as Return did.
        { QStringLiteral("Alt+Down"),      QStringLiteral("next_thread") },
        { QStringLiteral("Alt+Up"),        QStringLiteral("prev_thread") },
        { QStringLiteral("Return"),       QStringLiteral("open_thread") },
        { QStringLiteral("Ctrl+E"),       QStringLiteral("archive") },
        { QStringLiteral("Ctrl+D"),       QStringLiteral("delete") },
        { QStringLiteral("Ctrl+Shift+S"), QStringLiteral("spam") },
        { QStringLiteral("Ctrl+U"),       QStringLiteral("toggle_unread") },
        // Shifted against Ctrl+U, which toggles unread on the selection: this
        // is the same idea applied to the whole view, and the wider-reaching
        // action takes the harder chord rather than the easier one.
        { QStringLiteral("Ctrl+Shift+U"), QStringLiteral("mark_all_read") },
        { QStringLiteral("Ctrl+I"),       QStringLiteral("flag") },
        { QStringLiteral("Ctrl+T"),       QStringLiteral("edit_tags") },
        { QStringLiteral("Ctrl+L"),       QStringLiteral("focus_query") },
        // Ctrl+Space is the completion idiom users already carry over from
        // shells and editors, and it is a named key rather than a symbol, so
        // no layout has to shift it.
        { QStringLiteral("Ctrl+Space"),   QStringLiteral("complete_query") },
        // The conventional select-all key, and free here: the thread list is a
        // read-only view, so nothing else in the window wants it.
        { QStringLiteral("Ctrl+A"),       QStringLiteral("select_all") },
        // Escape is not claimed by anything else at window level. The query
        // completer handles its own Escape while its popup is up, and a popup
        // consumes the key before a window shortcut sees it.
        //
        // It clears the SELECTION as well as the pane (item 50). Deselecting is
        // what Escape means nearly everywhere else, and blanking a pane while
        // leaving the row highlighted reads as half an action.
        //
        // clear_pane keeps the narrower behaviour on Shift+Esc: same key, and
        // the modifier reads as "less than the plain one". It needs SOME
        // default rather than being left unbound, since every action carries
        // one and everyActionHasAShortcut enforces exactly that.
        { QStringLiteral("Esc"),          QStringLiteral("clear_selection") },
        { QStringLiteral("Shift+Esc"),    QStringLiteral("clear_pane") },
        { QStringLiteral("Ctrl+H"),       QStringLiteral("toggle_html") },
        { QStringLiteral("Ctrl+M"),       QStringLiteral("load_remote") },
        // Shifted because Ctrl+D is delete. Both are "D for details/delete"
        // words, and the destructive one keeps the unshifted key it already
        // had rather than being moved to make room.
        { QStringLiteral("Ctrl+Shift+D"), QStringLiteral("message_details") },
        // Ctrl++ is what the '+' key really delivers on a layout where '+' is
        // unshifted, an Italian one among them, confirmed against the actual
        // keyboard. QTest::keyClick() cannot reproduce it, so a synthetic-input
        // probe wrongly reports this binding as dead; do not "fix" it on that
        // evidence. A US layout, where '+' is Shift+'=', wants Ctrl+Shift+= in
        // [keys] instead.
        { QStringLiteral("Ctrl++"),       QStringLiteral("zoom_in") },
        { QStringLiteral("Ctrl+-"),       QStringLiteral("zoom_out") },
        { QStringLiteral("Ctrl+0"),       QStringLiteral("zoom_reset") },
        { QStringLiteral("Ctrl+Z"),       QStringLiteral("undo") },
        { QStringLiteral("Ctrl+G"),       QStringLiteral("sync") },
        { QStringLiteral("Ctrl+Q"),       QStringLiteral("quit") },
    };
}

QStringList KeyMap::defaultActions()
{
    QStringList actions;
    const auto bindings = defaultBindings();
    actions.reserve(bindings.size());
    for (const auto &binding : bindings)
        actions.append(binding.second);
    return actions;
}

QKeySequence KeyMap::normalizeSequence(const QString &text)
{
    const QKeySequence sequence = QKeySequence::fromString(text);

    // fromString() does not return an empty sequence for unparseable input;
    // it returns a non-empty one whose toString() is empty (verified on
    // Qt 6.11). Both checks are needed to detect garbage.
    if (sequence.isEmpty() || sequence.toString().isEmpty())
        return {};

    // A bare uppercase letter, no modifiers: the user wrote "N" meaning the
    // key they press to type a capital N, which is Shift+N. fromString()
    // folded the case away, so put the Shift back.
    if (text.size() == 1 && text.at(0).isUpper() && text.at(0).isLetter())
        return QKeySequence(sequence[0].key() | Qt::SHIFT);

    return sequence;
}

void KeyMap::loadDefaults()
{
    for (const auto &binding : defaultBindings())
        m_bindings.insert(normalizeSequence(binding.first), binding.second);
}

QList<QKeySequence> KeyMap::sequencesFor(const QString &action) const
{
    const QKeySequence primary = sequenceFor(action);
    if (primary.isEmpty())
        return {};

    QList<QKeySequence> all{ primary };
    QList<QKeySequence> rest;
    for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it) {
        if (it.value() == action && it.key() != primary)
            rest.append(it.key());
    }
    // QHash iteration order is unspecified, so the tail is sorted rather than
    // left to chance: an action's shortcut list must not reorder between runs.
    std::sort(rest.begin(), rest.end(),
              [](const QKeySequence &a, const QKeySequence &b) {
                  return a.toString() < b.toString();
              });
    all += rest;
    return all;
}

QKeySequence KeyMap::sequenceFor(const QString &action) const
{
    // Several sequences can reach one action: the built-in default, which
    // loadOverrides() does not remove, plus whatever the user added. Their
    // binding is the one to show and to put on the QAction, or configuring
    // "Ctrl+Alt+A = archive" would leave the menu still advertising Ctrl+E.
    //
    // QHash iteration order is unspecified, so ties are broken on the text
    // rather than left to chance.
    const QKeySequence builtIn = defaultSequenceFor(action);
    QKeySequence best;
    bool bestIsBuiltIn = false;

    for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it) {
        if (it.value() != action)
            continue;

        const bool isBuiltIn = !builtIn.isEmpty() && it.key() == builtIn;
        if (best.isEmpty()) {
            best = it.key();
            bestIsBuiltIn = isBuiltIn;
            continue;
        }
        // A user binding always beats the default.
        if (bestIsBuiltIn && !isBuiltIn) {
            best = it.key();
            bestIsBuiltIn = false;
        } else if (bestIsBuiltIn == isBuiltIn
                   && it.key().toString() < best.toString()) {
            best = it.key();
        }
    }
    return best;
}

QKeySequence KeyMap::defaultSequenceFor(const QString &action)
{
    for (const auto &binding : defaultBindings()) {
        if (binding.second == action)
            return normalizeSequence(binding.first);
    }
    return {};
}

void KeyMap::loadOverrides(QSettings &settings)
{
    const QStringList known = knownActions();

    // Sequences bound so far *within this override pass*. Defaults already
    // sit in m_bindings before this runs, so a plain m_bindings.contains()
    // check would misfire on every legitimate override of a default (e.g.
    // "j=archive" overriding the default 'j' binding). Only a collision
    // between two entries in this same pass (e.g. two INI keys that
    // normalize to the same QKeySequence, such as "y" and "Y") is a bug.
    QHash<QKeySequence, QString> seenThisPass;

    settings.beginGroup(QStringLiteral("keys"));
    const QStringList keys = settings.childKeys();
    for (const QString &key : keys) {
        const QString action = settings.value(key).toString();

        // Shares the defaults' normalization, so a hand-written "N" binds the
        // key the user actually presses rather than one nothing emits.
        const QKeySequence sequence = normalizeSequence(key);
        if (sequence.isEmpty()) {
            m_warnings.append(
                QStringLiteral("Unparseable key sequence '%1' in [keys]").arg(key));
            continue;
        }

        if (!known.contains(action)) {
            m_warnings.append(
                QStringLiteral("Unknown action '%1' bound to '%2' in [keys]")
                    .arg(action, key));
            continue;
        }

        const auto previous = seenThisPass.constFind(sequence);
        if (previous != seenThisPass.constEnd()) {
            m_warnings.append(
                QStringLiteral("Key sequence '%1' bound to both '%2' and '%3' "
                                "in [keys]; keeping '%2'")
                    .arg(key, previous.value(), action));
            continue;
        }
        seenThisPass.insert(sequence, action);

        m_bindings.insert(sequence, action);
    }
    settings.endGroup();
}

QString KeyMap::actionFor(const QKeySequence &sequence) const
{
    return m_bindings.value(sequence);
}
