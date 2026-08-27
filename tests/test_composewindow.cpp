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
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QSplitter>
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
    void aSavedDraftIsFlaggedSeen();
    void aForwardCarriesTheOriginalHtmlAndStripsRemoteContent();
    void anHtmlForwardPreviewsTheOriginalInsteadOfQuotingIt();
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

/// A draft is authored by the user, so it is SEEN by definition and must never
/// be tagged `unread`.
///
/// `maildir.synchronize_flags` is on, so the tag is decided by the filename:
/// notmuch tags any message lacking the `S` flag `unread`. Writing a draft as
/// `:2,D` therefore puts it in the Unread view until the folder next syncs,
/// at which point mbsync round-trips the file and the `S` appears. That is
/// what made the defect look intermittent: only the newest draft, in a folder
/// that has not synced since, shows the symptom. Measured on the user's own
/// mail 2026-08-27, where two drafts written two minutes apart differed only
/// in whether their folder had synced afterwards.
///
/// Asserting on the FILENAME rather than on a notmuch tag is deliberate: the
/// flags are what the code here controls, and a tag assertion would need an
/// indexed database to say the same thing less directly.
void TestComposeWindow::aSavedDraftIsFlaggedSeen()
{
    const Config config = configWithDrafts();

    ComposeContext context;
    context.kind = ComposeContext::Kind::New;
    context.accountKey = QStringLiteral("work");

    ComposeWindow window(context, config, m_dir->path());
    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);
    body->setPlainText(QStringLiteral("A draft the user wrote."));

    QSignalSpy saved(&window, &ComposeWindow::draftSaved);
    auto *save = window.findChild<QAction *>(QStringLiteral("compose_save"));
    QVERIFY(save);
    save->trigger();

    QCOMPARE(saved.size(), 1);
    const QString path = saved.first().first().toString();
    QVERIFY2(!path.isEmpty(), "the save reported no path");

    // The guard the probe needs: without it a rename that dropped the info
    // suffix entirely would pass the S check below by never reaching it.
    const QString name = QFileInfo(path).fileName();
    QVERIFY2(name.contains(QStringLiteral(":2,")),
             qPrintable(QStringLiteral("no Maildir info suffix in %1").arg(name)));

    const QString flags = name.section(QStringLiteral(":2,"), 1);
    QVERIFY2(flags.contains(QLatin1Char('D')),
             qPrintable(QStringLiteral("a draft must carry the D flag, got %1")
                            .arg(flags)));
    QVERIFY2(flags.contains(QLatin1Char('S')),
             qPrintable(QStringLiteral("a draft must carry the S flag or notmuch "
                                       "tags it unread, got %1").arg(flags)));
}

