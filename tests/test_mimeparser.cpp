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
#include <QTemporaryDir>
#include <QDir>
#include "mimeparser.h"

class TestMimeParser : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void parsesPlainText();
    void prefersHtmlWhenAvailable();
    void fallsBackToPlainWhenHtmlDisabled();
    void collectsInlineCidParts();
    void aBodyCarryingAContentIdStillRenders();
    void decodesQuotedPrintableAttachment();
    void decodesEncodedHeaders();
    void malformedMessageDoesNotCrash();
    void missingFileIsReported();
    void hostileFilenameIsSanitised();
    void savedAttachmentMatchesBytes();
    void safeFilenameStripsPathComponents();
    void pathInsideDirectoryRejectsSiblingPrefix();
    void attachmentFolderNameIsASinglePlainComponent();
    void recipientSummaryPrefersDisplayNames();
    void recipientSummaryKeepsACommaInsideADisplayName();
    void recipientSummaryCollapsesTheOverflow();
    void recipientSummarySurvivesUnusableInput();
    void folderNameSurvivesATimezoneComment();
    void savingABatchNeverOverwrites();

private:
    QString fixture(const QString &name) const
    { return m_fixtureDir + QLatin1Char('/') + name; }

    QString m_fixtureDir;
};

void TestMimeParser::initTestCase()
{
    // FIXTURE_DIR is defined by CMake so the test can run from any cwd.
    m_fixtureDir = QStringLiteral(FIXTURE_DIR);
    QVERIFY2(QDir(m_fixtureDir).exists(), "fixture directory missing");
}

void TestMimeParser::parsesPlainText()
{
    MimeParser parser;
    const ParsedMessage msg = parser.parse(fixture(QStringLiteral("plain.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.subject, QStringLiteral("Plain hello"));
    QCOMPARE(msg.from, QStringLiteral("Alice <alice@example.org>"));
    QVERIFY(msg.plainBody.contains(QStringLiteral("Hello Bob.")));
    QVERIFY(msg.htmlBody.isEmpty());
    QVERIFY(msg.attachments.isEmpty());
}

void TestMimeParser::prefersHtmlWhenAvailable()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("alternative.eml")));

    QVERIFY(msg.ok);
    QVERIFY(msg.htmlBody.contains(QStringLiteral("html version")));
    // The plain alternative is kept so the user can toggle to it.
    QVERIFY(msg.plainBody.contains(QStringLiteral("plain version")));
    QVERIFY(msg.hasHtml());
}

void TestMimeParser::fallsBackToPlainWhenHtmlDisabled()
{
    MimeParser parser;
    const ParsedMessage msg = parser.parse(fixture(QStringLiteral("plain.eml")));

    QVERIFY(!msg.hasHtml());
    QVERIFY(!msg.plainBody.isEmpty());
}

void TestMimeParser::collectsInlineCidParts()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("inline_image.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.inlineParts.size(), 1);
    // Content-ID angle brackets are stripped so it matches the cid: URL body.
    QVERIFY(msg.inlineParts.contains(QStringLiteral("logo@example.org")));

    const InlinePart part = msg.inlineParts.value(QStringLiteral("logo@example.org"));
    QCOMPARE(part.mimeType, QStringLiteral("image/png"));
    // Decoded 1x1 PNG starts with the PNG magic bytes.
    QVERIFY(part.data.startsWith(QByteArray("\x89PNG", 4)));
}

void TestMimeParser::aBodyCarryingAContentIdStillRenders()
{
    // Reported by the user: a bulk sender's message opened blank, with the app
    // saying it had no HTML part.
    //
    // A Content-Id makes a part referenceable, not non-displayable, and setting
    // one on the text/html body is legal and common. collectParts filed any
    // part with an id into inlineParts and returned before the body branches,
    // so such a message parsed with both body slots empty.
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("body_with_content_id.eml")));

    QVERIFY(msg.ok);

    // The body fills its slot despite the id.
    QVERIFY2(msg.hasHtml(), "the html body was swallowed by its own content id");
    QVERIFY(msg.htmlBody.contains(QStringLiteral("Body text.")));

    // And it stays reachable under that id, so a sibling referencing it still
    // resolves. Register first, then assign: the two are independent.
    QVERIFY(msg.inlineParts.contains(QStringLiteral("body@example.org")));
    QCOMPARE(msg.inlineParts.value(QStringLiteral("body@example.org")).mimeType,
             QStringLiteral("text/html"));

    // The genuinely inline image is untouched by the change.
    QVERIFY(msg.inlineParts.contains(QStringLiteral("logo@example.org")));
    QCOMPARE(msg.inlineParts.size(), 2);
}

