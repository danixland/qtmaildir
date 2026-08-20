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

#include "mailsync.h"
// For kMinZoom/kMaxZoom. The bounds live with the widget that enforces them,
// so this reports the same numbers rather than keeping a second copy.
#include "messageview.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

/// Bounds for [general] toolbar_icon_size. 16 is the smallest size the icon
/// themes actually ship art for, and is what this desktop's style reports;
/// above 64 the toolbar is taller than the thread rows it sits over.
constexpr int kMinToolbarIconSize = 16;
constexpr int kMaxToolbarIconSize = 64;

/// queries.json format version. Bump only for a BREAKING change: an optional
/// field needs no bump, because an older build preserves what it does not
/// understand rather than dropping it.
///
/// Unlike rules.json this file has ONE implementation, so a bump here is not a
/// two-repo change and no hook stops tagging if it is half-deployed.
constexpr int kQueriesFormatVersion = 1;

/// Generators a saved query may name in its `generated` field.
///
/// A closed set, checked on load so a typo is reported rather than producing a
/// button that silently finds nothing. Adding one here needs no format bump:
/// an older build keeps the row and reports it, which is why an unknown
/// generator is a problem rather than a reason to drop the entry.
const QStringList kQueryGenerators = { QStringLiteral("unread"),
                                       QStringLiteral("inbox"),
                                       QStringLiteral("flagged"),
                                       QStringLiteral("sent"),
                                       QStringLiteral("trash") };

/// The tag a generator matches, for the three filters that are a plain tag
/// query. Empty for "sent", which composes from each account's folder instead
/// and is handled separately.
QString generatorTag(const QString &generator)
{
    if (generator == QStringLiteral("unread"))
        return QStringLiteral("unread");
    if (generator == QStringLiteral("inbox"))
        return QStringLiteral("inbox");
    if (generator == QStringLiteral("flagged"))
        return QStringLiteral("flagged");
    return QString();
}

} // namespace

QString Account::scopedQuery(const QString &query) const
{
    const QString prefix = QStringLiteral("path:\"%1/**\"").arg(maildir);
    if (query.trimmed().isEmpty())
        return prefix;
    return QStringLiteral("%1 and (%2)").arg(prefix, query);
}

namespace {

/// Composes `path:"<maildir>/<folder>/**"`, or empty when the folder is unset.
///
/// The QUOTES are load-bearing, not decoration. A real provider nests both its
/// sent and its drafts folder under a bracketed parent with a localised name,
/// "[Provider]/Posta inviata" and "[Provider]/Bozze", and "[" and "]" are
/// Xapian syntax: unquoted, the term is parsed rather than matched and the
/// query silently returns nothing while looking correct.
///
/// The path is user config and is interpolated into a query, so this is the
/// only place that composition happens; a caller building it by hand would be
/// a second chance to forget the quotes.
QString folderQuery(const QString &maildir, const QString &folder)
{
    if (folder.isEmpty())
        return QString();
    return QStringLiteral("path:\"%1/%2/**\"").arg(maildir, folder);
}

/// Joins the non-empty results of `extract` across `accounts` with " or ".
///
/// Collect first, join after. Appending "or" per account and trimming the
/// result is the version that produced the defect this guards: an account with
/// no key contributes an empty term, notmuch accepts the bare "or" without
/// complaint, and the query quietly means something else. Measured against a
/// real database, `A or  or B` returns 190 where the correct pair returns 211.
QString joinAccountQueries(const QList<Account> &accounts,
                           QString (Account::*extract)() const)
{
    QStringList parts;
    for (const Account &account : accounts) {
        const QString query = (account.*extract)();
        if (!query.isEmpty())
            parts.append(query);
    }
    return parts.join(QStringLiteral(" or "));
}

} // namespace

QString Account::sentQuery() const
{
    return folderQuery(maildir, sent);
}

QString Account::draftsQuery() const
{
    return folderQuery(maildir, drafts);
}

QString Account::trashQuery() const
{
    return folderQuery(maildir, trash);
}

QString Account::inboxFolder() const
{
    // Never empty: Restore needs a folder to name, and "Inbox" is both the
    // Maildir convention and what mbsync's own Inbox directive defaults to.
    return inbox.isEmpty() ? QStringLiteral("Inbox") : inbox;
}

QString Account::inboxQuery() const
{
    return folderQuery(maildir, inboxFolder());
}

