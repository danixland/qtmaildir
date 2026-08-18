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

#include <QCheckBox>
#include <QCompleter>
#include <QLineEdit>
#include <QListWidget>

#include "tagdialog.h"

class TestTagDialog : public QObject
{
    Q_OBJECT
private slots:
    void validNamesAreAccepted();
    void emptyNameIsRejected();
    void leadingDashIsRejected();
    void whitespaceIsRejected();
    void controlCharactersAreRejected();
    void everyProblemHasAMessage();

    void typedTagIsAdded();
    void unknownTagIsStillAccepted();
    void multipleTagsSeparateOnComma();
    void uncheckingACurrentTagRemovesIt();
    void aPartialTagLeftAloneChangesNothing();
    void aPartialTagCheckedIsAddedEverywhere();
    void nothingTouchedYieldsNoChange();
    void completionFollowsTheTagAfterAComma();
    void acceptingACandidateKeepsTheOtherTags();
    void removeCompletesOnlyTheSelectionsOwnTags();
    void removeStillAcceptsATagItDoesNotSuggest();
    void aTagWithASpaceCanStillBeRemoved();
};

void TestTagDialog::validNamesAreAccepted()
{
    // Hierarchical tags are the common case here, and the '/' must survive:
    // notmuch treats it as an ordinary character in a tag name.
    for (const QString &tag : { QStringLiteral("inbox"),
                                QStringLiteral("shopping/amazon"),
                                QStringLiteral("mailing-list/SBo"),
                                QStringLiteral("2026"),
                                QStringLiteral("with.dots"),
                                QStringLiteral("under_score"),
                                // A dash anywhere but the front is fine.
                                QStringLiteral("half-done") }) {
        QCOMPARE(validateTagName(tag), TagNameProblem::Ok);
    }
}

void TestTagDialog::emptyNameIsRejected()
{
    QCOMPARE(validateTagName(QString()), TagNameProblem::Empty);
    QCOMPARE(validateTagName(QStringLiteral("")), TagNameProblem::Empty);
    // Whitespace only is empty in every sense that matters.
    QCOMPARE(validateTagName(QStringLiteral("   ")), TagNameProblem::Empty);
    QCOMPARE(validateTagName(QStringLiteral("\t")), TagNameProblem::Empty);
}

void TestTagDialog::leadingDashIsRejected()
{
    // notmuch's own CLI reads -tag as "remove tag". A tag named "-inbox" would
    // therefore be a permanent trap for anyone who later types it at a prompt.
    QCOMPARE(validateTagName(QStringLiteral("-inbox")),
             TagNameProblem::LeadingDash);
    // Also after trimming, or a leading space would smuggle one through.
    QCOMPARE(validateTagName(QStringLiteral("  -inbox")),
             TagNameProblem::LeadingDash);
}

void TestTagDialog::whitespaceIsRejected()
{
    // An embedded space is the failure that looks like it worked: the user
    // believes they made one tag and notmuch sees something else.
    QCOMPARE(validateTagName(QStringLiteral("two words")),
             TagNameProblem::ContainsSpace);
    QCOMPARE(validateTagName(QStringLiteral("tab\there")),
             TagNameProblem::ContainsSpace);
    QCOMPARE(validateTagName(QStringLiteral("new\nline")),
             TagNameProblem::ContainsSpace);
}

void TestTagDialog::controlCharactersAreRejected()
{
    // A null is a control character like any other here. QStringLiteral keeps
    // the whole literal rather than truncating at the null, so this is
    // "null\0byte" in full and the null is what the check catches.
    QString withNull = QStringLiteral("null");
    withNull.append(QChar(0x00));
    withNull.append(QStringLiteral("byte"));
    QCOMPARE(validateTagName(withNull), TagNameProblem::ControlChar);

    QString withBell = QStringLiteral("bell");
    withBell.append(QChar(0x07));
    QCOMPARE(validateTagName(withBell), TagNameProblem::ControlChar);
}

void TestTagDialog::everyProblemHasAMessage()
{
    // A rejection the user cannot read is the same as a silent one.
    for (TagNameProblem problem : { TagNameProblem::Empty,
                                    TagNameProblem::LeadingDash,
                                    TagNameProblem::ContainsSpace,
                                    TagNameProblem::ControlChar }) {
        QVERIFY(!tagNameProblemText(problem, QStringLiteral("x")).isEmpty());
    }
    QVERIFY(tagNameProblemText(TagNameProblem::Ok,
                               QStringLiteral("x")).isEmpty());
}

