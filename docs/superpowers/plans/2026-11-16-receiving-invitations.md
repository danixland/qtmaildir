# Receiving invitations: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A message carrying a `text/calendar` invitation shows a native bar in the message pane; Accept / Tentative / Decline mail an iMIP reply and, once it is sent, write the event into the account's calendar; an organiser's update or cancellation is applied by a button.

**Architecture:** Pure functions in `CalendarStore` parse an `Invitation`, build the stored file and the reply, and choose the bar's state, all tested without a widget. `MimeParser` exposes the calendar part; `MessageBuilder` attaches one; `ComposeWindow` carries it. A small `InvitationBar` widget renders a state and emits actions; `MainWindow` decides and writes through `CalendarWriter`, only after `ComposeWindow::sourceMessageAnswered` confirms the send. Spec: `docs/superpowers/specs/2026-11-16-receiving-invitations-design.md`.

**Tech Stack:** Qt 6 / C++, libical 3, GMime 3, QtTest.

---

## Ground rules for every task

- Work on branch `invitations` in a worktree, so `master` stays shippable while the user runs it daily (AGENTS.md: branch substantial UI work):
  ```bash
  cd ~/Programming/GIT/qtmaildir
  git worktree add ../qtmaildir-invitations -b invitations
  cd ../qtmaildir-invitations
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
  ```
- **Never run a test binary without `QT_QPA_PLATFORM=offscreen`. Never launch `./build/src/qtmaildir`.**
- **No commits until the user's verdict** (standing rule). Tasks end at "green".
- **gmime headers before any Qt header** in a translation unit that includes both.
- Every user-facing string in `tr()`; libical stays inside `calendarstore.cpp` (no `icalcomponent*` crosses out).
- Known pre-existing failures on master, ignore: in `test_mainwindow`, `everyBuiltinFilterButtonCarriesAnIconAndItsText`, `everyActionCarriesAnIcon`, `noTwoActionsShareAnIcon` (item 215) and `undoMovesTheMessageBack` (item 136).
- No em dashes in comments.

---

### Task 1: `Invitation` and `CalendarStore::parseInvitation`

**Files:**
- Modify: `src/caltypes.h` (after `struct CalEvent`), `src/calendarstore.h`, `src/calendarstore.cpp`
- Test: `tests/test_calendarstore.cpp`

- [ ] **Step 1: Tests.** Add fixtures at file scope (inside the anonymous namespace, after `kRichEvent`) and four slots:

```cpp
/// An invitation as an organiser's client sends it. `me@example.org` is the
/// user; `other@example.net` is a second attendee.
QByteArray invite(const QString &method, int sequence,
                  const QString &extraLines = QString())
{
    return ics(QStringLiteral("METHOD:%1\r\n").arg(method)
               + QString::fromLatin1(kRomeVtimezone)
               + vevent(QStringLiteral(
                     "UID:meeting-1@example.com\r\n"
                     "SEQUENCE:%1\r\n"
                     "DTSTAMP:20261110T090000Z\r\n"
                     "DTSTART;TZID=Europe/Rome:20261120T100000\r\n"
                     "DTEND;TZID=Europe/Rome:20261120T110000\r\n"
                     "SUMMARY:Planning\r\n"
                     "LOCATION:Room 2\r\n"
                     "ORGANIZER;CN=Boss:mailto:boss@example.com\r\n"
                     "ATTENDEE;CN=Me;PARTSTAT=NEEDS-ACTION;RSVP=TRUE:mailto:me@example.org\r\n"
                     "ATTENDEE;CN=Other;PARTSTAT=ACCEPTED:mailto:other@example.net\r\n"
                     "%2").arg(sequence).arg(extraLines)));
}
```

```cpp
void TestCalendarStore::anInvitationParsesWithItsMethod()
{
    const Invitation inv = CalendarStore::parseInvitation(invite("REQUEST", 2));
    QVERIFY(inv.isValid());
    QCOMPARE(inv.method, QStringLiteral("REQUEST"));
    QCOMPARE(inv.uid, QStringLiteral("meeting-1@example.com"));
    QCOMPARE(inv.sequence, 2);
    QCOMPARE(inv.summary, QStringLiteral("Planning"));
    QCOMPARE(inv.location, QStringLiteral("Room 2"));
    QCOMPARE(inv.organizer.address, QStringLiteral("boss@example.com"));
    QCOMPARE(inv.attendees.size(), 2);
    QCOMPARE(inv.start.toTimeZone(QTimeZone("Europe/Rome")).time(), QTime(10, 0));
    QVERIFY(!inv.recurrenceId.isValid());
}

void TestCalendarStore::aCalendarWithoutAMethodIsNotAnInvitation()
{
    // A stored event, or a PUBLISHed calendar file attached for reading.
    QVERIFY(!CalendarStore::parseInvitation(ics(vevent(
        QStringLiteral("UID:x\r\nDTSTART:20261120T100000Z\r\n")))).isValid());
    QVERIFY(!CalendarStore::parseInvitation("not a calendar").isValid());
}

void TestCalendarStore::aSingleOccurrenceInvitationCarriesItsRecurrenceId()
{
    const Invitation inv = CalendarStore::parseInvitation(invite(
        "REQUEST", 3,
        QStringLiteral("RECURRENCE-ID;TZID=Europe/Rome:20261120T100000\r\n")));
    QVERIFY(inv.isValid());
    QVERIFY(inv.recurrenceId.isValid());
}

void TestCalendarStore::theUserIsFoundAmongTheAttendeesWhateverTheCase()
{
    const Invitation inv = CalendarStore::parseInvitation(invite("REQUEST", 0));
    QCOMPARE(CalendarStore::attendeeIndex(inv.attendees,
                                          QStringLiteral("ME@Example.org")), 0);
    QCOMPARE(CalendarStore::attendeeIndex(inv.attendees,
                                          QStringLiteral("nobody@example.org")), -1);
}
```

- [ ] **Step 2: Build, expect a compile failure** (`Invitation` unknown).

- [ ] **Step 3: Implement.** `src/caltypes.h`, after `CalEvent`:

```cpp
/// An iTIP message carried in mail (RFC 5546): a request, a cancellation, a
/// reply. Every text field came from a stranger and is shown as plain text.
struct Invitation
{
    QString method;          ///< Upper case, "REQUEST", "CANCEL", ...
    QString uid;
    int sequence = 0;
    QDateTime recurrenceId;  ///< Valid when the message is about ONE occurrence.
    QString summary;
    QString location;
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    CalPerson organizer;
    QList<CalPerson> attendees;
    QByteArray rawText;      ///< The part as received; what Accept writes from.

    bool isValid() const { return !method.isEmpty() && !uid.isEmpty(); }
};
```

`src/calendarstore.h`, inside the namespace:

