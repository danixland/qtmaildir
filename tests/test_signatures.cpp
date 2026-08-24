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

QTEST_MAIN(TestSignatures)
#include "test_signatures.moc"
