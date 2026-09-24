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
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QUndoStack>

#include <memory>

#include "calendarwindow.h"
#include "config.h"

namespace {

QByteArray eventText(const QString &uid, const QString &summary, int day)
{
    return QStringLiteral(
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//example//test//EN\r\n"
        "BEGIN:VEVENT\r\nUID:%1\r\nDTSTAMP:20260901T000000Z\r\n"
        "DTSTART:202609%2T080000Z\r\nDTEND:202609%2T090000Z\r\nSUMMARY:%3\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid).arg(day, 2, 10, QLatin1Char('0')).arg(summary).toUtf8();
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(content);
}

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// A vdir with collections "a" and "b", two events in "a", and a config
/// pointing at it with syncing off, so no test ever runs vdirsyncer.
struct Fixture
{
    QTemporaryDir dir;
    Config config;
    QString a() const { return dir.filePath(QStringLiteral("cal/a/one.ics")); }

    Fixture()
    {
        writeFile(a(), eventText(QStringLiteral("one@example.org"), QStringLiteral("One"), 22));
        writeFile(dir.filePath(QStringLiteral("cal/a/two.ics")),
                  eventText(QStringLiteral("two@example.org"), QStringLiteral("Two"), 23));
        QDir().mkpath(dir.filePath(QStringLiteral("cal/b")));
        const QString ini = dir.filePath(QStringLiteral("qtmaildir.conf"));
        writeFile(ini, QStringLiteral("[general]\ncalendars_dir = %1\n"
                                      "default_calendar = b\ncalendar_sync_command = \n")
                           .arg(dir.filePath(QStringLiteral("cal"))).toUtf8());
        config.load(ini);
    }

    CalendarWindow *window()
    {
        auto *w = new CalendarWindow(config, { QStringLiteral("me@example.org") },
                                     dir.filePath(QStringLiteral("uistate.conf")));
        w->showMonth(2026, 9);
        w->show();
        return w;
    }
};

/// Answers the next modal QMessageBox with `button` once it is up.
void answerNextBox(QMessageBox::StandardButton button)
{
    QTimer::singleShot(0, [button]() {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY2(box, "no modal message box appeared");
        box->button(button)->click();
    });
}

void editTitle(CalendarWindow *w, const QString &title)
{
    w->findChild<QPushButton *>(QStringLiteral("editEvent"))->click();
    w->findChild<QLineEdit *>(QStringLiteral("eventTitle"))->setText(title);
}

} // namespace

class TestCalendarWindow : public QObject
{
    Q_OBJECT

private slots:
    void loadsAndSelects();
    void theEditLockRefusesASelectionChange();
    void closingWithUnsavedChangesAsksOnEveryRoute();
    void saveWritesAndUndoRestoresTheOriginalBytes();
    void aNewEventGoesToTheDefaultCalendar();
    void aStaleSaveKeepsTheFormAndTheSyncedFile();
    void theToolbarCarriesTheNewEventAction();
    void aFreshWindowShowsTheCurrentMonth();
    void aStaleSaveCanBeRetried();
    void aRolledBackWriteIsNotReportedLostAfterSync();
    void escapeClosesTheDetailsButNotAnEditFromOutsideThePane();
};

void TestCalendarWindow::loadsAndSelects()
{
    Fixture f;
    std::unique_ptr<CalendarWindow> w(f.window());
    QVERIFY(w->selectEvent(QStringLiteral("two@example.org")));
    QCOMPARE(w->selectedUid(), QStringLiteral("two@example.org"));
    QVERIFY(!w->selectEvent(QStringLiteral("absent@example.org")));
}

void TestCalendarWindow::theEditLockRefusesASelectionChange()
{
    Fixture f;
    std::unique_ptr<CalendarWindow> w(f.window());
    QVERIFY(w->selectEvent(QStringLiteral("one@example.org")));
    editTitle(w.get(), QStringLiteral("Changed"));
    QVERIFY(!w->selectEvent(QStringLiteral("two@example.org")));
    QCOMPARE(w->selectedUid(), QStringLiteral("one@example.org"));
}

void TestCalendarWindow::closingWithUnsavedChangesAsksOnEveryRoute()
{
    Fixture f;
    const QByteArray original = read(f.a());

    // Cancel keeps the window and the file. Route: close().
    std::unique_ptr<CalendarWindow> w(f.window());
    w->selectEvent(QStringLiteral("one@example.org"));
    editTitle(w.get(), QStringLiteral("Changed"));
    answerNextBox(QMessageBox::Cancel);
    w->close();
    QVERIFY(w->isVisible());
    QCOMPARE(read(f.a()), original);

    // Discard closes and writes nothing. Route: the File menu's Close action.
    answerNextBox(QMessageBox::Discard);
    w->findChild<QAction *>(QStringLiteral("closeCalendar"))->trigger();
    QVERIFY(!w->isVisible());
    QCOMPARE(read(f.a()), original);

    // Save closes and writes.
    std::unique_ptr<CalendarWindow> again(f.window());
    again->selectEvent(QStringLiteral("one@example.org"));
    editTitle(again.get(), QStringLiteral("Saved on close"));
    answerNextBox(QMessageBox::Save);
    again->close();
    QVERIFY(!again->isVisible());
    QVERIFY(read(f.a()).contains("SUMMARY:Saved on close"));
}

void TestCalendarWindow::saveWritesAndUndoRestoresTheOriginalBytes()
{
    Fixture f;
    const QByteArray original = read(f.a());
    std::unique_ptr<CalendarWindow> w(f.window());
    w->selectEvent(QStringLiteral("one@example.org"));
    editTitle(w.get(), QStringLiteral("Renamed"));
    w->findChild<QPushButton *>(QStringLiteral("saveEvent"))->click();
    QVERIFY(read(f.a()).contains("SUMMARY:Renamed"));

    w->undoStack()->undo();
    QCOMPARE(read(f.a()), original);   // exactly the bytes it had: item 176's rule
    w->undoStack()->redo();
    QVERIFY(read(f.a()).contains("SUMMARY:Renamed"));
}

void TestCalendarWindow::aNewEventGoesToTheDefaultCalendar()
{
    Fixture f;
    std::unique_ptr<CalendarWindow> w(f.window());
    w->findChild<QAction *>(QStringLiteral("newEvent"))->trigger();
    w->findChild<QLineEdit *>(QStringLiteral("eventTitle"))->setText(QStringLiteral("Fresh"));
    w->findChild<QPushButton *>(QStringLiteral("saveEvent"))->click();
    const QStringList created = QDir(f.dir.filePath(QStringLiteral("cal/b")))
                                    .entryList({ QStringLiteral("*.ics") }, QDir::Files);
    QCOMPARE(created.size(), 1);
    QVERIFY(read(f.dir.filePath(QStringLiteral("cal/b/")) + created.first()).contains("SUMMARY:Fresh"));

    w->undoStack()->undo();
    QVERIFY(QDir(f.dir.filePath(QStringLiteral("cal/b"))).entryList({ QStringLiteral("*.ics") }).isEmpty());
}

void TestCalendarWindow::aStaleSaveKeepsTheFormAndTheSyncedFile()
{
    Fixture f;
    std::unique_ptr<CalendarWindow> w(f.window());
    w->selectEvent(QStringLiteral("one@example.org"));
    editTitle(w.get(), QStringLiteral("Mine"));
    // A sync pulls a server change while the form is open.
    const QByteArray synced = eventText(QStringLiteral("one@example.org"), QStringLiteral("Theirs"), 22);
    writeFile(f.a(), synced);
    w->findChild<QPushButton *>(QStringLiteral("saveEvent"))->click();
    QCOMPARE(read(f.a()), synced);           // nothing clobbered
    QVERIFY(w->isEditing());                  // the user's values are still there
    QCOMPARE(w->findChild<QLineEdit *>(QStringLiteral("eventTitle"))->text(), QStringLiteral("Mine"));
}

void TestCalendarWindow::theToolbarCarriesTheNewEventAction()
{
    Fixture f;
    std::unique_ptr<CalendarWindow> w(f.window());
    auto *bar = w->findChild<QToolBar *>(QStringLiteral("calendarToolbar"));
    QVERIFY(bar);
    auto *newEvent = w->findChild<QAction *>(QStringLiteral("newEvent"));
    QVERIFY(newEvent);
    QVERIFY(bar->actions().contains(newEvent));
}

void TestCalendarWindow::aFreshWindowShowsTheCurrentMonth()
{
    Fixture f;
    std::unique_ptr<CalendarWindow> w(
        new CalendarWindow(f.config, { QStringLiteral("me@example.org") },
                          f.dir.filePath(QStringLiteral("uistate.conf"))));
    const QDate today = QDate::currentDate();
    QCOMPARE(w->findChild<QComboBox *>(QStringLiteral("monthBox"))->currentData().toInt(),
             today.month());
    QCOMPARE(w->findChild<QSpinBox *>(QStringLiteral("yearSpin"))->value(), today.year());
}

void TestCalendarWindow::aStaleSaveCanBeRetried()
{
    Fixture f;
    std::unique_ptr<CalendarWindow> w(f.window());
    w->selectEvent(QStringLiteral("one@example.org"));
    editTitle(w.get(), QStringLiteral("Mine"));
    // A sync pulls a server change while the form is open.
    const QByteArray synced = eventText(QStringLiteral("one@example.org"), QStringLiteral("Theirs"), 22);
    writeFile(f.a(), synced);
    w->findChild<QPushButton *>(QStringLiteral("saveEvent"))->click();
    QCOMPARE(read(f.a()), synced);           // first Save refuses, nothing clobbered
    QVERIFY(w->isEditing());

    // The user checks what arrived and saves again deliberately: the form's
    // bytes were rebased on the synced file, so this one lands.
    w->findChild<QPushButton *>(QStringLiteral("saveEvent"))->click();
    QVERIFY(read(f.a()).contains("SUMMARY:Mine"));
    QVERIFY(!w->isEditing());
}

void TestCalendarWindow::aRolledBackWriteIsNotReportedLostAfterSync()
{
    // applyChanges records every successful write in m_written for the next
    // sync to verify. A later change in the same call fails, rolling the
    // earlier one back, so that write never reached disk and must be
    // forgotten. Keeping it makes the next sync compare the file against
    // bytes that were reverted and warn that the server kept a different
    // version, a difference that never happened.
    QTemporaryDir dir;
    const QString cal = dir.filePath(QStringLiteral("cal"));
    const QString one = cal + QStringLiteral("/a/one.ics");
    const QString two = cal + QStringLiteral("/a/two.ics");
    const QByteArray original =
        eventText(QStringLiteral("one@example.org"), QStringLiteral("One"), 22);
    writeFile(one, original);
    writeFile(two, eventText(QStringLiteral("two@example.org"), QStringLiteral("Two"), 23));

    // A real sync command, so the post-sync check actually runs.
    const QString ini = dir.filePath(QStringLiteral("qtmaildir.conf"));
    writeFile(ini, QStringLiteral("[general]\ncalendars_dir = %1\n"
                                  "calendar_sync_command = /bin/true\n"
                                  "calendar_sync_delay_ms = 0\n").arg(cal).toUtf8());
    Config config;
    config.load(ini);
    std::unique_ptr<CalendarWindow> w(
        new CalendarWindow(config, { QStringLiteral("me@example.org") },
                           dir.filePath(QStringLiteral("uistate.conf"))));

    // Change 1 succeeds; change 2 is stale (its expected bytes do not match a
    // missing file), so change 1 is rolled back.
    const QByteArray rolled =
        eventText(QStringLiteral("one@example.org"), QStringLiteral("Rolled"), 22);
    const QList<CalendarWindow::Change> changes = {
        { one, original, rolled },
        { cal + QStringLiteral("/a/ghost.ics"), QByteArray("gone"), QByteArray("x") },
    };
    QVERIFY(!w->applyChanges(changes, false));
    QCOMPARE(read(one), original);  // the first change was reverted

    // A later successful write triggers the sync. The rolled-back path must
    // not reappear in the post-sync check: with the fix the sync reports
    // clean, without it the stale entry produces a false "different version".
    const QByteArray twoAfter =
        eventText(QStringLiteral("two@example.org"), QStringLiteral("Two, edited"), 23);
    QVERIFY(w->applyChanges({ { two, read(two), twoAfter } }, false));

    QTRY_VERIFY_WITH_TIMEOUT(
        w->statusBar()->currentMessage().contains(QStringLiteral("Calendars synced.")),
        5000);
}


void TestCalendarWindow::escapeClosesTheDetailsButNotAnEditFromOutsideThePane()
{
    Fixture f;
    std::unique_ptr<CalendarWindow> w(f.window());
    auto *close = w->findChild<QAction *>(QStringLiteral("cancelEdit"));
    QVERIFY(close);
    QVERIFY(w->selectEvent(QStringLiteral("one@example.org")));
    close->trigger();
    QVERIFY(w->selectedUid().isEmpty());

    // Focus is nowhere inside the pane, as after a click on the grid.
    QVERIFY(w->selectEvent(QStringLiteral("one@example.org")));
    editTitle(w.get(), QStringLiteral("Changed"));
    close->trigger();
    QVERIFY(w->isEditing());
}

QTEST_MAIN(TestCalendarWindow)
#include "test_calendarwindow.moc"