void TestMimeParser::decodesQuotedPrintableAttachment()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("attachment.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.attachments.size(), 1);
    QCOMPARE(msg.attachments.first().filename, QStringLiteral("notes.txt"));
    QCOMPARE(QString::fromUtf8(msg.attachments.first().data),
             QStringLiteral("café notes"));
}

void TestMimeParser::decodesEncodedHeaders()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("encoded_subject.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.subject, QStringLiteral("Café meeting"));
    QVERIFY(msg.from.contains(QStringLiteral("Älice")));
}

void TestMimeParser::malformedMessageDoesNotCrash()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("truncated.eml")));

    // GMime is tolerant: it recovers the headers and whatever body it found.
    // The requirement is only that parsing terminates and reports something.
    QCOMPARE(msg.subject, QStringLiteral("Truncated"));
}

void TestMimeParser::missingFileIsReported()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("does_not_exist.eml")));

    QVERIFY(!msg.ok);
    QVERIFY(!msg.error.isEmpty());
}

void TestMimeParser::hostileFilenameIsSanitised()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("hostile_filename.eml")));

    QVERIFY(msg.ok);
    QCOMPARE(msg.attachments.size(), 1);

    // The raw header value is preserved for display...
    QVERIFY(msg.attachments.first().filename.contains(QStringLiteral("..")));
    // ...but the name used on disk is reduced to a basename.
    QCOMPARE(msg.attachments.first().safeFilename(), QStringLiteral("pwned.txt"));
}

void TestMimeParser::savedAttachmentMatchesBytes()
{
    MimeParser parser;
    const ParsedMessage msg =
        parser.parse(fixture(QStringLiteral("attachment.eml")));
    QVERIFY(msg.ok);

    QTemporaryDir dir;
    QString error;
    const QString written =
        msg.attachments.first().saveTo(dir.path(), &error);

    QVERIFY2(!written.isEmpty(), qPrintable(error));
    // Never escapes the target directory.
    QVERIFY(written.startsWith(dir.path()));

    QFile f(written);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), msg.attachments.first().data);
}

void TestMimeParser::safeFilenameStripsPathComponents()
{
    // This is the control that genuinely stops traversal: saveTo() always
    // routes through safeFilename() first, so whatever this function
    // guarantees is what actually protects a write to disk. Constructed by
    // hand since these are adversarial names not tied to any fixture.
    Attachment a;
    a.mimeType = QStringLiteral("text/plain");
    a.data = QByteArrayLiteral("x");

    a.filename = QStringLiteral("../../../../tmp/pwned.txt");
    QCOMPARE(a.safeFilename(), QStringLiteral("pwned.txt"));

    a.filename = QStringLiteral("../xyz-evil/x.txt");
    QCOMPARE(a.safeFilename(), QStringLiteral("x.txt"));

    a.filename = QStringLiteral("..\\..\\windows\\evil.txt");
    QCOMPARE(a.safeFilename(), QStringLiteral("evil.txt"));

    a.filename = QStringLiteral("plain.txt");
    QCOMPARE(a.safeFilename(), QStringLiteral("plain.txt"));

    // Nothing usable remains: a generated name is produced instead. Assert
    // its shape rather than an exact value, since it embeds a fresh UUID.
    a.filename = QStringLiteral("..");
    QString generated = a.safeFilename();
    QVERIFY(!generated.isEmpty());
    QVERIFY(generated != QStringLiteral(".."));
    QVERIFY(!generated.contains(QLatin1Char('/')));

    a.filename = QString();
    generated = a.safeFilename();
    QVERIFY(!generated.isEmpty());
    QVERIFY(!generated.contains(QLatin1Char('/')));
}

void TestMimeParser::pathInsideDirectoryRejectsSiblingPrefix()
{
    // Direct test of the containment guard's own comparison, independent of
    // safeFilename() (which always runs first inside saveTo() and would
    // mask a broken guard, since it never produces an escaping path). This
    // targets exactly the defect that was found: a plain string
    // startsWith() incorrectly treats a sibling directory whose name merely
    // extends the target's name (e.g. "/tmp/safe-evil") as contained within
    // it (e.g. "/tmp/safe").
    const QString base = QStringLiteral("/tmp/safe");

    QVERIFY(Attachment::isPathInsideDirectory(base, base + QStringLiteral("/notes.txt")));
    QVERIFY(Attachment::isPathInsideDirectory(base, base + QStringLiteral("/sub/notes.txt")));
    QVERIFY(Attachment::isPathInsideDirectory(base, base));

    QVERIFY(!Attachment::isPathInsideDirectory(base, QStringLiteral("/tmp/safe-evil/x")));
    QVERIFY(!Attachment::isPathInsideDirectory(base, QStringLiteral("/tmp/safe/../etc/passwd")));
    QVERIFY(!Attachment::isPathInsideDirectory(base, QStringLiteral("/etc/passwd")));
}

