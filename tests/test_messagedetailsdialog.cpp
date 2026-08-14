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
#include <QSignalSpy>
#include <QtTest>

#include "htmlbuilder.h"
#include "messagedetailsdialog.h"

/// The details dialog, which shows every header of every message in a thread.
///
/// Rows rather than one text box since item 85, so a value can carry its own
/// context menu without anything parsing rendered text back into structure.
class TestMessageDetailsDialog : public QObject
{
    Q_OBJECT
private slots:
    void showsEveryHeaderOfEveryMessage();
    void valueLabelsCannotRenderMarkup();
    void offersASearchForEachValue();
    void omitsAnEmptyHeader();
    void messageIdIsShownButNotSearchable();
    void excludeIsOfferedOnlyWithAQueryToExcludeFrom();

private:
    /// One message, with every header populated. The date's weekday matches
    /// the date: Qt::RFC2822Date validates the two against each other, and
    /// 2026-08-14 is a Friday.
    ThreadRenderItem oneMessage() const
    {
        ThreadRenderItem item;
        item.message.ok = true;
        item.message.subject = QStringLiteral("Quarterly report");
        item.message.from = QStringLiteral("Sender <sender@example.org>");
        item.message.to = QStringLiteral("Recipient <recipient@example.org>");
        item.message.cc = QStringLiteral("Copied <copied@example.org>");
        item.message.date = QStringLiteral("Fri, 14 Aug 2026 09:30:00 +0200");
        item.message.messageId = QStringLiteral("<abc123@example.org>");
        return item;
    }
};

void TestMessageDetailsDialog::showsEveryHeaderOfEveryMessage()
{
    ThreadRenderItem second = oneMessage();
    second.message.subject = QStringLiteral("Re: Quarterly report");

    MessageDetailsDialog dialog({ oneMessage(), second });

    const QList<HeaderRow> rows = dialog.rows();
    QVERIFY2(!rows.isEmpty(), "no rows: the dialog was never populated");

    // Both messages are represented, each row knowing which one it belongs to.
    QVERIFY(std::any_of(rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
        return row.messageIndex == 0;
    }));
    QVERIFY(std::any_of(rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
        return row.messageIndex == 1;
    }));

    QStringList values;
    for (const HeaderRow &row : rows)
        values << row.value;
    QVERIFY(values.contains(QStringLiteral("Sender <sender@example.org>")));
    QVERIFY(values.contains(QStringLiteral("Re: Quarterly report")));
    QVERIFY(values.contains(QStringLiteral("<abc123@example.org>")));
}

void TestMessageDetailsDialog::valueLabelsCannotRenderMarkup()
{
    // The QPlainTextEdit this replaced was plain by DESIGN, not by style:
    // header values come from strangers and plain text cannot interpret
    // markup. A QLabel guesses under Qt::AutoText, so every label states its
    // format rather than relying on escaping, which is the same protection one
    // mistake away from failing.
    ThreadRenderItem hostile = oneMessage();
    hostile.message.subject =
        QStringLiteral("<b>bold</b><img src=x onerror=1>");

    MessageDetailsDialog dialog({ hostile });

    const QList<QLabel *> labels = dialog.findChildren<QLabel *>();
    QVERIFY2(!labels.isEmpty(), "no labels: the dialog was never populated");

    bool sawTheSubject = false;
    for (const QLabel *label : labels) {
        QCOMPARE(label->textFormat(), Qt::PlainText);
        if (label->text().contains(QStringLiteral("<b>bold</b>")))
            sawTheSubject = true;
    }

    // The markup survives AS TEXT, which is the proof it was not interpreted.
    QVERIFY2(sawTheSubject, "the hostile subject never reached a label");
}

