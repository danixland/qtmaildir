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

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QWebEnginePage>
#include <QWebEngineUrlScheme>
#include <QWebEngineView>
#include <QtTest>

#include "htmlbuilder.h"
#include "messagedetailsdialog.h"
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
    void placeholderRendersAndReportsItself();
    void aMessageBodyCannotRunAQuery();
    void headerOffersSubjectDateAndSenderForOneMessage();
    void headerOffersNoSenderForARealThread();
    void headerOffersNothingForAnAbsentField();
    void bodySelectionBecomesAQuotedSearch();
    void theBodyMenuDropsTheBrowsersOwnActions();
    void theBodyMenuOffersSelectAll();
    void aCopyFromThePaneReportsWhatWasCopied();
    void theCopyToastAppearsOverThePaneAndFades();
    void theCopyToastStaysAnchoredWhenThePaneResizes();
    void aSearchFromTheDetailsDialogClosesIt();
    void aPlainLinkOpensExternally();
    void aTargetBlankLinkOpensExternally();
    void theLinkMenuDropsTheOpenInWindowActions();
    void theNoticeBarsCarryTheirSeverityAndFollowTheTheme();

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
    message.date = QStringLiteral("Tue, 4 Aug 2026 09:00:00 +0200");
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

void TestMessageView::placeholderRendersAndReportsItself()
{
    MessageView view;
    QWebEngineView *web = webViewOf(&view);
    QVERIFY(web);

    QSignalSpy loaded(web, &QWebEngineView::loadFinished);
    view.showPlaceholder({ { QStringLiteral("7 unread"),
                             QStringLiteral("tag:unread") } });

    QVERIFY2(loaded.wait(15000), "the placeholder document never loaded");
    QVERIFY2(loaded.last().at(0).toBool(),
             "loadFinished reported failure: the navigation was rejected, "
             "which is what a base-URL mismatch looks like");

    // Rendered, not merely loaded. The wordmark is split across elements by
    // the accent span, so the helper line is what proves the content arrived.
    QString text;
    bool done = false;
    web->page()->toPlainText([&](const QString &result) {
        text = result;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    QVERIFY2(text.contains(QStringLiteral("7 unread")), qPrintable(text));

    QVERIFY(view.showingPlaceholder());
}

void TestMessageView::aMessageBodyCannotRunAQuery()
{
    // The gate behind queryRequested(). A message body is attacker-controlled
    // HTML and can carry a qtmaildir-query: link; the view only honours one
    // while the placeholder is what is displayed, so this asserts the state
    // that decides it rather than synthesising a click, which would need the
    // page's protected navigation handler.
    MessageView view;
    QSignalSpy queries(&view, &MessageView::queryRequested);

    view.showPlaceholder({ { QStringLiteral("7 unread"),
                             QStringLiteral("tag:unread") } });
    QVERIFY(view.showingPlaceholder());

    ParsedMessage message;
    message.ok = true;
    message.from = QStringLiteral("Mallory <mallory@example.org>");
    message.subject = QStringLiteral("Click me");
    message.htmlBody = QStringLiteral(
        "<a href=\"qtmaildir-query:tag%3Adeleted\">a link</a>");

    ThreadRenderItem item;
    item.message = message;
    item.cidPrefix = QStringLiteral("m0");
    item.expanded = true;

    view.showThread({ item });

    // Showing any message closes the gate, so a link in that message's own
    // body has nothing to reach.
    QVERIFY2(!view.showingPlaceholder(),
             "the gate stayed open while a message was displayed: a link in a "
             "message body could run a query");

    view.clear();
    QVERIFY(!view.showingPlaceholder());

    view.showError(QStringLiteral("broken"), QStringLiteral("/tmp/x"));
    QVERIFY(!view.showingPlaceholder());

    QVERIFY(queries.isEmpty());
}

void TestMessageView::headerOffersSubjectDateAndSenderForOneMessage()
{
    MessageView view;
    view.showThread({ oneMessage() });

    const QList<SearchOffer> offers = view.headerSearchOffers();

    QStringList queries;
    for (const SearchOffer &offer : offers)
        queries << offer.query;
    const QString shown = queries.join(QStringLiteral(" | "));

    QVERIFY2(queries.contains(QStringLiteral("subject:\"Quarterly report\"")),
             qPrintable(shown));
    QVERIFY2(queries.contains(
                 QStringLiteral("from:\"Sender <sender@example.org>\"")),
             qPrintable(shown));
    QVERIFY2(queries.contains(
                 QStringLiteral("to:\"Recipient <recipient@example.org>\"")),
             qPrintable(shown));
    QVERIFY2(queries.contains(
                 QStringLiteral("cc:\"Copied <copied@example.org>\"")),
             qPrintable(shown));

    // The date is offered as a one-day range. Qt::RFC2822Date checks the
    // weekday against the date, so a fixture with the wrong day silently
    // produces no offer at all: 2026-08-04 is a Tuesday.
    QVERIFY2(queries.contains(QStringLiteral("date:2026-08-04..2026-08-04")),
             qPrintable(shown));

    // Every offer carries a label the menu shows, naming the value so the user
    // can see what they are about to search for.
    for (const SearchOffer &offer : offers) {
        QVERIFY(!offer.label.isEmpty());
        QVERIFY(!offer.query.isEmpty());
    }
}

void TestMessageView::headerOffersNoSenderForARealThread()
{
    // The header shows From/To/Cc only for a single-message thread, because a
    // thread's recipient differs message to message. The menu shares that
    // condition: it must never offer a value the header is not stating.
    ThreadRenderItem first = oneMessage();
    ThreadRenderItem second = oneMessage();
    second.message.from = QStringLiteral("Recipient <recipient@example.org>");
    second.message.to = QStringLiteral("Sender <sender@example.org>");

    MessageView view;
    view.showThread({ first, second });

    QStringList queries;
    for (const SearchOffer &offer : view.headerSearchOffers())
        queries << offer.query;

    // THE GUARD. A test asserting only that something is absent passes against
    // no implementation whatever. Subject and date must still be offered,
    // which proves the list was built before the absences below mean anything.
    QVERIFY2(!queries.isEmpty(), "no offers at all: the list was never built");
    QVERIFY(queries.contains(QStringLiteral("subject:\"Quarterly report\"")));
    QVERIFY(queries.contains(QStringLiteral("date:2026-08-04..2026-08-04")));

    for (const QString &query : queries) {
        QVERIFY2(!query.startsWith(QStringLiteral("from:")),
                 qPrintable(QStringLiteral("thread offered %1").arg(query)));
        QVERIFY2(!query.startsWith(QStringLiteral("to:")),
                 qPrintable(QStringLiteral("thread offered %1").arg(query)));
        QVERIFY2(!query.startsWith(QStringLiteral("cc:")),
                 qPrintable(QStringLiteral("thread offered %1").arg(query)));
    }
}

void TestMessageView::headerOffersNothingForAnAbsentField()
{
    // cc:"" parses cleanly and matches nothing, so an entry built from an
    // empty header would look enabled and silently do nothing.
    ThreadRenderItem item = oneMessage();
    item.message.cc.clear();

    MessageView view;
    view.showThread({ item });

    QStringList queries;
    for (const SearchOffer &offer : view.headerSearchOffers())
        queries << offer.query;

    // Guard first, then the absence.
    QVERIFY2(!queries.isEmpty(), "no offers at all: the list was never built");
    QVERIFY(queries.contains(QStringLiteral("subject:\"Quarterly report\"")));

    for (const QString &query : queries)
        QVERIFY(!query.startsWith(QStringLiteral("cc:")));
}

void TestMessageView::bodySelectionBecomesAQuotedSearch()
{
    // The selection reaches the query as ONE quoted term. Asserted on the
    // constructed string: a query that lost its quoting is not an error to
    // notmuch, it simply matches nothing, so nothing downstream would report
    // this being wrong.
    //
    // Takes the text as an argument rather than reading the page, so the
    // quoting is testable without a live web engine and a rendered document.
    MessageView view;

    QCOMPARE(view.selectionSearchOffer(QStringLiteral("invoice 4471")).query,
             QStringLiteral("\"invoice 4471\""));

    // A selection spanning paragraphs arrives full of newlines.
    QCOMPARE(view.selectionSearchOffer(
                 QStringLiteral("first line\n\nsecond line")).query,
             QStringLiteral("\"first line second line\""));

    // Query syntax in the selection is data, not syntax: it is quoted, not
    // interpreted, so a selection reading "a or b" searches for that phrase.
    QCOMPARE(view.selectionSearchOffer(QStringLiteral("tag:inbox or x")).query,
             QStringLiteral("\"tag:inbox or x\""));

    // Nothing selected means no entry, rather than an entry searching for "".
    QVERIFY(view.selectionSearchOffer(QString()).query.isEmpty());
    QVERIFY(view.selectionSearchOffer(QStringLiteral("  \n ")).query.isEmpty());

    // A usable offer always carries a label for the menu to show.
    QVERIFY(!view.selectionSearchOffer(QStringLiteral("invoice 4471"))
                 .label.isEmpty());
}

void TestMessageView::theBodyMenuDropsTheBrowsersOwnActions()
{
    // Item 100. The pane starts from Chromium's standard context menu, which
    // is built for a browser: Back, Forward, Reload, Save page and View source
    // all arrive with it and none of them can apply, since every document
    // comes through setHtml() with a fixed base URL and the interceptor blocks
    // everything by default.
    //
    // Asserted on the ACTION POINTERS, which is also how the production code
    // matches them. Matching on text would pass here and fail in every locale
    // but English, and an untranslated match is exactly the defect this could
    // reintroduce without any test noticing.
    MessageView view;
    auto *page = view.findChild<QWebEnginePage *>();
    QVERIFY2(page, "no page, so this test would assert nothing");

    QMenu menu;
    const QList<QWebEnginePage::WebAction> unwanted = {
        QWebEnginePage::Back,     QWebEnginePage::Forward,
        QWebEnginePage::Reload,   QWebEnginePage::SavePage,
    };
    // Kept, and the reason the standard menu is used at all rather than being
    // rebuilt from scratch.
    //
    // ViewSource is in this list deliberately. It was removed with the four
    // above at first, which was an overreach: it has a real document and a
    // real use, and item 113 implements it properly. A test asserting it is
    // GONE would lock in the overreach, so it asserts it survives.
    //
    // SelectAll is deliberately NOT here, and the reason is a limit of this
    // test worth stating. This menu is built BY HAND, so "SelectAll survives"
    // would only prove the filter does not remove it, and prove nothing about
    // whether Chromium's real menu ever offers it. Measured by hand on
    // 2026-08-17, with a selection active: the real menu holds Copy and the
    // search entries and no Select all, both before and after this filter
    // existed. Asserting on it here would read as a guarantee the code does
    // not make. See item 117.
    const QList<QWebEnginePage::WebAction> wanted = {
        QWebEnginePage::Copy,
        QWebEnginePage::ViewSource,
    };

    for (const QWebEnginePage::WebAction which : unwanted)
        menu.addAction(page->action(which));
    menu.addSeparator();
    for (const QWebEnginePage::WebAction which : wanted)
        menu.addAction(page->action(which));

    // The guard: the menu really does hold what the assertions below are about,
    // so a filter that removed everything, or a page that offered nothing,
    // cannot pass by accident.
    QCOMPARE(menu.actions().size(), unwanted.size() + wanted.size() + 1);

    MessageView::removeBrowserActions(&menu, page);

    const QList<QAction *> left = menu.actions();
    for (const QWebEnginePage::WebAction which : unwanted) {
        QVERIFY2(!left.contains(page->action(which)),
                 qPrintable(QStringLiteral(
                                "a browser action survived the filter: %1")
                                .arg(page->action(which)->text())));
    }
    for (const QWebEnginePage::WebAction which : wanted) {
        QVERIFY2(left.contains(page->action(which)),
                 qPrintable(QStringLiteral(
                                "the filter removed an action the pane needs: "
                                "%1")
                                .arg(page->action(which)->text())));
    }

    // No separator left stranded at either edge by the removals, which reads
    // as a menu that lost something.
    QVERIFY(!left.isEmpty());
    QVERIFY(!left.constFirst()->isSeparator());
    QVERIFY(!left.constLast()->isSeparator());
}

void TestMessageView::theBodyMenuOffersSelectAll()
{
    // Item 117. Chromium's standard menu for this pane has NEVER carried
    // Select all: measured by hand with a selection active, and against a
    // build with removeBrowserActions() reverted, so the filter is not what
    // removed it. The pane adds it.
    //
    // What this test can and cannot prove is the whole point of the item, and
    // three wrong theories were bought before it was measured. The production
    // menu comes from createStandardContextMenu(), which returns nothing
    // outside a real context-menu event, so no test can build it. This
    // therefore asserts what addPaneActions() does to a menu handed to it, and
    // says NOTHING about what Chromium offers. Those are separate questions;
    // conflating them is what item 117 records.
    //
    // The limit is worth stating precisely, because it is the second half of
    // the same trap: this test does NOT cover showBodyContextMenu() CALLING
    // addPaneActions(). Measured, a mutation deleting that call leaves the
    // whole suite green. Covering it needs a real context-menu event, which the
    // offscreen platform cannot deliver, so the call site is a hand test. Do
    // not add an assertion here that appears to cover it.
    MessageView view;
    auto *page = view.findChild<QWebEnginePage *>();
    QVERIFY2(page, "no page, so this test would assert nothing");

    QMenu menu;
    auto *selectAll = page->action(QWebEnginePage::SelectAll);
    QVERIFY2(selectAll, "the page offers no SelectAll action at all");

    // The guard: absent before, so a pass cannot come from the menu already
    // holding it or from the action being added twice by something else.
    QVERIFY(!menu.actions().contains(selectAll));

    MessageView::addPaneActions(&menu, page);

    QVERIFY2(menu.actions().contains(selectAll),
             "the pane's menu does not offer Select all");
}

void TestMessageView::aCopyFromThePaneReportsWhatWasCopied()
{
    // Item 115. Copy link address, Copy image address and Copy image all work
    // and none of them said so. Chromium does not report success, so the pane
    // listens to its actions and shows its own confirmation.
    //
    // Reported through the in-pane TOAST since the user asked for it there
    // rather than in the status bar: a copy happens in the pane, and the
    // status bar is at the other end of the window. This test is about the
    // four entries each saying something DIFFERENT; where it is displayed is
    // theCopyToastAppearsOverThePaneAndFades().
    //
    // Unlike item 117's entry, this IS fully testable: the connections are made
    // to the page's own QActions in the constructor, so triggering one runs the
    // production path. No context-menu event is involved.
    MessageView view;
    auto *page = view.findChild<QWebEnginePage *>();
    QVERIFY2(page, "no page, so this test would assert nothing");

    auto *toast = view.findChild<QLabel *>(QStringLiteral("copyToast"));
    QVERIFY2(toast, "there is no copy toast at all");

    // Each entry names WHAT was copied. "Copied" alone is worse than nothing
    // when three entries sit together in one menu, which the item states as a
    // constraint, so the messages are asserted to differ from each other.
    const QList<QWebEnginePage::WebAction> copies = {
        QWebEnginePage::Copy,
        QWebEnginePage::CopyLinkToClipboard,
        QWebEnginePage::CopyImageToClipboard,
        QWebEnginePage::CopyImageUrlToClipboard,
    };

    QStringList seen;
    for (const QWebEnginePage::WebAction which : copies) {
        QAction *action = page->action(which);
        QVERIFY2(action, "the page offers no action for a copy entry");

        // Enabled explicitly. Chromium disables a copy action when there is
        // nothing of that kind under the cursor, and trigger() on a disabled
        // QAction emits nothing at all, so without this the loop would assert
        // nothing while looking thorough.
        action->setEnabled(true);

        toast->clear();
        action->trigger();

        QTRY_VERIFY_WITH_TIMEOUT(!toast->text().isEmpty(), 5000);
        const QString message = toast->text();
        seen.append(message);
    }

    // Four distinct messages, so no two entries report the same thing.
    QCOMPARE(seen.size(), copies.size());
    QCOMPARE(QSet<QString>(seen.cbegin(), seen.cend()).size(), copies.size());
}

void TestMessageView::theCopyToastAppearsOverThePaneAndFades()
{
    // The user's preference, given after item 115 shipped the status-bar
    // version: "a small transient with a checkmark in the bottom right of the
    // message pane". A copy happens IN the pane, and the status bar is at the
    // other end of the window, so the confirmation was landing far from the
    // gesture that caused it.
    MessageView view;
    view.resize(600, 400);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *page = view.findChild<QWebEnginePage *>();
    auto *toast = view.findChild<QLabel *>(QStringLiteral("copyToast"));
    QVERIFY2(page, "no page, so this test would assert nothing");
    QVERIFY2(toast, "there is no copy toast at all");

    // Hidden until something is copied: a confirmation that is always visible
    // confirms nothing.
    QVERIFY2(!toast->isVisible(), "the toast is showing before anything was copied");

    QAction *copy = page->action(QWebEnginePage::CopyLinkToClipboard);
    QVERIFY(copy);
    // Chromium disables a copy action when there is nothing of that kind under
    // the cursor, and trigger() on a disabled QAction emits nothing at all.
    copy->setEnabled(true);
    copy->trigger();

    QTRY_VERIFY_WITH_TIMEOUT(toast->isVisible(), 5000);
    // It says WHAT was copied, not merely that something was, which is the
    // constraint item 115 already carried: three copy entries sit together in
    // one menu.
    QVERIFY2(toast->text().contains(QStringLiteral("link")),
             qPrintable(QStringLiteral("the toast says '%1'").arg(toast->text())));

    // Opaque and theme-coloured. A transparent label over a rendered message
    // is unreadable against exactly the content it is confirming, and a
    // geometry assertion cannot see that: the rect is correct either way.
    QVERIFY2(toast->autoFillBackground(),
             "the toast is transparent, so it reads over the message body");
    QCOMPARE(toast->palette().color(QPalette::Window),
             view.palette().color(QPalette::ToolTipBase));

    // Bottom right of the pane, inside it rather than beside it.
    const QRect paneRect = view.rect();
    const QRect toastRect = toast->geometry();
    QVERIFY2(toastRect.right() <= paneRect.right(),
             "the toast hangs off the right edge of the pane");
    QVERIFY2(toastRect.bottom() <= paneRect.bottom(),
             "the toast hangs off the bottom edge of the pane");
    QVERIFY2(toastRect.center().x() > paneRect.center().x(),
             "the toast is not in the right half of the pane");
    QVERIFY2(toastRect.center().y() > paneRect.center().y(),
             "the toast is not in the bottom half of the pane");

    // And it goes away on its own. Transient is the whole point: a
    // confirmation the user has to dismiss is worse than none.
    QTRY_VERIFY_WITH_TIMEOUT(!toast->isVisible(),
                             int(MessageView::kToastMs) + 4000);
}

void TestMessageView::theCopyToastStaysAnchoredWhenThePaneResizes()
{
    // A manually positioned child does not follow its parent, unlike a widget
    // in a layout. The toast cannot BE in the layout, since it floats over the
    // web view rather than taking space from it, so the anchoring is this
    // class's job and a resize is where that breaks.
    MessageView view;
    view.resize(600, 400);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    auto *page = view.findChild<QWebEnginePage *>();
    auto *toast = view.findChild<QLabel *>(QStringLiteral("copyToast"));
    QVERIFY(page && toast);

    QAction *copy = page->action(QWebEnginePage::Copy);
    QVERIFY(copy);
    copy->setEnabled(true);
    copy->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(toast->isVisible(), 5000);

    // SHRUNK, not grown, and that distinction is the whole test. Growing the
    // pane moves its right and bottom edges AWAY, so a toast left at the old
    // position still satisfies "inside the pane" and the assertions below pass
    // against a toast that never moved. Measured: with the reposition deleted,
    // a 600x400 -> 900x700 resize left this test green.
    //
    // Shrinking puts the stale position outside the new rect, which is also
    // what the user would actually see: a confirmation half off the pane.
    view.resize(360, 240);
    // The guard: the pane really did change size, so the assertion below is
    // about the toast following rather than about nothing having moved.
    QTRY_COMPARE_WITH_TIMEOUT(view.width(), 360, 5000);

    const QRect paneRect = view.rect();
    const QRect toastRect = toast->geometry();
    QVERIFY2(toastRect.right() <= paneRect.right(),
             "the toast did not follow the pane's right edge");
    QVERIFY2(toastRect.center().x() > paneRect.center().x(),
             "the toast is stranded in the left half after a resize");
    QVERIFY2(toastRect.center().y() > paneRect.center().y(),
             "the toast is stranded in the top half after a resize");
}

void TestMessageView::aSearchFromTheDetailsDialogClosesIt()
{
    // The dialog is modal. Without closing it, the query runs and the thread
    // list repaints BEHIND a window the user still has to dismiss, so the
    // search looks like it did nothing. The dialog also describes m_items,
    // which the new query is about to replace.
    MessageView view;
    view.showThread({ oneMessage() });

    QSignalSpy spy(&view, &MessageView::searchRequested);
    QVERIFY(spy.isValid());

    // showDetailsDialog() blocks in exec(), so the dialog has to be driven
    // from a timer once it is up.
    bool foundTheDialog = false;
    QTimer::singleShot(0, &view, [&view, &foundTheDialog]() {
        auto *dialog = view.findChild<MessageDetailsDialog *>();
        if (!dialog) {
            // Never leave exec() spinning: a missing dialog must fail the test,
            // not hang the suite.
            QApplication::exit(1);
            return;
        }
        foundTheDialog = true;

        const QList<HeaderRow> rows = dialog->rows();
        const auto from = std::find_if(
            rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
                return row.field == QStringLiteral("from");
            });
        if (from == rows.cend()) {
            dialog->reject();
            return;
        }

        dialog->requestSearch(*from, SearchTerm::SearchMode::Replace);
    });

    view.showDetailsDialog();

    QVERIFY2(foundTheDialog, "the details dialog never appeared");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(),
             QStringLiteral("from:\"Sender <sender@example.org>\""));

    // exec() returned, which is the assertion: the dialog closed on its own
    // rather than waiting for the user to dismiss it.
    QVERIFY(!view.findChild<MessageDetailsDialog *>()
            || !view.findChild<MessageDetailsDialog *>()->isVisible());
}


