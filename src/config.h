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

#include "completionentry.h"

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
    /// a provider-plus-mailbox key of 25 characters is a lot of row for one
    /// bit of information. This renames nothing in notmuch, only the display.
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

    /// The saved query to open at startup, by name. Falls back to "Unread"
    /// when unset, and to the first saved query when no query by that name
    /// exists: [queries] is read through childKeys(), which sorts
    /// alphabetically, so "first" would otherwise mean whatever happens to
    /// sort first rather than anything the user chose.
    QString startupQuery() const { return m_startupQuery; }

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

    QList<Account> m_accounts;
    QList<SavedQuery> m_savedQueries;
    QString m_syncCommand;
    QString m_notmuchConfig;
    qreal m_messageZoom = 1.0;
    bool m_completionOnFocus = false;
    int m_markReadDelayMs = 2000;
    SyncOnExit m_syncOnExit = SyncOnExit::Ask;
    QList<CompletionEntry> m_extraMimetypes;
    QString m_startupQuery = QStringLiteral("Unread");

    /// Whether startup_query came from the config rather than being the
    /// built-in default. Only a name the user wrote is worth reporting when
    /// it matches no saved query.
    bool m_startupQueryWasSet = false;
    QStringList m_warnings;
    QStringList m_problems;
};
