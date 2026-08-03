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

#include <QList>
#include <QString>

#include "completionentry.h"

/// Where the cursor sits in a query, and therefore what should be offered.
///
/// A plain value type produced by a pure function so the parsing rules can be
/// tested without a widget or a database.
struct CompletionContext
{
    enum Kind {
        None,    ///< Complete nothing: inside a quoted literal, for instance.
        Prefix,  ///< Complete a query keyword: tag:, date:, and, or, not.
        Value,   ///< Complete a value for `prefix`.
    };

    Kind kind = None;

    /// For Value, the keyword left of ':', lowercased. Empty for Prefix.
    QString prefix;

    /// The text being matched against the candidates.
    QString stem;

    /// The exact span an accepted completion overwrites. Covers only the text
    /// being completed, so accepting never disturbs neighbouring text.
    int replaceFrom = 0;
    int replaceLength = 0;

    /// Whether candidates that are themselves ranges may be offered.
    ///
    /// The relative date entries ("1week..") are complete open-ended ranges.
    /// Offering one inside an existing range yields date:1week....today, which
    /// is malformed, so they are withheld once a range is underway.
    bool allowRangeEntries = true;
};

/// Decides what the cursor position implies about completion.
///
/// `cursor` is an offset into `text`, as QLineEdit::cursorPosition() returns.
CompletionContext completionContext(const QString &text, int cursor);

/// The notmuch query keywords, with descriptions. Hardcoded: notmuch exposes
/// no way to enumerate its own prefixes, so this list must track releases by
/// hand. See the spec's Consequences section.
QList<CompletionEntry> prefixVocabulary();

/// Symbolic and relative date values. Absolute dates are not enumerable and
/// are covered by the free-form hint in the popup footer instead.
QList<CompletionEntry> dateVocabulary();

/// The built-in mimetypes, before the user's extra_mimetypes are appended.
QList<CompletionEntry> mimetypeVocabulary();
