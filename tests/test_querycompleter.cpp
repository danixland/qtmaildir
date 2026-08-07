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
#include <QTemporaryDir>

#include <QLineEdit>
#include <QImage>
#include <QListView>
#include <QPixmap>

#include "config.h"
#include "querycompleter.h"

class TestQueryCompleter : public QObject
{
    Q_OBJECT

private slots:
    void emptyTextCompletesPrefix();
    void bareWordCompletesPrefix();
    void wordAfterOperatorCompletesPrefix();
    void prefixReplaceSpanCoversTheWord();
    void colonSwitchesToValue();
    void valueReplaceSpanExcludesThePrefix();
    void prefixIsLowercased();
    void emptyValueAfterColonStillCompletes();
    void insideQuotesCompletesNothing();
    void afterClosedQuotesCompletesAgain();
    void rangeUpperBoundCompletes();
    void rangeLowerBoundCompletes();
    void bareValueAllowsRelativeEntries();
    void rangeSuppressesRelativeEntries();
    void prefixVocabularyCoversNotmuchKeywords();
    void dateVocabularySeparatesRelativeEntries();
    void tagAndIsShareTheTagModel();
    void pathOffersAccountMaildirsBothForms();
    void folderOffersNothing();
    void mimetypeAppendsConfiguredEntries();
    void rangeContextDropsRelativeDates();
    void acceptReplacesOnlyThePrefixToken();
    void acceptReplacesOnlyTheValueAfterThePrefix();
    void acceptReplacesOnlyTheEditedRangeBound();
    void acceptReplacesTheWholeBoundWhenCompletingMidWord();

    // The tests above call acceptCompletion() directly and so never touch the
    // widget. These drive the path a user actually hits.
    void typingOpensThePopupOnALaterToken();
    void tabAcceptsTheHighlightedCompletion();
    void tabIsIgnoredWhileThePopupIsHidden();
    void returnIsIgnoredWhileThePopupIsHidden();
    void focusOpensThePopupOnlyWhenConfigured();

    // Delivered to the widget the window system actually gives the key to,
    // rather than straight to the line edit. While the popup is up that is the
    // popup, which has grabbed the keyboard, and a filter on the line edit
    // never runs. Sending to the edit hides exactly the bug the user reports.
    void tabAcceptsWhenTheKeyGoesToTheGrabbingPopup();
    void returnAcceptsWhenTheKeyGoesToTheGrabbingPopup();
    void acceptingAPrefixReopensThePopupForValues();
    void acceptingAValueDoesNotReopenAnEmptyPopup();
    void keysFallThroughWhileThePopupIsHidden();

    // The application filter re-entrancy bug: sendEvent() re-runs application
    // event filters, so forwarding a key to the popup from inside the filter
    // hands it straight back and the recursion only ends in a stack overflow.
    void arrowNavigationDoesNotRecurse();
    void returnRunsTheQueryOnceCompletionIsDone();
    void returnRunsTheQueryAfterAMouseAccept();
    void returnRunsTheQueryWhenThePopupMatchesNothing();
    void theDescriptionSurvivesAModestPopupWidth();
};

// Copied from tests/test_config.cpp rather than shared, so the two test files
// stay independent.
static QString writeIni(const QTemporaryDir &dir, const QString &body)
{
    const QString path = dir.filePath(QStringLiteral("qtmaildir.conf"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(body.toUtf8());
    f.close();
    return path;
}

void TestQueryCompleter::emptyTextCompletesPrefix()
{
    const CompletionContext ctx = completionContext(QString(), 0);
    QCOMPARE(ctx.kind, CompletionContext::Prefix);
    QCOMPARE(ctx.stem, QString());
}

void TestQueryCompleter::bareWordCompletesPrefix()
{
    const CompletionContext ctx = completionContext(QStringLiteral("su"), 2);
    QCOMPARE(ctx.kind, CompletionContext::Prefix);
    QCOMPARE(ctx.stem, QStringLiteral("su"));
}

void TestQueryCompleter::wordAfterOperatorCompletesPrefix()
{
    // The token boundary is whitespace, not the start of the line.
    const QString text = QStringLiteral("tag:inbox and su");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Prefix);
    QCOMPARE(ctx.stem, QStringLiteral("su"));
}

void TestQueryCompleter::prefixReplaceSpanCoversTheWord()
{
    const QString text = QStringLiteral("tag:inbox and su");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.replaceFrom, 14);
    QCOMPARE(ctx.replaceLength, 2);
}

