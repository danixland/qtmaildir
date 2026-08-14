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

#include <QRect>
#include <QStringList>
#include <QWidget>

class TagColors;
class QContextMenuEvent;

/// One row of tag chips under the message pane.
///
/// A single row by design: the message area must not shift as you move between
/// threads with different numbers of tags. Whatever does not fit collapses
/// into a trailing "+N" chip whose tooltip names the hidden tags.
class TagStrip : public QWidget
{
    Q_OBJECT
public:
    explicit TagStrip(QWidget *parent = nullptr);

    /// Not owned; must outlive the strip.
    void setTagColors(const TagColors *colours);

    /// Account tags are filtered out: they belong to the thread list chip,
    /// being a different taxonomy from the functional tags shown here.
    void setTags(const QStringList &tags);

    QSize sizeHint() const override;

    /// The tags actually drawn, in order. Exposed for testing the overflow
    /// split without rendering.
    QStringList visibleTags() const { return m_visible; }
    QStringList hiddenTags() const { return m_hidden; }

    /// The rect of the visible chip at `index`, empty when out of range.
    ///
    /// The SAME function paintEvent lays out from, so what is drawn and what
    /// is clickable cannot drift. `CardDelegate::expanderRectFor` exists for
    /// this reason and this follows it.
    QRect chipRectAt(int index) const;

    /// The tag under `point`, empty when the point is on no chip.
    ///
    /// The trailing "+N" chip yields an empty string: it stands for a list of
    /// tags rather than for one, so there is nothing a search could name.
    QString chipAt(const QPoint &point) const;

signals:
    /// A visible chip was right-clicked. `globalPos` is where to pop a menu.
    ///
    /// The strip does not build the menu itself: what a tag can do belongs to
    /// the window, which owns the query bar and the actions.
    void tagContextMenuRequested(const QString &tag, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    /// Recomputes the visible/hidden split for the current width.
    void relayout();

    QStringList m_tags;     ///< Functional tags only, account ones removed.
    QStringList m_visible;
    QStringList m_hidden;
    const TagColors *m_tagColors = nullptr;
};
