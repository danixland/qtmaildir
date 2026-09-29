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

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTranslator>
#include <QWebEngineUrlScheme>

#include <notmuch.h>

#include <cstdio>
#include <cstring>

#include "config.h"
#include "launchselectors.h"
#include "mainwindow.h"
#include "singleinstance.h"
#include "version.h"

int main(int argc, char *argv[])
{
    // Answered before anything heavier starts: registering web engine schemes
    // and constructing a QApplication to print one line would be absurd, and
    // --version has to work on a machine where the GUI cannot open at all.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0
            || std::strcmp(argv[i], "-v") == 0) {
            std::printf("qtmaildir %s\n", QTMAILDIR_VERSION_DISPLAY);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0
            || std::strcmp(argv[i], "-h") == 0) {
            // The text lives with the parser, so the options and their
            // descriptions cannot drift apart.
            std::printf("%s",
                        LaunchSelectors::helpText(
                            QStringLiteral(QTMAILDIR_VERSION_DISPLAY))
                            .toLocal8Bit()
                            .constData());
            return 0;
        }
    }

    // Custom schemes must be registered before QApplication is constructed.
    {
        QWebEngineUrlScheme scheme(QByteArrayLiteral("cid"));
        scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                        | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
        QWebEngineUrlScheme::registerScheme(scheme);
    }
    {
        QWebEngineUrlScheme scheme(QByteArrayLiteral("qtmaildir"));
        scheme.setFlags(QWebEngineUrlScheme::SecureScheme);
        QWebEngineUrlScheme::registerScheme(scheme);
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qtmaildir"));
    app.setOrganizationName(QStringLiteral("qtmaildir"));
    app.setApplicationVersion(QStringLiteral(QTMAILDIR_VERSION));

    // Parsed AFTER QApplication, from app.arguments() rather than argv: the
    // constructor consumes Qt's own options (-platform, -style and the rest)
    // and removes them, so parsing the raw argv would reject a standard Qt
    // option as unknown. No window exists yet, so a bad option still exits
    // before anything is shown.
    QString selectorError;
    const LaunchSelectors selectors =
        LaunchSelectors::parse(app.arguments(), &selectorError);
    if (!selectorError.isEmpty()) {
        std::fprintf(stderr, "qtmaildir: %s\n",
                     selectorError.toLocal8Bit().constData());
        return 2;
    }

    // Connect first, become the server only if that fails. A live instance is
    // handed the selectors and this process exits without ever opening a
    // database: notmuch permits one handle per process, so two windows are two
    // handles, which this avoids as a side effect of the feature.
    //
    // On main's stack, like the QTranslator below: it owns the socket for the
    // life of the process and must outlive exec().
    const QString socketPath = MainWindow::singleInstanceSocketPath();
    // The state directory does not exist on a first run, and listen() cannot
    // create a socket in a missing directory.
    QDir().mkpath(QFileInfo(socketPath).absolutePath());
    SingleInstance instance(socketPath);
    if (!instance.tryBecomeServer()) {
        if (instance.sendToRunningInstance(selectors))
            return 0;
        // No running instance answered and no socket could be created either.
        // Carry on and open a window: losing single-instance behaviour is a
        // degradation, and losing the mail client is not acceptable.
    }

    // Compiled in rather than read from disk, so the icon is there whether or
    // not the app was installed. setDesktopFileName() is what lets a Wayland
    // compositor match the window to its .desktop entry, which is where the
    // taskbar icon really comes from there.
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/qtmaildir.svg")));
    app.setDesktopFileName(QStringLiteral("qtmaildir"));

    Config config;
    config.load(Config::defaultPath());

    // On main's stack deliberately: a QTranslator must outlive exec(), and one
    // scoped to a helper function unloads on return, silently reverting every
    // string to English.
    //
    // Loaded AFTER Config, because [general] language overrides the
    // environment. The cost is that config warnings are generated before the
    // translator exists, so they are built in English; retranslating them would
    // mean re-running load(), and a warning about the config file is the one
    // string a user can still act on in either language.
    //
    // An empty language() means follow the environment, which is what QLocale()
    // default-constructs to. A missing .qm returns false and the app runs in
    // English: that is the correct outcome both for an unsupported language and
    // for `language = en_US`, since English is the source and ships no .qm.
    const QLocale locale = config.language().isEmpty()
                               ? QLocale()
                               : QLocale(config.language());

    QTranslator translator;
    QStringList translationDirs;
    // Beside the binary first, so a build tree works without installing.
    translationDirs << QCoreApplication::applicationDirPath()
                           + QStringLiteral("/translations");
    const QStringList dataDirs =
        QStandardPaths::standardLocations(QStandardPaths::AppDataLocation);
    for (const QString &dir : dataDirs)
        translationDirs << dir + QStringLiteral("/translations");

    for (const QString &dir : std::as_const(translationDirs)) {
        if (translator.load(locale, QStringLiteral("qtmaildir"),
                            QStringLiteral("_"), dir)) {
            app.installTranslator(&translator);
            break;
        }
    }

    // Fail loudly on an ABI mismatch rather than crashing later.
    if (LIBNOTMUCH_MAJOR_VERSION < 5) {
        QMessageBox::critical(nullptr, QObject::tr("qtmaildir"),
            QObject::tr("libnotmuch 5 or newer is required."));
        return 1;
    }

    MainWindow window(config);
    window.show();

    // What this launch asked for. After show(), so the window is up before a
    // query starts running against it.
    window.applySelectors(selectors);

    // A later launch. The selectors arrive on the socket and go through the
    // same applySelectors() this startup path just used.
    QObject::connect(&instance, &SingleInstance::selectorsReceived, &window,
                     [&window](const LaunchSelectors &arrived) {
                         // Raised whatever the selectors say, an empty set
                         // included: a bare launch against a running window
                         // means "show me the window".
                         //
                         // Under Wayland this is a REQUEST, not a command. The
                         // compositor may honour it as a focus hint or ignore
                         // it by policy, which is its decision and not a defect
                         // to work around: the selectors still apply and the
                         // window still shows the right thing.
                         window.setWindowState(window.windowState()
                                               & ~Qt::WindowMinimized);
                         window.show();
                         window.raise();
                         window.activateWindow();
                         window.applySelectors(arrived);
                     });

    // After show(), and out here rather than inside the constructor. A modal
    // raised from the constructor cannot be dismissed under the offscreen
    // platform, so it hung the test suite with no output (item 84). Showing it
    // here also gives the dialog a visible parent to sit on.
    const QStringList problems = window.configProblems();
    if (!problems.isEmpty()) {
        QMessageBox::warning(&window, QObject::tr("Configuration problems"),
                             problems.join(QLatin1Char('\n')));
    }

    return app.exec();
}
