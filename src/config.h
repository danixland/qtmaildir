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
#include <QCoreApplication>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "completionentry.h"

class QSettings;

/// One mail account. notmuch has no concept of accounts; it sees a single flat
/// tree. An account is therefore a path prefix within that tree plus an
/// identity.
struct Account
{
    QString key;      ///< INI group suffix, e.g. "work" from [account/work].
    QString name;
    QString address;
    QString maildir;  ///< Relative to notmuch's database.path.
    /// The account's drafts folder, relative to maildir. Optional, exactly as
    /// `sent` is, and absent more often: an account that composes elsewhere
    /// keeps no local drafts folder at all.
    ///
    /// Composing drafts is v2. Reading them is not: the placeholder pane
    /// counts them (item 67), which is why this is no longer unused.
    QString drafts;

    /// The account's sent folder, relative to maildir. Optional and empty for
    /// an account that has none, which is a real case rather than a
    /// misconfiguration: an account may keep no sent mail locally at all.
    ///
    /// A key rather than a <maildir>/Sent convention because the folder is not
    /// uniform across providers. Measured across one real setup: two accounts
    /// use `Sent`, two nest a localised name under a bracketed parent
    /// (`[Provider]/Posta inviata`), and one has no sent folder whatsoever. A
    /// convention would produce an empty view for the nested ones and a wrong
    /// one for the account that has none.
    QString sent;

    /// Chip colour in the thread list. Invalid when unset, in which case one
    /// is generated from the account tag's name.
    QColor color;

    /// Text shown on the chip. Empty falls back to the key, which can be long:
    /// a provider-plus-mailbox key of 25 characters is a lot of row for one
    /// bit of information. This renames nothing in notmuch, only the display.
    QString label;

    /// mbsync channel name, when it differs from the key. Optional, and empty
    /// for most accounts: see syncChannel().
    QString channel;

    /// The mbsync channel to sync this account, for item 49's per-account sync.
    ///
    /// Defaults to the key, which is right for most accounts, but the two are
    /// genuinely separate names and cannot be collapsed. A QSettings section
    /// key may carry dots that the channel does not ([account.mail-first.last]
    /// against the channel `mail-firstlast`), and mbsync exits nonzero on a
    /// channel it does not know, which qtmaildir would report as a failed sync.
    QString syncChannel() const { return channel.isEmpty() ? key : channel; }

    bool isValid() const { return !key.isEmpty() && !maildir.isEmpty(); }

    /// Restricts a notmuch query to this account's subtree.
    QString scopedQuery(const QString &query) const;

    /// Matches this account's sent mail, or empty when `sent` is unset.
    ///
    /// Composes with scopedQuery() rather than replacing it: the account
    /// selector wraps whatever query runs, so a Sent view under one account
    /// intersects to that account's sent mail and cannot leak another's.
    QString sentQuery() const;

    /// Matches this account's drafts, or empty when `drafts` is unset.
    ///
    /// Separate from sentQuery() rather than one parameterised helper: the two
    /// keys are independent, and one real account configures `drafts` with no
    /// `sent` at all.
    QString draftsQuery() const;
};

/// A named query, stored in queries.json.
///
/// Stored as an ORDERED array, which is the whole reason the storage moved out
/// of `[queries]`: QSettings reads a section through childKeys(), which sorts
/// alphabetically and cannot express the order the buttons appear in.
struct SavedQuery
{
    QString name;
    QString query;

    /// Renders as a button in the query row; otherwise it lives in the menu.
    /// The two tiers are the point of the flag: a row that shows every saved
    /// query does not scale past a handful.
    bool pinned = false;

    /// Account KEY, the INI group suffix ("work" from [account.work]), and
    /// empty for a query that spans every account.
    ///
    /// A key rather than a maildir path: the path already lives in the account
    /// section, and storing a second copy here would go stale the moment the
    /// user edits it. Resolve through Config::resolvedQuery().
    QString account;