/// Item 171, the composer half. A forward of an HTML message carries the
/// original's markup, with remote content stripped BY DEFAULT and a control to
/// keep it.
///
/// The default is the security-relevant half: forwarding a tracking pixel
/// forwards the tracking, and the original sender learns the recipient opened
/// it. The user chose "ask per forward, default to strip" over always
/// stripping and over keeping everything.
void TestComposeWindow::aForwardCarriesTheOriginalHtmlAndStripsRemoteContent()
{
    const Config config = configWithDrafts();

    // A real file on disk: the composer reads originalPath itself, exactly as
    // extractForwardedAttachments() does, so a fixture built in memory would
    // not exercise the path that runs.
    const QString path = m_dir->path() + QStringLiteral("/original.eml");
    writeFile(path, QStringLiteral(
        "From: Sender <sender@example.org>\r\n"
        "To: someone@example.org\r\n"
        "Subject: Quarterly report\r\n"
        "Date: Wed, 26 Aug 2026 10:00:00 +0200\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "\r\n"
        "<p>Revenue rose <b>12%</b>.</p>"
        "<img src=\"https://tracker.example/px?id=abc\">\r\n"));

    ComposeContext context;
    context.kind = ComposeContext::Kind::Forward;
    context.accountKey = QStringLiteral("work");
    context.originalPath = path;
    context.subject = QStringLiteral("Fwd: Quarterly report");

    ComposeWindow window(context, config, m_dir->path());

    auto *strip = window.findChild<QCheckBox *>(QStringLiteral("stripRemote"));
    QVERIFY2(strip, "there is no strip-remote-content control on a forward");
    QVERIFY2(strip->isChecked(),
             "stripping must be the DEFAULT: a forward must not leak a "
             "tracking pixel to the recipient unless the user asks for it");

    // Checked: the markup survives, the beacon does not.
    const OutgoingMessage stripped = window.currentMessage();
    QVERIFY2(stripped.forwardedHtml.contains(QStringLiteral("Revenue rose")),
             qPrintable(QStringLiteral("the original's markup was lost:\n%1")
                            .arg(stripped.forwardedHtml)));
    QVERIFY2(stripped.forwardedHtml.contains(QStringLiteral("<b>")),
             "the formatting was flattened, which is the defect being fixed");
    QVERIFY2(!stripped.forwardedHtml.contains(QStringLiteral("tracker.example")),
             qPrintable(QStringLiteral("a tracking pixel survived:\n%1")
                            .arg(stripped.forwardedHtml)));

    // Unchecked: the user's explicit choice is honoured.
    strip->setChecked(false);
    const OutgoingMessage kept = window.currentMessage();
    QVERIFY2(kept.forwardedHtml.contains(QStringLiteral("tracker.example")),
             "unchecking the control must actually keep the remote content");

    // A New message has neither the control nor any forwarded markup.
    ComposeContext fresh;
    fresh.kind = ComposeContext::Kind::New;
    fresh.accountKey = QStringLiteral("work");
    ComposeWindow plain(fresh, config, m_dir->path());
    QVERIFY2(plain.currentMessage().forwardedHtml.isEmpty(),
             "a new message must carry no forwarded markup");
}