void TestQueryCompleter::colonSwitchesToValue()
{
    const QString text = QStringLiteral("tag:sho");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.prefix, QStringLiteral("tag"));
    QCOMPARE(ctx.stem, QStringLiteral("sho"));
}

void TestQueryCompleter::valueReplaceSpanExcludesThePrefix()
{
    // Accepting must overwrite "sho" only, never "tag:".
    const QString text = QStringLiteral("tag:sho");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.replaceFrom, 4);
    QCOMPARE(ctx.replaceLength, 3);
}

void TestQueryCompleter::prefixIsLowercased()
{
    const QString text = QStringLiteral("TAG:sho");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.prefix, QStringLiteral("tag"));
}

void TestQueryCompleter::emptyValueAfterColonStillCompletes()
{
    // "tag:" with the cursor at the end offers every tag.
    const QString text = QStringLiteral("tag:");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.prefix, QStringLiteral("tag"));
    QCOMPARE(ctx.stem, QString());
    QCOMPARE(ctx.replaceFrom, 4);
    QCOMPARE(ctx.replaceLength, 0);
}

void TestQueryCompleter::insideQuotesCompletesNothing()
{
    // subject:"foo bar| is a literal, not a keyword position.
    const QString text = QStringLiteral("subject:\"foo bar");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::None);
}

void TestQueryCompleter::afterClosedQuotesCompletesAgain()
{
    const QString text = QStringLiteral("subject:\"foo bar\" and ta");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Prefix);
    QCOMPARE(ctx.stem, QStringLiteral("ta"));
}

void TestQueryCompleter::rangeUpperBoundCompletes()
{
    const QString text = QStringLiteral("date:today..yes");
    const CompletionContext ctx = completionContext(text, text.size());
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.prefix, QStringLiteral("date"));
    QCOMPARE(ctx.stem, QStringLiteral("yes"));
    // Overwrites "yes" only: "date:today.." must survive.
    QCOMPARE(ctx.replaceFrom, 12);
    QCOMPARE(ctx.replaceLength, 3);
}

void TestQueryCompleter::rangeLowerBoundCompletes()
{
    // Cursor sits at offset 8, mid-way through the lower bound rather than at
    // its end. End-of-bound would not discriminate: there the whole-bound span
    // and the typed-so-far span happen to be the same length.
    const QString text = QStringLiteral("date:lastweek..today");
    const CompletionContext ctx = completionContext(text, 8);
    QCOMPARE(ctx.kind, CompletionContext::Value);
    QCOMPARE(ctx.stem, QStringLiteral("las"));
    QCOMPARE(ctx.replaceFrom, 5);
    // Covers the whole lower bound, so accepting leaves no "tweek" tail.
    QCOMPARE(ctx.replaceLength, 8);
    QVERIFY(!ctx.allowRangeEntries);
}

void TestQueryCompleter::bareValueAllowsRelativeEntries()
{
    const QString text = QStringLiteral("date:1w");
    const CompletionContext ctx = completionContext(text, text.size());
    QVERIFY(ctx.allowRangeEntries);
}

void TestQueryCompleter::rangeSuppressesRelativeEntries()
{
    // "1week.." offered here would produce date:1week....today.
    const QString text = QStringLiteral("date:1w..today");
    const CompletionContext ctx = completionContext(text, 7);
    QVERIFY(!ctx.allowRangeEntries);
}

