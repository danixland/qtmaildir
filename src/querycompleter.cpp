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

#include <QCompleter>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QResizeEvent>
#include <QStandardItemModel>
#include <QStyledItemDelegate>

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

/// Draws the description greyed and right-aligned beside the value.
///
/// The two are drawn into disjoint halves of the row rather than simply
/// painted on top of each other: a long value ("application/vnd.oasis..."
/// exceeds the popup width on its own) would otherwise run underneath the
/// description and render both unreadable.
class CompletionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const QModelIndex sibling = index.sibling(index.row(), 1);
        const QString description = sibling.data(Qt::DisplayRole).toString();
        if (description.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        const QFontMetrics metrics(option.font);
        const int gap = 12;
        const int rightMargin = 6;

        // The description never takes more than its share, so a long value
        // keeps room to be legible and a long description gets elided too.
        const int available = option.rect.width() - gap - rightMargin;
        int descriptionWidth = qMin(metrics.horizontalAdvance(description),
                                    available / 2);
        descriptionWidth = qMax(descriptionWidth, 0);

        // Let the base class draw the selection background and the value, but
        // only into the part of the row the description does not claim.
        QStyleOptionViewItem valueOption = option;
        valueOption.rect = option.rect.adjusted(
            0, 0, -(descriptionWidth + gap + rightMargin), 0);
        QStyledItemDelegate::paint(painter, valueOption, index);

        // The background belongs to the whole row, so repaint the strip the
        // base class just left untouched before drawing the description.
        painter->save();
        QRect descriptionRect = option.rect;
        descriptionRect.setLeft(valueOption.rect.right() + 1);
        if (option.state & QStyle::State_Selected)
            painter->fillRect(descriptionRect, option.palette.highlight());

        painter->setPen(option.palette.color(QPalette::Disabled, QPalette::Text));
        painter->drawText(descriptionRect.adjusted(0, 0, -rightMargin, 0),
                          Qt::AlignRight | Qt::AlignVCenter,
                          metrics.elidedText(description, Qt::ElideRight,
                                             descriptionWidth));
        painter->restore();
    }
};

} // namespace

/// A completion list with a non-selectable footer strip below the items.
///
/// The footer is a child label sitting in space reserved by
/// setViewportMargins, not a model row. A row would be filtered away by
/// QCompleter's filter model on the first keystroke that did not match it,
/// and could be selected and inserted, producing a query that errors.
///
/// QCompleter::setPopup takes a QAbstractItemView, so the label cannot simply
/// be laid out beside the view in a container widget: the container would not
/// be accepted. Reserving margin inside the view is what fits that signature.
class CompletionPopup : public QListView
{
public:
    explicit CompletionPopup(QWidget *parent = nullptr)
        : QListView(parent), m_hint(new QLabel(this))
    {
        // Illustrative, not a promise: notmuch's date parser is permissive and
        // the set it accepts varies between builds.
        m_hintText = QueryCompleter::tr(
            "also accepts free-form dates, e.g. 2026-01-15 or 15/01/2026..today");
        m_hint->setText(m_hintText);
        m_hint->setTextInteractionFlags(Qt::NoTextInteraction);
        m_hint->setMargin(4);

        QFont hintFont = m_hint->font();
        hintFont.setItalic(true);
        hintFont.setPointSizeF(hintFont.pointSizeF() * 0.9);
        m_hint->setFont(hintFont);

        m_hint->hide();
    }

    void setHintVisible(bool visible)
    {
        if (visible == !m_hint->isHidden())
            return;
        m_hint->setVisible(visible);
        setViewportMargins(0, 0, 0,
                           visible ? m_hint->sizeHint().height() : 0);
        layoutHint();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QListView::resizeEvent(event);
        layoutHint();
    }

private:
    void layoutHint()
    {
        // isHidden, not isVisible: while the popup window is still unmapped
        // every child reports invisible, which would skip the only layout pass
        // that runs before the popup appears.
        if (m_hint->isHidden())
            return;

        // The popup is only as wide as the line edit, which is routinely
        // narrower than the hint. Elide rather than let it clip mid-word.
        const int textWidth = width() - 2 * m_hint->margin();
        m_hint->setText(QFontMetrics(m_hint->font())
                            .elidedText(m_hintText, Qt::ElideRight, textWidth));

        const int height = m_hint->sizeHint().height();
        m_hint->setGeometry(0, this->height() - height, width(), height);
    }