```cpp
/// The invitation in \p text, or an invalid one when the text does not parse,
/// carries no METHOD, or holds no VEVENT.
Invitation parseInvitation(const QByteArray &text);

/// The index of \p address among \p people, compared case-insensitively, or -1.
int attendeeIndex(const QList<CalPerson> &people, const QString &address);
```

`src/calendarstore.cpp`, inside `namespace CalendarStore`:

```cpp
Invitation parseInvitation(const QByteArray &text)
{
    Invitation inv;
    IcalComponent root = parseRoot(text);
    if (!root)
        return inv;
    const icalproperty_method method = icalcomponent_get_method(root.get());
    icalcomponent *master = masterOf(root.get());
    if (method == ICAL_METHOD_NONE || !master)
        return inv;

    // The event fields through parseEvent, so a time is read exactly as a
    // stored event's is: TZID honoured, a missing VTIMEZONE resolved by name.
    bool unknownZone = false;
    const CalEvent event = parseEvent(text, QString(), QString(), &unknownZone);
    inv.method = str(icalproperty_method_to_string(method)).toUpper();
    inv.uid = event.uid;
    inv.sequence = event.sequence;
    inv.summary = event.summary;
    inv.location = event.location;
    inv.start = event.start;
    inv.end = event.end;
    inv.allDay = event.allDay;
    inv.organizer = event.organizer;
    inv.attendees = event.attendees;
    inv.rawText = text;

    // masterOf() falls back to the first VEVENT when every one carries a
    // RECURRENCE-ID, which is exactly the single-occurrence message.
    if (icalproperty *rid = icalcomponent_get_first_property(master, ICAL_RECURRENCEID_PROPERTY))
        inv.recurrenceId = toDateTime(icalproperty_get_recurrenceid(rid), tzidOf(rid), &unknownZone);
    return inv;
}

int attendeeIndex(const QList<CalPerson> &people, const QString &address)
{
    for (int i = 0; i < people.size(); ++i)
        if (people.at(i).address.compare(address, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}
```

If `parseEvent` reads `uid` etc. from a different master than `masterOf` here, both use `masterOf`, so they agree.

- [ ] **Step 4: Build and run, expect pass.** `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_calendarstore`

---

### Task 2: `storedText`, `replyText`, `fileNameForUid`

**Files:** `src/calendarstore.h`, `src/calendarstore.cpp`, `tests/test_calendarstore.cpp`

- [ ] **Step 1: Tests.**

```cpp
void TestCalendarStore::theStoredFileDropsTheMethodAndSetsOnlyMyAnswer()
{
    const QByteArray stored = CalendarStore::storedText(
        invite("REQUEST", 2), QStringLiteral("me@example.org"),
        QStringLiteral("ACCEPTED"));
    QVERIFY(!stored.contains("METHOD:"));
    // The organiser's TZID survives: converting would move the event for
    // every other client (the 206 rule).
    QVERIFY(stored.contains("DTSTART;TZID=Europe/Rome:20261120T100000"));

    bool unknown = false;
    const CalEvent back = CalendarStore::parseEvent(stored, QStringLiteral("/x/e.ics"),
                                                    QStringLiteral("x"), &unknown);
    QCOMPARE(back.uid, QStringLiteral("meeting-1@example.com"));
    QCOMPARE(back.attendees.at(0).partstat, QStringLiteral("ACCEPTED"));
    QCOMPARE(back.attendees.at(1).partstat, QStringLiteral("ACCEPTED"));
    QVERIFY(!stored.contains("RSVP=TRUE:mailto:me@example.org"));
}

void TestCalendarStore::theReplyCarriesOnlyMyAttendeeLine()
{
    const QByteArray reply = CalendarStore::replyText(
        invite("REQUEST", 2), QStringLiteral("me@example.org"),
        QStringLiteral("DECLINED"));
    const Invitation inv = CalendarStore::parseInvitation(reply);
    QCOMPARE(inv.method, QStringLiteral("REPLY"));
    QCOMPARE(inv.uid, QStringLiteral("meeting-1@example.com"));
    QCOMPARE(inv.sequence, 2);
    QCOMPARE(inv.organizer.address, QStringLiteral("boss@example.com"));
    QCOMPARE(inv.attendees.size(), 1);
    QCOMPARE(inv.attendees.at(0).address, QStringLiteral("me@example.org"));
    QCOMPARE(inv.attendees.at(0).partstat, QStringLiteral("DECLINED"));
}

void TestCalendarStore::aUidBecomesASafeFileName()
{
    QCOMPARE(CalendarStore::fileNameForUid(QStringLiteral("meeting-1@example.com")),
             QStringLiteral("meeting-1@example.com.ics"));
    // A stranger's UID reaches a path: nothing may climb or nest.
    const QString climbing = CalendarStore::fileNameForUid(QStringLiteral("../../etc/x"));
    QVERIFY(!climbing.contains(QLatin1Char('/')));
    QVERIFY(!climbing.startsWith(QLatin1Char('.')));
    QVERIFY(climbing.endsWith(QStringLiteral(".ics")));
    QVERIFY(CalendarStore::fileNameForUid(QString()).endsWith(QStringLiteral(".ics")));
    QVERIFY(CalendarStore::fileNameForUid(QString()).size() > 4);
}
```

- [ ] **Step 2: Build, expect a compile failure.**

- [ ] **Step 3: Implement.** Header:

```cpp
/// The file to store when the user answers \p inviteText with \p partstat:
/// METHOD removed (a stored object carries none), the user's ATTENDEE given
/// \p partstat and stripped of RSVP, everything else as the organiser sent it.
QByteArray storedText(const QByteArray &inviteText, const QString &myAddress,
                      const QString &partstat);

/// The METHOD:REPLY calendar answering \p inviteText (RFC 5546 3.2.3): the
/// event's UID, SEQUENCE, ORGANIZER, times and RECURRENCE-ID, and ONLY the
/// user's ATTENDEE line. The invite's VTIMEZONEs travel with it.
QByteArray replyText(const QByteArray &inviteText, const QString &myAddress,
                     const QString &partstat);

/// A file name for an event whose UID came from a stranger: every character
/// outside [A-Za-z0-9@._-] becomes '_', a leading '.' is replaced, and an
/// empty result gets a fresh UUID. Always ends in ".ics".
QString fileNameForUid(const QString &uid);
```

Implementation (anonymous-namespace helper first, then the three functions):

```cpp
namespace {
/// Sets PARTSTAT on every ATTENDEE of \p c whose address is \p me, and drops
/// its RSVP. Returns whether one was found.
bool answerAs(icalcomponent *c, const QString &me, const QString &partstat)
{
    bool found = false;
    for (icalproperty *a = icalcomponent_get_first_property(c, ICAL_ATTENDEE_PROPERTY);
         a; a = icalcomponent_get_next_property(c, ICAL_ATTENDEE_PROPERTY)) {
        if (personOf(a).address.compare(me, Qt::CaseInsensitive) != 0)
            continue;
        icalproperty_set_parameter_from_string(a, "PARTSTAT", partstat.toUtf8().constData());
        icalproperty_remove_parameter_by_name(a, "RSVP");
        found = true;
    }
    return found;
}
} // namespace
```

