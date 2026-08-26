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

#include "pendingchangesdialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

QVector<PendingChangeRow> PendingChangesDialog::rowsFor(
    const QVector<PendingChange> &changes)
{
    QVector<PendingChangeRow> rows;
    rows.reserve(changes.size());

    // A run is a stretch of changes sharing one id, which the snapshot has
    // already grouped. Only the first row of a run carries a subject, so the
    // actions read as belonging to the message above them.
    //
    // Compared against the PREVIOUS id rather than collected into a map: the
    // snapshot's order is deliberate (the actions under one message keep the
    // order they were made in), and a map would discard it.
    QString previousId;
    bool first = true;
    for (const PendingChange &change : changes) {
        const bool startsMessage = first || change.id != previousId;
        rows.append(PendingChangeRow{
            startsMessage ? change.subject : QString(),
            change.action,
            startsMessage,
            startsMessage ? change.messageCount : -1 });
        previousId = change.id;
        first = false;
    }
    return rows;
}

PendingChangesDialog::PendingChangesDialog(
    const QVector<PendingChange> &changes, QWidget *parent)
    : QDialog(parent), m_rows(rowsFor(changes))
{
    setWindowTitle(tr("Unsynced changes"));

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Changes made here that a sync has not yet carried to the mail "
           "store. This list is a snapshot taken when it was opened."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *content = new QWidget;
    auto *grid = new QGridLayout(content);
    grid->setColumnStretch(0, 1);

    int line = 0;
    for (const PendingChangeRow &row : m_rows) {
        if (row.startsMessage) {
            // PlainText stated rather than left to Qt, for the reason
            // MessageDetailsDialog states it on every value: a subject comes
            // from a stranger, and a QLabel guesses under Qt::AutoText. Plain
            // text cannot interpret markup, so there is nothing to escape.
            QString text = row.subject;
            if (text.isEmpty()) {
                // The id no longer resolves. The row stays, because the count
                // the user clicked has to equal the list they are shown.
                text = tr("(no longer in the index)");
            }
            if (row.messageCount >= 0) {
                text = tr("%1 (whole thread, %n message(s))", "",
                          row.messageCount).arg(text);
            }
            auto *subject = new QLabel(text, content);
            subject->setTextFormat(Qt::PlainText);
            subject->setWordWrap(true);
            grid->addWidget(subject, line, 0);
        }

        auto *action = new QLabel(row.action, content);
        action->setTextFormat(Qt::PlainText);
        grid->addWidget(action, line, 1, Qt::AlignTop | Qt::AlignRight);
        ++line;
    }

    if (m_rows.isEmpty()) {
        grid->addWidget(new QLabel(tr("Nothing is waiting to be synced."),
                                   content),
                        0, 0);
    }

    grid->setRowStretch(line, 1);

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    layout->addWidget(scroll);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(600, 380);
}