void TestMimeParser::attachmentFolderNameIsASinglePlainComponent()
{
    const QString validDate = QStringLiteral("Thu, 7 May 2026 16:51:48 +0200");

    // The ordinary case: date prefix so the folders sort chronologically.
    QCOMPARE(attachmentFolderName(validDate, QStringLiteral("Quarterly report")),
             QStringLiteral("2026-05-07 Quarterly report"));

    // A subject is attacker-controlled and is about to become a directory
    // name. None of these may produce anything but one plain component.
    const QStringList hostile = {
        QStringLiteral("../../etc"),
        QStringLiteral("/etc/passwd"),
        QStringLiteral("a/b/c"),
        QStringLiteral(".."),
        QStringLiteral("."),
        QStringLiteral(".hidden"),
        QStringLiteral("with\\backslash"),
        QStringLiteral("null\0byte"),
    };
    for (const QString &subject : hostile) {
        const QString folder = attachmentFolderName(validDate, subject);
        QVERIFY2(!folder.contains(QLatin1Char('/')),
                 qPrintable(QStringLiteral("'%1' -> '%2'").arg(subject, folder)));
        QVERIFY2(!folder.contains(QLatin1Char('\\')),
                 qPrintable(QStringLiteral("'%1' -> '%2'").arg(subject, folder)));
        QVERIFY2(!folder.startsWith(QLatin1Char('.')),
                 qPrintable(QStringLiteral("'%1' -> '%2'").arg(subject, folder)));
        QVERIFY2(folder != QLatin1String("..") && folder != QLatin1String("."),
                 qPrintable(QStringLiteral("'%1' -> '%2'").arg(subject, folder)));
        QVERIFY(!folder.isEmpty());

        // The decisive check: joining it onto a directory cannot escape.
        QVERIFY2(Attachment::isPathInsideDirectory(
                     QStringLiteral("/tmp/parent"),
                     QDir(QStringLiteral("/tmp/parent")).absoluteFilePath(folder)),
                 qPrintable(QStringLiteral("'%1' escaped as '%2'")
                                .arg(subject, folder)));
    }

    // An unparseable Date: is dropped rather than guessed at.
    QCOMPARE(attachmentFolderName(QStringLiteral("not a date"),
                                  QStringLiteral("Subject here")),
             QStringLiteral("Subject here"));

    // Neither a usable date nor a usable subject still yields a name, since
    // the caller is about to create a directory with it.
    const QString generated = attachmentFolderName(QString(), QStringLiteral("///"));
    QVERIFY(!generated.isEmpty());
    QVERIFY(!generated.contains(QLatin1Char('/')));

    // A subject can be far longer than a filesystem component allows.
    const QString huge = attachmentFolderName(validDate, QString(500, QLatin1Char('x')));
    QVERIFY2(huge.size() <= 120,
             qPrintable(QStringLiteral("length %1").arg(huge.size())));
}

void TestMimeParser::folderNameSurvivesATimezoneComment()
{
    // "+0200 (CEST)" is legal per RFC 5322 and common in real mail, but
    // Qt::RFC2822Date rejects the entire string when the comment is present
    // (verified on Qt 6.11). Every such message silently lost its date prefix.
    QCOMPARE(attachmentFolderName(
                 QStringLiteral("Thu, 7 May 2026 16:51:48 +0200 (CEST)"),
                 QStringLiteral("Report")),
             QStringLiteral("2026-05-07 Report"));

    // The same date without the comment must not regress.
    QCOMPARE(attachmentFolderName(
                 QStringLiteral("Thu, 7 May 2026 16:51:48 +0200"),
                 QStringLiteral("Report")),
             QStringLiteral("2026-05-07 Report"));
}