```cpp
QByteArray storedText(const QByteArray &inviteText, const QString &myAddress,
                      const QString &partstat)
{
    IcalComponent root = parseRoot(inviteText);
    if (!root)
        return QByteArray();
    removeAll(root.get(), ICAL_METHOD_PROPERTY);
    for (icalcomponent *c = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT);
         c; c = icalcomponent_get_next_component(root.get(), ICAL_VEVENT_COMPONENT))
        answerAs(c, myAddress, partstat);
    return serialise(root.get());
}

QByteArray replyText(const QByteArray &inviteText, const QString &myAddress,
                     const QString &partstat)
{
    IcalComponent invite = parseRoot(inviteText);
    icalcomponent *master = invite ? masterOf(invite.get()) : nullptr;
    if (!master)
        return QByteArray();

    IcalComponent root(icalcomponent_new(ICAL_VCALENDAR_COMPONENT));
    icalcomponent_add_property(root.get(), icalproperty_new_version("2.0"));
    icalcomponent_add_property(root.get(), icalproperty_new_prodid("-//qtmaildir//EN"));
    icalcomponent_add_property(root.get(), icalproperty_new_method(ICAL_METHOD_REPLY));
    for (icalcomponent *tz = icalcomponent_get_first_component(invite.get(), ICAL_VTIMEZONE_COMPONENT);
         tz; tz = icalcomponent_get_next_component(invite.get(), ICAL_VTIMEZONE_COMPONENT))
        icalcomponent_add_component(root.get(), icalcomponent_new_clone(tz));

    icalcomponent *event = icalcomponent_new(ICAL_VEVENT_COMPONENT);
    icalcomponent_add_component(root.get(), event);
    for (icalproperty_kind kind : { ICAL_UID_PROPERTY, ICAL_SEQUENCE_PROPERTY,
                                    ICAL_DTSTART_PROPERTY, ICAL_DTEND_PROPERTY,
                                    ICAL_SUMMARY_PROPERTY, ICAL_ORGANIZER_PROPERTY,
                                    ICAL_RECURRENCEID_PROPERTY }) {
        if (icalproperty *p = icalcomponent_get_first_property(master, kind))
            icalcomponent_add_property(event, icalproperty_new_clone(p));
    }
    for (icalproperty *a = icalcomponent_get_first_property(master, ICAL_ATTENDEE_PROPERTY);
         a; a = icalcomponent_get_next_property(master, ICAL_ATTENDEE_PROPERTY)) {
        if (personOf(a).address.compare(myAddress, Qt::CaseInsensitive) == 0) {
            icalcomponent_add_property(event, icalproperty_new_clone(a));
            break;
        }
    }
    answerAs(event, myAddress, partstat);
    const icaltimetype now = icaltime_current_time_with_zone(icaltimezone_get_utc_timezone());
    icalcomponent_add_property(event, icalproperty_new_dtstamp(now));
    return serialise(root.get());
}

QString fileNameForUid(const QString &uid)
{
    QString name;
    for (const QChar ch : uid) {
        const bool keep = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
                          || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
                          || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                          || ch == QLatin1Char('@') || ch == QLatin1Char('.')
                          || ch == QLatin1Char('_') || ch == QLatin1Char('-');
        name.append(keep ? ch : QLatin1Char('_'));
    }
    if (name.startsWith(QLatin1Char('.')))
        name[0] = QLatin1Char('_');
    if (name.isEmpty())
        name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return name + QStringLiteral(".ics");
}
```

`removeAll` is the existing helper and works on any component. If `removeAll`'s signature takes only components that are events, it does not matter: it walks properties of the component it is given.

- [ ] **Step 4: Build and run, expect pass.**

---

### Task 3: `invitationState`

**Files:** `src/caltypes.h`, `src/calendarstore.h`, `src/calendarstore.cpp`, `tests/test_calendarstore.cpp`

- [ ] **Step 1: Tests.**

```cpp
void TestCalendarStore::theBarStateFollowsTheTable()
{
    using S = InvitationState;
    const auto inv = [](const char *method, int seq, const QString &extra = QString()) {
        return CalendarStore::parseInvitation(invite(QString::fromLatin1(method), seq, extra));
    };
    CalEvent stored;
    stored.uid = QStringLiteral("meeting-1@example.com");
    stored.sequence = 2;

    QCOMPARE(CalendarStore::invitationState(Invitation(), nullptr, true, true), S::None);
    QCOMPARE(CalendarStore::invitationState(inv("REPLY", 2), nullptr, true, true), S::None);
    QCOMPARE(CalendarStore::invitationState(inv("REQUEST", 2), nullptr, true, false), S::NoCalendar);
    QCOMPARE(CalendarStore::invitationState(inv("REQUEST", 2), nullptr, false, true), S::NotInvited);
    QCOMPARE(CalendarStore::invitationState(inv("REQUEST", 2), nullptr, true, true), S::NewRequest);
    QCOMPARE(CalendarStore::invitationState(inv("REQUEST", 2), &stored, true, true), S::Answered);
    QCOMPARE(CalendarStore::invitationState(inv("REQUEST", 3), &stored, true, true), S::Update);
    QCOMPARE(CalendarStore::invitationState(inv("REQUEST", 1), &stored, true, true), S::OutOfDate);
    QCOMPARE(CalendarStore::invitationState(inv("CANCEL", 3), &stored, true, true), S::Cancelled);
    QCOMPARE(CalendarStore::invitationState(inv("CANCEL", 1), &stored, true, true), S::OutOfDate);
    QCOMPARE(CalendarStore::invitationState(inv("CANCEL", 3), nullptr, true, true), S::CancelledUnknown);
    // A cancel needs no attendee match: being uninvited from a meeting you
    // are in is still a cancel.
    QCOMPARE(CalendarStore::invitationState(inv("CANCEL", 3), &stored, false, true), S::Cancelled);
    QCOMPARE(CalendarStore::invitationState(
                 inv("REQUEST", 3, QStringLiteral("RECURRENCE-ID:20261120T090000Z\r\n")),
                 &stored, true, true),
             S::SingleOccurrence);
}
```

- [ ] **Step 2: Build, expect a compile failure.**

- [ ] **Step 3: Implement.** `src/caltypes.h`, after `Invitation`:

```cpp
/// What the message pane's invitation bar shows. Chosen by
/// CalendarStore::invitationState(); the widget only renders it.
enum class InvitationState {
    None,              ///< No bar: not an invitation, or a method not handled here.
    NoCalendar,        ///< calendars_dir is unset: details, no buttons.
    NotInvited,        ///< The account's address is not an attendee.
    NewRequest,
    Answered,          ///< Stored, same SEQUENCE: the answer can still change.
    Update,            ///< Stored, the invitation is newer.
    OutOfDate,         ///< Stored, the calendar holds a newer version.
    Cancelled,
    CancelledUnknown,  ///< A cancel for an event not stored. Normal, not an error.
    SingleOccurrence,  ///< Carries a RECURRENCE-ID; not supported yet.
};
```

