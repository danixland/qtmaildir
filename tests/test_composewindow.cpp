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
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSignalSpy>
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
    void changingTheAccountFollowsItsSignature();
    void changingTheAccountStopsFollowingOnceTheSwitchIsUsed();
    void aResumedDraftDoesNotReseedOnAnAccountChange();
    void savingADraftEmitsItsPathAndTheReplacedOne();

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

void TestComposeWindow::changingTheAccountFollowsItsSignature()
{
    for (const auto &entry :
         QList<QPair<QString, QString>>{
             { QStringLiteral("work.md"), QStringLiteral("Work sig") },
             { QStringLiteral("home.md"), QStringLiteral("Home sig") } }) {
        QFile file(m_signatureDir + QStringLiteral("/") + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(entry.second.toUtf8());
        file.close();
    }

    const QString path = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.work]\n"
            << "name = Someone\naddress = someone@example.org\n"
            << "maildir = work\nsend_command = /bin/cat\n"
            << "signature = work\n"
            << "\n[account.home]\n"
            << "name = Someone\naddress = other@example.org\n"
            << "maildir = home\nsend_command = /bin/cat\n"
            << "signature = home\n";
    }
    Config config;
    config.load(path);

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *from = window.findChild<QComboBox *>(QStringLiteral("from"));
    QVERIFY(body);
    QVERIFY(from);
    QVERIFY(body->toPlainText().contains(QStringLiteral("Work sig")));

    // Select the other account by its key, never by index: the order of the
    // combo is the config's and an index assertion would pass on the wrong one.
    const int home = from->findData(QStringLiteral("home"));
    QVERIFY(home >= 0);
    from->setCurrentIndex(home);

    QVERIFY(body->toPlainText().contains(QStringLiteral("Home sig")));
    QVERIFY(!body->toPlainText().contains(QStringLiteral("Work sig")));
}

void TestComposeWindow::changingTheAccountStopsFollowingOnceTheSwitchIsUsed()
{
    for (const auto &entry :
         QList<QPair<QString, QString>>{
             { QStringLiteral("work.md"), QStringLiteral("Work sig") },
             { QStringLiteral("home.md"), QStringLiteral("Home sig") },
             { QStringLiteral("chosen.md"), QStringLiteral("Chosen sig") } }) {
        QFile file(m_signatureDir + QStringLiteral("/") + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(entry.second.toUtf8());
        file.close();
    }

    const QString path = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.work]\n"
            << "name = Someone\naddress = someone@example.org\n"
            << "maildir = work\nsend_command = /bin/cat\n"
            << "signature = work\n"
            << "\n[account.home]\n"
            << "name = Someone\naddress = other@example.org\n"
            << "maildir = home\nsend_command = /bin/cat\n"
            << "signature = home\n";
    }
    Config config;
    config.load(path);

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *from = window.findChild<QComboBox *>(QStringLiteral("from"));
    auto *button =
        window.findChild<QToolButton *>(QStringLiteral("signatureSwitch"));
    QVERIFY(body);
    QVERIFY(from);
    QVERIFY(button);

    // The user picks one deliberately.
    for (QAction *action : button->menu()->actions()) {
        if (action->data().toString() == QStringLiteral("chosen"))
            action->trigger();
    }
    QVERIFY(body->toPlainText().contains(QStringLiteral("Chosen sig")));

    const int home = from->findData(QStringLiteral("home"));
    QVERIFY(home >= 0);
    from->setCurrentIndex(home);

    // The deliberate choice survives the account change. Overwriting it is
    // the one behaviour that can silently discard something the user just did.
    QVERIFY(body->toPlainText().contains(QStringLiteral("Chosen sig")));
    QVERIFY(!body->toPlainText().contains(QStringLiteral("Home sig")));
}

void TestComposeWindow::aResumedDraftDoesNotReseedOnAnAccountChange()
{
    for (const auto &entry :
         QList<QPair<QString, QString>>{
             { QStringLiteral("work.md"), QStringLiteral("Work sig") },
             { QStringLiteral("home.md"), QStringLiteral("Home sig") } }) {
        QFile file(m_signatureDir + QStringLiteral("/") + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(entry.second.toUtf8());
        file.close();
    }

    const QString path = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&file);
        out << "[account.work]\n"
            << "name = Someone\naddress = someone@example.org\n"
            << "maildir = work\nsend_command = /bin/cat\n"
            << "signature = work\n"
            << "\n[account.home]\n"
            << "name = Someone\naddress = other@example.org\n"
            << "maildir = home\nsend_command = /bin/cat\n"
            << "signature = home\n";
    }
    Config config;
    config.load(path);

    // The saved body already carries its own signature, which does not match
    // any on-disk file. A From: change must not replace it with the new
    // account's: the draft is the message the user wrote, exactly as
    // seedBody() takes its body verbatim.
    ComposeContext context;
    context.kind = ComposeContext::Kind::Draft;
    context.accountKey = QStringLiteral("work");
    context.body = QStringLiteral("Half a thought.\n\n-- \nJane Doe");
    context.draftPath = m_dir->path() + QStringLiteral("/draft");

    ComposeWindow window(context, config, m_dir->path());
    window.setSignatureDir(m_signatureDir);
    window.seedSignature();

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    auto *from = window.findChild<QComboBox *>(QStringLiteral("from"));
    QVERIFY(body);
    QVERIFY(from);
    QVERIFY(body->toPlainText().contains(QStringLiteral("Jane Doe")));

    const int home = from->findData(QStringLiteral("home"));
    QVERIFY(home >= 0);
    from->setCurrentIndex(home);

    QVERIFY(body->toPlainText().contains(QStringLiteral("Jane Doe")));
    QVERIFY(!body->toPlainText().contains(QStringLiteral("Home sig")));
}

void TestComposeWindow::savingADraftEmitsItsPathAndTheReplacedOne()
{
    // A config whose account has a drafts folder, which makeConfig() does not
    // set, so the save can actually write somewhere.
    const QString confPath = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    {
        QString conf;
        QTextStream out(&conf);
        out << "[account.work]\n"
            << "name = Someone\n"
            << "address = someone@example.org\n"
            << "maildir = work\n"
            << "drafts = Drafts\n"
            << "send_command = /bin/cat\n";
        writeFile(confPath, conf);
    }
    Config config;
    config.load(confPath);

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);

    QSignalSpy saved(&window, &ComposeWindow::draftSaved);

    body->setPlainText(QStringLiteral("First revision."));
    QVERIFY(window.saveDraftNow());

    QCOMPARE(saved.size(), 1);
    const QString first = saved.first().at(0).toString();
    const QString firstPrevious = saved.first().at(1).toString();
    QVERIFY(!first.isEmpty());
    QVERIFY(firstPrevious.isEmpty());
    QVERIFY(QFile::exists(first));

    // A rewrite writes a fresh file and unlinks the old; the previous path
    // comes back so the owner can drop the old index entry.
    body->setPlainText(QStringLiteral("Second revision."));
    QVERIFY(window.saveDraftNow());

    QCOMPARE(saved.size(), 2);
    const QString second = saved.at(1).at(0).toString();
    const QString secondPrevious = saved.at(1).at(1).toString();
    QVERIFY(!second.isEmpty());
    QCOMPARE(secondPrevious, first);
    QVERIFY2(second != first, "a rewrite reused the old filename");
}

QTEST_MAIN(TestComposeWindow)
#include "test_composewindow.moc"
