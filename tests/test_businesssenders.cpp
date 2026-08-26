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

#include <QTemporaryDir>
#include <QTest>

#include "businesssenders.h"

class TestBusinessSenders : public QObject
{
    Q_OBJECT

private slots:
    void anExactAddressMatches();
    void aDomainEntryMatchesEveryAddressUnderIt();
    void commentsAndBlankLinesAreIgnored();
    void whitespaceAroundAnEntryIsIgnored();
    void matchingIsCaseInsensitive();
    void anAbsentFileMatchesNothing();
};

void TestBusinessSenders::anExactAddressMatches()
{
    const BusinessSenders::List list = BusinessSenders::parse(
        QStringLiteral("noreply@cofidis.it\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("someone@cofidis.it")));
}

void TestBusinessSenders::aDomainEntryMatchesEveryAddressUnderIt()
{
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("@cofidis.it\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@cofidis.it")));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("a@example.org")));
}

void TestBusinessSenders::commentsAndBlankLinesAreIgnored()
{
    // A commented entry is the REJECT gesture: present in the file, not
    // applied. This is the property the whole file format rests on.
    const BusinessSenders::List list = BusinessSenders::parse(
        QStringLiteral("# noreply@cofidis.it (47 messages)\n"
                       "\n"
                       "   \n"
                       "billing@example.org\n"));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("noreply@cofidis.it")));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@example.org")));
}

void TestBusinessSenders::whitespaceAroundAnEntryIsIgnored()
{
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("  billing@example.org  \n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@example.org")));
}

void TestBusinessSenders::matchingIsCaseInsensitive()
{
    // Addresses arrive from headers in whatever case the sender used, so a
    // list entry that matched only one casing would look broken at random.
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("NoReply@Cofidis.IT\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
}

void TestBusinessSenders::anAbsentFileMatchesNothing()
{
    QTemporaryDir dir;
    const BusinessSenders::List list =
        BusinessSenders::load(dir.filePath(QStringLiteral("does-not-exist")));
    QVERIFY(!BusinessSenders::contains(list, QStringLiteral("a@example.org")));
}

QTEST_MAIN(TestBusinessSenders)
#include "test_businesssenders.moc"
