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
        QStringLiteral("restore"),
        QStringLiteral("cleanup_stranded"),
        // Item 118. No default binding, deliberately: this is the one action
        // that destroys mail with no undo, and a chord is how it would be run
        // by accident. Menu only, which item 132 made a legitimate choice.
        QStringLiteral("empty_trash"),
        // Item 185. Empty trash's sibling, scoped to the selection, and it
        // carries no default binding for exactly the same reason: the act is
        // identical and so is the hazard.
        QStringLiteral("purge"),
        QStringLiteral("spam"),
        QStringLiteral("toggle_unread"),
        QStringLiteral("mark_all_read"),
        QStringLiteral("edit_tags"),
        QStringLiteral("tag_rules"),
        QStringLiteral("flag"),
        // Compose and send (item 123). save_message deliberately carries no
        // default chord: since item 132 a shortcut is a chosen subset rather
        // than a requirement, and writing the raw message to a file is the
        // rarely-used escape hatch. Menu reachability is the rule that holds.
        QStringLiteral("compose"),
        QStringLiteral("reply"),
        QStringLiteral("reply_all"),
        QStringLiteral("reply_no_quote"),
        QStringLiteral("forward"),
        QStringLiteral("edit_draft"),
        QStringLiteral("save_message"),
        QStringLiteral("focus_query"),
        QStringLiteral("complete_query"),
        QStringLiteral("save_query"),
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
        // Compose and send (item 123), listed where the Message menu presents
        // them: composing sits above organising.
        //
        // PROVISIONAL. The user intends to rework the bindings.
        //
        // Each was checked against every sequence in this table, not merely
        // against the lines above it: these sit near the top, so most of the
        // table is BELOW them, Ctrl+Shift+U and Ctrl+Shift+S among it.
        // Checking only upwards would miss exactly those. The near misses are
        // Ctrl+R for restore and Ctrl+A for select_all, so none of these five
        // is a reuse.
        //
        // save_message gets none. Item 132 made a chord a chosen subset rather
        // than a requirement, and this is the escape hatch nobody presses a
        // key for.
        { QStringLiteral("Ctrl+N"),       QStringLiteral("compose") },
        { QStringLiteral("Ctrl+Shift+R"), QStringLiteral("reply") },
        { QStringLiteral("Ctrl+Shift+A"), QStringLiteral("reply_all") },
        { QStringLiteral("Ctrl+Alt+R"),   QStringLiteral("reply_no_quote") },
        { QStringLiteral("Ctrl+Shift+F"), QStringLiteral("forward") },
        { QStringLiteral("Ctrl+E"),       QStringLiteral("archive") },
        // Del FIRST, and the order matters twice over. defaultSequenceFor()
        // returns the first match, and sequenceFor() prefers any binding that
        // is not that default, treating it as a user override; listing Del
        // second therefore made it the "override" of Ctrl+D and left the two
        // functions disagreeing about which key the menus should advertise.
        // First also makes it the ADVERTISED one, which is the point: it is
        // the key a user reaches for, and Ctrl+D is not a guess anyone makes.
        //
        // Bare, which is safe for a reason that does NOT generalise to other
        // bare keys. Delete is not a letter, so Qt's protection for editable
        // widgets does not cover it, but QLineEdit accepts the
        // ShortcutOverride for Delete itself, because it is one of its own
        // editing keys. Return is not, which is why that one needed an
        // explicit filter in MainWindow::eventFilter() and this one does not.
        // Measured both ways; see theDeleteKeyEditsTextInTheQueryBar().
        { QStringLiteral("Del"),          QStringLiteral("delete") },
        { QStringLiteral("Ctrl+D"),       QStringLiteral("delete") },
        // Restore is only enabled in the trash view, so its key is dead
        // elsewhere rather than doing something surprising.
        { QStringLiteral("Ctrl+R"),       QStringLiteral("restore") },
        // Item 103's cleanup. A chord rather than a plain key: it replaces the
        // whole view, and it is reached from a menu far more often than from
        // the keyboard. Ctrl+Shift+D is message_details, so this takes the T
        // of "trash".
        { QStringLiteral("Ctrl+Alt+T"),   QStringLiteral("cleanup_stranded") },
        { QStringLiteral("Ctrl+Shift+S"), QStringLiteral("spam") },
        { QStringLiteral("Ctrl+U"),       QStringLiteral("toggle_unread") },
        // Shifted against Ctrl+U, which toggles unread on the selection: this
        // is the same idea applied to the whole view, and the wider-reaching
        // action takes the harder chord rather than the easier one.
        { QStringLiteral("Ctrl+Shift+U"), QStringLiteral("mark_all_read") },
        { QStringLiteral("Ctrl+I"),       QStringLiteral("flag") },
        { QStringLiteral("Ctrl+T"),       QStringLiteral("edit_tags") },
        // Shifted against Ctrl+T for the same reason Ctrl+Shift+U is shifted
        // against Ctrl+U: this is the standing version of tagging, applied to
        // every message that arrives rather than to the selection, so it takes
        // the harder chord.
        { QStringLiteral("Ctrl+Shift+T"), QStringLiteral("tag_rules") },
        { QStringLiteral("Ctrl+L"),       QStringLiteral("focus_query") },
        // Ctrl+Space is the completion idiom users already carry over from
        // shells and editors, and it is a named key rather than a symbol, so
        // no layout has to shift it.
        { QStringLiteral("Ctrl+Space"),   QStringLiteral("complete_query") },
        // The conventional save key, and free here: nothing in this window
        // saves a document, so Ctrl+S is unclaimed and means what a user
        // expects it to.
        { QStringLiteral("Ctrl+S"),       QStringLiteral("save_query") },
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

        const bool isBuiltIn = isDefaultBinding(it.key(), action);
        if (best.isEmpty()) {
            best = it.key();
            bestIsBuiltIn = isBuiltIn;
            continue;
        }
        // A user binding always beats the default.
        if (bestIsBuiltIn && !isBuiltIn) {
            best = it.key();
            bestIsBuiltIn = false;
        } else if (bestIsBuiltIn && isBuiltIn) {
            // Both are defaults, so the ADVERTISED one is whichever
            // defaultBindings() lists first: that order is the author's
            // preference and is why Del is listed before Ctrl+D. Falling back
            // to alphabetical here would advertise Ctrl+D instead.
            if (it.key() == builtIn)
                best = it.key();
        } else if (bestIsBuiltIn == isBuiltIn
                   && it.key().toString() < best.toString()) {
            best = it.key();
        }
    }
    return best;
}

bool KeyMap::isDefaultBinding(const QKeySequence &sequence,
                             const QString &action)
{
    // ANY of the action's defaults, not just the first.
    //
    // An action can ship with more than one binding: `delete` has Del and
    // Ctrl+D. sequenceFor() compares against defaultSequenceFor(), which
    // returns only the first, so the second looked like a USER binding and
    // won the "a user binding always beats the default" rule. The menus then
    // advertised Ctrl+D for a user who had configured nothing, and
    // sequenceFor() and defaultSequenceFor() disagreed about an untouched
    // action.
    for (const auto &binding : defaultBindings()) {
        if (binding.second == action
            && normalizeSequence(binding.first) == sequence) {
            return true;
        }
    }
    return false;
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
                tr("Unparseable key sequence '%1' in [keys]").arg(key));
            continue;
        }

        if (!known.contains(action)) {
            m_warnings.append(
                tr("Unknown action '%1' bound to '%2' in [keys]")
                    .arg(action, key));
            continue;
        }

        const auto previous = seenThisPass.constFind(sequence);
        if (previous != seenThisPass.constEnd()) {
            m_warnings.append(
                tr("Key sequence '%1' bound to both '%2' and '%3' "
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