    /// Names a builtin that COMPOSES this query from the accounts at run time,
    /// rather than storing it. Empty for an ordinary query.
    ///
    /// "sent" is the only one today. Its query is built from every account's
    /// `sent` key, so adding an account or correcting a folder name is a config
    /// edit and nothing else; a stored copy of the same string would go stale
    /// silently. That property is why Sent used to be hardcoded beside the
    /// saved queries instead of living with them, which left one button on the
    /// row that could not be reordered, renamed, unpinned or removed.
    ///
    /// Storing the GENERATOR rather than its output keeps both: the query stays
    /// live, and the entry is an ordinary row the user owns.
    QString generated;

    /// Lists messages rather than threads. Set for the sent view, where a
    /// thread would fold every reply back into the conversation the user sent
    /// one message into.
    bool flat = false;

    bool isGenerated() const { return !generated.isEmpty(); }

    /// Keys this build does not understand, preserved verbatim so a file
    /// written by a later version survives a save from this one.
    QJsonObject unknown;
};

/// Reads ~/.config/qtmaildir/qtmaildir.conf.
///
/// The Maildir path is deliberately NOT configurable here: notmuch already
/// stores it as database.path and libnotmuch reads it. Duplicating it would
/// allow the GUI to index a different tree than the CLI.
class Config
{
    // Not a QObject: this class is a value holder read from every thread. The
    // macro gives it tr() for the built-in filters' NAMES, which are the labels
    // on the query row's buttons and therefore user-facing.
    Q_DECLARE_TR_FUNCTIONS(Config)

public:
    /// Path used when load() is called with no argument.
    static QString defaultPath();

    void load(const QString &path);

    QList<Account> accounts() const { return m_accounts; }
    Account account(const QString &key) const;

    /// In document order, which IS the display order. Never sort this.
    QList<SavedQuery> savedQueries() const { return m_savedQueries; }
    void setSavedQueries(const QList<SavedQuery> &queries)
    {
        m_savedQueries = queries;
    }

    /// Path of queries.json, derived from the config file's own directory so a
    /// test can point load() at a temporary tree and get both files there.
    static QString queriesPath(const QString &configPath);

    /// Writes queries.json. False when the file could not be written, or when
    /// the loaded file had a version this build refuses: overwriting a
    /// newer-format file with a lossy reading of it is the one outcome worth
    /// preventing outright.
    bool saveSavedQueries() const;

    /// The query as it should be run: scoped to its account when it names one.
    ///
    /// Composes through Account::scopedQuery(), whose parentheses are
    /// load-bearing. `path:... and a or b` binds as `(path:... and a) or b`, so
    /// an unparenthesised disjunction escapes its scope and matches every
    /// account. An account key naming nothing returns the bare query rather
    /// than a scope built from an empty maildir, which would be path:"/**".
    QString resolvedQuery(const SavedQuery &query) const;

    /// The query as it should be run in one account's scope, or across all of
    /// them when `accountKey` is empty.
    ///
    /// This is what makes a built-in filter COMPOSE with the account dropdown
    /// rather than fight it (item 93). A generator is asked for the account's
    /// own query, never handed its all-accounts query to wrap: wrapping gives
    ///     path:"a/**" and (path:"a/Sent/**" or path:"b/Sent/**")
    /// which returns the right rows only because path: is hierarchical, and
    /// says something other than what is meant.
    ///
    /// An ordinary saved query ignores `accountKey` and keeps resolving through
    /// its OWN stored account, which is the behaviour item 90 leaves alone: a
    /// saved query is a destination and states its own scope.
    QString resolvedQuery(const SavedQuery &query,
                          const QString &accountKey) const;

    /// The built-in filters, in the order they appear on the query row.
    ///
    /// Shipped rather than stored: these are not the user's saved queries and
    /// are not in queries.json at all. The row used to be whatever the user had
    /// pinned, which is how it drifted (item 93).
    static QList<SavedQuery> builtinFilters();

    /// One built-in filter by generator name, or a default-constructed
    /// SavedQuery when the name is not one.
    static SavedQuery builtinFilter(const QString &generator);

