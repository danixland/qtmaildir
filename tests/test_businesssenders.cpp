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

#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include "businesssenders.h"

class TestBusinessSenders : public QObject
{
    Q_OBJECT

private slots:
    void anExactAddressMatches();
    void aDomainEntryMatchesEveryAddressUnderIt();
    void commentsAndBlankLinesAreIgnored();
    void whitespaceAroundAnEntryIsIgnored();
    void matchingIsCaseInsensitive();
    void anAbsentFileMatchesNothing();
    void candidatesAreAppendedCommentedOut();
    void appendingDoesNotCorruptALineThatLacksATrailingNewline();
    void anAddressAlreadyPresentIsNeverReproposed();
    void onlyBulkLookingLocalPartsAreProposed();
    void theFirstRunScansEverything();
    void alaterRunScansOnlyRecentMail();
};

void TestBusinessSenders::anExactAddressMatches()
{
    const BusinessSenders::List list = BusinessSenders::parse(
        QStringLiteral("noreply@cofidis.it\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("someone@cofidis.it")));
}

void TestBusinessSenders::aDomainEntryMatchesEveryAddressUnderIt()
{
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("@cofidis.it\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@cofidis.it")));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("a@example.org")));
}

void TestBusinessSenders::commentsAndBlankLinesAreIgnored()
{
    // A commented entry is the REJECT gesture: present in the file, not
    // applied. This is the property the whole file format rests on.
    const BusinessSenders::List list = BusinessSenders::parse(
        QStringLiteral("# noreply@cofidis.it (47 messages)\n"
                       "\n"
                       "   \n"
                       "billing@example.org\n"));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("noreply@cofidis.it")));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@example.org")));
}

void TestBusinessSenders::whitespaceAroundAnEntryIsIgnored()
{
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("  billing@example.org  \n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@example.org")));
}

void TestBusinessSenders::matchingIsCaseInsensitive()
{
    // Addresses arrive from headers in whatever case the sender used, so a
    // list entry that matched only one casing would look broken at random.
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("NoReply@Cofidis.IT\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
}

void TestBusinessSenders::anAbsentFileMatchesNothing()
{
    QTemporaryDir dir;
    const BusinessSenders::List list =
        BusinessSenders::load(dir.filePath(QStringLiteral("does-not-exist")));
    QVERIFY(!BusinessSenders::contains(list, QStringLiteral("a@example.org")));
}

void TestBusinessSenders::candidatesAreAppendedCommentedOut()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));

    QHash<QString, int> counts;
    counts.insert(QStringLiteral("noreply@cofidis.it"), 47);
    BusinessSenders::appendCandidates(path, counts);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString written = QString::fromUtf8(file.readAll());

    // Commented, and carrying the count so the user can judge it.
    QVERIFY(written.contains(QStringLiteral("# noreply@cofidis.it")));
    QVERIFY(written.contains(QStringLiteral("47")));

    // Nothing it wrote may take effect on its own.
    const BusinessSenders::List list = BusinessSenders::load(path);
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("noreply@cofidis.it")));
}

void TestBusinessSenders::appendingDoesNotCorruptALineThatLacksATrailingNewline()
{
    // Hand-editing, the documented workflow, can leave the file without a
    // trailing newline. Appending then glued the first candidate onto the last
    // existing line, silently breaking the user's own active entry so it
    // stopped matching. The guard writes a newline before the additions.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));
    QFile seed(path);
    QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
    seed.write("billing@example.org");   // deliberately no trailing newline
    seed.close();

    QHash<QString, int> counts;
    counts.insert(QStringLiteral("noreply@a.org"), 3);
    BusinessSenders::appendCandidates(path, counts);

    // The original entry is intact and still matches.
    const BusinessSenders::List list = BusinessSenders::load(path);
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@example.org")));

    // ...and the candidate sits on its own commented line, not glued onto it.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString written = QString::fromUtf8(file.readAll());
    QVERIFY(written.contains(QStringLiteral(
        "billing@example.org\n# noreply@a.org (3 messages)")));
}

void TestBusinessSenders::anAddressAlreadyPresentIsNeverReproposed()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));

    // Both forms count as present: an active entry and a rejected one. The
    // rejected case is the one that matters, since re-proposing it would undo
    // the user's decision every ten minutes with no explanation.
    QFile seed(path);
    QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
    seed.write("billing@example.org\n# noreply@cofidis.it (47 messages)\n");
    seed.close();
    const qint64 sizeBefore = QFileInfo(path).size();

    QHash<QString, int> counts;
    counts.insert(QStringLiteral("noreply@cofidis.it"), 51);
    counts.insert(QStringLiteral("billing@example.org"), 12);
    BusinessSenders::appendCandidates(path, counts);

    QCOMPARE(QFileInfo(path).size(), sizeBefore);
}

void TestBusinessSenders::onlyBulkLookingLocalPartsAreProposed()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));

    QHash<QString, int> counts;
    counts.insert(QStringLiteral("noreply@a.org"), 3);
    counts.insert(QStringLiteral("john.doe@b.org"), 3);
    BusinessSenders::appendCandidates(path, counts);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString written = QString::fromUtf8(file.readAll());
    QVERIFY(written.contains(QStringLiteral("noreply@a.org")));
    QVERIFY(!written.contains(QStringLiteral("john.doe@b.org")));
}

void TestBusinessSenders::theFirstRunScansEverything()
{
    QTemporaryDir dir;
    const QString missing = dir.filePath(QStringLiteral("business-senders"));

    // No file at all: a week of mail would propose almost nothing and the
    // list would take months to become useful, so the first run pays for a
    // full scan once.
    QCOMPARE(BusinessSenders::scanQuery(missing), QStringLiteral("*"));

    // A file holding ONLY rejected candidates is still a first run: nothing
    // has been accepted yet. Rescanning re-proposes none of them, since
    // appendCandidates skips anything already mentioned.
    QFile rejected(missing);
    QVERIFY(rejected.open(QIODevice::WriteOnly | QIODevice::Text));
    rejected.write("# noreply@cofidis.it (47 messages)\n");
    rejected.close();
    QCOMPARE(BusinessSenders::scanQuery(missing), QStringLiteral("*"));
}

void TestBusinessSenders::alaterRunScansOnlyRecentMail()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("billing@example.org\n");
    file.close();

    QCOMPARE(BusinessSenders::scanQuery(path), QStringLiteral("date:1week.."));
}

QTEST_MAIN(TestBusinessSenders)
#include "test_businesssenders.moc"
