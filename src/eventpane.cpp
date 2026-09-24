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

#include "eventpane.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QLabel *plainLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);  // untrusted: never AutoText
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QIcon swatch(const QColor &colour)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter(&pixmap).fillRect(pixmap.rect(), colour);
    return QIcon(pixmap);
}

bool sameEdit(const EventEdit &a, const EventEdit &b)
{
    return a.summary == b.summary && a.location == b.location
        && a.description == b.description && a.start == b.start && a.end == b.end
        && a.allDay == b.allDay && a.repeat == b.repeat && a.collectionDir == b.collectionDir;
}

constexpr int kCustomIndex = 5;

} // namespace

EventPane::EventPane(QWidget *parent) : QStackedWidget(parent)
{
    addWidget(new QWidget(this));   // 0: nothing selected
    addWidget(buildDetails());      // 1
    addWidget(buildForm());         // 2
    setMinimumWidth(260);
    showNothing();
}

QWidget *EventPane::buildDetails()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    m_title = plainLabel(page);
    QFont big = m_title->font();
    big.setBold(true);
    big.setPointSizeF(big.pointSizeF() > 0 ? big.pointSizeF() * 1.3 : big.pointSizeF());
    m_title->setFont(big);
    layout->addWidget(m_title);

    auto *form = new QFormLayout;
    m_when = plainLabel(page);
    m_repeatText = plainLabel(page);
    m_calendar = plainLabel(page);
    m_location = plainLabel(page);
    m_notes = plainLabel(page);
    m_reminder = plainLabel(page);
    m_people = plainLabel(page);
    form->addRow(tr("When"), m_when);
    form->addRow(tr("Repeats"), m_repeatText);
    form->addRow(tr("Calendar"), m_calendar);
    form->addRow(tr("Location"), m_location);
    form->addRow(tr("Notes"), m_notes);
    form->addRow(tr("Reminder"), m_reminder);
    form->addRow(tr("People"), m_people);
    layout->addLayout(form);

    m_notice = plainLabel(page);
    m_notice->setObjectName(QStringLiteral("eventNotice"));
    layout->addWidget(m_notice);

    auto *buttons = new QHBoxLayout;
    m_editButton = new QPushButton(tr("&Edit"), page);
    m_editButton->setObjectName(QStringLiteral("editEvent"));
    m_deleteButton = new QPushButton(tr("&Delete"), page);
    m_deleteButton->setObjectName(QStringLiteral("deleteEvent"));
    buttons->addWidget(m_editButton);
    buttons->addWidget(m_deleteButton);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addStretch();

    connect(m_editButton, &QPushButton::clicked, this, &EventPane::editRequested);
    connect(m_deleteButton, &QPushButton::clicked, this, &EventPane::deleteRequested);
    return page;
}