    /// Whether `generator` is one this build knows how to resolve.
    ///
    /// A closed set, so a typo is reported on load rather than producing a
    /// button that silently finds nothing.
    static bool isKnownGenerator(const QString &generator);

    /// A query that deliberately matches no message.
    ///
    /// Needed because an EMPTY query means "match everything" to notmuch, so a
    /// generator with nothing to match cannot simply return one: Sent under an
    /// account that configures no sent folder would show the entire Maildir.
    static QString matchNothingQuery();

    /// Empty when unset; the caller disables the Sync button in that case.
    QString syncCommand() const { return m_syncCommand; }

    /// Toolbar icon size in pixels, 16 to 64, defaulting to 24.
    ///
    /// The desktop's own PM_ToolBarIconSize was the obvious default and was
    /// rejected empirically: it reports 16 here, which is a small target now
    /// that the toolbar follows the platform's "icon only" style and the icon
    /// is the whole control. Setting this to 16 restores the theme's value.
    ///
    /// Clamped rather than trusted: a 4px icon is invisible and a 4000px one
    /// makes the toolbar taller than the window, and neither is recoverable
    /// from the UI the value just broke.
    int toolbarIconSize() const { return m_toolbarIconSize; }

    /// The sync script's log file, read to learn the outcome of a sync this
    /// process did not start (item 54).
    ///
    /// Never empty: an unset key falls back to where assets/mailsync.sh writes
    /// by default. An empty value would make every background sync report
    /// SyncOutcome::Unknown, and the pending-edit indicator would then never
    /// clear on a cron sync, which is exactly the defect this exists to fix.
    QString syncLog() const { return m_syncLog; }

    /// Optional alternate notmuch config file. Empty means "let notmuch decide".
    QString notmuchConfig() const { return m_notmuchConfig; }

    /// Matches every configured account's sent mail, or empty when no account
    /// configures one.
    ///
    /// Joins only the NON-EMPTY sentQuery() results. An account without a
    /// `sent` key contributes nothing, and joining it anyway would leave a bare
    /// `or` in the query. notmuch does not reject that: `A or  or B` returns
    /// 190 messages where the correct pair returns 211, measured directly. A
    /// malformed query that still returns plausible mail is the failure that
    /// ships, which is why the join lives here and is tested rather than being
    /// open-coded at the call site.
    QString allSentQuery() const;

    /// Matches every configured account's drafts, or empty when none has one.
    ///
    /// Joins only the NON-EMPTY draftsQuery() results, for the same reason
    /// allSentQuery() does: notmuch accepts a bare "or" without complaint and
    /// silently answers a different question.
    QString allDraftsQuery() const;

    /// A QDateTime::toString() pattern for the date on a card, or empty for the
    /// system locale's short format.
    ///
    /// Empty is both the default and what an unusable pattern falls back to, so
    /// a caller never has to distinguish "unset" from "rejected": either way
    /// the locale decides. Validated at load, because toString() with a pattern
    /// carrying no date field returns the pattern verbatim, which would print
    /// the same fixed string on every card rather than failing visibly.
    QString dateFormat() const { return m_dateFormat; }

    /// The saved query to open at startup, by name. Falls back to "Unread"
    /// when unset, and to the first saved query when no query by that name
    /// exists: [queries] is read through childKeys(), which sorts
    /// alphabetically, so "first" would otherwise mean whatever happens to
    /// sort first rather than anything the user chose.
    QString startupQuery() const { return m_startupQuery; }

    /// Account key the dropdown starts on, or empty for "All accounts".
    ///
    /// Scoping happens because the built-in filters COMPOSE with the dropdown,
    /// so setting it before the startup query runs is the whole mechanism: this
    /// key does not need to reach the query builders at all.
    ///
    /// Validated on load. A name matching no account is reported and left
    /// empty, rather than passed on to a dropdown that has no such entry and
    /// would silently stay on "All accounts".
    QString startupAccount() const { return m_startupAccount; }

