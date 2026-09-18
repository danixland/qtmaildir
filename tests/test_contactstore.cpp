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
#include <QFileInfo>
#include <QTemporaryDir>

#include "contactstore.h"

namespace {

QString readFixture(const QString &name)
{
    QFile file(QStringLiteral(FIXTURE_DIR) + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

/// A minimal but complete vCard. Used by the loadDirectory cases, which build
/// their own tree in a QTemporaryDir rather than reaching for the shared
/// fixtures: a directory walk is about more than one card, and the flat
/// fixture directory has no subdirectories to walk into.
QString card(const QString &name, const QString &email)
{
    return QStringLiteral("BEGIN:VCARD\nVERSION:3.0\nFN:%1\nEMAIL:%2\nEND:VCARD\n")
        .arg(name, email);
}

void writeFile(const QString &path, const QString &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
             qPrintable(file.errorString()));
    file.write(content.toUtf8());
}

} // namespace

/// The vCard 3.0 parse behind the contact completion.
///
/// Asserted on values, never on a rendered UI: ContactStore is a namespace of
/// free functions over values, which is what makes the parse testable without
/// a window at all. The loadDirectory cases each build a throwaway tree in a
/// QTemporaryDir, because the shared fixtures directory is flat.
class TestContactStore : public QObject
{
    Q_OBJECT
private slots:
    void unfoldStripsTheFoldWhitespaceOnly();
    void unfoldJoinsAcrossSeveralFolds();
    void unfoldLeavesUnfoldedTextAlone();

    void parsesNameAndAddress();
    void parsesAFoldedCard();
    void takesAContactPerEmailLine();
    void cardWithoutEmailYieldsNothing();
    void cardWithoutNameYieldsAnEmptyName();
    void ignoresPhotoAddressAndPhone();
    void acceptsParametersBeforeTheColon();
    void doesNotSplitOnAColonInsideAQuotedParameter();
    void propertyNamesAreCaseInsensitive();
    void unescapesTheFormattedName();
    void unescapesTheOtherTextEscapes();

    void readsRecursively();
    void deduplicatesOnTheAddressCaseInsensitively();
    void sortsByNameThenAddress();
    void skipsFilesThatHoldNoCard();
    void skipsFilesThatAreNotVcf();
    void missingDirectoryIsEmptyAndQuiet();
    void emptyDirectoryIsEmptyAndQuiet();
};

void TestContactStore::unfoldStripsTheFoldWhitespaceOnly()
{
    // The fold marker is CRLF plus ONE linear whitespace character. Removing
    // the newline and that one character is the whole operation: the trailing
    // space the writer left on the first line is content, not marker.
    QCOMPARE(ContactStore::unfold(QStringLiteral("a \n b")),
             QStringLiteral("a b"));
    QCOMPARE(ContactStore::unfold(QStringLiteral("x\n\ty")),
             QStringLiteral("xy"));
}

void TestContactStore::unfoldJoinsAcrossSeveralFolds()
{
    // A fold may land anywhere, including mid-token, so every continuation
    // line joins to the line that precedes it, not to the original.
    QCOMPARE(ContactStore::unfold(QStringLiteral("EMAIL:folded@exam\n ple.org")),
             QStringLiteral("EMAIL:folded@example.org"));
    QCOMPARE(ContactStore::unfold(QStringLiteral("a\n b\n c")),
             QStringLiteral("abc"));
}

void TestContactStore::unfoldLeavesUnfoldedTextAlone()
{
    // No leading whitespace, no fold. The newlines are structure and survive.
    QCOMPARE(ContactStore::unfold(QStringLiteral("FN:Alice\nEMAIL:a@example.org")),
             QStringLiteral("FN:Alice\nEMAIL:a@example.org"));
}

void TestContactStore::parsesNameAndAddress()
{
    const QList<Contact> contacts =
        ContactStore::parseCard(readFixture(QStringLiteral("contact_plain.vcf")));

    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.at(0).name, QStringLiteral("Alice Example"));
    QCOMPARE(contacts.at(0).email, QStringLiteral("alice@example.org"));
}

void TestContactStore::parsesAFoldedCard()
{
    // The pipeline loadDirectory uses: unfold first, because a fold is only
    // legal before any field is looked at, then parse.
    const QList<Contact> contacts = ContactStore::parseCard(
        ContactStore::unfold(readFixture(QStringLiteral("contact_folded.vcf"))));

    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.at(0).name, QStringLiteral("Folded Person"));
    QCOMPARE(contacts.at(0).email, QStringLiteral("folded@example.org"));
}

