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

#include "composewindow.h"

#include <QTemporaryDir>

#include "draftstore.h"
#include "messagebuilder.h"
#include "mimeparser.h"
#include "messagesender.h"
#include "senddialog.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

/// Splits a comma-separated recipient field into addresses.
///
/// Splitting on commas is WRONG for a raw header, which is why
/// ComposeContextBuilder::parseAddressHeader parses instead. It is right here
/// and only here: this is a field the user typed, and the composer's own
/// rendering of it joins with ", ". A display name containing a comma has to
/// be quoted by the user, exactly as it has to be in the wire format, and
/// MessageBuilder is what turns each entry into a mailbox.
QStringList splitRecipients(const QString &text)
{
    QStringList out;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

/// Everything about a message the user can change, as one comparable string.
///
/// Joined with a character no field can contain, because concatenating them
/// bare lets a change move a boundary without changing the whole: a subject
/// "ab" with body "c" and a subject "a" with body "bc" would produce the same
/// string and the second edit would never be saved. A unit separator (U+001F)
/// cannot be typed into a QLineEdit or a QPlainTextEdit and cannot appear in a
/// file path.
QString fingerprintOf(const OutgoingMessage &message)
{
    const QChar sep(QChar(0x1F));
    return message.accountKey + sep + message.to.join(sep) + sep
           + message.cc.join(sep) + sep + message.bcc.join(sep) + sep
           + message.subject + sep + message.markdownBody + sep
           + (message.sendHtml ? QStringLiteral("1") : QStringLiteral("0")) + sep
           + message.attachments.join(sep);
}

}  // namespace

ComposeWindow::ComposeWindow(const ComposeContext &context,
                             const Config &config, const QString &mailRoot,
                             QWidget *parent)
    : QMainWindow(parent)
    , m_context(context)
    , m_config(config)
    , m_mailRoot(mailRoot)
    , m_attachments(context.attachments)
{
    // A window in its own right, not a child dialog: it must appear in the
    // task switcher and be reachable while the main window is used. Passing a
    // parent still makes Qt treat it as a window because of Qt::Window, which
    // QMainWindow carries.
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Compose"));

    // A sensible default. NOT restored and NOT saved; see the header.
    resize(760, 640);

    // BEFORE buildUi(), and this ordering is load-bearing rather than
    // stylistic. buildUi() connects every field to markDirty(), and seeding
    // then fills those fields, so markDirty() runs during construction and
    // calls m_autosaveTimer->start(). Created afterwards, that is a null
    // dereference on the first seeded field, which is every composer.
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setObjectName(QStringLiteral("autosave"));
    m_autosaveTimer->setSingleShot(true);
    m_autosaveTimer->setInterval(m_config.compose().autosaveIntervalMs);
    connect(m_autosaveTimer, &QTimer::timeout, this, &ComposeWindow::autosave);

    m_sender = new MessageSender(this);

    buildUi();
    buildFormatToolbar();
    seedFields();
    seedBody();

    // AFTER buildUi(), which creates m_banner, and BEFORE
    // refreshAttachmentList(), which renders m_attachments: extraction appends
    // to that list, so listing first would show a Forward with no attachments
    // on it, which is precisely the defect this fixes.
    extractForwardedAttachments();

    refreshAttachmentList();

    // Seeding is not an edit. Every field was just filled from the context, so
    // the widgets have emitted their change signals and left the window dirty
    // before the user has typed anything; a composer opened and closed at once
    // would then write a draft nobody asked for. The timer is stopped as well
    // as the flag cleared, since markDirty() started it.
    // After seedFields(): a reply that carries Cc, or a draft that carries
    // either, must show what the message is addressed to.
    revealCcBccIfUsed();

    m_dirty = false;
    m_autosaveTimer->stop();

    // The body, whenever there is already a recipient: a Reply or a Forward
    // has To: filled in from the context, so the first widget in the form
    // would take focus and the user would have to click into the editor
    // before typing. A New message keeps the default, since To: is empty and
    // is genuinely the first thing to fill in.
    if (!m_to->text().trimmed().isEmpty())
        m_body->setFocus();
}


ComposeWindow::~ComposeWindow() = default;

void ComposeWindow::extractForwardedAttachments()
{
    if (m_context.kind != ComposeContext::Kind::Forward
        || m_context.originalPath.isEmpty()) {
        return;
    }

    MimeParser parser;
    const ParsedMessage original = parser.parse(m_context.originalPath);
    if (!original.ok || original.attachments.isEmpty())
        return;

    m_forwardedParts = std::make_unique<QTemporaryDir>();
    if (!m_forwardedParts->isValid()) {
        m_forwardedParts.reset();
        m_banner->setText(
            tr("The forwarded attachments could not be extracted."));
        m_banner->show();
        return;
    }

    // Not auto-removed on destruction by accident: QTemporaryDir does this by
    // default, and it is the whole reason the directory rather than the files
    // is what this window owns.
    m_forwardedParts->setAutoRemove(true);

    QStringList failed;
    for (const Attachment &attachment : original.attachments) {
        QString error;
        // saveWithoutOverwriting, never saveTo. One message really can carry
        // two parts with the same filename, and saveTo overwrites: CLAUDE.md
        // records six of sixteen files lost that way, every write reporting
        // success. Here it would silently forward fewer files than the
        // original had.
        const QString written =
            attachment.saveWithoutOverwriting(m_forwardedParts->path(), &error);
        if (written.isEmpty()) {
            failed.append(attachment.safeFilename());
            continue;
        }
        m_attachments.append(written);
    }

    if (!failed.isEmpty()) {
        // Said out loud rather than swallowed. The composer looks entirely
        // correct with an attachment missing, and the recipient gets a body
        // quoting a document that is not there.
        m_banner->setText(
            tr("%n forwarded attachment(s) could not be extracted: %1", "",
               failed.size())
                .arg(failed.join(QStringLiteral(", "))));
        m_banner->show();
    }
}

