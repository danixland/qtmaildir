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

#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include "keymap.h"

class TestKeyMap : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAreLoaded();
    void iniOverridesDefault();
    void iniAddsNewBinding();
    void chordSequenceParses();
    void unknownActionIsReported();
    void invalidSequenceIsReported();
    void collidingOverridesAreReported();
    void bareCapitalMatchesShiftedPress();
    void userBindingWinsOverDefaultInMenus();
    void defaultsDoNotCollide();
    void everyDefaultIsAKnownAction();
    void everyDefaultParses();
    void completeQueryIsBoundByDefault();
};

void TestKeyMap::completeQueryIsBoundByDefault()
{
    QVERIFY(KeyMap::knownActions().contains(QStringLiteral("complete_query")));
    QCOMPARE(KeyMap::defaultSequenceFor(QStringLiteral("complete_query")),
             QKeySequence(QStringLiteral("Ctrl+Space")));
}

void TestKeyMap::everyDefaultParses()
{
    // A default that does not parse is a dead binding, the failure mode
    // bareCapitalMatchesShiftedPress() covers for user-written keys.
    //
    // This deliberately does NOT try to decide which keys a keyboard can
    // deliver. Whether a symbol needs Shift is a layout property, not a Qt
    // one: Ctrl++ is exactly what the '+' key emits on an Italian layout and
    // is unreachable on a US one, and QTest::keyClick() cannot reproduce
    // either faithfully. A test asserting reachability from synthetic input
    // would encode one layout's habits as a rule for all of them.
    for (const auto &binding : KeyMap::defaultBindings()) {
        const QKeySequence sequence = KeyMap::normalizeSequence(binding.first);
        QVERIFY2(!sequence.isEmpty(),
                 qPrintable(QStringLiteral("default '%1' for %2 does not parse")
                                .arg(binding.first, binding.second)));
    }
}

void TestKeyMap::defaultsAreLoaded()
{
    KeyMap map;
    map.loadDefaults();
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("Ctrl+J"))),
             QStringLiteral("next_thread"));
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("Ctrl+E"))),
             QStringLiteral("archive"));
}

void TestKeyMap::bareCapitalMatchesShiftedPress()
{
    // Typing a capital produces Shift+<key>, but QKeySequence::fromString()
    // discards the case of a bare letter: "N" and "n" both parse to plain
    // Key_N, which no keypress can ever produce. A user who writes "N = flag"
    // would get a binding that silently never fires. Normalizing a bare
    // capital to Shift+<key> is what they meant.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("N"), QStringLiteral("flag"));
        s.endGroup();
    }

    KeyMap map;
    map.loadDefaults();
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    // The sequence a real Shift+N keypress produces.
    QKeyEvent press(QEvent::KeyPress, Qt::Key_N, Qt::ShiftModifier);
    QCOMPARE(map.actionFor(QKeySequence(press.keyCombination())),
             QStringLiteral("flag"));

    // A lowercase binding stays unshifted, so the two remain distinguishable.
    QVERIFY(map.warnings().isEmpty());

    // "y" and "Y" are two different keys, not a collision: the second would
    // have silently displaced the first before normalization.
    QTemporaryDir caseDir;
    const QString casePath = caseDir.filePath(QStringLiteral("case.conf"));
    {
        QSettings s(casePath, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("y"), QStringLiteral("archive"));
        s.setValue(QStringLiteral("Y"), QStringLiteral("delete"));
        s.endGroup();
    }
    KeyMap caseMap;
    QSettings caseSettings(casePath, QSettings::IniFormat);
    caseMap.loadOverrides(caseSettings);

    QKeyEvent lower(QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier);
    QKeyEvent upper(QEvent::KeyPress, Qt::Key_Y, Qt::ShiftModifier);
    QCOMPARE(caseMap.actionFor(QKeySequence(lower.keyCombination())),
             QStringLiteral("archive"));
    QCOMPARE(caseMap.actionFor(QKeySequence(upper.keyCombination())),
             QStringLiteral("delete"));
    QVERIFY(caseMap.warnings().isEmpty());
}

void TestKeyMap::userBindingWinsOverDefaultInMenus()
{
    // loadOverrides() adds a binding without removing the default, so two
    // sequences reach 'archive'. sequenceFor() is what the menus and the
    // shortcut reference display: it must show the user's, not the built-in
    // one they were trying to replace.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("Ctrl+Alt+A"), QStringLiteral("archive"));
        s.endGroup();
    }

    KeyMap map;
    map.loadDefaults();
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    QCOMPARE(map.sequenceFor(QStringLiteral("archive")),
             QKeySequence(QStringLiteral("Ctrl+Alt+A")));

    // The default still fires; it is only no longer the advertised one.
    QCOMPARE(map.actionFor(KeyMap::defaultSequenceFor(QStringLiteral("archive"))),
             QStringLiteral("archive"));

    // An action the user left alone still shows its default.
    QCOMPARE(map.sequenceFor(QStringLiteral("delete")),
             KeyMap::defaultSequenceFor(QStringLiteral("delete")));
}

