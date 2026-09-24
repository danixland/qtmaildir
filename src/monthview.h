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

#include "caltypes.h"
#include "monthlayout.h"

#include <QWidget>

/// The month grid. Paints a MonthLayout; knows nothing of libical, files or
/// CalEvent, only the CalendarItems it is handed.
class MonthView : public QWidget
{
    Q_OBJECT

public:
    explicit MonthView(QWidget *parent = nullptr);

    void setMonth(int year, int month);
    void setItems(const QList<CalendarItem> &items);
    /// Index into the items, or -1. Drawn with an outline.
    void setSelected(int item);

    /// The first and one-past-last day on the grid, for the caller's query.
    QDate firstDay() const;
    QDate lastDay() const;

signals:
    void itemClicked(int item);
    void dayDoubleClicked(const QDate &day);
    void moreClicked(const QDate &day);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    MonthLayout layout() const;
    /// Items on each cell, in start order; a multi-day item is on every cell
    /// it covers (spec, Month view).
    QList<QList<int>> itemsPerCell(const MonthLayout &layout) const;

    int m_year;
    int m_month;
    QList<CalendarItem> m_items;
    int m_selected = -1;
};
