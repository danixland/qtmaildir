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

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "completionentry.h"

class Config;
class QCompleter;
class QLineEdit;
class QStandardItemModel;

/// The completion popup, defined in the .cpp: a list view with a footer strip.
class CompletionPopup;

/// Where the cursor sits in a query, and therefore what should be offered.
///
/// A plain value type produced by a pure function so the parsing rules can be
/// tested without a widget or a database.
struct CompletionContext
{
    enum Kind {
        None,    ///< Complete nothing: inside a quoted literal, for instance.
        Prefix,  ///< Complete a query keyword: tag:, date:, and, or, not.
        Value,   ///< Complete a value for `prefix`.
    };

    Kind kind = None;

    /// For Value, the keyword left of ':', lowercased. Empty for Prefix.
    QString prefix;

    /// The text being matched against the candidates.
    QString stem;

    /// The exact span an accepted completion overwrites. Covers only the text
    /// being completed, so accepting never disturbs neighbouring text.
    int replaceFrom = 0;
    int replaceLength = 0;

    /// Whether candidates that are themselves ranges may be offered.
    ///
    /// The relative date entries ("1week..") are complete open-ended ranges.
    /// Offering one inside an existing range yields date:1week....today, which
    /// is malformed, so they are withheld once a range is underway.
    bool allowRangeEntries = true;
};

/// Decides what the cursor position implies about completion.
///
/// `cursor` is an offset into `text`, as QLineEdit::cursorPosition() returns.
CompletionContext completionContext(const QString &text, int cursor);

/// The notmuch query keywords, with descriptions. Hardcoded: notmuch exposes
/// no way to enumerate its own prefixes, so this list must track releases by
/// hand. See the spec's Consequences section.
QList<CompletionEntry> prefixVocabulary();

/// Symbolic and relative date values. Absolute dates are not enumerable and
/// are covered by the free-form hint in the popup footer instead.
QList<CompletionEntry> dateVocabulary();

/// The built-in mimetypes, before the user's extra_mimetypes are appended.
QList<CompletionEntry> mimetypeVocabulary();

/// Completion for the notmuch query bar.
///
/// completionContext() above decides which context the cursor sits in; this
/// class owns the candidates offered for that context.
class QueryCompleter : public QObject
{
    Q_OBJECT
public:
    /// `edit` may be null in tests that exercise candidate selection only.
    QueryCompleter(QLineEdit *edit, const Config &config,
                   QObject *parent = nullptr);

    /// Replaces the tag candidates. Called with the worker's allTagsReady.
    void setTags(const QStringList &tags);

    /// The candidate values for a context, in the order they are offered.
    QStringList candidatesFor(const CompletionContext &context) const;

    /// Recomputes the context from the line edit and refills the popup model.
    void updateContext();

    /// Inserts `value` over the span the last updateContext() identified.
    ///
    /// Public so a test can drive the accept path directly. Going through
    /// synthetic key events instead would test the keyboard layout, not this.
    void acceptCompletion(const QString &value);

private:
    QList<CompletionEntry> entriesFor(const CompletionContext &context) const;
    void rebuildModel(const CompletionContext &context);

    QLineEdit *m_edit = nullptr;
    const Config &m_config;
    QStringList m_tags;

    QCompleter *m_completer = nullptr;
    QStandardItemModel *m_model = nullptr;
    CompletionPopup *m_popup = nullptr;
    CompletionContext m_context;
};
