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

#include <QDialog>
#include <QVector>

#include "types.h"

/// What one line of the dialog shows.
///
/// Separate from PendingChange because the two answer different questions.
/// PendingChange is what is outstanding; this is what is drawn, and the
/// difference is the grouping: a message with several actions contributes
/// several rows here, only the first of which carries a subject.
struct PendingChangeRow
{
    /// The subject, drawn only on the first row of a run sharing one id.
    /// Empty on the rows beneath it, which is what puts the actions under
    /// their message rather than beside a repeated subject.
    QString subject;

    /// What the user did. Every row has one; this is the point of the list.
    QString action;

    /// True when this row opens a new message, i.e. when `subject` is drawn.
    /// Carried explicitly rather than inferred from a non-empty subject: a
    /// message whose id no longer resolves has an EMPTY subject and still
    /// opens a run of its own.
    bool startsMessage = false;

    /// How many messages a thread row covered, or -1 for a message row.
    int messageCount = -1;
};

/// The list behind the unsynced-changes count (item 119).
///
/// Read-only, deliberately. This is an information window, not a place to
/// retry or discard a change: either would be a new mutation path with its own
/// undo question, and the count exists to answer "is my work safe to quit on"
/// rather than to be edited.
///
/// A SNAPSHOT. The rows are built once, when the user opens it, and never
/// refreshed underneath them: a dialog left open for twenty minutes shows what
/// was true when it was opened, which is what the user clicked on.
///
/// Rows are exposed so the grouping can be asserted without rendering
/// anything, which is how MessageDetailsDialog is tested and for the same
/// reason: a pixel probe cannot tell a correct layout from a plausible one.
class PendingChangesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PendingChangesDialog(const QVector<PendingChange> &changes,
                                  QWidget *parent = nullptr);

    /// The lines on display, in order. Exposed for testing without rendering.
    QVector<PendingChangeRow> rows() const { return m_rows; }

    /// Groups the changes into display rows: a subject on the first row of
    /// each run sharing an id, the actions beneath it.
    ///
    /// Static and value-in, value-out so the grouping is testable with no
    /// widget at all.
    static QVector<PendingChangeRow> rowsFor(
        const QVector<PendingChange> &changes);

private:
    QVector<PendingChangeRow> m_rows;
};
