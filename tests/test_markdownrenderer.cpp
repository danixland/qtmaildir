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

#include "markdownrenderer.h"

/// The extension configuration cmark-gfm renders the composer's body with.
///
/// No QApplication is needed here: MarkdownRenderer is a pure function over
/// strings, so QTEST_APPLESS_MAIN avoids pulling in a platform plugin for a
/// test that has nothing to do with widgets.
class TestMarkdownRenderer : public QObject
{
    Q_OBJECT
private slots:
    void commonMarkBasicsRender();
    void autolinkTurnsABareUrlIntoALink();
    void strikethroughRenders();
    void tasklistRenders();
    void tablesAreNotEnabled();
    void rawHtmlIsSuppressed();
    void unsafeLinksAreStripped();
    void accentedTextSurvivesAsUtf8();
    void emptyInputProducesEmptyOutput();
};

void TestMarkdownRenderer::commonMarkBasicsRender()
{
    const QString html = MarkdownRenderer::toHtml(
        QStringLiteral("**bold** *italic* `code`"));
    QVERIFY2(html.contains(QStringLiteral("<strong>")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("<em>")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("<code>")), qPrintable(html));
}

void TestMarkdownRenderer::autolinkTurnsABareUrlIntoALink()
{
    // The whole reason cmark-gfm was chosen over plain cmark. Under
    // CommonMark a bare URL is text, and a bare URL in mail is expected to
    // be clickable.
    const QString html = MarkdownRenderer::toHtml(
        QStringLiteral("see https://example.org for details"));
    QVERIFY2(html.contains(QStringLiteral("<a href=\"https://example.org\"")),
             qPrintable(html));
}

void TestMarkdownRenderer::strikethroughRenders()
{
    const QString html = MarkdownRenderer::toHtml(QStringLiteral("~~gone~~"));
    QVERIFY2(html.contains(QStringLiteral("<del>gone</del>")), qPrintable(html));
}

void TestMarkdownRenderer::tasklistRenders()
{
    // Known ceiling: many mail clients strip the checkbox, so those
    // recipients see the item with no marker. The plain part still carries
    // the literal "- [ ]", so nothing is lost, only the HTML rendering.
    const QString html = MarkdownRenderer::toHtml(
        QStringLiteral("- [ ] todo\n- [x] done"));
    QVERIFY2(html.contains(QStringLiteral("type=\"checkbox\"")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("checked")), qPrintable(html));
}

void TestMarkdownRenderer::tablesAreNotEnabled()
{
    // Deliberately off: tables render badly across mail clients regardless of
    // who generates them. The extension EXISTS in the library, so this
    // asserts a decision rather than a limitation, and would silently start
    // passing the wrong way if someone attached it "for completeness".
    const QString html = MarkdownRenderer::toHtml(
        QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |"));
    QVERIFY2(!html.contains(QStringLiteral("<table")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("| a | b |")), qPrintable(html));
}

void TestMarkdownRenderer::rawHtmlIsSuppressed()
{
    // Safe mode (the cmark-gfm 0.29 default, not CMARK_OPT_SAFE, which is a
    // no-op in this version, see markdownrenderer.cpp). The body is the
    // user's own text, but a body that can inject markup into its own
    // generated HTML part is a sharp edge with no upside.
    //
    // Asserted on the actual placeholder rather than only "no <script>",
    // because the weaker assertion would still pass with CMARK_OPT_UNSAFE
    // set by mistake, as long as something ELSE in the string also matched
    // "not <script>" and "contains after" (measured: it does not distinguish
    // safe from unsafe mode on its own). "raw HTML omitted" is what safe mode
    // actually emits in place of the tag.
    const QString html = MarkdownRenderer::toHtml(
        QStringLiteral("<script>alert(1)</script>\n\nafter"));
    QVERIFY2(!html.contains(QStringLiteral("<script>")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("raw HTML omitted")), qPrintable(html));
    QVERIFY2(html.contains(QStringLiteral("after")), qPrintable(html));
}

void TestMarkdownRenderer::unsafeLinksAreStripped()
{
    // A protection this gets for free from safe mode, and previously
    // asserted nothing about: a javascript: link is replaced with an empty
    // href rather than passed through. The body is the user's own text, but
    // it is rendered into an HTML part sent to other people, so a
    // javascript: link surviving into that part would be a real defect, not
    // a cosmetic one.
    const QString html = MarkdownRenderer::toHtml(
        QStringLiteral("[click](javascript:alert(1))"));
    QVERIFY2(!html.contains(QStringLiteral("javascript:")), qPrintable(html));
}

void TestMarkdownRenderer::accentedTextSurvivesAsUtf8()
{
    // This user writes Italian, so accented text is every message rather
    // than an edge case, and a UTF-8 round trip through a C library is
    // exactly where it would be lost.
    const QString source = QString::fromUtf8("perch\xC3\xA9 \xC3\xA8 cos\xC3\xAC");
    const QString html = MarkdownRenderer::toHtml(source);
    QVERIFY2(html.contains(source), qPrintable(html));
}

void TestMarkdownRenderer::emptyInputProducesEmptyOutput()
{
    // reply_no_quote opens a composer with an empty body and it must not
    // produce a stray paragraph or crash the renderer.
    const QString html = MarkdownRenderer::toHtml(QString());
    QVERIFY2(html.trimmed().isEmpty(), qPrintable(html));
}

QTEST_APPLESS_MAIN(TestMarkdownRenderer)
#include "test_markdownrenderer.moc"
