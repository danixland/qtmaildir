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

#include "maildirname.h"

#include <QSet>
#include <QTest>

class TestMaildirName : public QObject
{
    Q_OBJECT

private slots:
    void aFreshNameIsUniquePerCall();
    void theFlagSuffixIsPreserved();
    void anEmptyFlagSuffixIsPreserved();
    void aNameWithNoSuffixGetsNone();
    void theUidInfixIsNotCarriedAcross();
};

// Two messages written in the same second must not collide, which a
// timestamp alone does not guarantee, and that is what the counter is for.
void TestMaildirName::aFreshNameIsUniquePerCall()
{
    QSet<QString> names;
    for (int i = 0; i < 100; ++i)
        names.insert(MaildirName::fresh(QStringLiteral("1234.M1P1Q1.host")));

    QVERIFY2(names.size() == 100,
             qPrintable(QStringLiteral("expected 100 unique names, got %1")
                            .arg(names.size())));
}

// The flags say whether a message is read, flagged or draft, and losing them
// on a move silently marks mail unread again.
void TestMaildirName::theFlagSuffixIsPreserved()
{
    const QString name = MaildirName::fresh(QStringLiteral("1234.M1P1Q1.host:2,FS"));
    QVERIFY2(name.endsWith(QStringLiteral(":2,FS")),
             qPrintable(QStringLiteral("generated name did not preserve flags: %1")
                            .arg(name)));
}

// `:2,` with no flags is not the same as no suffix at all, it says the flags
// are known and empty.
void TestMaildirName::anEmptyFlagSuffixIsPreserved()
{
    const QString name = MaildirName::fresh(QStringLiteral("1234.M1P1Q1.host:2,"));
    QVERIFY2(name.endsWith(QStringLiteral(":2,")),
             qPrintable(QStringLiteral("generated name did not preserve empty flag suffix: %1")
                            .arg(name)));
}

// A suffix must not be invented.
void TestMaildirName::aNameWithNoSuffixGetsNone()
{
    const QString name = MaildirName::fresh(QStringLiteral("1234.M1P1Q1.host"));
    QVERIFY2(!name.contains(QStringLiteral(":2,")),
             qPrintable(QStringLiteral("generated name invented a flag suffix: %1")
                            .arg(name)));
}

// This is the reason the function exists; carrying mbsync's `,U=` infix
// across a folder boundary produced "Maildir error: duplicate UID" on real
// mail.
void TestMaildirName::theUidInfixIsNotCarriedAcross()
{
    const QString name = MaildirName::fresh(QStringLiteral("1234.M1P1Q1.host,U=42:2,S"));
    QVERIFY2(!name.contains(QStringLiteral("U=42")),
             qPrintable(QStringLiteral("generated name carried the UID infix across: %1")
                            .arg(name)));
    QVERIFY2(name.endsWith(QStringLiteral(":2,S")),
             qPrintable(QStringLiteral("generated name did not preserve flags: %1")
                            .arg(name)));
}

QTEST_MAIN(TestMaildirName)
#include "test_maildirname.moc"
