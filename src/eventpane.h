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

#pragma once

#include "caltypes.h"

#include <QStackedWidget>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDateTimeEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

/// The calendar window's right-hand pane: a read-only page for the selected
/// event, and the edit form that replaces it (spec decision 11).
///
/// Every value from a file is shown in a Qt::PlainText label: SUMMARY,
/// DESCRIPTION, LOCATION and attendee names come from strangers once
/// invitations arrive, which is MessageDetailsDialog's rule.
class EventPane : public QStackedWidget
{
    Q_OBJECT

public:
    explicit EventPane(QWidget *parent = nullptr);

    void showNothing();
    void showDetails(const CalEvent &event, const CalCollection &collection,
                     const Occurrence &occurrence, bool editable);

    /// Opens the form. `collections` are the writable ones, for the Calendar
    /// combo; `repeatEditable` is false for a single occurrence, which has no
    /// rule of its own; `collectionEditable` is false for the same reason.
    void startEdit(const EventEdit &initial, const QList<CalCollection> &collections,
                   bool repeatEditable, bool collectionEditable,
                   const QString &reminderText, const QString &attendeesText);
    void stopEdit();

    bool isEditing() const;
    bool isDirty() const;
    EventEdit edit() const;

signals:
    void editRequested();
    void deleteRequested();
    void saveRequested();
    void cancelRequested();

private:
    QWidget *buildDetails();
    QWidget *buildForm();
    QWidget *buildRepeat();
    void setRepeat(const RepeatRule &rule);
    RepeatRule repeat() const;
    void proposeFromStart();
    void refreshRepeatVisibility();
    void refreshAllDay();

    // Details page.
    QLabel *m_title;
    QLabel *m_when;
    QLabel *m_repeatText;
    QLabel *m_calendar;
    QLabel *m_calendarSwatch;
    QLabel *m_location;
    QLabel *m_notes;
    QLabel *m_reminder;
    QLabel *m_people;
    QLabel *m_notice;
    QPushButton *m_editButton;
    QPushButton *m_deleteButton;

    // Form.
    QLineEdit *m_titleEdit;
    QComboBox *m_calendarBox;
    QCheckBox *m_allDay;
    QDateTimeEdit *m_startEdit;
    QDateTimeEdit *m_endEdit;
    QLineEdit *m_locationEdit;
    QPlainTextEdit *m_notesEdit;
    QLabel *m_formReminder;
    QLabel *m_formPeople;
    QLabel *m_formNotice;

    // Repeat control.
    QWidget *m_repeatBox;
    QComboBox *m_freq;          ///< None, Daily, Weekly, Monthly, Yearly, [Custom]
    QSpinBox *m_interval;
    QWidget *m_weekdayRow;
    QList<QCheckBox *> m_weekdays;  ///< Monday first, index = Qt::DayOfWeek - 1
    QComboBox *m_by;            ///< "on day N" / "on the Nth weekday"
    QSpinBox *m_monthDay;
    QComboBox *m_ordinal;       ///< first..fourth, last
    QComboBox *m_weekday;
    QComboBox *m_month;
    QComboBox *m_end;           ///< never, on a date, after N times
    QDateEdit *m_until;
    QSpinBox *m_count;
    QLabel *m_customLabel;

    EventEdit m_initial;
    RepeatRule m_customRule;    ///< Kept when the rule read was custom.
    QDateTime m_previousStart;  ///< For keeping the duration as the start moves.
    bool m_editing = false;
};
