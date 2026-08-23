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

/// Writes message bytes into a Maildir folder.
///
/// Drafts and sent copies are the same operation into different folders with
/// different flags, so they are one unit. Nothing here calls notmuch: the
/// files become visible on the next sync, which keeps the read-only-by-default
/// rule intact and needs no write lock.
class DraftStore
{
public:
    struct Result
    {
        QString path;   ///< The file written. Empty on failure.
        QString error;  ///< Empty on success.

        bool ok() const { return error.isEmpty(); }
    };

    /// Writes \p bytes into \p folderPath, an absolute Maildir folder.
    ///
    /// The folder is the CALLER's to resolve, and item 124 is why it is not
    /// resolved here: the mail root comes from
    /// `notmuch_config_get(NOTMUCH_CONFIG_MAIL_ROOT)`, never from
    /// `notmuch_database_get_path()`, which under a split index returns the
    /// Xapian directory. A store that composed its own path from the wrong
    /// accessor would write drafts into the index tree.
    ///
    /// \p flags is the Maildir flag string without the `:2,` prefix: "D" for a
    /// draft, "S" for a sent copy.
    ///
    /// \p previousPath, when not empty, is unlinked AFTER the new file is
    /// safely in place. Maildir has no in-place edit, so a draft rewritten
    /// every thirty seconds would otherwise accumulate one file per pause.
    /// The order matters: unlinking first would lose the draft entirely if the
    /// write then failed.
    static Result write(const QString &folderPath, const QByteArray &bytes,
                        const QString &flags, const QString &previousPath = {});
};
