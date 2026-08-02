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

Q_DECLARE_METATYPE(ThreadSummary)
Q_DECLARE_METATYPE(MessageRef)
Q_DECLARE_METATYPE(TagChange)