void TestQueryCompleter::prefixVocabularyCoversNotmuchKeywords()
{
    const QList<CompletionEntry> entries = prefixVocabulary();

    QStringList values;
    for (const CompletionEntry &entry : entries)
        values.append(entry.value);

    QVERIFY(values.contains(QStringLiteral("tag:")));
    QVERIFY(values.contains(QStringLiteral("date:")));
    QVERIFY(values.contains(QStringLiteral("and")));

    // Every entry carries a description; a blank column teaches nothing.
    for (const CompletionEntry &entry : entries)
        QVERIFY(!entry.description.isEmpty());
}

void TestQueryCompleter::dateVocabularySeparatesRelativeEntries()
{
    const QList<CompletionEntry> entries = dateVocabulary();

    bool sawSymbolic = false;
    bool sawRelative = false;
    for (const CompletionEntry &entry : entries) {
        if (entry.value == QStringLiteral("today"))
            sawSymbolic = true;
        if (entry.value.contains(QStringLiteral("..")))
            sawRelative = true;
    }
    QVERIFY(sawSymbolic);
    QVERIFY(sawRelative);
}

void TestQueryCompleter::tagAndIsShareTheTagModel()
{
    Config config;
    QueryCompleter completer(nullptr, config);
    completer.setTags({ QStringLiteral("inbox"), QStringLiteral("shopping/amazon") });

    const QStringList forTag = completer.candidatesFor(
        completionContext(QStringLiteral("tag:"), 4));
    const QStringList forIs = completer.candidatesFor(
        completionContext(QStringLiteral("is:"), 3));

    QVERIFY(forTag.contains(QStringLiteral("shopping/amazon")));
    QCOMPARE(forTag, forIs);
}

void TestQueryCompleter::pathOffersAccountMaildirsBothForms()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[account.work]\n"
        "maildir = work\n"
        "address = you@example.org\n")));

    QueryCompleter completer(nullptr, config);
    const QStringList candidates = completer.candidatesFor(
        completionContext(QStringLiteral("path:"), 5));

    QVERIFY(candidates.contains(QStringLiteral("work")));
    // The recursive form is what scopedQuery() itself builds and is not
    // guessable, so it is offered directly.
    QVERIFY(candidates.contains(QStringLiteral("work/**")));
}

void TestQueryCompleter::folderOffersNothing()
{
    // folder: matches a Maildir folder name, not a path, and its values are
    // not enumerable from config. Prefix-only, like from: and to:.
    Config config;
    QueryCompleter completer(nullptr, config);
    const QStringList candidates = completer.candidatesFor(
        completionContext(QStringLiteral("folder:"), 7));
    QVERIFY(candidates.isEmpty());
}

void TestQueryCompleter::mimetypeAppendsConfiguredEntries()
{
    QTemporaryDir dir;
    Config config;
    config.load(writeIni(dir, QStringLiteral(
        "[completion]\n"
        "extra_mimetypes = application/epub+zip|EPUB book\n")));

    QueryCompleter completer(nullptr, config);
    const QStringList candidates = completer.candidatesFor(
        completionContext(QStringLiteral("mimetype:"), 9));

    QVERIFY(candidates.contains(QStringLiteral("application/epub+zip")));
    QVERIFY(candidates.contains(QStringLiteral("application/pdf")));
}

void TestQueryCompleter::rangeContextDropsRelativeDates()
{
    Config config;
    QueryCompleter completer(nullptr, config);

    const QStringList bare = completer.candidatesFor(
        completionContext(QStringLiteral("date:"), 5));
    QVERIFY(bare.contains(QStringLiteral("1week..")));

    const QString ranged = QStringLiteral("date:today..");
    const QStringList inRange = completer.candidatesFor(
        completionContext(ranged, ranged.size()));
    QVERIFY(inRange.contains(QStringLiteral("yesterday")));
    QVERIFY(!inRange.contains(QStringLiteral("1week..")));
}

