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

#include "monthview.h"
#include "monthlayout.h"

#include <QLocale>
#include <QMouseEvent>
#include <QPainter>

MonthView::MonthView(QWidget *parent)
    : QWidget(parent),
      m_year(QDate::currentDate().year()), m_month(QDate::currentDate().month())
{
    setMinimumSize(420, 320);
}

void MonthView::setMonth(int year, int month)
{
    m_year = year;
    m_month = month;
    update();
}

void MonthView::setItems(const QList<CalendarItem> &items)
{
    m_items = items;
    m_selected = -1;
    update();
}

void MonthView::setSelected(int item)
{
    m_selected = item;
    update();
}

MonthLayout MonthView::layout() const
{
    const int line = fontMetrics().height();
    return MonthLayout(rect().adjusted(0, line + 4, 0, 0), m_year, m_month,
                       QLocale().firstDayOfWeek(), line + 2, line + 2);
}

QDate MonthView::firstDay() const { return layout().dateAt(0); }
QDate MonthView::lastDay() const { return layout().dateAt(MonthLayout::kCells - 1).addDays(1); }

QList<QList<int>> MonthView::itemsPerCell(const MonthLayout &l) const
{
    QList<QList<int>> cells(MonthLayout::kCells);
    for (int i = 0; i < m_items.size(); ++i) {
        const Occurrence &o = m_items[i].occurrence;
        QDate day = o.start.toLocalTime().date();
        // The end is exclusive: an event ending at midnight does not touch
        // the day that midnight begins.
        const QDate last = o.end > o.start ? o.end.toLocalTime().addMSecs(-1).date() : day;
        for (; day <= last; day = day.addDays(1)) {
            const int cell = l.cellOf(day);
            if (cell >= 0)
                cells[cell].append(i);
        }
    }
    return cells;
}

void MonthView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    const MonthLayout l = layout();
    const QLocale locale;
    const int line = fontMetrics().height();

    // Weekday header.
    for (int col = 0; col < 7; ++col) {
        const QRect cell = l.cellRect(col);
        const int dow = (locale.firstDayOfWeek() - 1 + col) % 7 + 1;
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(QRect(cell.left(), 0, cell.width(), line + 4), Qt::AlignCenter,
                   locale.dayName(dow, QLocale::ShortFormat));
    }

    const QList<QList<int>> cells = itemsPerCell(l);
    const QDate today = QDate::currentDate();
    for (int c = 0; c < MonthLayout::kCells; ++c) {
        const QRect cell = l.cellRect(c);
        const QDate date = l.dateAt(c);
        p.setPen(palette().color(QPalette::Mid));
        p.drawRect(cell.adjusted(0, 0, -1, -1));

        QRect number(cell.left() + 4, cell.top() + 1,
                     fontMetrics().horizontalAdvance(QStringLiteral("00")) + 8, line);
        if (date == today) {
            p.setPen(Qt::NoPen);
            p.setBrush(palette().highlight());
            p.drawRoundedRect(number, line / 2, line / 2);
            p.setPen(palette().color(QPalette::HighlightedText));
        } else {
            p.setPen(palette().color(date.month() == m_month ? QPalette::Text : QPalette::PlaceholderText));
        }
        p.drawText(number, Qt::AlignCenter, QString::number(date.day()));

        const QList<int> &items = cells[c];
        const int shown = l.visibleChips(items.size());
        for (int slot = 0; slot < shown; ++slot) {
            const CalendarItem &item = m_items[items[slot]];
            const QRect chip = l.chipRect(c, slot);
            if (item.occurrence.allDay) {
                p.setPen(Qt::NoPen);
                p.setBrush(item.color);
                p.drawRoundedRect(chip, 3, 3);
                p.setPen(Qt::white);
                p.drawText(chip.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           fontMetrics().elidedText(item.title, Qt::ElideRight, chip.width() - 6));
            } else {
                const int dot = chip.height() / 2;
                p.setPen(Qt::NoPen);
                p.setBrush(item.color);
                p.drawEllipse(QRect(chip.left() + 2, chip.center().y() - dot / 2, dot, dot));
                p.setPen(palette().color(QPalette::Text));
                const QString text = locale.toString(item.occurrence.start.toLocalTime().time(),
                                                     QLocale::ShortFormat)
                                     + QLatin1Char(' ') + item.title;
                const QRect textRect = chip.adjusted(dot + 6, 0, 0, 0);
                p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                           fontMetrics().elidedText(text, Qt::ElideRight, textRect.width()));
            }
            if (items[slot] == m_selected) {
                p.setPen(QPen(palette().color(QPalette::Highlight), 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(chip, 3, 3);
            }
        }
        if (shown < items.size()) {
            p.setPen(palette().color(QPalette::Link));
            p.drawText(l.chipRect(c, shown).adjusted(4, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       tr("+%n more", nullptr, int(items.size() - shown)));
        }
    }
}

void MonthView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const MonthLayout l = layout();
    const int cell = l.cellAt(event->position().toPoint());
    if (cell < 0)
        return;
    const QList<int> items = itemsPerCell(l)[cell];
    const int shown = l.visibleChips(items.size());
    for (int slot = 0; slot < shown; ++slot) {
        if (l.chipRect(cell, slot).contains(event->position().toPoint())) {
            emit itemClicked(items[slot]);
            return;
        }
    }
    if (shown < items.size() && l.chipRect(cell, shown).contains(event->position().toPoint()))
        emit moreClicked(l.dateAt(cell));
}

void MonthView::mouseDoubleClickEvent(QMouseEvent *event)
{
    const MonthLayout l = layout();
    const int cell = l.cellAt(event->position().toPoint());
    if (cell < 0)
        return;
    // A double-click on a chip, or on the "+N more" line, is two clicks on
    // that thing, not a new event.
    const QList<int> items = itemsPerCell(l)[cell];
    const int shown = l.visibleChips(items.size());
    for (int slot = 0; slot < shown; ++slot)
        if (l.chipRect(cell, slot).contains(event->position().toPoint()))
            return;
    if (shown < items.size() && l.chipRect(cell, shown).contains(event->position().toPoint()))
        return;
    emit dayDoubleClicked(l.dateAt(cell));
}
