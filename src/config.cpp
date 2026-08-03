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

void Config::addProblem(const QString &message)
{
    m_warnings.append(message);
    m_problems.append(message);
}

void Config::addNotice(const QString &message)
{
    m_warnings.append(message);
}

void Config::load(const QString &path)
{
    QSettings settings(path, QSettings::IniFormat);

    // Keys of [general] are read WITHOUT the "general/" prefix. QSettings'
    // INI backend treats a section literally named [general] as its own
    // fallback section and strips the prefix, so "general/notmuch_config"
    // never matches anything, in any section arrangement (verified on
    // Qt 6.11). The file still reads as [general] to the user; only the
    // lookup differs. Same family of trap as the [account.work] dot and the
    // childKeys() ordering already documented in CLAUDE.md.
    m_notmuchConfig =
        settings.value(QStringLiteral("notmuch_config")).toString();

    // Absent is fine and silent: the default is 1.0. Present but unparseable
    // is a problem, since the user asked for something and is not getting it.
    // The range check lives in MessageView::clampZoom(), the one place that
    // knows what the web view can render.
    // Empty is treated as unset rather than as "a query named nothing".
    const QString startup =
        settings.value(QStringLiteral("startup_query")).toString().trimmed();
    if (!startup.isEmpty()) {
        m_startupQuery = startup;
        m_startupQueryWasSet = true;
    }

    const QVariant zoom = settings.value(QStringLiteral("message_zoom"));
    if (zoom.isValid()) {
        bool ok = false;
        const double value = zoom.toString().toDouble(&ok);
        if (ok) {
            m_messageZoom = value;
        } else {
            addProblem(QStringLiteral("Message zoom '%1' is not a number; "
                                      "using the default.")
                           .arg(zoom.toString()));
        }
    }

    m_syncCommand = settings.value(QStringLiteral("sync/command")).toString();
    if (m_syncCommand.isEmpty()) {
        // Not a problem: sync is optional, and nothing the user asked for is
        // being ignored. A modal here would fire on every launch.
        addNotice(QStringLiteral(
            "No sync command configured ([sync] command); syncing is disabled."));
    } else if (!QFileInfo::exists(m_syncCommand.split(QLatin1Char(' ')).first())) {
        addProblem(
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

        // Both optional, and both describe this account's chip in the thread
        // list. An account tag is a different taxonomy from a functional one,
        // saying which mailbox a thread arrived in rather than what state it
        // is in, so these live here rather than in [tagcolors].
        account.label = settings.value(QStringLiteral("label")).toString();

        const QString colour = settings.value(QStringLiteral("color")).toString();
        if (!colour.isEmpty()) {
            account.color = QColor(colour);
            if (!account.color.isValid()) {
                addProblem(
                    QStringLiteral("Account '%1' has an unparseable color '%2'; "
                                   "using a generated one.")
                        .arg(account.key, colour));
            }
        }
        settings.endGroup();

        if (!account.isValid()) {
            addProblem(
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

    // Checked here rather than where startup_query is read: [queries] is not
    // parsed until now. Only a name the user actually wrote is worth a
    // problem; the built-in default naming a query they never created is not
    // something they got wrong.
    if (m_startupQueryWasSet && !m_savedQueries.isEmpty()
        && startupSavedQuery().name.compare(m_startupQuery,
                                            Qt::CaseInsensitive) != 0) {
        addProblem(QStringLiteral("Startup query '%1' is not a saved query; "
                                  "opening '%2' instead.")
                       .arg(m_startupQuery, startupSavedQuery().name));
    }
}

SavedQuery Config::startupSavedQuery() const
{
    if (m_savedQueries.isEmpty())
        return {};

    for (const SavedQuery &query : m_savedQueries) {
        if (query.name.compare(m_startupQuery, Qt::CaseInsensitive) == 0)
            return query;
    }

    // Named a query that does not exist. Not worth a warning: the default is
    // a name the user never wrote, so an install with no [queries] Unread
    // entry would warn on every launch about a key it never set.
    return m_savedQueries.first();
}

Account Config::account(const QString &key) const
{
    for (const Account &a : m_accounts) {
        if (a.key == key)
            return a;
    }
    return {};
}
