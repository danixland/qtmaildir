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

#include "savequerydialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

bool SaveQueryDialog::namesAnExistingQuery(const Config &config,
                                           const QString &name)
{
    for (const SavedQuery &saved : config.savedQueries()) {
        if (saved.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

SaveQueryDialog::SaveQueryDialog(const Config &config, const QString &query,
                                 const QString &accountKey, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
{
    setWindowTitle(tr("Save query"));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("saveQueryName"));
    m_name->setPlaceholderText(tr("A name for this query"));
    form->addRow(tr("Name"), m_name);

    m_query = new QLineEdit(query, this);
    m_query->setObjectName(QStringLiteral("saveQueryQuery"));
    form->addRow(tr("Query"), m_query);

    // The scope is stored as an account KEY, so the entries carry the key as
    // data exactly as the main window's dropdown does. "All accounts" is the
    // empty key, not a missing entry, so an unscoped query is a real choice
    // rather than the absence of one.
    m_account = new QComboBox(this);
    m_account->setObjectName(QStringLiteral("saveQueryAccount"));
    m_account->addItem(tr("All accounts"), QString());
    for (const Account &account : config.accounts())
        m_account->addItem(account.key, account.key);
    const int index = m_account->findData(accountKey);
    m_account->setCurrentIndex(index >= 0 ? index : 0);
    form->addRow(tr("Account"), m_account);

    m_pinned = new QCheckBox(tr("Show as a button"), this);
    m_pinned->setObjectName(QStringLiteral("saveQueryPinned"));
    m_pinned->setChecked(true);
    form->addRow(QString(), m_pinned);

    layout->addLayout(form);

    // Says what is about to happen rather than refusing the name. Overwriting
    // a saved query on purpose is a normal edit, and the only thing worth
    // preventing is doing it without noticing.
    m_notice = new QLabel(this);
    m_notice->setObjectName(QStringLiteral("saveQueryNotice"));
    m_notice->setWordWrap(true);
    layout->addWidget(m_notice);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_ok = buttons->button(QDialogButtonBox::Save);
    m_ok->setObjectName(QStringLiteral("saveQueryOk"));

    connect(m_name, &QLineEdit::textChanged,
            this, &SaveQueryDialog::updateOkState);
    connect(m_query, &QLineEdit::textChanged,
            this, &SaveQueryDialog::updateOkState);
    updateOkState();

    m_name->setFocus();
}

void SaveQueryDialog::updateOkState()
{
    const QString name = m_name->text().trimmed();
    const bool usable = !name.isEmpty() && !m_query->text().trimmed().isEmpty();
    m_ok->setEnabled(usable);

    if (!name.isEmpty() && namesAnExistingQuery(m_config, name)) {
        m_notice->setText(
            tr("A saved query named '%1' already exists and will be "
               "replaced.").arg(name));
    } else {
        m_notice->clear();
    }
}

SavedQuery SaveQueryDialog::savedQuery() const
{
    SavedQuery saved;
    saved.name = m_name->text().trimmed();
    saved.query = m_query->text().trimmed();
    saved.account = m_account->currentData().toString();
    saved.pinned = m_pinned->isChecked();
    return saved;
}
