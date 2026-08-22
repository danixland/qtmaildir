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

#include <QMainWindow>
#include <QStringList>

#include <memory>

#include "config.h"
#include "formattoolbar.h"  // MarkdownFormat::Edit is used by value below, and
                            // a type nested in a namespace cannot be
                            // forward-declared from outside it.
#include "types.h"

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTimer;
class QToolBar;
class QTemporaryDir;
class QWidget;

class MessageSender;

/// One draft. A separate top-level window, several open at once.
///
/// A QMainWindow rather than a dialog: a modal dialog cannot consult another
/// message while writing, which is most of what replying is, and taking over
/// the message pane fights the pane that exists to show what is being replied
/// to.
///
/// NO GEOMETRY RESTORE and no geometry save. CLAUDE.md records what
/// saveGeometry does under a tiling compositor: it stores normalGeometry, the
/// compositor owns the tile, and the restore is correct while looking broken.
/// A whole session went into that once. The composer opens at a sensible
/// default size and the compositor places it.
///
/// It contains no MIME and no process logic: a composer bug and a MIME bug are
/// found in different files. Everything it does with a message goes through
/// MessageBuilder, DraftStore, MessageSender, MarkdownFormat and SendDialog.
class ComposeWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// \p mailRoot is the Maildir root, passed in rather than derived.
    ///
    /// There is NO Config::maildirPath(). The root comes from
    /// notmuch_config_get(NOTMUCH_CONFIG_MAIL_ROOT), wrapped by mailRootOf()
    /// which is file-static inside notmuchworker.cpp and needs the database
    /// handle. Item 124 records why this matters: notmuch can split the index
    /// from the mail, and under that layout database.path is the INDEX
    /// directory. Composing a destination from the wrong root would write
    /// drafts and sent copies into the Xapian tree. MainWindow already
    /// receives the root from the worker; it passes it here.
    ComposeWindow(const ComposeContext &context, const Config &config,
                  const QString &mailRoot, QWidget *parent = nullptr);

    /// Defined in the .cpp, not defaulted here. m_forwardedParts is a
    /// unique_ptr to a forward-declared QTemporaryDir, whose deleter needs the
    /// complete type; an implicit destructor would be generated here, where it
    /// is still incomplete.
    ~ComposeWindow() override;

    /// True when the buffer has changed since the last successful autosave.
    /// The quit path asks every open composer this.
    bool hasUnsavedEdits() const { return m_dirty; }

    /// True when the LAST autosave attempt failed. Escalated to its own
    /// dialog on the way out, because saving is what is already not working
    /// and quitting therefore loses that text.
    bool lastSaveFailed() const { return m_saveFailed; }

    /// Writes the current buffer to the drafts folder now. Returns false and
    /// leaves the banner up on failure.
    ///
    /// Returns TRUE when the account configures no drafts folder: nothing was
    /// written and nothing failed, and reporting a failure would make the quit
    /// path offer a retry for a state no retry can change. The composer
    /// running without draft protection is warned about at startup instead.
    bool saveDraftNow();

    /// What the composer would send or save right now.
    ///
    /// Public so a test can assert on the message the widgets produce without
    /// building MIME, and so the quit path can be reasoned about from values.
    OutgoingMessage currentMessage() const;

    /// The paths currently attached, in the order they were attached.
    QStringList attachments() const { return m_attachments; }

    /// Attaches \p path, asking first when it is larger than
    /// [compose] attachment_warn_bytes.
    ///
    /// A warning rather than a refusal: the limit belongs to the recipient's
    /// server, which this application cannot know, so the user decides.
    void attachFile(const QString &path);

    /// A byte count as a figure a person reads.
    ///
    /// Static and public so the formatting is testable without a modal. The
    /// integer MB division this replaces produced "0 MB" for any
    /// attachment_warn_bytes under a megabyte, in both halves of the same
    /// sentence.
    static QString humanSize(qint64 bytes);

    /// Whether \p size would raise the large-attachment question.
    ///
    /// Split out so the threshold is testable without a modal. A limit of zero
    /// or less disables the warning outright rather than warning about
    /// everything.
    bool attachmentNeedsWarning(qint64 size) const;

signals:
    /// The composer finished with its message, one way or another, and the
    /// registry should forget it.
    ///
    /// Emitted from the close path, so a registry connected to it can drop its
    /// pointer before WA_DeleteOnClose destroys the window.
    void closed(ComposeWindow *window);