// The accept path is driven directly rather than through synthetic key
// events: whether a key needs Shift is a keyboard-layout property, so
// QTest::keyClick could never decide whether this logic is right.
static QString acceptInto(const QString &text, int cursor, const QString &value)
{
    Config config;
    QLineEdit edit;
    QueryCompleter completer(&edit, config);

    edit.setText(text);
    edit.setCursorPosition(cursor);
    completer.updateContext();
    completer.acceptCompletion(value);

    return edit.text();
}

void TestQueryCompleter::acceptReplacesOnlyThePrefixToken()
{
    // The neighbouring token must survive untouched.
    QCOMPARE(acceptInto(QStringLiteral("tag:inbox su"), 12,
                        QStringLiteral("subject:")),
             QStringLiteral("tag:inbox subject:"));
}

void TestQueryCompleter::acceptReplacesOnlyTheValueAfterThePrefix()
{
    // QCompleter's own insertion would overwrite "date:tod" whole, because
    // that is the token it matched on. Only "tod" may be replaced.
    QCOMPARE(acceptInto(QStringLiteral("date:tod"), 8, QStringLiteral("today")),
             QStringLiteral("date:today"));
}

void TestQueryCompleter::acceptReplacesOnlyTheEditedRangeBound()
{
    const QString text = QStringLiteral("date:yesterday..to");
    QCOMPARE(acceptInto(text, text.size(), QStringLiteral("today")),
             QStringLiteral("date:yesterday..today"));
}

void TestQueryCompleter::acceptReplacesTheWholeBoundWhenCompletingMidWord()
{
    // Caret sits after "yest" but the bound runs to the "..", so accepting
    // must leave no "erday" tail behind.
    QCOMPARE(acceptInto(QStringLiteral("date:yesterday..today"), 9,
                        QStringLiteral("this_week")),
             QStringLiteral("date:this_week..today"));
}

// The popup is owned by the QCompleter, which is not reachable from the line
// edit now that setCompleter is deliberately not used. It is the only list
// view these tests create, so find it that way.
static QListView *findPopup()
{
    const auto widgets = QApplication::allWidgets();
    for (QWidget *w : widgets) {
        if (auto *view = qobject_cast<QListView *>(w))
            return view;
    }
    return nullptr;
}

void TestQueryCompleter::typingOpensThePopupOnALaterToken()
{
    // The regression: QLineEdit::setCompleter reset the completion prefix to
    // the widget's whole text on every keystroke, so nothing matched and the
    // popup stopped appearing after the first token.
    Config config;
    QLineEdit edit;
    edit.show();
    QueryCompleter completer(&edit, config);

    QTest::keyClicks(&edit, QStringLiteral("tag:unread date:last"));

    QListView *popup = findPopup();
    QVERIFY(popup);
    QVERIFY(popup->isVisible());

    QStringList offered;
    for (int row = 0; row < popup->model()->rowCount(); ++row)
        offered << popup->model()->index(row, 0).data().toString();
    QCOMPARE(offered, QStringList({ QStringLiteral("last_week"),
                                    QStringLiteral("last_month") }));
}

void TestQueryCompleter::tabAcceptsTheHighlightedCompletion()
{
    // The user's exact scenario. Tab used to fall through to focus navigation,
    // leaving the query half-typed.
    Config config;
    QLineEdit edit;
    edit.show();
    QueryCompleter completer(&edit, config);

    QTest::keyClicks(&edit, QStringLiteral("tag:unread date:last"));
    QVERIFY(findPopup() && findPopup()->isVisible());

    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier,
                  QStringLiteral("\t"));
    QApplication::sendEvent(&edit, &tab);

    // Consumed, so focus does not move to the next widget.
    QVERIFY(tab.isAccepted());
    // Only the token being completed is replaced, not the whole line.
    QCOMPARE(edit.text(), QStringLiteral("tag:unread date:last_week"));
    QVERIFY(!findPopup()->isVisible());
}

