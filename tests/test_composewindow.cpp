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
#include <QLabel>
#include <QMenuBar>
#include <QToolBar>
#include <QSet>
#include <functional>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
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
    void anUnsavedEditIsAnnouncedInBothPlaces();
    void aSavedDraftReportsItAndClearsTheCue();
    void aSentMessageLeavesNoUnsavedCue();
    void onlyTheSetterWritesTheDirtyFlag();
    void theMenuBarReachesEveryComposerAction();
    void saveDraftWritesAndReports();
    void theMenusReuseTheToolbarActions();
    void theHtmlMenuItemTracksTheToolbarButton();
    void theAgeLineFollowsTheClock();

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

    /// A config whose one account has a drafts folder, so a save can write.
    Config configWithDrafts();

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

/// A helper for the status-bar tests: a config whose account has a drafts
/// folder, which makeConfig() deliberately does not set.
Config TestComposeWindow::configWithDrafts()
{
    const QString confPath = m_dir->path() + QStringLiteral("/qtmaildir.conf");
    QString conf;
    {
        QTextStream out(&conf);
        out << "[account.work]\n"
            << "name = Someone\n"
            << "address = someone@example.org\n"
            << "maildir = work\n"
            << "drafts = Drafts\n"
            << "send_command = /bin/cat\n";
    }
    writeFile(confPath, conf);

    Config config;
    config.load(confPath);
    return config;
}

/// Both cues answer the same question and must agree. The status label is
/// what the user reads while typing; the title marker is what they see when
/// the composer is behind another window.
void TestComposeWindow::anUnsavedEditIsAnnouncedInBothPlaces()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());

    // Seeding is not an edit: the constructor clears the flag after filling
    // the fields, so a composer nobody has typed into is clean.
    auto *unsaved = window.findChild<QLabel *>(QStringLiteral("unsavedCue"));
    QVERIFY(unsaved);
    QVERIFY2(unsaved->isHidden(), "a freshly opened composer is not dirty");
    QVERIFY2(!window.isWindowModified(),
             "a freshly opened composer must not claim unsaved edits");

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("Something typed."));

    QVERIFY2(!unsaved->isHidden(), "the status cue must appear on an edit");
    QVERIFY2(window.isWindowModified(),
             "the title marker must appear on an edit");
}

/// The gap item 160 exists to close: a successful save said nothing at all.
void TestComposeWindow::aSavedDraftReportsItAndClearsTheCue()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    auto *unsaved = window.findChild<QLabel *>(QStringLiteral("unsavedCue"));
    auto *age = window.findChild<QLabel *>(QStringLiteral("draftAge"));
    QVERIFY(unsaved);
    QVERIFY(age);

    QVERIFY2(age->text().isEmpty(),
             "nothing has been saved yet, so there is no age to report");

    body->setPlainText(QStringLiteral("First revision."));
    QVERIFY(window.saveDraftNow());

    QVERIFY2(unsaved->isHidden(), "a save must clear the unsaved cue");
    QVERIFY2(!window.isWindowModified(),
             "a save must clear the title marker");
    QVERIFY2(!age->text().isEmpty(), "a save must be reported");
}

/// The send path clears the flag WITHOUT saving a draft, and it is one of the
/// four sites that write it. A cue hung off the save alone would leave a sent
/// message claiming unsaved edits on the way out.
void TestComposeWindow::aSentMessageLeavesNoUnsavedCue()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("Outgoing."));
    QVERIFY(window.isWindowModified());

    // What the send handler does on success, without running a real send.
    window.markClean();

    auto *unsaved = window.findChild<QLabel *>(QStringLiteral("unsavedCue"));
    QVERIFY(unsaved);
    QVERIFY2(unsaved->isHidden(), "a sent message has no unsaved edits");
    QVERIFY2(!window.isWindowModified(),
             "a sent message must not claim unsaved edits");
}

/// The test above drives markClean() directly, which proves what the SETTER
/// does and nothing about whether the send path calls it: a mutation putting
/// `m_dirty = false` back into the send handler left the whole suite green,
/// measured. That is CLAUDE.md's "a probe pointed at the wrong object".
///
/// The property that actually matters is structural, so it is asserted
/// structurally: m_dirty has ONE writer. Four of the seven sites that used to
/// assign it clear it, and only two of those are a save, so a cue hung off
/// the save path alone silently missed the constructor and the send.
void TestComposeWindow::onlyTheSetterWritesTheDirtyFlag()
{
    QFile source(QStringLiteral(SOURCE_DIR "/src/composewindow.cpp"));
    QVERIFY2(source.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(source.errorString()));
    const QStringList lines =
        QString::fromUtf8(source.readAll()).split(QLatin1Char('\n'));
    source.close();

    // A guard against the probe itself rotting: if the member is ever
    // renamed, this test must fail rather than quietly verify nothing.
    QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("m_dirty")),
             "m_dirty is gone; this test needs updating, not deleting");

    QStringList offenders;
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        // An assignment, not a read: `m_dirty =` but not `m_dirty ==`.
        static const QRegularExpression assignment(
            QStringLiteral("\\bm_dirty\\s*=[^=]"));
        if (!assignment.match(line).hasMatch())
            continue;
        // The one legitimate writer.
        if (line.contains(QStringLiteral("m_dirty = dirty")))
            continue;
        offenders.append(QStringLiteral("%1: %2").arg(i + 1).arg(line.trimmed()));
    }

    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral(
                 "m_dirty must only be written by setDirty(), or the status "
                 "cue and the title marker drift from it. Offending lines:\n%1")
                            .arg(offenders.join(QLatin1Char('\n')))));
}