QString Config::allSentQuery() const
{
    return joinAccountQueries(m_accounts, &Account::sentQuery);
}

QString Config::allDraftsQuery() const
{
    return joinAccountQueries(m_accounts, &Account::draftsQuery);
}

QString Config::allTrashQuery() const
{
    return joinAccountQueries(m_accounts, &Account::trashQuery);
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
    // The range is enforced by MessageView::clampZoom(), the one place that
    // knows what the web view can render; out of range is reported below.
    // Empty is treated as unset rather than as "a query named nothing".
    const QString startup =
        settings.value(QStringLiteral("startup_query")).toString().trimmed();
    if (!startup.isEmpty()) {
        m_startupQuery = startup;
        m_startupQueryWasSet = true;
    }

    // Stored raw here and validated once the accounts are parsed, below: the
    // account sections have not been read yet at this point.
    m_startupAccount =
        settings.value(QStringLiteral("startup_account")).toString().trimmed();

    // Interface language. "system" is spelled out so the default can be written
    // down rather than only expressed by deleting the key.
    //
    // Validated here rather than left to whether a translation loads, because
    // those are different questions and only one of them is an error. QLocale
    // accepts anything and degrades an unrecognised name to C, so `language =
    // itallian` would load no translation and look exactly like asking for
    // English. Meanwhile `language = en_US` legitimately loads nothing, since
    // English is the source language and ships no .qm. Checking the NAME
    // separates the typo from the deliberate choice.
    const QString language =
        settings.value(QStringLiteral("language")).toString().trimmed();
    if (!language.isEmpty()
        && language.compare(QStringLiteral("system"), Qt::CaseInsensitive) != 0) {
        if (QLocale(language).language() == QLocale::C) {
            addProblem(tr("Language '%1' is not a locale name; using the "
                          "system language. Expected something like 'it' or "
                          "'it_IT'.")
                           .arg(language));
        } else {
            m_language = language;
        }
    }

    const QVariant zoom = settings.value(QStringLiteral("message_zoom"));
    if (zoom.isValid()) {
        bool ok = false;
        const double value = zoom.toString().toDouble(&ok);
        if (ok) {
            m_messageZoom = value;
            // Reported, not clamped: MessageView::clampZoom() owns the bounds
            // and already stops this reaching the web view, so clamping here
            // too would be a second copy of the range, free to drift from the
            // first. What was missing is the report. The value parses, so
            // nothing ever said the 500 in the file is not what is on screen.
            if (value < MessageView::kMinZoom || value > MessageView::kMaxZoom) {
                addProblem(tr("Message zoom %1 is outside %2 to %3; "
                                          "using the nearest allowed value.")
                               .arg(value)
                               .arg(MessageView::kMinZoom)
                               .arg(MessageView::kMaxZoom));
            }
        } else {
            addProblem(tr("Message zoom '%1' is not a number; "
                                      "using the default.")
                           .arg(zoom.toString()));
        }
    }

    // A [general] key, so no prefix, per the note at the top of load().
    m_completionOnFocus =
        settings.value(QStringLiteral("completion_on_focus"), false).toBool();

    // Clamped, unlike message_zoom above, which documents a 0.5 to 3.0 range in
    // the README and enforces none of it. Both ends here are unrecoverable from
    // the UI they break: too small is an invisible icon, too large is a toolbar
    // taller than the window, and in either case the control the user would
    // reach for to fix it is the one that just broke.
    const QVariant iconSize = settings.value(QStringLiteral("toolbar_icon_size"));
    if (iconSize.isValid()) {
        bool ok = false;
        const int value = iconSize.toString().trimmed().toInt(&ok);
        if (!ok) {
            addProblem(tr("Toolbar icon size '%1' is not a number; "
                                      "using %2.")
                           .arg(iconSize.toString())
                           .arg(m_toolbarIconSize));
        } else if (value < kMinToolbarIconSize || value > kMaxToolbarIconSize) {
            m_toolbarIconSize =
                qBound(kMinToolbarIconSize, value, kMaxToolbarIconSize);
            addProblem(tr("Toolbar icon size %1 is outside %2 to "
                                      "%3; using %4.")
                           .arg(value)
                           .arg(kMinToolbarIconSize)
                           .arg(kMaxToolbarIconSize)
                           .arg(m_toolbarIconSize));
        } else {
            m_toolbarIconSize = value;
        }
    }

    // Absent or empty means the system locale's short format, which is what
    // every other application on the desktop shows. Only a non-empty pattern is
    // validated, and a rejected one falls back to that same default.
    const QString dateFormat =
        settings.value(QStringLiteral("date_format")).toString().trimmed();
    if (!dateFormat.isEmpty()) {
        // QDateTime::toString() with a pattern carrying no date or time field
        // returns the pattern verbatim rather than failing, so "banana" would
        // print "banana" on every card. Formatting two DIFFERENT instants and
        // comparing is what catches that: a pattern with any real field gives
        // two different strings, one with none gives the same string twice.
        const QDateTime a(QDate(2028, 12, 28), QTime(22, 58));
        const QDateTime b(QDate(2019, 1, 3), QTime(4, 5));
        const QLocale locale = QLocale::system();
        if (locale.toString(a, dateFormat) == locale.toString(b, dateFormat)) {
            addProblem(tr("Date format '%1' contains no date or "
                                      "time field; using the system format.")
                           .arg(dateFormat));
        } else {
            m_dateFormat = dateFormat;
        }
    }

    // Absent is silent, the default being 2000. Present but unparseable warns,
    // for the same reason message_zoom does: the user asked for something and
    // is not getting it.
    //
    // Zero and negative are NOT errors and must not be clamped. Zero means mark
    // read at once, and any negative value means never, which is how the
    // behaviour is turned off.
    // Three values, not a bool: "prompt me", "just do it" and "do nothing" are
    // three distinct behaviours and true/false can only express two of them.
    const QString syncExit =
        settings.value(QStringLiteral("sync_on_exit"),
                       QStringLiteral("ask")).toString().trimmed().toLower();
    if (syncExit == QStringLiteral("ask")) {
        m_syncOnExit = SyncOnExit::Ask;
    } else if (syncExit == QStringLiteral("always")) {
        m_syncOnExit = SyncOnExit::Always;
    } else if (syncExit == QStringLiteral("never")) {
        m_syncOnExit = SyncOnExit::Never;
    } else {
        // Naming the accepted values, since a typo here silently changes what
        // happens to unsynced work at exit.
        addProblem(tr("Unknown sync_on_exit '%1'; expected ask, "
                                  "always or never. Using ask.")
                       .arg(syncExit));
    }

    const QVariant markRead = settings.value(QStringLiteral("mark_read_delay_ms"));
    if (markRead.isValid()) {
        bool ok = false;
        const int value = markRead.toString().toInt(&ok);
        if (ok) {
            m_markReadDelayMs = value;
        } else {
            addProblem(tr("Mark-read delay '%1' is not a number; "
                                      "using the default.")
                           .arg(markRead.toString()));
        }
    }

    // Item 71. Same shape as mark_read_delay_ms above, including that zero and
    // negative are not errors: 0 syncs on the next trip through the event loop
    // and negative disables the automatic sync entirely, which is how a user who
    // wants only their cron job turns this off.
    const QVariant autoSync = settings.value(QStringLiteral("auto_sync_delay_ms"));
    if (autoSync.isValid()) {
        bool ok = false;
        const int value = autoSync.toString().toInt(&ok);
        if (ok) {
            m_autoSyncDelayMs = value;
        } else {
            addProblem(tr("Auto-sync delay '%1' is not a number; "
                                      "using the default.")
                           .arg(autoSync.toString()));
        }
    }

    // [completion] is an ordinary section, so this one DOES take its prefix.
    // ',' separates entries and '|' separates a value from its description:
    // two different characters because QSettings splits comma lists itself,
    // so a description holding a comma would otherwise become two entries.
    // Neither character is legal in a mimetype.
    const QStringList rawMimetypes =
        settings.value(QStringLiteral("completion/extra_mimetypes")).toStringList();
    for (const QString &raw : rawMimetypes) {
        const QString entry = raw.trimmed();
        if (entry.isEmpty())
            continue;

        const int bar = entry.indexOf(QLatin1Char('|'));
        const QString value = (bar < 0 ? entry : entry.left(bar)).trimmed();
        const QString description =
            (bar < 0 ? QString() : entry.mid(bar + 1)).trimmed();

        // Skip only the bad entry: one typo must not cost the user the rest
        // of the list, and the built-ins are appended to regardless.
        if (value.isEmpty()) {
            addProblem(tr("[completion] extra_mimetypes: entry '%1' "
                                      "has no mimetype; ignoring it.")
                           .arg(entry));
            continue;
        }
        m_extraMimetypes.append({ value, description });
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

    // Not validated for existence, unlike the command above. The log is written
    // by the script when it runs, so a fresh install has no file yet, and a
    // startup problem reported for that would be noise. A missing file simply
    // reads as SyncOutcome::Unknown when the time comes.
    m_syncLog = settings.value(QStringLiteral("sync/log")).toString().trimmed();
    if (m_syncLog.isEmpty())
        m_syncLog = MailSync::defaultLogPath();

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

        // Optional, and absent for an account that keeps no sent mail locally.
        // Trimmed because a trailing space would land inside the quoted path
        // and match nothing, which is invisible in a config file.
        account.sent =
            settings.value(QStringLiteral("sent")).toString().trimmed();

        // Mandatory, unlike sent: Delete moves a file into this folder, so an
        // account without one cannot delete at all. Trimmed for the same
        // reason as sent, above.
        account.trash =
            settings.value(QStringLiteral("trash")).toString().trimmed();

        // Optional, unlike trash: inboxFolder() defaults it to "Inbox", which
        // is right for any ordinary Maildir. Read so an account whose inbox is
        // named otherwise can say so, rather than having Restore create a
        // second folder under a name this program assumed.
        account.inbox =
            settings.value(QStringLiteral("inbox")).toString().trimmed();

        // Optional, and its absence IS the receive-only state: see the field
        // comment in config.h. Run without a shell, so trimming here is only
        // whitespace hygiene, never a quoting concern.
        account.sendCommand =
            settings.value(QStringLiteral("send_command")).toString().trimmed();

        // Both optional, and both describe this account's chip in the thread
        // list. An account tag is a different taxonomy from a functional one,
        // saying which mailbox a thread arrived in rather than what state it
        // is in, so these live here rather than in [tagcolors].
        account.label = settings.value(QStringLiteral("label")).toString();

        // Optional, and absent for most accounts: syncChannel() falls back to
        // the key. Needed only where the section key and the mbsync channel
        // name diverge.
        account.channel = settings.value(QStringLiteral("channel")).toString();

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

        // Mandatory, unlike sent: Delete moves a file into this folder, so an
        // account without one cannot delete at all. Reported rather than
        // silently disabled, so the user finds out from a warning rather than
        // from a Delete that quietly does nothing. The account still loads;
        // only Delete is unusable, which does not warrant losing the rest of
        // the account's mail.
        if (account.trash.isEmpty()) {
            addProblem(
                tr("Account '%1' has no trash folder configured; add a "
                   "'trash' key to its section. Delete will not work for "
                   "this account until it does.")
                    .arg(account.key));
        }

        m_accounts.append(account);
    }

    settings.beginGroup(QStringLiteral("compose"));
    // value(key, default) throughout rather than testing contains(): an
    // absent key and a key set to its default must behave identically, and
    // send_delay_ms = 0 is a REAL setting meaning "send at once" that a
    // zero-test would mistake for unset.
    m_compose.quotePosition =
        settings.value(QStringLiteral("quote_position"), QStringLiteral("above"))
                    .toString().compare(QStringLiteral("below"), Qt::CaseInsensitive) == 0
            ? ComposeSettings::QuotePosition::Below
            : ComposeSettings::QuotePosition::Above;
    m_compose.sendHtml =
        settings.value(QStringLiteral("send_html"), true).toBool();
    m_compose.autosaveIntervalMs =
        settings.value(QStringLiteral("autosave_interval_ms"), 30000).toInt();
    m_compose.sendDelayMs =
        settings.value(QStringLiteral("send_delay_ms"), 5000).toInt();
    m_compose.defaultAccount =
        settings.value(QStringLiteral("default_account")).toString().trimmed();
    m_compose.attachmentWarnBytes =
        settings.value(QStringLiteral("attachment_warn_bytes"), qint64(26214400))
            .toLongLong();
    settings.endGroup();

    loadSavedQueries(path, settings);

    // Checked here rather than where startup_query is read: the saved queries
    // are not parsed until now. Only a name the user actually wrote is worth a
    // problem; the built-in default naming a query they never created is not
    // something they got wrong.
    // Same shape as the startup_query check below: a name the user wrote that
    // matches nothing is a problem, because they asked for something and are
    // not getting it. Cleared rather than passed on, since the dropdown has no
    // entry for an account that does not exist and would sit on "All accounts"
    // without saying why.
    if (!m_startupAccount.isEmpty() && !account(m_startupAccount).isValid()) {
        addProblem(tr("Startup account '%1' is not a configured "
                                  "account; starting on all accounts.")
                       .arg(m_startupAccount));
        m_startupAccount.clear();
    }

    // default_account is validated here, once the accounts are parsed. A
    // named account that cannot send is reported: the user named an account
    // and expects mail to come from it, unlike an installation where no
    // account can send at all, which is a valid read-only setup and not
    // warned about below.
    if (!m_compose.defaultAccount.isEmpty()) {
        const auto named = std::find_if(
            m_accounts.cbegin(), m_accounts.cend(),
            [this](const Account &a) { return a.key == m_compose.defaultAccount; });

        if (named == m_accounts.cend()) {
            addProblem(
                tr("[compose] default_account names '%1', which is not a "
                   "configured account. A new message will pick a sending "
                   "account by the usual rules.")
                    .arg(m_compose.defaultAccount));
        } else if (!named->canSend()) {
            addProblem(
                tr("[compose] default_account names '%1', which has no "
                   "send_command and cannot send. A new message will pick a "
                   "sending account by the usual rules.")
                    .arg(m_compose.defaultAccount));
        }
    }

    for (const Account &account : m_accounts) {
        if (!account.canSend())
            continue;
        if (account.sent.isEmpty()) {
            addProblem(
                tr("Account '%1' can send but configures no `sent` folder, so "
                   "no local copy of sent mail is filed.")
                    .arg(account.key));
        }
        if (account.drafts.isEmpty()) {
            addProblem(
                tr("Account '%1' can send but configures no `drafts` folder, "
                   "so the composer runs without draft protection.")
                    .arg(account.key));
        }
    }

    // Asks whether the resolved query matched on EITHER a name or a generator,
    // rather than comparing the name alone. Comparing names warned about a
    // config that was working: `startup_query = Inbox` resolves through the
    // generator under a translated UI, where the filter's name is "In arrivo",
    // so the app opened the right view and reported it as wrong.
    if (m_startupQueryWasSet && !m_savedQueries.isEmpty()) {
        const SavedQuery resolved = startupSavedQuery();
        const bool matched =
            resolved.name.compare(m_startupQuery, Qt::CaseInsensitive) == 0
            || (!resolved.generated.isEmpty()
                && resolved.generated.compare(m_startupQuery,
                                              Qt::CaseInsensitive) == 0);
        if (!matched) {
            addProblem(tr("Startup query '%1' is not a saved query; "
                                      "opening '%2' instead.")
                           .arg(m_startupQuery, resolved.name));
        }
    }
}

