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

#include <QPalette>
#include <QRegularExpression>
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

    // Theming.
    void aDarkPaletteProducesADarkDocument();
    void everyColourComesFromThePalette();

    // The placeholder pane (item 30).
    void placeholderPicksTheBrandSetFromTheDesktopTheme();
    void placeholderHelperBecomesALinkOnItsQuery();
    void placeholderHelperWithoutAQueryIsNotALink();
    void placeholderEscapesHelperText();
    void placeholderEmbedsItsFontsRatherThanFetchingThem();
    void placeholderReferencesNoRemoteResource();
    void placeholderStyleHasNoUnsubstitutedTokens();
    void theBodyAlwaysGetsABackground();
    void aSendersOwnHtmlIsNotRecoloured();
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

/// A palette with unmistakable colours, so a hardcoded value cannot pass by
/// coincidentally resembling a real theme.
static QPalette makeTestPalette(const QColor &window, const QColor &text)
{
    QPalette palette;
    palette.setColor(QPalette::Base, window);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::WindowText, text);
    return palette;
}

void TestHtmlBuilder::aDarkPaletteProducesADarkDocument()
{
    // The whole point of the item: the CSS was hardcoded light, so a user on a
    // dark desktop read plain-text mail as black on white inside a dark window.
    ParsedMessage msg;
    msg.plainBody = QStringLiteral("hello");

    const HtmlBuilder::Palette dark = HtmlBuilder::paletteFrom(
        makeTestPalette(QColor(0x12, 0x34, 0x56), QColor(0xab, 0xcd, 0xef)));

    const QString html =
        HtmlBuilder::build(msg, HtmlBuilder::ForcePlain, dark);

    QVERIFY2(html.contains(QStringLiteral("#123456")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("#abcdef")), qPrintable(html));
}

void TestHtmlBuilder::everyColourComesFromThePalette()
{
    // A single leftover literal is the whole defect, and it survives a test
    // that only checks the palette colours are present. Assert the negative:
    // no hex colour appears that the palette did not put there.
    ParsedMessage msg;
    msg.plainBody = QStringLiteral("> quoted\nplain");

    const HtmlBuilder::Palette dark = HtmlBuilder::paletteFrom(
        makeTestPalette(QColor(0x12, 0x34, 0x56), QColor(0xab, 0xcd, 0xef)));
    const QString html =
        HtmlBuilder::build(msg, HtmlBuilder::ForcePlain, dark);

    // Only the <style> block: a sender's own HTML is not ours to police, and
    // the body of this message carries no colours anyway.
    const qsizetype start = html.indexOf(QStringLiteral("<style>"));
    const qsizetype end = html.indexOf(QStringLiteral("</style>"));
    QVERIFY(start >= 0 && end > start);
    const QString style = html.mid(start, end - start);

    static const QRegularExpression hex(QStringLiteral("#[0-9a-fA-F]{3,8}\\b"));
    auto it = hex.globalMatch(style);
    QSet<QString> found;
    while (it.hasNext())
        found.insert(it.next().captured(0).toLower());

    // Every colour in the stylesheet must be one the palette supplied. The
    // derived ones (a border, a dimmed label) are blends of those, so they are
    // listed by the builder rather than being free-floating literals.
    const QSet<QString> allowed = {
        dark.background.name().toLower(), dark.text.name().toLower(),
        dark.dim.name().toLower(),        dark.border.name().toLower(),
        dark.quote.name().toLower(),
    };

    for (const QString &colour : found) {
        QVERIFY2(allowed.contains(colour),
                 qPrintable(QStringLiteral("stylesheet carries '%1', which the "
                                           "palette did not supply: a "
                                           "hardcoded colour survives")
                                .arg(colour)));
    }
}

void TestHtmlBuilder::placeholderPicksTheBrandSetFromTheDesktopTheme()
{
    // The brand colours are fixed, deliberately: this is the one place where
    // the desktop palette does NOT supply the values. What the desktop decides
    // is which of the two sets is used, and getting that backwards is the
    // failure the item warns about, a light lockup on a dark desktop.
    const HtmlBuilder::BrandPalette dark = HtmlBuilder::brandPaletteFrom(
        makeTestPalette(QColor(0x1a, 0x1a, 0x1a), QColor(0xee, 0xee, 0xee)));
    const HtmlBuilder::BrandPalette light = HtmlBuilder::brandPaletteFrom(
        makeTestPalette(QColor(0xff, 0xff, 0xff), QColor(0x11, 0x11, 0x11)));

    QCOMPARE(dark.background.name(), QStringLiteral("#060b10"));
    QCOMPARE(light.background.name(), QStringLiteral("#ffffff"));

    // Not merely different: the right way round. A set whose background is
    // darker than its text is the dark set, whichever values it holds.
    QVERIFY(dark.background.lightnessF() < dark.title.lightnessF());
    QVERIFY(light.background.lightnessF() > light.title.lightnessF());
}

