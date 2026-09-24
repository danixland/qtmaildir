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

#include <QtTest>

#include "repeatrule.h"

class TestRepeatRule : public QObject
{
    Q_OBJECT

private slots:
    void roundTripsEveryRowOfTheTable_data();
    void roundTripsEveryRowOfTheTable();
    void fillsWhatAnRruleLeavesImplicit();
    void anEmptyRruleMeansDoesNotRepeat();
    void recognisesACustomRule_data();
    void recognisesACustomRule();
    void keepsWkst();
    void readsUntilInEveryForm();
    void describesInWords();
};

void TestRepeatRule::roundTripsEveryRowOfTheTable_data()
{
    QTest::addColumn<QString>("rrule");
    // Each row of the spec's repeat table, written the way toRRule writes it.
    QTest::newRow("daily") << QStringLiteral("FREQ=DAILY");
    QTest::newRow("every 3 days") << QStringLiteral("FREQ=DAILY;INTERVAL=3");
    QTest::newRow("weekly on days") << QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR");
    QTest::newRow("monthly on day") << QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=18");
    QTest::newRow("monthly last friday") << QStringLiteral("FREQ=MONTHLY;BYDAY=-1FR");
    QTest::newRow("monthly 2nd tuesday") << QStringLiteral("FREQ=MONTHLY;INTERVAL=2;BYDAY=2TU");
    QTest::newRow("yearly") << QStringLiteral("FREQ=YEARLY");
    QTest::newRow("yearly last fri of sep") << QStringLiteral("FREQ=YEARLY;BYMONTH=9;BYDAY=-1FR");
    QTest::newRow("count") << QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=18;COUNT=12");
    QTest::newRow("until date") << QStringLiteral("FREQ=YEARLY;UNTIL=20301231");
}

void TestRepeatRule::roundTripsEveryRowOfTheTable()
{
    QFETCH(QString, rrule);
    const QDate start(2026, 9, 18);
    const RepeatRule rule = RepeatRule::fromRRule(rrule, start);
    QVERIFY2(!rule.custom, qPrintable(rrule));
    // UNTIL in the "until date" row is a DATE, so the all-day form (no suffix).
    QCOMPARE(rule.toRRule(rule.end == RepeatRule::End::Until
                              ? rule.until.toString(QStringLiteral("yyyyMMdd"))
                              : QString()),
             rrule);
    QCOMPARE(RepeatRule::fromRRule(rule.toRRule(rule.until.toString(QStringLiteral("yyyyMMdd"))),
                                   start), rule);
}

void TestRepeatRule::fillsWhatAnRruleLeavesImplicit()
{
    // 2026-09-18 is a Friday.
    const RepeatRule weekly = RepeatRule::fromRRule(QStringLiteral("FREQ=WEEKLY"), QDate(2026, 9, 18));
    QCOMPARE(weekly.weekdays, QList<int>{ Qt::Friday });
    const RepeatRule monthly = RepeatRule::fromRRule(QStringLiteral("FREQ=MONTHLY"), QDate(2026, 9, 18));
    QCOMPARE(monthly.by, RepeatRule::By::MonthDay);
    QCOMPARE(monthly.monthDay, 18);
}

void TestRepeatRule::anEmptyRruleMeansDoesNotRepeat()
{
    const QDate start(2026, 9, 18);
    for (const QString &value : { QString(), QStringLiteral(""), QStringLiteral("  ") }) {
        const RepeatRule rule = RepeatRule::fromRRule(value, start);
        QCOMPARE(rule.freq, RepeatRule::Freq::None);
        QVERIFY(!rule.custom);
        QCOMPARE(rule.describe(), QStringLiteral("Does not repeat"));
        QCOMPARE(rule.toRRule(), QString());
    }
}

void TestRepeatRule::recognisesACustomRule_data()
{
    QTest::addColumn<QString>("rrule");
    QTest::newRow("bysetpos") << QStringLiteral("FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1");
    QTest::newRow("byweekno") << QStringLiteral("FREQ=YEARLY;BYWEEKNO=20");
    QTest::newRow("hourly") << QStringLiteral("FREQ=HOURLY");
    QTest::newRow("two months") << QStringLiteral("FREQ=YEARLY;BYMONTH=3,9;BYDAY=-1FR");
    QTest::newRow("two monthdays") << QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=1,15");
    QTest::newRow("weekly ordinal") << QStringLiteral("FREQ=WEEKLY;BYDAY=1MO");
    QTest::newRow("fifth monday") << QStringLiteral("FREQ=MONTHLY;BYDAY=5MO");
}

void TestRepeatRule::recognisesACustomRule()
{
    QFETCH(QString, rrule);
    const RepeatRule rule = RepeatRule::fromRRule(rrule, QDate(2026, 9, 18));
    QVERIFY(rule.custom);
    // An edit never rewrites a custom rule: it comes back exactly.
    QCOMPARE(rule.toRRule(), rrule);
}

void TestRepeatRule::keepsWkst()
{
    const RepeatRule rule = RepeatRule::fromRRule(QStringLiteral("FREQ=YEARLY;WKST=TU"), QDate(2026, 9, 18));
    QVERIFY(!rule.custom);
    QCOMPARE(rule.toRRule(), QStringLiteral("FREQ=YEARLY;WKST=TU"));
}

void TestRepeatRule::readsUntilInEveryForm()
{
    const QDate start(2026, 9, 18);
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=DAILY;UNTIL=20261231"), start).until,
             QDate(2026, 12, 31));
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=DAILY;UNTIL=20261231T120000"), start).until,
             QDate(2026, 12, 31));
    // A UTC UNTIL keeps its UTC date: the caller writes 23:59:59 local as UTC,
    // which in a zone east of UTC is still the same date.
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=DAILY;UNTIL=20261231T215959Z"), start).until,
             QDate(2026, 12, 31));
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=DAILY;UNTIL=20261231"), start).end,
             RepeatRule::End::Until);
}

void TestRepeatRule::describesInWords()
{
    QLocale::setDefault(QLocale::c());
    const QDate start(2026, 9, 18);
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=DAILY"), start).describe(),
             QStringLiteral("Daily"));
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=MONTHLY;BYDAY=-1FR"), start).describe(),
             QStringLiteral("Monthly, on the last Friday"));
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=18;COUNT=12"), start).describe(),
             QStringLiteral("Monthly, on day 18, 12 times"));
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=YEARLY;BYMONTH=9;BYDAY=-1FR"), start).describe(),
             QStringLiteral("Yearly, on the last Friday of September"));
    QCOMPARE(RepeatRule::fromRRule(QStringLiteral("FREQ=YEARLY;BYWEEKNO=20"), start).describe(),
             QStringLiteral("Custom rule"));
}

QTEST_MAIN(TestRepeatRule)
#include "test_repeatrule.moc"
