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

#include <QDialog>
#include <QtGlobal>

class BusyIndicator;
class QLabel;
class QCloseEvent;
class QKeyEvent;
class QPushButton;
class QTimer;

/// Owns a send from the cancellable countdown through to completion.
///
/// The delay is where cancelling is SAFE and it is the only place it is.
/// Nothing has reached a server during the countdown, so Undo means genuinely
/// nothing happened. Killing send_command once it runs leaves an UNKNOWN send:
/// the message may have reached the server in full before the kill, which is
/// worse than either clean outcome. So there is no cancel after commit, and
/// isCommitted() is the line between the two.
///
/// Three rows in every state, so nothing reflows and the window never jumps:
/// a status label, the bar, and Undo.
///
/// The bar CHANGES MODE, it does not change place. Determinate while the
/// countdown drains, because a countdown has measurable progress;
/// indeterminate once the command starts, because a send does not.
///
/// Modal to the composer, NOT to the application: sending from one composer
/// must not freeze a second composer or the main window.
///
/// DISMISSAL IS A THIRD ROUTE TO THE SAME FAILURE, and removing the close
/// button only removes the affordance. Escape, the window manager, close() and
/// QDialog's own machinery all still reach done(); see done() and closeEvent()
/// below, which are the two places that cover them. An earlier revision
/// reasoned about Escape and the titlebar button alone and left close()
/// committing a send with no window on screen.
///
/// Before commit, Undo is the ONLY way out and every other route is refused.
class SendDialog : public QDialog
{
    Q_OBJECT

public:
    /// \p delayMs of zero skips the countdown and sends at once.
    explicit SendDialog(int delayMs, QWidget *parent = nullptr);

    /// The stages, in order. Each sets the label; every stage after the
    /// countdown leaves the bar indeterminate.
    enum class Stage { CountingDown, Sending, FilingSentCopy, RemovingDraft };
    Q_ENUM(Stage)

    void setStage(Stage stage);

    /// True once the countdown has elapsed and the command has started, after
    /// which cancelling is no longer possible.
    bool isCommitted() const { return m_committed; }

signals:
    /// The countdown elapsed or was skipped: the caller should start sending.
    void committed();

    /// Undo was pressed during the countdown. NOTHING has been sent.
    void undone();

protected:
    /// Swallows Escape, with any modifiers. QDialog maps it to reject(), and
    /// during the countdown a bare dismissal is ambiguous in exactly the way
    /// the constructor describes; Undo is the control that says which it means.
    void keyPressEvent(QKeyEvent *event) override;

    /// The single choke point for every dismissal route, which is why the
    /// close button's removal was not enough on its own: QDialog reaches
    /// reject() from the window manager, from close(), and from its own
    /// machinery, and all of them arrive here.
    ///
    /// During the countdown a close is REFUSED. "Close means undo" is
    /// confusing: the user cannot tell whether dismissing the window stopped
    /// the send or merely hid it, and the two answers differ by whether their
    /// mail goes out. Undo is the only way out, which is what the popup's
    /// single control already says. After commit any close is honoured, since
    /// there is nothing left to cancel, and it is forced to Accepted so a
    /// caller reading result() cannot mistake a running send for a cancelled
    /// one. Task 12 closes the dialog after the send finishes, which is
    /// post-commit by definition and so needs no special entry point.
    void done(int result) override;

    /// CLAUDE.md's companion trap: close() on a widget that was never shown
    /// returns early WITHOUT reaching done(), so done()'s refusal alone would
    /// let exactly that one route through. Refuses on the same terms.
    void closeEvent(QCloseEvent *event) override;

private:
    /// The one place that can report "nothing was sent". Returns false, and
    /// does nothing at all, once the send has committed. Both the Undo button
    /// and every dismissal route funnel through it.
    bool undo();

    /// Shows the hint that Undo is the only way out, and holds it long enough
    /// to be read. One function because both refusal sites call it.
    void refuseDismissal();

    void tick();
    void commit();

    QLabel *m_status = nullptr;
    BusyIndicator *m_indicator = nullptr;
    QPushButton *m_undo = nullptr;
    QTimer *m_timer = nullptr;

    int m_remainingMs = 0;
    int m_totalMs = 0;
    bool m_committed = false;

    /// Set by the first undo(), so undone() is emitted exactly once however
    /// many dismissal routes fire. A shown dialog's close() reaches BOTH
    /// closeEvent() and done().
    bool m_undone = false;

    /// Deadline until which the refusal hint holds the status label against
    /// the countdown's own text. Zero when no hint is showing.
    qint64 m_hintUntil = 0;

    /// Set only by undo(), and what lets that one route through done()'s
    /// pre-commit refusal. Every other reject() is turned away.
    bool m_undoing = false;
};
