#include <QApplication>
#include <QMessageBox>
#include <QWebEngineUrlScheme>

#include <notmuch.h>

#include "config.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
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

    return app.exec();
}
