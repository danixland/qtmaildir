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

#include <QAbstractTableModel>
#include <QColor>
#include <QVector>

#include "tagcolors.h"
#include "types.h"

/// Table model over query results, filled in batches so a large query paints
/// its first screenful immediately.
class ThreadListModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /// No tags column: spelling out a dozen tags per row cost most of the
    /// list's width and was unreadable. Functional tags moved to a chip strip
    /// under the message pane, and the account tag renders as a chip in front
    /// of the subject.
    enum Column {
        /// A paperclip when the thread has an attachment, so it is visible
        /// without opening the thread. Icon only and deliberately narrow;
        /// it carries no text.
        AttachmentColumn = 0,

        /// A star when the thread carries the flagged tag. Beside the
        /// paperclip and the same shape: icon only, narrow, no text.
        FlagColumn,

        DateColumn,
        AuthorsColumn,
        SubjectColumn,
        ColumnCount,
    };

    enum Role {
        /// The thread id behind a row. Views hand out QModelIndexes, but the
        /// worker speaks thread ids, so the mapping belongs on the model
        /// rather than in every caller.
        ThreadIdRole = Qt::UserRole + 1,

        /// The account tag on this thread without its "account-" prefix, for
        /// the chip drawn in front of the subject. Empty when the thread
        /// carries none.
        AccountLabelRole,

        /// Fill colour for that chip.
        AccountColourRole,

        /// Every tag on the thread, for the strip under the message pane.
        TagsRole,

        /// The tags worth drawing as pills under the subject: every tag except
        /// the ones the row already shows another way. Sorted, so a row does
        /// not reshuffle its own pills between repaints.
        PillTagsRole,

        /// The colours for PillTagsRole, in the same order. Supplied by the
        /// model because it owns the TagColors instance; a delegate reading
        /// config itself would be a second source of truth.
        PillColoursRole,
    };

    /// Row fill for a thread tagged `deleted`, and for one tagged `spam`.
    /// Muted rather than saturated: a bulk delete paints every selected row,
    /// and a wall of pure red is harder to read than the list it replaces.
    /// Exposed so a test names the same colour the model uses.
    /// The character shown in AttachmentColumn for a thread that has one.
    /// A paperclip when the system font can draw it, "*" otherwise.
    static QString attachmentGlyph();

    /// The character shown in FlagColumn for a flagged thread.
    /// A star when the system font can draw it, "*" otherwise.
    static QString flagGlyph();

    static QColor deletedColour();
    static QColor spamColour();

    /// The dimmed text colour a READ thread carries.
    ///
    /// Unread rows are left at the palette's own colour and read ones recede,
    /// rather than unread being emphasised. Bold alone used to be the only
    /// cue, which leaves nothing to see when the desktop font is itself
    /// configured bold; colour is a second cue that survives that. Derived
    /// from the palette, never hardcoded.
    static QColor readColour();

    explicit ThreadListModel(QObject *parent = nullptr);

    /// Supplies the account chip colours. Not owned; must outlive the model.
    /// Without one, chips fall back to a colour generated from the tag name.
    void setTagColors(const TagColors *colours) { m_tagColors = colours; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    void appendBatch(const QVector<ThreadSummary> &batch);
    void clear();

    ThreadSummary threadAt(int row) const;

    /// The account keys behind a thread's account tags, for item 49's
    /// per-account sync.
    ///
    /// Returns every one of them, not the first: the thread list shows only one
    /// chip per row, but a thread whose messages landed in two mailboxes really
    /// does span two accounts, and tagging it touches files under both. Syncing
    /// only the one that happens to be shown would strand the other's edits.
    /// Empty when the thread is unknown or carries no account tag.
    QStringList accountKeysForThread(const QString &threadId) const;

    /// Applies a tag change locally so the UI updates before the worker
    /// confirms. To revert a failed write, call again with added and removed
    /// swapped.
    void applyTagChange(const QString &threadId, const QStringList &added,
                        const QStringList &removed);

private:
    QVector<ThreadSummary> m_threads;
    const TagColors *m_tagColors = nullptr;
};