QWidget *EventPane::buildForm()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QFormLayout;

    m_titleEdit = new QLineEdit(page);
    m_titleEdit->setObjectName(QStringLiteral("eventTitle"));
    m_calendarBox = new QComboBox(page);
    m_calendarBox->setObjectName(QStringLiteral("eventCalendar"));
    m_allDay = new QCheckBox(tr("All day"), page);
    m_startEdit = new QDateTimeEdit(page);
    m_startEdit->setObjectName(QStringLiteral("eventStart"));
    m_startEdit->setCalendarPopup(true);
    m_endEdit = new QDateTimeEdit(page);
    m_endEdit->setObjectName(QStringLiteral("eventEnd"));
    m_endEdit->setCalendarPopup(true);
    m_locationEdit = new QLineEdit(page);
    m_notesEdit = new QPlainTextEdit(page);
    m_formReminder = plainLabel(page);
    m_formPeople = plainLabel(page);

    form->addRow(tr("Title"), m_titleEdit);
    form->addRow(tr("Calendar"), m_calendarBox);
    form->addRow(QString(), m_allDay);
    form->addRow(tr("Starts"), m_startEdit);
    form->addRow(tr("Ends"), m_endEdit);
    form->addRow(tr("Repeat"), buildRepeat());
    form->addRow(tr("Location"), m_locationEdit);
    form->addRow(tr("Notes"), m_notesEdit);
    form->addRow(tr("Reminder"), m_formReminder);
    form->addRow(tr("People"), m_formPeople);
    layout->addLayout(form);

    m_formNotice = plainLabel(page);
    layout->addWidget(m_formNotice);

    auto *buttons = new QHBoxLayout;
    auto *save = new QPushButton(tr("&Save"), page);
    save->setObjectName(QStringLiteral("saveEvent"));
    save->setDefault(true);
    auto *cancel = new QPushButton(tr("Cancel"), page);
    cancel->setObjectName(QStringLiteral("cancelEvent"));
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    connect(save, &QPushButton::clicked, this, &EventPane::saveRequested);
    connect(cancel, &QPushButton::clicked, this, &EventPane::cancelRequested);
    connect(m_allDay, &QCheckBox::toggled, this, &EventPane::refreshAllDay);
    // Keep the duration when the start moves, as every calendar does.
    connect(m_startEdit, &QDateTimeEdit::dateTimeChanged, this, [this](const QDateTime &start) {
        if (m_previousStart.isValid() && m_editing) {
            const qint64 length = m_previousStart.secsTo(m_endEdit->dateTime());
            m_endEdit->setDateTime(start.addSecs(qMax<qint64>(0, length)));
        }
        m_previousStart = start;
        proposeFromStart();
    });
    return page;
}

QWidget *EventPane::buildRepeat()
{
    m_repeatBox = new QWidget(this);
    auto *box = new QVBoxLayout(m_repeatBox);
    box->setContentsMargins(0, 0, 0, 0);
    const QLocale locale;

    auto *row = new QHBoxLayout;
    m_freq = new QComboBox(m_repeatBox);
    m_freq->setObjectName(QStringLiteral("eventRepeat"));
    m_freq->addItems({ tr("Does not repeat"), tr("Daily"), tr("Weekly"),
                       tr("Monthly"), tr("Yearly") });
    m_interval = new QSpinBox(m_repeatBox);
    m_interval->setRange(1, 99);
    m_interval->setPrefix(tr("every "));
    row->addWidget(m_freq);
    row->addWidget(m_interval);
    box->addLayout(row);

    m_weekdayRow = new QWidget(m_repeatBox);
    auto *days = new QHBoxLayout(m_weekdayRow);
    days->setContentsMargins(0, 0, 0, 0);
    for (int d = 1; d <= 7; ++d) {
        auto *check = new QCheckBox(locale.dayName(d, QLocale::NarrowFormat), m_weekdayRow);
        m_weekdays.append(check);
        days->addWidget(check);
    }
    box->addWidget(m_weekdayRow);

    auto *byRow = new QHBoxLayout;
    m_by = new QComboBox(m_repeatBox);
    m_by->addItems({ tr("on day"), tr("on the") });
    m_monthDay = new QSpinBox(m_repeatBox);
    m_monthDay->setRange(1, 31);
    m_ordinal = new QComboBox(m_repeatBox);
    m_ordinal->addItem(tr("first"), 1);
    m_ordinal->addItem(tr("second"), 2);
    m_ordinal->addItem(tr("third"), 3);
    m_ordinal->addItem(tr("fourth"), 4);
    m_ordinal->addItem(tr("last"), -1);
    m_weekday = new QComboBox(m_repeatBox);
    for (int d = 1; d <= 7; ++d)
        m_weekday->addItem(locale.dayName(d), d);
    m_month = new QComboBox(m_repeatBox);
    for (int m = 1; m <= 12; ++m)
        m_month->addItem(tr("of %1").arg(locale.monthName(m)), m);
    byRow->addWidget(m_by);
    byRow->addWidget(m_monthDay);
    byRow->addWidget(m_ordinal);
    byRow->addWidget(m_weekday);
    byRow->addWidget(m_month);
    box->addLayout(byRow);

    auto *endRow = new QHBoxLayout;
    m_end = new QComboBox(m_repeatBox);
    m_end->addItems({ tr("forever"), tr("until"), tr("for") });
    m_until = new QDateEdit(m_repeatBox);
    m_until->setCalendarPopup(true);
    m_count = new QSpinBox(m_repeatBox);
    m_count->setRange(1, 999);
    m_count->setSuffix(tr(" times"));
    endRow->addWidget(m_end);
    endRow->addWidget(m_until);
    endRow->addWidget(m_count);
    box->addLayout(endRow);

    m_customLabel = new QLabel(tr("Custom rule, kept as is"), m_repeatBox);
    box->addWidget(m_customLabel);

    for (QComboBox *combo : { m_freq, m_by, m_end })
        connect(combo, &QComboBox::currentIndexChanged, this, &EventPane::refreshRepeatVisibility);
    connect(m_freq, &QComboBox::currentIndexChanged, this, &EventPane::proposeFromStart);
    return m_repeatBox;
}

