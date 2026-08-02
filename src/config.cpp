#include "config.h"

#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

QString Account::scopedQuery(const QString &query) const
{
    const QString prefix = QStringLiteral("path:\"%1/**\"").arg(maildir);
    if (query.trimmed().isEmpty())
        return prefix;
    return QStringLiteral("%1 and (%2)").arg(prefix, query);
}

QString Config::defaultPath()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return base + QStringLiteral("/qtmaildir/qtmaildir.conf");
}

void Config::load(const QString &path)
{
    QSettings settings(path, QSettings::IniFormat);

    m_notmuchConfig =
        settings.value(QStringLiteral("general/notmuch_config")).toString();

    m_syncCommand = settings.value(QStringLiteral("sync/command")).toString();
    if (m_syncCommand.isEmpty()) {
        m_warnings.append(QStringLiteral(
            "No sync command configured ([sync] command); syncing is disabled."));
    } else if (!QFileInfo::exists(m_syncCommand.split(QLatin1Char(' ')).first())) {
        m_warnings.append(
            QStringLiteral("Sync command '%1' does not exist; syncing is disabled.")
                .arg(m_syncCommand));
        m_syncCommand.clear();
    }

    // Account groups are written as [account.work], [account.personal], etc.
    // A dot, not a slash, separates the "account" namespace from the key:
    // QSettings' INI backend treats "/" as its own hierarchical group
    // separator, so a literal "[account/work]" section header would be
    // parsed as a *nested* group "work" inside a group "account" (and trips
    // a QSettings::FormatError besides), not as a single flat group named
    // "account/work". "." carries no such meaning to QSettings, so
    // childGroups() here returns "account.work" and "account.personal" as
    // plain top-level entries and status() stays NoError.
    for (const QString &group : settings.childGroups()) {
        if (!group.startsWith(QStringLiteral("account.")))
            continue;

        Account account;
        account.key = group.mid(QStringLiteral("account.").size());

        settings.beginGroup(group);
        account.name = settings.value(QStringLiteral("name")).toString();
        account.address = settings.value(QStringLiteral("address")).toString();
        account.maildir = settings.value(QStringLiteral("maildir")).toString();
        account.drafts = settings.value(QStringLiteral("drafts")).toString();
        settings.endGroup();

        if (!account.isValid()) {
            m_warnings.append(
                QStringLiteral("Account '%1' has no maildir; ignoring it.")
                    .arg(account.key));
            continue;
        }
        m_accounts.append(account);
    }

    settings.beginGroup(QStringLiteral("queries"));
    // QSettings::childKeys() returns keys in alphabetical order, not file
    // order, so the saved-query button order in the UI is alphabetical too.
    // A hand-rolled parser would be needed to preserve file order; not
    // needed in v1.
    for (const QString &name : settings.childKeys())
        m_savedQueries.append({ name, settings.value(name).toString() });
    settings.endGroup();
}

Account Config::account(const QString &key) const
{
    for (const Account &a : m_accounts) {
        if (a.key == key)
            return a;
    }
    return {};
}
