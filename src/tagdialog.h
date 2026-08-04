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

#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QListWidget;

/// Why a tag name was refused, or Ok when it was not.
///
/// A reason rather than a bool: a rejected tag has to say what was wrong with
/// it, since silently dropping one leaves the user believing they tagged
/// something they did not.
enum class TagNameProblem {
    Ok,
    Empty,          ///< Nothing, or only whitespace.
    LeadingDash,    ///< notmuch reads a leading '-' as "remove this tag".
    ContainsSpace,  ///< Splits into two tags, or fails the write outright.
    ControlChar,    ///< Not typeable, and unreadable once stored.
};

/// Whether `tag` is safe to hand to notmuch.
///
/// A free function so the rules can be tested without a widget. notmuch itself
/// accepts a great deal, so this is deliberately narrow: it rejects only what
/// produces a failed write or a tag the user cannot see they created.
TagNameProblem validateTagName(const QString &tag);

/// The message for a rejected tag, ready to show. Empty for Ok.
QString tagNameProblemText(TagNameProblem problem, const QString &tag);

/// Adds and removes tags across the selected threads.
///
/// One dialog rather than separate add and remove actions: the natural
/// operation is "make these threads look like this", and filing something under
/// a new tag while removing inbox is one thought, not two.
///
/// Pure UI. It contacts no worker and holds no database handle; it is handed
/// the vocabulary and the current state, and returns two lists. That is what
/// lets it be unit-tested without a notmuch database.
class TagDialog : public QDialog
{
    Q_OBJECT
public:
    /// `knownTags` is the completion vocabulary, usually every tag in the
    /// database. `currentTags` maps a tag to how many of the selected threads
    /// carry it, which is what drives the tri-state checkboxes.
    TagDialog(const QStringList &knownTags,
              const QHash<QString, int> &currentTags,
              int threadCount,
              QWidget *parent = nullptr);

    /// Tags to add. Empty when the user asked for nothing.
    QStringList tagsToAdd() const;

    /// Tags to remove.
    QStringList tagsToRemove() const;

    /// Reads both line edits and the checkbox list into the two lists,
    /// reporting the first invalid name rather than applying a partial change.
    ///
    /// Public because QDialog::accept() is: a test drives it directly rather
    /// than clicking a button, since which button carries the AcceptRole is
    /// not what this class is for.
    void accept() override;

private:

    QStringList m_add;
    QStringList m_remove;

    /// Tags that were already on EVERY selected thread when the dialog opened.
    ///
    /// Needed to tell "the user checked this box" apart from "this box was
    /// checked all along": the first is an instruction, the second is not a
    /// change and must not be sent as one.
    QStringList m_fullyTagged;

    int m_threadCount = 0;

    QLineEdit *m_addEdit = nullptr;
    QLineEdit *m_removeEdit = nullptr;
    QListWidget *m_currentList = nullptr;
};
