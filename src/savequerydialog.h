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

#include "config.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

/// Names a query and keeps it in queries.json.
///
/// Pure UI: it is handed the config and returns a SavedQuery. It writes
/// nothing, so it can be tested without touching a file, and the caller owns
/// the decision of what to do with the result.
class SaveQueryDialog : public QDialog
{
    Q_OBJECT
public:
    /// `query` is the text to store, `accountKey` the scope to preselect,
    /// which is the account the user is already looking at.
    SaveQueryDialog(const Config &config, const QString &query,
                    const QString &accountKey, QWidget *parent = nullptr);

    /// The query as edited. Only meaningful after exec() returned Accepted.
    SavedQuery savedQuery() const;

    /// Whether `name` already names a stored query. Case-insensitive, matching
    /// how startup_query resolves, so "inbox" and "Inbox" cannot both exist and
    /// leave the user unable to tell which one a button ran.
    static bool namesAnExistingQuery(const Config &config, const QString &name);

private:
    void updateOkState();

    const Config &m_config;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_query = nullptr;
    QComboBox *m_account = nullptr;
    QCheckBox *m_pinned = nullptr;
    QPushButton *m_ok = nullptr;
    QLabel *m_notice = nullptr;
};
