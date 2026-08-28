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

#include "threaddashboard.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include "avatar.h"
#include "cardlayout.h"
#include "tagstrip.h"

namespace {

/// The gap between blocks. One constant so the pane reads as evenly spaced
/// rather than as whatever each block happened to ask for.
constexpr int kBlockSpacing = 12;

/// The avatar beside a participant and beside an unread entry.
int avatarSide(const QFont &font)
{
    // From the font's metrics, not a fixed pixel count, for the reason
    // CardLayout::markSide records: qt6ct sets fonts in PIXELS, where
    // pointSizeF() returns -1, so the metric is what can be measured.
    return qMax(16, QFontMetrics(font).height() + 6);
}

/// How long ago `when` was, in words.
///
/// Deliberately coarse. The dashboard answers "is this stale", not "at what
/// minute did this arrive", and the exact timestamp is one click away on the
/// message itself.
QString relativeTime(const QDateTime &when)
{
    if (!when.isValid())
        return QString();

    const qint64 seconds = when.secsTo(QDateTime::currentDateTime());
    if (seconds < 60)
        return ThreadDashboard::tr("just now");
    if (seconds < 3600)
        return ThreadDashboard::tr("%1 min ago").arg(seconds / 60);
    if (seconds < 86400)
        return ThreadDashboard::tr("%1 h ago").arg(seconds / 3600);
    if (seconds < 86400 * 30)
        return ThreadDashboard::tr("%1 d ago").arg(seconds / 86400);
    return CardLayout::formatDate(when);
}

/// A label for a value that came from a stranger.
///
/// Qt::PlainText EXPLICITLY, on every one of them. A QLabel guesses under
/// Qt::AutoText, so a subject or a display name carrying markup would be
/// interpreted rather than shown. This is the same security property
/// MessageDetailsDialog states at each of its value labels, and it is not a
/// style choice: escaping into a rich-text label is the same protection one
/// mistake away from failing.
QLabel *untrustedLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    return label;
}

}  // namespace

