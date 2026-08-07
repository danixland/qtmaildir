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

#include "tagchip.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>

#include "tagcolors.h"
#include "threadlistmodel.h"

namespace TagChip {

QSize sizeFor(const QFontMetrics &metrics, const QString &text)
{
    return QSize(metrics.horizontalAdvance(text) + kPaddingX * 2,
                 metrics.height() + kPaddingY * 2);
}

void paint(QPainter *painter, const QRect &rect, const QString &text,
           const QColor &background)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(background);
    painter->drawRoundedRect(rect, kRadius, kRadius);

    painter->setPen(TagColors::textColourOn(background));
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
}

}  // namespace TagChip

void SubjectDelegate::initStyleOption(QStyleOptionViewItem *option,
                                      const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);

    // Qt resolves Qt::ForegroundRole into the palette's Text roles, and its
    // own painting then prefers those over HighlightedText: a model that
    // supplies a foreground wins even on a selected row.
    //
    // That is wrong for the read/unread dimming. A read thread's colour is
    // blended against the UNSELECTED background, so over the selection
    // highlight it lands as grey on purple, near unreadable. The highlight
    // already says "this row", so the dimming can yield to it while selected.
    //
    // Doomed threads are unaffected in practice: their fill is drawn beneath
    // the selection and their white is a contrast requirement, which
    // HighlightedText also satisfies.
    if (option->state & QStyle::State_Selected) {
        const QColor highlighted =
            option->palette.color(QPalette::HighlightedText);
        option->palette.setColor(QPalette::Text, highlighted);
        option->palette.setColor(QPalette::WindowText, highlighted);
    }
}

void SubjectDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    const QString account =
        index.data(ThreadListModel::AccountLabelRole).toString();
    if (account.isEmpty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // Draw the row's own background and selection first, then the chip and the
    // subject on top, so a selected or struck-through row still looks right.
    QStyleOptionViewItem chrome = option;
    initStyleOption(&chrome, index);
    chrome.text.clear();
    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &chrome, painter, widget);

    const QFontMetrics metrics(option.font);
    const QSize chipSize = TagChip::sizeFor(metrics, account);
    const QRect chipRect(option.rect.left() + TagChip::kSpacing,
                         option.rect.top()
                             + (option.rect.height() - chipSize.height()) / 2,
                         chipSize.width(), chipSize.height());

    const QColor colour =
        index.data(ThreadListModel::AccountColourRole).value<QColor>();
    TagChip::paint(painter, chipRect, account,
                   colour.isValid() ? colour : QColor(0x55, 0x55, 0x5f));

    // The subject follows the chip, elided so a long one cannot overflow.
    QRect textRect = option.rect;
    textRect.setLeft(chipRect.right() + TagChip::kSpacing * 2);
    if (textRect.width() <= 0)
        return;

    painter->save();
    // Selection outranks the model's colour, and that order matters. A read
    // thread carries a dimmed foreground blended against the UNSELECTED
    // background, so painting it over the highlight leaves grey-on-purple,
    // which is close to unreadable. The highlight already carries the "this
    // row" signal, so the read/unread distinction can yield to it for as long
    // as the row is selected.
    //
    // A doomed thread is the exception that proves the rule: its white is not
    // a dimming but a contrast requirement against its own fill, and the fill
    // is drawn under the selection too.
    const QVariant foreground = index.data(Qt::ForegroundRole);
    if (option.state & QStyle::State_Selected)
        painter->setPen(option.palette.highlightedText().color());
    else if (foreground.isValid())
        painter->setPen(foreground.value<QBrush>().color());
    else
        painter->setPen(option.palette.text().color());

    // The model's font carries bold for unread and strike-out for deleted.
    // initStyleOption() already resolved it into chrome.font; using it rather
    // than option.font is what keeps those cues on a delegate-drawn subject.
    const QVariant fontData = index.data(Qt::FontRole);
    const QFont rowFont = fontData.isValid() ? fontData.value<QFont>()
                                             : chrome.font;
    painter->setFont(rowFont);
    const QFontMetrics rowMetrics(rowFont);
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                      rowMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                            Qt::ElideRight, textRect.width()));
    painter->restore();
}

QSize SubjectDelegate::sizeHint(const QStyleOptionViewItem &option,
                                const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const QString account =
        index.data(ThreadListModel::AccountLabelRole).toString();
    if (!account.isEmpty()) {
        const QFontMetrics metrics(option.font);
        size.setWidth(size.width() + TagChip::sizeFor(metrics, account).width()
                      + TagChip::kSpacing * 3);
    }
    return size;
}
