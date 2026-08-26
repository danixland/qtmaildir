/*
 * qtmaildir - a Qt6 GUI for a local notmuch-indexed Maildir
 * Copyright (C) 2026 Danilo M. <danix@danix.xyz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "marks.h"

#include <QHash>
#include <QPainter>
#include <QRect>
#include <QSvgRenderer>

namespace Marks {

QByteArray svg(Mark mark)
{
    // Compiled in rather than loaded from a .qrc, for the linker reason given
    // in marks.h. Generated from assets/icons/marks/*.svg, which stay the
    // editable originals: change the asset, regenerate, do not hand-edit here.
    switch (mark) {
    case Mark::Attachment:
        return QByteArray(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
            "viewBox=\"0 0 16 16\"> <path fill=\"currentColor\" d=\"M 10.4,1.1 C "
            "8.75,1.1 7.4,2.45 7.4,4.1 V 11.6 a 1.1,1.1 0 0 0 2.2,0 V 4.6 a "
            "0.85,0.85 0 0 1 1.7,0 V 11.7 a 2.75,2.75 0 0 1 -5.5,0 V 4.35 a 1.1,1.1 "
            "0 0 0 -2.2,0 V 11.7 C 3.6,14.4 5.8,16 8.35,16 10.9,16 13.1,14.4 "
            "13.1,11.7 V 4.1 C 13.1,2.45 11.9,1.1 10.4,1.1 Z\"/> </svg>");
    case Mark::Flagged:
        return QByteArray(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
            "viewBox=\"0 0 16 16\"> <path fill=\"currentColor\" d=\"M 8.00,1.05 "
            "9.73,5.96 14.94,6.09 10.81,9.26 12.29,14.26 8.00,11.30 3.71,14.26 "
            "5.19,9.26 1.06,6.09 6.27,5.96 Z\"/> </svg>");
    case Mark::Passed:
        return QByteArray(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
            "viewBox=\"0 0 16 16\"> <path fill=\"currentColor\" d=\"M 9.2,2.2 V 5.0 "
            "H 7.3 C 4.1,5.0 1.8,7.4 1.8,10.8 V 13.8 a 0.9,0.9 0 0 0 1.75,0.28 C "
            "4.2,12.1 5.6,10.9 7.3,10.9 H 9.2 V 13.7 L 15.0,7.95 Z\"/> </svg>");
    case Mark::Replied:
        return QByteArray(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
            "viewBox=\"0 0 16 16\"> <path fill=\"currentColor\" d=\"M 6.8,2.2 V 5.0 "
            "H 8.7 C 11.9,5.0 14.2,7.4 14.2,10.8 V 13.8 a 0.9,0.9 0 0 1 -1.75,0.28 C "
            "11.8,12.1 10.4,10.9 8.7,10.9 H 6.8 V 13.7 L 1.0,7.95 Z\"/> </svg>");
    case Mark::ReceivedForward:
        return QByteArray(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
            "viewBox=\"0 0 16 16\"> <path fill=\"currentColor\" d=\"M 2.0,2.6 a "
            "0.95,0.95 0 0 1 1.9,0 V 6.4 C 3.9,8.1 5.2,9.4 6.9,9.4 H 9.6 V 6.6 L "
            "15.0,11.0 L 9.6,15.4 V 12.6 H 6.9 C 3.5,12.6 2.0,10.3 2.0,7.4 Z\"/> "
            "</svg>");
    case Mark::ExpanderCollapsed:
        return QByteArray(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
            "viewBox=\"0 0 16 16\"> <path fill=\"currentColor\" d=\"M 5.38,2.38 "
            "12.25,8 5.38,13.62 Z\"/> </svg>");
    case Mark::ExpanderExpanded:
        return QByteArray(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\" "
            "viewBox=\"0 0 16 16\"> <path fill=\"currentColor\" d=\"M 2.38,5.38 "
            "13.62,5.38 8,12.25 Z\"/> </svg>");    }
    return {};
}

namespace {

/// Key for the pixmap cache. The colour belongs in it because the mark is
/// recoloured per palette, and the ratio because a pixmap rendered for a 1x
/// screen is blurry on a 2x one.
struct CacheKey
{
    Mark mark;
    int width;
    int height;
    QRgb color;
    int ratio;   ///< devicePixelRatio scaled by 100, so it can be hashed.

    bool operator==(const CacheKey &other) const
    {
        return mark == other.mark && width == other.width
               && height == other.height && color == other.color
               && ratio == other.ratio;
    }
};

size_t qHash(const CacheKey &key, size_t seed = 0)
{
    return qHashMulti(seed, static_cast<int>(key.mark), key.width, key.height,
                      key.color, key.ratio);
}

}   // namespace

QPixmap pixmap(Mark mark, const QSize &size, const QColor &color,
               qreal devicePixelRatio)
{
    if (size.isEmpty() || !color.isValid())
        return {};

    static QHash<CacheKey, QPixmap> cache;

    const CacheKey key{ mark, size.width(), size.height(), color.rgba(),
                        qRound(devicePixelRatio * 100) };
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd())
        return *cached;

    QPixmap pm(size * devicePixelRatio);
    pm.setDevicePixelRatio(devicePixelRatio);
    pm.fill(Qt::transparent);

    {
        QSvgRenderer renderer(svg(mark));
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(size)));

        // The payloads paint with fill="currentColor", which QSvgRenderer does
        // not resolve: it renders them black. SourceIn keeps the alpha the
        // shape just produced and replaces the colour, which is what makes one
        // asset serve both a light and a dark palette.
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(QRect(QPoint(0, 0), size), color);
    }

    // Unbounded in principle, bounded in practice: the marks are six, the sizes
    // come from a handful of font heights, and the colours from the palette.
    cache.insert(key, pm);
    return pm;
}

void paint(QPainter *painter, const QRect &rect, Mark mark, const QColor &color)
{
    if (!painter || rect.isEmpty())
        return;

    // Square, sized to the shorter side, so a mark never stretches. The rects
    // CardLayout reserves are square already; the message pane's are not
    // necessarily.
    const int side = qMin(rect.width(), rect.height());
    const QSize size(side, side);
    const QPixmap pm = pixmap(mark, size, color,
                              painter->device()->devicePixelRatioF());
    if (pm.isNull())
        return;

    const QPoint at(rect.left() + (rect.width() - side) / 2,
                    rect.top() + (rect.height() - side) / 2);
    painter->drawPixmap(at, pm);
}

}   // namespace Marks
