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

#include <QtTest>

#include <QLabel>
#include <QPushButton>
#include <QSet>

#include "busyindicator.h"
#include "senddialog.h"

class TestSendDialog : public QObject
{
    Q_OBJECT

private slots:
    void theBarIsDeterminateWhileCountingDown();
    void theCountdownCommitsWhenItElapses();
    void aZeroDelayCommitsImmediately();
    void undoDuringTheCountdownEmitsUndoneAndNeverCommits();
    void undoDisablesItselfOnceTheCommandStarts();
    void theBarBecomesIndeterminateWhenSending();
    void undoStaysVisibleAfterItDisables();
    void theStatusLabelIsWideEnoughForEveryStage();
    void closingDuringTheCountdownIsRefused();
    void closingADialogThatWasNeverShownIsAlsoRefused();
    void theRefusalHintSurvivesTheNextCountdownTick();
    void rejectDuringTheCountdownIsRefused();
    void escapeDuringTheCountdownIsRefused();
    void undoIsTheOneRouteThatClosesBeforeCommit();
    void closingAfterCommitReportsAcceptedAndDoesNotUndo();
    void undoAfterCommitIsRefused();
    void everyStageSetsItsOwnLabelAndLeavesTheBarBusy();
    void windingBackToCountingDownAfterCommitIsRefused();
};

void TestSendDialog::theBarIsDeterminateWhileCountingDown()
{
    // A countdown has measurable progress, so the bar drains rather than
    // animating. This is the half of BusyIndicator MainWindow never uses: the
    // status bar's sync indicator is indeterminate for its whole life.
    //
    // A generous delay so the assertion cannot race the countdown's own end,
    // which would flip the bar to indeterminate for a legitimate reason and
    // report a defect that is not there.
    SendDialog dialog(5000);
    dialog.show();

    auto *indicator = dialog.findChild<BusyIndicator *>(
        QStringLiteral("sendProgress"));
    QVERIFY2(indicator, "the dialog has no BusyIndicator named sendProgress");
    QVERIFY2(indicator->isDeterminate(),
             "the bar was animating during a countdown that has a known end");
}

void TestSendDialog::theCountdownCommitsWhenItElapses()
{
    // A short delay rather than waiting out the shipped default: what is being
    // tested is that the countdown ends in a commit, not how long it is.
    SendDialog dialog(150);
    QSignalSpy spy(&dialog, &SendDialog::committed);
    dialog.show();

    QVERIFY2(spy.wait(3000), "the countdown never committed");
    QCOMPARE(spy.count(), 1);
    QVERIFY(dialog.isCommitted());
}

void TestSendDialog::aZeroDelayCommitsImmediately()
{
    // send_delay_ms = 0 sends at once, for anyone who finds the delay
    // irritating. It must still be a queued commit rather than one inside the
    // constructor, or a caller connecting to committed() after constructing the
    // dialog would never hear it.
    SendDialog dialog(0);
    QSignalSpy spy(&dialog, &SendDialog::committed);
    dialog.show();

    QVERIFY2(spy.wait(1000), "a zero delay never committed");
    QCOMPARE(spy.count(), 1);
    QVERIFY(dialog.isCommitted());
}

void TestSendDialog::undoDuringTheCountdownEmitsUndoneAndNeverCommits()
{
    // THE test for this feature, and the property that matters is the NEGATIVE
    // one. A test asserting only that undone() fired would pass against a
    // design that started the send anyway and threw the result away, which is
    // the whole failure the delay exists to prevent. Nothing has reached a
    // server during the countdown, so Undo must mean that nothing happened.
    SendDialog dialog(2000);
    QSignalSpy committedSpy(&dialog, &SendDialog::committed);
    QSignalSpy undoneSpy(&dialog, &SendDialog::undone);
    dialog.show();

    auto *undo = dialog.findChild<QPushButton *>(QStringLiteral("undoSend"));
    QVERIFY2(undo, "the dialog has no button named undoSend");
    QVERIFY2(undo->isEnabled(), "Undo was dead during the countdown");

    undo->click();

    QCOMPARE(undoneSpy.count(), 1);
    QCOMPARE(committedSpy.count(), 0);

    // Past the original deadline. A timer left running would commit here, after
    // the dialog has already reported that nothing was sent.
    QTest::qWait(2500);
    QVERIFY2(committedSpy.count() == 0,
             "the countdown committed after Undo was pressed");
}