QString Config::queriesPath(const QString &configPath)
{
    return QFileInfo(configPath).absolutePath()
           + QStringLiteral("/queries.json");
}

void Config::loadSavedQueries(const QString &configPath, QSettings &settings)
{
    m_queriesPath = queriesPath(configPath);

    QFile file(m_queriesPath);
    if (!file.exists()) {
        // Migration. Read [queries] once, write the JSON, and leave the INI
        // section alone: stripping it would mean rewriting a hand-edited file
        // with QSettings, which drops comments and key order across the WHOLE
        // file. A few stale lines the user can delete by hand is the cheaper
        // loss, and it keeps a downgrade working.
        settings.beginGroup(QStringLiteral("queries"));
        const QStringList names = settings.childKeys();
        for (const QString &name : names) {
            SavedQuery query;
            query.name = name;
            query.query = settings.value(name).toString();
            // Pinned, because these are buttons today. A migration that left
            // them unpinned would empty the query row on the first launch
            // after an upgrade, which reads as data loss.
            query.pinned = true;
            m_savedQueries.append(query);
        }
        settings.endGroup();

        // Sent is NOT migrated into queries.json any more. It used to become an
        // ordinary saved query here, so the hardcoded button could be
        // reordered, renamed or removed; item 93 makes it one of four built-in
        // filters instead, which are shipped rather than stored. Migrating it
        // as well would put two Sent buttons on the row, one of them the user's
        // to edit and one not.
        //
        // Nothing is lost: the built-in Sent resolves through the same
        // generator, so it still follows the accounts, and it now composes with
        // the account dropdown rather than resetting it.

        // Order is alphabetical here because childKeys() is genuinely all the
        // INI knows. The user reorders once and it sticks from then on.
        if (!m_savedQueries.isEmpty() && !saveSavedQueries()) {
            addProblem(tr("Could not write saved queries to %1.")
                           .arg(m_queriesPath));
        }
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        addProblem(tr("Could not read %1: %2.")
                       .arg(m_queriesPath, file.errorString()));
        m_queriesRefused = true;
        return;
    }

    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        addProblem(tr("%1 is not valid JSON: %2.")
                       .arg(m_queriesPath, error.errorString()));
        m_queriesRefused = true;
        return;
    }

    const QJsonObject root = document.object();
    const int version =
        root.value(QStringLiteral("version")).toInt(kQueriesFormatVersion);
    if (version != kQueriesFormatVersion) {
        // Refused rather than guessed at, and the refusal blocks the save:
        // rewriting a newer document with this build's reading of it would
        // destroy whatever the newer build stored.
        addProblem(tr("%1 has format version %2; this build "
                                  "understands %3. Saved queries were not "
                                  "loaded.")
                       .arg(m_queriesPath)
                       .arg(version)
                       .arg(kQueriesFormatVersion));
        m_queriesRefused = true;
        return;
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() != QStringLiteral("version")
            && it.key() != QStringLiteral("queries"))
            m_queriesUnknown.insert(it.key(), it.value());
    }

    const QJsonArray array = root.value(QStringLiteral("queries")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        SavedQuery query;
        query.name = object.value(QStringLiteral("name")).toString();
        query.query = object.value(QStringLiteral("query")).toString();
        query.pinned = object.value(QStringLiteral("pinned")).toBool(false);
        query.account = object.value(QStringLiteral("account")).toString();
        query.generated = object.value(QStringLiteral("generated")).toString();
        // A generator carries its own view mode, so "sent" is flat whether or
        // not the file says so. Storing it as a plain field would let a
        // hand-edited or migrated-from-elsewhere row produce a THREADED sent
        // view, which folds every reply back into the conversation the user
        // sent one message into. The file may still set it for an ordinary
        // query.
        query.flat = object.value(QStringLiteral("flat")).toBool(false)
                     || query.generated == QStringLiteral("sent");

        if (query.isGenerated()
            && !kQueryGenerators.contains(query.generated)) {
            // Reported but KEPT. A later build may know this generator, and
            // dropping the row here would delete it from the file on the next
            // save, which is the same data loss the unknown-field handling
            // exists to prevent.
            addProblem(tr("Saved query '%1' uses an unknown "
                                      "generator '%2' and will find nothing.")
                           .arg(query.name, query.generated));
        }

        if (query.name.isEmpty()) {
            addProblem(tr("A saved query in %1 has no name and was "
                                      "skipped.").arg(m_queriesPath));
            continue;
        }

        // A stored entry naming a generator now duplicates a BUILT-IN filter of
        // the same name, since item 93 ships all four rather than storing them.
        // 0.19.0 migrated the hardcoded Sent button into exactly such an entry,
        // so every existing install has one.
        //
        // Unpinned, never dropped: the row would otherwise carry two Sent
        // buttons, one the user's to edit and one not. Deleting it would be
        // data loss on a file whose readers are supposed to preserve what they
        // do not own, and an unpin is reversible from the UI.
        if (query.isGenerated() && isKnownGenerator(query.generated))
            query.pinned = false;

        for (auto it = object.begin(); it != object.end(); ++it) {
            static const QStringList known = {
                QStringLiteral("name"), QStringLiteral("query"),
                QStringLiteral("pinned"), QStringLiteral("account"),
                QStringLiteral("generated"), QStringLiteral("flat")
            };
            if (!known.contains(it.key()))
                query.unknown.insert(it.key(), it.value());
        }

        m_savedQueries.append(query);
    }
}