void EventPane::refreshRepeatVisibility()
{
    const int freq = m_freq->currentIndex();
    const bool custom = freq == kCustomIndex;
    const bool repeats = freq > 0 && !custom;
    const bool monthly = freq == 3, yearly = freq == 4;
    const bool byWeekday = m_by->currentIndex() == 1;
    m_interval->setVisible(repeats);
    m_weekdayRow->setVisible(freq == 2);
    m_by->setVisible(monthly || yearly);
    // Yearly "on day" means on the start date, which needs no control.
    m_monthDay->setVisible(monthly && !byWeekday);
    m_ordinal->setVisible((monthly || yearly) && byWeekday);
    m_weekday->setVisible((monthly || yearly) && byWeekday);
    m_month->setVisible(yearly && byWeekday);
    m_end->setVisible(repeats);
    m_until->setVisible(repeats && m_end->currentIndex() == 1);
    m_count->setVisible(repeats && m_end->currentIndex() == 2);
    m_customLabel->setVisible(custom);
}

/// Re-proposes the day-dependent defaults from the start date, as Thunderbird
/// does: "on day 24", or "the fourth Thursday" (the fifth becomes "last").
void EventPane::proposeFromStart()
{
    const QDate start = m_startEdit->date();
    m_monthDay->setValue(start.day());
    const int ordinal = (start.day() - 1) / 7 + 1;
    m_ordinal->setCurrentIndex(m_ordinal->findData(ordinal > 4 ? -1 : ordinal));
    m_weekday->setCurrentIndex(m_weekday->findData(start.dayOfWeek()));
    m_month->setCurrentIndex(start.month() - 1);
    if (m_freq->currentIndex() == 2
        && std::none_of(m_weekdays.cbegin(), m_weekdays.cend(),
                        [](QCheckBox *c) { return c->isChecked(); }))
        m_weekdays[start.dayOfWeek() - 1]->setChecked(true);
}

void EventPane::setRepeat(const RepeatRule &rule)
{
    if (m_freq->count() > kCustomIndex)
        m_freq->removeItem(kCustomIndex);
    m_customRule = RepeatRule();
    if (rule.custom) {
        m_customRule = rule;
        m_freq->addItem(tr("Custom rule"));
        m_freq->setCurrentIndex(kCustomIndex);
        refreshRepeatVisibility();
        return;
    }
    m_freq->setCurrentIndex(static_cast<int>(rule.freq));
    proposeFromStart();
    m_interval->setValue(rule.interval);
    for (int d = 0; d < 7; ++d)
        m_weekdays[d]->setChecked(rule.weekdays.contains(d + 1));
    m_by->setCurrentIndex(rule.by == RepeatRule::By::Weekday ? 1 : 0);
    if (rule.monthDay)
        m_monthDay->setValue(rule.monthDay);
    if (rule.ordinal)
        m_ordinal->setCurrentIndex(m_ordinal->findData(rule.ordinal));
    if (rule.weekday)
        m_weekday->setCurrentIndex(m_weekday->findData(rule.weekday));
    if (rule.month)
        m_month->setCurrentIndex(rule.month - 1);
    m_end->setCurrentIndex(static_cast<int>(rule.end));
    m_until->setDate(rule.until.isValid() ? rule.until : m_startEdit->date().addMonths(1));
    m_count->setValue(qMax(1, rule.count));
    refreshRepeatVisibility();
}

