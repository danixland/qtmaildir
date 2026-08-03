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

#include "querycompleter.h"

#include "config.h"

#include <QCoreApplication>

namespace {

// The vocabulary lives in free functions, not in a QObject, so there is no
// inherited tr(). Q_DECLARE_TR_FUNCTIONS gives this namespace its own tr()
// bound to an explicit context, which is what lupdate scans for. Calling
// QObject::tr() here would compile but file every string under the "QObject"
// context, mixing the vocabulary in with unrelated strings.
class VocabularyStrings
{
    Q_DECLARE_TR_FUNCTIONS(VocabularyStrings)
};

/// Whether the cursor sits inside a double-quoted literal. Counts quotes from
/// the start: an odd count before the cursor means the quote is still open.
bool insideQuotes(const QString &text, int cursor)
{
    int quotes = 0;
    for (int i = 0; i < cursor; ++i) {
        if (text.at(i) == QLatin1Char('"'))
            ++quotes;
    }
    return (quotes % 2) != 0;
}

/// Start of the token the cursor sits in. The boundary is whitespace or '(',
/// so "tag:inbox and su" has its last token starting at 14, not at 0.
int tokenStart(const QString &text, int cursor)
{
    int start = cursor;
    while (start > 0) {
        const QChar c = text.at(start - 1);
        if (c.isSpace() || c == QLatin1Char('('))
            break;
        --start;
    }
    return start;
}

/// End of the token the cursor sits in, using the same boundary characters as
/// tokenStart plus ')'. The token must extend past the cursor: a range
/// separator to the right of the cursor decides which bound is being edited,
/// so truncating the token at the cursor would hide it.
int tokenEnd(const QString &text, int cursor)
{
    int end = cursor;
    while (end < text.size()) {
        const QChar c = text.at(end);
        if (c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char(')'))
            break;
        ++end;
    }
    return end;
}

} // namespace

QList<CompletionEntry> prefixVocabulary()
{
    return {
        { QStringLiteral("tag:"),        VocabularyStrings::tr("messages with a tag") },
        { QStringLiteral("is:"),         VocabularyStrings::tr("same as tag:") },
        { QStringLiteral("from:"),       VocabularyStrings::tr("sender address or name") },
        { QStringLiteral("to:"),         VocabularyStrings::tr("recipient, including Cc") },
        { QStringLiteral("subject:"),    VocabularyStrings::tr("words in the subject") },
        { QStringLiteral("date:"),       VocabularyStrings::tr("a date or a range") },
        { QStringLiteral("attachment:"), VocabularyStrings::tr("attachment filename") },
        { QStringLiteral("mimetype:"),   VocabularyStrings::tr("attachment content type") },
        { QStringLiteral("folder:"),     VocabularyStrings::tr("Maildir folder name") },
        { QStringLiteral("path:"),       VocabularyStrings::tr("directory below the Maildir root") },
        { QStringLiteral("thread:"),     VocabularyStrings::tr("a thread id") },
        { QStringLiteral("id:"),         VocabularyStrings::tr("a single message id") },
        { QStringLiteral("and"),         VocabularyStrings::tr("both conditions") },
        { QStringLiteral("or"),          VocabularyStrings::tr("either condition") },
        { QStringLiteral("not"),         VocabularyStrings::tr("exclude what follows") },
    };
}

QList<CompletionEntry> dateVocabulary()
{
    return {
        { QStringLiteral("today"),      VocabularyStrings::tr("since midnight") },
        { QStringLiteral("yesterday"),  VocabularyStrings::tr("the previous day") },
        { QStringLiteral("this_week"),  VocabularyStrings::tr("the current week") },
        { QStringLiteral("last_week"),  VocabularyStrings::tr("the week before this one") },
        { QStringLiteral("this_month"), VocabularyStrings::tr("the current month") },
        { QStringLiteral("last_month"), VocabularyStrings::tr("the month before this one") },
        { QStringLiteral("this_year"),  VocabularyStrings::tr("the current year") },
        // These two are complete open-ended ranges, hence the trailing "..".
        { QStringLiteral("1week.."),    VocabularyStrings::tr("the last seven days") },
        { QStringLiteral("1month.."),   VocabularyStrings::tr("the last month") },
    };
}

QList<CompletionEntry> mimetypeVocabulary()
{
    return {
        { QStringLiteral("application/pdf"), VocabularyStrings::tr("PDF document") },
        { QStringLiteral("image/jpeg"),      VocabularyStrings::tr("JPEG image") },
        { QStringLiteral("image/png"),       VocabularyStrings::tr("PNG image") },
        { QStringLiteral("text/html"),       VocabularyStrings::tr("HTML document") },
        { QStringLiteral("application/zip"), VocabularyStrings::tr("ZIP archive") },
    };
}