void TestKeyMap::defaultsDoNotCollide()
{
    // Two defaults on one sequence means one of them is unreachable, and the
    // QHash would silently keep whichever was inserted last.
    KeyMap map;
    map.loadDefaults();

    QSet<QString> actions;
    for (const QString &action : KeyMap::knownActions()) {
        const QKeySequence seq = map.defaultSequenceFor(action);
        if (seq.isEmpty())
            continue;  // Not every action carries a default.
        QVERIFY2(map.actionFor(seq) == action,
                 qPrintable(QStringLiteral("default '%1' for '%2' resolves to '%3'")
                                .arg(seq.toString(), action, map.actionFor(seq))));
        actions.insert(action);
    }
    QVERIFY(!actions.isEmpty());
}

void TestKeyMap::everyDefaultIsAKnownAction()
{
    // A default bound to a name loadOverrides() would reject as unknown.
    KeyMap map;
    map.loadDefaults();
    const QStringList known = KeyMap::knownActions();
    for (const QString &action : known)
        QVERIFY(!action.isEmpty());

    for (const QString &action : map.defaultActions()) {
        QVERIFY2(known.contains(action),
                 qPrintable(QStringLiteral("default binds unknown action '%1'")
                                .arg(action)));
    }
}

void TestKeyMap::iniOverridesDefault()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("Ctrl+J"), QStringLiteral("archive"));
        s.endGroup();
    }

    KeyMap map;
    map.loadDefaults();
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("Ctrl+J"))),
             QStringLiteral("archive"));
    // An untouched default survives.
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("Ctrl+K"))),
             QStringLiteral("prev_thread"));
}

void TestKeyMap::iniAddsNewBinding()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("Ctrl+Shift+A"), QStringLiteral("archive"));
        s.endGroup();
    }

    KeyMap map;
    map.loadDefaults();
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("Ctrl+Shift+A"))),
             QStringLiteral("archive"));
}

void TestKeyMap::chordSequenceParses()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("g,i"), QStringLiteral("focus_query"));
        s.endGroup();
    }

    KeyMap map;
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    const QKeySequence chord = QKeySequence::fromString(QStringLiteral("g,i"));
    QCOMPARE(chord.count(), 2);
    QCOMPARE(map.actionFor(chord), QStringLiteral("focus_query"));
}

void TestKeyMap::unknownActionIsReported()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("z"), QStringLiteral("no_such_action"));
        s.endGroup();
    }

    KeyMap map;
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    // Reported, not fatal, and not bound.
    QCOMPARE(map.warnings().size(), 1);
    QVERIFY(map.warnings().first().contains(QStringLiteral("no_such_action")));
    QVERIFY(map.actionFor(QKeySequence(QStringLiteral("z"))).isEmpty());
}

void TestKeyMap::invalidSequenceIsReported()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("NotAKey++"), QStringLiteral("archive"));
        s.endGroup();
    }

    KeyMap map;
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    QCOMPARE(map.warnings().size(), 1);
}

void TestKeyMap::collidingOverridesAreReported()
{
    // Two spellings of one sequence. "Ctrl+Y" and "ctrl+y" parse identically,
    // so binding both in [keys] is a genuine collision that must not silently
    // drop one.
    //
    // Note "y" and "Y" are NOT a collision any more: normalizeSequence()
    // rewrites a bare capital to Shift+Y, which is the key a user actually
    // presses, leaving the two distinct. Before that they both folded to
    // plain Key_Y and one was lost.
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.conf"));
        {
            QSettings s(path, QSettings::IniFormat);
            s.beginGroup(QStringLiteral("keys"));
            s.setValue(QStringLiteral("Ctrl+Y"), QStringLiteral("archive"));
            s.setValue(QStringLiteral("ctrl+y"), QStringLiteral("delete"));
            s.endGroup();
        }

        KeyMap map;
        QSettings s(path, QSettings::IniFormat);
        map.loadOverrides(s);

        QCOMPARE(map.warnings().size(), 1);
        QVERIFY(map.warnings().first().contains(QStringLiteral("archive")));
        QVERIFY(map.warnings().first().contains(QStringLiteral("delete")));
    }

    // Overriding a default is not a collision: loadDefaults() puts 'j' in
    // the map first, then the override pass rebinds the same 'j'. That must
    // stay silent (regression check for iniOverridesDefault's scenario).
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.conf"));
        {
            QSettings s(path, QSettings::IniFormat);
            s.beginGroup(QStringLiteral("keys"));
            s.setValue(QStringLiteral("Ctrl+J"), QStringLiteral("archive"));
            s.endGroup();
        }

        KeyMap map;
        map.loadDefaults();
        QSettings s(path, QSettings::IniFormat);
        map.loadOverrides(s);

        QCOMPARE(map.warnings().size(), 0);
    }
}

QTEST_MAIN(TestKeyMap)
#include "test_keymap.moc"