Header declaration and implementation:

```cpp
/// \p stored is the event with the invitation's UID, or null. \p invited:
/// the receiving account's address is among the attendees.
InvitationState invitationState(const Invitation &invitation, const CalEvent *stored,
                                bool invited, bool calendarConfigured);
```

```cpp
InvitationState invitationState(const Invitation &invitation, const CalEvent *stored,
                                bool invited, bool calendarConfigured)
{
    using S = InvitationState;
    const bool request = invitation.method == QLatin1String("REQUEST");
    const bool cancel = invitation.method == QLatin1String("CANCEL");
    if (!invitation.isValid() || (!request && !cancel))
        return S::None;
    if (invitation.recurrenceId.isValid())
        return S::SingleOccurrence;
    if (!calendarConfigured)
        return S::NoCalendar;
    if (stored && invitation.sequence < stored->sequence)
        return S::OutOfDate;
    if (cancel)
        return stored ? S::Cancelled : S::CancelledUnknown;
    if (!invited)
        return S::NotInvited;
    if (!stored)
        return S::NewRequest;
    return invitation.sequence > stored->sequence ? S::Update : S::Answered;
}
```

- [ ] **Step 4: Build and run, expect pass.**

---

### Task 4: `MimeParser` keeps the calendar part

**Files:** `src/mimeparser.h` (`ParsedMessage`), `src/mimeparser.cpp` (`collectParts`), `tests/test_mimeparser.cpp`

- [ ] **Step 1: Tests.** Two messages built inline (see how the file builds others; if it reads files from `FIXTURE_DIR`, add two fixture files with the same content instead):

```cpp
void TestMimeParser::anInlineCalendarPartIsKept()
{
    const QByteArray raw =
        "From: boss@example.com\r\nTo: me@example.org\r\nSubject: Invitation\r\n"
        "Message-ID: <inv1@example.com>\r\nDate: Mon, 16 Nov 2026 10:00:00 +0000\r\n"
        "MIME-Version: 1.0\r\nContent-Type: multipart/alternative; boundary=b\r\n\r\n"
        "--b\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nYou are invited.\r\n"
        "--b\r\nContent-Type: text/calendar; charset=utf-8; method=REQUEST\r\n\r\n"
        "BEGIN:VCALENDAR\r\nMETHOD:REQUEST\r\nEND:VCALENDAR\r\n"
        "--b--\r\n";
    const ParsedMessage message = parseBytes(raw);
    QVERIFY(message.calendarPart.contains("METHOD:REQUEST"));
    QCOMPARE(message.plainBody.trimmed(), QStringLiteral("You are invited."));
}

void TestMimeParser::anAttachedCalendarPartIsKeptAndStaysAnAttachment()
{
    const QByteArray raw =
        "From: boss@example.com\r\nTo: me@example.org\r\nSubject: Invitation\r\n"
        "Message-ID: <inv2@example.com>\r\nDate: Mon, 16 Nov 2026 10:00:00 +0000\r\n"
        "MIME-Version: 1.0\r\nContent-Type: multipart/mixed; boundary=b\r\n\r\n"
        "--b\r\nContent-Type: text/plain; charset=utf-8\r\n\r\nSee attached.\r\n"
        "--b\r\nContent-Type: application/ics; name=invite.ics\r\n"
        "Content-Disposition: attachment; filename=invite.ics\r\n\r\n"
        "BEGIN:VCALENDAR\r\nMETHOD:REQUEST\r\nEND:VCALENDAR\r\n"
        "--b--\r\n";
    const ParsedMessage message = parseBytes(raw);
    QVERIFY(message.calendarPart.contains("METHOD:REQUEST"));
    QCOMPARE(message.attachments.size(), 1);
}
```

`parseBytes` stands for however this test file turns bytes into a `ParsedMessage` (write to a `QTemporaryDir` file and call `MimeParser().parse(path)` if no helper exists; add a small file-scope helper doing exactly that).

- [ ] **Step 2: Build, expect a compile failure** (`calendarPart`).

- [ ] **Step 3: Implement.** `ParsedMessage`, beside `attachments`:

```cpp
    /// The first text/calendar (or application/ics) part, decoded, inline or
    /// attached. What the invitation bar reads (item 207). Untrusted.
    QByteArray calendarPart;
```

In `collectParts`, right after `isAttachment` is computed and BEFORE the `if (isAttachment)` branch:

```cpp
    // Before the attachment branch, so an invitation sent as an attached .ics
    // is recognised as well as one inline in an alternative. It stays an
    // attachment chip too, as it always was.
    if (out.calendarPart.isEmpty()
        && (mimeType == QLatin1String("text/calendar")
            || mimeType == QLatin1String("application/ics"))) {
        out.calendarPart = decodePart(part);
    }
```

- [ ] **Step 4: Build and run, expect pass.** `QT_QPA_PLATFORM=offscreen ./build/tests/test_mimeparser`

---

### Task 5: per-account `calendar` key

**Files:** `src/config.h` (`Account`), `src/config.cpp` (account load), `tests/test_config.cpp`

- [ ] **Step 1: Test.** Modelled on an existing account-key test in `test_config.cpp` (find the one asserting `spam`, around line 952, and copy its setup):

```cpp
void TestConfig::anAccountMayNameItsCalendar()
{
    // Write a config with two accounts, one naming `calendar = work-cal`,
    // using this file's existing helper for writing a config.
    // Assert account("one").calendar == "work-cal" and account("two").calendar
    // is empty.
}
```

Fill the body with the file's own helper calls; the two assertions are the test.

- [ ] **Step 2: Build, expect a compile failure.**

- [ ] **Step 3: Implement.** `Account`, after `spam`:

```cpp
    /// The calendar collection an invitation received on this account is
    /// answered into: a directory name under [general] calendars_dir. Optional;
    /// empty means ask on the first write (item 207).
    QString calendar;
```

`config.cpp`, after the `spam` read:

```cpp
        account.calendar =
            settings.value(QStringLiteral("calendar")).toString().trimmed();
```

- [ ] **Step 4: Build and run, expect pass.**

---

### Task 6: the calendar part goes out with a message

**Files:** `src/types.h` (`ComposeContext`, `OutgoingMessage`), `src/messagebuilder.cpp`, `src/composewindow.h`, `src/composewindow.cpp` (`currentMessage()` near line 1401), `tests/test_messagebuilder.cpp`

- [ ] **Step 1: Test** in `test_messagebuilder.cpp`, using that file's existing way of building an `Account` and parsing the result:

