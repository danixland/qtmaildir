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

#include "threadlistview.h"

#include "carddelegate.h"
#include "threadlistmodel.h"

#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QWheelEvent>

void ThreadListView::mousePressEvent(QMouseEvent *event)
{
    const QModelIndex index = indexAt(event->pos());

    // The message count IS the expander. Anything outside its rect selects the
    // card and opens it, which is what the rest of the card is for.
    if (event->button() == Qt::LeftButton && index.isValid()
        && index.data(ThreadListModel::MessageCountRole).toInt() > 0) {

        QStyleOptionViewItem option;
        initViewItemOption(&option);
        option.rect = visualRect(index);
        // State_Open decides which way the glyph points, and the rect is the
        // same either way, but pass it so the layout sees the true state.
        if (isExpanded(index))
            option.state |= QStyle::State_Open;

        if (CardDelegate::expanderRectFor(option, index)
                .contains(event->pos())) {
            setExpanded(index, !isExpanded(index));
            // Swallowed, so expanding does not also load the thread into the
            // message pane: it is a request to see the thread's shape, not to
            // read it.
            event->accept();
            return;
        }
    }

    QTreeView::mousePressEvent(event);
}

void ThreadListView::wheelEvent(QWheelEvent *event)
{
    const int notches = event->angleDelta().y() / QWheelEvent::DefaultDeltasPerStep;
    if (!event->pixelDelta().isNull() || event->modifiers() != Qt::NoModifier
        || notches == 0) {
        // ponytail: a high-resolution wheel sending less than one notch per
        // event falls through to the plain per-pixel scroll, unanimated.
        QTreeView::wheelEvent(event);
        return;
    }

    QScrollBar *bar = verticalScrollBar();
    if (!m_glide) {
        m_glide = new QPropertyAnimation(bar, "value", this);
        m_glide->setDuration(160);
        m_glide->setEasingCurve(QEasingCurve::OutCubic);
    }

    const bool gliding = m_glide->state() == QAbstractAnimation::Running;
    const int from = gliding ? m_glide->endValue().toInt() : bar->value();
    const int to = qBound(bar->minimum(), from - notches * bar->singleStep(),
                          bar->maximum());

    m_glide->stop();
    m_glide->setStartValue(bar->value());
    m_glide->setEndValue(to);
    m_glide->start();
    event->accept();
}