Account ComposeWindow::currentAccount() const
{
    // The dropdown is the authority once the window is open: the context
    // chooses the initial account and the user may then change it, and every
    // build after that must use what the From field shows. Reading
    // m_context.accountKey here instead would send from the seeded account
    // however the dropdown was set, with the interface saying otherwise.
    if (m_from && m_from->currentIndex() >= 0) {
        const QString key = m_from->currentData().toString();
        if (!key.isEmpty())
            return m_config.account(key);
    }
    return m_config.account(m_context.accountKey);
}

void ComposeWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("composeCentral"));
    auto *layout = new QVBoxLayout(central);

    // The failed-save banner, above everything: a warning that must survive
    // until it is dealt with does not belong below the fold. Hidden until
    // there is something to say.
    m_banner = new QLabel(central);
    m_banner->setObjectName(QStringLiteral("draftBanner"));
    m_banner->setWordWrap(true);
    // PlainText explicitly. The text carries a filesystem error string and a
    // path, neither of which is ours, and a QLabel guesses under AutoText.
    m_banner->setTextFormat(Qt::PlainText);
    m_banner->hide();
    layout->addWidget(m_banner);

    // The headers take the left, Send the right. Send is the terminal action
    // and carries the weight to match, rather than sitting as one more entry
    // in a row of formatting buttons (item 142).
    auto *headerRow = new QHBoxLayout;
    auto *form = new QFormLayout;

    m_from = new QComboBox(central);
    m_from->setObjectName(QStringLiteral("from"));
    form->addRow(tr("From:"), m_from);

    // To, with the Cc/Bcc disclosure beside it: the two hidden fields are
    // revealed from the row they belong to.
    m_to = new QLineEdit(central);
    m_to->setObjectName(QStringLiteral("to"));
    auto *toRow = new QWidget(central);
    auto *toLayout = new QHBoxLayout(toRow);
    toLayout->setContentsMargins(0, 0, 0, 0);
    toLayout->addWidget(m_to, 1);

    m_ccBccDisclosure = new QToolButton(toRow);
    m_ccBccDisclosure->setObjectName(QStringLiteral("ccBccDisclosure"));
    m_ccBccDisclosure->setText(tr("Cc/Bcc"));
    m_ccBccDisclosure->setToolTip(tr("Show or hide the Cc and Bcc fields"));
    m_ccBccDisclosure->setCheckable(true);
    m_ccBccDisclosure->setArrowType(Qt::DownArrow);
    m_ccBccDisclosure->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toLayout->addWidget(m_ccBccDisclosure);
    form->addRow(tr("To:"), toRow);

    // Cc and Bcc are hidden by default (item 145). The LABEL has to be hidden
    // with the field: a QFormLayout keeps the two as separate items, so
    // hiding only the QLineEdit leaves a stranded "Cc:" over empty space.
    m_cc = new QLineEdit(central);
    m_cc->setObjectName(QStringLiteral("cc"));
    auto *ccLabel = new QLabel(tr("Cc:"), central);
    form->addRow(ccLabel, m_cc);

    m_bcc = new QLineEdit(central);
    m_bcc->setObjectName(QStringLiteral("bcc"));
    auto *bccLabel = new QLabel(tr("Bcc:"), central);
    form->addRow(bccLabel, m_bcc);

    const auto setCcBccVisible = [this, ccLabel, bccLabel](bool visible) {
        ccLabel->setVisible(visible);
        m_cc->setVisible(visible);
        bccLabel->setVisible(visible);
        m_bcc->setVisible(visible);
        m_ccBccDisclosure->setChecked(visible);
        m_ccBccDisclosure->setArrowType(visible ? Qt::UpArrow : Qt::DownArrow);
    };
    m_setCcBccVisible = setCcBccVisible;
    setCcBccVisible(false);
    connect(m_ccBccDisclosure, &QToolButton::toggled, this,
            [setCcBccVisible](bool on) { setCcBccVisible(on); });

    m_subject = new QLineEdit(central);
    m_subject->setObjectName(QStringLiteral("subject"));
    form->addRow(tr("Subject:"), m_subject);

    headerRow->addLayout(form, 1);

    // Icon above text, at the user's choice: a big target, with the word
    // removing any doubt about what it does.
    m_sendButton = new QToolButton(central);
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    m_sendButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    headerRow->addWidget(m_sendButton, 0);

    layout->addLayout(headerRow);

    // The editor bar is created by buildFormatToolbar(), which runs after
    // this, and inserted directly above the body. Recorded here so that
    // insertion has a stable index rather than counting widgets.
    m_editorBarIndex = layout->count();

    m_body = new QPlainTextEdit(central);
    m_body->setObjectName(QStringLiteral("body"));
    layout->addWidget(m_body, 1);

    // The attachment list, with Remove beside it: the control acts on the
    // list, so it lives with it, and both appear only once something is
    // attached. A Remove button that can never do anything is worse than no
    // button, since it invites a click that reports nothing.
    m_attachmentRow = new QWidget(central);
    auto *attachmentLayout = new QHBoxLayout(m_attachmentRow);
    attachmentLayout->setContentsMargins(0, 0, 0, 0);

    m_detachButton = new QToolButton(m_attachmentRow);
    m_detachButton->setObjectName(QStringLiteral("detachButton"));
    m_detachButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    attachmentLayout->addWidget(m_detachButton, 0, Qt::AlignTop);

    m_attachmentList = new QListWidget(m_attachmentRow);
    m_attachmentList->setObjectName(QStringLiteral("attachments"));
    m_attachmentList->setMaximumHeight(90);
    attachmentLayout->addWidget(m_attachmentList, 1);

    m_attachmentRow->hide();
    layout->addWidget(m_attachmentRow);

    // The send-failure pane, in the shape MainWindow's sync log already has:
    // a header with a Close button and a read-only QPlainTextEdit under it. A
    // QPlainTextEdit has no close affordance of its own, so the two travel
    // together as one widget.
    m_sendLogPane = new QWidget(central);
    m_sendLogPane->setObjectName(QStringLiteral("sendLogPane"));
    auto *logLayout = new QVBoxLayout(m_sendLogPane);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(2);

    auto *logHeader = new QHBoxLayout;
    logHeader->addWidget(new QLabel(tr("Send output"), m_sendLogPane));
    logHeader->addStretch();
    auto *closeLog = new QPushButton(tr("Close"), m_sendLogPane);
    closeLog->setObjectName(QStringLiteral("closeSendLog"));
    connect(closeLog, &QPushButton::clicked, m_sendLogPane, &QWidget::hide);
    logHeader->addWidget(closeLog);
    logLayout->addLayout(logHeader);

    m_sendLog = new QPlainTextEdit(m_sendLogPane);
    m_sendLog->setObjectName(QStringLiteral("sendLog"));
    m_sendLog->setReadOnly(true);
    m_sendLog->setMaximumHeight(140);
    logLayout->addWidget(m_sendLog);

    m_sendLogPane->hide();
    layout->addWidget(m_sendLogPane);

    setCentralWidget(central);

    // Every field marks the buffer dirty. The subject and the recipients are
    // part of the message as much as the body is, and a draft that saved the
    // body but not the address it was going to would be worse than none.
    connect(m_body, &QPlainTextEdit::textChanged, this,
            &ComposeWindow::markDirty);
    for (QLineEdit *field : { m_to, m_cc, m_bcc, m_subject })
        connect(field, &QLineEdit::textChanged, this, &ComposeWindow::markDirty);
    connect(m_sendHtml, &QCheckBox::toggled, this, &ComposeWindow::markDirty);
    connect(m_from, &QComboBox::currentIndexChanged, this,
            &ComposeWindow::markDirty);
}

