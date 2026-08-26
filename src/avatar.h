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

#include <QColor>
#include <QFont>
#include <QPixmap>
#include <QString>

/// A card's sender avatar: which letters it carries and what fills it.
///
/// A NAMESPACE of free functions over values, deliberately, for the reason
/// CardLayout is a struct with no painter: the letters and the fill choice are
/// decisions with right answers, and they must be assertable without a widget,
/// a model or an exposed view. Only pixmapFor() touches a QPainter, and it
/// paints into an image it owns rather than onto a widget.
namespace Avatar
{

/// Which of the two generated fills a sender gets.
enum class Fill
{
    /// A 5x5 symmetric grid from the hash bits, under a darkening veil.
    Identicon,
    /// Two related hues from the hash, split at an angle, initials on a large
    /// flat field.
    TwoTone,
};

/// Always exactly two characters, upper-cased.
///
/// In order: a display name of two or more words gives one letter from each of
/// the first two; a one-word name gives its own first two; a bare address
/// gives the first of the local part and the first of the domain; and with
/// nothing usable, the account's label. The uniform length is the point, so
/// every squircle reads as the same shape.
QString initialsFor(const QString &displayName, const QString &address,
                    const QString &accountLabel);

/// Which fill, given whether the list claims this address as a business one.
///
/// The list wins first, then the presence of a display name. That order is
/// what lets `Ian Farrell <notifications@github.com>` read as a person while
/// a listed address stays a business whatever name it presents.
Fill fillFor(const QString &displayName, bool isBusinessSender);

/// A stable colour for an address. Same input, same colour, always.
///
/// Generated at a FIXED saturation and lightness so the initials keep their
/// contrast in both themes, exactly as TagColors::colourFor() does for a tag
/// with nothing configured.
QColor colourFor(const QString &address);

/// The finished squircle, `side` pixels a side, ready to draw.
///
/// `seed` is what the fill is generated from, normally the sender's address
/// and the account's own address when there is no sender.
QPixmap pixmapFor(const QString &seed, const QString &initials, Fill fill,
                  int side, const QFont &font);

} // namespace Avatar
