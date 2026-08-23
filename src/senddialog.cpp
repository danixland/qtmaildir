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

#include "senddialog.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "busyindicator.h"

namespace {

// How often the countdown repaints: smooth enough for a draining bar without
// being a busy loop. It is also the resolution of the countdown itself, since
// tick() subtracts exactly this much rather than consulting a clock. Two
// consequences, both deliberate: a delay that is not a multiple of 100 rounds
// UP to one (250 runs for 300ms), and timer slack accumulates rather than
// being corrected against a clock. Drift is irrelevant at this scale, where
// the number is a courtesy pause and nothing downstream measures it.
constexpr int kTickMs = 100;

// How long the refused-dismissal hint holds the status label. Longer than a
// tick, or the countdown would overwrite it before it could be read and the
// refusal would be silent in practice; short enough that the countdown the
// user is waiting on is not hidden for any meaningful part of its life.
constexpr qint64 kHintMs = 1500;

} // namespace

SendDialog::SendDialog(int delayMs, QWidget *parent)
    : QDialog(parent)
    , m_remainingMs(qMax(0, delayMs))
    , m_totalMs(qMax(0, delayMs))
{
    setWindowTitle(tr("Sending"));

    // Modal to the composer, not to the application. Sending from one composer
    // must not freeze a second composer or the main window.
    setWindowModality(Qt::WindowModal);

    // No close button: during the countdown a bare dismissal is ambiguous,
    // since it could equally mean "cancel" or "send now", so Undo is the only
    // control that states which. This removes the AFFORDANCE only. Escape,
    // close() and the window manager all still reach done(), and that override
    // is what actually makes a dismissal safe; keyPressEvent() below merely
    // spares the user an Escape that would silently undo. Reasoning about this
    // flag alone is what left close() committing a send with no window up.
    setWindowFlags((windowFlags() | Qt::CustomizeWindowHint)
                   & ~Qt::WindowCloseButtonHint);

    auto *layout = new QVBoxLayout(this);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("sendStatus"));

    // Sized to the LONGEST string it can hold in the current language, not to
    // its content. Italian "Rimozione della bozza..." is longer than "Removing
    // draft...", so a label sized to whatever it happens to be showing resizes
    // the popup between stages. Computed from tr() results at construction, so
    // it is correct in whatever language is loaded AT THAT MOMENT. That is
    // sufficient here and not in general: main() installs the QTranslator on
    // its own stack before any window exists, so no dialog can outlive a
    // language change. A runtime language switch would need this recomputed.
    const QFontMetrics metrics(m_status->font());
    // The refusal hint is in this list too. It replaces the countdown text in
    // the same label, so leaving it out would resize the popup at exactly the
    // moment the user is being told the window will not close, which is the
    // worst possible time for it to jump.
    const QStringList candidates{
        tr("Sending in %1...").arg(99),
        tr("Sending..."),
        tr("Filing sent copy..."),
        tr("Removing draft..."),
        tr("Press Undo to stop sending."),
    };
    int widest = 0;
    for (const QString &candidate : candidates)
        widest = qMax(widest, metrics.horizontalAdvance(candidate));
    m_status->setMinimumWidth(widest);
    layout->addWidget(m_status);

    m_indicator = new BusyIndicator(this);
    m_indicator->setObjectName(QStringLiteral("sendProgress"));
    layout->addWidget(m_indicator);

    // Three rows in every state, so nothing reflows: Undo keeps its place and
    // its size after it disables rather than vanishing.
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    m_undo = new QPushButton(tr("Undo"), this);
    m_undo->setObjectName(QStringLiteral("undoSend"));
    buttons->addWidget(m_undo);
    layout->addLayout(buttons);

    // Built BEFORE the Undo connection below, which stops it. The lambda would
    // read a null m_timer otherwise, and only because nothing can click a
    // button mid-constructor does the reverse order happen to survive.
    m_timer = new QTimer(this);
    m_timer->setObjectName(QStringLiteral("sendCountdown"));
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &SendDialog::tick);

    // Both the button and done() funnel into one place, so the two dismissal
    // routes cannot drift into disagreeing about what a cancel does.
    connect(m_undo, &QPushButton::clicked, this, [this] { undo(); });

    if (m_totalMs == 0) {
        // Queued rather than immediate, so a caller that connects to
        // committed() AFTER constructing the dialog still hears it. Emitting
        // from the constructor would send to nobody.
        QTimer::singleShot(0, this, &SendDialog::commit);
    } else {
        setStage(Stage::CountingDown);
        m_timer->start();
    }
}

bool SendDialog::undo()
{
    // Undo is disabled at commit, but a disabled button is a UI property and
    // not an invariant. This is the ONE place that can report "nothing was
    // sent", so it refuses outright once the command is running rather than
    // trusting the button's state.
    //
    // m_undone is the second half and is NOT redundant: it makes undone()
    // fire exactly once however many times this is reached.
    if (m_committed || m_undone)
        return false;
    m_undone = true;

    // The timer stops FIRST. A timer left running commits after the dialog has
    // already reported that nothing was sent, which is the one outcome the
    // whole delay exists to make impossible.
    m_timer->stop();
    m_undo->setEnabled(false);
    emit undone();

    // Undo is the ONE route out before commit, so it is the one caller allowed
    // through done()'s refusal. The flag is what distinguishes it from every
    // other reject(); it is never cleared, because the dialog is finished.
    m_undoing = true;
    reject();
    return true;
}