    /// The saved query startupQuery() names, or the first one when it names
    /// nothing that exists. A default-constructed SavedQuery when there are
    /// none at all.
    SavedQuery startupSavedQuery() const;

    /// Starting message-pane zoom for a profile with no saved UI state. Once
    /// the user zooms, the state file remembers that instead, so this is only
    /// ever the default. Clamped by MessageView::clampZoom() on use.
    qreal messageZoom() const { return m_messageZoom; }

    /// Whether focusing an empty query bar opens the completion popup. Off by
    /// default: it is helpful when learning the query language and intrusive
    /// once it is known. The manual trigger works regardless.
    bool completionOnFocus() const { return m_completionOnFocus; }

    /// What to do about unsynced edits when the window closes.
    enum class SyncOnExit {
        Ask,     ///< Prompt, offering to sync, quit anyway, or stay. The default.
        Always,  ///< Sync without asking, then quit once it finishes.
        Never,   ///< Quit silently, which is the behaviour before this existed.
    };

    SyncOnExit syncOnExit() const { return m_syncOnExit; }

    /// How long an opened thread stays unread before it is marked read.
    ///
    /// Three meanings, all deliberate: a positive value is the delay in
    /// milliseconds, 0 marks read immediately, and any negative value disables
    /// the behaviour so a thread stays unread until toggled by hand.
    int markReadDelayMs() const { return m_markReadDelayMs; }

    /// How long to wait after a tag edit before syncing it out on the user's
    /// behalf. Item 71.
    ///
    /// Same three meanings as markReadDelayMs() above, and deliberately so: a
    /// positive value is the debounce in milliseconds, 0 syncs on the next trip
    /// through the event loop, and any negative value disables the behaviour so
    /// edits wait for a manual sync or the user's cron job, which is what every
    /// release before this one did.
    ///
    /// Defaults to 2000. The delay is a debounce, not a schedule: each edit
    /// restarts it, so a burst of tagging produces one sync after the burst
    /// rather than one per tag.
    int autoSyncDelayMs() const { return m_autoSyncDelayMs; }

    /// User-supplied mimetype completions, APPENDED to the built-in list.
    /// Appending rather than replacing means a typo cannot leave completion
    /// worse off than the defaults. Mimetypes are the only completion list a
    /// user can extend, because they are the only one with no enumerator and
    /// an open-ended set: prefixes are fixed, paths come from the configured
    /// accounts, dates are closed, tags come from the database.
    QList<CompletionEntry> extraMimetypes() const { return m_extraMimetypes; }

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

    /// Reads queries.json, or migrates [queries] when it is absent. Called by
    /// load(), which has already parsed the INI by then.
    void loadSavedQueries(const QString &configPath, QSettings &settings);

    QList<Account> m_accounts;
    QList<SavedQuery> m_savedQueries;

    /// Where saveSavedQueries() writes, remembered from load().
    QString m_queriesPath;

    /// Top-level keys of queries.json this build does not understand.
    QJsonObject m_queriesUnknown;

    /// Set when the file was refused for its version. Blocks the save, so a
    /// document from a newer build is never overwritten with less than it held.
    bool m_queriesRefused = false;

    QString m_syncCommand;
    QString m_syncLog;
    int m_toolbarIconSize = 24;
    QString m_notmuchConfig;
    QString m_dateFormat;
    qreal m_messageZoom = 1.0;
    bool m_completionOnFocus = false;
    int m_markReadDelayMs = 2000;
    int m_autoSyncDelayMs = 2000;
    SyncOnExit m_syncOnExit = SyncOnExit::Ask;
    QList<CompletionEntry> m_extraMimetypes;
    QString m_startupQuery = QStringLiteral("Unread");

    /// Empty means "All accounts", which is the same convention every other
    /// account key here follows.
    QString m_startupAccount;

    /// Whether startup_query came from the config rather than being the
    /// built-in default. Only a name the user wrote is worth reporting when
    /// it matches no saved query.
    bool m_startupQueryWasSet = false;
    QStringList m_warnings;
    QStringList m_problems;
};
