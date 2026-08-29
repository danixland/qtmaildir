// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 */
#ifndef QTMAILDIR_THREADDIGEST_H
#define QTMAILDIR_THREADDIGEST_H

#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include "types.h"

/// What the thread dashboard shows, as a plain value.
///
/// Crosses the worker boundary like every other structure here: no
/// notmuch_* pointer, no widget, no model. Everything in it is read from the
/// INDEX, so building one opens no message files.
struct ThreadDigest
{
    QString threadId;

    /// Display name and message count, most prolific first.
    QList<QPair<QString, int>> senders;

    /// The unread messages, NEWEST FIRST, at most kUnreadShown of them.
    ///
    /// MessageNode rather than MessageRef, because the dashboard DRAWS these
    /// rows for messages it never opens, which is what MessageNode exists for;
    /// MessageRef carries only what the pane needs once a message is already
    /// open, and has no subject, sender or date. The no-file-opening contract
    /// above still holds: all four of those are served from the index.
    QVector<MessageNode> unread;

    /// How many there really are. The list above is capped, so a count taken
    /// from its size would under-report on exactly the threads that need the
    /// number most.
    int unreadTotal = 0;

    int totalCount = 0;

    /// Every message's file, RELATIVE to the database path, in the walk's own
    /// order.
    ///
    /// Item 178. Delete and Restore ask whether a row is in the trash, and a
    /// CONVERSATION is in the trash only when all of its messages are.
    /// ThreadSummary carries one path, which was the right answer while a
    /// thread row meant its first message and is not one now, so a partly
    /// trashed conversation answered on whichever message the query returned
    /// first: Delete hidden on a thread with mail outside the trash, Restore
    /// offered on one that mostly is not.
    ///
    /// Relative and not absolute, for the same reason
    /// ThreadSummary::firstMessagePath is: the UI knows an account only by its
    /// `maildir`, itself a database-relative prefix, so an absolute path
    /// matches no account and silently resolves every row to none.
    ///
    /// Free, like everything else here: the digest already walks every message
    /// for the sender counts, and a filename is served from the INDEX rather
    /// than the message file. A message with several files contributes only
    /// its first, which is what notmuch_message_get_filename returns; the
    /// question is which FOLDER a message lives in, and a caller testing
    /// "every path is under trash" is answered correctly by any one of them.
    QStringList messagePaths;

    /// Always kBuckets entries. A fixed count is what keeps the sparkline's
    /// geometry testable; a thread spanning five days and one spanning two
    /// years cannot share a bucket size, so the span is what varies and the
    /// label beneath carries the truth.
    QVector<int> buckets;

    qint64 firstTimestamp = 0;
    qint64 lastTimestamp = 0;
    int busiestBucket = -1;

    static constexpr int kBuckets = 7;
    static constexpr int kUnreadShown = 5;
};

Q_DECLARE_METATYPE(ThreadDigest)

#endif // QTMAILDIR_THREADDIGEST_H