/// Drives the dialog the way a user would, then accepts it.
static void typeAndAccept(TagDialog *dialog, const QString &add,
                          const QString &remove)
{
    const QList<QLineEdit *> edits = dialog->findChildren<QLineEdit *>();
    QVERIFY(edits.size() >= 2);
    edits.at(0)->setText(add);
    edits.at(1)->setText(remove);
    dialog->accept();
}

void TestTagDialog::typedTagIsAdded()
{
    TagDialog dialog({ QStringLiteral("inbox") }, {}, 1);
    typeAndAccept(&dialog, QStringLiteral("shopping/amazon"), QString());

    QCOMPARE(dialog.tagsToAdd(), QStringList{ QStringLiteral("shopping/amazon") });
    QVERIFY(dialog.tagsToRemove().isEmpty());
}

void TestTagDialog::unknownTagIsStillAccepted()
{
    // Completion is a guard against typos, NOT a whitelist. Inventing a tag is
    // the entire point of the dialog, so a name absent from the vocabulary must
    // go through untouched.
    TagDialog dialog({ QStringLiteral("inbox") }, {}, 1);
    typeAndAccept(&dialog, QStringLiteral("brand/new/tag"), QString());

    QCOMPARE(dialog.tagsToAdd(), QStringList{ QStringLiteral("brand/new/tag") });
}

void TestTagDialog::multipleTagsSeparateOnComma()
{
    TagDialog dialog({}, {}, 1);
    typeAndAccept(&dialog, QStringLiteral("one, two,three"), QString());

    QCOMPARE(dialog.tagsToAdd(), QStringList({ QStringLiteral("one"),
                                               QStringLiteral("two"),
                                               QStringLiteral("three") }));
}

void TestTagDialog::aTagWithASpaceCanStillBeRemoved()
{
    // validateTagName() rejects a space, and that rule is right: it stops a
    // troublesome tag being CREATED. It ran on the removal list too, which is
    // not the same question. A tag that already exists is a fact, and refusing
    // to remove it because it breaks a naming rule leaves the user with a tag
    // they can see and cannot get rid of.
    //
    // Reached by a real Maildir: a folder named "Inbox/SlackBuilds users"
    // produced `deleted-from:Inbox/SlackBuilds users`, and the one dialog that
    // could have cleared it refused the only text that names it.
    //
    // Only the TYPED route was blocked. Unchecking appends to the removal list
    // after validation has run, so it worked throughout; that asymmetry is why
    // both routes are asserted here rather than just the one that failed.
    const QString spaced =
        QStringLiteral("deleted-from:Inbox/SlackBuilds users");
    QHash<QString, int> current;
    current.insert(spaced, 1);

    // Typed into the remove field, which is what a user does for a tag they
    // can see on the message. Before the fix this raised a modal warning and
    // returned without accepting, so the dialog simply would not close.
    TagDialog typed({ spaced }, current, 1);
    const QList<QLineEdit *> edits = typed.findChildren<QLineEdit *>();
    QCOMPARE(edits.size(), 2);
    edits.at(1)->setText(spaced);
    typed.accept();

    QCOMPARE(typed.tagsToRemove(), QStringList{ spaced });
    QVERIFY(typed.tagsToAdd().isEmpty());

    // And unchecking it in the list, the other way to the same place.
    TagDialog unchecked({ spaced }, current, 1);
    auto *list = unchecked.findChild<QListWidget *>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), spaced);
    list->item(0)->setCheckState(Qt::Unchecked);
    unchecked.accept();

    QCOMPARE(unchecked.tagsToRemove(), QStringList{ spaced });

    // ADDING one is still refused, which is the rule this must not have
    // weakened. accept() returns without setting the lists, so the dialog
    // stays open with the text there to fix.
    TagDialog added({}, {}, 1);
    const QList<QLineEdit *> addEdits = added.findChildren<QLineEdit *>();
    QCOMPARE(addEdits.size(), 2);
    addEdits.at(0)->setText(QStringLiteral("two words"));
    // Not calling accept(): it would raise a modal warning and block. The
    // validator is the thing under test and is asked directly.
    QVERIFY(validateTagName(QStringLiteral("two words"))
            != TagNameProblem::Ok);
}