CompletionContext completionContext(const QString &text, int cursor)
{
    CompletionContext ctx;

    if (cursor < 0 || cursor > text.size())
        return ctx;

    if (insideQuotes(text, cursor))
        return ctx;   // kind stays None

    const int start = tokenStart(text, cursor);
    const int end = tokenEnd(text, cursor);
    const QString token = text.mid(start, end - start);

    // Everything the user has typed up to the caret. Candidates are matched
    // against this, never against text still sitting to the right of it.
    const QString typed = text.mid(start, cursor - start);

    const int colon = token.indexOf(QLatin1Char(':'));
    if (colon < 0 || cursor <= start + colon) {
        // No prefix yet, or the caret is still inside the keyword itself.
        ctx.kind = CompletionContext::Prefix;
        ctx.stem = typed;
        ctx.replaceFrom = start;
        ctx.replaceLength = typed.size();
        return ctx;
    }

    ctx.kind = CompletionContext::Value;
    ctx.prefix = token.left(colon).toLower();

    const QString value = token.mid(colon + 1);
    const int valueStart = start + colon + 1;

    // A range is two independent values. Complete whichever side the cursor
    // is in, leaving the other untouched.
    const int separator = value.indexOf(QStringLiteral(".."));
    if (separator < 0) {
        ctx.stem = text.mid(valueStart, cursor - valueStart);
        ctx.replaceFrom = valueStart;
        ctx.replaceLength = ctx.stem.size();
        return ctx;
    }

    ctx.allowRangeEntries = false;

    const int cursorInValue = cursor - valueStart;
    if (cursorInValue <= separator) {
        // stem uses the cursor offset while replaceLength covers the whole
        // side: matching runs on what has been typed so far, but accepting
        // replaces the entire bound, so completing mid-word leaves no tail.
        ctx.stem = value.left(cursorInValue);
        ctx.replaceFrom = valueStart;
        ctx.replaceLength = separator;
    } else {
        const int upperStart = separator + 2;
        ctx.stem = value.mid(upperStart, cursorInValue - upperStart);
        ctx.replaceFrom = valueStart + upperStart;
        ctx.replaceLength = value.size() - upperStart;
    }
    return ctx;
}

QueryCompleter::QueryCompleter(QLineEdit *edit, const Config &config,
                               QObject *parent)
    : QObject(parent), m_edit(edit), m_config(config)
{
}

void QueryCompleter::setTags(const QStringList &tags)
{
    m_tags = tags;
}

QList<CompletionEntry> QueryCompleter::entriesFor(
    const CompletionContext &context) const
{
    if (context.kind == CompletionContext::None)
        return {};

    if (context.kind == CompletionContext::Prefix)
        return prefixVocabulary();

    // notmuch treats is:x as a synonym for tag:x, so both take the tag list.
    if (context.prefix == QStringLiteral("tag")
        || context.prefix == QStringLiteral("is")) {
        QList<CompletionEntry> entries;
        entries.reserve(m_tags.size());
        for (const QString &tag : m_tags)
            entries.append({ tag, QString() });
        return entries;
    }

    if (context.prefix == QStringLiteral("date")) {
        QList<CompletionEntry> entries;
        const QList<CompletionEntry> vocabulary = dateVocabulary();
        for (const CompletionEntry &entry : vocabulary) {
            // Entries that are themselves ranges cannot go inside a range.
            if (!context.allowRangeEntries
                && entry.value.contains(QStringLiteral("..")))
                continue;
            entries.append(entry);
        }
        return entries;
    }

    if (context.prefix == QStringLiteral("mimetype")) {
        QList<CompletionEntry> entries = mimetypeVocabulary();
        entries.append(m_config.extraMimetypes());
        return entries;
    }

    if (context.prefix == QStringLiteral("path")) {
        QList<CompletionEntry> entries;
        const QList<Account> accounts = m_config.accounts();
        for (const Account &account : accounts) {
            if (account.maildir.isEmpty())
                continue;
            entries.append({ account.maildir,
                             VocabularyStrings::tr("account directory") });
            entries.append({ account.maildir + QStringLiteral("/**"),
                             VocabularyStrings::tr("and everything below it") });
        }
        return entries;
    }

    // from:, to:, folder:, subject:, attachment:, thread:, id: complete no
    // values. Addresses need an enumerator libnotmuch does not expose;
    // folder: matches a Maildir folder name that config cannot enumerate, and
    // the rest are free text.
    return {};
}

QStringList QueryCompleter::candidatesFor(const CompletionContext &context) const
{
    QStringList values;
    const QList<CompletionEntry> entries = entriesFor(context);
    values.reserve(entries.size());
    for (const CompletionEntry &entry : entries)
        values.append(entry.value);
    return values;
}