```cpp
void TestMessageBuilder::aCalendarPartTravelsAsAnAlternativeWithItsMethod()
{
    OutgoingMessage message;
    message.accountKey = QStringLiteral("work");   // the file's fixture account
    message.to = { QStringLiteral("boss@example.com") };
    message.subject = QStringLiteral("Accepted: Planning");
    message.markdownBody = QStringLiteral("See you there.");
    message.calendarPart = "BEGIN:VCALENDAR\r\nMETHOD:REPLY\r\nEND:VCALENDAR\r\n";
    message.calendarMethod = QStringLiteral("REPLY");

    const MessageBuilder::Result result = MessageBuilder::build(message, account());
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.bytes.contains("multipart/alternative"));
    QVERIFY(result.bytes.contains("text/calendar"));
    QVERIFY(result.bytes.contains("method=REPLY"));
    QVERIFY(result.bytes.contains("METHOD:REPLY"));
}
```

If the file decodes parts rather than grepping bytes, assert the same through its decoder; `METHOD:REPLY` may be encoded, so check the decoded part.

- [ ] **Step 2: Build, expect a compile failure.**

- [ ] **Step 3: Implement.**

`ComposeContext` and `OutgoingMessage`, each gaining:

```cpp
    /// An iMIP part (item 207): the bytes of a VCALENDAR and its METHOD.
    /// Empty for every ordinary message.
    QByteArray calendarPart;
    QString calendarMethod;
```

`ComposeWindow::currentMessage()`, beside `message.attachments = m_attachments;`:

```cpp
    message.calendarPart = m_context.calendarPart;
    message.calendarMethod = m_context.calendarMethod;
```

`ComposeWindow`, a public method next to `attachFile`:

```cpp
    /// Starts the send at once, exactly as the Send button does, countdown
    /// included. The one-click reply to an invitation (item 207) uses it, so
    /// that route shares every step of the send with the composer.
    void sendNow() { send(); }
```

`messagebuilder.cpp`, after the html/alternative block and BEFORE `if (!message.attachments.isEmpty())`:

```cpp
    // Item 207. An iMIP reply is an alternative of the text body (RFC 6047
    // 2.4), which is the shape calendar clients look for; as an attachment
    // most of them show a file and offer nothing.
    if (!message.calendarPart.isEmpty()) {
        GMimePart *calendar = g_mime_part_new_with_type("text", "calendar");
        g_mime_object_set_content_type_parameter(GMIME_OBJECT(calendar), "charset", "utf-8");
        const QByteArray method = message.calendarMethod.toUtf8();
        g_mime_object_set_content_type_parameter(GMIME_OBJECT(calendar), "method",
                                                 method.constData());
        GMimeStream *stream = g_mime_stream_mem_new_with_buffer(
            message.calendarPart.constData(), static_cast<size_t>(message.calendarPart.size()));
        GMimeDataWrapper *wrapper =
            g_mime_data_wrapper_new_with_stream(stream, GMIME_CONTENT_ENCODING_DEFAULT);
        g_mime_part_set_content(calendar, wrapper);
        g_mime_part_set_content_encoding(calendar, GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE);
        g_object_unref(wrapper);
        g_object_unref(stream);

        if (GMIME_IS_MULTIPART(body)
            && g_mime_content_type_is_type(g_mime_object_get_content_type(body),
                                           "multipart", "alternative")) {
            g_mime_multipart_add(GMIME_MULTIPART(body), GMIME_OBJECT(calendar));
        } else {
            GMimeMultipart *alternative = g_mime_multipart_new_with_subtype("alternative");
            g_mime_multipart_add(alternative, body);
            g_mime_multipart_add(alternative, GMIME_OBJECT(calendar));
            g_object_unref(body);
            body = GMIME_OBJECT(alternative);
        }
        g_object_unref(calendar);
    }
```

A draft saved from a "...with note" composer loses the part on resume, because `forDraft` does not read it back. ponytail: drafts of invitation replies do not round-trip; add `calendarPart` to `forDraft` from `ParsedMessage::calendarPart` if that is ever used.

- [ ] **Step 4: Build and run, expect pass.** `QT_QPA_PLATFORM=offscreen ./build/tests/test_messagebuilder`

---

### Task 7: `InvitationBar` in the message pane

**Files:**
- Create: `src/invitationbar.h`, `src/invitationbar.cpp` (license header as every other file)
- Modify: `src/CMakeLists.txt` (add `invitationbar.cpp` to `qtmaildir_lib`), `src/messageview.h`, `src/messageview.cpp`
- Test: `tests/test_invitationbar.cpp`, `tests/CMakeLists.txt` (`add_qtmaildir_test(invitationbar)`)

- [ ] **Step 1: Test.**

```cpp
#include <QtTest>
#include <QPushButton>
#include <QSignalSpy>

#include "invitationbar.h"

class TestInvitationBar : public QObject
{
    Q_OBJECT
private slots:
    void aRequestOffersTheThreeAnswers();
    void aStateWithoutAnswersOffersNone();
    void aCancelOffersRemoval();
    void strangerTextIsPlain();
    void anAnswerReportsWhichAndWhetherWithANote();
};

namespace {
Invitation sample()
{
    Invitation inv;
    inv.method = QStringLiteral("REQUEST");
    inv.uid = QStringLiteral("u");
    inv.summary = QStringLiteral("<b>Planning</b>");
    inv.start = QDateTime(QDate(2026, 11, 20), QTime(10, 0));
    inv.end = inv.start.addSecs(3600);
    inv.organizer.name = QStringLiteral("Boss");
    inv.organizer.address = QStringLiteral("boss@example.com");
    return inv;
}
QPushButton *button(InvitationBar &bar, const char *name)
{
    return bar.findChild<QPushButton *>(QString::fromLatin1(name));
}
} // namespace

void TestInvitationBar::aRequestOffersTheThreeAnswers()
{
    InvitationBar bar;
    bar.setInvitation(sample(), InvitationState::NewRequest, QString());
    for (const char *name : { "inviteAccept", "inviteTentative", "inviteDecline" })
        QVERIFY2(button(bar, name) && !button(bar, name)->isHidden(), name);
    QVERIFY(button(bar, "inviteApply")->isHidden());
}

void TestInvitationBar::aStateWithoutAnswersOffersNone()
{
    InvitationBar bar;
    for (InvitationState s : { InvitationState::NotInvited, InvitationState::OutOfDate,
                               InvitationState::CancelledUnknown,
                               InvitationState::SingleOccurrence,
                               InvitationState::NoCalendar }) {
        bar.setInvitation(sample(), s, QString());
        for (const char *name : { "inviteAccept", "inviteTentative", "inviteDecline",
                                  "inviteApply", "inviteRemove" })
            QVERIFY2(button(bar, name)->isHidden(), name);
    }
}

void TestInvitationBar::aCancelOffersRemoval()
{
    InvitationBar bar;
    bar.setInvitation(sample(), InvitationState::Cancelled, QString());
    QVERIFY(!button(bar, "inviteRemove")->isHidden());
    QVERIFY(button(bar, "inviteAccept")->isHidden());
}

void TestInvitationBar::strangerTextIsPlain()
{
    InvitationBar bar;
    bar.setInvitation(sample(), InvitationState::NewRequest, QString());
    const auto labels = bar.findChildren<QLabel *>();
    QVERIFY(!labels.isEmpty());
    for (QLabel *label : labels)
        QCOMPARE(label->textFormat(), Qt::PlainText);
}

void TestInvitationBar::anAnswerReportsWhichAndWhetherWithANote()
{
    InvitationBar bar;
    bar.setInvitation(sample(), InvitationState::NewRequest, QString());
    QSignalSpy spy(&bar, &InvitationBar::answerRequested);
    button(bar, "inviteAccept")->click();
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("ACCEPTED"));
    QCOMPARE(spy.at(0).at(1).toBool(), false);
}

QTEST_MAIN(TestInvitationBar)
#include "test_invitationbar.moc"
```

