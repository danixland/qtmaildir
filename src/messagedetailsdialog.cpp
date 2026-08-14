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

#include "messagedetailsdialog.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QVBoxLayout>

#include "mimeparser.h"
#include "searchterm.h"

MessageDetailsDialog::MessageDetailsDialog(const QList<ThreadRenderItem> &items,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Message details"));
    setObjectName(QStringLiteral("messageDetailsDialog"));

    buildRows(items);

    auto *layout = new QVBoxLayout(this);

    // Scrollable: a long thread has many rows, and the dialog must not grow
    // past the screen to show them.
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *grid = new QGridLayout(content);

    // A monospaced value keeps a long id or address readable as the record it
    // is, which is what the text box did well and is worth carrying over.
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    int gridRow = 0;
    int lastMessage = -1;
    for (const HeaderRow &row : std::as_const(m_rows)) {
        if (items.size() > 1 && row.messageIndex != lastMessage) {
            lastMessage = row.messageIndex;
            auto *heading = new QLabel(
                tr("Message %1 of %2").arg(row.messageIndex + 1)
                    .arg(items.size()),
                content);
            heading->setTextFormat(Qt::PlainText);
            QFont headingFont = heading->font();
            headingFont.setBold(true);
            heading->setFont(headingFont);
            grid->addWidget(heading, gridRow, 0, 1, 2);
            ++gridRow;
        }

        auto *label = new QLabel(row.label, content);
        label->setTextFormat(Qt::PlainText);
        label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        grid->addWidget(label, gridRow, 0);

        // PlainText stated, not inferred. A QLabel guesses under AutoText, and
        // this value came from a stranger.
        auto *value = new QLabel(row.value, content);
        value->setTextFormat(Qt::PlainText);
        value->setFont(fixed);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);

        if (!row.query.isEmpty()) {
            value->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(value, &QWidget::customContextMenuRequested, this,
                    [this, value, row](const QPoint &pos) {
                        QMenu menu(this);
                        auto *replace = menu.addAction(tr("Search for this"));
                        connect(replace, &QAction::triggered, this,
                                [this, row]() { requestSearch(row, false); });
                        auto *narrow = menu.addAction(tr("Add to search"));
                        connect(narrow, &QAction::triggered, this,
                                [this, row]() { requestSearch(row, true); });
                        menu.exec(value->mapToGlobal(pos));
                    });
        }

        grid->addWidget(value, gridRow, 1);
        ++gridRow;
    }

    grid->setColumnStretch(1, 1);
    grid->setRowStretch(gridRow, 1);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    resize(700, 400);
}

void MessageDetailsDialog::buildRows(const QList<ThreadRenderItem> &items)
{
    for (int i = 0; i < items.size(); ++i) {
        const ParsedMessage &message = items.at(i).message;

        auto add = [this, i](const QString &field, const QString &label,
                             const QString &value, const QString &query) {
            if (value.isEmpty())
                return;   // An empty row reads as a rendering fault.
            m_rows.append({ field, label, value, query, i });
        };

        add(QStringLiteral("subject"), tr("Subject:"), message.subject,
            SearchTerm::field(QStringLiteral("subject"), message.subject));
        add(QStringLiteral("from"), tr("From:"), message.from,
            SearchTerm::field(QStringLiteral("from"), message.from));
        add(QStringLiteral("to"), tr("To:"), message.to,
            SearchTerm::field(QStringLiteral("to"), message.to));
        add(QStringLiteral("cc"), tr("Cc:"), message.cc,
            SearchTerm::field(QStringLiteral("cc"), message.cc));

        // The raw header is shown, but the query is a one-day range: a text
        // match on an RFC 2822 string would match almost nothing.
        const QDateTime sent = MimeParser::parseDate(message.date);
        add(QStringLiteral("date"), tr("Date:"), message.date,
            sent.isValid() ? SearchTerm::onDate(sent.date()) : QString());

        // Shown but not searchable: an id names one message, and the thread
        // holding it is already on screen.
        add(QString(), tr("Message-Id:"), message.messageId, QString());
    }
}

void MessageDetailsDialog::requestSearch(const HeaderRow &row, bool extend)
{
    if (row.query.isEmpty())
        return;
    emit searchRequested(row.query, extend);
}
