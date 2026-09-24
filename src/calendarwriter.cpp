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

#include "calendarwriter.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace CalendarWriter {

Result replace(const QString &path, const std::optional<QByteArray> &expected,
               const std::optional<QByteArray> &newText, QString *error)
{
    QFile current(path);
    const bool exists = current.exists();
    if (expected.has_value() != exists)
        return Result::Stale;
    if (exists) {
        if (!current.open(QIODevice::ReadOnly)) {
            *error = current.errorString();
            return Result::IoError;
        }
        if (current.readAll() != *expected)
            return Result::Stale;
        current.close();
    }

    if (!newText) {
        if (!QFile::remove(path)) {
            *error = QFile(path).errorString();
            return Result::IoError;
        }
        return Result::Ok;
    }

    // QSaveFile is the platform's atomic write: a temporary in the same
    // directory, renamed on commit(). Its temporary is named "<name>.XXXXXX",
    // which does not end in .ics, so vdirsyncer never lists it.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(*newText) != newText->size()
        || !file.commit()) {
        *error = file.errorString();
        return Result::IoError;
    }
    return Result::Ok;
}

}  // namespace CalendarWriter
