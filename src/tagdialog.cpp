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

#include "tagdialog.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

TagNameProblem validateTagName(const QString &tag)
{
    const QString trimmed = tag.trimmed();

    if (trimmed.isEmpty())
        return TagNameProblem::Empty;

    // notmuch's CLI reads "-tag" as an instruction to remove that tag, so a tag
    // actually named "-inbox" is a trap the user cannot easily undo later.
    if (trimmed.startsWith(QLatin1Char('-')))
        return TagNameProblem::LeadingDash;

    for (const QChar c : trimmed) {
        // Checked before the general control-character test, since space is not
        // a control character but splits a tag just as effectively.
        if (c.isSpace())
            return TagNameProblem::ContainsSpace;
        if (c.category() == QChar::Other_Control)
            return TagNameProblem::ControlChar;
    }

    return TagNameProblem::Ok;
}

QString tagNameProblemText(TagNameProblem problem, const QString &tag)
{
    switch (problem) {
    case TagNameProblem::Ok:
        return {};
    case TagNameProblem::Empty:
        return QCoreApplication::translate("TagDialog",
                                           "A tag name cannot be empty.");
    case TagNameProblem::LeadingDash:
        return QCoreApplication::translate(
            "TagDialog",
            "'%1' cannot start with '-': notmuch reads a leading dash as an "
            "instruction to remove a tag.").arg(tag);
    case TagNameProblem::ContainsSpace:
        return QCoreApplication::translate(
            "TagDialog", "'%1' cannot contain spaces.").arg(tag);
    case TagNameProblem::ControlChar:
        return QCoreApplication::translate(
            "TagDialog", "'%1' contains a character that cannot be typed "
                         "back.").arg(tag);
    }
    return {};
}

namespace {

/// Splits a line edit's contents into tag names, dropping empties.
///
/// Comma-separated, so several tags can be applied in one pass. A tag name
/// cannot contain a comma once validateTagName() has run, which is what makes
/// this unambiguous.
QStringList splitTags(const QString &text)
{
    QStringList tags;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            tags.append(trimmed);
    }
    return tags;
}

/// The tag the cursor sits in, which is what completion should match against.
///
/// The field holds a comma-separated list, so the last comma before the cursor
/// bounds the token. Leading space is dropped so "unread, fl" completes on "fl"
/// rather than on " fl", which would match nothing.
QString currentToken(const QLineEdit *edit)
{
    const QString text = edit->text();
    const int cursor = qBound(0, edit->cursorPosition(), int(text.size()));

    const int start = text.lastIndexOf(QLatin1Char(','), qMax(0, cursor - 1)) + 1;
    return text.mid(start, cursor - start).trimmed();
}

/// Overwrites the token under the cursor with `value`, leaving the rest of the
/// list alone, and puts the caret after what was inserted.
void replaceCurrentToken(QLineEdit *edit, const QString &value)
{
    const QString text = edit->text();
    const int cursor = qBound(0, edit->cursorPosition(), int(text.size()));

    const int start = text.lastIndexOf(QLatin1Char(','), qMax(0, cursor - 1)) + 1;

    // Keep the separator's spacing as the user typed it: replacing from `start`
    // would eat the space after the comma and give "unread,flagged".
    int tokenStart = start;
    while (tokenStart < cursor && text.at(tokenStart).isSpace())
        ++tokenStart;

    QString updated = text;
    updated.replace(tokenStart, cursor - tokenStart, value);

    edit->setText(updated);
    edit->setCursorPosition(tokenStart + value.size());
}

} // namespace