    QLabel *m_hint;
    QString m_hintText;
};

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
    if (!m_edit)
        return;

    m_model = new QStandardItemModel(this);

    m_completer = new QCompleter(m_model, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionColumn(0);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    // Tag hierarchies are the reason completion exists here, and a user who
    // types "amazon" means shopping/amazon.
    m_completer->setFilterMode(Qt::MatchContains);

    m_popup = new CompletionPopup;
    m_completer->setPopup(m_popup);
    // After setPopup, never before: setPopup installs a plain
    // QStyledItemDelegate of its own and discards whatever was set already,
    // which silently drops the description column.
    m_popup->setItemDelegate(new CompletionDelegate(m_popup));

    // setWidget, NOT QLineEdit::setCompleter. setCompleter hands completion to
    // the line edit, which then overwrites completionPrefix with the widget's
    // ENTIRE text on every keystroke. The prefix must be the stem instead, so
    // the whole-line prefix matches nothing and the popup stops appearing after
    // the first token. setWidget still gives the completer the anchor it needs:
    // complete() dereferences widget() unconditionally and crashes without one.
    m_completer->setWidget(m_edit);

    // With the line edit no longer driving completion, every edit has to open
    // the popup explicitly.
    connect(m_edit, &QLineEdit::textEdited, this, &QueryCompleter::triggerCompletion);
    connect(m_edit, &QLineEdit::cursorPositionChanged,
            this, [this]() { updateContext(); });

    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, [this](const QModelIndex &index) {
        // The mouse path. It must chain exactly like Tab does: the user's
        // report was that clicking "tag:" offered no tags afterwards.
        acceptCompletion(index.data(Qt::DisplayRole).toString());
        continueCompletion();
    });

    // Two filters, because the two jobs need different vantage points.
    //
    // The line edit filter handles FocusIn, which by definition arrives while
    // the popup is down and the edit is the delivery target, so watching the
    // widget is both sufficient and correctly scoped.
    m_edit->installEventFilter(this);

    // The key filter must be application-wide. Showing the popup takes focus
    // away from the line edit (focusWidget() becomes null) and the popup window
    // grabs the keyboard, so keys pressed while it is up are delivered to the
    // popup and a filter on the line edit never runs. That is precisely when
    // Tab and Return need to be intercepted. Only an application filter sees
    // those events. It is inert unless our own popup is visible.
    if (QCoreApplication *app = QCoreApplication::instance())
        app->installEventFilter(this);
}

void QueryCompleter::triggerCompletion()
{
    if (!m_edit || !m_completer)
        return;

    updateContext();
    if (m_context.kind == CompletionContext::None)
        return;

    // The prefix must be set explicitly: complete() filters against whatever
    // prefix QCompleter last derived from the line edit's full text, which is
    // not the stem once a keyword or a range bound is in play.
    m_completer->setCompletionPrefix(m_context.stem);
    m_completer->complete();
}

