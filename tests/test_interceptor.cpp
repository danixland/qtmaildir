#include <QtTest>
#include "requestinterceptor.h"

class TestInterceptor : public QObject
{
    Q_OBJECT
private slots:
    void blocksRemoteHttpByDefault();
    void blocksRemoteHttpsByDefault();
    void blocksFileUrlsAlways();
    void allowsCidForCurrentMessage();
    void blocksCidForForeignMessage();
    void allowRemoteFlagPermitsHttpButNotFile();
    void recordsThatSomethingWasBlocked();
    void resetClearsBlockedFlag();

    // Adversarial additions.
    void schemeIsCaseInsensitiveAndStillBlocked();
    void qtmaildirSchemeIsCaseInsensitiveAllow();
    void qtmaildirDocumentUrlIsAllowed();
    void qtmaildirOtherPathIsBlocked();
    void qtmaildirBlockedWhenNoDocumentUrlSet();
    void resetForNewMessageDoesNotClearDocumentUrl();
    void cidUrlDoesNotParseAsUserinfo();
    void cidPercentEncodingDoesNotBypassAllowlist();
    void javascriptSchemeBlocked();
    void dataSchemeBlocked();
    void blobSchemeBlocked();
    void aboutSchemeBlocked();
    void chromeSchemeBlocked();
    void qrcSchemeBlocked();
    void filesystemSchemeBlocked();
    void protocolRelativeUrlBlocked();
    void emptyUrlBlocked();
    void blankUrlBlocked();
    void colonOnlyUrlBlocked();
    void fragmentOnlyUrlBlocked();
};

void TestInterceptor::blocksRemoteHttpByDefault()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("http://tracker.example/pixel.gif"))));
}

void TestInterceptor::blocksRemoteHttpsByDefault()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("https://cdn.example/style.css"))));
}

void TestInterceptor::blocksFileUrlsAlways()
{
    RequestInterceptor interceptor;
    interceptor.setAllowRemote(true);
    // Even with remote content explicitly allowed, local files stay blocked:
    // a message must never read the filesystem.
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("file:///etc/passwd"))));
}

void TestInterceptor::allowsCidForCurrentMessage()
{
    RequestInterceptor interceptor;
    interceptor.setAllowedCids({ QStringLiteral("logo@example.org") });
    QVERIFY(interceptor.shouldAllow(QUrl(QStringLiteral("cid:logo@example.org"))));
}

void TestInterceptor::blocksCidForForeignMessage()
{
    RequestInterceptor interceptor;
    interceptor.setAllowedCids({ QStringLiteral("logo@example.org") });
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("cid:other@example.org"))));
}

void TestInterceptor::allowRemoteFlagPermitsHttpButNotFile()
{
    RequestInterceptor interceptor;
    interceptor.setAllowRemote(true);
    QVERIFY(interceptor.shouldAllow(QUrl(QStringLiteral("https://cdn.example/img.png"))));
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("file:///etc/passwd"))));
}

void TestInterceptor::recordsThatSomethingWasBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.blockedAnything());
    interceptor.shouldAllow(QUrl(QStringLiteral("http://tracker.example/p.gif")));
    // Drives the "Remote content blocked" banner in the message header.
    QVERIFY(interceptor.blockedAnything());
}

void TestInterceptor::resetClearsBlockedFlag()
{
    RequestInterceptor interceptor;
    interceptor.shouldAllow(QUrl(QStringLiteral("http://tracker.example/p.gif")));
    QVERIFY(interceptor.blockedAnything());

    interceptor.resetForNewMessage();
    QVERIFY(!interceptor.blockedAnything());
    // Remote permission never carries over to the next message.
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("https://cdn.example/x.png"))));
}

void TestInterceptor::schemeIsCaseInsensitiveAndStillBlocked()
{
    // QUrl::scheme() normalizes to lowercase, so "HTTP://..." must still hit
    // the http branch (and be blocked without allowRemote), not fall through
    // unexpectedly to an allow path.
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("HTTP://tracker.example/pixel.gif"))));
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("HTTPS://tracker.example/pixel.gif"))));

    interceptor.setAllowRemote(true);
    QVERIFY(interceptor.shouldAllow(QUrl(QStringLiteral("HTTP://tracker.example/pixel.gif"))));
}

void TestInterceptor::qtmaildirSchemeIsCaseInsensitiveAllow()
{
    // QUrl normalizes scheme (and host, for authority-form URLs) to lowercase
    // on parse, so a differently-cased spelling of the exact document URL
    // still compares equal via QUrl::operator== (verified empirically:
    // QUrl("qtmaildir://message") == QUrl("QTMAILDIR://message") is true).
    RequestInterceptor interceptor;
    interceptor.setDocumentUrl(QUrl(QStringLiteral("qtmaildir://body/index.html")));
    QVERIFY(interceptor.shouldAllow(QUrl(QStringLiteral("QTMAILDIR://body/index.html"))));
}

void TestInterceptor::qtmaildirDocumentUrlIsAllowed()
{
    RequestInterceptor interceptor;
    const QUrl doc(QStringLiteral("qtmaildir://message"));
    interceptor.setDocumentUrl(doc);
    QVERIFY(interceptor.shouldAllow(doc));
}

