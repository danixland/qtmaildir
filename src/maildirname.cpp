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

#include "maildirname.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHostInfo>

namespace MaildirName {

/// A fresh Maildir filename for a message being moved between folders,
/// preserving only its `:2,<flags>` suffix.
///
/// mbsync's manual is explicit about why this exists, under "the more
/// efficient default UID mapping scheme": "it is important that the MUA
/// renames files when moving them between Maildir folders", and "the general
/// expectation is that a completely new filename is generated as if the
/// message was new".
///
/// The `,U=<n>` infix mbsync writes is its per-folder IMAP UID. Carrying it
/// into another folder makes it a claim about a folder the file is no longer
/// in; moving a message out and back then reinserts a UID the server has
/// since reassigned, and mbsync refuses the folder with `Maildir error:
/// duplicate UID`. Measured on real mail, four collisions in one folder from
/// a single move-and-restore.
///
/// The FLAGS are kept, deliberately, and that is not a contradiction of
/// "as if the message was new". They record seen, flagged and replied, and
/// `maildir.synchronize_flags` is true, so notmuch reads them back as tags:
/// dropping them would mark every deleted message unread and lose Important
/// on the way to the trash. Only the unique part is regenerated.
QString fresh(const QString &oldName)
{
    // The `:2,` suffix, when there is one. `info` is everything from the
    // separator on, so an empty-flag `:2,` is preserved as faithfully as
    // `:2,FS`.
    QString info;
    const int sep = oldName.indexOf(QStringLiteral(":2,"));
    if (sep >= 0)
        info = oldName.mid(sep);

    // The conventional left-to-right unique part: time, a per-process counter,
    // the pid, the host. The counter is what makes two messages moved in the
    // same second distinct, which a timestamp alone does not guarantee.
    static quint64 counter = 0;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString host = QHostInfo::localHostName().isEmpty()
                             ? QStringLiteral("localhost")
                             : QHostInfo::localHostName();

    return QStringLiteral("%1.M%2P%3Q%4.%5%6")
        .arg(now)
        .arg(QDateTime::currentMSecsSinceEpoch() % 1000)
        .arg(QCoreApplication::applicationPid())
        .arg(++counter)
        // A `/` or a `:` in a hostname would break the path or the flag
        // separator. Neither is legal in a hostname, so this is belt and
        // braces rather than a known case.
        .arg(QString(host).replace(QLatin1Char('/'), QLatin1Char('_'))
                 .replace(QLatin1Char(':'), QLatin1Char('_')))
        .arg(info);
}

}  // namespace MaildirName
