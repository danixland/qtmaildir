#include <QSet>
#include <QtTest>
#include "cidschemehandler.h"
#include "htmlbuilder.h"

class TestHtmlBuilder : public QObject
{
    Q_OBJECT
private slots:
    void escapesPlainText();
    void preservesHtmlBodyWhenHtmlRequested();
    void marksQuotedLines();
    void plainTextScriptTagIsNeutralised();
    void buildsThreadWithAllMessages();
    void collapsedMessageShowsStubOnly();
    void threadNamespacesCidUrls();

    // Adversarial additions.
    void namespacesUnquotedCidAttribute();
    void namespacesCaseInsensitiveAttributeName();
    void namespacesCidInBackgroundAttribute();
    void namespacesCidInInlineStyleUrl();
    void namespacesCidInStyleBlock();
    void namespacesMultipleCidRefsOnOneLine();
    void namespacesWhitespaceAroundEquals();
    void namespacedKeyRejectsPrefixContainingSeparator();
};

void TestHtmlBuilder::escapesPlainText()
{
    ParsedMessage msg;
    msg.ok = true;
    msg.plainBody = QStringLiteral("a < b & c > d");

    const QString html = HtmlBuilder::build(msg, HtmlBuilder::ForcePlain);
    QVERIFY(html.contains(QStringLiteral("a &lt; b &amp; c &gt; d")));
}

void TestHtmlBuilder::preservesHtmlBodyWhenHtmlRequested()
{
    ParsedMessage msg;
    msg.ok = true;
    msg.htmlBody = QStringLiteral("<p>hello</p>");

    const QString html = HtmlBuilder::build(msg, HtmlBuilder::PreferHtml);
    QVERIFY(html.contains(QStringLiteral("<p>hello</p>")));
}

void TestHtmlBuilder::marksQuotedLines()
{
    ParsedMessage msg;
    msg.ok = true;
    msg.plainBody = QStringLiteral("reply\n> quoted\nend");

    const QString html = HtmlBuilder::build(msg, HtmlBuilder::ForcePlain);
    QVERIFY(html.contains(QStringLiteral("class=\"quote\"")));
}