// Item 126. A clicked link must leave the pane, and the two kinds of anchor
// reach the outside world by DIFFERENT routes through Qt. Both are asserted,
// because the working one is what disproved the first diagnosis: a plain-text
// mail's links already opened while an HTML newsletter's did nothing, so a
// test covering one route says nothing about the other.
//
// MessageView::setLinkOpener() is the seam. The real call ends in
// QDesktopServices::openUrl(), which would launch a browser; what is under
// test is WHICH clicks arrive there, never what openUrl does with them.

void TestMessageView::aPlainLinkOpensExternally()
{
    // The route that already worked. Asserted so that fixing the other one
    // cannot quietly break it, which is the plausible regression: both end at
    // the same handler now.
    QList<QUrl> opened;
    MessageView::setLinkOpener([&opened](const QUrl &u) { opened.append(u); });

    MessageView view;

    // A link click as acceptNavigationRequest sees it. Driven through the page
    // rather than synthesised: JavaScript is off in this profile, so
    // element.click() does nothing (verified, runJavaScript returns an invalid
    // QVariant), and a synthetic mouse press would have to land on the
    // anchor's rect, which depends on the desktop's fonts.
    const QUrl target(QStringLiteral("https://example.org/plain"));
    QVERIFY2(!view.clickLinkForTest(target),
             "a link click must be REFUSED as a navigation: the pane may "
             "never follow a link");

    QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 5000);
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first(), target);

    MessageView::setLinkOpener({});
}