void TestQueryCompleter::tabIsIgnoredWhileThePopupIsHidden()
{
    // Every key must fall through when the popup is closed, or the query bar
    // stops behaving like a line edit.
    Config config;
    QLineEdit edit;
    edit.show();
    QueryCompleter completer(&edit, config);

    edit.setText(QStringLiteral("tag:unread"));
    if (QListView *popup = findPopup())
        popup->hide();

    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier,
                  QStringLiteral("\t"));
    tab.ignore();
    QApplication::sendEvent(&edit, &tab);

    QCOMPARE(edit.text(), QStringLiteral("tag:unread"));
}

void TestQueryCompleter::returnIsIgnoredWhileThePopupIsHidden()
{
    // Enter accepts a completion only while the popup is up. With it closed it
    // must still reach returnPressed, which is what runs the query.
    Config config;
    QLineEdit edit;
    edit.show();
    QueryCompleter completer(&edit, config);

    edit.setText(QStringLiteral("tag:unread"));
    if (QListView *popup = findPopup())
        popup->hide();

    bool ran = false;
    connect(&edit, &QLineEdit::returnPressed, &edit, [&ran]() { ran = true; });

    QKeyEvent ret(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&edit, &ret);

    QVERIFY(ran);
    QCOMPARE(edit.text(), QStringLiteral("tag:unread"));
}

void TestQueryCompleter::focusOpensThePopupOnlyWhenConfigured()
{
    // The event filter is now installed unconditionally, so the
    // completion_on_focus check moved inside it. Both settings still behave.
    QTemporaryDir dir;

    {
        Config off;
        off.load(writeIni(dir, QStringLiteral("[general]\n"
                                              "completion_on_focus=false\n")));
        QLineEdit edit;
        edit.show();
        QueryCompleter completer(&edit, off);
        edit.setFocus();
        QVERIFY(!findPopup() || !findPopup()->isVisible());
    }

    {
        Config on;
        on.load(writeIni(dir, QStringLiteral("[general]\n"
                                             "completion_on_focus=true\n")));
        QVERIFY(on.completionOnFocus());
        QLineEdit edit;
        edit.show();
        QueryCompleter completer(&edit, on);
        QFocusEvent focusIn(QEvent::FocusIn);
        QApplication::sendEvent(&edit, &focusIn);
        QVERIFY(findPopup());
        QVERIFY(findPopup()->isVisible());
    }
}

// The widget the window system would hand the next key to. While the popup is
// up it has grabbed the keyboard, so that is the popup and NOT the line edit,
// which has by then lost focus entirely. Routing test keys through here is what
// makes these tests reproduce the user's experience instead of a synthetic one.
static QWidget *keyboardTarget(QLineEdit *edit)
{
    if (QWidget *popup = QApplication::activePopupWidget())
        return popup;
    return edit;
}

void TestQueryCompleter::tabAcceptsWhenTheKeyGoesToTheGrabbingPopup()
{
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);

    QTest::keyClicks(&edit, QStringLiteral("t"));
    QVERIFY(findPopup() && findPopup()->isVisible());

    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Tab);

    QCOMPARE(edit.text(), QStringLiteral("tag:"));
}

void TestQueryCompleter::returnAcceptsWhenTheKeyGoesToTheGrabbingPopup()
{
    // Return must be consumed too, or it reaches the thread list and opens a
    // thread, which is what the user sees.
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);

    bool ran = false;
    connect(&edit, &QLineEdit::returnPressed, &edit, [&ran]() { ran = true; });

    QTest::keyClicks(&edit, QStringLiteral("t"));
    QVERIFY(findPopup() && findPopup()->isVisible());

    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Return);

    QCOMPARE(edit.text(), QStringLiteral("tag:"));
    QVERIFY(!ran);
}

