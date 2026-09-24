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

#include <QDate>
#include <QPoint>
#include <QRect>

/// Where everything in the month grid goes, with no painter and no widget.
///
/// The CardLayout precedent: a geometric claim is tested as a function call,
/// because AGENTS.md records how rendering probes lie. Six rows of seven
/// always, so the grid does not jump in height between months.
class MonthLayout
{
public:
    static constexpr int kCells = 42;

    MonthLayout(const QRect &area, int year, int month, Qt::DayOfWeek firstDay,
                int dayNumberHeight, int chipHeight);

    static QDate gridStart(int year, int month, Qt::DayOfWeek firstDay);

    QDate dateAt(int cell) const;
    int cellOf(const QDate &date) const;  ///< -1 when not on the grid.
    QRect cellRect(int cell) const;
    int cellAt(const QPoint &point) const;  ///< -1 outside the grid.

    /// How many chip slots fit under a cell's day number.
    int capacity() const;
    /// How many of `total` chips are drawn; when fewer than `total`, the
    /// next slot holds "+N more".
    int visibleChips(int total) const;
    QRect chipRect(int cell, int slot) const;

private:
    QRect m_area;
    QDate m_start;
    int m_dayNumberHeight;
    int m_chipHeight;
};