RepeatRule EventPane::repeat() const
{
    if (m_freq->currentIndex() == kCustomIndex)
        return m_customRule;
    RepeatRule rule;
    rule.freq = static_cast<RepeatRule::Freq>(m_freq->currentIndex());
    if (rule.freq == RepeatRule::Freq::None)
        return rule;
    rule.interval = m_interval->value();
    rule.wkst = m_initial.repeat.wkst;  // carried through, spec
    if (rule.freq == RepeatRule::Freq::Weekly)
        for (int d = 0; d < 7; ++d)
            if (m_weekdays[d]->isChecked())
                rule.weekdays.append(d + 1);
    if (rule.freq == RepeatRule::Freq::Monthly || rule.freq == RepeatRule::Freq::Yearly) {
        rule.by = m_by->currentIndex() == 1 ? RepeatRule::By::Weekday : RepeatRule::By::MonthDay;
        if (rule.by == RepeatRule::By::Weekday) {
            rule.ordinal = m_ordinal->currentData().toInt();
            rule.weekday = m_weekday->currentData().toInt();
            if (rule.freq == RepeatRule::Freq::Yearly)
                rule.month = m_month->currentData().toInt();
        } else if (rule.freq == RepeatRule::Freq::Monthly) {
            rule.monthDay = m_monthDay->value();
        }
    }
    rule.end = static_cast<RepeatRule::End>(m_end->currentIndex());
    if (rule.end == RepeatRule::End::Until)
        rule.until = m_until->date();
    else if (rule.end == RepeatRule::End::Count)
        rule.count = m_count->value();
    return rule;
}

void EventPane::refreshAllDay()
{
    // All-day shows DATES, and the end is the LAST day, inclusive: what a
    // person means by "24 to 25 September". edit() converts back to the
    // exclusive end iCalendar needs.
    const QString format = m_allDay->isChecked()
        ? QLocale().dateFormat(QLocale::ShortFormat)
        : QLocale().dateTimeFormat(QLocale::ShortFormat);
    m_startEdit->setDisplayFormat(format);
    m_endEdit->setDisplayFormat(format);
}

void EventPane::showNothing()
{
    m_editing = false;
    setCurrentIndex(0);
}

void EventPane::showDetails(const CalEvent &event, const CalCollection &collection,
                            const Occurrence &occurrence, bool editable)
{
    m_editing = false;
    const QLocale locale;
    const CalOverride *ov = occurrence.isOverride ? &event.overrides[occurrence.overrideIndex] : nullptr;
    const QString summary = ov ? ov->summary : event.summary;
    m_title->setText(summary.isEmpty() ? tr("(no title)") : summary);

    const QDateTime start = occurrence.start.toLocalTime(), end = occurrence.end.toLocalTime();
    if (occurrence.allDay) {
        const QDate last = end.date().addDays(-1);
        m_when->setText(last > start.date()
            ? tr("%1 to %2, all day").arg(locale.toString(start.date(), QLocale::LongFormat),
                                          locale.toString(last, QLocale::LongFormat))
            : tr("%1, all day").arg(locale.toString(start.date(), QLocale::LongFormat)));
    } else {
        m_when->setText(tr("%1, %2 - %3").arg(locale.toString(start.date(), QLocale::LongFormat),
                                             locale.toString(start.time(), QLocale::ShortFormat),
                                             locale.toString(end.time(), QLocale::ShortFormat)));
    }
    m_repeatText->setText(event.repeat.freq == RepeatRule::Freq::None && !event.repeat.custom
                              ? tr("Does not repeat") : event.repeat.describe());
    m_calendar->setText(collection.displayName);
    m_location->setText(ov ? ov->location : event.location);
    m_notes->setText(ov ? ov->description : event.description);
    m_reminder->setText(event.hasAlarm ? tr("Yes, kept as set elsewhere") : tr("None"));

    QStringList people;
    if (!event.organizer.address.isEmpty())
        people << tr("Organiser: %1").arg(event.organizer.name.isEmpty()
                                              ? event.organizer.address : event.organizer.name);
    for (const CalPerson &a : event.attendees)
        people << QStringLiteral("%1 (%2)").arg(a.name.isEmpty() ? a.address : a.name,
                                                 a.partstat.isEmpty() ? tr("no reply") : a.partstat.toLower());
    m_people->setText(people.join(QLatin1Char('\n')));

    QString notice;
    if (collection.readOnly)
        notice = tr("Read-only calendar.");
    else if (!editable)
        notice = tr("Organised by %1: read-only.").arg(event.organizer.name.isEmpty()
                                                           ? event.organizer.address : event.organizer.name);
    m_notice->setText(notice);
    m_notice->setVisible(!notice.isEmpty());
    // Absent, not disabled (AGENTS.md precedent).
    m_editButton->setVisible(editable);
    m_deleteButton->setVisible(editable);
    setCurrentIndex(1);
}

