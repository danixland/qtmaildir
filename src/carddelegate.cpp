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
#include "marks.h"
#include "tagchip.h"
#include "threadlistmodel.h"

#include <QApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QPainter>
#include <QRegularExpression>
#include <QStyle>

namespace {

/// How much of a full-size chip's padding a SIBLING chip keeps.
///
/// Matched to CardLayout::siblingFont()'s own scale, so the chip shrinks as a
/// whole rather than keeping full-size margins around smaller letters.
constexpr qreal kSiblingPaddingScale = 0.70;

CardLayout::Input inputFor(const QModelIndex &index)
{
    CardLayout::Input in;
    in.isMessage = index.data(ThreadListModel::IsMessageRole).toBool();
    in.depth = index.data(ThreadListModel::MessageDepthRole).toInt();
    in.replyCount = index.data(ThreadListModel::ReplyCountRole).toInt();
    in.dateFormat = index.data(ThreadListModel::DateFormatRole).toString();

    // Item 70's marks. The layout reserves a rect for each, so these have to
    // reach it: a mark drawn without its rect reserved lands on top of the
    // subject rather than beside it.
    in.flagged = index.data(ThreadListModel::IsFlaggedRole).toBool();
    in.hasAttachment = index.data(ThreadListModel::HasAttachmentRole).toBool();
    in.passed = index.data(ThreadListModel::IsPassedRole).toBool();
    in.replied = index.data(ThreadListModel::IsRepliedRole).toBool();
    return in;
}

}  // namespace

QRect CardDelegate::expanderRectFor(const QStyleOptionViewItem &option,
                                    const QModelIndex &index)
{
    return CardLayout::compute(inputFor(index), option.rect, option.font)
        .expanderRect;
}

QSize CardDelegate::chipSize(const QFontMetrics &metrics, const QString &text,
                             bool own)
{
    // The padding shrinks with the font for a sibling chip. Left fixed it is
    // 18px around roughly 30px of text, so the chip stays wide while its
    // letters shrink and the tier reads as "same chip, smaller text".
    return own ? TagChip::sizeFor(metrics, text)
               : TagChip::sizeFor(metrics, text, kSiblingPaddingScale);
}