void TestInterceptor::qtmaildirOtherPathIsBlocked()
{
    // Defense in depth: the qtmaildir: scheme is trusted only for the exact
    // document URL the application itself set, never for the whole scheme.
    // A hostile message body can put any qtmaildir: URL in <img src> or
    // <link href>; none of these variants may pass.
    RequestInterceptor interceptor;
    interceptor.setDocumentUrl(QUrl(QStringLiteral("qtmaildir://message")));

    // Path traversal off the document URL.
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("qtmaildir://message/../etc"))));
    // A different qtmaildir origin entirely.
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("qtmaildir://other"))));
    // Opaque (non-authority) form of the scheme.
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("qtmaildir:whatever"))));
    // A sub-path of the document URL is still not the document URL itself.
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("qtmaildir://message/cid/foo"))));

    QVERIFY(interceptor.blockedAnything());
}

void TestInterceptor::qtmaildirBlockedWhenNoDocumentUrlSet()
{
    // Fail closed: if MessageView forgets to call setDocumentUrl(), nothing
    // on the qtmaildir: scheme should be reachable, not even the URL that
    // would otherwise be the legitimate document.
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("qtmaildir://message"))));
}

void TestInterceptor::resetForNewMessageDoesNotClearDocumentUrl()
{
    // The document/base URL is a property of the view's current navigation,
    // not of an individual message's content, so switching to a new message
    // (resetForNewMessage) must not force MessageView to re-supply it.
    RequestInterceptor interceptor;
    const QUrl doc(QStringLiteral("qtmaildir://message"));
    interceptor.setDocumentUrl(doc);
    interceptor.shouldAllow(QUrl(QStringLiteral("http://tracker.example/p.gif")));
    QVERIFY(interceptor.blockedAnything());

    interceptor.resetForNewMessage();

    QVERIFY(!interceptor.blockedAnything());
    QCOMPARE(interceptor.documentUrl(), doc);
    QVERIFY(interceptor.shouldAllow(doc));
}

void TestInterceptor::cidUrlDoesNotParseAsUserinfo()
{
    // Pin down QUrl's actual parsing of a cid: URL containing '@', so a
    // future Qt version change would be caught here rather than silently
    // breaking the allowlist comparison in shouldAllow().
    QUrl url(QStringLiteral("cid:logo@example.org"));
    QCOMPARE(url.path(), QStringLiteral("logo@example.org"));
    QCOMPARE(url.host(), QString());
    QCOMPARE(url.userName(), QString());

    RequestInterceptor interceptor;
    interceptor.setAllowedCids({ QStringLiteral("logo@example.org") });
    QVERIFY(interceptor.shouldAllow(url));
}

void TestInterceptor::cidPercentEncodingDoesNotBypassAllowlist()
{
    // QUrl::path() returns the percent-DECODED form (verified empirically:
    // QUrl("cid:%6Cogo@example.org").path() == "logo@example.org", and this
    // holds for full-string encodings too). A percent-encoded spelling of an
    // allowed id therefore compares equal to that same allowed id, which is
    // correct URI equivalence, not a bypass: decoding cannot turn a foreign
    // id into a *different* allowed id's literal string, only into its own
    // canonical form. What matters for the security boundary is that a
    // genuinely foreign id, encoded or not, is still rejected.
    RequestInterceptor interceptor;
    interceptor.setAllowedCids({ QStringLiteral("logo@example.org") });
    QVERIFY(interceptor.shouldAllow(QUrl(QStringLiteral("cid:%6Cogo@example.org"))));
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("cid:other@example.org"))));
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("cid:%6Fther@example.org"))));
}

void TestInterceptor::javascriptSchemeBlocked()
{
    RequestInterceptor interceptor;
    interceptor.setAllowRemote(true);
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("javascript:alert(1)"))));
}

void TestInterceptor::dataSchemeBlocked()
{
    RequestInterceptor interceptor;
    interceptor.setAllowRemote(true);
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("data:text/html,<script>alert(1)</script>"))));
}

void TestInterceptor::blobSchemeBlocked()
{
    RequestInterceptor interceptor;
    interceptor.setAllowRemote(true);
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("blob:https://example.org/uuid"))));
}

void TestInterceptor::aboutSchemeBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("about:blank"))));
}

void TestInterceptor::chromeSchemeBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("chrome://settings"))));
}

void TestInterceptor::qrcSchemeBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("qrc:/icons/foo.png"))));
}

void TestInterceptor::filesystemSchemeBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("filesystem:https://example.org/temporary/foo"))));
}

void TestInterceptor::protocolRelativeUrlBlocked()
{
    RequestInterceptor interceptor;
    interceptor.setAllowRemote(true);
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("//tracker.example/pixel.gif"))));
}

void TestInterceptor::emptyUrlBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl()));
}

void TestInterceptor::blankUrlBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral(""))));
}

void TestInterceptor::colonOnlyUrlBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral(":"))));
}

void TestInterceptor::fragmentOnlyUrlBlocked()
{
    RequestInterceptor interceptor;
    QVERIFY(!interceptor.shouldAllow(QUrl(QStringLiteral("#fragment"))));
}

QTEST_MAIN(TestInterceptor)
#include "test_interceptor.moc"