void TestQueryCompleter::acceptingAPrefixReopensThePopupForValues()
{
    // The user's third complaint: after taking "tag:" the caret sits where a
    // tag value goes, so the values must be offered without a second Ctrl+Space.
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);
    completer.setTags({ QStringLiteral("unread"), QStringLiteral("inbox") });

    QTest::keyClicks(&edit, QStringLiteral("t"));
    QVERIFY(findPopup() && findPopup()->isVisible());

    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Tab);
    QCOMPARE(edit.text(), QStringLiteral("tag:"));

    QListView *popup = findPopup();
    QVERIFY(popup);
    QVERIFY(popup->isVisible());
    QStringList offered;
    for (int row = 0; row < popup->model()->rowCount(); ++row)
        offered << popup->model()->index(row, 0).data().toString();
    QCOMPARE(offered, QStringList({ QStringLiteral("unread"),
                                    QStringLiteral("inbox") }));

    // And the chain completes: typing into the reopened popup and accepting
    // yields the finished term.
    QTest::keyClicks(&edit, QStringLiteral("un"));
    QVERIFY(findPopup() && findPopup()->isVisible());
    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Tab);
    QCOMPARE(edit.text(), QStringLiteral("tag:unread"));
}

void TestQueryCompleter::acceptingAValueDoesNotReopenAnEmptyPopup()
{
    // "tag:unread" is complete. Reopening here would put an empty list under
    // the caret and swallow the next Return.
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);
    completer.setTags({ QStringLiteral("unread") });

    QTest::keyClicks(&edit, QStringLiteral("tag:un"));
    QVERIFY(findPopup() && findPopup()->isVisible());

    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Tab);

    QCOMPARE(edit.text(), QStringLiteral("tag:unread"));
    QVERIFY(!findPopup() || !findPopup()->isVisible());
}

void TestQueryCompleter::keysFallThroughWhileThePopupIsHidden()
{
    // The filter is application-wide, so proving it does nothing with the popup
    // down is what keeps it from breaking the rest of the application.
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);

    if (QListView *popup = findPopup())
        popup->hide();

    QLineEdit other;
    other.show();
    QVERIFY(QTest::qWaitForWindowExposed(&other));
    other.setFocus();

    bool ran = false;
    connect(&other, &QLineEdit::returnPressed, &other, [&ran]() { ran = true; });

    QTest::keyClicks(&other, QStringLiteral("hello"));
    QTest::keyClick(&other, Qt::Key_Return);

    QCOMPARE(other.text(), QStringLiteral("hello"));
    QVERIFY(ran);
}

void TestQueryCompleter::arrowNavigationDoesNotRecurse()
{
    // QCoreApplication::sendEvent re-runs application-level event filters, so a
    // filter that forwards the key it just claimed to another widget is handed
    // the same key back. With the popup still visible the guard still passes and
    // it forwards again: unbounded recursion, and the process dies on the stack
    // rather than on any assertion. Reaching the end of this test is the check.
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);

    QTest::keyClicks(&edit, QStringLiteral("t"));
    QVERIFY(findPopup() && findPopup()->isVisible());

    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Down);
    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Down);

    // And navigation actually moved, so the fix is not "swallow the key".
    QListView *popup = findPopup();
    QVERIFY(popup);
    QVERIFY(popup->currentIndex().isValid());
    QCOMPARE(popup->currentIndex().row(), 1);
}

void TestQueryCompleter::returnRunsTheQueryOnceCompletionIsDone()
{
    // The second half of the user's report: with the query finished, Return has
    // to reach returnPressed and run it. A popup left visible over a completed
    // term swallows Return forever, and the query can never be run from the
    // keyboard at all.
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);
    completer.setTags({ QStringLiteral("unread"), QStringLiteral("inbox") });

    bool ran = false;
    connect(&edit, &QLineEdit::returnPressed, &edit, [&ran]() { ran = true; });

    // Drive the whole chain the way the user does: prefix, accept, value, accept.
    QTest::keyClicks(&edit, QStringLiteral("t"));
    QVERIFY(findPopup() && findPopup()->isVisible());
    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Tab);
    QCOMPARE(edit.text(), QStringLiteral("tag:"));

    QTest::keyClicks(&edit, QStringLiteral("un"));
    QVERIFY(findPopup() && findPopup()->isVisible());
    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Tab);
    QCOMPARE(edit.text(), QStringLiteral("tag:unread"));

    // The query is complete. Return must now run it, not be eaten.
    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Return);
    QVERIFY(ran);
}