void TestContactStore::takesAContactPerEmailLine()
{
    // A card with two addresses is two candidates, each carrying the one name.
    const QList<Contact> contacts = ContactStore::parseCard(
        readFixture(QStringLiteral("contact_twoemails.vcf")));

    QCOMPARE(contacts.size(), 2);
    QCOMPARE(contacts.at(0).name, QStringLiteral("Dana Two"));
    QCOMPARE(contacts.at(0).email, QStringLiteral("dana@example.org"));
    QCOMPARE(contacts.at(1).name, QStringLiteral("Dana Two"));
    QCOMPARE(contacts.at(1).email, QStringLiteral("dana.home@example.org"));
}

void TestContactStore::cardWithoutEmailYieldsNothing()
{
    // Measured on the real store: most cards carry no address. That is the
    // ordinary case, not a failure, and it must not produce a candidate.
    QVERIFY(ContactStore::parseCard(
                readFixture(QStringLiteral("contact_noemail.vcf")))
                .isEmpty());
}

void TestContactStore::cardWithoutNameYieldsAnEmptyName()
{
    // An address with no FN is still a usable candidate, on the address alone.
    const QList<Contact> contacts = ContactStore::parseCard(
        readFixture(QStringLiteral("contact_noname.vcf")));

    QCOMPARE(contacts.size(), 1);
    QVERIFY(contacts.at(0).name.isEmpty());
    QCOMPARE(contacts.at(0).email, QStringLiteral("noname@example.org"));
}

void TestContactStore::ignoresPhotoAddressAndPhone()
{
    // PHOTO, ADR and TEL are ignored entirely. They must not add candidates
    // and must not disturb the one the EMAIL line produces.
    const QList<Contact> contacts = ContactStore::parseCard(
        readFixture(QStringLiteral("contact_extra.vcf")));

    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.at(0).name, QStringLiteral("Extra Fields"));
    QCOMPARE(contacts.at(0).email, QStringLiteral("extra@example.org"));
}

void TestContactStore::acceptsParametersBeforeTheColon()
{
    // EMAIL;TYPE=WORK:a@example.org. The parameters belong to the property
    // name, so the value still parses.
    const QList<Contact> contacts = ContactStore::parseCard(
        readFixture(QStringLiteral("contact_params.vcf")));

    QCOMPARE(contacts.size(), 2);
    QCOMPARE(contacts.at(0).email, QStringLiteral("bob@example.org"));
}

void TestContactStore::doesNotSplitOnAColonInsideAQuotedParameter()
{
    // EMAIL;TYPE=INTERNET;LABEL="work: main":bob.work@example.org. Splitting
    // on the FIRST colon would cut inside the quoted parameter and hand the
    // parser a mangled value, so the split tracks the quotes.
    const QList<Contact> contacts = ContactStore::parseCard(
        readFixture(QStringLiteral("contact_params.vcf")));

    QCOMPARE(contacts.size(), 2);
    QCOMPARE(contacts.at(1).email, QStringLiteral("bob.work@example.org"));
}

void TestContactStore::propertyNamesAreCaseInsensitive()
{
    // RFC 2426 names are case-insensitive: `email:` is an EMAIL.
    const QList<Contact> contacts = ContactStore::parseCard(
        readFixture(QStringLiteral("contact_lowercase.vcf")));

    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.at(0).name, QStringLiteral("Carol Example"));
    QCOMPARE(contacts.at(0).email, QStringLiteral("carol@example.org"));
}

void TestContactStore::unescapesTheFormattedName()
{
    // FN is a TEXT value, so `\,` means a literal comma. Left escaped, the
    // user sees the backslash in the completion popup.
    const QList<Contact> contacts = ContactStore::parseCard(
        readFixture(QStringLiteral("contact_escaped.vcf")));

    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.at(0).name, QStringLiteral("Rossi, Mario"));
}

void TestContactStore::unescapesTheOtherTextEscapes()
{
    // The parser decodes the full TEXT escape set, so a name never reaches the
    // UI half-decoded. The literal `\n` line is the one that would otherwise
    // show two characters where the card described a line break.
    const QList<Contact> contacts = ContactStore::parseCard(QStringLiteral(
        "FN:Back\\\\slash\\; Semi\\nNext\nEMAIL:esc@example.org"));

    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.at(0).name,
             QStringLiteral("Back\\slash; Semi\nNext"));
}