void ComposeWindow::buildFormatToolbar()
{
    // A toolbar WIDGET in the central column, not addToolBar(): the row sits
    // directly above the text it formats, the way every editor puts it, and a
    // window toolbar cannot (item 142). The window has no toolbar at all now.
    m_formatToolbar = new QToolBar(centralWidget());
    m_formatToolbar->setObjectName(QStringLiteral("formatToolbar"));
    m_formatToolbar->setMovable(false);
    // Icon-only for the formatting half, per the user's request and item 143.
    m_formatToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    const int editorIconSize = qMax(16, m_config.toolbarIconSize());
    m_formatToolbar->setIconSize(QSize(editorIconSize, editorIconSize));
    if (auto *column = qobject_cast<QVBoxLayout *>(centralWidget()->layout()))
        column->insertWidget(m_editorBarIndex, m_formatToolbar);

    // A QAction parented to THIS WINDOW, not registered in KeyMap. Its
    // shortcut is therefore scoped to the composer: Qt dispatches a
    // WindowShortcut to the active window only, so the main window's Ctrl+B is
    // untouched and the two namespaces stay apart. These six do not
    // participate in item 132's reachability rule for the same reason.
    // The theme's icon, with the WORDS kept as the tooltip: icon-only is
    // exactly the state where a tooltip stops being decoration, and a theme
    // that lacks one of these names must fall back to text rather than render
    // an empty button (item 143).
    const auto decorate = [](QAction *action, const QString &iconName,
                             const QString &text) {
        action->setToolTip(text);
        const QIcon icon = QIcon::fromTheme(iconName);
        if (icon.isNull())
            return;
        action->setIcon(icon);
        // The text stays on the action for the tooltip and for any menu, but
        // an icon-only toolbar shows the icon alone.
    };

    const auto addFormat = [this, decorate](const QString &name,
                                            const QString &text,
                                            const QString &iconName,
                                            const QString &token,
                                            const QKeySequence &shortcut) {
        QAction *action = m_formatToolbar->addAction(text);
        action->setObjectName(name);
        decorate(action, iconName, text);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this,
                [this, token]() { applyFormat(token); });
    };

    addFormat(QStringLiteral("format_bold"), tr("Bold"),
              QStringLiteral("format-text-bold"),
              QStringLiteral("**"), QKeySequence(QStringLiteral("Ctrl+B")));
    addFormat(QStringLiteral("format_italic"), tr("Italic"),
              QStringLiteral("format-text-italic"),
              QStringLiteral("*"), QKeySequence(QStringLiteral("Ctrl+I")));
    addFormat(QStringLiteral("format_code"), tr("Code"),
              QStringLiteral("format-text-code"),
              QStringLiteral("`"), QKeySequence(QStringLiteral("Ctrl+`")));
    // No shortcut, per the spec's table.
    addFormat(QStringLiteral("format_strike"), tr("Strikethrough"),
              QStringLiteral("format-text-strikethrough"),
              QStringLiteral("~~"), QKeySequence());

    // Link and Quote are not wraps and cannot go through applyFormat().
    QAction *link = m_formatToolbar->addAction(tr("Link"));
    link->setObjectName(QStringLiteral("format_link"));
    decorate(link, QStringLiteral("insert-link"), tr("Link"));
    link->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(link, &QAction::triggered, this, [this]() {
        const QTextCursor cursor = m_body->textCursor();
        applyEdit(MarkdownFormat::link(m_body->toPlainText(),
                                       cursor.selectionStart(),
                                       cursor.selectionEnd()));
    });

    QAction *quote = m_formatToolbar->addAction(tr("Quote"));
    quote->setObjectName(QStringLiteral("format_quote"));
    decorate(quote, QStringLiteral("format-text-blockquote"), tr("Quote"));
    connect(quote, &QAction::triggered, this, [this]() {
        const QTextCursor cursor = m_body->textCursor();
        applyEdit(MarkdownFormat::quote(m_body->toPlainText(),
                                        cursor.selectionStart(),
                                        cursor.selectionEnd()));
    });

    // Everything after this sits at the RIGHT of the row, apart from the
    // formatting buttons, because none of it formats text.
    auto *spacer = new QWidget(m_formatToolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_formatToolbar->addWidget(spacer);

    m_attachAction = m_formatToolbar->addAction(tr("Attach..."));
    m_attachAction->setObjectName(QStringLiteral("compose_attach"));
    decorate(m_attachAction, QStringLiteral("mail-attachment"), tr("Attach..."));
    connect(m_attachAction, &QAction::triggered, this, [this]() {
        const QStringList chosen = QFileDialog::getOpenFileNames(
            this, tr("Attach files"));
        for (const QString &path : chosen)
            attachFile(path);
    });

    m_detachAction = m_formatToolbar->addAction(tr("Remove attachment"));
    m_detachAction->setObjectName(QStringLiteral("compose_detach"));
    connect(m_detachAction, &QAction::triggered, this, [this]() {
        const int row = m_attachmentList->currentRow();
        if (row < 0 || row >= m_attachments.size())
            return;
        m_attachments.removeAt(row);
        refreshAttachmentList();
        markDirty();
    });

    // The HTML toggle rides at the right end of the same row, icon AND text
    // (item 144). Alone on its side, and worded for what it does rather than
    // for the mechanism: "a formatted copy" never said HTML, which is what the
    // user had to infer. It is an action on the bar rather than a checkbox
    // under it, so it reads as a control of the editor.
    m_sendHtml = new QToolButton(m_formatToolbar);
    m_sendHtml->setObjectName(QStringLiteral("sendHtml"));
    m_sendHtml->setCheckable(true);
    m_sendHtml->setText(tr("Send as HTML"));
    m_sendHtml->setToolTip(
        tr("Sends the message as plain text with an HTML version alongside "
           "it. The plain text is what you typed."));
    m_sendHtml->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const QIcon htmlIcon = QIcon::fromTheme(QStringLiteral("text-html"));
    if (!htmlIcon.isNull())
        m_sendHtml->setIcon(htmlIcon);
    m_formatToolbar->addWidget(m_sendHtml);

    // Send is NOT on this row: it is the terminal action, and it lives on the
    // button beside the headers. The QAction survives because it carries the
    // shortcut and is what the button triggers.
    m_sendAction = new QAction(tr("Send"), this);
    m_sendAction->setObjectName(QStringLiteral("compose_send"));
    m_sendAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Return")));
    const QIcon sendIcon = QIcon::fromTheme(QStringLiteral("mail-send"));
    if (!sendIcon.isNull())
        m_sendAction->setIcon(sendIcon);
    connect(m_sendAction, &QAction::triggered, this, &ComposeWindow::send);
    addAction(m_sendAction);
    m_sendButton->setDefaultAction(m_sendAction);

    // Remove attachment moves to its own button beside the list it acts on.
    m_detachButton->setDefaultAction(m_detachAction);
}

