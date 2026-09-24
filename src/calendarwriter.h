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

#include <QByteArray>
#include <QString>

#include <optional>

/// Every write to the calendar vdir goes through here.
///
/// One function covers create, edit, delete and every undo of those, because
/// each is the same act: the file must currently be X, and must become Y,
/// where either may be "absent". vdirsyncer writes the same files from cron,
/// so "must currently be X" is the stale check that stops a save from
/// silently discarding what a sync just pulled.
namespace CalendarWriter {

enum class Result { Ok, Stale, IoError };

/// `expected` nullopt: the file must not exist. `newText` nullopt: delete it.
/// A write goes to a hidden temporary in the same directory and is renamed
/// into place, so vdirsyncer, which lists only *.ics, never reads half a file.
Result replace(const QString &path, const std::optional<QByteArray> &expected,
               const std::optional<QByteArray> &newText, QString *error);

}  // namespace CalendarWriter
