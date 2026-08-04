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

QTEST_MAIN(TestTagDialog)
#include "test_tagdialog.moc"
