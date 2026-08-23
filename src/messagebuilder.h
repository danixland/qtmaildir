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

#include "types.h"

struct Account;

/// Turns an OutgoingMessage into the RFC822 bytes that get sent.
///
/// ONE built message serves three consumers: the autosaved draft, the bytes on
/// the send command's stdin, and the sent copy. A draft is therefore
/// byte-identical to what would be sent.
///
/// GMime rather than assembling RFC822 by string. The alternative means
/// reimplementing RFC 2047 header encoding, quoted-printable for accented
/// bodies, boundary uniqueness and line-length limits. This user writes
/// Italian; a body containing an accented character is every message, and a
/// bug there produces mail that looks correct locally and arrives as mojibake.
namespace MessageBuilder {

struct Result
{
    QByteArray bytes;    ///< The complete message. Empty on failure.
    QString error;       ///< Empty on success.
    QString messageId;   ///< The generated Message-ID, for the caller's records.

    bool ok() const { return error.isEmpty(); }
};

/// Builds \p message as sent from \p account.
///
/// Fails, rather than sending a partial message, when an attachment named in
/// the message no longer exists. That is checked HERE, at build time, rather
/// than when the file was attached: a file can vanish in between, and the
/// failure must stop the send rather than produce a message missing the thing
/// it was written to carry.
Result build(const OutgoingMessage &message, const Account &account);

}  // namespace MessageBuilder