void TestTagDialog::uncheckingACurrentTagRemovesIt()
{
    // Every selected thread carries "inbox", so its box starts checked.
    // Clearing it is how a user removes a tag without typing its name.
    QHash<QString, int> current;
    current.insert(QStringLiteral("inbox"), 3);

    TagDialog dialog({ QStringLiteral("inbox") }, current, 3);

    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);

    QListWidgetItem *item = list->item(0);
    QCOMPARE(item->text(), QStringLiteral("inbox"));
    QCOMPARE(item->checkState(), Qt::Checked);

    item->setCheckState(Qt::Unchecked);
    dialog.accept();

    QCOMPARE(dialog.tagsToRemove(), QStringList{ QStringLiteral("inbox") });
    QVERIFY(dialog.tagsToAdd().isEmpty());
}

void TestTagDialog::aPartialTagLeftAloneChangesNothing()
{
    // THE case worth guarding. Two of three threads are unread, so the box is
    // partially checked. Leaving it alone must mean "do not touch", never
    // "apply to all": the second reading silently tags a thread the user never
    // looked at.
    QHash<QString, int> current;
    current.insert(QStringLiteral("unread"), 2);

    TagDialog dialog({ QStringLiteral("unread") }, current, 3);

    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    QListWidgetItem *item = list->item(0);
    QCOMPARE(item->checkState(), Qt::PartiallyChecked);

    dialog.accept();

    QVERIFY2(dialog.tagsToAdd().isEmpty(),
             "a partial tag left alone was added to every thread");
    QVERIFY2(dialog.tagsToRemove().isEmpty(),
             "a partial tag left alone was removed from every thread");
}

void TestTagDialog::aPartialTagCheckedIsAddedEverywhere()
{
    // Deliberately checking a partial box is an instruction: give it to all.
    QHash<QString, int> current;
    current.insert(QStringLiteral("unread"), 2);

    TagDialog dialog({ QStringLiteral("unread") }, current, 3);

    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    list->item(0)->setCheckState(Qt::Checked);
    dialog.accept();

    QCOMPARE(dialog.tagsToAdd(), QStringList{ QStringLiteral("unread") });
    QVERIFY(dialog.tagsToRemove().isEmpty());
}

void TestTagDialog::nothingTouchedYieldsNoChange()
{
    QHash<QString, int> current;
    current.insert(QStringLiteral("inbox"), 2);
    current.insert(QStringLiteral("unread"), 1);

    TagDialog dialog({ QStringLiteral("inbox") }, current, 2);
    dialog.accept();

    QVERIFY(dialog.tagsToAdd().isEmpty());
    QVERIFY(dialog.tagsToRemove().isEmpty());
}

void TestTagDialog::completionFollowsTheTagAfterAComma()
{
    // Reported by the user: the first tag completes, the second does not.
    //
    // QLineEdit::setCompleter matches against the widget's ENTIRE text, so once
    // the field reads "unread, fl" that whole string becomes the completion
    // prefix and nothing matches. The completer has to be driven on the token
    // under the cursor instead. This is the same defect QueryCompleter hit in
    // 01ba356, in a second place.
    //
    // Typed rather than setText(): setText does not drive a completer at all,
    // so a test using it passes against the broken code.
    TagDialog dialog({ QStringLiteral("inbox"), QStringLiteral("unread"),
                       QStringLiteral("flagged") }, {}, 1);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
    QVERIFY(edits.size() >= 2);
    QLineEdit *addEdit = edits.at(0);
    addEdit->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), addEdit);

    // findChild, not QLineEdit::completer(): the completer is attached with
    // setWidget() rather than setCompleter(), for the reason the fix documents,
    // so the line edit does not report one. It is parented to the edit, which
    // is what makes it reachable here.
    QCompleter *completer = addEdit->findChild<QCompleter *>();
    QVERIFY(completer);

    // First tag: this much always worked.
    QTest::keyClicks(addEdit, QStringLiteral("un"));
    QCOMPARE(completer->completionPrefix(), QStringLiteral("un"));
    QVERIFY(completer->completionCount() > 0);

    // Second tag, after a comma and a space. The prefix must be the new token,
    // not the whole line.
    QTest::keyClicks(addEdit, QStringLiteral("read, fl"));
    QCOMPARE(addEdit->text(), QStringLiteral("unread, fl"));

    QCOMPARE(completer->completionPrefix(), QStringLiteral("fl"));
    QVERIFY2(completer->completionCount() > 0,
             "no candidate for the tag after the comma: the completer is "
             "matching against the whole line");
}