void TestMessageDetailsDialog::offersASearchForEachValue()
{
    MessageDetailsDialog dialog({ oneMessage() });

    QSignalSpy spy(&dialog, &MessageDetailsDialog::searchRequested);
    QVERIFY(spy.isValid());

    const QList<HeaderRow> rows = dialog.rows();
    const auto from = std::find_if(
        rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
            return row.field == QStringLiteral("from");
        });
    QVERIFY2(from != rows.cend(), "no From row to search from");
    QCOMPARE(from->query, QStringLiteral("from:\"Sender <sender@example.org>\""));

    // The date becomes a one-day range rather than a text match on the header.
    const auto date = std::find_if(
        rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
            return row.field == QStringLiteral("date");
        });
    QVERIFY2(date != rows.cend(), "no Date row");
    QCOMPARE(date->query, QStringLiteral("date:2026-08-14..2026-08-14"));

    // Replacing and narrowing are both offered, and the mode distinguishes them.
    dialog.requestSearch(*from, SearchTerm::SearchMode::Replace);
    dialog.requestSearch(*from, SearchTerm::SearchMode::Narrow);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), from->query);
    QCOMPARE(spy.at(0).at(1).value<SearchTerm::SearchMode>(),
             SearchTerm::SearchMode::Replace);
    QCOMPARE(spy.at(1).at(1).value<SearchTerm::SearchMode>(),
             SearchTerm::SearchMode::Narrow);
}

void TestMessageDetailsDialog::omitsAnEmptyHeader()
{
    ThreadRenderItem noCc = oneMessage();
    noCc.message.cc.clear();

    MessageDetailsDialog dialog({ noCc });

    const QList<HeaderRow> rows = dialog.rows();
    // Guard first: an absence assertion alone passes against no implementation.
    QVERIFY2(!rows.isEmpty(), "no rows: the dialog was never populated");
    QVERIFY(std::any_of(rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
        return row.field == QStringLiteral("from");
    }));

    for (const HeaderRow &row : rows)
        QVERIFY(row.field != QStringLiteral("cc"));
}

void TestMessageDetailsDialog::messageIdIsShownButNotSearchable()
{
    // A message id names one message, and the thread holding it is already on
    // screen, so there is nothing useful to search for. It is still shown.
    MessageDetailsDialog dialog({ oneMessage() });

    const QList<HeaderRow> rows = dialog.rows();
    const auto id = std::find_if(
        rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
            return row.value == QStringLiteral("<abc123@example.org>");
        });
    QVERIFY2(id != rows.cend(), "the message id is not shown at all");
    QVERIFY(id->query.isEmpty());

    // And asking to search it emits nothing rather than an empty query.
    QSignalSpy spy(&dialog, &MessageDetailsDialog::searchRequested);
    dialog.requestSearch(*id, SearchTerm::SearchMode::Replace);
    QCOMPARE(spy.count(), 0);
}

void TestMessageDetailsDialog::excludeIsOfferedOnlyWithAQueryToExcludeFrom()
{
    // Excluding from an empty query bar would mean the whole Maildir minus one
    // value: a legitimate query, and an implausible thing to have meant by
    // right-clicking a value in a fresh window.
    MessageDetailsDialog withQuery({ oneMessage() }, true);
    MessageDetailsDialog withoutQuery({ oneMessage() }, false);

    // The menu is built inside a customContextMenuRequested lambda and cannot
    // be popped without a real context-menu event, so assert on the property
    // its enabled state is derived from.
    QVERIFY(withQuery.canExcludeFromSearch());
    QVERIFY(!withoutQuery.canExcludeFromSearch());

    const QList<HeaderRow> rows = withoutQuery.rows();
    const auto from = std::find_if(
        rows.cbegin(), rows.cend(), [](const HeaderRow &row) {
            return row.field == QStringLiteral("from");
        });
    QVERIFY2(from != rows.cend(), "no From row to search from");

    // The emit refuses too, so the guard does not rest on the menu alone.
    QSignalSpy blocked(&withoutQuery,
                       &MessageDetailsDialog::searchRequested);
    QVERIFY(blocked.isValid());
    withoutQuery.requestSearch(*from, SearchTerm::SearchMode::Exclude);
    QCOMPARE(blocked.count(), 0);

    // And with a query it goes through, so the guard is not simply refusing
    // every exclude.
    QSignalSpy allowed(&withQuery, &MessageDetailsDialog::searchRequested);
    QVERIFY(allowed.isValid());
    withQuery.requestSearch(*from, SearchTerm::SearchMode::Exclude);
    QCOMPARE(allowed.count(), 1);
    QCOMPARE(allowed.at(0).at(1).value<SearchTerm::SearchMode>(),
             SearchTerm::SearchMode::Exclude);
}

QTEST_MAIN(TestMessageDetailsDialog)
#include "test_messagedetailsdialog.moc"
