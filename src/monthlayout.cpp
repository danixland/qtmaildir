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

#include "monthlayout.h"

MonthLayout::MonthLayout(const QRect &area, int year, int month, Qt::DayOfWeek firstDay,
                         int dayNumberHeight, int chipHeight)
    : m_area(area), m_start(gridStart(year, month, firstDay)),
      m_dayNumberHeight(dayNumberHeight), m_chipHeight(chipHeight)
{
}

QDate MonthLayout::gridStart(int year, int month, Qt::DayOfWeek firstDay)
{
    const QDate first(year, month, 1);
    const int back = (first.dayOfWeek() - firstDay + 7) % 7;
    return first.addDays(-back);
}

QDate MonthLayout::dateAt(int cell) const { return m_start.addDays(cell); }

int MonthLayout::cellOf(const QDate &date) const
{
    const qint64 cell = m_start.daysTo(date);
    return cell >= 0 && cell < kCells ? int(cell) : -1;
}

QRect MonthLayout::cellRect(int cell) const
{
    // Edges computed from the area by proportion, so the rounding remainder
    // is spread across cells and the last edge lands exactly on the area's.
    const int col = cell % 7, row = cell / 7;
    const int left = m_area.left() + m_area.width() * col / 7;
    const int right = m_area.left() + m_area.width() * (col + 1) / 7;  // exclusive
    const int top = m_area.top() + m_area.height() * row / 6;
    const int bottom = m_area.top() + m_area.height() * (row + 1) / 6;
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
}

int MonthLayout::cellAt(const QPoint &point) const
{
    if (!m_area.contains(point))
        return -1;
    const int col = (point.x() - m_area.left()) * 7 / m_area.width();
    const int row = (point.y() - m_area.top()) * 6 / m_area.height();
    return row * 7 + col;
}

int MonthLayout::capacity() const
{
    const int height = m_area.height() / 6 - m_dayNumberHeight;
    return m_chipHeight > 0 ? qMax(0, height / m_chipHeight) : 0;
}

int MonthLayout::visibleChips(int total) const
{
    const int cap = capacity();
    return total <= cap ? total : qMax(0, cap - 1);
}

QRect MonthLayout::chipRect(int cell, int slot) const
{
    const QRect c = cellRect(cell);
    return QRect(c.left() + 2, c.top() + m_dayNumberHeight + slot * m_chipHeight,
                 c.width() - 4, m_chipHeight - 1);
}