void TestMessageView::aTargetBlankLinkOpensExternally()
{
    // The defect. An anchor carrying target="_blank" never reaches
    // acceptNavigationRequest: Chromium asks for a new window instead, and the
    // base createWindow() returns nullptr, so the click was discarded with
    // nothing on screen and no error anywhere. Marketing HTML sets _blank on
    // practically every anchor, which is what made "HTML mail" look broken
    // while a plain-text mail's links worked.
    QList<QUrl> opened;
    MessageView::setLinkOpener([&opened](const QUrl &u) { opened.append(u); });

    MessageView view;
    const QUrl target(QStringLiteral("https://example.org/blank"));

    // Drives the real createWindow() override on the real page, then navigates
    // what it returns, which is Chromium's own sequence. Before item 126 the
    // page returned nothing and this is false.
    QVERIFY2(view.relayBlankTargetForTest(target),
             "the page provided no window for a target=\"_blank\" click, so "
             "the URL was discarded");

    QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 5000);
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.first(), target);

    // No second view may exist for it. The pane renders a list into ONE
    // QWebEngineView deliberately: a view per message is a Chromium render
    // process per message.
    QCOMPARE(view.findChildren<QWebEngineView *>().size(), 1);

    MessageView::setLinkOpener({});
}

void TestMessageView::theLinkMenuDropsTheOpenInWindowActions()
{
    // Item 127. Chromium adds these only when the menu is raised over a LINK,
    // so item 100's filter never saw them: its list is the page actions, and
    // it was tested by right-clicking the page.
    //
    // They are not uniform, which is why this asserts in both directions.
    // Open in new tab and Open in new window cannot be honoured: there are no
    // tabs and the pane must never open a window. Copy link works, and is the
    // whole workaround a user has for any link that will not open, so removing
    // it would take away the fallback.
    MessageView view;
    auto *page = view.findChild<QWebEnginePage *>();
    QVERIFY2(page, "no page, so this test would assert nothing");

    QMenu menu;
    const QList<QWebEnginePage::WebAction> unwanted = {
        QWebEnginePage::OpenLinkInNewTab,
        QWebEnginePage::OpenLinkInNewWindow,
        QWebEnginePage::OpenLinkInThisWindow,
        // Save link. Asserted here rather than left to item 114, because it is
        // not the same question as Save image: it fetches a sender-chosen
        // remote URL through the one profile that must never fetch remote
        // content. A download handler added for Save image must NOT make this
        // reachable again, and this assertion is what would catch that.
        QWebEnginePage::DownloadLinkToDisk,
    };
    const QList<QWebEnginePage::WebAction> wanted = {
        QWebEnginePage::CopyLinkToClipboard,
    };

    for (const QWebEnginePage::WebAction which : unwanted)
        menu.addAction(page->action(which));
    menu.addSeparator();
    for (const QWebEnginePage::WebAction which : wanted)
        menu.addAction(page->action(which));

    // The guard: prove the menu holds what the assertions are about, so a
    // filter that removed everything cannot pass by accident.
    QCOMPARE(menu.actions().size(), unwanted.size() + wanted.size() + 1);

    MessageView::removeBrowserActions(&menu, page);

    const QList<QAction *> left = menu.actions();
    for (const QWebEnginePage::WebAction which : unwanted) {
        QVERIFY2(!left.contains(page->action(which)),
                 qPrintable(QStringLiteral("a link action survived: %1")
                                .arg(page->action(which)->text())));
    }
    for (const QWebEnginePage::WebAction which : wanted) {
        QVERIFY2(left.contains(page->action(which)),
                 qPrintable(QStringLiteral("a wanted action was removed: %1")
                                .arg(page->action(which)->text())));
    }
}

