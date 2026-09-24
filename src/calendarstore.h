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

#include "caltypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

/// The calendar's iCalendar work, and the only code that includes libical.
///
/// A namespace of free functions over values, like ContactStore and
/// MimeParser, so every rule with a right answer is testable without a
/// widget. See the spec, docs/superpowers/specs/2026-09-24-calendar-design.md.
namespace CalendarStore {

enum class Scope { All, ThisOccurrence };

/// Parses one file's text. An empty uid in the result means the text held no
/// usable VEVENT. `unknownZone` is set when a TZID resolved to nothing and the
/// times were read as local.
CalEvent parseEvent(const QByteArray &text, const QString &filePath,
                    const QString &collectionDir, bool *unknownZone);

/// Reads every collection under `dir` and every *.ics in each.
LoadResult load(const QString &dir);

/// Every occurrence of every event overlapping [from, to), sorted by start.
QList<Occurrence> occurrences(const QList<CalEvent> &events,
                              const QDateTime &from, const QDateTime &to);

/// Whether the user may edit or delete `event`: its collection is writable,
/// and it has no ORGANIZER or the organiser is one of `ownAddresses`.
bool isEditable(const CalEvent &event, const CalCollection &collection,
                const QStringList &ownAddresses);

/// Applies `edit` to the file text and returns the new text; empty when the
/// text does not parse. Scope::ThisOccurrence writes or replaces the override
/// for `recurrenceId`. Properties the form does not own survive.
QByteArray applyEdit(const QByteArray &text, const EventEdit &edit,
                     Scope scope, const QDateTime &recurrenceId);

/// A complete new file for `edit`, timed events in the IANA zone `zoneId`.
QByteArray newEvent(const EventEdit &edit, const QByteArray &zoneId);

/// Removes one occurrence: adds an EXDATE to the master and drops any override
/// for that slot. Empty on a parse failure.
QByteArray deleteOccurrence(const QByteArray &text, const QDateTime &recurrenceId);

/// Whether two texts describe the same event: summary, times, repeat rule,
/// exceptions. Formatting is ignored, since the server may normalise it.
bool sameMeaning(const QByteArray &a, const QByteArray &b);

}  // namespace CalendarStore
