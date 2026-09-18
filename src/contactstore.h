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

/// One completion candidate from the address book.
///
/// Deliberately only two fields. PHOTO, ADR and TEL are read past and dropped:
/// the user was asked and did not want a second use for the store, so anything
/// the completion cannot use is not carried.
struct Contact
{
    /// The card's FN, already unescaped. Empty is valid: a card with an
    /// address but no name completes on the address alone.
    QString name;

    /// The value of one EMAIL line. The identity the store de-duplicates on.
    QString email;
};

/// Reads a vdirsyncer contacts directory of vCard 3.0 files.
///
/// A namespace of free functions over values, like SearchTerm and MimeParser,
/// so the parse is testable without a widget and without a QCompleter. The
/// caller (MainWindow) holds the returned list and hands it to both consumers.
///
/// **No new dependency for this.** libical is installed but its vCard parser is
/// 4.0 while this store is 3.0.20, and libicalvcal reads vCalendar 1.0 rather
/// than vCard. Two fields and a directory walk are the proportionate answer.
namespace ContactStore {

/// Joins folded lines back into one logical line.
///
/// A vCard line may be split between any two characters by inserting CRLF and
/// one linear whitespace character (RFC 2426). Unfolding removes the newline
/// and that one whitespace character and nothing else, so a trailing space on
/// the first line survives. A fold may land mid-token or inside base64, which
/// is why this runs over the whole file before any field is looked at.
QString unfold(const QString &text);

/// Parses one card's already-unfolded text into one Contact per EMAIL line.
///
/// Every entry carries the card's single FN. A card with no EMAIL yields an
/// empty list; a card with no FN yields a Contact whose name is empty. The
/// property name is matched case-insensitively, and the field/value split is
/// on the first colon that is not inside a double-quoted parameter value, so
/// `EMAIL;TYPE=WORK:a@example.org` parses.
QList<Contact> parseCard(const QString &unfoldedText);

/// Walks `directory` recursively and returns every card found, de-duplicated
/// and sorted.
///
/// Every `*.vcf` at any depth is read, because a vdir keeps one subdirectory
/// per collection. A file that cannot be read or contributes no EMAIL is
/// skipped rather than fatal: one bad card must not cost the other 116. The
/// result is de-duplicated on the address, case-insensitively, and sorted by
/// name then address. A non-existent or empty directory yields an empty list
/// and no warning, since a machine with no vdir is an ordinary machine.
QList<Contact> loadDirectory(const QString &directory);

}  // namespace ContactStore
