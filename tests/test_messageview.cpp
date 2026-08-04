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

#include <QLabel>
#include <QPushButton>
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
    void zoomIsClampedToARenderableRange();
    void zoomSurvivesANewDocument();
    void attachmentBarOffersEveryAttachment();
    void attachmentBarClearsBetweenThreads();
    void singleMessageHeaderShowsFromToAndCc();
    void threadHeaderShowsOnlySubjectAndCount();
    void headerEscapesUntrustedValues();
    void headerOmitsAnAbsentCc();
    void detailsDialogIsOfferedForEveryThread();

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

void TestMessageView::zoomIsClampedToARenderableRange()
{
    // A factor outside the range leaves the pane unreadable, and the only way
    // back is a menu entry the user can no longer read. A corrupt state file
    // reaching setZoomFactor() must not be able to do that.
    QCOMPARE(MessageView::clampZoom(100.0), MessageView::kMaxZoom);
    QCOMPARE(MessageView::clampZoom(0.01), MessageView::kMinZoom);

    // A missing or non-numeric state value converts to 0.0, and a hand-edited
    // one can hold NaN or an infinity. None of those may reach the web view.
    QCOMPARE(MessageView::clampZoom(0.0), MessageView::kDefaultZoom);
    QCOMPARE(MessageView::clampZoom(-2.0), MessageView::kDefaultZoom);
    QCOMPARE(MessageView::clampZoom(qQNaN()), MessageView::kDefaultZoom);
    QCOMPARE(MessageView::clampZoom(qInf()), MessageView::kDefaultZoom);

    // In-range values pass through untouched.
    QCOMPARE(MessageView::clampZoom(1.4), 1.4);

    MessageView view;
    view.setZoomFactor(50.0);
    QCOMPARE(view.zoomFactor(), MessageView::kMaxZoom);
}

void TestMessageView::zoomSurvivesANewDocument()
{
    // MainWindow persists whatever zoomFactor() reports and never reapplies it
    // per render, which is only correct if the web view keeps the factor
    // across setHtml(). Verified rather than assumed.
    MessageView view;
    QWebEngineView *web = webViewOf(&view);
    QVERIFY(web);

    view.setZoomFactor(1.5);

    QSignalSpy loaded(web, &QWebEngineView::loadFinished);

    ParsedMessage message;
    message.ok = true;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.subject = QStringLiteral("Zoom");
    message.plainBody = QStringLiteral("body text");

    ThreadRenderItem item;
    item.message = message;
    item.cidPrefix = QStringLiteral("m0");
    item.expanded = true;

    view.showThread({ item });
    QVERIFY(loaded.wait(15000));

    QCOMPARE(view.zoomFactor(), 1.5);
}

/// The buttons in the attachment bar, by their label.
///
/// Identified by the bar being their parent, not by excluding the labels of
/// the other buttons in the pane: an exclusion list silently adopts every
/// button added later, and it did, counting the details button as an
/// attachment the moment one was added beside the header.
static QStringList attachmentButtonLabels(MessageView *view)
{
    QStringList labels;
    QWidget *bar = view->findChild<QWidget *>(QStringLiteral("attachmentBar"));
    if (!bar)
        return labels;
    for (QPushButton *button : bar->findChildren<QPushButton *>())
        labels.append(button->text());
    return labels;
}

void TestMessageView::attachmentBarOffersEveryAttachment()
{
    // The bar existed as an empty placeholder for two releases: it was created
    // and added to the layout, and nothing ever put anything in it, so
    // attachments were parsed and then unreachable.
    ParsedMessage first;
    first.ok = true;
    first.from = QStringLiteral("Sender <sender@example.org>");
    first.subject = QStringLiteral("With files");
    first.plainBody = QStringLiteral("see attached");
    first.attachments.append({ QStringLiteral("notes.txt"),
                               QStringLiteral("text/plain"),
                               QByteArray("hello") });

    ParsedMessage second;
    second.ok = true;
    second.from = QStringLiteral("Other <other@example.org>");
    second.subject = QStringLiteral("Reply");
    second.plainBody = QStringLiteral("mine too");
    second.attachments.append({ QStringLiteral("../../etc/passwd"),
                                QStringLiteral("text/plain"),
                                QByteArray("root:x:0:0") });

    ThreadRenderItem itemA;
    itemA.message = first;
    itemA.cidPrefix = QStringLiteral("m0");
    itemA.expanded = true;

    ThreadRenderItem itemB;
    itemB.message = second;
    itemB.cidPrefix = QStringLiteral("m1");
    itemB.expanded = true;

    MessageView view;
    view.showThread({ itemA, itemB });

    // ONE button whatever the count, carrying the total. A button per
    // attachment made the bar as wide as the window on a thread with fifteen
    // of them and pushed the splitter over, leaving the thread list unusable.
    const QStringList labels = attachmentButtonLabels(&view);
    QCOMPARE(labels.size(), 1);
    QVERIFY2(labels.first().contains(QStringLiteral("2")),
             qPrintable(QStringLiteral("expected the count in '%1'")
                            .arg(labels.first())));

    // A filename never reaches the bar, so a long one cannot widen it.
    QVERIFY(!labels.first().contains(QStringLiteral("notes.txt")));
    QVERIFY(!labels.first().contains(QStringLiteral("passwd")));
}