protected:
    /// The one place the registry is told, whichever route closes the window.
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildFormatToolbar();
    void seedFields();

    /// Extracts a forwarded message's parts into m_forwardedParts and appends
    /// their paths to m_attachments.
    ///
    /// The spec requires Forward to carry attachments, and they have to become
    /// FILES because MessageBuilder reads every attachment by path. Extraction
    /// happens here rather than in MainWindow so the files and the directory
    /// that owns them are created together and die together.
    ///
    /// A part that cannot be written is SKIPPED with a banner rather than
    /// failing the forward: some of the attachments is better than none, and
    /// MessageBuilder refuses a build naming any path that later vanishes, so
    /// a silently wrong send is not among the outcomes.
    void extractForwardedAttachments();
    void seedBody();
    void refreshAttachmentList();
    void setInputsEnabled(bool enabled);
    void showSendFailure(const QString &stderrText);
    void applyEdit(const MarkdownFormat::Edit &edit);
    void markDirty();
    void autosave();
    void send();
    void applyFormat(const QString &token);
    Account currentAccount() const;

    ComposeContext m_context;
    Config m_config;
    QString m_mailRoot;
    QStringList m_attachments;

    /// Holds the parts a Forward extracted, for exactly as long as this window.
    ///
    /// Owned HERE rather than by MainWindow, because the lifetime that makes
    /// sense is the composer's: MessageBuilder reads every attachment by PATH
    /// at build time (messagebuilder.cpp:212), on each autosave and again at
    /// send, so the files must outlive every build this window performs and
    /// nothing after it. QTemporaryDir's destructor removes the tree, so
    /// closing without sending cleans up rather than leaking.
    ///
    /// A draft does not depend on it. Autosave writes a COMPLETE MIME message
    /// with the bytes embedded, so a saved draft stays valid after these files
    /// are gone; and DraftStore is write-only, with no reopen path anywhere in
    /// this codebase, so the "reopened next session pointing at a dead temp
    /// path" hazard cannot arise. Should a reopen path ever be added, it must
    /// read attachments back out of the draft's own MIME rather than trusting
    /// a stored path.
    ///
    /// Null unless a Forward actually extracted something. unique_ptr because
    /// QTemporaryDir is neither copyable nor movable.
    std::unique_ptr<QTemporaryDir> m_forwardedParts;

    QLineEdit *m_to = nullptr;
    QLineEdit *m_cc = nullptr;
    QLineEdit *m_bcc = nullptr;
    QLineEdit *m_subject = nullptr;
    QComboBox *m_from = nullptr;
    QPlainTextEdit *m_body = nullptr;
    QCheckBox *m_sendHtml = nullptr;
    QLabel *m_banner = nullptr;
    QListWidget *m_attachmentList = nullptr;
    QWidget *m_sendLogPane = nullptr;
    QPlainTextEdit *m_sendLog = nullptr;
    QToolBar *m_formatToolbar = nullptr;
    QAction *m_sendAction = nullptr;
    QAction *m_attachAction = nullptr;
    QAction *m_detachAction = nullptr;

    QTimer *m_autosaveTimer = nullptr;
    MessageSender *m_sender = nullptr;

    QString m_draftPath;  ///< The revision on disk, unlinked on the next write.

    /// A fingerprint of the message the last successful save wrote, for the
    /// dirty CHECK.
    ///
    /// NOT the built bytes, and that is a correction of the plan's draft.
    /// MessageBuilder generates a fresh Date and Message-ID on every build
    /// (measured, messagebuilder.cpp around the g_mime_message_set_date call),
    /// so two builds of an unchanged message never compare equal and a check
    /// on the bytes can never fire. It would read as working while writing a
    /// file, and an mbsync upload, on every debounce.
    QString m_savedFingerprint;
    bool m_dirty = false;
    bool m_saveFailed = false;

    /// True from the moment Send is pressed until the operation ends, however
    /// it ends: the countdown, the command, the sent copy.
    ///
    /// ONE flag, covering the whole operation, and an earlier revision had two
    /// because a narrower "committed and running" flag reads as the honest
    /// thing to guard a live SMTP conversation with. It is not: every question
    /// this window has to answer while sending has the same answer through the
    /// countdown as after it. A close during the countdown destroys the
    /// parented SendDialog and committed() never fires, so the user watches a
    /// countdown for a message that is never sent, and a second Send during
    /// the countdown opens a second popup. Splitting the two left the narrower
    /// flag written in three places and read in none.
    bool m_sendInFlight = false;

    /// Set once the message has gone, so the close that follows a successful
    /// send is neither refused nor made to write a draft.
    ///
    /// The close-REFUSAL half is load-bearing: m_sendInFlight is cleared in
    /// the same handler, and without m_finished the composer's own close would
    /// depend on that clear having already happened, which is a race rather
    /// than a guarantee.
    ///
    /// The last-moment-SAVE half is deliberately redundant, and it is worth
    /// saying so rather than letting the next reader mistake it for load
    /// bearing: the send handler already clears m_dirty, so either condition
    /// alone stops the save. Measured, each survives the other's removal and
    /// only dropping both puts the draft of an already-sent message back on
    /// disk. Kept because the two say different things, "nothing to write" and
    /// "this window is done", and a future path that finishes without clearing
    /// m_dirty would otherwise resurrect a sent message's draft silently.
    bool m_finished = false;
};
