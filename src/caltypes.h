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

#include "repeatrule.h"

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QList>
#include <QString>

/// One vdir collection: a subdirectory of calendars_dir.
struct CalCollection
{
    QString dir;          ///< The directory's own name, e.g. "52".
    QString path;         ///< Absolute path.
    /// From the vdir's `displayname` file, which vdirsyncer's metasync fills
    /// from the server; the directory name when absent.
    QString displayName;
    /// From the vdir's `color` file (`#RRGGBB`); a colour hashed from the
    /// directory name when absent, so no collection is ever invisible.
    QColor color;
    bool readOnly = false;  ///< The directory is not writable.
};

struct CalPerson
{
    QString name;      ///< CN, possibly empty.
    QString address;   ///< The address with "mailto:" removed.
    QString partstat;  ///< PARTSTAT as written, e.g. "ACCEPTED"; empty if absent.
};

/// A RECURRENCE-ID override: one occurrence of a series moved or changed.
struct CalOverride
{
    QDateTime recurrenceId;  ///< The original slot it replaces.
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    QString summary;
    QString location;
    QString description;
    bool cancelled = false;  ///< STATUS:CANCELLED: the slot is removed.
};

/// One .ics file's event, as the rest of the application sees it.
struct CalEvent
{
    QString filePath;
    QString collectionDir;
    QString uid;
    QString summary;
    QString location;
    QString description;
    QDateTime start;
    QDateTime end;   ///< Exclusive. For an all-day event, midnight after the last day.
    bool allDay = false;
    RepeatRule repeat;  ///< Freq::None when the event does not repeat.
    QList<QDateTime> exdates;
    QList<CalOverride> overrides;
    bool hasAlarm = false;
    CalPerson organizer;  ///< Empty address: no ORGANIZER.
    QList<CalPerson> attendees;
    int sequence = 0;
    QByteArray rawText;  ///< The file exactly as read: stale checks and undo.
};

/// One expanded occurrence. What a view is built from.
struct Occurrence
{
    int eventIndex = -1;       ///< Into LoadResult::events.
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    /// The slot this occurrence fills in its series, which is what a
    /// RECURRENCE-ID or an EXDATE names. Invalid for a non-repeating event.
    QDateTime recurrenceId;
    bool isOverride = false;
    int overrideIndex = -1;    ///< Into CalEvent::overrides when isOverride.
};

/// An occurrence plus what a view draws. Views see nothing else.
struct CalendarItem
{
    Occurrence occurrence;
    QString title;
    QColor color;
};

/// What the edit form produces.
struct EventEdit
{
    QString summary;
    QString location;
    QString description;
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    RepeatRule repeat;
    QString collectionDir;
};

struct LoadResult
{
    QList<CalCollection> collections;
    QList<CalEvent> events;
    int unparsable = 0;     ///< Files skipped because they did not parse.
    int unknownZones = 0;   ///< Events whose TZID resolved to nothing, shown in local time.
};
