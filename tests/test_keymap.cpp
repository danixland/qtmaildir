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
};

void TestKeyMap::defaultsAreLoaded()
{
    KeyMap map;
    map.loadDefaults();
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("j"))),
             QStringLiteral("next_thread"));
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("a"))),
             QStringLiteral("archive"));
}

void TestKeyMap::iniOverridesDefault()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("j"), QStringLiteral("archive"));
        s.endGroup();
    }

    KeyMap map;
    map.loadDefaults();
    QSettings s(path, QSettings::IniFormat);
    map.loadOverrides(s);

    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("j"))),
             QStringLiteral("archive"));
    // An untouched default survives.
    QCOMPARE(map.actionFor(QKeySequence(QStringLiteral("k"))),
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

QTEST_MAIN(TestKeyMap)
#include "test_keymap.moc"