QColor CardDelegate::mutedChipColour(const QColor &chipColour)
{
    if (!chipColour.isValid())
        return chipColour;

    // Saturation only, and NOT a blend toward the background. The accent bar
    // above records what blending toward Base costs: on a dark theme it lands
    // on the background and the thing disappears. A chip is worse, because its
    // fill also has to carry legible text on top of it.
    //
    // Hue is untouched, so a muted `signed` is still recognisably the same
    // colour as a full-size `signed` elsewhere in the list. Lightness is
    // untouched too, which is what keeps TagColors::textColourOn() picking the
    // same text colour: draining saturation alone moves the fill toward grey
    // without moving it toward either black or white, so contrast is preserved
    // by construction rather than by hoping.
    constexpr float kSaturationScale = 0.45f;

    float h = 0, s = 0, l = 0, a = 0;
    chipColour.getHslF(&h, &s, &l, &a);
    return QColor::fromHslF(h, s * kSaturationScale, l, a);
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
    // The account's own hue, lifted to a floor of saturation and lightness.
    //
    // Blending toward Base was the first mistake and is long gone: a chip's
    // colour is already muted, since it is chosen to carry legible text on top,
    // and three pixels of a muted colour is nothing. Handing the raw colour
    // through was the second: it is better, but the five real accounts are all
    // mid-tone by construction and still read as faint stripes on a dark theme.
    //
    // A FLOOR rather than a repaint. A colour already past it is returned
    // untouched, so a user who deliberately picked something vivid keeps
    // exactly what they picked, and only the muted ones move. Hue is never
    // touched at all, because hue is the entire information the bar carries:
    // shifting it would make a bar stop matching its account's chip and its
    // swatch in the dropdown.
    //
    // The numbers were chosen by rendering all five accounts as 3px bars on
    // both a dark and a light card background and looking. Higher pushed the
    // green toward a neon that no longer matched its own chip; lower left it
    // where it started.
    constexpr float kMinSaturation = 0.65f;
    constexpr float kMinLightness = 0.50f;

    float h = 0, s = 0, l = 0, a = 0;
    accountColour.getHslF(&h, &s, &l, &a);
    return QColor::fromHslF(h, qMax(s, kMinSaturation),
                            qMax(l, kMinLightness), a);
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
    // The same format the layout reserved width from. Reading the role again
    // rather than a second config lookup, so the drawn string and the rect it
    // is drawn into cannot come from different patterns.
    painter->drawText(
        card.dateRect, Qt::AlignVCenter | Qt::AlignRight,
        CardLayout::formatDate(
            date, index.data(ThreadListModel::DateFormatRole).toString()));

    // Line 2: the flag mark, the subject, the attachment mark.
    QString subject = index.data(ThreadListModel::SubjectRole).toString();
    if (index.data(ThreadListModel::IsMessageRole).toBool()) {
        // Every reply repeating "Re: <the thread's subject>" is the visual
        // signature of a table of records, which is what item 53 is about.
        static const QRegularExpression re(
            QStringLiteral("^\\s*(?:[Rr][Ee]\\s*:\\s*)+"));
        subject.remove(re);
    }
    painter->drawText(card.subjectRect, Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(subject, Qt::ElideRight,
                                         card.subjectRect.width()));

    // Item 70's marks, drawn into the rects the layout reserved rather than
    // appended to the subject STRING as glyphs. The colour is the pen's, which
    // is already resolved above against selection and the read/unread
    // foreground, so a mark follows its card's text exactly: white on a
    // selected row, dimmed on a read one.
    const QColor markColour = painter->pen().color();
    const auto drawMark = [&](const QRect &rect, Marks::Mark mark) {
        if (!rect.isEmpty())
            Marks::paint(painter, rect, mark, markColour);
    };
    drawMark(card.flagRect, Marks::Mark::Flagged);
    drawMark(card.attachmentRect, Marks::Mark::Attachment);
    drawMark(card.passedRect, Marks::Mark::Passed);
    drawMark(card.repliedRect, Marks::Mark::Replied);

    // The reply count, which is also the expander, drawn as a PILL.
    //
    // A bare "3" on the card's own background read as an unexplained number
    // beside the subject and gave no hint that it could be clicked. The chip
    // shape says "this is a control", matching the tag chips on line 3, and the
    // word says what the number counts.
    if (!card.expanderRect.isEmpty()) {
        const int count = index.data(ThreadListModel::ReplyCountRole).toInt();
        const QString label = CardLayout::expanderLabel(
            count, option.state & QStyle::State_Open);

        painter->save();
        painter->setFont(CardLayout::smallFont(chrome.font));

        // Blended from Text toward Base rather than taken from a palette ROLE.
        // QPalette::Button is the role this obviously wants and it is
        // #2b2b2b against a Base of #2b2b2b on the user's theme: byte
        // identical, so the pill was invisible. A theme is free to make any two
        // roles equal, and several do; a blend cannot collide with the surface
        // it sits on because it is defined relative to it.
        //
        // Toward Text, so it darkens on a light theme and lightens on a dark
        // one, the same trick replyBackground() and threadLineColour() use.
        const QColor base = option.palette.color(QPalette::Base);
        const QColor text = option.palette.color(QPalette::Text);
        constexpr qreal kFillWeight = 0.18;
        const QColor fill = QColor::fromRgbF(
            text.redF() * kFillWeight + base.redF() * (1.0 - kFillWeight),
            text.greenF() * kFillWeight + base.greenF() * (1.0 - kFillWeight),
            text.blueF() * kFillWeight + base.blueF() * (1.0 - kFillWeight));
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        // Fully rounded ends, the same shape TagChip paints: the radius is half
        // the height, so the pill cannot look like a rectangle with soft corners.
        const qreal radius = card.expanderRect.height() / 2.0;
        painter->drawRoundedRect(card.expanderRect, radius, radius);

        // The pen is restored from the card's own text colour rather than
        // ButtonText, which belongs to the role that just proved unreliable.
        painter->setPen(option.state & QStyle::State_Selected
                            ? option.palette.highlightedText().color()
                            : text);

        // The triangle is a drawn mark since item 70, not a glyph in the label,
        // so the pill lays out its two pieces itself: the mark, a gap, then the
        // count. The layout reserved width for exactly this (ascent + kMarkGap),
        // so the two must agree or the text drifts out of its own background.
        const QFontMetrics pillMetrics(painter->font());
        const int side = pillMetrics.ascent();
        const int textWidth = pillMetrics.horizontalAdvance(label);
        const int contentWidth = side + CardLayout::kMarkGap + textWidth;
        const int left = card.expanderRect.left()
                         + (card.expanderRect.width() - contentWidth) / 2;
        const QRect markRect(left,
                             card.expanderRect.top()
                                 + (card.expanderRect.height() - side) / 2,
                             side, side);
        Marks::paint(painter, markRect,
                     option.state & QStyle::State_Open
                         ? Marks::Mark::ExpanderExpanded
                         : Marks::Mark::ExpanderCollapsed,
                     painter->pen().color());
        painter->drawText(QRect(markRect.right() + 1 + CardLayout::kMarkGap,
                                card.expanderRect.top(), textWidth,
                                card.expanderRect.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, label);
        painter->restore();
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

    // A thread card draws its own tags at full size and the rest of the
    // conversation's smaller and muted (item 111). The count is where the two
    // tiers meet; a message row has no such split and reports its whole list.
    //
    // Shown rather than dropped, at the user's request: a card sits above a
    // conversation, so what its siblings carry is worth seeing, just not at
    // the same weight. Before the row has been opened everything is in the own
    // tier, so a chip SHRINKS when the split becomes known and none vanishes.
    const int ownCount =
        isMessage ? tags.size()
                  : index.data(ThreadListModel::PillOwnCountRole).toInt();

    const QFont ownFont = CardLayout::smallFont(chrome.font);
    const QFont siblingFont = CardLayout::siblingFont(chrome.font);
    const QFontMetrics ownMetrics(ownFont);
    const QFontMetrics siblingMetrics(siblingFont);

    painter->save();
    int x = card.tagRect.left();
    for (int i = 0; i < tags.size(); ++i) {
        const bool own = i < ownCount;
        const QFontMetrics &metrics = own ? ownMetrics : siblingMetrics;

        const QSize size = chipSize(metrics, tags.at(i), own);
        if (x + size.width() > card.tagRect.right())
            break;  // Out of room; a clipped chip reads as a rendering fault.

        QColor colour = i < colours.size() ? colours.at(i).value<QColor>()
                                           : QColor(0x55, 0x55, 0x5f);
        if (!own)
            colour = mutedChipColour(colour);

        // Bottom-aligned, so a smaller chip sits on the same baseline as its
        // neighbours rather than floating in the middle of the row. Top
        // alignment would step the tier down and read as a layout fault.
        const int top = card.tagRect.top()
                        + (ownMetrics.height() - metrics.height());

        painter->setFont(own ? ownFont : siblingFont);
        TagChip::paint(painter, QRect(QPoint(x, top), size), tags.at(i),
                       colour);
        x += size.width() + TagChip::kSpacing;
    }
    painter->restore();
}