void ComposeWindow::seedFields()
{
    // Only accounts that can send. An account without a send_command is
    // receive-only by construction, and offering it in a From field would
    // produce a message that cannot be sent from the account it says it is
    // from.
    const QList<Account> senders = m_config.sendingAccounts();
    for (const Account &account : senders) {
        const QString label = account.name.isEmpty()
                                  ? account.address
                                  : account.name + QStringLiteral(" <")
                                        + account.address + QLatin1Char('>');
        m_from->addItem(label, account.key);
    }
    const int index = m_from->findData(m_context.accountKey);
    if (index >= 0)
        m_from->setCurrentIndex(index);

    m_to->setText(m_context.to.join(QStringLiteral(", ")));
    m_cc->setText(m_context.cc.join(QStringLiteral(", ")));
    m_subject->setText(m_context.subject);

    // New and Forward seed from [compose] send_html; Reply and Reply-all seed
    // from whether the original carried a text/html part, ignoring the config
    // value. An HTML part in the original is a fact about the sender's
    // software, not a guess about their taste.
    const bool isReply = m_context.kind == ComposeContext::Kind::Reply
                         || m_context.kind == ComposeContext::Kind::ReplyAll;
    m_sendHtml->setChecked(isReply ? m_context.seedHtml
                                   : m_config.compose().sendHtml);
}