/// Item 171, the WYSIWYG half. **What the composer shows must be what gets
/// sent**, and for an HTML forward the editable buffer cannot be that.
///
/// The first build seeded the text quote into the buffer and then dropped it
/// when building the HTML part, so the user could edit a quote whose edits
/// were silently discarded. That is worse than the defect it replaced: the
/// previous version at least sent what it displayed.
///
/// So on an HTML forward the buffer holds the user's own note ONLY, and the
/// original appears in a read-only preview instead. Nothing shown is
/// editable-but-ignored, and nothing sent is unshown. The user chose this over
/// a rich-text composer (recorded as item 173, which is the real WYSIWYG
/// answer and a much larger piece of work) and over attaching the original.
void TestComposeWindow::anHtmlForwardPreviewsTheOriginalInsteadOfQuotingIt()
{
    const Config config = configWithDrafts();

    const QString path = m_dir->path() + QStringLiteral("/original.eml");
    writeFile(path, QStringLiteral(
        "From: Sender <sender@example.org>\r\n"
        "To: someone@example.org\r\n"
        "Subject: Quarterly report\r\n"
        "Date: Wed, 26 Aug 2026 10:00:00 +0200\r\n"
        "MIME-Version: 1.0\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "\r\n"
        "<p>Revenue rose <b>12%</b>.</p>\r\n"));

    ComposeContext context;
    context.kind = ComposeContext::Kind::Forward;
    context.accountKey = QStringLiteral("work");
    context.originalPath = path;
    context.subject = QStringLiteral("Fwd: Quarterly report");
    context.quotedBody = QStringLiteral(
        "On Wed, sender@example.org wrote:\n\n> Revenue rose 12%.");

    ComposeWindow window(context, config, m_dir->path());

    auto *body = window.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(body);

    // The buffer carries the user's note only: no quote to edit in vain.
    QVERIFY2(!body->toPlainText().contains(QStringLiteral("Revenue rose")),
             qPrintable(QStringLiteral("the original was seeded into the "
                                       "editable buffer:\n%1").arg(body->toPlainText())));

    // The preview says what will be carried, and is NOT editable.
    auto *preview = window.findChild<QWidget *>(QStringLiteral("forwardPreview"));
    QVERIFY2(preview, "an HTML forward must show what it will carry");
    QVERIFY2(preview->isVisibleTo(&window),
             "the preview must not be hidden on an HTML forward");

    // **Beside the editor, not under it**, at the user's request 2026-08-27:
    // a vertical split, editor 60 and preview 40, so the note being written
    // and the message being forwarded are read side by side.
    auto *split = window.findChild<QSplitter *>(QStringLiteral("composeSplit"));
    QVERIFY2(split, "the preview must share a splitter with the editor");
    QCOMPARE(split->orientation(), Qt::Horizontal);
    QCOMPARE(split->count(), 2);
    QCOMPARE(split->widget(0), static_cast<QWidget *>(body));
    QCOMPARE(split->widget(1), preview);

    // **The ratio is asserted as STRETCH FACTORS, not as resulting pixels.**
    // CLAUDE.md records that the offscreen platform cannot test window sizing:
    // it prints "This plugin does not support propagateSizeHints()" and the
    // splitter here has no real width to divide, so sizes() reports an equal
    // 49/49 whatever the code asks for. Measured: a pixel assertion fails
    // against correct code. The stretch factors are what the layout stores and
    // what survives the first real resize, so they are the testable intent;
    // the appearance is a hand test.
    // QSplitter has no stretchFactor() getter: setStretchFactor() writes the
    // value into the CHILD's size policy, which is where it can be read back.
    QCOMPARE(body->sizePolicy().horizontalStretch(), 6);
    QCOMPARE(preview->sizePolicy().horizontalStretch(), 4);

    // A toggle closes and reopens it.
    auto *toggle = window.findChild<QAction *>(QStringLiteral("compose_show_forward"));
    QVERIFY2(toggle, "there is no toggle for the forwarded-message pane");
    QVERIFY2(toggle->isCheckable(), "the pane toggle must be checkable");
    QVERIFY2(toggle->isChecked(), "the pane starts open on an HTML forward");

    toggle->trigger();
    QVERIFY2(!preview->isVisibleTo(&window),
             "unchecking the toggle must hide the forwarded-message pane");
    toggle->trigger();
    QVERIFY2(preview->isVisibleTo(&window),
             "re-checking the toggle must bring the pane back");

    // What is sent still contains the original, from the markup rather than
    // from the buffer.
    const OutgoingMessage message = window.currentMessage();
    QVERIFY2(message.forwardedHtml.contains(QStringLiteral("Revenue rose")),
             "the forward must still carry the original");

    // A PLAIN forward is unchanged: the quote goes in the buffer, where it is
    // both editable and sent, so WYSIWYG already held there and must not be
    // broken by this.
    const QString plainPath = m_dir->path() + QStringLiteral("/plain.eml");
    writeFile(plainPath, QStringLiteral(
        "From: Sender <sender@example.org>\r\n"
        "Subject: Plain report\r\n"
        "Date: Wed, 26 Aug 2026 10:00:00 +0200\r\n"
        "\r\n"
        "Revenue rose 12%.\r\n"));

    ComposeContext plainContext;
    plainContext.kind = ComposeContext::Kind::Forward;
    plainContext.accountKey = QStringLiteral("work");
    plainContext.originalPath = plainPath;
    plainContext.quotedBody = QStringLiteral("> Revenue rose 12%.");

    ComposeWindow plainWindow(plainContext, config, m_dir->path());
    auto *plainBody = plainWindow.findChild<QPlainTextEdit *>(QStringLiteral("body"));
    QVERIFY(plainBody);
    QVERIFY2(plainBody->toPlainText().contains(QStringLiteral("Revenue rose")),
             qPrintable(QStringLiteral("a plain forward lost its quote:\n%1")
                            .arg(plainBody->toPlainText())));

    auto *noPreview = plainWindow.findChild<QWidget *>(QStringLiteral("forwardPreview"));
    QVERIFY2(!noPreview || !noPreview->isVisibleTo(&plainWindow),
             "a plain forward needs no preview: its quote is in the buffer");

    auto *noToggle = plainWindow.findChild<QAction *>(
        QStringLiteral("compose_show_forward"));
    QVERIFY2(!noToggle || !noToggle->isVisible(),
             "a plain forward must not offer a pane toggle that does nothing");
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