TagDialog::TagDialog(const QStringList &knownTags,
                     const QHash<QString, int> &currentTags,
                     int threadCount,
                     QWidget *parent)
    : QDialog(parent), m_threadCount(threadCount)
{
    setWindowTitle(tr("Edit tags"));

    auto *layout = new QVBoxLayout(this);

    auto *heading = new QLabel(tr("%n selected thread(s)", "", threadCount),
                               this);
    layout->addWidget(heading);

    auto *form = new QFormLayout;

    m_addEdit = new QLineEdit(this);
    m_addEdit->setPlaceholderText(tr("tag, or several separated by commas"));
    m_removeEdit = new QLineEdit(this);
    m_removeEdit->setPlaceholderText(tr("tag, or several separated by commas"));

    // Completion is a guard against typos, never a whitelist: a tag absent from
    // this list is exactly what the dialog exists to create, so the completer
    // suggests and does not constrain.
    //
    // The two fields complete against different vocabularies. Add reaches the
    // whole database, since naming a tag that does not exist yet is what it is
    // for. Remove offers only what the selection actually carries: on a
    // multi-thread selection that is the union with counts, not the
    // intersection, because removing a tag two of three threads have is a
    // meaningful thing to ask for.
    QStringList removeCandidates = currentTags.keys();
    removeCandidates.sort();

    const QList<QPair<QLineEdit *, QStringList>> fields = {
        { m_addEdit, knownTags },
        { m_removeEdit, removeCandidates },
    };

    for (const auto &[edit, candidates] : fields) {
        auto *completer = new QCompleter(candidates, edit);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        // Hierarchies are the reason this matters: typing "amazon" should find
        // "shopping/amazon".
        completer->setFilterMode(Qt::MatchContains);

        // setWidget, NOT QLineEdit::setCompleter. These fields hold a
        // comma-separated LIST, and setCompleter makes the line edit drive
        // completion, overwriting the prefix with the widget's ENTIRE text on
        // every keystroke. Once the field reads "unread, fl" that whole string
        // is matched against the tag names, nothing matches, and completion
        // silently stops working after the first tag. Setting the prefix from a
        // textEdited handler does not help: the line edit sets it again
        // afterwards.
        //
        // setWidget keeps the popup anchored without ceding control of the
        // prefix, which then becomes ours to drive per token. Exactly the fix
        // QueryCompleter needed in 01ba356; the trap belongs to
        // QLineEdit::setCompleter, not to either class.
        completer->setWidget(edit);

        connect(edit, &QLineEdit::textEdited, this, [edit, completer]() {
            const QString token = currentToken(edit);
            completer->setCompletionPrefix(token);
            if (token.isEmpty() || completer->completionCount() == 0) {
                completer->popup()->hide();
                return;
            }
            completer->complete();
        });

        // Accepting a candidate has to replace the token under the cursor
        // rather than the whole field, or taking "flagged" would discard every
        // tag already typed.
        connect(completer, QOverload<const QString &>::of(&QCompleter::activated),
                this, [edit](const QString &value) {
            replaceCurrentToken(edit, value);
        });
    }

    form->addRow(tr("Add:"), m_addEdit);
    form->addRow(tr("Remove:"), m_removeEdit);
    layout->addLayout(form);

    // The tags already on the selection, so removing one does not require
    // remembering its name. Tri-state, because with several threads selected a
    // tag can be on some and not others.
    m_currentList = new QListWidget(this);
    QStringList sorted = currentTags.keys();
    sorted.sort();
    for (const QString &tag : sorted) {
        const int count = currentTags.value(tag);

        auto *item = new QListWidgetItem(tag, m_currentList);
        if (count >= threadCount) {
            item->setCheckState(Qt::Checked);
            m_fullyTagged.append(tag);
        } else {
            item->setCheckState(Qt::PartiallyChecked);
            // Say what "partial" means in numbers, rather than leaving the user
            // to infer it from a shaded box.
            item->setText(tr("%1  (on %2 of %3)")
                              .arg(tag).arg(count).arg(threadCount));
            item->setData(Qt::UserRole, tag);
        }
        if (item->data(Qt::UserRole).isNull())
            item->setData(Qt::UserRole, tag);
    }

    if (m_currentList->count() > 0) {
        layout->addWidget(new QLabel(tr("Tags on the selection:"), this));
        layout->addWidget(m_currentList);
    } else {
        m_currentList->hide();
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                             | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    connect(buttons, &QDialogButtonBox::accepted, this, &TagDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_addEdit->setFocus();
}

void TagDialog::accept()
{
    QStringList add = splitTags(m_addEdit->text());
    QStringList remove = splitTags(m_removeEdit->text());

    // Validate what is being ADDED. A partial change is worse than none, since
    // the user cannot tell which half landed, so this runs before anything is
    // applied.
    //
    // REMOVAL is deliberately not validated. The rules here exist to stop a
    // troublesome tag being CREATED; a tag that already exists is a fact, and
    // refusing to remove it because it breaks a rule leaves the user with a
    // tag they can see, cannot type, and cannot get rid of. That happened with
    // `deleted-from:Inbox/SlackBuilds users`: an origin tag naming a Maildir
    // folder whose name contains a space, rejected by the space rule, so the
    // one dialog that could have cleared it refused the only text that names
    // it. Whether such a tag SHOULD exist is a separate question from whether
    // the user may delete it, and the answer to the second is always yes.
    for (const QString &tag : add) {
        const TagNameProblem problem = validateTagName(tag);
        if (problem != TagNameProblem::Ok) {
            QMessageBox::warning(this, tr("Invalid tag"),
                                 tagNameProblemText(problem, tag));
            return;   // Stay open, with the text still there to fix.
        }
    }

    // Then the checkbox list. An item whose state the user did not touch must
    // change nothing, which is why PartiallyChecked is skipped entirely: it is
    // the "leave this alone" state for a tag that is on some threads only.
    for (int row = 0; row < m_currentList->count(); ++row) {
        QListWidgetItem *item = m_currentList->item(row);
        const QString tag = item->data(Qt::UserRole).toString();

        switch (item->checkState()) {
        case Qt::Unchecked:
            // Was on at least one thread, since it is in this list at all.
            if (!remove.contains(tag))
                remove.append(tag);
            break;
        case Qt::Checked:
            // Only meaningful when it was NOT already on every thread; a tag
            // already everywhere and left checked is not a change.
            if (!add.contains(tag) && !m_fullyTagged.contains(tag))
                add.append(tag);
            break;
        case Qt::PartiallyChecked:
            break;   // Untouched: do nothing, deliberately.
        }
    }

    m_add = add;
    m_remove = remove;
    QDialog::accept();
}

QStringList TagDialog::tagsToAdd() const
{
    return m_add;
}

QStringList TagDialog::tagsToRemove() const
{
    return m_remove;
}
