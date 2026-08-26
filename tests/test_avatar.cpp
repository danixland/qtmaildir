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

#include <QTest>

#include "avatar.h"

class TestAvatar : public QObject
{
    Q_OBJECT

private slots:
    void twoWordNameTakesOneLetterFromEach();
    void oneWordNameTakesItsFirstTwoLetters();
    void bareAddressTakesLocalAndDomain();
    void nothingUsableFallsBackToTheAccountLabel();
    void initialsAreAlwaysTwoLetters();
};

void TestAvatar::twoWordNameTakesOneLetterFromEach()
{
    QCOMPARE(Avatar::initialsFor(QStringLiteral("John Doe"),
                                 QStringLiteral("john@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("JD"));
    // Three words still take the FIRST two, not the first and last.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("Maria Grazia Rossi"),
                                 QStringLiteral("maria@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("MG"));
}

void TestAvatar::oneWordNameTakesItsFirstTwoLetters()
{
    QCOMPARE(Avatar::initialsFor(QStringLiteral("Cofidis"),
                                 QStringLiteral("noreply@cofidis.it"),
                                 QStringLiteral("Work")),
             QStringLiteral("CO"));
}

void TestAvatar::bareAddressTakesLocalAndDomain()
{
    QCOMPARE(Avatar::initialsFor(QString(),
                                 QStringLiteral("noreply@cofidis.it"),
                                 QStringLiteral("Work")),
             QStringLiteral("NC"));
}

void TestAvatar::nothingUsableFallsBackToTheAccountLabel()
{
    // No name and no address at all: the account's label is the last resort,
    // so a card always carries a squircle rather than a hole.
    QCOMPARE(Avatar::initialsFor(QString(), QString(),
                                 QStringLiteral("Work")),
             QStringLiteral("WO"));
    // And with nothing whatsoever, still two characters rather than empty.
    QCOMPARE(Avatar::initialsFor(QString(), QString(), QString()).size(), 2);
}

void TestAvatar::initialsAreAlwaysTwoLetters()
{
    // The shape is the point: every squircle reads the same. An address with
    // no domain, a one-letter local part and a name of one letter all still
    // produce two characters.
    const QStringList names { QString(), QStringLiteral("X"),
                              QStringLiteral("A B") };
    const QStringList addresses { QStringLiteral("a@b.org"),
                                  QStringLiteral("malformed"),
                                  QString() };
    for (const QString &name : names) {
        for (const QString &address : addresses) {
            const QString initials =
                Avatar::initialsFor(name, address, QStringLiteral("Acct"));
            QCOMPARE(initials.size(), 2);
        }
    }
}

QTEST_MAIN(TestAvatar)
#include "test_avatar.moc"
