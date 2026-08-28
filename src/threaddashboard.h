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

#include <QVector>
#include <QWidget>

#include "threaddigest.h"
#include "types.h"

class TagColors;
class TagStrip;
class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

/// The activity histogram, as its own widget.
///
/// Separate because it is the one part of the dashboard that must paint, and
/// keeping it apart is what leaves the rest of the pane assertable without a
/// painter. It draws ThreadDigest::buckets at a fixed height, one colour for
/// the bars with the palette's highlight on the busiest.
class ActivitySparkline : public QWidget
{
    Q_OBJECT
public:
    explicit ActivitySparkline(QWidget *parent = nullptr);

    void setBuckets(const QVector<int> &buckets, int busiest);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<int> m_buckets;
    int m_busiest = -1;
};

/// The right pane for a THREAD row: what a conversation is, rather than one of
/// its messages.
///
/// A thread row stands for the conversation, so there is no single message to
/// render. This shows the conversation itself: who is in it, what it is
/// tagged, how much of it is unread, which messages are still waiting, and
/// when it was busy.
///
/// Built once in the constructor and repopulated by setDigest(), so switching
/// rows rebuilds no widgets.
///
/// **It invents no colours.** Participants reuse Avatar::pixmapFor(), tags use
/// TagColors::colourFor() through a TagStrip, and the progress bar and the
/// sparkline use the palette's highlight. Every colour here already means the
/// same thing somewhere else in the application.
class ThreadDashboard : public QWidget
{
    Q_OBJECT
public:
    explicit ThreadDashboard(QWidget *parent = nullptr);

    /// Not owned; must outlive the dashboard. Forwarded to the tag strip.
    void setTagColors(const TagColors *colours);

    /// The thread's subject and account, which the digest does not carry: it
    /// is built from the index by the worker, while these are already on the
    /// summary the list holds. Call before or after setDigest(), either way.
    void setThreadHeading(const QString &subject, const QString &accountLabel);

    /// The thread's tags, from the summary. The digest carries none: a
    /// thread's tags are notmuch's union and the model already has them.
    void setTags(const QStringList &tags);

    void setDigest(const ThreadDigest &digest);

    /// How many unread entries the Waiting-for-you block actually lists.
    ///
    /// This and the two below exist so the pane's content is assertable
    /// without rendering it. A probe that renders and counts pixels proves
    /// nothing here, for every reason under "Rendering probes lie".
    int unreadCountShown() const { return m_unreadShown.size(); }

    /// How many unread messages the block could not list. Zero when the list
    /// is complete, which is also when no "+N more" link is shown.
    ///
    /// Taken from unreadTotal rather than from the list's own size, because
    /// the list is capped and a count derived from it would under-report on
    /// exactly the threads where the number matters.
    int hiddenUnreadCount() const;

    /// True when the thread has nothing unread, so the block is replaced by a
    /// single "All caught up" line.
    bool showingAllCaughtUp() const { return m_digest.unreadTotal == 0; }

signals:
    /// An unread entry was clicked. The pane is a dead end without this: it
    /// has just told the user what they have not read.
    void messageActivated(const QString &messageId);

    /// The "+N more" link was clicked. Expands the thread in the LEFT pane,
    /// where the full list already lives, and leaves the selection alone.
    void expandRequested();

    void markAllReadRequested();
    void archiveRequested();
    void deleteRequested();

private:
    /// Rebuilds the Waiting-for-you block from m_digest.
    void rebuildUnread();

    /// Rebuilds the participants row from m_digest.senders.
    void rebuildParticipants();

    ThreadDigest m_digest;
    QVector<MessageNode> m_unreadShown;

    QLabel *m_subject = nullptr;
    QLabel *m_summaryLine = nullptr;  ///< "N people, N days".
    QLabel *m_account = nullptr;
    QWidget *m_participants = nullptr;
    QVBoxLayout *m_participantsLayout = nullptr;
    TagStrip *m_tags = nullptr;
    QLabel *m_counts = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_waitingHeading = nullptr;
    QWidget *m_unreadBlock = nullptr;
    QVBoxLayout *m_unreadLayout = nullptr;
    QLabel *m_allCaughtUp = nullptr;
    QPushButton *m_moreLink = nullptr;
    ActivitySparkline *m_sparkline = nullptr;
    QLabel *m_activityLine = nullptr;
};
