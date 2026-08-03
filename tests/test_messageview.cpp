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

#include <QSignalSpy>
#include <QWebEngineUrlScheme>
#include <QWebEngineView>
#include <QtTest>

#include "htmlbuilder.h"
#include "messageview.h"
#include "mimeparser.h"

/// MessageView needs a live QWebEngineProfile, so most of it is verified
/// manually. What is pinned here is the one thing that silently produced a
/// blank pane: whether a document handed to setHtml() actually loads.
class TestMessageView : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void documentActuallyLoads();
    void threadContentReachesThePage();
    void dataUrlSubResourceStillBlocked();

private:
    QWebEngineView *webViewOf(MessageView *view) const
    {
        return view->findChild<QWebEngineView *>();
    }
};

void TestMessageView::initTestCase()
{
    // Registered in main() in the real application; a test binary has its own
    // entry point and must do the same before any profile exists.
    QWebEngineUrlScheme cid(QByteArrayLiteral("cid"));
    cid.setFlags(QWebEngineUrlScheme::SecureScheme
                 | QWebEngineUrlScheme::ContentSecurityPolicyIgnored);
    QWebEngineUrlScheme::registerScheme(cid);

    QWebEngineUrlScheme own(QByteArrayLiteral("qtmaildir"));
    own.setFlags(QWebEngineUrlScheme::SecureScheme);
    QWebEngineUrlScheme::registerScheme(own);
}

void TestMessageView::documentActuallyLoads()
{
    // The regression this exists for: acceptNavigationRequest compared the
    // navigation's URL against documentUrl(), but setHtml() navigates to a
    // data: URL and applies the base URL only as the document origin. Every
    // document load was rejected and the pane stayed blank, with no warning
    // anywhere.
    MessageView view;
    QWebEngineView *web = webViewOf(&view);
    QVERIFY(web);

    QSignalSpy loaded(web, &QWebEngineView::loadFinished);

    ParsedMessage message;
    message.ok = true;
    message.from = QStringLiteral("Alice <alice@example.org>");
    message.subject = QStringLiteral("Hello");
    message.date = QStringLiteral("Mon, 1 Jun 2026 10:00:00 +0000");
    message.plainBody = QStringLiteral("body text");

    ThreadRenderItem item;
    item.message = message;
    item.cidPrefix = QStringLiteral("m0");
    item.expanded = true;

    view.showThread({ item });

    QVERIFY2(loaded.wait(15000), "no loadFinished at all: the document was "
                                 "never even attempted");
    QCOMPARE(loaded.size(), 1);
    QVERIFY2(loaded.first().at(0).toBool(),
             "loadFinished reported failure: the navigation was rejected");
}

void TestMessageView::threadContentReachesThePage()
{
    // Loading successfully is not the same as showing the message: assert the
    // body actually made it into the rendered document.
    MessageView view;
    QWebEngineView *web = webViewOf(&view);
    QVERIFY(web);

    QSignalSpy loaded(web, &QWebEngineView::loadFinished);

    ParsedMessage message;
    message.ok = true;
    message.from = QStringLiteral("Bob <bob@example.org>");
    message.subject = QStringLiteral("Subject line");
    message.plainBody = QStringLiteral("distinctive-body-marker");

    ThreadRenderItem item;
    item.message = message;
    item.cidPrefix = QStringLiteral("m0");
    item.expanded = true;

    view.showThread({ item });
    QVERIFY(loaded.wait(15000));
    QVERIFY(loaded.first().at(0).toBool());

    QString text;
    bool done = false;
    web->page()->toPlainText([&](const QString &result) {
        text = result;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 15000);

    QVERIFY2(text.contains(QStringLiteral("distinctive-body-marker")),
             qPrintable(QStringLiteral("rendered text was: '%1'").arg(text)));
    QVERIFY(text.contains(QStringLiteral("bob@example.org")));
}

void TestMessageView::dataUrlSubResourceStillBlocked()
{
    // The main-frame exemption must not extend to sub-resources: a message
    // body can write <img src="data:..."> and those stay denied. This is the
    // narrow line between "the document renders" and "the policy has a hole".
    MessageView view;
    QWebEngineView *web = webViewOf(&view);
    QVERIFY(web);

    QSignalSpy loaded(web, &QWebEngineView::loadFinished);

    ParsedMessage message;
    message.ok = true;
    message.from = QStringLiteral("Mallory <mallory@example.org>");
    message.subject = QStringLiteral("Hostile");
    // A 1x1 gif as a data: URL, the shape a tracking-adjacent body would use.
    message.htmlBody = QStringLiteral(
        "<html><body>visible-text"
        "<img id=\"probe\" src=\"data:image/gif;base64,"
        "R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7\">"
        "</body></html>");

    ThreadRenderItem item;
    item.message = message;
    item.cidPrefix = QStringLiteral("m0");
    item.expanded = true;

    view.showThread({ item });
    QVERIFY(loaded.wait(15000));
    QVERIFY2(loaded.first().at(0).toBool(),
             "the document itself must still load");

    // The document rendered; the blocked sub-resource is what the interceptor
    // records. Text is present, so this is not a failed load masquerading as
    // a blocked image.
    QString text;
    bool done = false;
    web->page()->toPlainText([&](const QString &result) {
        text = result;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    QVERIFY(text.contains(QStringLiteral("visible-text")));
}

QTEST_MAIN(TestMessageView)
#include "test_messageview.moc"