void TestContactStore::readsRecursively()
{
    // A vdir keeps one subdirectory per collection, so the walk has to
    // descend. The tree lives in a QTemporaryDir, not the shared fixtures.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + QStringLiteral("/collection-a/one.vcf"),
              card(QStringLiteral("One"), QStringLiteral("one@example.org")));
    writeFile(dir.path() + QStringLiteral("/collection-b/two.vcf"),
              card(QStringLiteral("Two"), QStringLiteral("two@example.org")));
    writeFile(dir.path() + QStringLiteral("/three.vcf"),
              card(QStringLiteral("Three"), QStringLiteral("three@example.org")));

    const QList<Contact> contacts = ContactStore::loadDirectory(dir.path());
    QCOMPARE(contacts.size(), 3);
}

void TestContactStore::deduplicatesOnTheAddressCaseInsensitively()
{
    // Two collections holding the same person is the ordinary case, not an
    // error. The address is the identity, and it is compared ignoring case.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + QStringLiteral("/a.vcf"),
              card(QStringLiteral("Dup"), QStringLiteral("dup@example.org")));
    writeFile(dir.path() + QStringLiteral("/b.vcf"),
              card(QStringLiteral("Dup"), QStringLiteral("DUP@Example.org")));

    const QList<Contact> contacts = ContactStore::loadDirectory(dir.path());
    QCOMPARE(contacts.size(), 1);
}

void TestContactStore::sortsByNameThenAddress()
{
    // Name first, case-insensitively; the address breaks a name tie. The
    // lowercase "bob" is what a case-sensitive sort would get wrong.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + QStringLiteral("/c.vcf"),
              card(QStringLiteral("Charlie"), QStringLiteral("charlie@example.org")));
    writeFile(dir.path() + QStringLiteral("/a.vcf"),
              card(QStringLiteral("Alice"), QStringLiteral("alice@example.org")));
    writeFile(dir.path() + QStringLiteral("/b.vcf"),
              card(QStringLiteral("bob"), QStringLiteral("bob@example.org")));
    writeFile(dir.path() + QStringLiteral("/s1.vcf"),
              card(QStringLiteral("Same"), QStringLiteral("b@example.org")));
    writeFile(dir.path() + QStringLiteral("/s2.vcf"),
              card(QStringLiteral("Same"), QStringLiteral("a@example.org")));

    const QList<Contact> contacts = ContactStore::loadDirectory(dir.path());
    QCOMPARE(contacts.size(), 5);
    QCOMPARE(contacts.at(0).name, QStringLiteral("Alice"));
    QCOMPARE(contacts.at(1).name, QStringLiteral("bob"));
    QCOMPARE(contacts.at(2).name, QStringLiteral("Charlie"));
    QCOMPARE(contacts.at(3).name, QStringLiteral("Same"));
    QCOMPARE(contacts.at(3).email, QStringLiteral("a@example.org"));
    QCOMPARE(contacts.at(4).email, QStringLiteral("b@example.org"));
}

void TestContactStore::skipsFilesThatHoldNoCard()
{
    // One bad card must not cost the other 116. A file with no EMAIL line
    // contributes nothing and must not stop the walk.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + QStringLiteral("/good.vcf"),
              card(QStringLiteral("Good"), QStringLiteral("good@example.org")));
    writeFile(dir.path() + QStringLiteral("/bad.vcf"),
              QStringLiteral("this is not a vcard\nnot a property either\n"));

    const QList<Contact> contacts = ContactStore::loadDirectory(dir.path());
    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.at(0).email, QStringLiteral("good@example.org"));
}

void TestContactStore::skipsFilesThatAreNotVcf()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + QStringLiteral("/notes.txt"),
              card(QStringLiteral("Ignored"), QStringLiteral("ignored@example.org")));
    writeFile(dir.path() + QStringLiteral("/keep.vcf"),
              card(QStringLiteral("Kept"), QStringLiteral("kept@example.org")));

    const QList<Contact> contacts = ContactStore::loadDirectory(dir.path());
    QCOMPARE(contacts.size(), 1);
    QCOMPARE(contacts.at(0).email, QStringLiteral("kept@example.org"));
}

void TestContactStore::missingDirectoryIsEmptyAndQuiet()
{
    // A machine with no vdir is the ordinary case for anyone who is not this
    // user, so a missing path must not warn.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QTest::failOnWarning();
    QVERIFY(ContactStore::loadDirectory(
                dir.path() + QStringLiteral("/does-not-exist"))
                .isEmpty());
}

void TestContactStore::emptyDirectoryIsEmptyAndQuiet()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir(dir.path()).mkpath(QStringLiteral("empty"));
    QTest::failOnWarning();
    QVERIFY(ContactStore::loadDirectory(dir.path() + QStringLiteral("/empty"))
                .isEmpty());
}

QTEST_MAIN(TestContactStore)
#include "test_contactstore.moc"