void TestMessageView::attachmentBarClearsBetweenThreads()
{
    ParsedMessage withFile;
    withFile.ok = true;
    withFile.from = QStringLiteral("Sender <sender@example.org>");
    withFile.subject = QStringLiteral("With a file");
    withFile.plainBody = QStringLiteral("attached");
    withFile.attachments.append({ QStringLiteral("report.pdf"),
                                  QStringLiteral("application/pdf"),
                                  QByteArray("%PDF-1.4") });

    ThreadRenderItem carrying;
    carrying.message = withFile;
    carrying.cidPrefix = QStringLiteral("m0");
    carrying.expanded = true;

    MessageView view;
    view.showThread({ carrying });
    QCOMPARE(attachmentButtonLabels(&view).size(), 1);

    // Moving to a thread without attachments must not leave the previous
    // thread's buttons behind, still offering to save a file from a message
    // that is no longer on screen.
    ParsedMessage plain;
    plain.ok = true;
    plain.from = QStringLiteral("Sender <sender@example.org>");
    plain.subject = QStringLiteral("Nothing attached");
    plain.plainBody = QStringLiteral("just text");

    ThreadRenderItem bare;
    bare.message = plain;
    bare.cidPrefix = QStringLiteral("m0");
    bare.expanded = true;

    view.showThread({ bare });
    QVERIFY(attachmentButtonLabels(&view).isEmpty());

    view.showThread({ carrying });
    QCOMPARE(attachmentButtonLabels(&view).size(), 1);

    view.clear();
    QVERIFY(attachmentButtonLabels(&view).isEmpty());
}

/// The header strip's text. It is rich text, so the assertions below are
/// against markup as well as content.
static QString headerTextOf(MessageView *view)
{
    for (QLabel *label : view->findChildren<QLabel *>()) {
        if (label->textFormat() == Qt::RichText)
            return label->text();
    }
    return QString();
}

/// One message, from the same shape the other tests build.
static ThreadRenderItem oneMessage()
{
    ParsedMessage message;
    message.ok = true;
    message.from = QStringLiteral("Sender <sender@example.org>");
    message.to = QStringLiteral("Recipient <recipient@example.org>");
    message.cc = QStringLiteral("Copied <copied@example.org>");
    message.subject = QStringLiteral("Quarterly report");
    message.date = QStringLiteral("Mon, 4 Aug 2026 09:00:00 +0200");
    message.plainBody = QStringLiteral("body");

    ThreadRenderItem item;
    item.message = message;
    item.cidPrefix = QStringLiteral("m0");
    item.expanded = true;
    return item;
}

void TestMessageView::singleMessageHeaderShowsFromToAndCc()
{
    // MimeParser filled To and Cc all along; HtmlBuilder simply never
    // interpolated them, so they were parsed and dropped. With one message in
    // the thread every field is unambiguous, which is why this is the case that
    // shows them.
    MessageView view;
    view.showThread({ oneMessage() });

    const QString header = headerTextOf(&view);
    QVERIFY2(header.contains(QStringLiteral("sender@example.org")),
             qPrintable(QStringLiteral("no From in '%1'").arg(header)));
    QVERIFY2(header.contains(QStringLiteral("recipient@example.org")),
             qPrintable(QStringLiteral("no To in '%1'").arg(header)));
    QVERIFY2(header.contains(QStringLiteral("copied@example.org")),
             qPrintable(QStringLiteral("no Cc in '%1'").arg(header)));
    QVERIFY(header.contains(QStringLiteral("Quarterly report")));
}

