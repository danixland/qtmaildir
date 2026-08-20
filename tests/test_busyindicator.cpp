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

#include "busyindicator.h"

class TestBusyIndicator : public QObject
{
    Q_OBJECT

private slots:
    void itStartsIndeterminateAndHidden();
    void itStaysHiddenInsideAShownParent();
    void setProgressShowsARealFraction();
    void aTotalOfZeroFallsBackToBusyRatherThanAnEmptyBar();
    void itSwitchesBackFromDeterminateToIndeterminate();
    void aValueOutsideTheRangeIsClamped();
};

void TestBusyIndicator::itStartsIndeterminateAndHidden()
{
    BusyIndicator indicator;

    // setRange(0, 0) is what Qt reads as indeterminate, and it is the state
    // the status bar's sync indicator lives in.
    QCOMPARE(indicator.minimum(), 0);
    QCOMPARE(indicator.maximum(), 0);
    QVERIFY(!indicator.isDeterminate());
}

void TestBusyIndicator::itStaysHiddenInsideAShownParent()
{
    // The hidden-on-construction property needs a SHOWN PARENT to be
    // observable at all, and both obvious probes measure nothing without one.
    // Measured against a standalone Qt program: a parentless widget reports
    // isVisible() false and isHidden() true whether or not hide() was ever
    // called, so a mutation deleting the constructor's hide() survives both
    // assertions. What differs is WA_WState_ExplicitShowHide, and the
    // behaviour it produces appears only once a parent is shown, which is how
    // MainWindow's status bar holds this widget.
    QWidget parent;
    BusyIndicator *indicator = new BusyIndicator(&parent);
    parent.show();

    QVERIFY2(!indicator->isBusy(),
             "the indicator showed itself as soon as its parent appeared");

    indicator->setBusy(true);
    QVERIFY(indicator->isBusy());
}

void TestBusyIndicator::setProgressShowsARealFraction()
{
    BusyIndicator indicator;
    indicator.setProgress(3, 10);

    QVERIFY2(indicator.isDeterminate(),
             "a bar given a total stayed in the indeterminate range");
    QCOMPARE(indicator.maximum(), 10);
    QCOMPARE(indicator.value(), 3);

    // Showing a fraction means being visible. A caller that set progress and
    // got a hidden widget would have to remember a second call.
    QVERIFY(indicator.isBusy());
}

void TestBusyIndicator::aTotalOfZeroFallsBackToBusyRatherThanAnEmptyBar()
{
    // The trap this guards: setRange(0, 0) IS the indeterminate range, so a
    // total of zero passed straight through would hand the caller an
    // animating bar while it believed it had drawn an empty fraction. Making
    // it explicitly busy is the honest reading of "no total known".
    BusyIndicator indicator;
    indicator.setProgress(0, 0);

    QVERIFY(!indicator.isDeterminate());
    QVERIFY2(indicator.isBusy(),
             "a total of zero left the indicator hidden and silent");

    indicator.setProgress(5, -1);
    QVERIFY(!indicator.isDeterminate());
    QVERIFY(indicator.isBusy());
}

void TestBusyIndicator::itSwitchesBackFromDeterminateToIndeterminate()
{
    // The send popup's whole reason for one widget rather than two: it drains
    // a determinate bar through the undo countdown, then switches THE SAME
    // widget to indeterminate when send_command starts and the duration stops
    // being knowable. A setBusy() that left a stale range would keep drawing
    // the countdown's last fraction.
    BusyIndicator indicator;
    indicator.setProgress(9, 10);
    QVERIFY(indicator.isDeterminate());

    indicator.setBusy(true);
    QVERIFY2(!indicator.isDeterminate(),
             "the bar kept the countdown's range after switching to busy");
    QCOMPARE(indicator.maximum(), 0);
    QVERIFY(indicator.isBusy());

    indicator.setBusy(false);
    QVERIFY(!indicator.isBusy());
}

void TestBusyIndicator::aValueOutsideTheRangeIsClamped()
{
    // A countdown driven by a timer can overshoot its own total by a tick.
    // Qt leaves an out-of-range value unset rather than clamping, which shows
    // the PREVIOUS fraction, so the bar would appear to stall near the end.
    BusyIndicator indicator;
    indicator.setProgress(50, 10);
    QCOMPARE(indicator.value(), 10);

    indicator.setProgress(-5, 10);
    QCOMPARE(indicator.value(), 0);
}

QTEST_MAIN(TestBusyIndicator)
#include "test_busyindicator.moc"
