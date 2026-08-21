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

#include <csignal>
#include <sys/resource.h>

#include <QtTest>
#include <QTemporaryDir>

#include "draftstore.h"

class TestDraftStore : public QObject
{
    Q_OBJECT

private slots:
    void aWriteLandsInCurWithTheGivenFlags();
    void twoWritesProduceDistinctFiles();
    void thePreviousRevisionIsUnlinked();
    void theNewFileExistsBeforeTheOldOneGoes();
    void anUnwritableDirectoryReportsRatherThanThrows();
    void theFolderIsCreatedWhenAbsent();
    void theBytesAreWrittenVerbatim();
    void aFailedWriteLeavesNoFileBehind();
    void anEmptyFolderPathReportsRatherThanWriting();
};

void TestDraftStore::aWriteLandsInCurWithTheGivenFlags()
{
    // cur/, never new/. A file dropped in new/ is re-announced as fresh mail
    // by every reader of the Maildir, so a draft would arrive as a new
    // message every time it autosaved.
    QTemporaryDir dir;
    const DraftStore::Result result = DraftStore::write(
        dir.path(), QByteArray("From: a@example.org\r\n\r\nbody\r\n"),
        QStringLiteral("D"));

    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY2(result.path.contains(QStringLiteral("/cur/")),
             qPrintable(QStringLiteral("not written to cur/: %1").arg(result.path)));
    QVERIFY2(result.path.endsWith(QStringLiteral(":2,D")),
             qPrintable(QStringLiteral("flags missing: %1").arg(result.path)));
    QVERIFY(QFile::exists(result.path));
}

void TestDraftStore::twoWritesProduceDistinctFiles()
{
    QTemporaryDir dir;
    const DraftStore::Result first = DraftStore::write(
        dir.path(), QByteArray("one"), QStringLiteral("D"));
    const DraftStore::Result second = DraftStore::write(
        dir.path(), QByteArray("two"), QStringLiteral("D"));

    QVERIFY(first.ok() && second.ok());
    QVERIFY2(first.path != second.path,
             "two writes in the same second produced the same filename");
}

void TestDraftStore::thePreviousRevisionIsUnlinked()
{
    // Otherwise a draft autosaved every thirty seconds accumulates one file
    // per pause, and every one of them syncs to the server.
    QTemporaryDir dir;
    const DraftStore::Result first = DraftStore::write(
        dir.path(), QByteArray("revision one"), QStringLiteral("D"));
    QVERIFY(first.ok());

    const DraftStore::Result second = DraftStore::write(
        dir.path(), QByteArray("revision two"), QStringLiteral("D"), first.path);
    QVERIFY(second.ok());

    QVERIFY2(!QFile::exists(first.path),
             "the previous draft revision was left behind");
    QVERIFY(QFile::exists(second.path));
}

void TestDraftStore::theNewFileExistsBeforeTheOldOneGoes()
{
    // The ordering that matters: unlinking first would lose the draft
    // entirely if the write then failed.
    //
    // The failure has to happen at the WRITE, not before it. A destination
    // whose mkpath() fails returns too early to reach either ordering, so a
    // mutation moving the unlink ahead of the write still passes: measured,
    // "11 passed, 0 failed" with the unlink moved above the QSaveFile. The
    // seam is a cur/ that exists and is read-only, which mkpath() reports as
    // success (it is already there) and QSaveFile then refuses with
    // "Permission denied".
    QTemporaryDir good;
    const DraftStore::Result first = DraftStore::write(
        good.path(), QByteArray("precious"), QStringLiteral("D"));
    QVERIFY(first.ok());

    QTemporaryDir hostile;
    const QString cur = hostile.path() + QStringLiteral("/cur");
    QVERIFY(QDir().mkpath(cur));
    QVERIFY(QFile::setPermissions(cur, QFile::ReadOwner | QFile::ExeOwner));

    const DraftStore::Result failed = DraftStore::write(
        hostile.path(), QByteArray("replacement"), QStringLiteral("D"),
        first.path);

    // Restored before any assertion, so a failing assertion does not leave a
    // directory QTemporaryDir cannot clean up.
    QFile::setPermissions(cur, QFile::ReadOwner | QFile::WriteOwner
                                   | QFile::ExeOwner);

    QVERIFY2(!failed.ok(), "a write into an unwritable cur/ reported success");
    QVERIFY2(QFile::exists(first.path),
             "the previous revision was unlinked even though the new write failed");
}

void TestDraftStore::anUnwritableDirectoryReportsRatherThanThrows()
{
    const DraftStore::Result result = DraftStore::write(
        QStringLiteral("/proc/nonexistent-and-unwritable"),
        QByteArray("body"), QStringLiteral("D"));

    QVERIFY2(!result.ok(), "an unwritable directory reported success");
    QVERIFY2(!result.error.isEmpty(), "a failure carried no message to show");
    QVERIFY(result.path.isEmpty());
}

