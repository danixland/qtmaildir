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
#include "config.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QMainWindow>
#include <QTimer>
#include <QUndoStack>

#include <optional>

class AgendaView;
class CalendarSync;
class EventPane;
class MonthView;
class QComboBox;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QToolButton;

/// The calendar (item 206): its own top-level window, like ComposeWindow.
/// Spec: docs/superpowers/specs/2026-09-24-calendar-design.md.
class CalendarWindow : public QMainWindow
{
    Q_OBJECT

public:
    CalendarWindow(const Config &config, const QStringList &ownAddresses,
                   const QString &uiStatePath, QWidget *parent = nullptr);
    ~CalendarWindow() override;

    void showMonth(int year, int month);
    /// Selects an event's occurrence (the first in view when `start` is
    /// invalid). False when not found, or refused by the edit lock.
    bool selectEvent(const QString &uid, const QDateTime &start = {});
    QString selectedUid() const;
    bool isEditing() const;
    QUndoStack *undoStack() { return &m_undo; }

    /// One file's before and after, either absent. What a write and its undo
    /// are made of.
    struct Change
    {
        QString path;
        std::optional<QByteArray> before;
        std::optional<QByteArray> after;
    };
    /// Applies `changes` in order through CalendarWriter. On a failure the
    /// ones already applied are rolled back and the reason is reported.
    bool applyChanges(const QList<Change> &changes, bool reverse);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildActions();
    void reload();
    void rebuildItems();
    void refreshToolbar();
    void select(int item);
    void startEdit();
    void startNew(const QDate &day);
    QDate defaultNewDay() const;
    bool save();
    void cancelEdit();
    void deleteSelected();
    bool writeAndRecord(const QString &label, const QList<Change> &changes);
    void checkAfterSync();
    const CalCollection *collection(const QString &dir) const;
    QList<CalCollection> writableCollections() const;
    QString defaultCollection() const;
    void status(const QString &text);

    Config m_config;
    QStringList m_ownAddresses;
    QString m_uiStatePath;

    LoadResult m_data;
    QList<CalendarItem> m_items;
    int m_year;
    int m_month;
    int m_selected = -1;
    QString m_selectedUid;         ///< survives a reload
    QDateTime m_selectedStart;

    // The open edit. m_editPath empty with m_editing: a new event.
    QString m_editPath;
    QByteArray m_editBase;
    bool m_editWholeSeries = true;
    QDateTime m_editRecurrenceId;

    QUndoStack m_undo;
    CalendarSync *m_sync;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
    /// Written since the last sync started: path to the text written, or
    /// nullopt for a deletion. Checked by meaning after the next sync.
    QHash<QString, std::optional<QByteArray>> m_written;
    QHash<QString, std::optional<QByteArray>> m_checking;

    QComboBox *m_collectionBox;
    QComboBox *m_monthBox;
    QSpinBox *m_yearSpin;
    QToolButton *m_monthButton;
    QToolButton *m_agendaButton;
    QStackedWidget *m_views;
    MonthView *m_monthView;
    AgendaView *m_agendaView;
    EventPane *m_pane;
    QLabel *m_dataWarning;
};
