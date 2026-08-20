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

#include <QString>

/// Maildir filename generation, shared by every path that writes a message
/// file: NotmuchWorker::moveMessages() and DraftStore.
///
/// A namespace rather than a class; there is no state beyond a counter.
namespace MaildirName {

/// A fresh, unique Maildir filename, preserving \p oldName's flag suffix.
///
/// A FRESH name, never a reuse. mbsync writes a `,U=<n>` infix that is
/// meaningful only within one folder, and carrying it across a folder
/// boundary produced "Maildir error: duplicate UID" on real mail. Only the
/// `:2,` flag suffix is carried, because the flags describe the message
/// rather than its position.
///
/// Pass an empty string for a message that has no previous name, which is
/// what a newly composed draft is.
QString fresh(const QString &oldName);

}  // namespace MaildirName
