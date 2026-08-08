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

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

struct ThreadSummary
{
    QString threadId;
    QString subject;
    QString authors;
    QDateTime date;
    int totalCount = 0;
    int matchedCount = 0;
    QStringList tags;

    bool isUnread() const { return tags.contains(QStringLiteral("unread")); }
    bool isFlagged() const { return tags.contains(QStringLiteral("flagged")); }
    bool isDeleted() const { return tags.contains(QStringLiteral("deleted")); }
    bool isSpam() const { return tags.contains(QStringLiteral("spam")); }

    /// notmuch applies "attachment" itself while indexing, so this needs no
    /// MIME parsing and no extra worker query: the tag is already in tags.
    bool hasAttachment() const
    {
        return tags.contains(QStringLiteral("attachment"));
    }

    /// True while the thread is tagged for removal. notmuch deletes nothing
    /// itself: the tag marks the thread for whatever the user's sync script
    /// does next, so the row has to show it is on its way out.
    bool isDoomed() const { return isDeleted() || isSpam(); }
};

struct MessageRef
{
    QString messageId;
    QString filePath;
    QStringList tags;

    /// True when the message itself matched the user's query, as opposed to
    /// being pulled in only because a sibling in its thread matched. Drives
    /// whether it renders expanded or as a stub.
    bool matched = true;
};

/// One message as a row in the thread list.
///
/// Separate from MessageRef, which exists for RENDERING a thread and carries
/// only what the message pane needs. A row has to be drawn without opening the
/// message at all, so the display facts live here.
struct MessageNode
{
    QString messageId;
    QString threadId;  ///< The thread this message belongs to.
    QString from;
    QString subject;
    QDateTime date;
    QStringList tags;
    QString filePath;

    /// Reply depth within the thread. 0 is the thread's first message, which
    /// occupies the ROOT row rather than a child row: the user's model is
    /// "N replies", so a thread of 7 shows 1 root and 6 descendants.
    int depth = 0;

    bool isUnread() const { return tags.contains(QStringLiteral("unread")); }
    bool isFlagged() const { return tags.contains(QStringLiteral("flagged")); }

    /// notmuch applies "attachment" while indexing, so this needs no MIME
    /// parsing, exactly as on ThreadSummary.
    bool hasAttachment() const
    {
        return tags.contains(QStringLiteral("attachment"));
    }
};

/// One tag mutation, kept so it can be inverted for undo.
struct TagChange
{
    QStringList messageIds;
    QStringList added;
    QStringList removed;
    QString description;  ///< Shown in the undo action's text.

    TagChange inverted() const
    {
        return TagChange{ messageIds, removed, added,
                          QStringLiteral("Undo %1").arg(description) };
    }
};

/// Database-level facts for the Maildir overview.
///
/// Every field is -1 until answered, so a dialog opened against a database that
/// cannot be read shows "unknown" rather than a confident zero. A zero is a
/// claim, and "no mail at all" is exactly the wrong thing to tell someone whose
/// index failed to open.
struct DatabaseStats
{
    int messages = -1;  ///< Every message notmuch has indexed.
    int threads = -1;   ///< Every thread. Differs from messages by reply depth.
    int tags = -1;      ///< Distinct tag names in the database.
};

Q_DECLARE_METATYPE(ThreadSummary)
Q_DECLARE_METATYPE(MessageRef)
Q_DECLARE_METATYPE(MessageNode)
Q_DECLARE_METATYPE(TagChange)
Q_DECLARE_METATYPE(DatabaseStats)