bool Config::saveSavedQueries() const
{
    if (m_queriesPath.isEmpty() || m_queriesRefused)
        return false;

    QJsonArray array;
    for (const SavedQuery &query : m_savedQueries) {
        // Only what carries information. The file is hand-editable, so a key
        // that always holds the same value, or one the generator already
        // implies, is just something the reader has to skip past. Same reason
        // `pinned` and `account` are written only when set.
        QJsonObject object;
        object.insert(QStringLiteral("name"), query.name);
        if (query.isGenerated()) {
            object.insert(QStringLiteral("generated"), query.generated);
        } else {
            object.insert(QStringLiteral("query"), query.query);
        }
        if (query.pinned)
            object.insert(QStringLiteral("pinned"), true);
        if (!query.account.isEmpty())
            object.insert(QStringLiteral("account"), query.account);
        // Skipped when the generator already implies it, which loadSavedQueries
        // reapplies on the way back in.
        if (query.flat && query.generated != QStringLiteral("sent"))
            object.insert(QStringLiteral("flat"), true);
        for (auto it = query.unknown.begin(); it != query.unknown.end(); ++it)
            object.insert(it.key(), it.value());
        array.append(object);
    }

    QJsonObject root = m_queriesUnknown;
    root.insert(QStringLiteral("version"), kQueriesFormatVersion);
    root.insert(QStringLiteral("queries"), array);

    QDir().mkpath(QFileInfo(m_queriesPath).absolutePath());

    // QSaveFile writes a temporary and renames on commit, so an interrupted
    // write cannot leave a half-written file where the queries used to be.
    QSaveFile file(m_queriesPath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString Config::resolvedQuery(const SavedQuery &query) const
{
    // Composed from the accounts every time it is asked for, which is the
    // point: the answer follows the config rather than a copy of it taken when
    // the entry was written.
    if (query.isGenerated()) {
        if (query.generated == QStringLiteral("sent"))
            return allSentQuery();
        if (query.generated == QStringLiteral("trash"))
            return allTrashQuery();
        // An unknown generator was reported on load. Empty rather than the
        // bare stored query, which for a generated entry is empty anyway and
        // would otherwise run as "match everything".
        return QString();
    }

    if (query.account.isEmpty())
        return query.query;

    const Account scope = account(query.account);
    if (!scope.isValid())
        return query.query;

    return scope.scopedQuery(query.query);
}

bool Config::isKnownGenerator(const QString &generator)
{
    return kQueryGenerators.contains(generator);
}

QString Config::matchNothingQuery()
{
    // notmuch reads an EMPTY query as "match everything", so a generator with
    // nothing to match must say so explicitly. `tag:` and its negation cannot
    // both hold, and the tag name is irrelevant: what matters is that this
    // parses and matches nothing. A malformed string would not do, since
    // notmuch accepts almost anything and matches nothing quietly, which is the
    // same result reached by luck rather than by contract.
    return QStringLiteral("tag:unread and not tag:unread");
}

QList<SavedQuery> Config::builtinFilters()
{
    // Left to right on the query row. Fixed rather than configurable: item 94
    // removes the mixed row entirely once these are confirmed, so a settings
    // surface for the order would be built and deleted inside two items.
    QList<SavedQuery> filters;
    for (const QString &generator : kQueryGenerators)
        filters.append(builtinFilter(generator));
    return filters;
}

SavedQuery Config::builtinFilter(const QString &generator)
{
    if (!isKnownGenerator(generator))
        return {};

    SavedQuery filter;
    filter.generated = generator;

    // Translated, because these are the labels on the buttons. The GENERATOR
    // name is not: it is stored in queries.json and matched against a closed
    // set, so translating it would make a file written in one locale unreadable
    // in another.
    if (generator == QStringLiteral("unread")) {
        filter.name = tr("Unread");
    } else if (generator == QStringLiteral("inbox")) {
        filter.name = tr("Inbox");
    } else if (generator == QStringLiteral("flagged")) {
        // "Important", matching the `flag` action, which item 57 renamed from
        // "Flag" for exactly this reason. Shipping the filter as "Flagged"
        // beside it put the same tag under two names in one window. The
        // GENERATOR stays `flagged`: that string is stored in queries.json and
        // matched against a closed set, so it is wire format, not a label.
        filter.name = tr("Important");
    } else if (generator == QStringLiteral("sent")) {
        filter.name = tr("Sent");
        // Messages rather than threads, and the only filter that sets this. A
        // thread would fold the user's sent message back into the conversation
        // it belongs to, which is item 63's finding.
        filter.flat = true;
    } else if (generator == QStringLiteral("trash")) {
        filter.name = tr("Trash");
        // NOT flat, unlike Sent. A deleted message still belongs to its
        // conversation, and folding it back is what Sent had to avoid rather
        // than something every folder filter wants.
    }

    return filter;
}

QString Config::resolvedQuery(const SavedQuery &query,
                              const QString &accountKey) const
{
    // An ordinary saved query is a DESTINATION: it states its own scope and
    // ignores the dropdown, which is the behaviour item 90 leaves alone. Only a
    // generated filter composes.
    if (!query.isGenerated())
        return resolvedQuery(query);

    if (!isKnownGenerator(query.generated))
        return QString();

    if (accountKey.isEmpty()) {
        // Across every account, which for the tag filters is the bare query and
        // for Sent is the union of the accounts' folders.
        if (query.generated == QStringLiteral("sent")) {
            const QString all = allSentQuery();
            return all.isEmpty() ? matchNothingQuery() : all;
        }
        if (query.generated == QStringLiteral("trash")) {
            const QString all = allTrashQuery();
            return all.isEmpty() ? matchNothingQuery() : all;
        }
        return QStringLiteral("tag:%1").arg(generatorTag(query.generated));
    }

    const Account scope = account(accountKey);
    if (!scope.isValid())
        return resolvedQuery(query, QString());

    if (query.generated == QStringLiteral("sent")) {
        // The account's OWN sent query, never the all-accounts one wrapped in
        // this account's path. Wrapping gives
        //     path:"a/**" and (path:"a/Sent/**" or path:"b/Sent/**")
        // which returns the right rows because path: is hierarchical, and is
        // still wrong: it double-scopes and works by accident of the syntax.
        const QString sent = scope.sentQuery();
        // Empty when the account configures no sent folder, which is a real
        // case and not a misconfiguration. Returned as-is it would mean "match
        // everything", so a button labelled Sent would show the whole Maildir.
        return sent.isEmpty() ? matchNothingQuery() : sent;
    }

    if (query.generated == QStringLiteral("trash")) {
        // The account's OWN trash query, for the reason spelled out above the
        // sent case: wrapping the all-accounts query in this account's path
        // works by accident of path: being hierarchical.
        const QString trash = scope.trashQuery();
        return trash.isEmpty() ? matchNothingQuery() : trash;
    }

    // A tag filter carries no path of its own, so scoping is exactly what
    // scopedQuery() does. Its parentheses are load-bearing: `path:... and a or
    // b` binds as `(path:... and a) or b`.
    return scope.scopedQuery(
        QStringLiteral("tag:%1").arg(generatorTag(query.generated)));
}

SavedQuery Config::startupSavedQuery() const
{
    // The user's own queries first, so a saved query wins a name collision with
    // a built-in filter: they named theirs deliberately, where the filter's
    // name is one this application chose for them.
    for (const SavedQuery &query : m_savedQueries) {
        if (query.name.compare(m_startupQuery, Qt::CaseInsensitive) == 0)
            return query;
    }

    // Then the built-in filters. Without this, a startup_query of "Inbox"
    // matched nothing once item 93 shipped Inbox as a filter and the duplicated
    // saved query was removed, and the app started on whatever query happened
    // to be first in the file.
    //
    // Matched on the GENERATOR as well as the name, and the generator is what
    // makes this survive translation. A filter's name is a translated label, so
    // under LANG=it_IT the Inbox filter is called "In arrivo" and a config
    // reading `startup_query = Inbox` matched nothing, warned, and fell back to
    // another filter: the user's startup view changed because the UI language
    // did. The generator is stored in queries.json and matched against a closed
    // set, so it is wire format and identical in every locale. Names are still
    // tried first, so a translated name a user copied out of their own UI keeps
    // working.
    const QList<SavedQuery> filters = builtinFilters();
    for (const SavedQuery &filter : filters) {
        if (filter.name.compare(m_startupQuery, Qt::CaseInsensitive) == 0)
            return filter;
    }
    for (const SavedQuery &filter : filters) {
        if (filter.generated.compare(m_startupQuery, Qt::CaseInsensitive) == 0)
            return filter;
    }

    // Named nothing that exists. Falling back to m_savedQueries.first() is what
    // this used to do and it is worse than it looks: after the duplicated
    // entries were removed it could be any leftover query, so a startup view
    // became a search for one sender, and an empty queries.json started nothing
    // at all. A filter is always present, so the fallback can be one.
    //
    // Still not worth a warning: the default is a name the user never wrote.
    return builtinFilter(QStringLiteral("unread"));
}

QList<Account> Config::sendingAccounts() const
{
    QList<Account> sending;
    for (const Account &account : m_accounts) {
        if (account.canSend())
            sending.append(account);
    }
    return sending;
}

Account Config::account(const QString &key) const
{
    for (const Account &a : m_accounts) {
        if (a.key == key)
            return a;
    }
    return {};
}