bool QueryCompleter::eventFilter(QObject *watched, QEvent *event)
{
    // Only the empty-bar case: once there is text, ordinary typing has
    // already driven completion.
    if (watched == m_edit && event->type() == QEvent::FocusIn
        && m_config.completionOnFocus() && m_edit->text().isEmpty()) {
        triggerCompletion();
        return QObject::eventFilter(watched, event);
    }

    // This filter is installed on the application, so it sees every key in the
    // process. Claim nothing unless our own popup is on screen, otherwise the
    // keyboard breaks everywhere else in the window.
    if (event->type() != QEvent::KeyPress || !popupVisible())
        return QObject::eventFilter(watched, event);

    // A key this filter is itself redelivering. sendEvent re-runs application
    // event filters, so without this the forwarded key comes straight back and
    // recurses until the stack is gone.
    if (m_forwarding)
        return QObject::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    switch (keyEvent->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Enter:
    case Qt::Key_Return: {
        // Accept whatever the popup highlights. A freshly opened popup has no
        // current row, so fall back to the first entry: the user sees it at the
        // top of the list and expects Tab to take it.
        QModelIndex index = m_popup->currentIndex();
        if (!index.isValid())
            index = m_popup->model()->index(0, 0);
        if (!index.isValid())
            return QObject::eventFilter(watched, event);

        acceptCompletion(index.data(Qt::DisplayRole).toString());
        // Hide before reopening: accepting "tag:" moves the caret into value
        // position, and the popup has to be rebuilt around the new context
        // rather than left showing the prefix list.
        m_popup->hide();
        continueCompletion();
        // Consume it. Tab would otherwise move focus to the next widget, and
        // Return would run the half-typed query or reach the thread list.
        return true;
    }
    case Qt::Key_Escape:
        m_popup->hide();
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: {
        // Navigation belongs to the popup, which is not the focus widget while
        // the user is typing in the bar.
        //
        // m_forwarding is what keeps this from recursing; see the guard at the
        // top of the filter.
        m_forwarding = true;
        QCoreApplication::sendEvent(m_popup, event);
        m_forwarding = false;
        return true;
    }
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

bool QueryCompleter::popupVisible() const
{
    return m_popup && m_popup->isVisible();
}

void QueryCompleter::acceptCompletion(const QString &value)
{
    if (!m_edit)
        return;

    // Replace exactly the span the tokenizer identified. QCompleter's own
    // insertion replaces the whole "completion prefix", which is not the same
    // span once a prefix or a range bound is involved.
    QString text = m_edit->text();
    if (m_context.replaceFrom < 0 || m_context.replaceLength < 0
        || m_context.replaceFrom + m_context.replaceLength > text.size())
        return;

    text.replace(m_context.replaceFrom, m_context.replaceLength, value);

    // Setting the text re-emits cursorPositionChanged, which would recompute
    // the context from a caret Qt has not moved yet. Block that so the caret
    // lands past the insertion first.
    const QSignalBlocker blocker(m_edit);
    m_edit->setText(text);
    m_edit->setCursorPosition(m_context.replaceFrom + value.size());

    // The context is now stale in every field; recompute it from the caret we
    // just placed so a second accept without an intervening keystroke is sane.
    m_context = completionContext(m_edit->text(), m_edit->cursorPosition());
}

void QueryCompleter::continueCompletion()
{
    if (!m_edit || !m_completer)
        return;

    // acceptCompletion() already recomputed the context from the caret it
    // placed, so this reads the situation the accept created.
    if (m_context.kind == CompletionContext::None)
        return;

    // Reopen only for a value whose keyword actually offers candidates.
    // Accepting a prefix ("tag:") lands here with an empty stem and the tag
    // list waiting, which is the case worth reopening for. Accepting a value
    // ("tag:unread") leaves a stem that already equals the only match, so
    // reopening would show a one-entry popup that swallows the next Return.
    if (m_context.kind != CompletionContext::Value)
        return;

    const QStringList candidates = candidatesFor(m_context);
    if (candidates.isEmpty())
        return;

    // A stem that is already a complete candidate needs nothing more. This is
    // also what stops the reopen from recurring: the next accept always
    // produces such a stem, so the chain terminates after one step.
    if (candidates.contains(m_context.stem, Qt::CaseInsensitive))
        return;

    rebuildModel(m_context);
    m_completer->setCompletionPrefix(m_context.stem);
    m_completer->complete();
}

void QueryCompleter::updateContext()
{
    if (!m_edit)
        return;
    m_context = completionContext(m_edit->text(), m_edit->cursorPosition());
    rebuildModel(m_context);
}

void QueryCompleter::rebuildModel(const CompletionContext &context)
{
    if (!m_model)
        return;

    m_model->clear();

    const QList<CompletionEntry> entries = entriesFor(context);
    for (const CompletionEntry &entry : entries) {
        // Matching runs on column 0 only, so a description never influences
        // which candidates are offered.
        auto *value = new QStandardItem(entry.value);
        auto *description = new QStandardItem(entry.description);
        value->setEditable(false);
        description->setEditable(false);
        m_model->appendRow({ value, description });
    }

    if (m_popup) {
        m_popup->setHintVisible(context.kind == CompletionContext::Value
                                && context.prefix == QStringLiteral("date"));
    }
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