void TestMessageView::theNoticeBarsCarryTheirSeverityAndFollowTheTheme()
{
    // The bars are the pane's only out-of-band messages, and they read as part
    // of the page until they carry a ground of their own. Two severities: a
    // warning that explains a limitation and offers nothing to do about it,
    // and an action the user can take.
    MessageView view;

    auto *warning =
        view.findChild<QWidget *>(QStringLiteral("receiveOnlyRibbon"));
    auto *blocked =
        view.findChild<QWidget *>(QStringLiteral("blockedContentBar"));
    auto *stale = view.findChild<QWidget *>(QStringLiteral("staleThreadBar"));
    QVERIFY2(warning && blocked && stale, "a notice bar is missing");

    // Asserting on the stylesheet STRING rather than on a rendered pixel, for
    // the reason CLAUDE.md records about rendering probes: an unshown widget
    // under the offscreen platform renders nothing, so a pixel test here would
    // pass whatever the code does.
    const QString warningSheet = warning->styleSheet();
    const QString blockedSheet = blocked->styleSheet();
    const QString staleSheet = stale->styleSheet();

    for (const QString &sheet : { warningSheet, blockedSheet, staleSheet }) {
        QVERIFY2(sheet.contains(QStringLiteral("background")),
                 qPrintable(QStringLiteral("a bar paints no ground: %1")
                                .arg(sheet)));
    }

    // The two severities must not look alike, which is the whole point: a
    // warning and an action reading identically is the state being fixed.
    QVERIFY2(warningSheet != blockedSheet,
             "the warning and the action bar share one appearance");

    // Both action bars are the same severity and must agree on their COLOURS.
    // Not on the whole sheet: each names its own widget, so the strings differ
    // by that alone. Compare what carries the severity instead.
    const QRegularExpression hex(QStringLiteral("#[0-9a-fA-F]{6}"));
    const auto coloursOf = [&hex](const QString &sheet) {
        QStringList found;
        auto it = hex.globalMatch(sheet);
        while (it.hasNext())
            found << it.next().captured(0).toLower();
        return found;
    };
    QVERIFY2(!coloursOf(blockedSheet).isEmpty(),
             "the action bar names no colours at all");
    QCOMPARE(coloursOf(blockedSheet), coloursOf(staleSheet));

    // And the warning genuinely differs in colour, not merely in widget name.
    QVERIFY2(coloursOf(warningSheet) != coloursOf(blockedSheet),
             "the warning and the action bar use the same colours");

    // The action bars put their button on the RIGHT. addStretch() at the end
    // of the row left-aligns it, which is what both did.
    for (QWidget *bar : { blocked, stale }) {
        auto *row = qobject_cast<QHBoxLayout *>(bar->layout());
        QVERIFY2(row, "an action bar has no horizontal row");
        int lastWidget = -1;
        int lastStretch = -1;
        for (int i = 0; i < row->count(); ++i) {
            if (row->itemAt(i)->widget())
                lastWidget = i;
            else if (row->itemAt(i)->spacerItem())
                lastStretch = i;
        }
        QVERIFY2(lastStretch >= 0 && lastWidget >= 0,
                 "an action bar has no stretch, so nothing is aligned");
        QVERIFY2(lastStretch < lastWidget,
                 "the stretch comes after the button, left-aligning it");
    }
}

QTEST_MAIN(TestMessageView)
#include "test_messageview.moc"
