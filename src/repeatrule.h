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

#include <QCoreApplication>
#include <QDate>
#include <QList>
#include <QString>

/// The repeat control's model, and its RRULE value text in both directions.
///
/// No libical: an RRULE value is `KEY=VALUE;KEY=VALUE`, which is simpler to
/// read and write as text than through icalrecurrencetype, and keeping it pure
/// is what lets every row of the spec's repeat table be tested as a string.
struct RepeatRule
{
    // describe() formats the rule in words, and lupdate needs the class's own
    // context to extract them (AGENTS.md: a class needs this macro, an array
    // needs QT_TRANSLATE_NOOP on the literal). The macro ends in a private:
    // section, so the explicit public: restores the fields below.
    Q_DECLARE_TR_FUNCTIONS(RepeatRule)
public:

    enum class Freq { None, Daily, Weekly, Monthly, Yearly };
    /// Monthly and Yearly only: on a day of the month, or on the Nth weekday.
    enum class By { MonthDay, Weekday };
    enum class End { Never, Until, Count };

    Freq freq = Freq::None;
    int interval = 1;
    /// Weekly: Qt::DayOfWeek values, 1 (Monday) to 7 (Sunday), sorted.
    QList<int> weekdays;
    By by = By::MonthDay;
    int monthDay = 0;   ///< Monthly, By::MonthDay: 1 to 31.
    int ordinal = 0;    ///< By::Weekday: 1 to 4, or -1 for "last".
    int weekday = 0;    ///< By::Weekday: Qt::DayOfWeek.
    int month = 0;      ///< Yearly, By::Weekday: 1 to 12.
    End end = End::Never;
    QDate until;        ///< End::Until: the last day an occurrence may fall on.
    int count = 0;      ///< End::Count.
    /// Carried through a rewrite untouched, per the spec: several existing
    /// rules carry it and it changes nothing for the modes above.
    QString wkst;

    /// A rule outside the control's table. `customText` is the value exactly
    /// as read, and an edit never rewrites it.
    bool custom = false;
    QString customText;

    /// Parses an RRULE value (no "RRULE:" prefix). `start` is the event's
    /// first day, which fills in what an RRULE leaves implicit (a weekly rule
    /// with no BYDAY repeats on the start's weekday).
    static RepeatRule fromRRule(const QString &value, const QDate &start);

    /// The RRULE value, or an empty string for Freq::None. `untilValue` is
    /// the whole UNTIL value, `20261231` for an all-day event or
    /// `20261231T225959Z` for a timed one, computed by the caller because
    /// only it knows the event's zone. Ignored unless end is End::Until.
    QString toRRule(const QString &untilValue = QString()) const;

    /// "Monthly, on the last Friday", translated. Custom rules say so.
    QString describe() const;

    bool operator==(const RepeatRule &other) const;
};
