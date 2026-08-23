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

#include "draftstore.h"

#include "maildirname.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSaveFile>

DraftStore::Result DraftStore::write(const QString &folderPath,
                                     const QByteArray &bytes,
                                     const QString &flags,
                                     const QString &previousPath)
{
    Result result;

    if (folderPath.isEmpty()) {
        result.error = QObject::tr("No folder was configured to write to.");
        return result;
    }

    // cur/, never new/. A file in new/ is re-announced as fresh mail by every
    // reader of the Maildir, so an autosaved draft would arrive as a new
    // message on every revision.
    const QString curPath = folderPath + QStringLiteral("/cur");
    if (!QDir().mkpath(curPath)) {
        result.error = QObject::tr("Cannot create the folder %1.").arg(curPath);
        return result;
    }

    // A FRESH name, with no previous one to preserve flags from: a draft is
    // newly composed, and MessageBuilder's bytes carry no filename. The flags
    // are appended here instead.
    const QString name = MaildirName::fresh(QString())
                         + QStringLiteral(":2,") + flags;
    const QString target = curPath + QLatin1Char('/') + name;

    // QSaveFile: writes to a temporary and renames into place, so a reader
    // never sees a half-written message. mbsync and notmuch both watch this
    // directory.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = QObject::tr("Cannot write to %1: %2")
                           .arg(target, file.errorString());
        return result;
    }

    if (file.write(bytes) != bytes.size() || !file.commit()) {
        result.error = QObject::tr("Cannot write to %1: %2")
                           .arg(target, file.errorString());
        return result;
    }

    result.path = target;

    // AFTER the new file is safely in place, never before: unlinking first
    // would lose the draft entirely if the write then failed. A failure to
    // remove the old revision is not reported as a failure of the write,
    // because the new revision IS on disk; the cost is one stale file.
    if (!previousPath.isEmpty() && previousPath != target)
        QFile::remove(previousPath);

    return result;
}
