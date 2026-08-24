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
#include <QStringList>

/// Signatures, as markdown files spliced into the composer's buffer.
///
/// Free functions over values, with no widget anywhere, matching
/// MarkdownFormat, MessageBuilder and DraftStore. The splice is the part worth
/// testing and it is testable with no painter.
///
/// MARKDOWN, and that is what makes this small: MessageBuilder already builds
/// text/plain from the buffer verbatim and text/html from MarkdownRenderer
/// over the same string, so a signature in the buffer yields both forms with
/// no change there and no second code path. One choice by the user serves both
/// parts, which is what the feature was asked for.
namespace Signatures {

/// Where a newly inserted signature goes, from [compose] signature_position.
enum class Position {
    End,        ///< The end of the buffer. The default and the user's habit.
    AboveQuote  ///< Before the first quoted line, or the end when there is none.
};

/// The stems of every `*.md` in \p dir, sorted, without the extension.
///
/// A missing or unreadable directory yields an empty list. That is not a
/// misconfiguration: it means the user keeps no signatures, and the switch
/// then offers only "None".
QStringList names(const QString &dir);

/// The content of `<dir>/<name>.md`, or empty when it cannot be read.
///
/// \p name is a stem from names(), never a path. It is rejected if it contains
/// a path separator, so a value arriving from the config file cannot reach
/// outside \p dir.
QString text(const QString &dir, const QString &name);

/// Returns \p buffer with \p signature spliced in.
///
/// Any signature already present is replaced; \p signature empty removes it
/// and inserts nothing, which is what "None" selects.
///
/// \p known is the text of every signature in the directory, and it is what
/// makes this non-destructive. A `-- ` delimiter is NOT sufficient authority
/// to delete what follows it: the block is replaced only when its text matches
/// one of \p known, and otherwise the new signature is INSERTED with nothing
/// removed. `-- ` reaches a buffer without the user ever choosing a signature,
/// most plausibly pasted in with quoted text from another client, and the
/// unguarded rule would silently delete everything after it.
///
/// The failure is therefore directional, which is the whole point: a wrong
/// guess adds a visible second signature, one undo away, rather than losing
/// the user's own writing.
QString replace(const QString &buffer, const QString &signature,
                const QStringList &known, Position position);

}  // namespace Signatures
