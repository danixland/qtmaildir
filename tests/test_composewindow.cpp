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
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolButton>

#include "composecontext.h"
#include "composewindow.h"
#include "config.h"
#include "signatures.h"

class TestComposeWindow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void aNewMessageSeedsTheComposeSignature();
    void anAccountSignatureOverridesTheComposeOne();
    void aResumedDraftSeedsNoSignature();
    void anUnknownSignatureNameSeedsNothing();
    void theSwitchListsEveryFileAndNone();

private:
    /// A config pointing at a signatures directory holding \p files, with one
    /// account that can send.
    Config makeConfig(const QList<QPair<QString, QString>> &files,
                      const QString &composeSignature,
                      const QString &accountSignature = {});

    /// QVERIFY cannot appear inside makeConfig(), which returns a value: the
    /// macro expands to a bare `return;` on failure, which is invalid in a
    /// non-void function. A void helper keeps the check and sidesteps that.
    void writeFile(const QString &path, const QString &content);

    QTemporaryDir *m_dir = nullptr;
    QString m_signatureDir;
};

void TestComposeWindow::init()
{
    m_dir = new QTemporaryDir;
    QVERIFY(m_dir->isValid());
    m_signatureDir = m_dir->path() + QStringLiteral("/signatures");
    QVERIFY(QDir().mkpath(m_signatureDir));
}

void TestComposeWindow::cleanup()
{
    delete m_dir;
    m_dir = nullptr;
}

void TestComposeWindow::writeFile(const QString &path, const QString &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << content;
}

Config TestComposeWindow::makeConfig(
    const QList<QPair<QString, QString>> &files,
    const QString &composeSignature, const QString &accountSignature)
{
    for (const auto &entry : files)
        writeFile(m_signatureDir + QStringLiteral("/") + entry.first, entry.second);

    QString conf;
    {
        QTextStream out(&conf);
        out << "[compose]\n"
            << "signature = " << composeSignature << "\n"
            << "\n"
            << "[account.work]\n"
            << "name = Someone\n"
            << "address = someone@example.org\n"
            << "maildir = work\n"
            << "send_command = /bin/cat\n";
        if (!accountSignature.isEmpty())
            out << "signature = " << accountSignature << "\n";
    }
    const QString path = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    writeFile(path, conf);

    Config config;
    config.load(path);
    return config;
}

void TestComposeWindow::aNewMessageSeedsTheComposeSignature()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Jane Doe") } },
        QStringLiteral("work"));

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    QVERIFY(body->toPlainText().endsWith(QStringLiteral("-- \nJane Doe")));
}

void TestComposeWindow::anAccountSignatureOverridesTheComposeOne()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Long one") },
          { QStringLiteral("brief.md"), QStringLiteral("Brief") } },
        QStringLiteral("work"), QStringLiteral("brief"));

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    QVERIFY(body->toPlainText().endsWith(QStringLiteral("-- \nBrief")));
}

void TestComposeWindow::aResumedDraftSeedsNoSignature()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Jane Doe") } },
        QStringLiteral("work"));

    // The saved body already carries whatever signature it was written with.
    // Seeding again would put a SECOND one on a message written once.
    ComposeContext context;
    context.kind = ComposeContext::Kind::Draft;
    context.accountKey = QStringLiteral("work");
    context.body = QStringLiteral("Half a thought.\n\n-- \nJane Doe");
    context.draftPath = m_dir->path() + QStringLiteral("/draft");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    QCOMPARE(body->toPlainText().count(QStringLiteral("-- \nJane Doe")), 1);
}

void TestComposeWindow::anUnknownSignatureNameSeedsNothing()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Jane Doe") } },
        QStringLiteral("absent"));

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    // No signature, and the composer still opened rather than refusing.
    QVERIFY(!body->toPlainText().contains(QStringLiteral("-- ")));
}

void TestComposeWindow::theSwitchListsEveryFileAndNone()
{
    const Config config = makeConfig(
        { { QStringLiteral("work.md"), QStringLiteral("Jane Doe") },
          { QStringLiteral("brief.md"), QStringLiteral("Brief") } },
        QString());

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *button =
        window.findChild<QToolButton *>(QStringLiteral("signatureSwitch"));
    QVERIFY(button);
    QVERIFY(button->menu());
    // "None" plus one per file.
    QCOMPARE(button->menu()->actions().size(), 3);
}

QTEST_MAIN(TestComposeWindow)
#include "test_composewindow.moc"
