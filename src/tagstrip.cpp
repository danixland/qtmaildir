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

#include "tagstrip.h"

#include <QFontMetrics>
#include <QPainter>

#include "tagchip.h"
#include "tagcolors.h"

namespace {

/// Text of the chip standing in for tags that did not fit.
QString overflowText(int count)
{
    return QStringLiteral("+%1").arg(count);
}

}  // namespace

TagStrip::TagStrip(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void TagStrip::setTagColors(const TagColors *colours)
{
    m_tagColors = colours;
    update();
}

void TagStrip::setTags(const QStringList &tags)
{
    m_tags.clear();
    for (const QString &tag : tags) {
        // The account tag is shown as a chip in the thread list instead: it
        // says which mailbox the thread came from, not what state it is in.
        if (!TagColors::isAccountTag(tag))
            m_tags.append(tag);
    }
    m_tags.sort();

    relayout();
    setVisible(!m_tags.isEmpty());
    update();
}

void TagStrip::relayout()
{
    m_visible.clear();
    m_hidden.clear();
    if (m_tags.isEmpty())
        return;

    const QFontMetrics metrics(font());
    // Reserve room for the overflow chip up front. Sizing it for the worst
    // case avoids the loop having to back out a chip it already placed.
    const int overflowWidth =
        TagChip::sizeFor(metrics, overflowText(m_tags.size())).width()
        + TagChip::kSpacing;

    int used = 0;
    for (int i = 0; i < m_tags.size(); ++i) {
        const int chipWidth =
            TagChip::sizeFor(metrics, m_tags.at(i)).width() + TagChip::kSpacing;
        const bool isLast = (i == m_tags.size() - 1);
        // Every chip but the last must also leave room for the overflow chip,
        // since anything after it will be hidden.
        const int needed = used + chipWidth + (isLast ? 0 : overflowWidth);
        if (needed > width() && !m_visible.isEmpty()) {
            m_hidden = m_tags.mid(i);
            break;
        }
        m_visible.append(m_tags.at(i));
        used += chipWidth;
    }

    setToolTip(m_hidden.isEmpty() ? QString()
                                  : m_hidden.join(QStringLiteral(", ")));
}

QSize TagStrip::sizeHint() const
{
    const QFontMetrics metrics(font());
    return QSize(0, metrics.height() + TagChip::kPaddingY * 2
                        + TagChip::kSpacing * 2);
}

void TagStrip::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void TagStrip::paintEvent(QPaintEvent *)
{
    if (m_visible.isEmpty())
        return;

    QPainter painter(this);
    const QFontMetrics metrics(font());
    const int top = (height() - (metrics.height() + TagChip::kPaddingY * 2)) / 2;

    int x = 0;
    for (const QString &tag : m_visible) {
        const QSize size = TagChip::sizeFor(metrics, tag);
        const QColor colour = m_tagColors ? m_tagColors->colourFor(tag)
                                          : TagColors().colourFor(tag);
        TagChip::paint(&painter, QRect(QPoint(x, top), size), tag, colour);
        x += size.width() + TagChip::kSpacing;
    }

    if (!m_hidden.isEmpty()) {
        const QString text = overflowText(m_hidden.size());
        const QSize size = TagChip::sizeFor(metrics, text);
        TagChip::paint(&painter, QRect(QPoint(x, top), size), text,
                       QColor(0x44, 0x44, 0x4c));
    }
}
