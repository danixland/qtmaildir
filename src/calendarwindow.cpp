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

#include "calendarwindow.h"
#include "agendaview.h"
#include "calendarstore.h"
#include "calendarsync.h"
#include "calendarwriter.h"
#include "eventpane.h"
#include "monthview.h"

#include <QAction>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimeZone>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>

namespace {

/// One undoable write. The write itself happened before the push, so the
/// first redo() (which QUndoStack::push calls) does nothing. Undo restores
/// exactly the bytes the write replaced, expecting on disk exactly the bytes
/// it wrote: a sync in between makes it refuse, and the command is dropped.
class FileCommand : public QUndoCommand
{
public:
    FileCommand(const QString &text, const QList<CalendarWindow::Change> &changes,
                CalendarWindow *window)
        : QUndoCommand(text), m_changes(changes), m_window(window) {}

    void undo() override
    {
        if (!m_window->applyChanges(m_changes, true))
            setObsolete(true);
    }

    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        if (!m_window->applyChanges(m_changes, false))
            setObsolete(true);
    }

private:
    QList<CalendarWindow::Change> m_changes;
    CalendarWindow *m_window;
    bool m_first = true;
};

QIcon swatch(const QColor &colour)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter(&pixmap).fillRect(pixmap.rect(), colour);
    return QIcon(pixmap);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

CalendarWindow::CalendarWindow(const Config &config, const QStringList &ownAddresses,
                               const QString &uiStatePath, QWidget *parent)
    : QMainWindow(parent), m_config(config), m_ownAddresses(ownAddresses),
      m_uiStatePath(uiStatePath),
      m_year(QDate::currentDate().year()), m_month(QDate::currentDate().month())
{
    setWindowTitle(tr("Calendar"));
    m_sync = new CalendarSync(m_config.calendarSyncCommand(),
                              m_config.calendarSyncDelayMs(), this);
    connect(m_sync, &CalendarSync::started, this, [this]() {
        status(tr("Syncing calendars..."));
        m_checking.insert(m_written);  // insert(QHash) merges
        m_written.clear();
    });
    connect(m_sync, &CalendarSync::finished, this, [this](bool ok, const QString &output) {
        if (!ok) {
            QMessageBox::warning(this, tr("Calendar sync failed"), output);
            status(tr("Calendar sync failed."));
        }
        checkAfterSync();
    });

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(500);
    connect(&m_reloadTimer, &QTimer::timeout, this, &CalendarWindow::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this]() { m_reloadTimer.start(); });

    buildUi();
    buildActions();

    QSettings state(m_uiStatePath, QSettings::IniFormat);
    state.beginGroup(QStringLiteral("calendar"));
    restoreGeometry(state.value(QStringLiteral("geometry")).toByteArray());
    const bool agenda = state.value(QStringLiteral("agenda"), false).toBool();
    const QString collectionDir = state.value(QStringLiteral("collection")).toString();
    state.endGroup();

    reload();
    refreshToolbar();
    const int index = m_collectionBox->findData(collectionDir);
    if (index >= 0)
        m_collectionBox->setCurrentIndex(index);
    (agenda ? m_agendaButton : m_monthButton)->click();
}

CalendarWindow::~CalendarWindow() = default;

