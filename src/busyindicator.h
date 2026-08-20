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

#include <QProgressBar>

// A progress bar that carries both of the modes this application needs, so the
// two places that show one do not each configure a bare QProgressBar. The
// status bar's sync indicator is indeterminate, because mbsync reports no
// percentage; the composer's send popup drains a determinate bar through its
// undo countdown and then switches THE SAME widget to indeterminate when the
// command starts. That switch is why one class exists rather than two.
//
// Deliberately no label. MainWindow's status text is m_statusLabel, which is
// the window's and carries far more than this widget's state, and the send
// popup's phase text belongs to the popup. A label here would be a third
// owner of status text.
class BusyIndicator : public QProgressBar
{
    Q_OBJECT

public:
    explicit BusyIndicator(QWidget *parent = nullptr);

    // Indeterminate: an animating bar meaning "working, duration unknown".
    // setRange(0, 0) is what Qt reads as indeterminate, and a bar left in that
    // range shows no fraction however many times setValue() is called.
    void setBusy(bool busy);

    // Determinate: a real fraction. Passing a total of 0 or less would be a
    // silent request for the indeterminate range, so it is treated as busy
    // instead of quietly drawing an empty bar forever.
    void setProgress(int value, int total);

    bool isBusy() const;
    bool isDeterminate() const;
};
