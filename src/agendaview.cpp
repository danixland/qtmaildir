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

#include "agendaview.h"

#include <QIcon>
#include <QLocale>
#include <QPainter>
#include <QPixmap>

namespace {
constexpr int kItemRole = Qt::UserRole;      ///< Index into the items, or -1 for a header.
constexpr int kDayRole = Qt::UserRole + 1;   ///< The header's date.
}

AgendaView::AgendaView(QWidget *parent) : QListWidget(parent)
{
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *row) {
        const int item = row->data(kItemRole).toInt();
        if (item >= 0)
            emit itemClicked(item);
    });
}

void AgendaView::setItems(const QList<CalendarItem> &items)
{
    clear();
    const QLocale locale;
    QDate current;
    for (int i = 0; i < items.size(); ++i) {
        const CalendarItem &item = items[i];
        // A blank item is a placeholder the window leaves so every row's
        // index stays the same as the month grid's: skipped, not shown.
        if (item.title.isEmpty())
            continue;
        const QDate day = item.occurrence.start.toLocalTime().date();
        if (day != current) {
            current = day;
            auto *header = new QListWidgetItem(locale.toString(day, QLocale::LongFormat), this);
            QFont bold = header->font();
            bold.setBold(true);
            header->setFont(bold);
            header->setFlags(Qt::ItemIsEnabled);  // not selectable
            header->setData(kItemRole, -1);
            header->setData(kDayRole, day);
        }
        const QString when = item.occurrence.allDay
            ? tr("all day")
            : tr("%1 - %2").arg(locale.toString(item.occurrence.start.toLocalTime().time(), QLocale::ShortFormat),
                                locale.toString(item.occurrence.end.toLocalTime().time(), QLocale::ShortFormat));
        QPixmap swatch(10, 10);
        swatch.fill(Qt::transparent);
        QPainter(&swatch).fillRect(swatch.rect(), item.color);
        auto *row = new QListWidgetItem(QIcon(swatch), when + QStringLiteral("    ") + item.title, this);
        row->setData(kItemRole, i);
    }
}

void AgendaView::setSelected(int item)
{
    for (int r = 0; r < count(); ++r) {
        if (this->item(r)->data(kItemRole).toInt() == item) {
            setCurrentRow(r);
            return;
        }
    }
    clearSelection();
}

void AgendaView::scrollToDay(const QDate &day)
{
    for (int r = 0; r < count(); ++r) {
        const QDate header = item(r)->data(kDayRole).toDate();
        if (header.isValid() && header >= day) {
            scrollToItem(item(r), QAbstractItemView::PositionAtTop);
            return;
        }
    }
}
