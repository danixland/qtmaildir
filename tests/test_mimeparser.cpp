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
    void decodesQuotedPrintableAttachment();
    void decodesEncodedHeaders();
    void malformedMessageDoesNotCrash();
    void missingFileIsReported();
    void hostileFilenameIsSanitised();
    void savedAttachmentMatchesBytes();

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

QTEST_MAIN(TestMimeParser)
#include "test_mimeparser.moc"
