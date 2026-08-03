#include <QtTest>

#include "keymap.h"
#include "mainwindow.h"

/// MainWindow is mostly wiring and needs a live QApplication plus a real
/// database, so it is verified manually in Task 13. Two things do not need
/// either, and both are the kind of drift a comment alone does not prevent.
class TestMainWindow : public QObject
{
    Q_OBJECT
private slots:
    void everyKnownActionIsRegistered();
    void everyRegisteredActionIsKnown();
    void cidPrefixesAreBangFree();
    void cidPrefixesAreDistinctPerMessage();
};

void TestMainWindow::everyKnownActionIsRegistered()
{
    // KeyMap::knownActions() is what loadOverrides() validates config bindings
    // against. An action listed there but never registered means a user can
    // bind a key in qtmaildir.conf, get no warning, and have it do nothing.
    const QStringList known = KeyMap::knownActions();
    const QStringList registered = MainWindow::registeredActionNames();

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
    const QStringList known = KeyMap::knownActions();
    const QStringList registered = MainWindow::registeredActionNames();

    for (const QString &action : registered) {
        QVERIFY2(known.contains(action),
                 qPrintable(QStringLiteral("registered action '%1' is not in "
                                           "KeyMap::knownActions()").arg(action)));
    }
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

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