- [ ] **Step 2: Build, expect a compile failure.**

- [ ] **Step 3: Implement.**

`src/invitationbar.h`:

```cpp
#pragma once

#include <QWidget>

#include "caltypes.h"

class QLabel;
class QPushButton;

/// The message pane's invitation row (item 207). Renders one
/// InvitationState and reports what was pressed; it decides nothing and
/// writes nothing, so MainWindow keeps every calendar write in one place.
///
/// Every label is Qt::PlainText: the summary, place and names come from
/// whoever sent the mail.
class InvitationBar : public QWidget
{
    Q_OBJECT
public:
    explicit InvitationBar(QWidget *parent = nullptr);

    /// \p detail is an extra line, e.g. what an update changed, or empty.
    void setInvitation(const Invitation &invitation, InvitationState state,
                       const QString &detail);

    const Invitation &invitation() const { return m_invitation; }

signals:
    /// \p partstat is "ACCEPTED", "TENTATIVE" or "DECLINED".
    void answerRequested(const QString &partstat, bool withNote);
    void applyUpdateRequested();
    void removeRequested();

private:
    QPushButton *answerButton(const QString &text, const QString &partstat,
                              const char *objectName);

    Invitation m_invitation;
    QLabel *m_title = nullptr;
    QLabel *m_when = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_accept = nullptr;
    QPushButton *m_tentative = nullptr;
    QPushButton *m_decline = nullptr;
    QPushButton *m_apply = nullptr;
    QPushButton *m_remove = nullptr;
};
```

`src/invitationbar.cpp`:

```cpp
#include "invitationbar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QLabel *plainLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    // A QLabel guesses under Qt::AutoText, and every value here is a stranger's.
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
} // namespace

InvitationBar::InvitationBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("invitationBar"));
    m_title = plainLabel(this);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_when = plainLabel(this);
    m_status = plainLabel(this);

    m_accept = answerButton(tr("Accept"), QStringLiteral("ACCEPTED"), "inviteAccept");
    m_tentative = answerButton(tr("Tentative"), QStringLiteral("TENTATIVE"), "inviteTentative");
    m_decline = answerButton(tr("Decline"), QStringLiteral("DECLINED"), "inviteDecline");

    m_apply = new QPushButton(tr("Update calendar"), this);
    m_apply->setObjectName(QStringLiteral("inviteApply"));
    connect(m_apply, &QPushButton::clicked, this, &InvitationBar::applyUpdateRequested);
    m_remove = new QPushButton(tr("Remove from calendar"), this);
    m_remove->setObjectName(QStringLiteral("inviteRemove"));
    connect(m_remove, &QPushButton::clicked, this, &InvitationBar::removeRequested);

    auto *text = new QVBoxLayout;
    text->addWidget(m_title);
    text->addWidget(m_when);
    text->addWidget(m_status);
    auto *row = new QHBoxLayout(this);
    row->addLayout(text, 1);
    for (QPushButton *b : { m_accept, m_tentative, m_decline, m_apply, m_remove })
        row->addWidget(b, 0, Qt::AlignTop);
    hide();
}

QPushButton *InvitationBar::answerButton(const QString &text, const QString &partstat,
                                         const char *objectName)
{
    // A split button: the click answers at once, the arrow offers the composer
    // route, so a note costs one extra step and the common case none.
    auto *button = new QPushButton(text, this);
    button->setObjectName(QString::fromLatin1(objectName));
    auto *menu = new QMenu(button);
    menu->addAction(tr("%1 with a note...").arg(text), this,
                    [this, partstat] { emit answerRequested(partstat, true); });
    button->setMenu(menu);
    connect(button, &QPushButton::clicked, this,
            [this, partstat] { emit answerRequested(partstat, false); });
    return button;
}

void InvitationBar::setInvitation(const Invitation &invitation, InvitationState state,
                                  const QString &detail)
{
    using S = InvitationState;
    m_invitation = invitation;
    if (state == S::None) {
        hide();
        return;
    }

    const QString organizer = invitation.organizer.name.isEmpty()
                                  ? invitation.organizer.address
                                  : invitation.organizer.name;
    m_title->setText(tr("Invitation: %1").arg(invitation.summary));
    const QLocale locale;
    QString when = invitation.allDay
                       ? locale.toString(invitation.start.date(), QLocale::LongFormat)
                       : tr("%1 to %2").arg(
                             locale.toString(invitation.start.toLocalTime(), QLocale::ShortFormat),
                             locale.toString(invitation.end.toLocalTime().time(), QLocale::ShortFormat));
    if (!invitation.location.isEmpty())
        when += QStringLiteral(" · ") + invitation.location;
    m_when->setText(tr("%1. Organised by %2, %n attendee(s).", "", invitation.attendees.size())
                        .arg(when, organizer));

    QString status;
    switch (state) {
    case S::NoCalendar: status = tr("No calendar is configured, so this cannot be answered here."); break;
    case S::NotInvited: status = tr("You are not on the attendee list."); break;
    case S::Answered: status = tr("In your calendar."); break;
    case S::Update: status = tr("The organiser changed this invitation."); break;
    case S::OutOfDate: status = tr("Out of date: your calendar holds a newer version."); break;
    case S::Cancelled: status = tr("Cancelled by the organiser."); break;
    case S::CancelledUnknown: status = tr("Cancelled; not in your calendar."); break;
    case S::SingleOccurrence: status = tr("Changes to a single occurrence aren't supported yet."); break;
    default: break;
    }
    if (!detail.isEmpty())
        status = status.isEmpty() ? detail : status + QLatin1Char(' ') + detail;
    m_status->setText(status);
    m_status->setVisible(!status.isEmpty());

    const bool answers = state == S::NewRequest || state == S::Answered || state == S::Update;
    m_accept->setVisible(answers);
    m_tentative->setVisible(answers);
    m_decline->setVisible(answers);
    m_apply->setVisible(state == S::Update);
    m_remove->setVisible(state == S::Cancelled);
    show();
}
```

