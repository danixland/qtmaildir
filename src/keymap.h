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