/// The age changes with no edit to drive it, so it needs a tick of its own.
/// Asserting on the TEXT changing rather than on a wording, which is
/// translated and would pin the test to one locale.
void TestComposeWindow::theAgeLineFollowsTheClock()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("First revision."));
    QVERIFY(window.saveDraftNow());

    auto *age = window.findChild<QLabel *>(QStringLiteral("draftAge"));
    QVERIFY(age);
    const QString justSaved = age->text();
    QVERIFY(!justSaved.isEmpty());

    // Driven rather than waited for: a real wait would put seconds into the
    // suite for a label that reads in tens of them.
    auto *tick = window.findChild<QTimer *>(QStringLiteral("draftAgeTick"));
    QVERIFY2(tick, "the age needs a tick of its own; an edit cannot drive it");
    QVERIFY(tick->isActive());

    window.reportDraftAgeFor(90);
    QVERIFY2(age->text() != justSaved,
             "the age line must move as the clock does");
}

/// Item 132's rule for the main window, applied to the composer: an action
/// nobody can find in a menu is reachable only by a chord the user has to
/// know. Walks the real menu bar rather than a list, so an action added to
/// the toolbar and forgotten in the menus fails here.
void TestComposeWindow::theMenuBarReachesEveryComposerAction()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());

    QMenuBar *bar = window.menuBar();
    QVERIFY2(bar, "the composer has no menu bar");

    // Every action the menus reach, submenus included.
    QSet<QString> reachable;
    std::function<void(QMenu *)> walk = [&](QMenu *menu) {
        const QList<QAction *> actions = menu->actions();
        for (QAction *action : actions) {
            if (QMenu *sub = action->menu()) {
                // An action owning a submenu is not itself reachable: Qt
                // emits no triggered for it, as CLAUDE.md records.
                walk(sub);
                continue;
            }
            if (action->isSeparator())
                continue;
            if (!action->objectName().isEmpty())
                reachable.insert(action->objectName());
        }
    };
    const QList<QAction *> top = bar->actions();
    for (QAction *action : top) {
        QVERIFY2(action->menu(), "a top-level menu bar entry with no menu");
        walk(action->menu());
    }

    // Every named action the composer owns. findChildren, so an action added
    // later is picked up without touching this list.
    const QList<QAction *> owned = window.findChildren<QAction *>();
    QStringList missing;
    for (QAction *action : owned) {
        const QString name = action->objectName();
        if (name.isEmpty())
            continue;
        // The signature entries are built from the files on disk and named
        // per signature; the switch itself is what a menu offers.
        if (name.startsWith(QStringLiteral("signature_choice")))
            continue;
        if (!reachable.contains(name))
            missing.append(name);
    }

    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("not reachable from any menu: %1")
                            .arg(missing.join(QStringLiteral(", ")))));
}

/// The action item 161 adds. Save draft did not exist at all: saveDraftNow()
/// was reachable only from the timer, the send path and closeEvent, so the
/// user could not ask for a save.
void TestComposeWindow::saveDraftWritesAndReports()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("Saved by hand."));

    auto *save = window.findChild<QAction *>(QStringLiteral("compose_save"));
    QVERIFY2(save, "there is no Save draft action");
    QCOMPARE(save->shortcut(), QKeySequence(QStringLiteral("Ctrl+S")));

    QSignalSpy saved(&window, &ComposeWindow::draftSaved);
    save->trigger();

    QCOMPARE(saved.size(), 1);

    // Routed through saveDraftNow(), so item 160's reporting comes free. A
    // second write path would have to repeat it, and would be the ghost-file
    // bug item 158 fixed.
    auto *age = window.findChild<QLabel *>(QStringLiteral("draftAge"));
    QVERIFY(age);
    QVERIFY2(!age->text().isEmpty(), "a manual save must report like an autosave");
    QVERIFY2(!window.isWindowModified(), "a manual save must clear the marker");
}

/// The same QAction objects, shown twice over, exactly as item 140 required
/// for the message pane's bar. A copy would drift: an enablement change or a
/// new shortcut would reach one surface and not the other.
void TestComposeWindow::theMenusReuseTheToolbarActions()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());

    auto *bold = window.findChild<QAction *>(QStringLiteral("format_bold"));
    auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("formatToolbar"));
    QVERIFY(bold);
    QVERIFY(toolbar);
    QVERIFY2(toolbar->actions().contains(bold),
             "Bold left the formatting toolbar");

    QMenu *format = nullptr;
    const QList<QAction *> top = window.menuBar()->actions();
    for (QAction *action : top) {
        if (action->menu() && action->menu()->actions().contains(bold))
            format = action->menu();
    }
    QVERIFY2(format, "Bold is not in any menu");

    // The pointer itself, not a namesake.
    QVERIFY2(format->actions().contains(bold),
             "the menu holds a copy of Bold rather than the action itself");
}

/// The HTML toggle is a QToolButton, not a QAction, so a menu entry for it
/// has to be built and kept in step by hand. Both directions: a menu that
/// only follows the button is half a control.
void TestComposeWindow::theHtmlMenuItemTracksTheToolbarButton()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());

    auto *button = window.findChild<QToolButton *>(QStringLiteral("sendHtml"));
    auto *item = window.findChild<QAction *>(QStringLiteral("compose_send_html"));
    QVERIFY(button);
    QVERIFY2(item, "there is no menu entry for the HTML toggle");
    QVERIFY(item->isCheckable());

    const bool initial = button->isChecked();
    QCOMPARE(item->isChecked(), initial);

    button->setChecked(!initial);
    QCOMPARE(item->isChecked(), !initial);

    item->setChecked(initial);
    QCOMPARE(button->isChecked(), initial);
}

QTEST_MAIN(TestComposeWindow)
#include "test_composewindow.moc"
