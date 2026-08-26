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

#include "avatar.h"

#include <QCryptographicHash>
#include <QLinearGradient>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>

namespace {

QString twoFrom(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() >= 2)
        return trimmed.left(2).toUpper();
    if (trimmed.size() == 1)
        return (trimmed + trimmed).toUpper();
    return QString();
}

} // namespace

namespace Avatar {

QString initialsFor(const QString &displayName, const QString &address,
                    const QString &accountLabel)
{
    const QStringList words = displayName.split(QLatin1Char(' '),
                                                Qt::SkipEmptyParts);
    if (words.size() >= 2) {
        return (words.at(0).left(1) + words.at(1).left(1)).toUpper();
    }
    if (words.size() == 1) {
        const QString one = twoFrom(words.at(0));
        if (!one.isEmpty())
            return one;
    }

    // No usable name. The local part and the domain each give one letter,
    // which never degrades to a single letter the way the local part alone
    // would, and never reads as a truncated word.
    const int at = address.indexOf(QLatin1Char('@'));
    if (at > 0) {
        const QString local = address.left(at).trimmed();
        const QString domain = address.mid(at + 1).trimmed();
        if (!local.isEmpty() && !domain.isEmpty())
            return (local.left(1) + domain.left(1)).toUpper();
    }
    // An address with no `@` is still something to show.
    const QString bare = twoFrom(address);
    if (!bare.isEmpty())
        return bare;

    const QString account = twoFrom(accountLabel);
    if (!account.isEmpty())
        return account;

    // Nothing at all. Two characters regardless, so the shape never breaks.
    return QStringLiteral("??");
}

Fill fillFor(const QString &displayName, bool isBusinessSender)
{
    // The list first: it is the user's explicit override and must beat the
    // heuristic, or a listed sender could never be pinned.
    if (isBusinessSender)
        return Fill::TwoTone;
    return displayName.trimmed().isEmpty() ? Fill::TwoTone : Fill::Identicon;
}

QColor colourFor(const QString &address)
{
    // The same construction TagColors::colourFor() uses for a tag with nothing
    // configured: hashed so it is stable, at a fixed saturation and lightness
    // so it cannot come out neon and cannot lose its contrast with the
    // initials. The lightness differs from that function's deliberately: a
    // chip carries dark text, a squircle carries white.
    const QByteArray digest =
        QCryptographicHash::hash(address.toUtf8(), QCryptographicHash::Md5);
    const int hue = static_cast<quint8>(digest.at(0)) * 360 / 256;
    return QColor::fromHsl(hue, 110, 95);
}

QPixmap pixmapFor(const QString &seed, const QString &initials, Fill fill,
                  int side, const QFont &font)
{
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);

    const QByteArray digest =
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Md5);
    const QColor base = colourFor(seed);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The squircle. A rounded rect at ~30% of the side reads as one without
    // needing a superellipse, and clipping to it means neither fill has to
    // know the shape.
    QPainterPath squircle;
    squircle.addRoundedRect(QRectF(0, 0, side, side), side * 0.3, side * 0.3);
    painter.setClipPath(squircle);

    if (fill == Fill::Identicon) {
        // A 5x5 grid, mirrored about the vertical axis, so only the left
        // three columns come from the hash: 15 cells, one bit each, which is
        // two bytes of the digest. Symmetry is what makes the shape read as a
        // deliberate mark rather than as noise.
        painter.fillRect(QRect(0, 0, side, side), base.darker(220));
        const qreal cell = qreal(side) / 5.0;
        for (int col = 0; col < 3; ++col) {
            for (int row = 0; row < 5; ++row) {
                const int bit = col * 5 + row;
                const bool on =
                    (static_cast<quint8>(digest.at(bit / 8)) >> (bit % 8)) & 1;
                if (!on)
                    continue;
                painter.fillRect(QRectF(col * cell, row * cell, cell, cell),
                                 base);
                const int mirrored = 4 - col;
                painter.fillRect(
                    QRectF(mirrored * cell, row * cell, cell, cell), base);
            }
        }
        // The veil. Without it the initials sit on whatever the pattern
        // happens to do behind them, which is the classic legibility failure
        // this fill invites. Tune the opacity against the real font before
        // calling it done.
        painter.fillRect(QRect(0, 0, side, side), QColor(0, 0, 0, 77));
    } else {
        // Two related hues split at an angle, both from the hash. The field
        // behind the letters stays large and flat, which is the whole reason
        // this fill exists beside the identicon.
        const int angle = static_cast<quint8>(digest.at(1)) * 360 / 256;
        QLineF axis = QLineF::fromPolar(side, angle);
        axis.translate(side / 2.0, side / 2.0);
        QLinearGradient gradient(axis.p2(), axis.p1());
        gradient.setColorAt(0.0, base);
        gradient.setColorAt(0.499, base);
        gradient.setColorAt(0.5, base.darker(135));
        gradient.setColorAt(1.0, base.darker(135));
        painter.fillRect(QRect(0, 0, side, side), gradient);
    }

    // The letters. White with a soft shadow rather than a computed contrast
    // colour: the fills are generated at a fixed lightness precisely so one
    // choice works for all of them.
    QFont letters = font;
    letters.setBold(true);
    letters.setPixelSize(qMax(8, int(side * 0.36)));
    painter.setFont(letters);
    painter.setPen(QColor(0, 0, 0, 120));
    painter.drawText(QRect(1, 1, side, side), Qt::AlignCenter, initials);
    painter.setPen(Qt::white);
    painter.drawText(QRect(0, 0, side, side), Qt::AlignCenter, initials);

    return pixmap;
}

} // namespace Avatar
