#pragma once

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

    /// Non-fatal problems, shown once in a startup banner.
    QStringList warnings() const { return m_warnings; }

private:
    QList<Account> m_accounts;
    QList<SavedQuery> m_savedQueries;
    QString m_syncCommand;
    QString m_notmuchConfig;
    QStringList m_warnings;
};