void TestMessageView::threadHeaderShowsOnlySubjectAndCount()
{
    // A thread's To differs per message: once the user replies, one message is
    // addressed to them and the next to the other party. Rather than pick a
    // message arbitrarily or compute a participants list, the thread header
    // says only what it can say honestly. Per-message detail is the dialog's
    // job. This test is what stops a recipient line reappearing here.
    ThreadRenderItem first = oneMessage();

    ThreadRenderItem second = oneMessage();
    second.message.from = QStringLiteral("Recipient <recipient@example.org>");
    second.message.to = QStringLiteral("Sender <sender@example.org>");
    second.message.cc = QString();
    second.cidPrefix = QStringLiteral("m1");

    MessageView view;
    view.showThread({ first, second });

    const QString header = headerTextOf(&view);
    QVERIFY(header.contains(QStringLiteral("Quarterly report")));
    QVERIFY2(!header.contains(QStringLiteral("recipient@example.org")),
             qPrintable(QStringLiteral("a recipient leaked into '%1'")
                            .arg(header)));
    QVERIFY2(!header.contains(QStringLiteral("copied@example.org")),
             qPrintable(QStringLiteral("a Cc leaked into '%1'").arg(header)));
}

void TestMessageView::headerEscapesUntrustedValues()
{
    // Every one of these values comes from a stranger, and the label is
    // Qt::RichText, so an unescaped From is markup injection into the chrome of
    // the application rather than into the sandboxed page.
    ThreadRenderItem item = oneMessage();
    item.message.from =
        QStringLiteral("<b>bold</b> <script>x</script> <evil@example.org>");
    item.message.to = QStringLiteral("<i>italic</i> <to@example.org>");
    item.message.cc = QStringLiteral("<u>under</u> <cc@example.org>");
    item.message.subject = QStringLiteral("<h1>huge</h1>");

    MessageView view;
    view.showThread({ item });

    const QString header = headerTextOf(&view);
    QVERIFY2(!header.contains(QStringLiteral("<b>bold</b>")),
             qPrintable(QStringLiteral("unescaped From in '%1'").arg(header)));
    QVERIFY(!header.contains(QStringLiteral("<script>")));
    QVERIFY(!header.contains(QStringLiteral("<i>italic</i>")));
    QVERIFY(!header.contains(QStringLiteral("<u>under</u>")));
    QVERIFY(!header.contains(QStringLiteral("<h1>huge</h1>")));

    // Escaped, not merely stripped: the text must still be readable.
    QVERIFY(header.contains(QStringLiteral("&lt;b&gt;bold&lt;/b&gt;")));
}

void TestMessageView::headerOmitsAnAbsentCc()
{
    // Most mail carries no Cc. An empty label with nothing after it reads as a
    // rendering fault, so the row is omitted rather than left blank.
    ThreadRenderItem item = oneMessage();
    item.message.cc = QString();

    MessageView view;
    view.showThread({ item });

    const QString header = headerTextOf(&view);
    QVERIFY(header.contains(QStringLiteral("recipient@example.org")));
    QVERIFY2(!header.contains(QStringLiteral("Cc")),
             qPrintable(QStringLiteral("empty Cc row left in '%1'")
                            .arg(header)));
}

void TestMessageView::detailsDialogIsOfferedForEveryThread()
{
    // The button is the discoverable half of the feature: the shortcut alone
    // repeats the complaint that started this backlog. It must be present for a
    // thread as well as a single message, since a thread is exactly the case
    // where the header withholds the most.
    MessageView view;
    view.showThread({ oneMessage() });
    QVERIFY(view.findChild<QPushButton *>(QStringLiteral("messageDetails")));

    ThreadRenderItem second = oneMessage();
    second.cidPrefix = QStringLiteral("m1");
    view.showThread({ oneMessage(), second });
    QVERIFY(view.findChild<QPushButton *>(QStringLiteral("messageDetails")));

    // And it goes away when there is nothing to describe.
    view.clear();
    QPushButton *button =
        view.findChild<QPushButton *>(QStringLiteral("messageDetails"));
    QVERIFY(!button || !button->isVisible());
}

QTEST_MAIN(TestMessageView)
#include "test_messageview.moc"
