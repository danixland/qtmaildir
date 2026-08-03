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

#include <QAction>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "config.h"
#include "keymap.h"
#include "mainwindow.h"

/// MainWindow is mostly wiring, and the parts that need a real database are
/// still verified manually. What is checked here is the action registry: the
/// bindings a user configures reach the QActions the menus and the keyboard
/// both read from, and no action is left unreachable.
class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void everyKnownActionIsRegistered();
    void everyRegisteredActionIsKnown();
    void everyActionHasAShortcut();
    void configuredBindingReachesTheAction();
    void cidPrefixesAreBangFree();
    void cidPrefixesAreDistinctPerMessage();
    void uiStateIsNotWrittenIntoTheUserConfig();
    void uiStateSurvivesARestart();
    void missingUiStateLeavesTheDefaults();
};

void TestMainWindow::everyKnownActionIsRegistered()
{
    // KeyMap::knownActions() is what loadOverrides() validates config bindings
    // against. An action listed there but never registered means a user can
    // bind a key in qtmaildir.conf, get no warning, and have it do nothing.
    //
    // registeredActionNames() is now derived from the QActions themselves, so
    // this compares against what the window really installed.
    const Config config;
    MainWindow window(config);

    const QStringList known = KeyMap::knownActions();
    const QStringList registered = window.registeredActionNames();

    for (const QString &action : known) {
        QVERIFY2(registered.contains(action),
                 qPrintable(QStringLiteral("known action '%1' is never registered "
                                           "by MainWindow").arg(action)));
    }
}

void TestMainWindow::everyRegisteredActionIsKnown()
{
    // The reverse drift: an action MainWindow implements but KeyMap rejects.
    // The user would get "unknown action" for a binding that is really there.
    const Config config;
    MainWindow window(config);

    const QStringList known = KeyMap::knownActions();
    const QStringList registered = window.registeredActionNames();

    for (const QString &action : registered) {
        QVERIFY2(known.contains(action),
                 qPrintable(QStringLiteral("registered action '%1' is not in "
                                           "KeyMap::knownActions()").arg(action)));
    }
}

void TestMainWindow::everyActionHasAShortcut()
{
    // An action with no binding is unreachable from the keyboard. Every one
    // of them carries a default, so an empty shortcut means the default table
    // and the action list have drifted apart.
    const Config config;
    MainWindow window(config);

    for (const QString &name : window.registeredActionNames()) {
        const QAction *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(QStringLiteral("no QAction named '%1'").arg(name)));
        QVERIFY2(!action->shortcut().isEmpty(),
                 qPrintable(QStringLiteral("action '%1' has no shortcut").arg(name)));
    }
}

void TestMainWindow::configuredBindingReachesTheAction()
{
    // The whole point of [keys]: a user's override must end up on the QAction,
    // which is what both the keyboard and the menus read.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("keys"));
        s.setValue(QStringLiteral("Ctrl+Alt+A"), QStringLiteral("archive"));
        s.endGroup();
    }

    // MainWindow reads its keymap from Config::defaultPath(), so point that
    // at the temporary file for this test.
    const QString previous = qEnvironmentVariable("XDG_CONFIG_HOME");
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("qtmaildir"))));
    QVERIFY(QFile::copy(path, dir.filePath(QStringLiteral("qtmaildir/qtmaildir.conf"))));
    qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

    {
        const Config config;
        MainWindow window(config);
        const QAction *archive =
            window.findChild<QAction *>(QStringLiteral("archive"));
        QVERIFY(archive);
        QCOMPARE(archive->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+A")));
    }

    if (previous.isEmpty())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", previous.toUtf8());
}

void TestMainWindow::cidPrefixesAreBangFree()
{
    // MainWindow is the only producer of cidPrefix in the application. The
    // '!' separator that keeps two messages' cid: references apart is only
    // unambiguous while the prefix half contains none.
    for (int i : { 0, 1, 9, 10, 99, 1000 }) {
        const QString prefix = MainWindow::cidPrefixForIndex(i);
        QVERIFY(!prefix.isEmpty());
        QVERIFY2(!prefix.contains(QLatin1Char('!')),
                 qPrintable(QStringLiteral("prefix '%1' contains '!'").arg(prefix)));
    }
}

void TestMainWindow::cidPrefixesAreDistinctPerMessage()
{
    // Two messages sharing a prefix would share a cid: namespace, which is the
    // collision the namespacing exists to prevent.
    QSet<QString> seen;
    for (int i = 0; i < 200; ++i) {
        const QString prefix = MainWindow::cidPrefixForIndex(i);
        QVERIFY2(!seen.contains(prefix),
                 qPrintable(QStringLiteral("prefix '%1' repeats").arg(prefix)));
        seen.insert(prefix);
    }
}

void TestMainWindow::uiStateIsNotWrittenIntoTheUserConfig()
{
    // The config file is hand-edited and must never gain a base64 geometry
    // blob, nor be rewritten on exit: QSettings preserves neither comments nor
    // key order, so writing it would quietly destroy the user's formatting.
    QVERIFY(MainWindow::uiStatePath() != Config::defaultPath());

    // One qtmaildir component, not two. QStandardPaths::StateLocation appends
    // both the organization and the application name, and here both are
    // "qtmaildir", so using it nests the directory inside itself.
    QCOMPARE(MainWindow::uiStatePath().count(QStringLiteral("/qtmaildir/")), 1);
    QVERIFY(MainWindow::uiStatePath().endsWith(
        QStringLiteral("/qtmaildir/uistate.conf")));
}

void TestMainWindow::uiStateSurvivesARestart()
{
    // Test mode redirects QStandardPaths at the process level, so the state
    // file lands in a scratch directory rather than the real ~/.local/state.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MainWindow::uiStatePath());

    const QSize resized(940, 620);
    {
        const Config config;
        MainWindow window(config);
        window.resize(resized);
        window.close();  // closeEvent() is what persists the state
    }

    QVERIFY2(QFile::exists(MainWindow::uiStatePath()),
             qPrintable(QStringLiteral("no state file at %1")
                            .arg(MainWindow::uiStatePath())));

    const Config config;
    MainWindow reopened(config);
    QCOMPARE(reopened.size(), resized);

    QFile::remove(MainWindow::uiStatePath());
    QStandardPaths::setTestModeEnabled(false);
}

void TestMainWindow::missingUiStateLeavesTheDefaults()
{
    // A restore that silently succeeded on an empty blob would give a
    // zero-size window on first launch. Absent state must be a no-op.
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MainWindow::uiStatePath());

    const Config config;
    MainWindow window(config);
    QCOMPARE(window.size(), QSize(1200, 800));

    QStandardPaths::setTestModeEnabled(false);
}

// Constructing a MainWindow needs a QApplication and a platform plugin. The
// test has no display under ctest, so it runs offscreen unless the caller
// asked for something else.
int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", qgetenv("QT_QPA_PLATFORM").isEmpty()
                                   ? QByteArray("offscreen")
                                   : qgetenv("QT_QPA_PLATFORM"));
    QApplication app(argc, argv);
    TestMainWindow test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_mainwindow.moc"