`MessageView`: own one `InvitationBar *m_invitationBar`, created in the constructor next to `m_staleBar` and added to the layout right after `m_messageBar` (before `m_view`). Add:

```cpp
    /// The invitation row (item 207). MainWindow fills it after showThread()
    /// and connects to its signals; showThread() and clear() hide it, so a
    /// message without an invitation never inherits the last one's bar.
    InvitationBar *invitationBar() const { return m_invitationBar; }
```

and call `m_invitationBar->hide();` at the top of `showThread()` and `clear()` (and `showPlaceholder()` / `showDashboard()` if those do not go through `clear()`).

- [ ] **Step 4: Build and run, expect pass.** `QT_QPA_PLATFORM=offscreen ./build/tests/test_invitationbar` and `./build/tests/test_messageview` (the latter must stay green).

---

### Task 8: MainWindow wiring

**Files:** `src/mainwindow.h`, `src/mainwindow.cpp`, `src/calendarwindow.h` (make `reload()` public), `tests/test_mainwindow.cpp`

What `MainWindow` does, in five pieces:

1. **On render.** At the end of `renderMessages()`, when exactly one message was rendered and its `calendarPart` is non-empty, call a new `showInvitation(const MessageRef &ref, const ParsedMessage &message)`:

```cpp
void MainWindow::showInvitation(const MessageRef &ref, const ParsedMessage &message)
{
    const Invitation invitation = CalendarStore::parseInvitation(message.calendarPart);
    const Account account = accountForMessagePath(ref.filePath);
    const bool configured = !m_config.calendarsDir().isEmpty();
    const LoadResult calendar = configured ? CalendarStore::load(m_config.calendarsDir())
                                           : LoadResult();
    const CalEvent *stored = nullptr;
    for (const CalEvent &event : calendar.events)
        if (event.uid == invitation.uid) { stored = &event; break; }
    const bool invited = !account.address.isEmpty()
        && CalendarStore::attendeeIndex(invitation.attendees, account.address) >= 0;

    const InvitationState state =
        CalendarStore::invitationState(invitation, stored, invited, configured);

    QString detail;
    if (stored && state == InvitationState::Update) {
        QStringList changed;
        if (stored->start != invitation.start || stored->end != invitation.end)
            changed << tr("time");
        if (stored->location != invitation.location)
            changed << tr("place");
        if (!changed.isEmpty())
            detail = tr("Changed: %1.").arg(changed.join(QStringLiteral(", ")));
    } else if (stored && state == InvitationState::Answered) {
        const int me = CalendarStore::attendeeIndex(stored->attendees, account.address);
        if (me >= 0)
            detail = partstatLabel(stored->attendees.at(me).partstat);
    }

    m_invitationRef = ref;
    m_invitationAccountKey = account.key;
    m_invitationMessageId = message.messageId;
    m_invitationStored = stored ? std::optional<CalEvent>(*stored) : std::nullopt;
    m_messageView->invitationBar()->setInvitation(invitation, state, detail);
}
```

`partstatLabel()` maps `ACCEPTED`/`TENTATIVE`/`DECLINED` to `tr("You accepted.")` etc., file-static.