void ComposeWindow::revealCcBccIfUsed()
{
    // Never hides: only the user's own click on the disclosure does that. A
    // field holding an address must not become invisible because something
    // else changed, which is the whole reason this exists rather than a plain
    // "start collapsed".
    if (!m_setCcBccVisible)
        return;
    if (!m_cc->text().trimmed().isEmpty() || !m_bcc->text().trimmed().isEmpty())
        m_setCcBccVisible(true);
}

void ComposeWindow::seedBody()
{
    if (m_context.quotedBody.isEmpty())
        return;

    // Applied when the window opens and never again. The buffer is text the
    // user owns after that, and there is deliberately no live toggle:
    // tracking "my text" and "the quote" as separate pieces to make a toggle
    // reversible is machinery for a case answered by closing the composer and
    // reopening it.
    // quote_position names where the QUOTE goes, so the reply goes on the
    // other side of it, and the cursor follows the reply rather than the
    // buffer. Start in both cases was wrong for Above: it put the cursor on
    // the attribution line, so the user had to make room before typing.
    if (m_config.compose().quotePosition
        == ComposeSettings::QuotePosition::Above) {
        // The quote first, then blank lines for the reply, and the cursor in
        // them. Two lines rather than one so the reply is separated from the
        // attribution by a blank line once typing starts.
        m_body->setPlainText(m_context.quotedBody + QStringLiteral("\n\n"));
        m_body->moveCursor(QTextCursor::End);
    } else {
        m_body->setPlainText(QStringLiteral("\n\n") + m_context.quotedBody);
        m_body->moveCursor(QTextCursor::Start);
    }

    // The seeded quote is not an edit the user made, so it must not survive as
    // an undo step: one Ctrl+Z on a fresh composer would otherwise wipe the
    // quote and read as the buffer losing its content.
    m_body->document()->clearUndoRedoStacks();
}

void ComposeWindow::refreshAttachmentList()
{
    m_attachmentList->clear();
    for (const QString &path : m_attachments)
        m_attachmentList->addItem(QFileInfo(path).fileName());
    // The ROW, so Remove goes with the list it acts on. The action's own
    // visibility follows, which is what keeps it off screen with nothing
    // attached even though it lives on a button rather than in a toolbar.
    const bool any = !m_attachments.isEmpty();
    m_attachmentRow->setVisible(any);
    if (m_detachAction)
        m_detachAction->setVisible(any);
}

bool ComposeWindow::attachmentNeedsWarning(qint64 size) const
{
    const qint64 limit = m_config.compose().attachmentWarnBytes;
    // A limit of zero or less disables the warning outright. Treating it as a
    // threshold would warn about every attachment including an empty one,
    // which is the opposite of what turning a warning off means.
    return limit > 0 && size > limit;
}

/// A byte count as a figure a person reads, with one decimal below 10 units.
///
/// Integer MB division is what this replaces and it produced "'x' is 0 MB.
/// Many mail servers refuse messages above about 0 MB.", which is what any
/// attachment_warn_bytes under a megabyte reads as. The unit steps down as
/// well, so a small configured limit is stated in KB rather than as zero of a
/// larger unit.
QString ComposeWindow::humanSize(qint64 bytes)
{
    constexpr qint64 kKb = 1024;
    constexpr qint64 kMb = 1024 * 1024;

    if (bytes >= kMb) {
        const double mb = double(bytes) / double(kMb);
        // One decimal only while the figure is small enough for it to say
        // something; 26.2 MB is informative, 1234.6 MB is noise.
        return mb < 10.0 ? QObject::tr("%1 MB").arg(mb, 0, 'f', 1)
                         : QObject::tr("%1 MB").arg(qRound(mb));
    }
    if (bytes >= kKb) {
        const double kb = double(bytes) / double(kKb);
        return kb < 10.0 ? QObject::tr("%1 KB").arg(kb, 0, 'f', 1)
                         : QObject::tr("%1 KB").arg(qRound(kb));
    }
    return QObject::tr("%1 bytes").arg(bytes);
}

