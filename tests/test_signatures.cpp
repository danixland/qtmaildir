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
#include <QTemporaryDir>

#include "signatures.h"

class TestSignatures : public QObject
{
    Q_OBJECT

private slots:
    void namesAreTheFileStemsSorted();
    void namesIgnoreFilesThatAreNotMarkdown();
    void aMissingDirectoryHasNoNames();
    void textIsTheFileContent();
    void textOfAnUnknownNameIsEmpty();
    void insertingAtTheEndAppendsAfterADelimiter();
    void insertingAboveTheQuotePutsItBeforeTheFirstQuotedLine();
    void insertingAboveTheQuoteWithNoQuoteIsTheSameAsEnd();
    void insertingNothingLeavesTheBufferAlone();

private:
    /// Writes \p files as name -> content into a fresh temporary directory.
    static void write(const QTemporaryDir &dir,
                      const QList<QPair<QString, QString>> &files);
};

void TestSignatures::write(const QTemporaryDir &dir,
                           const QList<QPair<QString, QString>> &files)
{
    for (const auto &entry : files) {
        QFile file(dir.path() + QStringLiteral("/") + entry.first);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(entry.second.toUtf8());
        file.close();
    }
}

void TestSignatures::namesAreTheFileStemsSorted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir, { { QStringLiteral("work.md"), QStringLiteral("Work") },
                 { QStringLiteral("brief.md"), QStringLiteral("Brief") } });

    QCOMPARE(Signatures::names(dir.path()),
             QStringList({ QStringLiteral("brief"), QStringLiteral("work") }));
}

void TestSignatures::namesIgnoreFilesThatAreNotMarkdown()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir, { { QStringLiteral("work.md"), QStringLiteral("Work") },
                 { QStringLiteral("notes.txt"), QStringLiteral("Not one") },
                 { QStringLiteral("README"), QStringLiteral("Nor this") } });

    QCOMPARE(Signatures::names(dir.path()),
             QStringList({ QStringLiteral("work") }));
}

void TestSignatures::aMissingDirectoryHasNoNames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.path() + QStringLiteral("/nothing-here");

    QVERIFY(Signatures::names(missing).isEmpty());
}

void TestSignatures::textIsTheFileContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    write(dir, { { QStringLiteral("work.md"),
                   QStringLiteral("Jane Doe\n**qtmaildir**\n") } });

    QCOMPARE(Signatures::text(dir.path(), QStringLiteral("work")),
             QStringLiteral("Jane Doe\n**qtmaildir**\n"));
}

void TestSignatures::textOfAnUnknownNameIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QVERIFY(Signatures::text(dir.path(), QStringLiteral("absent")).isEmpty());
}

void TestSignatures::insertingAtTheEndAppendsAfterADelimiter()
{
    const QString buffer = QStringLiteral("Hello.\n");

    const QString result = Signatures::replace(
        buffer, QStringLiteral("Jane Doe"), {}, Signatures::Position::End);

    QCOMPARE(result, QStringLiteral("Hello.\n\n-- \nJane Doe"));
}

void TestSignatures::insertingAboveTheQuotePutsItBeforeTheFirstQuotedLine()
{
    const QString buffer = QStringLiteral(
        "My reply.\n"
        "\n"
        "On Mon, someone wrote:\n"
        "> the original\n"
        "> second line\n");

    const QString result = Signatures::replace(
        buffer, QStringLiteral("Jane Doe"), {},
        Signatures::Position::AboveQuote);

    // Before the QUOTED lines, and the attribution stays with the quote it
    // introduces: it is the line the quote hangs from, not part of the reply.
    QCOMPARE(result, QStringLiteral(
        "My reply.\n"
        "\n"
        "-- \n"
        "Jane Doe\n"
        "\n"
        "On Mon, someone wrote:\n"
        "> the original\n"
        "> second line\n"));
}

void TestSignatures::insertingAboveTheQuoteWithNoQuoteIsTheSameAsEnd()
{
    const QString buffer = QStringLiteral("A new message.\n");

    const QString above = Signatures::replace(
        buffer, QStringLiteral("Jane Doe"), {},
        Signatures::Position::AboveQuote);
    const QString end = Signatures::replace(
        buffer, QStringLiteral("Jane Doe"), {}, Signatures::Position::End);

    QCOMPARE(above, end);
}

void TestSignatures::insertingNothingLeavesTheBufferAlone()
{
    const QString buffer = QStringLiteral("Hello.\n");

    QCOMPARE(Signatures::replace(buffer, QString(), {},
                                 Signatures::Position::End),
             buffer);
}

QTEST_MAIN(TestSignatures)
#include "test_signatures.moc"