void TestDraftStore::theFolderIsCreatedWhenAbsent()
{
    // A configured drafts folder that does not exist yet is ordinary on a
    // fresh account. Note the asymmetry with the trash folder: creating a
    // folder here is safe because the NAME came from configuration and is
    // validated at load, not composed from a tag.
    QTemporaryDir dir;
    const QString nested = dir.filePath(QStringLiteral("Drafts"));
    const DraftStore::Result result = DraftStore::write(
        nested, QByteArray("body"), QStringLiteral("D"));

    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(QDir(nested + QStringLiteral("/cur")).exists());
}

void TestDraftStore::theBytesAreWrittenVerbatim()
{
    // A draft must be byte-identical to what would be sent, so nothing here
    // may re-encode, add a trailing newline, or translate line endings.
    QTemporaryDir dir;
    const QByteArray bytes("From: a@example.org\r\nSubject: x\r\n\r\nbody\r\n");
    const DraftStore::Result result =
        DraftStore::write(dir.path(), bytes, QStringLiteral("D"));
    QVERIFY(result.ok());

    QFile file(result.path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
}

void TestDraftStore::aFailedWriteLeavesNoFileBehind()
{
    // A Maildir reader scans cur/ and indexes whatever it finds, so a write
    // that fails PART WAY THROUGH must leave nothing, not a truncated
    // message. A truncated message is the worse outcome by far: it is a
    // plausible file that notmuch indexes and mbsync uploads.
    //
    // The failure has to land after a successful open() or it proves nothing
    // about the QSaveFile choice: an unwritable directory refuses a plain
    // QFile at open() too, and both then leave the directory empty. Measured
    // that way, a mutation swapping QSaveFile for QFile passed.
    //
    // RLIMIT_FSIZE opens the real case. With the limit below the payload the
    // open succeeds and write() returns short: measured, a plain QFile leaves
    // a 4096-byte file in the listing, while the store leaves nothing.
    //
    // What produces that nothing is the ORDER of the condition, not the
    // choice of QSaveFile, and getting this backwards is the dangerous
    // reading. commit() is NOT the protection: called after a short write it
    // returns true and renames the truncated bytes into place, measured as
    // "write 4096 of 65536, commit true" with the directory then holding that
    // file. The store never reaches it, because comparing write()'s return
    // against the payload size short-circuits the `||` first and returns; the
    // scratch file is then discarded by ~QSaveFile() having never been
    // committed, and the listing is empty.
    //
    // So the size comparison must stay AHEAD of commit() in that condition.
    // Reducing `write(bytes) != bytes.size() || !file.commit()` to
    // `!file.commit()` looks like a simplification and writes a truncated
    // draft into cur/, where notmuch indexes it and mbsync uploads it.
    //
    // The signal must be ignored before the limit is set, or the process is
    // killed by SIGXFSZ rather than seeing a short write.
    QTemporaryDir dir;

    struct rlimit previous;
    QVERIFY(getrlimit(RLIMIT_FSIZE, &previous) == 0);
    void (*previousHandler)(int) = signal(SIGXFSZ, SIG_IGN);

    struct rlimit limited;
    limited.rlim_cur = 4096;
    limited.rlim_max = previous.rlim_max;
    QVERIFY(setrlimit(RLIMIT_FSIZE, &limited) == 0);

    const DraftStore::Result result = DraftStore::write(
        dir.path(), QByteArray(64 * 1024, 'x'), QStringLiteral("D"));

    // Restored before any assertion, so a failing one does not leave the rest
    // of the suite unable to write a file.
    setrlimit(RLIMIT_FSIZE, &previous);
    signal(SIGXFSZ, previousHandler);

    QVERIFY2(!result.ok(), "a truncated write reported success");
    QVERIFY(result.path.isEmpty() || !QFile::exists(result.path));

    const QStringList entries =
        QDir(dir.path() + QStringLiteral("/cur")).entryList(QDir::Files);
    QVERIFY2(entries.isEmpty(),
             qPrintable(QStringLiteral("a truncated write left a message "
                                       "behind for notmuch to index: %1")
                            .arg(entries.join(QLatin1Char(' ')))));
}

void TestDraftStore::anEmptyFolderPathReportsRatherThanWriting()
{
    // An account with no drafts folder configured reaches here with an empty
    // string. Without the guard the destination becomes "/cur", an absolute
    // path at the root of the filesystem, and the only thing stopping the
    // write is that this process does not run as root. That is not a
    // safeguard, so the guard is asserted on its own MESSAGE rather than on
    // the failure: a refusal naming the missing configuration is a different
    // outcome from a permission error, and only the first survives being run
    // by a privileged user.
    const DraftStore::Result result =
        DraftStore::write(QString(), QByteArray("body"), QStringLiteral("D"));

    QVERIFY2(!result.ok(), "an empty folder path reported success");
    QVERIFY(result.path.isEmpty());
    QVERIFY2(!result.error.contains(QStringLiteral("/cur")),
             qPrintable(QStringLiteral(
                 "the empty path reached the filesystem instead of being "
                 "refused: %1").arg(result.error)));
    QVERIFY(!result.error.isEmpty());
}

QTEST_MAIN(TestDraftStore)
#include "test_draftstore.moc"