void TestHtmlBuilder::placeholderHelperBecomesALinkOnItsQuery()
{
    // JavaScript is off in this profile, so a helper can only be actionable by
    // being a real link that the page's navigation handler intercepts.
    const QString html = HtmlBuilder::buildPlaceholder(
        { { QStringLiteral("12 unread"), QStringLiteral("tag:unread") } },
        QStringLiteral("0.10.0"),
        HtmlBuilder::brandPaletteFrom(QPalette()));

    QVERIFY(html.contains(QStringLiteral("href=\"qtmaildir-query:tag%3Aunread\"")));
    QVERIFY(html.contains(QStringLiteral("12 unread")));
}

void TestHtmlBuilder::placeholderHelperWithoutAQueryIsNotALink()
{
    // The sync line reports a state rather than naming a search, so clicking it
    // must not run an empty query and wipe the thread list.
    const QString html = HtmlBuilder::buildPlaceholder(
        { { QStringLiteral("3 edits waiting to sync"), QString() } },
        QStringLiteral("0.10.0"),
        HtmlBuilder::brandPaletteFrom(QPalette()));

    QVERIFY(html.contains(QStringLiteral("3 edits waiting to sync")));
    QVERIFY(!html.contains(QStringLiteral("qtmaildir-query:")));
}

void TestHtmlBuilder::placeholderEscapesHelperText()
{
    // A helper label carries a count this code produced, but the query half is
    // built from configuration and a saved query is user-written. Neither may
    // reach the document unescaped, and the query is doubly encoded: percent
    // for the URL, then HTML for the attribute.
    const QString html = HtmlBuilder::buildPlaceholder(
        { { QStringLiteral("<script>alert(1)</script>"),
            QStringLiteral("tag:\"a\"><script>") } },
        QStringLiteral("0.10.0"),
        HtmlBuilder::brandPaletteFrom(QPalette()));

    QVERIFY(!html.contains(QStringLiteral("<script>")));
    QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
}

void TestHtmlBuilder::placeholderEmbedsItsFontsRatherThanFetchingThem()
{
    // The mockup @imports Google Fonts, which the interceptor blocks by design.
    // The fonts ship in the binary and must arrive as data: URIs, or the pane
    // silently falls back to a system font and stops looking like the brand.
    const QString html = HtmlBuilder::buildPlaceholder(
        {}, QStringLiteral("0.10.0"),
        HtmlBuilder::brandPaletteFrom(QPalette()));

    QVERIFY(!html.contains(QStringLiteral("fonts.googleapis.com")));

    // BOTH faces, each with a real payload. Counting @font-face rules or
    // checking the document's total size passes with one face missing, since
    // the other is large enough on its own to carry either check: a mutation
    // pointing one src at a nonexistent resource survived exactly that test.
    // A missing resource yields an empty src, so the length is what catches it.
    static const QRegularExpression src(
        QStringLiteral("src: url\\('data:font/woff2;base64,([^']*)'\\)"));
    auto it = src.globalMatch(html);
    int faces = 0;
    while (it.hasNext()) {
        ++faces;
        QVERIFY(it.next().captured(1).size() > 1000);
    }
    QCOMPARE(faces, 2);
}

void TestHtmlBuilder::placeholderReferencesNoRemoteResource()
{
    // The load-bearing security check, asserted as a negative for the same
    // reason everyColourComesFromThePalette is: one leftover reference is the
    // entire defect, and it would be invisible because the interceptor blocks
    // it and the pane just renders slightly wrong.
    const QString html = HtmlBuilder::buildPlaceholder(
        { { QStringLiteral("12 unread"), QStringLiteral("tag:unread") } },
        QStringLiteral("0.10.0"),
        HtmlBuilder::brandPaletteFrom(QPalette()));

    QVERIFY(!html.contains(QStringLiteral("//fonts")));
    QVERIFY(!html.contains(QStringLiteral("@import")));

    // The one http: URL is the SVG namespace, which is an identifier and never
    // fetched. Asserting its exact value rather than excluding the scheme
    // wholesale: a second http: URL appearing later would be a real resource.
    static const QRegularExpression http(QStringLiteral("http://[^\"' ]*"));
    auto plain = http.globalMatch(html);
    while (plain.hasNext()) {
        QCOMPARE(plain.next().captured(0),
                 QStringLiteral("http://www.w3.org/2000/svg"));
    }

    // Every https: URL must be the footer's website link, which is a link the
    // user clicks and not a resource the document fetches.
    static const QRegularExpression https(QStringLiteral("https://[^\"' ]*"));
    auto it = https.globalMatch(html);
    while (it.hasNext()) {
        QCOMPARE(it.next().captured(0),
                 QStringLiteral("https://danix.xyz/qtmaildir"));
    }
}