void CalendarWindow::buildUi()
{
    auto *toolbar = addToolBar(tr("Calendar"));
    toolbar->setObjectName(QStringLiteral("calendarToolbar"));
    toolbar->setMovable(false);

    m_collectionBox = new QComboBox(this);
    m_collectionBox->setObjectName(QStringLiteral("collectionBox"));
    toolbar->addWidget(m_collectionBox);
    toolbar->addSeparator();

    auto addButton = [&](const QString &text, auto slot) {
        auto *b = new QToolButton(this);
        b->setText(text);
        connect(b, &QToolButton::clicked, this, slot);
        toolbar->addWidget(b);
        return b;
    };
    addButton(QStringLiteral("‹"), [this]() {
        const QDate d = QDate(m_year, m_month, 1).addMonths(-1);
        showMonth(d.year(), d.month());
    })->setToolTip(tr("Previous month"));
    addButton(tr("Today"), [this]() {
        showMonth(QDate::currentDate().year(), QDate::currentDate().month());
        m_agendaView->scrollToDay(QDate::currentDate());
    })->setToolTip(tr("Go to today"));
    addButton(QStringLiteral("›"), [this]() {
        const QDate d = QDate(m_year, m_month, 1).addMonths(1);
        showMonth(d.year(), d.month());
    })->setToolTip(tr("Next month"));

    m_monthBox = new QComboBox(this);
    m_monthBox->setObjectName(QStringLiteral("monthBox"));
    for (int m = 1; m <= 12; ++m)
        m_monthBox->addItem(QLocale().standaloneMonthName(m), m);
    toolbar->addWidget(m_monthBox);
    m_yearSpin = new QSpinBox(this);
    m_yearSpin->setObjectName(QStringLiteral("yearSpin"));
    m_yearSpin->setRange(1900, 2200);
    toolbar->addWidget(m_yearSpin);
    connect(m_monthBox, &QComboBox::activated, this,
            [this](int index) { showMonth(m_year, index + 1); });
    connect(m_yearSpin, &QSpinBox::valueChanged, this, [this](int year) {
        if (year != m_year)
            showMonth(year, m_month);
    });

    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    m_monthButton = new QToolButton(this);
    m_monthButton->setText(tr("Month"));
    m_monthButton->setCheckable(true);
    m_agendaButton = new QToolButton(this);
    m_agendaButton->setText(tr("Agenda"));
    m_agendaButton->setCheckable(true);
    auto *group = new QButtonGroup(this);
    group->addButton(m_monthButton, 0);
    group->addButton(m_agendaButton, 1);
    toolbar->addWidget(m_monthButton);
    toolbar->addWidget(m_agendaButton);

    auto *splitter = new QSplitter(this);
    m_views = new QStackedWidget(splitter);
    m_monthView = new MonthView(m_views);
    m_agendaView = new AgendaView(m_views);
    m_views->addWidget(m_monthView);
    m_views->addWidget(m_agendaView);
    m_pane = new EventPane(splitter);
    splitter->addWidget(m_views);
    splitter->addWidget(m_pane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    m_dataWarning = new QLabel(this);
    statusBar()->addPermanentWidget(m_dataWarning);

    connect(group, &QButtonGroup::idClicked, m_views, &QStackedWidget::setCurrentIndex);
    connect(m_collectionBox, &QComboBox::currentIndexChanged, this, &CalendarWindow::rebuildItems);
    connect(m_monthView, &MonthView::itemClicked, this, &CalendarWindow::select);
    connect(m_agendaView, &AgendaView::itemClicked, this, &CalendarWindow::select);
    connect(m_monthView, &MonthView::dayDoubleClicked, this, &CalendarWindow::startNew);
    connect(m_monthView, &MonthView::moreClicked, this, [this](const QDate &day) {
        m_agendaButton->click();
        m_agendaView->scrollToDay(day);
    });
    connect(m_pane, &EventPane::editRequested, this, &CalendarWindow::startEdit);
    connect(m_pane, &EventPane::deleteRequested, this, &CalendarWindow::deleteSelected);
    connect(m_pane, &EventPane::saveRequested, this, &CalendarWindow::save);
    connect(m_pane, &EventPane::cancelRequested, this, &CalendarWindow::cancelEdit);
}

void CalendarWindow::buildActions()
{
    auto make = [this](const QString &name, const QString &text, const QKeySequence &key,
                       QWidget *scope, auto slot) {
        auto *a = new QAction(text, this);
        a->setObjectName(name);
        a->setShortcut(key);
        if (scope) {
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            scope->addAction(a);
        } else {
            addAction(a);
        }
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    QAction *newEvent = make(QStringLiteral("newEvent"), tr("&New event"),
                             QKeySequence(Qt::CTRL | Qt::Key_N), nullptr,
                             [this]() { startNew(defaultNewDay()); });
    // The spec's toolbar ends with + New, after the Month | Agenda toggle.
    // buildUi() has already placed every widget there, so appending now lands
    // it last.
    if (auto *bar = findChild<QToolBar *>(QStringLiteral("calendarToolbar")))
        bar->addAction(newEvent);
    QAction *close = make(QStringLiteral("closeCalendar"), tr("&Close"),
                          QKeySequence(Qt::CTRL | Qt::Key_W), nullptr, [this]() { this->close(); });
    QAction *edit = make(QStringLiteral("editEvent"), tr("&Edit event"),
                         QKeySequence(Qt::CTRL | Qt::Key_E), nullptr, &CalendarWindow::startEdit);
    QAction *del = make(QStringLiteral("deleteEvent"), tr("&Delete event"),
                        QKeySequence(Qt::Key_Delete), m_views, &CalendarWindow::deleteSelected);
    QAction *cancel = make(QStringLiteral("cancelEdit"), tr("Cancel &editing"),
                           QKeySequence(Qt::Key_Escape), m_pane, &CalendarWindow::cancelEdit);

    QAction *undo = m_undo.createUndoAction(this, tr("&Undo"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_views->addAction(undo);
    QAction *redo = m_undo.createRedoAction(this, tr("&Redo"));
    redo->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_views->addAction(redo);

    QAction *prev = make(QStringLiteral("previousMonth"), tr("&Previous month"),
                         QKeySequence(Qt::Key_PageUp), m_views, [this]() {
        const QDate d = QDate(m_year, m_month, 1).addMonths(-1);
        showMonth(d.year(), d.month());
    });
    QAction *next = make(QStringLiteral("nextMonth"), tr("&Next month"),
                         QKeySequence(Qt::Key_PageDown), m_views, [this]() {
        const QDate d = QDate(m_year, m_month, 1).addMonths(1);
        showMonth(d.year(), d.month());
    });
    QAction *today = make(QStringLiteral("today"), tr("&Today"),
                          QKeySequence(Qt::Key_Home), m_views, [this]() {
        showMonth(QDate::currentDate().year(), QDate::currentDate().month());
    });
    QAction *month = make(QStringLiteral("monthView"), tr("&Month"), {}, nullptr,
                          [this]() { m_monthButton->click(); });
    QAction *agenda = make(QStringLiteral("agendaView"), tr("&Agenda"), {}, nullptr,
                           [this]() { m_agendaButton->click(); });

    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(newEvent);
    file->addSeparator();
    file->addAction(close);
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(undo);
    editMenu->addAction(redo);
    editMenu->addSeparator();
    editMenu->addAction(edit);
    editMenu->addAction(del);
    editMenu->addAction(cancel);
    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(month);
    view->addAction(agenda);
    view->addSeparator();
    view->addAction(prev);
    view->addAction(next);
    view->addAction(today);
}

/// Where + New puts an event: today when today is in the shown month,
/// otherwise the month's first day, so the new event is on screen.
QDate CalendarWindow::defaultNewDay() const
{
    const QDate first(m_year, m_month, 1);
    const QDate today = QDate::currentDate();
    return today >= first && today < first.addMonths(1) ? today : first;
}

void CalendarWindow::showMonth(int year, int month)
{
    m_year = year;
    m_month = month;
    m_monthView->setMonth(year, month);
    refreshToolbar();
    rebuildItems();
}

void CalendarWindow::refreshToolbar()
{
    const QSignalBlocker a(m_monthBox), b(m_yearSpin);
    m_monthBox->setCurrentIndex(m_month - 1);
    m_yearSpin->setValue(m_year);
}

void CalendarWindow::reload()
{
    m_data = CalendarStore::load(m_config.calendarsDir());

    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    QStringList dirs{ m_config.calendarsDir() };
    for (const CalCollection &c : m_data.collections)
        dirs << c.path;
    m_watcher.addPaths(dirs);

    const QString current = m_collectionBox->currentData().toString();
    {
        const QSignalBlocker block(m_collectionBox);
        m_collectionBox->clear();
        m_collectionBox->addItem(tr("All calendars"), QString());
        for (const CalCollection &c : m_data.collections)
            m_collectionBox->addItem(swatch(c.color), c.displayName, c.dir);
        m_collectionBox->setCurrentIndex(qMax(0, m_collectionBox->findData(current)));
    }

    QStringList warnings;
    if (m_data.unparsable)
        warnings << tr("%n file(s) could not be read", nullptr, m_data.unparsable);
    if (m_data.unknownZones)
        warnings << tr("%n event(s) in an unknown time zone, shown in local time", nullptr,
                       m_data.unknownZones);
    m_dataWarning->setText(warnings.join(QStringLiteral("; ")));
    rebuildItems();
}

void CalendarWindow::rebuildItems()
{
    const QString filter = m_collectionBox->currentData().toString();
    const QDateTime from(m_monthView->firstDay(), QTime(0, 0));
    const QDateTime to(m_monthView->lastDay(), QTime(0, 0));
    m_items.clear();
    for (const Occurrence &o : CalendarStore::occurrences(m_data.events, from, to)) {
        const CalEvent &e = m_data.events[o.eventIndex];
        if (!filter.isEmpty() && e.collectionDir != filter)
            continue;
        const CalCollection *c = collection(e.collectionDir);
        QString title = o.isOverride ? e.overrides[o.overrideIndex].summary : e.summary;
        if (title.isEmpty())
            title = tr("(no title)");
        m_items.append({ o, title, c ? c->color : QColor(Qt::gray) });
    }
    m_monthView->setItems(m_items);

    // The agenda covers the month the toolbar names, not the grid's
    // spill-over. Out-of-month items become blanks, which AgendaView skips,
    // so a row's index is the same index the month grid reports.
    // ponytail: blanks rather than an index map; add the map if the agenda
    // ever needs a range of its own.
    const QDate first(m_year, m_month, 1);
    QList<CalendarItem> agenda;
    for (const CalendarItem &item : m_items) {
        const QDate d = item.occurrence.start.toLocalTime().date();
        agenda.append(d >= first && d < first.addMonths(1) ? item : CalendarItem{});
    }
    m_agendaView->setItems(agenda);

    // Re-find the selection by identity: a reload renumbers everything.
    m_selected = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        const Occurrence &o = m_items[i].occurrence;
        if (m_data.events[o.eventIndex].uid == m_selectedUid
            && (!m_selectedStart.isValid() || o.start == m_selectedStart)) {
            m_selected = i;
            break;
        }
    }
    m_monthView->setSelected(m_selected);
    m_agendaView->setSelected(m_selected);
    if (!m_pane->isEditing()) {
        if (m_selected >= 0) {
            const Occurrence &o = m_items[m_selected].occurrence;
            const CalEvent &e = m_data.events[o.eventIndex];
            const CalCollection *c = collection(e.collectionDir);
            m_pane->showDetails(e, c ? *c : CalCollection{}, o,
                                c && CalendarStore::isEditable(e, *c, m_ownAddresses));
        } else {
            m_pane->showNothing();
        }
    }
}

void CalendarWindow::select(int item)
{
    if (m_pane->isEditing()) {
        status(tr("Save or cancel the event being edited first."));
        return;
    }
    if (item < 0 || item >= m_items.size())
        return;
    const Occurrence &o = m_items[item].occurrence;
    m_selectedUid = m_data.events[o.eventIndex].uid;
    m_selectedStart = o.start;
    rebuildItems();
}

bool CalendarWindow::selectEvent(const QString &uid, const QDateTime &start)
{
    if (m_pane->isEditing())
        return false;
    for (int i = 0; i < m_items.size(); ++i) {
        const Occurrence &o = m_items[i].occurrence;
        if (m_data.events[o.eventIndex].uid == uid && (!start.isValid() || o.start == start)) {
            select(i);
            return true;
        }
    }
    return false;
}

QString CalendarWindow::selectedUid() const
{
    return m_selected >= 0 ? m_data.events[m_items[m_selected].occurrence.eventIndex].uid : QString();
}

bool CalendarWindow::isEditing() const { return m_pane->isEditing(); }

const CalCollection *CalendarWindow::collection(const QString &dir) const
{
    for (const CalCollection &c : m_data.collections)
        if (c.dir == dir)
            return &c;
    return nullptr;
}

QList<CalCollection> CalendarWindow::writableCollections() const
{
    QList<CalCollection> result;
    for (const CalCollection &c : m_data.collections)
        if (!c.readOnly)
            result.append(c);
    return result;
}

QString CalendarWindow::defaultCollection() const
{
    const QList<CalCollection> writable = writableCollections();
    for (const CalCollection &c : writable)
        if (c.dir == m_config.defaultCalendar())
            return c.dir;
    return writable.isEmpty() ? QString() : writable.first().dir;
}

void CalendarWindow::startEdit()
{
    if (m_pane->isEditing() || m_selected < 0)
        return;
    const Occurrence &o = m_items[m_selected].occurrence;
    const CalEvent &e = m_data.events[o.eventIndex];
    const CalCollection *c = collection(e.collectionDir);
    if (!c || !CalendarStore::isEditable(e, *c, m_ownAddresses))
        return;

    // A repeating event asks its scope when Edit is pressed, never after.
    m_editWholeSeries = true;
    m_editRecurrenceId = {};
    if (e.repeat.freq != RepeatRule::Freq::None || e.repeat.custom) {
        QMessageBox box(QMessageBox::Question, tr("Edit a repeating event"),
                        tr("Edit only this occurrence, or every occurrence?"),
                        QMessageBox::Cancel, this);
        QPushButton *one = box.addButton(tr("This occurrence"), QMessageBox::AcceptRole);
        QPushButton *all = box.addButton(tr("All occurrences"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() != one && box.clickedButton() != all)
            return;
        m_editWholeSeries = box.clickedButton() == all;
        m_editRecurrenceId = o.recurrenceId;
    }

    EventEdit initial;
    const CalOverride *ov = o.isOverride ? &e.overrides[o.overrideIndex] : nullptr;
    initial.summary = ov ? ov->summary : e.summary;
    initial.location = ov ? ov->location : e.location;
    initial.description = ov ? ov->description : e.description;
    initial.allDay = m_editWholeSeries ? e.allDay : o.allDay;
    initial.start = m_editWholeSeries ? e.start : o.start;
    initial.end = m_editWholeSeries ? e.end : o.end;
    initial.repeat = e.repeat;
    initial.collectionDir = e.collectionDir;

    QStringList people;
    for (const CalPerson &a : e.attendees)
        people << (a.name.isEmpty() ? a.address : a.name);

    m_editPath = e.filePath;
    m_editBase = e.rawText;
    m_pane->startEdit(initial, writableCollections(), m_editWholeSeries, m_editWholeSeries,
                      e.hasAlarm ? tr("Kept as set elsewhere") : tr("None"),
                      people.join(QStringLiteral(", ")));
}

void CalendarWindow::startNew(const QDate &day)
{
    if (m_pane->isEditing() || defaultCollection().isEmpty())
        return;
    EventEdit initial;
    initial.start = QDateTime(day, QTime(9, 0));
    initial.end = initial.start.addSecs(3600);
    initial.collectionDir = defaultCollection();
    m_editPath.clear();
    m_editBase.clear();
    m_editWholeSeries = true;
    m_editRecurrenceId = {};
    m_pane->startEdit(initial, writableCollections(), true, true, tr("None"), QString());
}

bool CalendarWindow::save()
{
    const EventEdit edit = m_pane->edit();
    const CalCollection *target = collection(edit.collectionDir);
    if (!target)
        return false;
    QList<Change> changes;
    QString label;

    if (m_editPath.isEmpty()) {
        const QByteArray text = CalendarStore::newEvent(edit, QTimeZone::systemTimeZoneId());
        bool ignored = false;
        const QString uid = CalendarStore::parseEvent(text, {}, {}, &ignored).uid;
        changes.append({ target->path + QLatin1Char('/') + uid + QStringLiteral(".ics"),
                         std::nullopt, text });
        label = tr("New event");
    } else {
        const QByteArray text = CalendarStore::applyEdit(
            m_editBase, edit,
            m_editWholeSeries ? CalendarStore::Scope::All : CalendarStore::Scope::ThisOccurrence,
            m_editRecurrenceId);
        if (text.isEmpty())
            return false;
        const QString newPath = target->path + QLatin1Char('/') + QFileInfo(m_editPath).fileName();
        if (newPath == m_editPath) {
            changes.append({ m_editPath, m_editBase, text });
        } else {
            // Moving to another calendar: the new file first, then the old
            // one removed, so a failure between them duplicates rather than
            // loses the event.
            changes.append({ newPath, std::nullopt, text });
            changes.append({ m_editPath, m_editBase, std::nullopt });
        }
        label = tr("Edit event");
    }

    if (!writeAndRecord(label, changes)) {
        // The spec promises a stale save can be checked and tried again. The
        // write refused because the file moved under the form, so rebase the
        // form's bytes on what is on disk NOW; a deliberate second Save then
        // proceeds and overwrites with the user's values. A file that is gone
        // is left alone, so Save keeps refusing and Cancel is the way out.
        if (m_lastStale && !m_editPath.isEmpty() && QFile::exists(m_editPath))
            m_editBase = readFile(m_editPath);
        return false;  // the form stays open with the user's values
    }
    m_pane->stopEdit();
    bool ignored = false;
    m_selectedUid = CalendarStore::parseEvent(*changes.first().after, {}, {}, &ignored).uid;
    m_selectedStart = {};
    reload();
    return true;
}

void CalendarWindow::cancelEdit()
{
    if (!m_pane->isEditing())
        return;
    m_pane->stopEdit();
    rebuildItems();
}

void CalendarWindow::deleteSelected()
{
    if (m_pane->isEditing() || m_selected < 0)
        return;
    const Occurrence &o = m_items[m_selected].occurrence;
    const CalEvent &e = m_data.events[o.eventIndex];
    const CalCollection *c = collection(e.collectionDir);
    if (!c || !CalendarStore::isEditable(e, *c, m_ownAddresses))
        return;

    // No confirmation: the delete is undoable (spec decision 14). A repeating
    // event asks its SCOPE, which is a choice, not a confirmation.
    std::optional<QByteArray> after = std::nullopt;
    if (e.repeat.freq != RepeatRule::Freq::None || e.repeat.custom) {
        QMessageBox box(QMessageBox::Question, tr("Delete a repeating event"),
                        tr("Delete only this occurrence, or every occurrence?"),
                        QMessageBox::Cancel, this);
        QPushButton *one = box.addButton(tr("This occurrence"), QMessageBox::AcceptRole);
        QPushButton *all = box.addButton(tr("All occurrences"), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() == one) {
            const QByteArray removed = CalendarStore::deleteOccurrence(e.rawText, o.recurrenceId);
            // A parse failure returns empty; writing it would TRUNCATE the file
            // instead of removing one occurrence. Same guard as save().
            if (removed.isEmpty()) {
                status(tr("Could not remove this occurrence."));
                return;
            }
            after = removed;
        } else if (box.clickedButton() != all) {
            return;
        }
    }
    if (writeAndRecord(tr("Delete event"), { { e.filePath, e.rawText, after } })) {
        m_selectedUid.clear();
        reload();
    }
}

bool CalendarWindow::applyChanges(const QList<Change> &changes, bool reverse)
{
    QList<Change> done;
    for (int i = 0; i < changes.size(); ++i) {
        const Change &c = changes[reverse ? changes.size() - 1 - i : i];
        const auto from = reverse ? c.after : c.before;
        const auto to = reverse ? c.before : c.after;
        QString error;
        const CalendarWriter::Result r = CalendarWriter::replace(c.path, from, to, &error);
        if (r != CalendarWriter::Result::Ok) {
            m_lastStale = r == CalendarWriter::Result::Stale;
            // Roll back what this call already did, newest first.
            // ponytail: best effort; a rollback that itself fails is reported
            // by the file being wrong on the next reload, not handled here.
            for (int j = done.size() - 1; j >= 0; --j) {
                QString ignored;
                CalendarWriter::replace(done[j].path, done[j].after, done[j].before, &ignored);
            }
            status(r == CalendarWriter::Result::Stale
                       ? tr("The event changed on disk, probably from a sync. Check it and try again.")
                       : tr("Could not write the calendar: %1").arg(error));
            reload();
            return false;
        }
        done.append({ c.path, from, to });
        m_written.insert(c.path, to);
    }
    m_sync->schedule();
    m_reloadTimer.start();
    return true;
}

bool CalendarWindow::writeAndRecord(const QString &label, const QList<Change> &changes)
{
    if (!applyChanges(changes, false))
        return false;
    m_undo.push(new FileCommand(label, changes, this));
    return true;
}

void CalendarWindow::checkAfterSync()
{
    // Compared by MEANING: the server may normalise the text on the round
    // trip, and a byte comparison would warn on every save.
    QStringList lost;
    for (auto it = m_checking.cbegin(); it != m_checking.cend(); ++it) {
        const bool exists = QFile::exists(it.key());
        const bool kept = it.value()
            ? exists && CalendarStore::sameMeaning(readFile(it.key()), *it.value())
            : !exists;
        if (!kept) {
            bool ignored = false;
            const QString title = it.value()
                ? CalendarStore::parseEvent(*it.value(), {}, {}, &ignored).summary
                : QFileInfo(it.key()).completeBaseName();
            lost << title;
        }
    }
    m_checking.clear();
    status(lost.isEmpty()
               ? tr("Calendars synced.")
               : tr("The server kept a different version of: %1").arg(lost.join(QStringLiteral(", "))));
}

void CalendarWindow::status(const QString &text)
{
    statusBar()->showMessage(text, 10000);
}

void CalendarWindow::closeEvent(QCloseEvent *event)
{
    // A QMainWindow, not a QDialog, so every route out (the title bar, the
    // File menu, close()) arrives here; AGENTS.md's QDialog trap does not
    // apply, but the test still drives two routes.
    if (m_pane->isDirty()) {
        const auto answer = QMessageBox::question(
            this, tr("Unsaved event"), tr("Save the changes to this event?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            event->ignore();
            return;
        }
        m_pane->stopEdit();
    }
    QDir().mkpath(QFileInfo(m_uiStatePath).absolutePath());
    QSettings state(m_uiStatePath, QSettings::IniFormat);
    state.beginGroup(QStringLiteral("calendar"));
    state.setValue(QStringLiteral("geometry"), saveGeometry());
    state.setValue(QStringLiteral("agenda"), m_views->currentIndex() == 1);
    state.setValue(QStringLiteral("collection"), m_collectionBox->currentData().toString());
    state.endGroup();
    QMainWindow::closeEvent(event);
}
