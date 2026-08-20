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

#include "busyindicator.h"

BusyIndicator::BusyIndicator(QWidget *parent)
    : QProgressBar(parent)
{
    // Starts indeterminate and hidden, which is what both consumers want
    // before anything is happening. A caller that wants a fraction calls
    // setProgress() and never sees this range.
    setRange(0, 0);

    // The percentage would be meaningless in the indeterminate mode and is
    // redundant beside the phase text in the determinate one, where the popup
    // says what is being waited for.
    setTextVisible(false);
    hide();
}

void BusyIndicator::setBusy(bool busy)
{
    setRange(0, 0);
    setVisible(busy);
}

void BusyIndicator::setProgress(int value, int total)
{
    if (total <= 0) {
        // Not an empty determinate bar: setRange(0, 0) IS the indeterminate
        // range, so falling through here would hand the caller an animating
        // bar while it believed it had shown a fraction.
        setBusy(true);
        return;
    }

    setRange(0, total);
    setValue(qBound(0, value, total));
    show();
}

bool BusyIndicator::isBusy() const
{
    return isVisible();
}

bool BusyIndicator::isDeterminate() const
{
    return maximum() > minimum();
}