void SendDialog::refuseDismissal()
{
    // A window that ignores a close reads as a hang, so the refusal says where
    // the exit is rather than doing nothing at all. One function because both
    // done() and closeEvent() refuse, and two copies of this meant neutering
    // either one left the other still setting the text, hiding the regression.
    //
    // Held for kHintMs, because the countdown's next tick is only kTickMs away
    // and would otherwise overwrite the hint before it could be read, leaving
    // the refusal effectively silent after all. setStage() honours the hold
    // rather than this scheduling a restore, so the countdown keeps running
    // underneath and there is no second timer to get out of step.
    m_hintUntil = QDateTime::currentMSecsSinceEpoch() + kHintMs;
    m_status->setText(tr("Press Undo to stop sending."));
    m_undo->setFocus();
}

void SendDialog::keyPressEvent(QKeyEvent *event)
{
    // QDialog maps Escape to reject(). Swallowed WITH ANY MODIFIER: Shift and
    // Ctrl variants are the same keystroke as far as intent goes, and letting
    // one through would be an undocumented back door to the same dismissal.
    // done() would treat it safely as an Undo either way; this just spares the
    // user a cancel they did not ask for by reflex.
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SendDialog::done(int result)
{
    // Every dismissal route arrives here, which is the point: close(), the
    // window manager, Escape and QDialog's own reject() all converge on
    // done(), and guarding any one of them individually leaves the others
    // open. Which routes are permitted, and when:
    //
    // BEFORE COMMIT, nothing closes the dialog except Undo. A close is
    // REFUSED, not silently reinterpreted as a cancel: "close means undo" is
    // confusing, because the user cannot tell whether dismissing the window
    // stopped the send or merely hid it, and the two answers differ by whether
    // their mail goes out. The popup carries exactly one control and it says
    // what it does. Undo reaches QDialog::done() through m_undoing below.
    //
    // AFTER COMMIT, the send is in flight and there is nothing left to cancel,
    // so any close is honoured. It is forced to Accepted so a caller reading
    // result() cannot mistake a running send for a cancelled one.
    //
    // TASK 12 closes this dialog when the send finishes, and it does so after
    // commit by definition, so the ordinary accept()/close() works and needs
    // no special entry point. A stray reject() cannot reach the pre-commit
    // state at all, which is the property this refusal buys.
    if (m_committed) {
        QDialog::done(QDialog::Accepted);
        return;
    }

    if (m_undoing) {
        QDialog::done(QDialog::Rejected);
        return;
    }

    refuseDismissal();
}

void SendDialog::closeEvent(QCloseEvent *event)
{
    // Measured against a standalone Qt program, not assumed: close() on a
    // dialog that was NEVER SHOWN reaches closeEvent() but returns BEFORE
    // done(), so done()'s refusal alone would let that one route through. A
    // shown dialog reaches both, and ignoring the event here stops it before
    // done() is consulted.
    if (!m_committed && !m_undoing) {
        event->ignore();
        refuseDismissal();
        return;
    }
    QDialog::closeEvent(event);
}

void SendDialog::tick()
{
    m_remainingMs -= kTickMs;
    if (m_remainingMs <= 0) {
        commit();
        return;
    }
    setStage(Stage::CountingDown);
}

void SendDialog::commit()
{
    // Idempotent: a stray tick racing the singleShot must not emit twice.
    if (m_committed)
        return;

    m_timer->stop();
    m_committed = true;

    // Disabled, never hidden. A greyed Undo says why cancelling is no longer
    // possible; an absent one only looks like it was never offered.
    m_undo->setEnabled(false);

    setStage(Stage::Sending);
    emit committed();
}

void SendDialog::setStage(Stage stage)
{
    // The enum is documented "in order", so the class enforces that rather
    // than trusting its caller: Task 12 passes values from this public enum,
    // and winding back would relabel a running send "Sending in 0..." and
    // redraw a full countdown bar under it, offering a cancel that no longer
    // exists. Only the backwards step is refused; the forward stages are the
    // caller's to drive.
    if (m_committed && stage == Stage::CountingDown)
        return;

    // The refusal hint outranks the countdown text for as long as it is held.
    // Only the countdown is suppressed: a stage change is a real event and
    // must always be shown, and commit() clears the hold anyway.
    if (stage == Stage::CountingDown
        && QDateTime::currentMSecsSinceEpoch() < m_hintUntil) {
        m_indicator->setProgress(m_remainingMs, m_totalMs);
        return;
    }

    switch (stage) {
    case Stage::CountingDown:
        // Rounded up, so a countdown with 1ms left still reads "1" rather than
        // sitting on "0" for a tick.
        m_status->setText(tr("Sending in %1...")
                              .arg((m_remainingMs + 999) / 1000));
        m_indicator->setProgress(m_remainingMs, m_totalMs);
        return;
    case Stage::Sending:
        m_status->setText(tr("Sending..."));
        break;
    case Stage::FilingSentCopy:
        m_status->setText(tr("Filing sent copy..."));
        break;
    case Stage::RemovingDraft:
        m_status->setText(tr("Removing draft..."));
        break;
    }

    // Everything past the countdown: the duration stops being knowable, so the
    // same widget switches from a fraction to an animation.
    m_indicator->setBusy(true);
}
