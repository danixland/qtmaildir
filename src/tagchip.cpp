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
    // Radius from the chip's own height rather than the fixed kRadius: a 3px
    // corner on a 17px chip reads as a slightly-softened rectangle, which is
    // hard to tell from the square cells of the columns behind it. Half the
    // height gives fully rounded ends, so a chip reads as an object sitting on
    // the row instead of as another compartment of it.
    const qreal radius = rect.height() / 2.0;
    painter->drawRoundedRect(rect, radius, radius);

    painter->setPen(TagColors::textColourOn(background));
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
}

}  // namespace TagChip

int SubjectDelegate::subjectBandHeight(const QStyleOptionViewItem &option)
{
    return QFontMetrics(option.font).height();
}

QFont SubjectDelegate::pillFont(const QFont &rowFont)
{
    QFont font = rowFont;

    // Two points down, floored. One point was measured to change nothing at a
    // 12pt desktop font: 12 and 11 both render 17px tall, so the pills came
    // out the same size as the subject and read as competing content rather
    // than as annotation.
    //
    // pointSize() is -1 when the font was specified in pixels, which
    // subtracting from would be nonsense, hence the two branches.
    if (rowFont.pointSize() > 0)
        font.setPointSize(qMax(6, rowFont.pointSize() - 2));
    else if (rowFont.pixelSize() > 0)
        font.setPixelSize(qMax(8, rowFont.pixelSize() - 3));

    return font;
}

int SubjectDelegate::rowHeightFor(const QFont &rowFont)
{
    // The text band uses the ROW's font and the strip its own smaller one.
    // Measuring both with one font is what put the pills over the date text.
    const QFontMetrics rowMetrics(rowFont);
    const QFontMetrics pillMetrics(pillFont(rowFont));

    return rowMetrics.height()
         + TagChip::sizeFor(pillMetrics, QStringLiteral("x")).height()
         + kRowPadding * 2 + TagChip::kSpacing;
}

void RowStyleDelegate::initStyleOption(QStyleOptionViewItem *option,
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

    // Top-aligned and on one line, matching the subject beside them.
    //
    // The row is tall enough for a pill strip under the text, and Qt centres a
    // cell's text in the whole rectangle by default: date and sender floated
    // into the middle while the subject sat at the top, so the three did not
    // share a baseline. Confining the rectangle to the text band puts them all
    // on one.
    //
    // Wrapping matters more than it looks. A long sender ran to a second line,
    // which reached down into the strip's band and collided with the pills; a
    // cell cannot know they are there, since the view paints them afterwards.
    // Eliding keeps every row's text inside its own band whatever it holds.
    // Top of the row rather than centre of it, so the alignment is expressed
    // without shrinking the rectangle: the rect is also what the background
    // and selection fill are drawn into, and clipping it to the text band
    // would leave the highlight covering only the upper part of the row.
    option->features &= ~QStyleOptionViewItem::WrapText;
    option->textElideMode = Qt::ElideRight;

    // The marker columns keep their centring. Their glyphs are the row's
    // symbols rather than its text, so aligning them with the subject's
    // baseline would strand them at the top of a tall row with the pill strip
    // empty beneath; centred, they read as marking the whole row.
    const bool marker = index.column() == ThreadListModel::AttachmentColumn
                     || index.column() == ThreadListModel::FlagColumn;
    option->displayAlignment = marker
        ? Qt::AlignCenter
        : (Qt::AlignLeft | Qt::AlignTop);
}

void SubjectDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    // AccountLabelRole is a property of the ROW, not of a cell, so this
    // delegate must only ever be installed on the subject column. Installed
    // view-wide it draws the account chip into every column, which is exactly
    // what happened when that was tried.
    Q_ASSERT(index.column() == ThreadListModel::SubjectColumn);

    const QString account =
        index.data(ThreadListModel::AccountLabelRole).toString();
    if (account.isEmpty()) {
        // No chip to draw, so the base class renders the text, confined to the
        // upper band: the lower one belongs to the row-wide pill strip that
        // ThreadListView paints after every cell.
        QStyleOptionViewItem chrome = option;
        initStyleOption(&chrome, index);
        chrome.rect.setHeight(subjectBandHeight(option));
        QStyledItemDelegate::paint(painter, chrome, index);

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

    // The subject and its chip occupy the upper band; ThreadListView paints
    // the pill strip across the lower one. Centring the chip in the whole row
    // would leave it floating beside that gap rather than beside its text.
    const int textBandHeight = subjectBandHeight(option);
    const int textTop = option.rect.top() + kRowPadding;

    const QRect chipRect(option.rect.left() + TagChip::kSpacing,
                         textTop + (textBandHeight - chipSize.height()) / 2,
                         chipSize.width(), chipSize.height());

    const QColor colour =
        index.data(ThreadListModel::AccountColourRole).value<QColor>();
    TagChip::paint(painter, chipRect, account,
                   colour.isValid() ? colour : QColor(0x55, 0x55, 0x5f));

    // The subject follows the chip, elided so a long one cannot overflow.
    QRect textRect = option.rect;
    textRect.setLeft(chipRect.right() + TagChip::kSpacing * 2);
    textRect.setTop(textTop);
    textRect.setHeight(textBandHeight);
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

    // Height comes from rowHeightFor(), applied by the view to every row at
    // once. A QTableView takes ONE height per row, so a hint returned here
    // would only win if the view happened to ask this column, and this
    // delegate is on the subject column alone.
    size.setHeight(rowHeightFor(option.font));
    return size;
}
