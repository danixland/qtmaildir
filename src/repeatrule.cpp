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

#include "repeatrule.h"

#include <QCoreApplication>
#include <QLocale>
#include <QMap>

#include <algorithm>

namespace {

// iCalendar weekday codes, indexed by Qt::DayOfWeek - 1.
const char *const kDays[] = { "MO", "TU", "WE", "TH", "FR", "SA", "SU" };

int dayFromCode(const QString &code)
{
    for (int i = 0; i < 7; ++i)
        if (code == QLatin1String(kDays[i]))
            return i + 1;
    return 0;
}

/// Splits "-1FR" into ordinal -1 and Friday. A plain "FR" gives ordinal 0.
bool splitByDay(const QString &token, int *ordinal, int *day)
{
    if (token.size() < 2)
        return false;
    *day = dayFromCode(token.right(2));
    const QString head = token.left(token.size() - 2);
    bool ok = true;
    *ordinal = head.isEmpty() ? 0 : head.toInt(&ok);
    return *day != 0 && ok;
}

// Array literals have no enclosing class, so the context must be named on each
// literal (AGENTS.md: QT_TR_NOOP here compiles and extracts nothing).
const char *const kOrdinals[] = {
    QT_TRANSLATE_NOOP("RepeatRule", "first"),
    QT_TRANSLATE_NOOP("RepeatRule", "second"),
    QT_TRANSLATE_NOOP("RepeatRule", "third"),
    QT_TRANSLATE_NOOP("RepeatRule", "fourth"),
};

QString tr(const char *text) { return QCoreApplication::translate("RepeatRule", text); }

QString ordinalWord(int ordinal)
{
    if (ordinal == -1)
        return tr("last");
    return (ordinal >= 1 && ordinal <= 4) ? tr(kOrdinals[ordinal - 1]) : QString();
}

} // namespace

RepeatRule RepeatRule::fromRRule(const QString &value, const QDate &start)
{
    RepeatRule rule;
    auto markCustom = [&]() {
        RepeatRule custom;
        custom.custom = true;
        custom.customText = value;
        return custom;
    };

    if (value.trimmed().isEmpty())
        return rule;

    QMap<QString, QString> parts;
    for (const QString &pair : value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq <= 0)
            return markCustom();
        parts.insert(pair.left(eq).toUpper(), pair.mid(eq + 1).toUpper());
    }

    const QString freq = parts.take(QStringLiteral("FREQ"));
    if (freq == QLatin1String("DAILY")) rule.freq = Freq::Daily;
    else if (freq == QLatin1String("WEEKLY")) rule.freq = Freq::Weekly;
    else if (freq == QLatin1String("MONTHLY")) rule.freq = Freq::Monthly;
    else if (freq == QLatin1String("YEARLY")) rule.freq = Freq::Yearly;
    else return markCustom();

    if (parts.contains(QStringLiteral("INTERVAL"))) {
        rule.interval = parts.take(QStringLiteral("INTERVAL")).toInt();
        if (rule.interval < 1)
            return markCustom();
    }
    if (parts.contains(QStringLiteral("COUNT"))) {
        rule.end = End::Count;
        rule.count = parts.take(QStringLiteral("COUNT")).toInt();
        if (rule.count < 1)
            return markCustom();
    }
    if (parts.contains(QStringLiteral("UNTIL"))) {
        const QString until = parts.take(QStringLiteral("UNTIL"));
        rule.end = End::Until;
        rule.until = QDate::fromString(until.left(8), QStringLiteral("yyyyMMdd"));
        if (!rule.until.isValid())
            return markCustom();
    }
    rule.wkst = parts.take(QStringLiteral("WKST"));

    const QStringList byDay = parts.contains(QStringLiteral("BYDAY"))
        ? parts.take(QStringLiteral("BYDAY")).split(QLatin1Char(',')) : QStringList();
    const QStringList byMonthDay = parts.contains(QStringLiteral("BYMONTHDAY"))
        ? parts.take(QStringLiteral("BYMONTHDAY")).split(QLatin1Char(',')) : QStringList();
    const QStringList byMonth = parts.contains(QStringLiteral("BYMONTH"))
        ? parts.take(QStringLiteral("BYMONTH")).split(QLatin1Char(',')) : QStringList();

    if (!parts.isEmpty())  // BYSETPOS, BYWEEKNO, BYHOUR, ... : outside the table.
        return markCustom();

    switch (rule.freq) {
    case Freq::Daily:
        if (!byDay.isEmpty() || !byMonthDay.isEmpty() || !byMonth.isEmpty())
            return markCustom();
        break;
    case Freq::Weekly:
        if (!byMonthDay.isEmpty() || !byMonth.isEmpty())
            return markCustom();
        for (const QString &token : byDay) {
            int ordinal = 0, day = 0;
            if (!splitByDay(token, &ordinal, &day) || ordinal != 0)
                return markCustom();
            rule.weekdays.append(day);
        }
        if (rule.weekdays.isEmpty())
            rule.weekdays.append(start.dayOfWeek());
        std::sort(rule.weekdays.begin(), rule.weekdays.end());
        break;
    case Freq::Monthly:
    case Freq::Yearly: {
        const bool yearly = rule.freq == Freq::Yearly;
        if (!byMonthDay.isEmpty() && !byDay.isEmpty())
            return markCustom();
        if (!byDay.isEmpty()) {
            int ordinal = 0, day = 0;
            if (byDay.size() != 1 || !splitByDay(byDay.first(), &ordinal, &day)
                || !(ordinal == -1 || (ordinal >= 1 && ordinal <= 4)))
                return markCustom();
            if (yearly && byMonth.size() != 1)
                return markCustom();
            rule.by = By::Weekday;
            rule.ordinal = ordinal;
            rule.weekday = day;
            if (yearly)
                rule.month = byMonth.first().toInt();
        } else if (yearly) {
            if (!byMonth.isEmpty() || !byMonthDay.isEmpty())
                return markCustom();  // "on the start date" writes neither
        } else {
            if (!byMonth.isEmpty() || byMonthDay.size() > 1)
                return markCustom();
            rule.monthDay = byMonthDay.isEmpty() ? start.day() : byMonthDay.first().toInt();
            if (rule.monthDay < 1 || rule.monthDay > 31)
                return markCustom();
        }
        break;
    }
    case Freq::None:
        break;
    }
    return rule;
}

