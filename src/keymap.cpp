#include "keymap.h"

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
        QStringLiteral("flag"),
        QStringLiteral("focus_query"),
        QStringLiteral("toggle_html"),
        QStringLiteral("load_remote"),
        QStringLiteral("undo"),
        QStringLiteral("sync"),
        QStringLiteral("quit"),
    };
}

void KeyMap::loadDefaults()
{
    const QHash<QString, QString> defaults = {
        { QStringLiteral("j"),      QStringLiteral("next_thread") },
        { QStringLiteral("k"),      QStringLiteral("prev_thread") },
        { QStringLiteral("Return"), QStringLiteral("open_thread") },
        { QStringLiteral("a"),      QStringLiteral("archive") },
        { QStringLiteral("d"),      QStringLiteral("delete") },
        { QStringLiteral("N"),      QStringLiteral("toggle_unread") },
        { QStringLiteral("F"),      QStringLiteral("flag") },
        { QStringLiteral("/"),      QStringLiteral("focus_query") },
        { QStringLiteral("h"),      QStringLiteral("toggle_html") },
        { QStringLiteral("u"),      QStringLiteral("undo") },
        { QStringLiteral("G"),      QStringLiteral("sync") },
        { QStringLiteral("Ctrl+Q"), QStringLiteral("quit") },
    };

    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
        m_bindings.insert(QKeySequence::fromString(it.key()), it.value());
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

        const QKeySequence sequence = QKeySequence::fromString(key);
        // QKeySequence::fromString() does not return an empty sequence for
        // unparseable input; it returns a non-empty sequence whose
        // toString() is empty (verified on Qt 6.11). Use that to detect
        // garbage input instead.
        if (sequence.isEmpty() || sequence.toString().isEmpty()) {
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