void TestHtmlBuilder::plainTextScriptTagIsNeutralised()
{
    ParsedMessage msg;
    msg.ok = true;
    msg.plainBody = QStringLiteral("<script>alert(1)</script>");

    const QString html = HtmlBuilder::build(msg, HtmlBuilder::ForcePlain);
    // Escaped, not embedded. (JavaScript is also disabled at the profile level,
    // so this is the second of two independent defences.)
    QVERIFY(!html.contains(QStringLiteral("<script>")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
}

void TestHtmlBuilder::buildsThreadWithAllMessages()
{
    ThreadRenderItem first;
    first.message.ok = true;
    first.message.subject = QStringLiteral("First");
    first.message.from = QStringLiteral("Alice");
    first.message.plainBody = QStringLiteral("first body");
    first.expanded = true;

    ThreadRenderItem second;
    second.message.ok = true;
    second.message.subject = QStringLiteral("Second");
    second.message.from = QStringLiteral("Bob");
    second.message.plainBody = QStringLiteral("second body");
    second.expanded = true;

    const QString html =
        HtmlBuilder::buildThread({ first, second }, HtmlBuilder::ForcePlain);

    QVERIFY(html.contains(QStringLiteral("first body")));
    QVERIFY(html.contains(QStringLiteral("second body")));
    // Each message is its own section, so per-message CSS and anchors work.
    QCOMPARE(html.count(QStringLiteral("class=\"message\"")), 2);
}

void TestHtmlBuilder::collapsedMessageShowsStubOnly()
{
    ThreadRenderItem item;
    item.message.ok = true;
    item.message.from = QStringLiteral("Carol");
    item.message.subject = QStringLiteral("Old news");
    item.message.plainBody = QStringLiteral("secret body text");
    item.expanded = false;

    const QString html =
        HtmlBuilder::buildThread({ item }, HtmlBuilder::ForcePlain);

    // Unmatched messages collapse to a one-line stub; the body is not emitted.
    QVERIFY(html.contains(QStringLiteral("Carol")));
    QVERIFY(!html.contains(QStringLiteral("secret body text")));
    QVERIFY(html.contains(QStringLiteral("class=\"stub\"")));
}

void TestHtmlBuilder::threadNamespacesCidUrls()
{
    // Two messages in one document may both reference cid:logo@x. Without
    // namespacing, the second would show the first's image.
    ThreadRenderItem first;
    first.message.ok = true;
    first.message.htmlBody =
        QStringLiteral("<img src=\"cid:logo@example.org\">");
    first.expanded = true;
    first.cidPrefix = QStringLiteral("m0");

    ThreadRenderItem second;
    second.message.ok = true;
    second.message.htmlBody =
        QStringLiteral("<img src=\"cid:logo@example.org\">");
    second.expanded = true;
    second.cidPrefix = QStringLiteral("m1");

    const QString html =
        HtmlBuilder::buildThread({ first, second }, HtmlBuilder::PreferHtml);

    QVERIFY(html.contains(QStringLiteral("cid:m0!logo@example.org")));
    QVERIFY(html.contains(QStringLiteral("cid:m1!logo@example.org")));
    // The bare form must not survive, or it would resolve ambiguously.
    QVERIFY(!html.contains(QStringLiteral("\"cid:logo@example.org\"")));
}

void TestHtmlBuilder::namespacesUnquotedCidAttribute()
{
    // <img src=cid:logo@example.org> is valid HTML. An unquoted reference
    // that survives un-namespaced would resolve against the WRONG message's
    // parts map (or none), so it must be rewritten too.
    const QString html = HtmlBuilder::namespaceCids(
        QStringLiteral("<img src=cid:logo@example.org>"), QStringLiteral("m0"));
    QVERIFY(html.contains(QStringLiteral("cid:m0!logo@example.org")));
    QVERIFY(!html.contains(QStringLiteral("src=cid:logo@example.org")));
}

void TestHtmlBuilder::namespacesCaseInsensitiveAttributeName()
{
    const QString html = HtmlBuilder::namespaceCids(
        QStringLiteral("<img SRC = \"cid:logo@example.org\">"), QStringLiteral("m0"));
    QVERIFY(html.contains(QStringLiteral("cid:m0!logo@example.org")));
}

void TestHtmlBuilder::namespacesCidInBackgroundAttribute()
{
    // HTML email frequently sets background images via the background=
    // attribute on <table>/<td>/<body>.
    const QString html = HtmlBuilder::namespaceCids(
        QStringLiteral("<table background=\"cid:bg@example.org\">"),
        QStringLiteral("m0"));
    QVERIFY(html.contains(QStringLiteral("cid:m0!bg@example.org")));
}

void TestHtmlBuilder::namespacesCidInInlineStyleUrl()
{
    const QString html = HtmlBuilder::namespaceCids(
        QStringLiteral("<div style=\"background-image:url(cid:bg@example.org)\">"),
        QStringLiteral("m0"));
    QVERIFY(html.contains(QStringLiteral("cid:m0!bg@example.org")));
}

void TestHtmlBuilder::namespacesCidInStyleBlock()
{
    const QString html = HtmlBuilder::namespaceCids(
        QStringLiteral("<style>.logo{background:url('cid:bg@example.org')}</style>"),
        QStringLiteral("m0"));
    QVERIFY(html.contains(QStringLiteral("cid:m0!bg@example.org")));
}

void TestHtmlBuilder::namespacesMultipleCidRefsOnOneLine()
{
    // Guards against a greedy [^"']+ eating past the first closing quote.
    const QString html = HtmlBuilder::namespaceCids(
        QStringLiteral("<img src=\"cid:a@x\"><img src=\"cid:b@x\">"),
        QStringLiteral("m0"));
    QVERIFY(html.contains(QStringLiteral("cid:m0!a@x")));
    QVERIFY(html.contains(QStringLiteral("cid:m0!b@x")));
    // A greedy [^"']+ would eat past the first closing quote and swallow the
    // second tag's markup into the first cid value; guard against that by
    // requiring the first tag to close immediately after its own value.
    QVERIFY(html.contains(QStringLiteral("cid:m0!a@x\"><img")));
}

void TestHtmlBuilder::namespacesWhitespaceAroundEquals()
{
    const QString html = HtmlBuilder::namespaceCids(
        QStringLiteral("<img src\n = \n\"cid:logo@example.org\">"),
        QStringLiteral("m0"));
    QVERIFY(html.contains(QStringLiteral("cid:m0!logo@example.org")));
}

void TestHtmlBuilder::namespacedKeyRejectsPrefixContainingSeparator()
{
    // The property that actually matters, and holds even in release builds
    // where Q_ASSERT is compiled out: distinct (prefix, id) pairs, drawn from
    // the documented "m<index>" generator form plus hostile Content-IDs
    // (including ones containing '!', a percent-encoded '!', and several
    // '!'s), always produce distinct keys, and the key always splits at its
    // FIRST '!' back to exactly the original prefix. That only holds because
    // prefixes generated in the "m<index>" form are themselves '!'-free; the
    // attacker only ever controls the id half, which sits after the first
    // (and only guaranteed-separator) '!'.
    const QStringList prefixes = { QStringLiteral("m0"), QStringLiteral("m1"),
                                    QStringLiteral("m2"), QStringLiteral("m10") };
    const QStringList hostileIds = {
        QStringLiteral("logo@example.org"),
        QStringLiteral("a!b@x"),
        QStringLiteral("a!b!c@x"),
        QStringLiteral("%21encoded@x"),
        QStringLiteral(""),
        QStringLiteral("!leading@x"),
        QStringLiteral("trailing!@x"),
    };

    QSet<QString> seenKeys;
    for (const QString &prefix : prefixes) {
        for (const QString &id : hostileIds) {
            const QString key = CidSchemeHandler::namespacedKey(prefix, id);

            // Distinctness: no other (prefix, id) pair already produced this
            // exact key.
            QVERIFY2(!seenKeys.contains(key),
                     qPrintable(QStringLiteral("collision for key '%1'").arg(key)));
            seenKeys.insert(key);

            // Splitting at the FIRST '!' always recovers the original
            // prefix, regardless of how many '!' the hostile id contributes.
            const qsizetype sep = key.indexOf(QLatin1Char('!'));
            QVERIFY(sep != -1);
            QCOMPARE(key.left(sep), prefix);
            QCOMPARE(key.mid(sep + 1), id);
        }
    }

    // Same property, exercised through HtmlBuilder::namespaceCids, which
    // performs the identical concatenation independently: the resulting URL
    // must contain the CidSchemeHandler-computed key verbatim.
    const QString html = HtmlBuilder::namespaceCids(
        QStringLiteral("<img src=\"cid:a!b@x\">"), QStringLiteral("m0"));
    const QString expectedKey = CidSchemeHandler::namespacedKey(
        QStringLiteral("m0"), QStringLiteral("a!b@x"));
    QVERIFY(html.contains(QStringLiteral("cid:%1").arg(expectedKey)));
}

QTEST_MAIN(TestHtmlBuilder)
#include "test_htmlbuilder.moc"