void TestSendDialog::undoDisablesItselfOnceTheCommandStarts()
{
    // There is no cancel after commit. Killing send_command once it runs leaves
    // an UNKNOWN send: the message may have reached the server in full before
    // the kill, which is worse than either clean outcome.
    SendDialog dialog(100);
    dialog.show();

    auto *undo = dialog.findChild<QPushButton *>(QStringLiteral("undoSend"));
    QVERIFY(undo);

    QSignalSpy spy(&dialog, &SendDialog::committed);
    QVERIFY2(spy.wait(3000), "the countdown never committed");

    QVERIFY2(!undo->isEnabled(),
             "Undo was still live after the send command started");
}

void TestSendDialog::theBarBecomesIndeterminateWhenSending()
{
    // The bar CHANGES MODE, it does not change place: a send has no measurable
    // progress, so the same widget stops drawing a fraction and starts
    // animating, and nothing in the popup reflows.
    SendDialog dialog(100);
    dialog.show();

    auto *indicator = dialog.findChild<BusyIndicator *>(
        QStringLiteral("sendProgress"));
    QVERIFY(indicator);
    QVERIFY(indicator->isDeterminate());

    dialog.setStage(SendDialog::Stage::Sending);
    QVERIFY2(!indicator->isDeterminate(),
             "the bar kept the countdown's fraction while sending");
}

void TestSendDialog::undoStaysVisibleAfterItDisables()
{
    // A control that vanishes re-lays out the popup mid-operation, and a greyed
    // Undo says WHY cancelling is no longer possible where an absent one only
    // looks like it was never offered.
    SendDialog dialog(100);
    dialog.show();

    auto *undo = dialog.findChild<QPushButton *>(QStringLiteral("undoSend"));
    QVERIFY(undo);

    QSignalSpy spy(&dialog, &SendDialog::committed);
    QVERIFY2(spy.wait(3000), "the countdown never committed");

    QVERIFY2(undo->isVisibleTo(&dialog),
             "Undo disappeared instead of greying out");
}

void TestSendDialog::theStatusLabelIsWideEnoughForEveryStage()
{
    // The label is sized to the LONGEST string it can hold in the current
    // language, not to its content, so the popup does not resize between
    // stages. Asserted against the metrics of the strings themselves rather
    // than a constant, so it holds in whatever language is loaded.
    SendDialog dialog(2000);
    dialog.show();

    auto *status = dialog.findChild<QLabel *>(QStringLiteral("sendStatus"));
    QVERIFY2(status, "the dialog has no label named sendStatus");

    const QFontMetrics metrics(status->font());
    const QStringList candidates{
        SendDialog::tr("Sending in %1...").arg(99),
        SendDialog::tr("Sending..."),
        SendDialog::tr("Filing sent copy..."),
        SendDialog::tr("Removing draft..."),
        SendDialog::tr("Press Undo to stop sending."),
    };
    int widest = 0;
    for (const QString &candidate : candidates)
        widest = qMax(widest, metrics.horizontalAdvance(candidate));

    QVERIFY2(status->minimumWidth() >= widest,
             "the status label was sized to its content, so the popup will "
             "resize when a longer stage name arrives");
}

void TestSendDialog::closingDuringTheCountdownIsRefused()
{
    // The same failure as the Undo test, reached by a different door. Removing
    // the close BUTTON removes the visual affordance, not the code path: the
    // window manager, close() and QDialog's own machinery all still reach
    // done(). Left unguarded, close() hides the window and leaves the timer
    // running, so the send starts with no window on screen and the only cancel
    // control destroyed.
    //
    // The close is REFUSED rather than reinterpreted as an Undo, at the user's
    // call: "close means undo is confusing", because a dismissed window cannot
    // tell you whether it stopped the send or merely hid it. So the dialog
    // stays up, the send stays scheduled, and Undo remains the only way out.
    SendDialog dialog(2000);
    QSignalSpy committedSpy(&dialog, &SendDialog::committed);
    QSignalSpy undoneSpy(&dialog, &SendDialog::undone);
    dialog.show();

    QVERIFY2(!dialog.close(), "close() during the countdown was accepted");

    QVERIFY2(dialog.isVisible(),
             "the dialog vanished on a close it was supposed to refuse");
    QVERIFY2(undoneSpy.count() == 0,
             "a refused close silently undid the send anyway");

    // Refusing must not be silent: a window that ignores a close reads as a
    // hang, so the popup has to say where the exit is.
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("sendStatus"));
    QVERIFY(status);
    QVERIFY2(status->text().contains(QStringLiteral("Undo")),
             "a refused close gave the user no hint that Undo is the way out");

    // The send was never cancelled, so it still goes out. That is the whole
    // point of refusing rather than undoing.
    QVERIFY2(committedSpy.wait(3000),
             "the refused close cancelled the send after all");
}

