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
#include <QList>
#include <QString>

#include "htmlbuilder.h"

/// One header of one message, as shown and as searched for.
///
/// The query is built when the row is, from the parsed value, so nothing has
/// to parse displayed text back into structure. That is the whole reason this
/// dialog stopped being a text box.
struct HeaderRow
{
    /// notmuch's field name, or empty for a header with no searchable form.
    /// Wire format, never translated.
    QString field;

    /// Translated label shown at the start of the row, e.g. "From:".
    QString label;

    /// The header's value, verbatim and untrusted.
    QString value;

    /// The finished query, empty when the header has no searchable form.
    QString query;

    /// Which message of the thread this row belongs to, zero-based.
    int messageIndex = 0;
};

/// The full headers of every message in a thread, read-only.
///
/// Rows rather than one text box, so a value can carry its own context menu
/// without anything parsing rendered text back into structure. The user also
/// asked not to be shown a text box.
///
/// **Every value label is explicitly `Qt::PlainText`.** This replaced a
/// `QPlainTextEdit` whose plain-textness was a security property rather than a
/// style: header values come from strangers, and plain text cannot interpret
/// markup, so there is nothing to escape and nothing that can render. A QLabel
/// guesses under `Qt::AutoText`, so stating the format is what preserves that.
class MessageDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MessageDetailsDialog(const QList<ThreadRenderItem> &items,
                                  QWidget *parent = nullptr);

    /// The rows on display, in order. Exposed for testing without rendering.
    QList<HeaderRow> rows() const { return m_rows; }

    /// Emits searchRequested for `row`, or nothing when the row carries no
    /// searchable query. The menu entries call this; a test can too, without
    /// popping a menu.
    void requestSearch(const HeaderRow &row, bool extend);

signals:
    /// The user chose a search from a row's menu. `extend` narrows the current
    /// query rather than replacing it.
    void searchRequested(const QString &query, bool extend);

private:
    /// Builds the rows from the thread, one group per message.
    void buildRows(const QList<ThreadRenderItem> &items);

    QList<HeaderRow> m_rows;
};
