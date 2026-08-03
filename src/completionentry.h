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

/// One completion candidate and the prose describing it.
///
/// This lives in its own header because both Config and QueryCompleter need
/// it, and QueryCompleter needs Config. Declaring it in querycompleter.h
/// would make the two headers include each other.
struct CompletionEntry
{
    QString value;        ///< Inserted verbatim. Query syntax, never translated.
    QString description;  ///< Shown beside it. Prose, always translated.
};
