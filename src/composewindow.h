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

#include <QDateTime>
#include <QMainWindow>

#include <functional>
#include <QStringList>

#include <memory>

#include "config.h"
#include "formattoolbar.h"  // MarkdownFormat::Edit is used by value below, and
                            // a type nested in a namespace cannot be
                            // forward-declared from outside it.
#include "types.h"

class QAction;
class QCheckBox;
class QSplitter;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTimer;
class QToolBar;
class QAbstractButton;
class QToolButton;
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

    /// Clear the unsaved-edits state without writing a draft. The send path
    /// needs this: the message is gone, so there is nothing left to save and
    /// nothing to warn about on the way out.
    void markClean();

    /// Render the age line as though the last save were \p seconds ago.
    /// Exists so a test can drive the clock instead of waiting on it.
    void reportDraftAgeFor(qint64 seconds);

    /// Where the signature files live. Defaults to
    /// <config>/qtmaildir/signatures; a test points it at its own directory.
    ///
    /// A setter rather than a config key: nothing yet suggests the user wants
    /// a second location, and the tests need to not read the real one.
    void setSignatureDir(const QString &dir);

    /// Seeds the signature from config and fills the switch.
    ///
    /// Public and called by the constructor rather than private, so a test can
    /// drive it after pointing setSignatureDir() somewhere safe. A resumed
    /// draft seeds nothing: its body already carries the signature it was
    /// written with.
    void seedSignature();

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

    /// Shows Cc and Bcc when either already carries a value, and leaves them
    /// shown. Called after seeding and after a draft is loaded.
    ///
    /// The load-bearing half of item 145: a hidden field holding an address is
    /// a message going somewhere the sender cannot see, which is worse than
    /// the clutter the disclosure removes. Never hides: only the user's own
    /// click does that.
    void revealCcBccIfUsed();

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

    /// A draft was written to disk, so the window's owner can index it and it
    /// appears in the Drafts view without a full sync (item 158).
    ///
    /// \p path is the file just written, absolute. \p previousPath is the file
    /// the write replaced, empty on the first save of a new draft.
    void draftSaved(const QString &path, const QString &previousPath);

    /// A draft file was unlinked (sent), so its index entry must go too.
    /// \p path is the file that was removed, absolute.
    void draftRemoved(const QString &path);

    /// A send succeeded, and the message it answers should record that.
    ///
    /// Item 68. \p sourceMessageId is the Message-ID of the message replied to
    /// or forwarded, \p tag is "replied" or "passed". The window emits rather
    /// than writing, because a tag write belongs to the one applyTags path in
    /// MainWindow and the composer owns no worker.
    ///
    /// Emitted only after the send itself succeeded: a failed send leaves the
    /// source untouched, since the flag asserts that the mail went.
    void sourceMessageAnswered(const QString &sourceMessageId,
                               const QString &tag);

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

    /// Reads the forwarded original's HTML, before the body is seeded.
    void readForwardedHtml();

    /// Creates the strip-remote-content checkbox, for a forward that needs it.
    void buildStripRemoteControl();

    /// Creates the read-only preview of what an HTML forward will carry.
    void buildForwardPreview();
    void seedBody();

    /// Applies \p name to the buffer, replacing whatever is there.
    void applySignature(const QString &name);

    /// The text of every signature on disk, for replace()'s guard.
    QStringList knownSignatures() const;

    /// The signature name this account seeds, falling through to [compose].
    QString seededSignatureName() const;

    void refreshAttachmentList();
    void setInputsEnabled(bool enabled);
    void showSendFailure(const QString &stderrText);
    void applyEdit(const MarkdownFormat::Edit &edit);
    void markDirty();

    /// Builds the menu bar (item 161). Called after buildFormatToolbar(),
    /// since the menus SHOW its actions rather than owning copies.
    void buildMenuBar();

    /// Builds the status bar carrying the unsaved cue and the age line.
    void buildDraftStatusBar();

    /// The one writer of m_dirty. Refreshes the status cue and the title
    /// marker so neither can drift from the flag.
    void setDirty(bool dirty);

    /// Repaint the age line from m_lastSavedAt. Called by the tick and after
    /// a save.
    void refreshDraftStatus();
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
    QWidget *m_attachmentRow = nullptr;
    QToolButton *m_detachButton = nullptr;
    int m_editorBarIndex = -1;
    std::function<void(bool)> m_setCcBccVisible;
    QWidget *m_ccRow = nullptr;
    QWidget *m_bccRow = nullptr;
    QToolButton *m_ccBccDisclosure = nullptr;
    QToolButton *m_sendButton = nullptr;
    QLineEdit *m_cc = nullptr;
    QLineEdit *m_bcc = nullptr;
    QLineEdit *m_subject = nullptr;
    QComboBox *m_from = nullptr;
    QPlainTextEdit *m_body = nullptr;
    QToolButton *m_sendHtml = nullptr;

    /// Item 171. Strips remote content from the forwarded original, checked by
    /// default. Only created for a Forward whose original carries remote
    /// content, so an ordinary message gains no control.
    QCheckBox *m_stripRemote = nullptr;

    /// Read-only view of the original an HTML forward will carry. Item 171:
    /// the buffer holds the user's note only, so this is what makes the rest
    /// of the message visible without pretending it can be edited.
    QWidget *m_forwardPreview = nullptr;

    /// Holds the editor, and the forward preview beside it when there is one.
    QSplitter *m_split = nullptr;

    /// Shows and hides the forwarded-message pane. Only for a forward.
    QAction *m_showForwardAction = nullptr;

    /// The forwarded original's HTML, as read from `originalPath` at
    /// construction. Held raw; the stripping happens at currentMessage(),
    /// so toggling the control does not need a re-parse.
    QString m_forwardedHtmlRaw;
    QToolButton *m_signatureSwitch = nullptr;
    QString m_signatureDir;
    QString m_signatureName;  ///< The selected signature, empty for None.

    /// True once the user has used the switch. From then on a From: change
    /// stops re-seeding, so a deliberate choice is never overwritten. Matches
    /// how send_html seeds from context and is then left alone.
    bool m_signatureChosen = false;
    QLabel *m_banner = nullptr;

    /// The status bar's two halves. The cue answers "is there anything
    /// unwritten"; the age answers "when did the last write happen". Both
    /// are refreshed from setDirty()/refreshDraftStatus() and never assigned
    /// directly, so they cannot disagree with m_dirty.
    QLabel *m_unsavedCue = nullptr;
    QLabel *m_draftAge = nullptr;
    QTimer *m_draftAgeTick = nullptr;

    /// When the last successful save happened, invalid until one has. Drives
    /// the age line, which changes with no edit to prompt it.
    QDateTime m_lastSavedAt;
    QListWidget *m_attachmentList = nullptr;
    QWidget *m_sendLogPane = nullptr;
    QPlainTextEdit *m_sendLog = nullptr;
    QToolBar *m_formatToolbar = nullptr;
    QAction *m_sendAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_closeAction = nullptr;

    /// The menu twin of the m_sendHtml tool button, which is a QToolButton
    /// and cannot be put in a menu. Kept in step both ways.
    QAction *m_sendHtmlAction = nullptr;
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

    /// Never assigned directly outside setDirty(). Seven sites used to write
    /// it and four of them clear it, only two of which are a save, so a cue
    /// hung off the save path alone missed the constructor and the send. The
    /// setter is what keeps the flag and both displays in step.
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