void TestQueryCompleter::returnRunsTheQueryAfterAMouseAccept()
{
    // The user builds the whole query with the mouse, which never goes through
    // the key filter, and then Return does not run it. Clicking a row is what
    // QCompleter reports as activated(), so drive that and then press Return
    // exactly as the user does.
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);
    completer.setTags({ QStringLiteral("unread"), QStringLiteral("inbox") });

    bool ran = false;
    connect(&edit, &QLineEdit::returnPressed, &edit, [&ran]() { ran = true; });

    QTest::keyClicks(&edit, QStringLiteral("t"));
    QListView *popup = findPopup();
    QVERIFY(popup && popup->isVisible());

    // Click the "tag:" row.
    const QModelIndex prefixRow = popup->model()->index(0, 0);
    QVERIFY(prefixRow.isValid());
    popup->setCurrentIndex(prefixRow);
    QTest::mouseClick(popup->viewport(), Qt::LeftButton, Qt::NoModifier,
                      popup->visualRect(prefixRow).center());
    QCOMPARE(edit.text(), QStringLiteral("tag:"));

    // Then click a tag value in the popup the accept chained open.
    popup = findPopup();
    QVERIFY(popup && popup->isVisible());
    const QModelIndex valueRow = popup->model()->index(0, 0);
    QVERIFY(valueRow.isValid());
    popup->setCurrentIndex(valueRow);
    QTest::mouseClick(popup->viewport(), Qt::LeftButton, Qt::NoModifier,
                      popup->visualRect(valueRow).center());
    QCOMPARE(edit.text(), QStringLiteral("tag:unread"));

    // The query is complete and built entirely with the mouse. Return runs it.
    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Return);
    QVERIFY(ran);
}

void TestQueryCompleter::returnRunsTheQueryWhenThePopupMatchesNothing()
{
    // A query the user finished by hand. The bar is mid-token, so the popup is
    // still up, but nothing in it matches what was typed. Return must run the
    // query: there is no completion to accept, and accepting the first row of
    // an unrelated list would rewrite the query the user just wrote.
    Config config;
    QLineEdit edit;
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);
    completer.setTags({ QStringLiteral("unread"), QStringLiteral("inbox") });

    bool ran = false;
    connect(&edit, &QLineEdit::returnPressed, &edit, [&ran]() { ran = true; });

    // "zzz" matches no tag, so the popup has nothing to offer for it.
    QTest::keyClicks(&edit, QStringLiteral("tag:zzz"));

    QTest::keyClick(keyboardTarget(&edit), Qt::Key_Return);

    QCOMPARE(edit.text(), QStringLiteral("tag:zzz"));
    QVERIFY(ran);
}