`LoadResult` is loaded per invitation shown: 312 files parse in about 5 ms (206's measurement), so no cache.

2. **An answer.** Connect `InvitationBar::answerRequested` to `answerInvitation(partstat, withNote)`:

```cpp
void MainWindow::answerInvitation(const QString &partstat, bool withNote)
{
    const Invitation &invitation = m_messageView->invitationBar()->invitation();
    const Account account = m_config.account(m_invitationAccountKey);
    if (!account.canSend()) {
        showTransientStatus(tr("That invitation arrived at an account that cannot send"));
        return;
    }
    // A stored event already has its file; only a first answer that stores
    // something needs a collection, so a Decline never asks.
    QString collection;
    if (!m_invitationStored && partstat != QLatin1String("DECLINED")) {
        collection = calendarCollectionFor(account);
        if (collection.isEmpty())
            return;  // the user cancelled the picker
    }

    ComposeContext context;
    context.kind = ComposeContext::Kind::Reply;
    context.accountKey = account.key;
    context.sourceMessageId = m_invitationMessageId;
    context.inReplyTo = m_invitationMessageId;
    context.to = { invitation.organizer.address };
    context.subject = invitationReplySubject(partstat, invitation.summary);
    context.calendarPart = CalendarStore::replyText(invitation.rawText, account.address, partstat);
    context.calendarMethod = QStringLiteral("REPLY");

    // The write waits for the send (spec: the calendar never claims an answer
    // the organiser did not receive). Keyed by the invitation's Message-ID,
    // which is what sourceMessageAnswered reports back.
    PendingInvitationWrite pending;
    const QString path = m_invitationStored
        ? m_invitationStored->filePath
        : collection + QLatin1Char('/') + CalendarStore::fileNameForUid(invitation.uid);
    pending.path = path;
    pending.expected = m_invitationStored
        ? std::optional<QByteArray>(m_invitationStored->rawText) : std::nullopt;
    if (partstat == QLatin1String("DECLINED"))
        pending.newText = std::nullopt;      // nothing stored, or remove it
    else
        pending.newText = CalendarStore::storedText(invitation.rawText, account.address, partstat);
    // A decline of something never stored writes nothing at all.
    if (pending.expected || pending.newText)
        m_pendingInvitationWrites.insert(m_invitationMessageId, pending);

    ComposeWindow *composer = openComposer(context);  // see below
    if (composer && !withNote)
        composer->sendNow();
}
```

`openComposer` currently returns `void`; change it to return the `ComposeWindow *` it creates (`nullptr` on its early return). Its other callers ignore the value.

`PendingInvitationWrite` is a private struct in `mainwindow.h`: `QString path; std::optional<QByteArray> expected; std::optional<QByteArray> newText;`. `m_pendingInvitationWrites` is `QHash<QString, PendingInvitationWrite>`.

`invitationReplySubject()` is file-static: `tr("Accepted: %1")`, `tr("Tentative: %1")`, `tr("Declined: %1")`.

3. **After the send.** In `openComposer`, the existing `sourceMessageAnswered` connection gains a second receiver:

```cpp
    connect(composer, &ComposeWindow::sourceMessageAnswered, this,
            [this](const QString &sourceMessageId, const QString &) {
                applyPendingInvitationWrite(sourceMessageId);
            });
```

```cpp
void MainWindow::applyPendingInvitationWrite(const QString &sourceMessageId)
{
    const auto it = m_pendingInvitationWrites.constFind(sourceMessageId);
    if (it == m_pendingInvitationWrites.cend())
        return;
    const PendingInvitationWrite pending = it.value();
    m_pendingInvitationWrites.erase(it);
    writeInvitationChange(pending.path, pending.expected, pending.newText);
}

void MainWindow::writeInvitationChange(const QString &path,
                                       const std::optional<QByteArray> &expected,
                                       const std::optional<QByteArray> &newText)
{
    // The UID-derived name is sanitised, and the resolved path must still be
    // inside calendars_dir, compared as paths (the attachment rule).
    const QString root = QDir(m_config.calendarsDir()).canonicalPath();
    const QString dir = QFileInfo(path).absolutePath();
    if (root.isEmpty() || !QDir(dir).canonicalPath().startsWith(root + QLatin1Char('/'))) {
        showTransientStatus(tr("The calendar file is outside the calendar directory"));
        return;
    }
    QString error;
    switch (CalendarWriter::replace(path, expected, newText, &error)) {
    case CalendarWriter::Result::Ok:
        showTransientStatus(tr("Calendar updated"));
        break;
    case CalendarWriter::Result::Stale:
        showTransientStatus(tr("The calendar changed meanwhile; open the message again"));
        return;
    case CalendarWriter::Result::IoError:
        showTransientStatus(tr("The calendar could not be written: %1").arg(error));
        return;
    }
    if (!m_calendarSync)
        m_calendarSync = new CalendarSync(m_config.calendarSyncCommand(),
                                          m_config.calendarSyncDelayMs(), this);
    m_calendarSync->schedule();
    if (m_calendar)
        m_calendar->reload();
}
```

The `startsWith(root + '/')` compares canonical DIRECTORY paths, which is the "as paths" rule for a directory: `dir` is canonical, so `/cal-evil` cannot pass for `/cal`. `m_calendarSync` is a new `QPointer<CalendarSync>` member.

A composer closed or aborted without sending never emits `sourceMessageAnswered`, so its pending entry is never applied. It stays in the hash; ponytail: entries for abandoned replies are never pruned, a few bytes each per session.

4. **Update / Remove.** `applyUpdateRequested` writes `CalendarStore::storedText(invitation.rawText, account.address, <the stored partstat, or NEEDS-ACTION>)` over the stored event; `removeRequested` writes `std::nullopt`. Both call `writeInvitationChange` with the stored `rawText` as `expected`, at once (no mail goes out).

5. **Choosing the collection.** `calendarCollectionFor(account)`: `account.calendar` when set and present among `CalendarStore::load(...).collections` by `dir`, returning its `path`; otherwise a `QInputDialog::getItem` over the writable collections' display names (title `tr("Answer into which calendar?")`), returning the chosen `path` or empty on cancel. If no collection is writable, a transient status and empty.

Finally, connect the bar's three signals in the constructor where `m_messageView` is wired.

- [ ] **Step 1: Tests** in `test_mainwindow.cpp` (bare window is enough; add `applyPendingInvitationWriteForTest(const QString &id)` and `addPendingInvitationWriteForTest(id, path, newText)` public hooks in the `ForTest` style the class already uses):

```cpp
void TestMainWindow::anInvitationAnswerIsWrittenOnlyAfterItsReplySends()
{
    // The spec's rule: an aborted or failed send writes nothing.
    QTemporaryDir cal;
    QVERIFY(QDir().mkpath(cal.filePath(QStringLiteral("work"))));
    // Point calendars_dir at `cal` the way this file configures other keys.
    MainWindow window(/* this file's usual construction, with calendars_dir = cal.path() */);
    const QString path = cal.filePath(QStringLiteral("work/meeting.ics"));
    window.addPendingInvitationWriteForTest(QStringLiteral("<inv@example.com>"), path,
                                            "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");

    window.applyPendingInvitationWriteForTest(QStringLiteral("<other@example.com>"));
    QVERIFY(!QFile::exists(path));

    window.applyPendingInvitationWriteForTest(QStringLiteral("<inv@example.com>"));
    QVERIFY(QFile::exists(path));
}

void TestMainWindow::anInvitationWriteCannotLeaveTheCalendarDirectory()
{
    QTemporaryDir cal;
    QTemporaryDir elsewhere;
    MainWindow window(/* calendars_dir = cal.path() */);
    const QString path = elsewhere.filePath(QStringLiteral("x.ics"));
    window.addPendingInvitationWriteForTest(QStringLiteral("<inv@example.com>"), path,
                                            "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");
    window.applyPendingInvitationWriteForTest(QStringLiteral("<inv@example.com>"));
    QVERIFY(!QFile::exists(path));
}
```

Fill the construction from the file's own config-writing helper; the assertions are the test.

- [ ] **Step 2: Build, expect a compile failure.**

- [ ] **Step 3: Implement** pieces 1 to 5 above.

- [ ] **Step 4: Build and run.** `cmake --build build && QT_QPA_PLATFORM=offscreen ./build/tests/test_mainwindow` : the new tests pass, the four known failures stay the only failures.

---

### Task 9: translations, docs, hand-over

- [ ] **Step 1:** `lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete -locations none`, zero context warnings. Translate every new `unfinished` entry into Italian (house style: see neighbouring entries; "Accetta", "Forse", "Rifiuta", "Aggiorna il calendario", "Rimuovi dal calendario", "Invito: %1", "Accettato: %1", "Provvisorio: %1", "Rifiutato: %1"). `lrelease` must report 0 unfinished.
- [ ] **Step 2:** `cmake --build build && ctest --test-dir build --output-on-failure`: only the four known `mainwindow` failures.
- [ ] **Step 3:** `CHANGELOG.md`, `[Unreleased]` / `### Added`:

```markdown
- Invitations can be answered from the message pane. A message carrying a
  calendar invitation shows its title, time, place and organiser above the
  body, with Accept, Tentative and Decline (each also "with a note", which
  opens the composer). The reply is mailed to the organiser, and the event
  is written into the account's calendar once the reply has actually been
  sent. An organiser's update or cancellation is applied with one button.
  Each account can name its calendar with a new optional `calendar` key; an
  account without one asks the first time.
```

and under `### Upgrading` (create it if absent): `- Optional per-account key \`calendar = <collection>\` names the calendar an invitation received on that account is answered into.`

- [ ] **Step 4:** `AGENTS.md`: add `InvitationBar` to the list of widget classes that exist, and `Invitation` / `InvitationState` beside the calendar's value types in the Calendar section with one line: "A calendar write from the mail pane waits for the reply to send: `MainWindow::applyPendingInvitationWrite`, keyed by the invitation's Message-ID."
- [ ] **Step 5:** Backlog row 207: `built 2026-MM-DD on \`invitations\`, awaiting the user's hand test`.
- [ ] **Step 6: Hand-over, stop.** Report the suite result and what to look at, without committing:
  - open a message with an invitation (the user has real ones; a test can be sent from another client to oneself);
  - the bar's look and wording; Accept sends via the countdown and the event appears in the calendar window afterwards; aborting the countdown leaves the calendar untouched; "Accept with a note..." opens a composer;
  - a cancellation for that event shows Remove from calendar.
  On the user's go-ahead: commit in logical parts (store + parser + builder; bar + pane; MainWindow; translations + docs), merge `invitations` into `master`, delete the local branch only, remove the worktree, push.
