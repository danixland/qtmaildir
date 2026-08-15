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
#include "mainwindow.h"
#include "version.h"

int main(int argc, char *argv[])
{
    // Answered before anything heavier starts: registering web engine schemes
    // and constructing a QApplication to print one line would be absurd, and
    // --version has to work on a machine where the GUI cannot open at all.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0
            || std::strcmp(argv[i], "-v") == 0) {
            std::printf("qtmaildir %s\n", QTMAILDIR_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0
            || std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "qtmaildir %s - a Qt6 mail client for notmuch-indexed Maildirs\n"
                "\n"
                "Usage: qtmaildir [options]\n"
                "\n"
                "  -h, --help     Show this help and exit\n"
                "  -v, --version  Show the version and exit\n"
                "\n"
                "Configuration: ~/.config/qtmaildir/qtmaildir.conf\n"
                "qtmaildir reads a notmuch-indexed Maildir. It does no network\n"
                "protocol work: fetching and sending are external commands.\n",
                QTMAILDIR_VERSION);
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

    // Compiled in rather than read from disk, so the icon is there whether or
    // not the app was installed. setDesktopFileName() is what lets a Wayland
    // compositor match the window to its .desktop entry, which is where the
    // taskbar icon really comes from there.
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/qtmaildir.svg")));
    app.setDesktopFileName(QStringLiteral("qtmaildir"));

    // On main's stack deliberately: a QTranslator must outlive exec(), and one
    // scoped to a helper function unloads on return, silently reverting every
    // string to English. Installed before Config is loaded, because config
    // warnings are generated at load time and are themselves translated.
    //
    // QLocale() reads the system locale, so LANG=it_IT.UTF-8 selects the file
    // with no config key of our own. A missing .qm returns false and the app
    // runs in English, which is the correct failure rather than a fatal one.
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
        if (translator.load(QLocale(), QStringLiteral("qtmaildir"),
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

    Config config;
    config.load(Config::defaultPath());

    MainWindow window(config);
    window.show();

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