void TestTagDialog::acceptingACandidateKeepsTheOtherTags()
{
    // Driving the prefix per token is only half the fix. Accepting a candidate
    // has to overwrite that token too: QCompleter's own insertion replaces the
    // whole field, so taking "flagged" here would discard "unread" with it.
    TagDialog dialog({ QStringLiteral("inbox"), QStringLiteral("unread"),
                       QStringLiteral("flagged") }, {}, 1);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
    QVERIFY(edits.size() >= 2);
    QLineEdit *addEdit = edits.at(0);
    addEdit->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), addEdit);

    QCompleter *completer = addEdit->findChild<QCompleter *>();
    QVERIFY(completer);

    QTest::keyClicks(addEdit, QStringLiteral("unread, fl"));
    QCOMPARE(completer->completionPrefix(), QStringLiteral("fl"));

    // What clicking a row emits.
    emit completer->activated(QStringLiteral("flagged"));

    QCOMPARE(addEdit->text(), QStringLiteral("unread, flagged"));
    // And the separator's spacing survives: replacing from the comma itself
    // would have produced "unread,flagged".
    QVERIFY(addEdit->text().contains(QStringLiteral(", ")));
}

void TestTagDialog::removeCompletesOnlyTheSelectionsOwnTags()
{
    // Reported by the user: removing a tag suggested every tag in the database.
    // Only the tags the selection already carries can be removed, and those are
    // already in the dialog as currentTags.
    //
    // Typed rather than setText(), which does not drive a completer at all.
    TagDialog dialog({ QStringLiteral("inbox"), QStringLiteral("unread"),
                       QStringLiteral("flagged"), QStringLiteral("archive") },
                     { { QStringLiteral("inbox"), 1 } }, 1);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
    QVERIFY(edits.size() >= 2);
    QLineEdit *removeEdit = edits.at(1);
    removeEdit->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), removeEdit);

    QCompleter *completer = removeEdit->findChild<QCompleter *>();
    QVERIFY(completer);

    // "fl" matches "flagged", which the database has and the selection does not.
    QTest::keyClicks(removeEdit, QStringLiteral("fl"));
    QCOMPARE(completer->completionPrefix(), QStringLiteral("fl"));
    QCOMPARE(completer->completionCount(), 0);

    // A tag the selection does carry still completes.
    removeEdit->clear();
    QTest::keyClicks(removeEdit, QStringLiteral("inb"));
    QCOMPARE(completer->completionPrefix(), QStringLiteral("inb"));
    QCOMPARE(completer->completionCount(), 1);
    QCOMPARE(completer->currentCompletion(), QStringLiteral("inbox"));

    // Add is unchanged: it must still reach the whole vocabulary, since
    // creating a tag is what that field is for.
    QLineEdit *addEdit = edits.at(0);
    addEdit->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), addEdit);
    QCompleter *addCompleter = addEdit->findChild<QCompleter *>();
    QVERIFY(addCompleter);
    QTest::keyClicks(addEdit, QStringLiteral("fl"));
    QVERIFY(addCompleter->completionCount() > 0);
}

void TestTagDialog::removeStillAcceptsATagItDoesNotSuggest()
{
    // Completion is a suggestion, never a whitelist. Narrowing the candidates
    // must not start validating input against them.
    TagDialog dialog({ QStringLiteral("inbox") },
                     { { QStringLiteral("inbox"), 1 } }, 1);

    const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
    QVERIFY(edits.size() >= 2);
    edits.at(1)->setText(QStringLiteral("flagged"));
    dialog.accept();

    QVERIFY(dialog.tagsToRemove().contains(QStringLiteral("flagged")));
}

QTEST_MAIN(TestTagDialog)
#include "test_tagdialog.moc"