void TestSendDialog::closingADialogThatWasNeverShownIsAlsoRefused()
{
    // CLAUDE.md's documented companion trap: close() on a widget that was
    // never shown returns early WITHOUT reaching done(), so a refusal written
    // only in done() would miss this one route entirely. The countdown is
    // running either way, because it starts in the constructor rather than on
    // show(). Refused on the same terms as the shown case.
    SendDialog dialog(2000);
    QSignalSpy committedSpy(&dialog, &SendDialog::committed);
    QSignalSpy undoneSpy(&dialog, &SendDialog::undone);

    QVERIFY2(!dialog.close(),
             "close() on an unshown dialog slipped past the refusal");
    QVERIFY2(undoneSpy.count() == 0,
             "closing an unshown dialog undid the send");

    QVERIFY2(committedSpy.wait(3000),
             "the unshown dialog's send was cancelled by a refused close");
}

void TestSendDialog::theRefusalHintSurvivesTheNextCountdownTick()
{
    // Without a hold the hint lives for one tick, which is 100ms, and the
    // countdown text overwrites it before it can be read. A refusal the user
    // cannot see is a window that ignores them, which reads as a hang, so the
    // hold is what makes the refusal honest rather than decorative.
    SendDialog dialog(5000);
    dialog.show();

    auto *status = dialog.findChild<QLabel *>(QStringLiteral("sendStatus"));
    QVERIFY(status);

    dialog.close();
    const QString hint = status->text();
    QVERIFY2(hint.contains(QStringLiteral("Undo")), "no hint on refusal");

    // Several ticks later, well past the point the countdown would have
    // reclaimed the label.
    QTest::qWait(500);
    QCOMPARE(status->text(), hint);

    // And it does eventually give the label back, or the countdown would be
    // hidden for the rest of its life.
    QTest::qWait(1500);
    QVERIFY2(status->text() != hint,
             "the hint never released the label back to the countdown");
}

void TestSendDialog::rejectDuringTheCountdownIsRefused()
{
    // reject() is the route neither close() nor Escape goes through directly,
    // and it is the one a caller reaches for. CLAUDE.md's rule is that every
    // route out gets asserted: "a test used close() and the user used Cancel"
    // is the documented way one of three gets missed.
    SendDialog dialog(2000);
    QSignalSpy committedSpy(&dialog, &SendDialog::committed);
    QSignalSpy undoneSpy(&dialog, &SendDialog::undone);
    dialog.show();

    dialog.reject();

    QVERIFY2(dialog.isVisible(), "reject() dismissed the countdown");
    QVERIFY2(undoneSpy.count() == 0, "reject() undid the send");
    QVERIFY2(committedSpy.wait(3000), "reject() cancelled the send after all");
}

void TestSendDialog::escapeDuringTheCountdownIsRefused()
{
    // Escape is QDialog's built-in reject(), and swallowing it in
    // keyPressEvent is only the first line: done() refuses it too, so the
    // dialog is safe even if the key handler is ever removed.
    SendDialog dialog(2000);
    QSignalSpy committedSpy(&dialog, &SendDialog::committed);
    QSignalSpy undoneSpy(&dialog, &SendDialog::undone);
    dialog.show();

    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY2(dialog.isVisible(), "Escape dismissed the countdown");

    // With modifiers too, so neither is an undocumented back door.
    QTest::keyClick(&dialog, Qt::Key_Escape, Qt::ShiftModifier);
    QTest::keyClick(&dialog, Qt::Key_Escape, Qt::ControlModifier);
    QVERIFY2(dialog.isVisible(), "a modified Escape dismissed the countdown");

    QVERIFY2(undoneSpy.count() == 0, "Escape undid the send");
    QVERIFY2(committedSpy.wait(3000), "Escape cancelled the send after all");
}

void TestSendDialog::undoIsTheOneRouteThatClosesBeforeCommit()
{
    // The counterpart to the four refusals above: having refused every other
    // way out, the one remaining control must actually work, or the popup is
    // a trap with no exit at all.
    SendDialog dialog(2000);
    QSignalSpy undoneSpy(&dialog, &SendDialog::undone);
    dialog.show();

    auto *undo = dialog.findChild<QPushButton *>(QStringLiteral("undoSend"));
    QVERIFY(undo);
    undo->click();

    QCOMPARE(undoneSpy.count(), 1);
    QVERIFY2(!dialog.isVisible(), "Undo did not close the dialog");
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
}