ActivitySparkline::ActivitySparkline(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ActivitySparkline::setBuckets(const QVector<int> &buckets, int busiest)
{
    m_buckets = buckets;
    m_busiest = busiest;
    update();
}

QSize ActivitySparkline::sizeHint() const
{
    // Scaled off the font rather than fixed, so the bars keep their proportion
    // to the text beside them at any desktop font size.
    return QSize(120, qMax(24, QFontMetrics(font()).height() * 2));
}

void ActivitySparkline::paintEvent(QPaintEvent *)
{
    if (m_buckets.isEmpty())
        return;

    int tallest = 0;
    for (int n : m_buckets)
        tallest = qMax(tallest, n);
    if (tallest <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Two colours, both already meaning something: the palette's text at low
    // opacity for the bars, and its highlight for the busiest bucket. A bucket
    // is a quantity, not a category, so nothing here is a new colour.
    QColor bar = palette().color(QPalette::Text);
    bar.setAlpha(90);
    const QColor busy = palette().color(QPalette::Highlight);

    const int gap = 2;
    const int count = m_buckets.size();
    const int available = width() - gap * (count - 1);
    if (available <= 0)
        return;
    const int barWidth = qMax(1, available / count);

    for (int i = 0; i < count; ++i) {
        const int x = i * (barWidth + gap);
        // A non-empty bucket always gets at least one pixel, so "some activity"
        // and "none at all" cannot render identically.
        const int barHeight =
            m_buckets.at(i) == 0
                ? 0
                : qMax(1, m_buckets.at(i) * height() / tallest);
        if (barHeight == 0)
            continue;
        painter.fillRect(QRect(x, height() - barHeight, barWidth, barHeight),
                         i == m_busiest ? busy : bar);
    }
}

ThreadDashboard::ThreadDashboard(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // The content scrolls; the action strip below is its SIBLING, outside the
    // scroll area, so the actions stay on the pane's bottom edge. Hunting for
    // them past a long participant list is the case the scrolling exists for.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll, 1);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(kBlockSpacing);
    scroll->setWidget(content);

    // Block 1: the header.
    m_subject = untrustedLabel(QString());
    m_subject->setWordWrap(true);
    QFont subjectFont = m_subject->font();
    subjectFont.setBold(true);
    m_subject->setFont(subjectFont);
    layout->addWidget(m_subject);

    m_summaryLine = new QLabel;
    m_summaryLine->setTextFormat(Qt::PlainText);
    layout->addWidget(m_summaryLine);

    // The account label is configured by the user rather than received from a
    // stranger, but it is shown as plain text like everything else here: one
    // rule for the pane is easier to keep than an exception per label.
    m_account = untrustedLabel(QString());
    layout->addWidget(m_account);

    m_participants = new QWidget;
    m_participantsLayout = new QVBoxLayout(m_participants);
    m_participantsLayout->setContentsMargins(0, 0, 0, 0);
    m_participantsLayout->setSpacing(4);
    layout->addWidget(m_participants);

    // Block 2: the tag chips. One tier, no muting, drawn by the same strip the
    // message pane uses so the two cannot drift.
    m_tags = new TagStrip;
    // Named for the same reason the message pane's is: the two sit in one
    // widget tree and an unqualified findChild cannot tell them apart.
    m_tags->setObjectName(QStringLiteral("dashboardTagStrip"));
    layout->addWidget(m_tags);

    // Block 3: the counts, with the read-progress bar beneath.
    m_counts = new QLabel;
    m_counts->setTextFormat(Qt::PlainText);
    layout->addWidget(m_counts);

    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    // Deliberately short: it is a proportion, not a control. The palette's
    // highlight fills it, which is the one colour a quantity gets here.
    m_progress->setMaximumHeight(6);
    layout->addWidget(m_progress);

    // Block 4: Waiting for you.
    m_waitingHeading = new QLabel(tr("Waiting for you"));
    m_waitingHeading->setTextFormat(Qt::PlainText);
    QFont headingFont = m_waitingHeading->font();
    headingFont.setBold(true);
    m_waitingHeading->setFont(headingFont);
    layout->addWidget(m_waitingHeading);

    m_unreadBlock = new QWidget;
    m_unreadLayout = new QVBoxLayout(m_unreadBlock);
    m_unreadLayout->setContentsMargins(0, 0, 0, 0);
    m_unreadLayout->setSpacing(6);
    layout->addWidget(m_unreadBlock);

    m_moreLink = new QPushButton;
    m_moreLink->setFlat(true);
    m_moreLink->setCursor(Qt::PointingHandCursor);
    connect(m_moreLink, &QPushButton::clicked, this,
            &ThreadDashboard::expandRequested);
    layout->addWidget(m_moreLink);

    m_allCaughtUp = new QLabel(tr("All caught up"));
    m_allCaughtUp->setTextFormat(Qt::PlainText);
    layout->addWidget(m_allCaughtUp);

    // Block 5: activity.
    m_sparkline = new ActivitySparkline;
    layout->addWidget(m_sparkline);

    m_activityLine = new QLabel;
    m_activityLine->setTextFormat(Qt::PlainText);
    layout->addWidget(m_activityLine);

    layout->addStretch(1);

    // Block 6: the pinned action strip.
    auto *actions = new QWidget;
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(12, 6, 12, 12);
    actionsLayout->setSpacing(6);

    auto *markRead = new QPushButton(tr("Mark all read"));
    connect(markRead, &QPushButton::clicked, this,
            &ThreadDashboard::markAllReadRequested);
    actionsLayout->addWidget(markRead);

    auto *archive = new QPushButton(tr("Archive"));
    connect(archive, &QPushButton::clicked, this,
            &ThreadDashboard::archiveRequested);
    actionsLayout->addWidget(archive);

    auto *remove = new QPushButton(tr("Delete"));
    connect(remove, &QPushButton::clicked, this,
            &ThreadDashboard::deleteRequested);
    actionsLayout->addWidget(remove);

    actionsLayout->addStretch(1);
    outer->addWidget(actions, 0);

    setDigest(ThreadDigest());
}

void ThreadDashboard::setTagColors(const TagColors *colours)
{
    m_tags->setTagColors(colours);
}

void ThreadDashboard::setThreadHeading(const QString &subject,
                                       const QString &accountLabel)
{
    m_subject->setText(subject.isEmpty() ? tr("(no subject)") : subject);
    m_account->setText(accountLabel);
    m_account->setVisible(!accountLabel.isEmpty());
}

void ThreadDashboard::setTags(const QStringList &tags)
{
    m_tags->setTags(tags);
    m_tags->setVisible(!tags.isEmpty());
}

int ThreadDashboard::hiddenUnreadCount() const
{
    return qMax(0, m_digest.unreadTotal - m_unreadShown.size());
}

void ThreadDashboard::setDigest(const ThreadDigest &digest)
{
    m_digest = digest;

    // The cap belongs here as well as in the worker. The worker truncates so
    // the value crossing the boundary stays small; this one is what makes the
    // accessors truthful whatever a caller hands over, which is what task 9
    // and the tests read.
    m_unreadShown = digest.unread;
    if (m_unreadShown.size() > ThreadDigest::kUnreadShown)
        m_unreadShown.resize(ThreadDigest::kUnreadShown);

    const int people = digest.senders.size();
    QString span;
    if (digest.firstTimestamp > 0 && digest.lastTimestamp > 0) {
        const qint64 days =
            (digest.lastTimestamp - digest.firstTimestamp) / 86400;
        span = days <= 0 ? tr("today") : tr("%n day(s)", nullptr, int(days));
    }
    if (span.isEmpty()) {
        m_summaryLine->setText(tr("%n person(s)", nullptr, people));
    } else {
        // Two facts joined by a separator, each already translated. Building
        // the sentence from a format string keeps the word order the
        // translator's to choose.
        m_summaryLine->setText(
            tr("%1 · %2").arg(tr("%n person(s)", nullptr, people), span));
    }

    rebuildParticipants();

    m_counts->setText(tr("%n message(s)", nullptr, digest.totalCount) +
                      QStringLiteral(" · ") +
                      tr("%n unread", nullptr, digest.unreadTotal));

    // The bar shows what has been READ, so a thread with nothing unread reads
    // as full rather than empty.
    m_progress->setMaximum(qMax(1, digest.totalCount));
    m_progress->setValue(qMax(0, digest.totalCount - digest.unreadTotal));
    m_progress->setVisible(digest.totalCount > 0);

    rebuildUnread();

    m_sparkline->setBuckets(digest.buckets, digest.busiestBucket);

    const bool haveSpan = digest.firstTimestamp > 0 && digest.lastTimestamp > 0;
    m_sparkline->setVisible(haveSpan);
    m_activityLine->setVisible(haveSpan);
    if (haveSpan) {
        const QDateTime first =
            QDateTime::fromSecsSinceEpoch(digest.firstTimestamp);
        const QDateTime last =
            QDateTime::fromSecsSinceEpoch(digest.lastTimestamp);
        QString line = tr("%1 → %2")
                           .arg(CardLayout::formatDate(first),
                                CardLayout::formatDate(last));
        if (digest.busiestBucket >= 0 &&
            digest.busiestBucket < digest.buckets.size() &&
            digest.buckets.at(digest.busiestBucket) > 0) {
            // Which bucket was busiest, named by the date it starts on: the
            // bucket size varies with the thread's span, so a bucket INDEX
            // means nothing to the reader and the date is the only honest
            // label.
            const qint64 bucketSpan =
                qMax<qint64>(1, (digest.lastTimestamp - digest.firstTimestamp) /
                                    ThreadDigest::kBuckets);
            const QDateTime busiest = QDateTime::fromSecsSinceEpoch(
                digest.firstTimestamp + bucketSpan * digest.busiestBucket);
            line += QStringLiteral(" · ") +
                    tr("busiest %1").arg(CardLayout::formatDate(busiest));
        }
        m_activityLine->setText(line);
    }
}

void ThreadDashboard::rebuildParticipants()
{
    while (QLayoutItem *item = m_participantsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const int side = avatarSide(font());
    for (const auto &sender : m_digest.senders) {
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        // Avatar::pixmapFor unchanged, so a participant is the same squircle
        // and the same colour they carry on every card in the list. The
        // dashboard chooses nothing here.
        auto *avatar = new QLabel;
        avatar->setPixmap(Avatar::pixmapFor(
            sender.first,
            Avatar::initialsFor(sender.first, sender.first, QString()),
            Avatar::fillFor(sender.first, false), side, font()));
        avatar->setFixedSize(side, side);
        rowLayout->addWidget(avatar);

        auto *name = untrustedLabel(sender.first);
        rowLayout->addWidget(name, 1);

        auto *count = new QLabel(tr("%n message(s)", nullptr, sender.second));
        count->setTextFormat(Qt::PlainText);
        rowLayout->addWidget(count);

        m_participantsLayout->addWidget(row);
    }
    m_participants->setVisible(!m_digest.senders.isEmpty());
}

void ThreadDashboard::rebuildUnread()
{
    while (QLayoutItem *item = m_unreadLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    const bool caughtUp = showingAllCaughtUp();
    m_allCaughtUp->setVisible(caughtUp);
    m_waitingHeading->setVisible(!caughtUp);
    m_unreadBlock->setVisible(!caughtUp);

    const int hidden = hiddenUnreadCount();
    m_moreLink->setVisible(!caughtUp && hidden > 0);
    if (hidden > 0)
        m_moreLink->setText(tr("+%n more", nullptr, hidden));

    if (caughtUp)
        return;

    const int side = avatarSide(font());
    for (const MessageNode &node : std::as_const(m_unreadShown)) {
        // A button rather than a label, so the entry is clickable and
        // keyboard-reachable without the pane growing an event filter. Flat,
        // so it reads as a row rather than as a control.
        auto *entry = new QPushButton;
        entry->setFlat(true);
        entry->setCursor(Qt::PointingHandCursor);
        entry->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        auto *entryLayout = new QHBoxLayout(entry);
        entryLayout->setContentsMargins(4, 2, 4, 2);
        entryLayout->setSpacing(6);

        auto *avatar = new QLabel;
        avatar->setAttribute(Qt::WA_TransparentForMouseEvents);
        avatar->setPixmap(Avatar::pixmapFor(
            node.senderAddress,
            Avatar::initialsFor(node.from, node.senderAddress, QString()),
            Avatar::fillFor(node.from, false), side, font()));
        avatar->setFixedSize(side, side);
        entryLayout->addWidget(avatar);

        // Sender, then subject: both come from a stranger, so both are
        // explicitly plain text.
        const QString who =
            node.from.isEmpty() ? node.senderAddress : node.from;
        auto *text = untrustedLabel(
            who.isEmpty() ? node.subject
                          : QStringLiteral("%1 · %2").arg(who, node.subject));
        text->setAttribute(Qt::WA_TransparentForMouseEvents);
        entryLayout->addWidget(text, 1);

        const QString when = relativeTime(node.date);
        if (!when.isEmpty()) {
            auto *age = new QLabel(when);
            age->setTextFormat(Qt::PlainText);
            age->setAttribute(Qt::WA_TransparentForMouseEvents);
            entryLayout->addWidget(age);
        }

        const QString messageId = node.messageId;
        connect(entry, &QPushButton::clicked, this,
                [this, messageId] { emit messageActivated(messageId); });

        m_unreadLayout->addWidget(entry);
    }
}
