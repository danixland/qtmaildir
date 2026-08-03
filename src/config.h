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

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

/// One mail account. notmuch has no concept of accounts; it sees a single flat
/// tree. An account is therefore a path prefix within that tree plus an
/// identity.
struct Account
{
    QString key;      ///< INI group suffix, e.g. "work" from [account/work].
    QString name;
    QString address;
    QString maildir;  ///< Relative to notmuch's database.path.
    QString drafts;   ///< Unused in v1; send is v2.

    /// Chip colour in the thread list. Invalid when unset, in which case one
    /// is generated from the account tag's name.
    QColor color;

    /// Text shown on the chip. Empty falls back to the key, which can be long:
    /// "provider-work" is a lot of row for one bit of information.
    /// This renames nothing in notmuch, only what the chip displays.
    QString label;

    bool isValid() const { return !key.isEmpty() && !maildir.isEmpty(); }

    /// Restricts a notmuch query to this account's subtree.
    QString scopedQuery(const QString &query) const;
};

struct SavedQuery
{
    QString name;
    QString query;
};

/// Reads ~/.config/qtmaildir/qtmaildir.conf.
///
/// The Maildir path is deliberately NOT configurable here: notmuch already
/// stores it as database.path and libnotmuch reads it. Duplicating it would
/// allow the GUI to index a different tree than the CLI.
class Config
{
public:
    /// Path used when load() is called with no argument.
    static QString defaultPath();

    void load(const QString &path);

    QList<Account> accounts() const { return m_accounts; }
    Account account(const QString &key) const;
    QList<SavedQuery> savedQueries() const { return m_savedQueries; }

    /// Empty when unset; the caller disables the Sync button in that case.
    QString syncCommand() const { return m_syncCommand; }

    /// Optional alternate notmuch config file. Empty means "let notmuch decide".
    QString notmuchConfig() const { return m_notmuchConfig; }

    /// Every non-fatal problem, both kinds below. Shown in the status bar.
    QStringList warnings() const { return m_warnings; }

    /// The subset worth interrupting startup for: something in the config is
    /// wrong and the user's stated intent is not being honoured (a malformed
    /// account, an unparseable key binding, a sync command that does not
    /// exist). An optional setting simply being absent is NOT one of these:
    /// nothing is broken, the feature is just off, and a modal on every launch
    /// trains the user to dismiss dialogs without reading them.
    QStringList problems() const { return m_problems; }

private:
    /// Records a problem: something configured but wrong. Also appears in
    /// warnings(), so callers that want everything need only that one.
    void addProblem(const QString &message);

    /// Records a notice: nothing is wrong, a feature is simply not configured.
    void addNotice(const QString &message);

    QList<Account> m_accounts;
    QList<SavedQuery> m_savedQueries;
    QString m_syncCommand;
    QString m_notmuchConfig;
    QStringList m_warnings;
    QStringList m_problems;
};
