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

#include "carddelegate.h"

#include "cardlayout.h"
#include "threadlistmodel.h"

#include <QApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QPainter>
#include <QRegularExpression>
#include <QStyle>

namespace {

CardLayout::Input inputFor(const QModelIndex &index)
{
    CardLayout::Input in;
    in.isMessage = index.data(ThreadListModel::IsMessageRole).toBool();
    in.depth = index.data(ThreadListModel::MessageDepthRole).toInt();
    in.replyCount = index.data(ThreadListModel::ReplyCountRole).toInt();
    return in;
}

}  // namespace

QRect CardDelegate::expanderRectFor(const QStyleOptionViewItem &option,
                                    const QModelIndex &index)
{
    return CardLayout::compute(inputFor(index), option.rect, option.font)
        .expanderRect;
}

QColor CardDelegate::accentLineColour(const QColor &accountColour)
{
    if (!accountColour.isValid())
        return ThreadListModel::threadLineColour();

    // 0.35 toward Base was the first attempt and produced an INVISIBLE bar on
    // a dark theme: rendered against a Base of (0.169, 0.169, 0.169) it landed
    // at (0.18, 0.22, 0.26), which is the background. The weight is a fraction
    // OF THE ACCOUNT COLOUR, so a low one keeps the background, not the hue.
    //
    // The bar is the account's colour, undiluted. Blending it toward Base at
    // all was the mistake: a chip's colour is chosen to carry text on top and
    // is therefore already muted, and three pixels of a muted colour on a dark
    // background is nothing at all. There is no text on this bar, so nothing
    // needs the contrast a chip's fill was picked for.
    //
    // What DOES step back is the spine, below: a line running the height of a
    // whole expansion has to be followable without competing with the senders
    // beside it, which is a different problem from a 3px edge marker.
    return accountColour;
}

QSize CardDelegate::sizeHint(const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    Q_UNUSED(index);
    // One height for every row, thread and reply alike. Asserted directly in
    // test_cardlayout rather than left to two cards happening to agree.
    return QSize(option.rect.width(), CardLayout::heightFor(option.font));
}

void CardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    // Background, selection and any model fill first, through the style, so a
    // selected or doomed card looks right before anything is drawn on top.
    QStyleOptionViewItem chrome = option;
    initStyleOption(&chrome, index);
    chrome.text.clear();
    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &chrome, painter, widget);

    const CardLayout card =
        CardLayout::compute(inputFor(index), option.rect, option.font);

    painter->save();

    // The account's colour, for both the accent bar and the spines.
    //
    // A reply must resolve its THREAD's colour, not its own: AccountColourRole
    // is empty on a message row, and a spine that fell back to the neutral
    // line under an accented root would break the one continuous edge this
    // design is built on. index.parent() is the thread for a depth-1 reply and
    // the containing subtree for a deeper one, so walk to the root.
    QModelIndex root = index;
    while (root.parent().isValid())
        root = root.parent();
    const QColor accountColour =
        root.data(ThreadListModel::AccountColourRole).value<QColor>();
    const QColor lineColour = accentLineColour(accountColour);

    // The accent bar, thread cards only. Drawn after the chrome so the
    // selection highlight cannot cover it: which account a card belongs to
    // must stay readable on the row the user is looking at.
    if (!card.accentRect.isEmpty())
        painter->fillRect(card.accentRect, lineColour);

    // Spines, under everything else, in the account's hue so an expanded thread
    // is bounded by one colour from its root to its last reply. Muted against
    // the pane's own background, unlike the accent bar: this line runs the full
    // height of every reply and at full strength it shouts.
    if (!card.spines.isEmpty()) {
        const QColor base =
            QGuiApplication::palette().color(QPalette::Base);
        constexpr qreal kSpineWeight = 0.55;
        const qreal inverse = 1.0 - kSpineWeight;
        const QColor spineColour = QColor::fromRgbF(
            lineColour.redF() * kSpineWeight + base.redF() * inverse,
            lineColour.greenF() * kSpineWeight + base.greenF() * inverse,
            lineColour.blueF() * kSpineWeight + base.blueF() * inverse);
        for (const QRect &spine : card.spines)
            painter->fillRect(spine, spineColour);
    }

    // Selection outranks the model's foreground, and the order matters: a read
    // card carries a dimmed colour blended against the UNSELECTED background,
    // so over the highlight it lands grey-on-highlight and close to unreadable.
    const QVariant foreground = index.data(Qt::ForegroundRole);
    if (option.state & QStyle::State_Selected)
        painter->setPen(option.palette.highlightedText().color());
    else if (foreground.isValid())
        painter->setPen(foreground.value<QBrush>().color());
    else
        painter->setPen(option.palette.text().color());

    // The model's font carries bold for unread and strike-out for deleted;
    // initStyleOption resolved it into chrome.font.
    painter->setFont(chrome.font);
    const QFontMetrics metrics(chrome.font);

    // Line 1: sender, then the date flush right.
    painter->drawText(card.senderRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(
                          index.data(ThreadListModel::SendersRole).toString(),
                          Qt::ElideRight, card.senderRect.width()));
    const QDateTime date =
        index.data(ThreadListModel::DateRole).toDateTime();
    painter->drawText(card.dateRect, Qt::AlignVCenter | Qt::AlignRight,
                      date.toString(QStringLiteral("yyyy-MM-dd hh:mm")));

    // Line 2: the flag mark, the subject, the attachment mark.
    QString subject = index.data(ThreadListModel::SubjectRole).toString();
    if (index.data(ThreadListModel::IsMessageRole).toBool()) {
        // Every reply repeating "Re: <the thread's subject>" is the visual
        // signature of a table of records, which is what item 53 is about.
        static const QRegularExpression re(
            QStringLiteral("^\\s*(?:[Rr][Ee]\\s*:\\s*)+"));
        subject.remove(re);
    }
    QString line2;
    if (index.data(ThreadListModel::IsFlaggedRole).toBool())
        line2 += ThreadListModel::flagGlyph() + QLatin1Char(' ');
    line2 += subject;
    if (index.data(ThreadListModel::HasAttachmentRole).toBool())
        line2 += QLatin1Char(' ') + ThreadListModel::attachmentGlyph();
    painter->drawText(card.subjectRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(line2, Qt::ElideRight,
                                         card.subjectRect.width()));

    // The reply count, which is also the expander.
    if (!card.expanderRect.isEmpty()) {
        painter->setFont(CardLayout::smallFont(chrome.font));
        const int count = index.data(ThreadListModel::ReplyCountRole).toInt();
        const QString glyph = (option.state & QStyle::State_Open)
                                  ? QStringLiteral("▾")
                                  : QStringLiteral("▸");
        painter->drawText(card.expanderRect, Qt::AlignVCenter | Qt::AlignRight,
                          QStringLiteral("%1 %2").arg(glyph).arg(count));
        painter->setFont(chrome.font);
    }

    painter->restore();

    // Line 3: the chips. A thread card draws its own tags; a reply draws only
    // the tags its thread does not already carry, so the thread's chips are
    // not repeated down the whole expansion.
    const bool isMessage =
        index.data(ThreadListModel::IsMessageRole).toBool();
    const QStringList tags =
        index.data(isMessage ? ThreadListModel::MessageOwnTagsRole
                             : ThreadListModel::PillTagsRole)
            .toStringList();
    const QVariantList colours =
        index.data(isMessage ? ThreadListModel::MessageOwnColoursRole
                             : ThreadListModel::PillColoursRole)
            .toList();

    const QFont chipFont = CardLayout::smallFont(chrome.font);
    const QFontMetrics chipMetrics(chipFont);
    painter->save();
    painter->setFont(chipFont);
    int x = card.tagRect.left();
    for (int i = 0; i < tags.size(); ++i) {
        const QSize size = TagChip::sizeFor(chipMetrics, tags.at(i));
        if (x + size.width() > card.tagRect.right())
            break;  // Out of room; a clipped chip reads as a rendering fault.
        const QColor colour = i < colours.size()
                                  ? colours.at(i).value<QColor>()
                                  : QColor(0x55, 0x55, 0x5f);
        TagChip::paint(painter, QRect(QPoint(x, card.tagRect.top()), size),
                       tags.at(i), colour);
        x += size.width() + TagChip::kSpacing;
    }
    painter->restore();
}
