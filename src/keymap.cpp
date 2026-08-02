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

        m_bindings.insert(sequence, action);
    }
    settings.endGroup();
}

QString KeyMap::actionFor(const QKeySequence &sequence) const
{
    return m_bindings.value(sequence);
}
