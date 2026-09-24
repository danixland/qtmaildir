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

#include <QListWidget>

/// The agenda: the same items as the month grid, as a list under day headers.
class AgendaView : public QListWidget
{
    Q_OBJECT

public:
    explicit AgendaView(QWidget *parent = nullptr);

    void setItems(const QList<CalendarItem> &items);
    void setSelected(int item);
    /// Scrolls so `day`'s header, or the first day after it, is at the top.
    void scrollToDay(const QDate &day);

signals:
    void itemClicked(int item);
};