void TestMimeParser::savingABatchNeverOverwrites()
{
    // Saving a thread's attachments with saveTo() destroyed files: several
    // messages in one thread commonly attach the same filename, each write
    // landed on the previous one, and all of them reported success. Sixteen
    // attachments produced ten files.
    QTemporaryDir dir;

    Attachment first;
    first.filename = QStringLiteral("questionario.pdf");
    first.data = QByteArray("first copy");

    Attachment second;
    second.filename = QStringLiteral("questionario.pdf");
    second.data = QByteArray("second copy, different bytes");

    Attachment third;
    third.filename = QStringLiteral("questionario.pdf");
    third.data = QByteArray("third");

    QString error;
    const QString pathA = first.saveWithoutOverwriting(dir.path(), &error);
    const QString pathB = second.saveWithoutOverwriting(dir.path(), &error);
    const QString pathC = third.saveWithoutOverwriting(dir.path(), &error);

    QVERIFY(!pathA.isEmpty());
    QVERIFY(!pathB.isEmpty());
    QVERIFY(!pathC.isEmpty());

    // Three distinct files, and every one still holds its own bytes.
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 3);
    QVERIFY(pathA != pathB);
    QVERIFY(pathB != pathC);

    const auto contentsOf = [](const QString &path) {
        QFile file(path);
        file.open(QIODevice::ReadOnly);
        return file.readAll();
    };
    QCOMPARE(contentsOf(pathA), QByteArray("first copy"));
    QCOMPARE(contentsOf(pathB), QByteArray("second copy, different bytes"));
    QCOMPARE(contentsOf(pathC), QByteArray("third"));

    // The extension is kept whole rather than split at the first dot.
    Attachment tarball;
    tarball.filename = QStringLiteral("archive.tar.gz");
    tarball.data = QByteArray("one");
    Attachment tarballAgain = tarball;
    tarballAgain.data = QByteArray("two");

    QVERIFY(!tarball.saveWithoutOverwriting(dir.path(), &error).isEmpty());
    const QString second_tar =
        tarballAgain.saveWithoutOverwriting(dir.path(), &error);
    QVERIFY(second_tar.endsWith(QStringLiteral(".gz")));
    QVERIFY2(second_tar.contains(QStringLiteral("archive.tar")),
             qPrintable(second_tar));
}

void TestMimeParser::recipientSummaryPrefersDisplayNames()
{
    // A display name where there is one, the address where there is not, so a
    // list does not mix "Mario Rossi" with a bare address for no reason the
    // reader can see.
    QCOMPARE(recipientSummary(
                 QStringLiteral("Mario Rossi <mario@example.org>")),
             QStringLiteral("Mario Rossi"));

    QCOMPARE(recipientSummary(QStringLiteral("info@example.net")),
             QStringLiteral("info@example.net"));

    QCOMPARE(recipientSummary(QStringLiteral(
                 "Mario Rossi <mario@example.org>, info@example.net")),
             QStringLiteral("Mario Rossi, info@example.net"));
}

void TestMimeParser::recipientSummaryKeepsACommaInsideADisplayName()
{
    // The reason this uses GMime rather than QString::split(','). A quoted
    // display name may CONTAIN a comma, and splitting reports three recipients
    // where there are two, with "Mario" alone as one of them.
    const QString summary = recipientSummary(QStringLiteral(
        "\"Rossi, Mario\" <mario@example.org>, info@example.net"));

    QCOMPARE(summary, QStringLiteral("Rossi, Mario, info@example.net"));

    // Stated separately, because the QCOMPARE above would also pass a naive
    // implementation that happened to rejoin the pieces in the same order.
    QVERIFY2(!summary.contains(QStringLiteral("+")),
             "a comma inside a display name was counted as another recipient");
}

void TestMimeParser::recipientSummaryCollapsesTheOverflow()
{
    // "+N" rather than eliding mid-name, mirroring the tag strip's overflow
    // chip: a card has one line for this and a truncated name is worse than an
    // honest count.
    QCOMPARE(recipientSummary(QStringLiteral(
                 "a@example.org, b@example.org, c@example.org, "
                 "d@example.org")),
             QStringLiteral("a@example.org, b@example.org +2"));

    // Exactly at the limit does not collapse: a "+0" would be absurd.
    QCOMPARE(recipientSummary(
                 QStringLiteral("a@example.org, b@example.org")),
             QStringLiteral("a@example.org, b@example.org"));
}

void TestMimeParser::recipientSummarySurvivesUnusableInput()
{
    // The header is untrusted and every one of these is real mail.
    //
    // The empty string is the one that crashes if unguarded:
    // internet_address_list_parse returns NULL for it rather than an empty
    // list, verified against GMime directly.
    QVERIFY(recipientSummary(QString()).isEmpty());
    QVERIFY(recipientSummary(QStringLiteral("")).isEmpty());
    QVERIFY(recipientSummary(QStringLiteral("   ")).isEmpty());

    // Not a crash and not a lie: a group with no members has no names to show.
    const QString group =
        recipientSummary(QStringLiteral("undisclosed-recipients:;"));
    QVERIFY2(!group.contains(QStringLiteral("@")),
             qPrintable(QStringLiteral("a memberless group produced an "
                                       "address: %1").arg(group)));

    // Garbage parses to something or to nothing, but never to a crash.
    recipientSummary(QStringLiteral("<<<>>>"));
    recipientSummary(QStringLiteral("\"unterminated <a@example.org>"));
}

QTEST_MAIN(TestMimeParser)
#include "test_mimeparser.moc"