QString RepeatRule::toRRule(const QString &untilValue) const
{
    if (custom)
        return customText;
    if (freq == Freq::None)
        return {};

    QStringList parts;
    static const char *const kFreq[] = { "", "DAILY", "WEEKLY", "MONTHLY", "YEARLY" };
    parts << QStringLiteral("FREQ=%1").arg(QLatin1String(kFreq[static_cast<int>(freq)]));
    if (interval > 1)
        parts << QStringLiteral("INTERVAL=%1").arg(interval);

    auto byDayToken = [](int ordinal, int day) {
        return (ordinal ? QString::number(ordinal) : QString()) + QLatin1String(kDays[day - 1]);
    };
    if (freq == Freq::Weekly && !weekdays.isEmpty()) {
        QStringList days;
        for (int d : weekdays)
            days << QLatin1String(kDays[d - 1]);
        parts << QStringLiteral("BYDAY=%1").arg(days.join(QLatin1Char(',')));
    } else if (freq == Freq::Monthly) {
        if (by == By::Weekday)
            parts << QStringLiteral("BYDAY=%1").arg(byDayToken(ordinal, weekday));
        else
            parts << QStringLiteral("BYMONTHDAY=%1").arg(monthDay);
    } else if (freq == Freq::Yearly && by == By::Weekday) {
        parts << QStringLiteral("BYMONTH=%1").arg(month)
              << QStringLiteral("BYDAY=%1").arg(byDayToken(ordinal, weekday));
    }

    if (end == End::Count)
        parts << QStringLiteral("COUNT=%1").arg(count);
    else if (end == End::Until)
        parts << QStringLiteral("UNTIL=%1").arg(untilValue);
    if (!wkst.isEmpty())
        parts << QStringLiteral("WKST=%1").arg(wkst);
    return parts.join(QLatin1Char(';'));
}

QString RepeatRule::describe() const
{
    if (custom)
        return tr("Custom rule");
    if (freq == Freq::None)
        return tr("Does not repeat");

    const QLocale locale;
    QString text;
    switch (freq) {
    case Freq::Daily:
        text = interval == 1 ? tr("Daily") : tr("Every %1 days").arg(interval);
        break;
    case Freq::Weekly: {
        QStringList days;
        for (int d : weekdays)
            days << locale.dayName(d, QLocale::ShortFormat);
        text = (interval == 1 ? tr("Weekly") : tr("Every %1 weeks").arg(interval))
               + tr(", on %1").arg(days.join(QStringLiteral(", ")));
        break;
    }
    case Freq::Monthly:
        text = interval == 1 ? tr("Monthly") : tr("Every %1 months").arg(interval);
        text += by == By::Weekday
            ? tr(", on the %1 %2").arg(ordinalWord(ordinal), locale.dayName(weekday))
            : tr(", on day %1").arg(monthDay);
        break;
    case Freq::Yearly:
        text = interval == 1 ? tr("Yearly") : tr("Every %1 years").arg(interval);
        if (by == By::Weekday)
            text += tr(", on the %1 %2 of %3")
                        .arg(ordinalWord(ordinal), locale.dayName(weekday),
                             locale.monthName(month));
        break;
    case Freq::None:
        break;
    }
    if (end == End::Count)
        text += tr(", %1 times").arg(count);
    else if (end == End::Until)
        text += tr(", until %1").arg(locale.toString(until, QLocale::ShortFormat));
    return text;
}

bool RepeatRule::operator==(const RepeatRule &o) const
{
    return freq == o.freq && interval == o.interval && weekdays == o.weekdays
        && by == o.by && monthDay == o.monthDay && ordinal == o.ordinal
        && weekday == o.weekday && month == o.month && end == o.end
        && until == o.until && count == o.count && wkst == o.wkst
        && custom == o.custom && customText == o.customText;
}