void EventPane::startEdit(const EventEdit &initial, const QList<CalCollection> &collections,
                          bool repeatEditable, bool collectionEditable,
                          const QString &reminderText, const QString &attendeesText)
{
    m_initial = initial;
    m_editing = false;  // no duration-keeping while the fields are filled
    m_calendarBox->clear();
    for (const CalCollection &c : collections)
        m_calendarBox->addItem(swatch(c.color), c.displayName, c.dir);
    m_calendarBox->setCurrentIndex(qMax(0, m_calendarBox->findData(initial.collectionDir)));
    m_calendarBox->setEnabled(collectionEditable);

    m_previousStart = QDateTime();
    m_titleEdit->setText(initial.summary);
    m_allDay->setChecked(initial.allDay);
    refreshAllDay();
    m_startEdit->setDateTime(initial.start.toLocalTime());
    m_endEdit->setDateTime(initial.allDay ? initial.end.toLocalTime().addDays(-1)
                                          : initial.end.toLocalTime());
    m_locationEdit->setText(initial.location);
    m_notesEdit->setPlainText(initial.description);
    setRepeat(initial.repeat);
    m_repeatBox->setEnabled(repeatEditable);
    m_formReminder->setText(reminderText);
    m_formPeople->setText(attendeesText);
    m_formNotice->setText(attendeesText.isEmpty() ? QString()
                              : tr("Attendees have not been notified of changes."));
    m_formNotice->setVisible(!attendeesText.isEmpty());

    // The form rounds what it shows (seconds, the display format), so the
    // baseline for isDirty() is what the form PRODUCES, not what came in.
    m_initial = edit();
    m_editing = true;
    setCurrentIndex(2);
    m_titleEdit->setFocus();
}

void EventPane::stopEdit() { m_editing = false; }
bool EventPane::isEditing() const { return m_editing; }
bool EventPane::isDirty() const { return m_editing && !sameEdit(edit(), m_initial); }

EventEdit EventPane::edit() const
{
    EventEdit e;
    e.summary = m_titleEdit->text().trimmed();
    e.location = m_locationEdit->text().trimmed();
    e.description = m_notesEdit->toPlainText();
    e.allDay = m_allDay->isChecked();
    e.collectionDir = m_calendarBox->currentData().toString();
    if (e.allDay) {
        e.start = QDateTime(m_startEdit->date(), QTime(0, 0));
        e.end = QDateTime(qMax(m_endEdit->date(), m_startEdit->date()).addDays(1), QTime(0, 0));
    } else {
        e.start = m_startEdit->dateTime();
        e.end = qMax(m_endEdit->dateTime(), e.start);
    }
    e.repeat = repeat();
    return e;
}