void TestSendDialog::closingAfterCommitReportsAcceptedAndDoesNotUndo()
{
    // After commit there is nothing to undo, so closing is permitted. What it
    // must NOT do is report Rejected: a caller inspecting result() would read
    // a send that is running as one that was cancelled, and undone() must stay
    // silent because the message is on its way.
    SendDialog dialog(100);
    QSignalSpy undoneSpy(&dialog, &SendDialog::undone);
    QSignalSpy committedSpy(&dialog, &SendDialog::committed);
    dialog.show();

    QVERIFY2(committedSpy.wait(3000), "the countdown never committed");
    QVERIFY(dialog.isCommitted());

    dialog.close();

    QCOMPARE(undoneSpy.count(), 0);
    QVERIFY2(dialog.result() != QDialog::Rejected,
             "closing a committed dialog reported the send as cancelled");
}

void TestSendDialog::undoAfterCommitIsRefused()
{
    // Undo is disabled at commit, but a disabled button is a UI property, not
    // an invariant. This asserts the handler's own guard, so a future change
    // that re-enables the button cannot turn it back into a claim that nothing
    // was sent while send_command is already running.
    SendDialog dialog(100);
    QSignalSpy committedSpy(&dialog, &SendDialog::committed);
    QSignalSpy undoneSpy(&dialog, &SendDialog::undone);
    dialog.show();

    QVERIFY2(committedSpy.wait(3000), "the countdown never committed");

    auto *undo = dialog.findChild<QPushButton *>(QStringLiteral("undoSend"));
    QVERIFY(undo);

    // Deliberately re-enabled, to reach the handler that the disabled state
    // would otherwise hide. This is the mutation a future edit could make by
    // accident; the guard behind it is what this test is for.
    undo->setEnabled(true);
    undo->click();

    QVERIFY2(undoneSpy.count() == 0,
             "Undo claimed nothing was sent after the send command started");
}

void TestSendDialog::everyStageSetsItsOwnLabelAndLeavesTheBarBusy()
{
    // Walks all four, because a break accidentally deleted from one case would
    // fall through to the next and nothing else would notice. FilingSentCopy
    // and RemovingDraft are also the two whose Italian strings drove the whole
    // label-width design, so leaving them unexercised would test the sizing of
    // strings nothing ever displays.
    SendDialog dialog(2000);
    dialog.show();

    auto *status = dialog.findChild<QLabel *>(QStringLiteral("sendStatus"));
    auto *indicator = dialog.findChild<BusyIndicator *>(
        QStringLiteral("sendProgress"));
    QVERIFY(status);
    QVERIFY(indicator);

    const QString countingDown = status->text();
    QVERIFY2(!countingDown.isEmpty(), "the countdown showed no text");
    QVERIFY(indicator->isDeterminate());

    QStringList seen;
    const QVector<SendDialog::Stage> stages{
        SendDialog::Stage::Sending,
        SendDialog::Stage::FilingSentCopy,
        SendDialog::Stage::RemovingDraft,
    };
    for (SendDialog::Stage stage : stages) {
        dialog.setStage(stage);
        QVERIFY2(!status->text().isEmpty(), "a stage set no text at all");
        QVERIFY2(!indicator->isDeterminate(),
                 "a post-countdown stage left the bar drawing a fraction");
        seen << status->text();
    }

    // Distinct from each other and from the countdown: a fallthrough would
    // show the following stage's text and collapse two of these into one.
    seen << countingDown;
    QCOMPARE(QSet<QString>(seen.begin(), seen.end()).size(), seen.size());
}

void TestSendDialog::windingBackToCountingDownAfterCommitIsRefused()
{
    // setStage() is public and Task 12 passes values from the public enum. The
    // enum is documented "in order", so the class enforces that itself rather
    // than trusting its caller: winding back would relabel a running send
    // "Sending in 0..." and redraw a full countdown bar under it, offering a
    // cancel that no longer exists.
    SendDialog dialog(100);
    QSignalSpy committedSpy(&dialog, &SendDialog::committed);
    dialog.show();

    QVERIFY2(committedSpy.wait(3000), "the countdown never committed");

    auto *status = dialog.findChild<QLabel *>(QStringLiteral("sendStatus"));
    auto *indicator = dialog.findChild<BusyIndicator *>(
        QStringLiteral("sendProgress"));
    const QString sending = status->text();

    dialog.setStage(SendDialog::Stage::CountingDown);

    QCOMPARE(status->text(), sending);
    QVERIFY2(!indicator->isDeterminate(),
             "the bar drew a countdown fraction over a running send");
}

QTEST_MAIN(TestSendDialog)
#include "test_senddialog.moc"