void TestQueryCompleter::theDescriptionSurvivesAModestPopupWidth()
{
    // The delegate lends the description whatever the value does not need, up
    // to 65% of the row. Under the previous even split the longest built-in
    // description needed a ~650px popup to survive; it now needs ~500px, which
    // is the difference between the column working at an ordinary window size
    // and being decorative.
    //
    // 550px is chosen to sit inside that band: the current rule paints the text
    // in full there, an even split cannot. A width outside the band would pass
    // against both rules and prove nothing.
    //
    // The delegate is private to the .cpp, so this renders the real popup and
    // reads the pixels back rather than reaching for the class: whether the text
    // is legible on screen is a painting question, not an arithmetic one.
    Config config;
    QLineEdit edit;
    edit.resize(550, edit.sizeHint().height());
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    edit.setFocus();
    QueryCompleter completer(&edit, config);

    QTest::keyClicks(&edit, QStringLiteral("path"));
    QListView *popup = findPopup();
    QVERIFY(popup && popup->isVisible());
    QVERIFY(QTest::qWaitForWindowExposed(popup));

    // "path:" carries the longest built-in description, so it is the row that
    // fails first if the column is starved.
    const QModelIndex row = popup->model()->index(0, 0);
    QVERIFY(row.isValid());
    QCOMPARE(row.data(Qt::DisplayRole).toString(), QStringLiteral("path:"));

    // The geometry this test depends on, asserted rather than assumed.
    //
    // The whole point here is that the description survives a MODEST popup, so
    // a popup the windowing system decided to make wide would pass while
    // proving nothing about the 550px case. Under Wayland the compositor gave
    // this popup a 1278x0 viewport: the zero height made the grab below return
    // a null pixmap and the failure was at least loud, but the width would have
    // been a silent false pass had the height been usable. The tests are pinned
    // to the offscreen platform in tests/CMakeLists.txt for this reason; this
    // guard is what makes a run outside ctest fail honestly instead.
    const QSize viewport = popup->viewport()->size();
    QVERIFY2(viewport.height() > 0,
             qPrintable(QStringLiteral("popup viewport has no height (%1x%2): "
                                       "the platform never laid the popup out, "
                                       "so any grab of it is empty")
                            .arg(viewport.width()).arg(viewport.height())));
    QVERIFY2(viewport.width() <= 700,
             qPrintable(QStringLiteral("popup viewport is %1px wide, far more "
                                       "than the ~550px this test exists to "
                                       "check: it would measure a different "
                                       "popup and pass for the wrong reason")
                            .arg(viewport.width())));

    const QRect rect = popup->visualRect(row);
    QVERIFY(rect.isValid());
    QPixmap shot = popup->viewport()->grab(rect);
    QVERIFY(!shot.isNull());
    const QImage image = shot.toImage();

    // Measure how much of the row carries ink, then compare that against the
    // width the description needs when it is NOT elided.
    //
    // "Something was drawn" is too weak a check: the previous even-split rule
    // also drew the description, just cut down to an ellipsis, so a blank-or-not
    // test passes against the very code this replaces. What distinguishes the
    // two is whether the full text fits, which is a width comparison.
    const QRgb background = image.pixel(image.width() - 2, image.height() / 2);
    int rightmostInk = -1;
    int leftmostInkAfterValue = image.width();
    for (int x = 0; x < image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y) {
            if (image.pixel(x, y) != background) {
                rightmostInk = qMax(rightmostInk, x);
                break;
            }
        }
    }
    QVERIFY2(rightmostInk >= 0, "the row rendered entirely blank");

    // The description is right-aligned, so the ink running to the right edge is
    // the description itself. Walk left from there over the contiguous run to
    // find how wide it was actually painted.
    int x = rightmostInk;
    int gapRun = 0;
    while (x > 0 && gapRun < 8) {
        bool column = false;
        for (int y = 0; y < image.height(); ++y) {
            if (image.pixel(x, y) != background) {
                column = true;
                break;
            }
        }
        gapRun = column ? 0 : gapRun + 1;
        if (column)
            leftmostInkAfterValue = x;
        --x;
    }

    const QFontMetrics metrics(popup->font());
    const QString description = popup->model()->index(0, 1).data().toString();
    QCOMPARE(description, QStringLiteral("directory below the Maildir root"));
    const int painted = rightmostInk - leftmostInkAfterValue;
    const int needed = metrics.horizontalAdvance(description);

    // Allow a little slack for antialiasing at the glyph edges.
    QVERIFY2(painted >= needed - 4,
             qPrintable(QStringLiteral("description elided: painted %1px of the "
                                       "%2px it needs")
                            .arg(painted).arg(needed)));
}

QTEST_MAIN(TestQueryCompleter)
#include "test_querycompleter.moc"
