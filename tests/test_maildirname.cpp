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

#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
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
    void resolveRenamedReturnsAPathThatStillExists();
    void resolveRenamedFindsTheFileMbsyncRenamed();
    void resolveRenamedIsEmptyWhenTheFileIsReallyGone();
    void resolveRenamedDoesNotMatchADifferentMessage();
    void resolveRenamedRefusesAnAmbiguousMatch();
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

namespace {

/// One empty file, so a test can assert on which PATH is chosen rather than on
/// content. resolveRenamed() answers a filesystem question and never opens the
/// file.
bool touch(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.close();
    return true;
}

}  // namespace

void TestMaildirName::resolveRenamedReturnsAPathThatStillExists()
{
    // The ordinary case, and the one that must stay cheap: nothing was
    // renamed, so the answer is the question.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath(QStringLiteral("1787647354.M369Q2.host:2,D"));
    QVERIFY(touch(path));

    QCOMPARE(MaildirName::resolveRenamed(path), path);
}

void TestMaildirName::resolveRenamedFindsTheFileMbsyncRenamed()
{
    // Item 163. mbsync uploads the file and inserts its `,U=<uid>` infix
    // before the flag suffix, leaving the unique stem alone.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString stale = dir.filePath(QStringLiteral("1787647354.M369Q2.host:2,D"));
    const QString renamed =
        dir.filePath(QStringLiteral("1787647354.M369Q2.host,U=5:2,D"));
    QVERIFY(touch(renamed));
    QVERIFY2(!QFile::exists(stale), "the stale path must not exist");

    QCOMPARE(MaildirName::resolveRenamed(stale), renamed);
}

void TestMaildirName::resolveRenamedIsEmptyWhenTheFileIsReallyGone()
{
    // The bounded half. A deleted file must NOT be recovered from, or a
    // reportable defect becomes a wrong answer.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString gone = dir.filePath(QStringLiteral("1787647354.M369Q2.host:2,D"));
    QVERIFY(!QFile::exists(gone));

    QVERIFY(MaildirName::resolveRenamed(gone).isEmpty());
}

void TestMaildirName::resolveRenamedDoesNotMatchADifferentMessage()
{
    // A neighbouring file in the same folder is not this message. Matching on
    // anything looser than the whole stem would return it, and the caller
    // would then open, display or MOVE the wrong mail.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString stale = dir.filePath(QStringLiteral("1787647354.M369Q2.host:2,D"));
    QVERIFY(touch(dir.filePath(QStringLiteral("1787647354.M369Q3.host,U=5:2,D"))));
    QVERIFY(touch(dir.filePath(QStringLiteral("9999999999.M111Q1.host,U=6:2,D"))));

    QVERIFY(MaildirName::resolveRenamed(stale).isEmpty());
}

void TestMaildirName::resolveRenamedRefusesAnAmbiguousMatch()
{
    // Two files sharing one stem cannot happen in a correct Maildir, so this
    // is a "the world is not what I assumed" case. Guessing between them could
    // move or delete the wrong file, and the caller reports honestly instead.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString stale = dir.filePath(QStringLiteral("1787647354.M369Q2.host:2,D"));
    QVERIFY(touch(dir.filePath(QStringLiteral("1787647354.M369Q2.host,U=5:2,D"))));
    QVERIFY(touch(dir.filePath(QStringLiteral("1787647354.M369Q2.host,U=6:2,S"))));

    QVERIFY(MaildirName::resolveRenamed(stale).isEmpty());
}

QTEST_MAIN(TestMaildirName)
#include "test_maildirname.moc"
