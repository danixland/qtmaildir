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

    /// The thread's FIRST message, which is the one the root card stands for.
    ///
    /// Carried by the query itself rather than learned when the thread is
    /// expanded. That timing was item 66: until a thread had been opened the
    /// model did not know this id, so clicking an unexpanded root fell through
    /// to rendering the whole conversation, and the identical click behaved
    /// differently afterwards.
    ///
    /// Unlike `recipients` below, this is free. It comes from
    /// notmuch_thread_get_toplevel_messages, which reads the INDEX, not the
    /// message files: measured indistinguishable from not collecting it at all
    /// over a 36,615-thread database. Do not move it behind a flag by analogy
    /// with recipients; the two have nothing in common but their position here.
    QString firstMessageId;

    /// The tags of that ONE message, as opposed to `tags` above, which is
    /// notmuch's union over the whole thread.
    ///
    /// A card stands for one message but sits above a conversation, and shows
    /// both: its own tags at full size, the thread's others smaller (item
    /// 111). Without this the split is unknown until the row is opened and the
    /// message loads, so every chip renders as the card's own and then shrinks
    /// on selection, which is what the user reported.
    ///
    /// Free, for the same reason `firstMessageId` is: the walk that finds that
    /// message is already happening and this reads the INDEX, not the message
    /// file. Do not move it behind a flag by analogy with `recipients`.
    QStringList firstMessageTags;

    /// That message's sender, as a BARE ADDRESS with no display name.
    ///
    /// `authors` above is notmuch's own summarised string and carries display
    /// names ONLY: measured against the real index, 'Ryanair' and 'The Hacker
    /// News tramite LinkedIn', with no `@` anywhere. A card therefore has no
    /// address to hash for its avatar and nothing for the business-sender list
    /// to match, which is why this exists (item 169).
    ///
    /// Hashing the display name instead was rejected: notmuch BUILDS those
    /// strings, so one sender's identity varies as the string does.
    ///
    /// Free, for the same reason `firstMessageId` and `firstMessageTags` are:
    /// the walk that finds that message is already happening and From is
    /// served from the INDEX, not the message file. Measured 2026-08-26 on the
    /// developer's database: 1322 distinct senders in 12 ms, 5105 messages
    /// enumerated in 76 ms. Do not move it behind a flag by analogy with
    /// `recipients`.
    QString firstMessageSender;

    /// That message's file, RELATIVE to the database path, which is what says
    /// which ACCOUNT it belongs to.
    ///
    /// Relative and not absolute, deliberately. The UI knows an account only
    /// by its `maildir`, itself a database-relative prefix, so an absolute
    /// path here matches no account and silently resolves every row to none.
    ///
    /// Needed because Delete moves the file (item 103) and the destination is
    /// per account, so the action has to resolve an account before it can name
    /// a trash folder. Resolving through the thread's account TAG instead is
    /// not equivalent: that tag is optional config, so an account without one
    /// would silently be undeletable, while a maildir prefix is what makes a
    /// message belong to an account in the first place.
    ///
    /// Free for the same reason firstMessageId and firstMessageTags are: the
    /// walk that finds that message is already happening, and this reads the
    /// INDEX rather than the message file. Do not move it behind a flag by
    /// analogy with `recipients`.
    QString firstMessagePath;

    /// Who the thread's messages were sent TO, summarised for one line.
    ///
    /// Empty unless the query asked for it, and that is a performance
    /// contract rather than a default: To is NOT served from notmuch's index,
    /// so filling this reads every message file. Measured at 8.7 ms per thread,
    /// which is 38 seconds over a 4411-thread inbox and 663 ms over a
    /// 601-thread sent view. Only a Sent query asks.
    ///
    /// Shown in the sender's place there, where `authors` is the user on every
    /// row and carries nothing.
    QString recipients;

    /// The FIRST recipient's bare address, for the avatar in a flat view.
    ///
    /// Filled by the same walk as `recipients` and under the same flag, so it
    /// costs nothing extra: the To header is already parsed there.
    ///
    /// A Sent or Drafts card's `firstMessageSender` is the user on every row,
    /// so every card would carry one pattern and only the initials would vary.
    /// The avatar answers "who is this row about", and there that is the
    /// recipient. `firstMessageSender` stays the fallback for a row with no
    /// usable To.
    QString firstMessageRecipient;

    bool isUnread() const { return tags.contains(QStringLiteral("unread")); }
    bool isFlagged() const { return tags.contains(QStringLiteral("flagged")); }
    /// True when this message was forwarded. The Maildir "P" (passed) flag,
    /// which notmuch translates to a tag under maildir.synchronize_flags.
    /// Item 68 measured the whole database: nothing derives this from a
    /// subject line, so a "Fwd:" subject with no flag is correctly unmarked.
    bool isPassed() const { return tags.contains(QStringLiteral("passed")); }

    /// True when this message was replied to. The Maildir "R" flag.
    bool isReplied() const { return tags.contains(QStringLiteral("replied")); }
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

    /// For the message pane header's flagged mark (item 70). The tags are
    /// already carried, so this is the same predicate MessageNode and
    /// ThreadSummary offer rather than new state.
    bool isFlagged() const { return tags.contains(QStringLiteral("flagged")); }
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

    /// The BARE address of the message's From, with any display name
    /// discarded, like `ThreadSummary::firstMessageSender` is for a thread
    /// row. `from` above is the raw header and usually carries a display
    /// name, which is what the avatar's hash and the business-senders
    /// lookup cannot take initials or match against.
    QString senderAddress;
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
    /// True when this message was forwarded. The Maildir "P" (passed) flag,
    /// which notmuch translates to a tag under maildir.synchronize_flags.
    /// Item 68 measured the whole database: nothing derives this from a
    /// subject line, so a "Fwd:" subject with no flag is correctly unmarked.
    bool isPassed() const { return tags.contains(QStringLiteral("passed")); }

    /// True when this message was replied to. The Maildir "R" flag.
    bool isReplied() const { return tags.contains(QStringLiteral("replied")); }

    /// notmuch applies "attachment" while indexing, so this needs no MIME
    /// parsing, exactly as on ThreadSummary.
    bool hasAttachment() const
    {
        return tags.contains(QStringLiteral("attachment"));
    }

    bool isDeleted() const { return tags.contains(QStringLiteral("deleted")); }
    bool isSpam() const { return tags.contains(QStringLiteral("spam")); }

    /// True while the message is tagged for removal, exactly as the thread
    /// predicate of the same name. A reply carries its own fate: a
    /// message-scoped Delete tags one message, and the reply's row is the only
    /// place the user can see that happen.
    bool isDoomed() const { return isDeleted() || isSpam(); }
};