void ComposeWindow::attachFile(const QString &path)
{
    const QFileInfo info(path);

    if (attachmentNeedsWarning(info.size())) {
        const qint64 limit = m_config.compose().attachmentWarnBytes;
        const auto answer = QMessageBox::question(
            this, tr("Large attachment"),
            tr("'%1' is %2. Many mail servers refuse messages above about "
               "%3. Attach it anyway?")
                .arg(info.fileName(), humanSize(info.size()),
                     humanSize(limit)),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    m_attachments.append(path);
    refreshAttachmentList();
    markDirty();
}

OutgoingMessage ComposeWindow::currentMessage() const
{
    OutgoingMessage message;
    message.accountKey = currentAccount().key;
    message.to = splitRecipients(m_to->text());
    message.cc = splitRecipients(m_cc->text());
    message.bcc = splitRecipients(m_bcc->text());
    message.subject = m_subject->text();
    message.markdownBody = m_body->toPlainText();
    message.sendHtml = m_sendHtml->isChecked();
    message.attachments = m_attachments;
    message.inReplyTo = m_context.inReplyTo;
    message.references = m_context.references;
    return message;
}

void ComposeWindow::applyEdit(const MarkdownFormat::Edit &edit)
{
    // A QTextCursor replacement rather than setPlainText(), and this is a
    // correction of the plan's draft. Measured under the offscreen platform:
    // setPlainText() DESTROYS the document's undo stack (isUndoAvailable goes
    // from true to false) and resets the cursor to position 0, so every
    // toolbar press would throw away everything the user could undo. A
    // document-wide select and insertText inside one edit block leaves undo
    // available, collapses to a SINGLE undo step, and emits textChanged once.
    QTextCursor cursor = m_body->textCursor();
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(edit.text);
    cursor.endEditBlock();

    // Restore the selection the transformation asked for. The cursor is left
    // at the end of the inserted text, so without this every button press
    // sends it to the bottom of the message; the empty-selection case relies
    // on it to land BETWEEN the tokens, which is the property a user notices
    // immediately when it is wrong.
    //
    // Clamped rather than trusted: QTextCursor::setPosition() past the end
    // warns on stderr and silently clamps, so a stale or arithmetic position
    // would produce noise rather than an error. MarkdownFormat clamps its own
    // output too, so this is a second line rather than the only one.
    const int length = m_body->toPlainText().length();
    const int start = qBound(0, edit.selectionStart, length);
    const int end = qBound(start, edit.selectionEnd, length);

    QTextCursor restored = m_body->textCursor();
    restored.setPosition(start);
    restored.setPosition(end, QTextCursor::KeepAnchor);
    m_body->setTextCursor(restored);
    m_body->setFocus();
}

void ComposeWindow::applyFormat(const QString &token)
{
    const QTextCursor cursor = m_body->textCursor();
    applyEdit(MarkdownFormat::wrap(m_body->toPlainText(),
                                   cursor.selectionStart(),
                                   cursor.selectionEnd(), token));
}

void ComposeWindow::markDirty()
{
    m_dirty = true;
    // Debounced: the timer restarts on every keystroke, so a write happens
    // once the user has paused, not once per character. Every autosave
    // produces a Maildir write that mbsync uploads, which is what the debounce
    // and the dirty check together keep to a few revisions per message.
    m_autosaveTimer->start();
}

void ComposeWindow::autosave()
{
    if (!m_dirty)
        return;
    saveDraftNow();
}

bool ComposeWindow::saveDraftNow()
{
    const Account account = currentAccount();
    if (account.drafts.isEmpty()) {
        // Configured without a drafts folder. Warned about at startup; there
        // is nothing to do here and nothing to report a second time. Reported
        // as success because nothing failed: a false here would make the quit
        // path offer a retry that cannot change anything.
        return true;
    }

    const OutgoingMessage message = currentMessage();

    // The dirty CHECK, not just the flag: an unchanged message means no file
    // is written and no sync is provoked. Every autosave produces a Maildir
    // write that mbsync uploads, so this and the debounce together are what
    // keep a message to a few revisions rather than dozens.
    //
    // Checked BEFORE the build, and on the message rather than on the bytes.
    // The plan's draft compared built.bytes, which can never match: GMime is
    // given a fresh Date and Message-ID on every build, so two builds of an
    // unchanged message differ. That check would have read as working while
    // writing a file on every debounce. Doing it first also skips the
    // blocking build entirely for the no-change case, which is the common one.
    const QString fingerprint = fingerprintOf(message);
    if (!m_savedFingerprint.isEmpty() && fingerprint == m_savedFingerprint) {
        m_dirty = false;
        return true;
    }

    // MessageBuilder::build() is SYNCHRONOUS and can block: a large attachment
    // is read and base64-encoded on this thread, which is the GUI thread. A
    // debounce firing with a 25MB attachment therefore stalls typing for as
    // long as the read takes. Deliberately not moved to a thread: nothing here
    // crosses the worker boundary, and a second threading model for one call
    // is worse than the stall. If someone is measuring a composer freeze, this
    // line is where to look.
    const MessageBuilder::Result built = MessageBuilder::build(message, account);
    if (!built.ok()) {
        m_saveFailed = true;
        m_banner->setText(tr("The draft could not be saved: %1").arg(built.error));
        m_banner->show();
        return false;
    }

    const QString folder = QDir(m_mailRoot).absoluteFilePath(
        account.maildir + QLatin1Char('/') + account.drafts);

    const DraftStore::Result written =
        DraftStore::write(folder, built.bytes, QStringLiteral("D"), m_draftPath);

    if (!written.ok()) {
        // A PERSISTENT banner, not a modal and not a status-bar line that
        // fades. A modal mid-sentence is hostile while the user is typing, but
        // the warning must survive until it is dealt with, because the quit
        // path's honesty depends on it.
        m_saveFailed = true;
        m_banner->setText(
            tr("The draft could not be saved: %1").arg(written.error));
        m_banner->show();
        return false;
    }

    m_draftPath = written.path;
    m_savedFingerprint = fingerprint;
    m_dirty = false;
    m_saveFailed = false;
    m_banner->hide();
    return true;
}

void ComposeWindow::setInputsEnabled(bool enabled)
{
    // Every input for the WHOLE operation, countdown included. The message
    // must not change between the user pressing Send and the bytes being
    // built. The send-failure pane is deliberately left alone: it is read-only
    // and disabling it would make the stderr it carries unreadable.
    m_to->setEnabled(enabled);
    m_cc->setEnabled(enabled);
    m_bcc->setEnabled(enabled);
    m_subject->setEnabled(enabled);
    m_from->setEnabled(enabled);
    m_body->setReadOnly(!enabled);
    m_sendHtml->setEnabled(enabled);
    m_attachmentList->setEnabled(enabled);
    m_formatToolbar->setEnabled(enabled);

    // Every control that used to live in the one toolbar, now that item 142
    // has split it four ways. m_formatToolbar->setEnabled() covered Bold
    // through Send when they shared a row; it now reaches only the editor bar,
    // and the rest have to be named. An Attach left live during the countdown
    // appends to m_attachments after MessageBuilder has already run, so the
    // file is either silently dropped or added to bytes already handed to the
    // send command, with nothing reported either way.
    m_sendButton->setEnabled(enabled);
    m_sendAction->setEnabled(enabled);
    m_detachButton->setEnabled(enabled);
    m_detachAction->setEnabled(enabled);
    m_attachAction->setEnabled(enabled);
    m_ccBccDisclosure->setEnabled(enabled);

    // The ACTIONS, not only the bar that holds them. Disabling a QToolBar
    // greys its buttons but leaves each QAction enabled, so the keyboard
    // shortcut still fires: Ctrl+B during a send would edit a message already
    // being built, through a button that looks unavailable.
    for (QAction *action : m_formatToolbar->actions())
        action->setEnabled(enabled);
}

void ComposeWindow::showSendFailure(const QString &stderrText)
{
    m_sendLog->setPlainText(stderrText.isEmpty()
                                ? tr("The send command reported no output.")
                                : stderrText);
    m_sendLogPane->show();
}

void ComposeWindow::send()
{
    // Refused outright while a send operation is up, countdown included.
    // setInputsEnabled(false) disables the toolbar the Send action lives on
    // and SendDialog is window-modal, so a user cannot reach this twice; the
    // guard covers the programmatic route, where a second call would put a
    // second dialog over the first and start a send MessageSender then
    // refuses, leaving a popup with no result coming for it.
    if (m_sendInFlight)
        return;
    m_sendInFlight = true;

    const Account account = currentAccount();

    if (!account.canSend()) {
        QMessageBox::warning(
            this, tr("Cannot send"),
            tr("The account '%1' has no send command configured.")
                .arg(account.key));
        m_sendInFlight = false;
        return;
    }

    const MessageBuilder::Result built =
        MessageBuilder::build(currentMessage(), account);
    if (!built.ok()) {
        // A missing attachment lands here, before anything runs.
        QMessageBox::warning(this, tr("Cannot send"), built.error);
        m_sendInFlight = false;
        return;
    }

    // Every input is disabled for the WHOLE operation, countdown included.
    setInputsEnabled(false);

    auto *dialog = new SendDialog(m_config.compose().sendDelayMs, this);

    connect(dialog, &SendDialog::undone, this, [this, dialog]() {
        // Nothing reached a server. The composer returns exactly as it was,
        // editable, popup gone, nothing sent.
        //
        // deleteLater(), never delete: this runs SYNCHRONOUSLY inside
        // SendDialog::undo(), which emits undone() and then calls reject() on
        // itself (senddialog.cpp), so the dialog is still on the stack here.
        // This is CLAUDE.md's "a modal dialog must close BEFORE the action it
        // asked for runs" arriving from the other side, and deleteLater is
        // what makes it safe: it posts a deletion event rather than freeing
        // the object the caller is about to keep using. A plain delete here
        // would return into a destroyed SendDialog's reject().
        m_sendInFlight = false;
        setInputsEnabled(true);
        dialog->deleteLater();
    });

    connect(dialog, &SendDialog::committed, this,
            [this, dialog, built, account]() {
        // No setStage(Sending) here: SendDialog::commit() sets it before
        // emitting committed(), so doing it again would be a second owner of
        // the same state.

        // Qt::SingleShotConnection IS REQUIRED HERE. m_sender is a long-lived
        // member, so a bare connect() beside each send() accumulates a
        // permanent receiver per send. Send, fail, correct the recipient, send
        // again, and the second result runs BOTH lambdas: the first still
        // holds the FIRST message's `built` and `account` by value, so it
        // files a sent copy of the wrong message and calls accept() on a
        // dialog it already deleteLater()'d. MessageSender's m_reported guard
        // cannot prevent this: it collapses two QProcess signals into one
        // emit, and this is one emit reaching many receivers. Measured in
        // test_messagesender.cpp::aPerSendConnectionMustBeSingleShot, where
        // the bare shape delivers 3 results for 2 sends and the single-shot
        // shape delivers 2.
        const QMetaObject::Connection resultConnection = connect(
            m_sender, &MessageSender::finished, this,
                [this, dialog, built, account](bool sent, const QString &error) {
            m_sendInFlight = false;

            if (!sent) {
                dialog->accept();
                dialog->deleteLater();
                setInputsEnabled(true);

                // The draft stays, and it must be the draft of what was just
                // attempted. send() builds from the widgets without saving, so
                // the revision on disk is whatever the last debounce wrote:
                // edit, send, fail, close, and the user gets the OLDER text
                // back, having watched their correction be sent. No retry
                // loop, but the text that failed to go is kept.
                saveDraftNow();

                showSendFailure(error);
                return;
            }

            dialog->setStage(SendDialog::Stage::FilingSentCopy);
            bool sentCopyFailed = false;
            QString sentCopyError;

            if (!account.sent.isEmpty()) {
                const QString folder = QDir(m_mailRoot).absoluteFilePath(
                    account.maildir + QLatin1Char('/') + account.sent);
                const DraftStore::Result filed =
                    DraftStore::write(folder, built.bytes, QStringLiteral("S"));
                if (!filed.ok()) {
                    sentCopyFailed = true;
                    sentCopyError = filed.error;
                }
            }

            dialog->setStage(SendDialog::Stage::RemovingDraft);
            if (!m_draftPath.isEmpty()) {
                QFile::remove(m_draftPath);
                m_draftPath.clear();
            }

            dialog->accept();
            dialog->deleteLater();

            if (sentCopyFailed) {
                // A MODAL, never a status-bar line, and never reported as a
                // send failure. The message went; reporting otherwise makes
                // someone send it twice. This is the one failure in the whole
                // design that produces a silent divergence between what the
                // recipient received and what the local archive shows, and
                // nobody discovers a missing sent copy by noticing a line that
                // appeared for a few seconds.
                QMessageBox::warning(
                    this, tr("Sent, but not filed"),
                    tr("The message was sent, but the copy could not be "
                       "written to '%1' for account '%2':\n\n%3\n\n"
                       "The message HAS been sent. Do not send it again.")
                        .arg(account.sent, account.key, sentCopyError));
            }

            // The composer closes either way: the message went, and holding a
            // composer open for a message already sent invites sending it
            // twice. m_finished stops closeEvent() saving a draft for a
            // message that is gone, and stops it refusing the close.
            m_finished = true;
            m_dirty = false;
            close();
        }, Qt::SingleShotConnection);

        if (!m_sender->send(account.sendCommand, built.bytes)) {
            // Refused before any process started, so no finished() will ever
            // arrive and the single-shot connection above would sit there for
            // good. Disconnected here rather than left, since the next send
            // would then have two receivers, which is exactly the defect the
            // flag exists to prevent.
            //
            // THE HANDLE, not disconnect(m_sender, &finished, this, nullptr).
            // That form drops every finished receiver on this object, so one
            // connection added anywhere else would be killed here silently,
            // and the failure it produces is not a wrong value but silence: a
            // send whose result nobody processes, leaving the popup on
            // "Sending...", the composer disabled, and no error anywhere.
            //
            // UNTESTED, and deliberately so rather than by omission. This
            // branch is currently UNREACHABLE: MessageSender::send() returns
            // false only for an empty command or a command already running,
            // and canSend() rejects the first while m_sendInFlight rejects the
            // second before either can arrive here. QSettings also unquotes
            // every INI value, so no configured string survives trimming yet
            // splits to nothing. A test would have to reach past the public
            // surface to provoke it, and a test that cannot fail is worse than
            // none. Kept because it costs nothing and stops being dead the
            // moment send() grows a third refusal, which is the shape an
            // outbox drain loop would add.
            m_sendInFlight = false;
            disconnect(resultConnection);
            dialog->accept();
            dialog->deleteLater();
            setInputsEnabled(true);
            showSendFailure(tr("The send command could not be started."));
        }
    });

    dialog->open();
}

void ComposeWindow::closeEvent(QCloseEvent *event)
{
    // Refused for the WHOLE send, countdown included, and the countdown half
    // is the one easily lost. A guard that starts at commit leaves the five
    // seconds before it unprotected: closing then destroys this window, takes
    // the parented SendDialog down with it, and committed() never fires, so
    // the user pressed Send, watched a countdown, and believes the mail went.
    // After commit the reason is the one MessageSender's destructor
    // documents: a live SMTP conversation abandoned is an outcome nobody can
    // report truthfully.
    //
    // Both windows close themselves when the operation ends, so refusing here
    // strands nothing.
    if (m_sendInFlight && !m_finished) {
        event->ignore();
        return;
    }

    // The last-moment autosave, and the reason it is here rather than in the
    // quit path: the debounce means a composer closed inside its interval has
    // unwritten text, and WA_DeleteOnClose destroys the window immediately
    // after this. Without this call, typing a paragraph and pressing the
    // window manager's X inside thirty seconds loses it silently, with no
    // prompt and no write, which is exactly the loss the autosave design
    // exists to prevent.
    //
    // Its failure is deliberately NOT allowed to refuse the close. A window
    // that will not close because it cannot save is worse than one that closes
    // having said so: the banner is already up from saveDraftNow(), and the
    // quit path reads lastSaveFailed() to escalate. Task 12 owns that dialog;
    // this call is what makes there be something to escalate ABOUT.
    if (m_dirty && !m_finished)
        saveDraftNow();

    emit closed(this);
    QMainWindow::closeEvent(event);
}
