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

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

/// One auto-tagging rule, as stored in ~/.config/mailrules/rules.json.
///
/// A rule carries NO scope. The notmuch post-new hook supplies `tag:new`, a
/// dry run supplies nothing and counts against the whole corpus. That split is
/// what lets one rule answer both "what would this tag on arrival" and "what
/// does this match in all my mail".
struct TagRule
{
    QString id;       ///< Stable handle, [a-z0-9-]. Never the tag name: tags
                      ///< contain '/' and can be renamed.
    QString query;    ///< notmuch query, unscoped.
    QString note;     ///< Why the rule is shaped this way. Shown in the dialog.
    QStringList add;
    QStringList remove;
    int stage = 50;   ///< Ascending. Account tags 10, topic rules 50.
    bool enabled = true;

    /// Fields this version of qtmaildir does not understand, kept verbatim and
    /// written back on save. Without this, one save from here silently strips
    /// whatever a newer mailctl wrote, and the shared format would belong to
    /// whichever tool saved last.
    QJsonObject unknown;
};

/// Reads and writes the shared rule store.
///
/// Degrades rather than refusing, exactly as Config does: a malformed rule is
/// dropped with a warning and the rest still load, because one typo must not
/// cost every rule. qtmaildir must never fail to open because of this file.
class TagRules
{
public:
    /// $XDG_CONFIG_HOME/mailrules/rules.json, or ~/.config/... as fallback.
    /// Deliberately not under qtmaildir's own config directory: mailctl reads
    /// the same file and neither tool owns it.
    static QString defaultPath();

    /// Replaces the current contents. Never throws; see warnings().
    void load(const QString &path = QString());

    /// Atomic: QSaveFile writes a temporary and renames, so the hook can never
    /// read a partial file. Returns false if the write failed.
    bool save(const QString &path = QString()) const;

    QList<TagRule> rules() const { return m_rules; }
    void setRules(const QList<TagRule> &rules) { m_rules = rules; }

    /// Enabled rules in execution order: stage ascending, ties in file order.
    QList<TagRule> ordered() const;

    QStringList warnings() const { return m_warnings; }

    /// The one predicate. load() drops or repairs by it, the dialog refuses to
    /// save against it, and mailrules.py enforces the same pattern in the
    /// companion repo. Anything failing this is invisible to the post-new hook.
    static bool isValidId(const QString &id);

    /// A typed name reduced to a legal id: lowercased, every run of anything
    /// else collapsed to one dash, dashes trimmed off both ends.
    ///
    /// Returns an EMPTY string when nothing legal survives ("!!!"), because an
    /// empty id is not writable and the caller must decide the fallback rather
    /// than have one invented here. Already-legal ids pass through untouched,
    /// so loading a good file never rewrites it.
    static QString sanitiseId(const QString &name);

    /// sanitiseId plus a numeric suffix when the result is already taken.
    /// Sanitising is many-to-one, so it manufactures duplicates that load()
    /// would then drop; this is what stops the second rule becoming the first.
    static QString uniqueId(const QString &name, const QStringList &taken);

    /// Every reason these rules would not survive a reload, one string each,
    /// empty when they all would. Written for the save path: the defect this
    /// answers is that save() wrote anything and load() validated, so a rule
    /// could reach the file and never come back.
    static QStringList validate(const QList<TagRule> &rules);

    /// No file yet, as distinct from a file that would not load. A fresh
    /// install is not an error and must not be reported as one.
    bool missing() const { return m_missing; }

private:
    QList<TagRule> m_rules;
    QStringList m_warnings;
    QJsonObject m_unknown;   ///< Unrecognised top-level keys.
    bool m_missing = false;
};