/// What an action is about to touch, resolved from the selection.
///
/// Exists because the thread list holds two kinds of row since item 20, so a
/// keypress alone no longer says whether it hit one message or seven. Actions
/// take one of these rather than a bare list of thread ids, and the status bar
/// reports it: this project's answer to that ambiguity is to make the scope
/// visible, not to add a confirmation dialog. See CLAUDE.md on why.
struct ActionScope
{
    QStringList threadIds;   ///< Whole threads to act on.
    QStringList messageIds;  ///< Individual messages to act on.

    /// Messages the action will touch in total, for the status bar. A whole
    /// thread contributes all of its messages, a message row contributes one.
    int messageCount = 0;

    /// True when any whole thread is in scope, which drives the
    /// "(whole thread)" suffix in the status bar.
    bool wholeThread = false;

    bool isEmpty() const
    {
        return threadIds.isEmpty() && messageIds.isEmpty();
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

/// What opens a composer. Built by MainWindow, consumed by ComposeWindow.
///
/// Built from the DATABASE, never from the model. The model's data comes from
/// the query, so a row whose state has not been re-queried carries stale
/// values, and a reply built from a stale row would carry the wrong
/// recipients. This is the same rule Restore already follows.
struct ComposeContext
{
    enum class Kind { New, Reply, ReplyAll, Forward, Draft };

    QString accountKey;          ///< Which account sends. Plain data here; the resolution rules live with whatever builds this context.
    Kind kind = Kind::New;
    QString originalPath;        ///< The .eml being replied to or forwarded. Empty for New.
    QString inReplyTo;           ///< Message-ID of the original. EMPTY for a Forward: carrying In-Reply-To would file the forward under the thread it left, in the recipient's client.

    /// Message-ID of the message being answered, for flagging it afterwards.
    ///
    /// Item 68. Separate from inReplyTo because that is a THREADING header and
    /// is deliberately empty on a Forward, while the P flag still has to land
    /// on the message that was forwarded. Set for Reply, ReplyAll and Forward;
    /// empty for New and for a resumed Draft.
    QString sourceMessageId;
    QStringList references;      ///< The original's References plus its Message-ID.
    QStringList to;              ///< Pre-filled, the user's own addresses already stripped.
    QStringList cc;
    QStringList bcc;             ///< Only a resumed draft has one; see MimeParser::bcc.
    QString subject;             ///< Re:/Fwd: prefixed, an existing prefix not doubled.
    QString quotedBody;          ///< The >-prefixed original. Empty when the action does not quote.

    /// The body as the user last left it, for a resumed draft ONLY.
    ///
    /// Separate from quotedBody because it is not a quote and must not be
    /// framed like one: no attribution, no blank lines added, no cursor moved
    /// to make room. It is the message itself.
    QString body;

    /// The draft file this composer OWNS, empty for every other kind.
    ///
    /// Seeded into ComposeWindow::m_draftPath so the next autosave REPLACES
    /// the file rather than leaving the original beside it. Without it a
    /// resumed draft becomes two drafts on the first autosave.
    QString draftPath;
    bool seedHtml = false;       ///< Did the original carry a text/html part.
    QStringList attachments;     ///< Carried forward for Forward, empty otherwise.
};

/// What the composer produces, consumed by MessageBuilder.
///
/// In-Reply-To and References are NOT optional. Without them a reply appears
/// as an orphan thread in the sender's own client.
struct OutgoingMessage
{
    QString accountKey;
    QStringList to;
    QStringList cc;
    QStringList bcc;
    QString subject;
    QString markdownBody;        ///< The source text, exactly as typed.
    bool sendHtml = false;       ///< The composer's per-message toggle.
    QStringList attachments;     ///< Local paths, read at build time.
    QString inReplyTo;
    QStringList references;
};

Q_DECLARE_METATYPE(ThreadSummary)
Q_DECLARE_METATYPE(MessageRef)
Q_DECLARE_METATYPE(MessageNode)
Q_DECLARE_METATYPE(TagChange)
Q_DECLARE_METATYPE(DatabaseStats)