void TestHtmlBuilder::placeholderStyleHasNoUnsubstitutedTokens()
{
    // The defect this exists for shipped once and was invisible. The template
    // used QString::arg with "%%" for every CSS percentage, and arg() does NOT
    // collapse "%%" into "%", so the stylesheet reached the browser carrying
    // "50%%". Each declaration holding one was dropped as invalid, which
    // silently disabled the grid mask, the glow and both radial gradients. The
    // pane still rendered, still looked plausible, and a geometry probe that
    // happened to measure only percentage-free properties reported it correct.
    const QString html = HtmlBuilder::buildPlaceholder(
        { { QStringLiteral("12 unread"), QStringLiteral("tag:unread") } },
        QStringLiteral("0.10.0"),
        HtmlBuilder::brandPaletteFrom(QPalette()));

    const qsizetype start = html.indexOf(QStringLiteral("<style>"));
    const qsizetype end = html.indexOf(QStringLiteral("</style>"));
    QVERIFY(start >= 0 && end > start);
    const QString style = html.mid(start, end - start);

    // No doubled percent survives into the document.
    QVERIFY2(!style.contains(QStringLiteral("%%")),
             "the stylesheet carries '%%', which CSS rejects: every rule "
             "containing one is silently dropped");

    // No token went unreplaced. A renamed colour would otherwise leave
    // '@ACCENT@' sitting in the CSS as a dropped declaration.
    static const QRegularExpression token(QStringLiteral("@[A-Z_]+@"));
    const QRegularExpressionMatch leftover = token.match(style);
    QVERIFY2(!leftover.hasMatch(),
             qPrintable(QStringLiteral("unsubstituted token '%1' in the "
                                       "stylesheet").arg(leftover.captured(0))));

    // The three effects the bug disabled, each asserted by name so that
    // deleting one is a test failure rather than a silent visual regression.
    QVERIFY(style.contains(QStringLiteral("mask-image")));
    QVERIFY(style.contains(QStringLiteral("radial-gradient")));
    QVERIFY(style.contains(QStringLiteral("aspect-ratio")));
}

void TestHtmlBuilder::theBodyAlwaysGetsABackground()
{
    // The original CSS set no background at all, which is why the pane was
    // white: the web view's default showed through regardless of the desktop.
    ParsedMessage msg;
    msg.plainBody = QStringLiteral("hello");

    const HtmlBuilder::Palette dark = HtmlBuilder::paletteFrom(
        makeTestPalette(QColor(0x12, 0x34, 0x56), QColor(0xab, 0xcd, 0xef)));
    const QString html =
        HtmlBuilder::build(msg, HtmlBuilder::ForcePlain, dark);

    const qsizetype start = html.indexOf(QStringLiteral("<style>"));
    const qsizetype end = html.indexOf(QStringLiteral("</style>"));
    const QString style = html.mid(start, end - start);

    static const QRegularExpression bodyRule(
        QStringLiteral("body\\s*\\{[^}]*background[^}]*\\}"));
    QVERIFY2(bodyRule.match(style).hasMatch(),
             qPrintable(QStringLiteral("body has no background rule:\n") + style));
}

void TestHtmlBuilder::aSendersOwnHtmlIsNotRecoloured()
{
    // Scope, asserted so it does not drift: an HTML message brings its own
    // styling and this change must not start rewriting it. A newsletter that
    // sets its own white background stays white, and that is correct.
    ParsedMessage msg;
    msg.htmlBody = QStringLiteral(
        "<div style=\"background:#ffffff;color:#000000\">hi</div>");

    const HtmlBuilder::Palette dark = HtmlBuilder::paletteFrom(
        makeTestPalette(QColor(0x12, 0x34, 0x56), QColor(0xab, 0xcd, 0xef)));
    const QString html =
        HtmlBuilder::build(msg, HtmlBuilder::PreferHtml, dark);

    QVERIFY(html.contains(QStringLiteral("background:#ffffff")));
    QVERIFY(html.contains(QStringLiteral("color:#000000")));
}

QTEST_MAIN(TestHtmlBuilder)
#include "test_htmlbuilder.moc"
