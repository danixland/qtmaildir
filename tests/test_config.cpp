#include <QtTest>
#include <QTemporaryDir>
#include <QSettings>
#include "config.h"

class TestConfig : public QObject
{
    Q_OBJECT
private slots:
    void parsesAccounts();
    void parsesSavedQueries();
    void missingSyncCommandIsEmpty();
    void accountWithoutMaildirIsRejected();
    void scopedQueryWrapsCorrectly();
};

static QString writeIni(const QTemporaryDir &dir, const QString &body)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(body.toUtf8());
    f.close();
    return path;
}

void TestConfig::parsesAccounts()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "name=Test User\n"
        "address=user@example.org\n"
        "maildir=work-mail\n"
        "drafts=Drafts\n"
        "\n"
        "[account.personal]\n"
        "name=Test User\n"
        "address=me@example.net\n"
        "maildir=personal\n"
    ));

    Config config;
    config.load(path);

    QCOMPARE(config.accounts().size(), 2);

    const Account work = config.account(QStringLiteral("work"));
    QCOMPARE(work.key, QStringLiteral("work"));
    QCOMPARE(work.name, QStringLiteral("Test User"));
    QCOMPARE(work.address, QStringLiteral("user@example.org"));
    QCOMPARE(work.maildir, QStringLiteral("work-mail"));
    QCOMPARE(work.drafts, QStringLiteral("Drafts"));

    // drafts is optional in v1 (send is v2).
    const Account personal = config.account(QStringLiteral("personal"));
    QVERIFY(personal.drafts.isEmpty());
    QVERIFY(personal.isValid());
}

void TestConfig::parsesSavedQueries()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[queries]\n"
        "Inbox=tag:inbox\n"
        "Unread=tag:unread\n"
    ));

    Config config;
    config.load(path);

    const QList<SavedQuery> queries = config.savedQueries();
    QCOMPARE(queries.size(), 2);
    // QSettings::childKeys() returns keys alphabetically, not in file order,
    // so the UI button order is alphabetical. This assertion happens to hold
    // either way since "Inbox" < "Unread", but the ordering guarantee is
    // alphabetical, not "follows the file".
    QCOMPARE(queries.at(0).name, QStringLiteral("Inbox"));
    QCOMPARE(queries.at(0).query, QStringLiteral("tag:inbox"));
}

void TestConfig::missingSyncCommandIsEmpty()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[general]\n"));

    Config config;
    config.load(path);

    QVERIFY(config.syncCommand().isEmpty());
    // The UI uses this to disable the Sync button with a tooltip.
    QVERIFY(!config.warnings().isEmpty());
}

void TestConfig::accountWithoutMaildirIsRejected()
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[sync]\n"
        "command=/bin/true\n"
        "\n"
        "[account.broken]\n"
        "name=No Maildir\n"
        "address=x@example.org\n"
    ));

    Config config;
    config.load(path);

    // Rejected, reported, and not offered to the user as a scope.
    QCOMPARE(config.accounts().size(), 0);
    QCOMPARE(config.warnings().size(), 1);
    QVERIFY(config.warnings().first().contains(QStringLiteral("broken")));
}

void TestConfig::scopedQueryWrapsCorrectly()
{
    Account account;
    account.key = QStringLiteral("work");
    account.maildir = QStringLiteral("work-mail");

    QCOMPARE(account.scopedQuery(QStringLiteral("tag:inbox")),
             QStringLiteral("path:\"work-mail/**\" and (tag:inbox)"));

    // An empty query still scopes to the account rather than matching nothing.
    QCOMPARE(account.scopedQuery(QString()),
             QStringLiteral("path:\"work-mail/**\""));
}

QTEST_MAIN(TestConfig)
#include "test_config.moc"
