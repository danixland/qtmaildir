# Card Avatars and the Account Fade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every card a sender avatar in its own gutter and fade the account colour across the card's left 60%, backlog item 169.

**Architecture:** Three new namespaces of free functions over values (`Avatar` for initials and fill choice, `BusinessSenders` for the sender list), so all of it is testable without a painter or a widget, exactly as `CardLayout`, `SearchTerm` and `MarkdownFormat` already are. `CardLayout` gains one rect and shifts `contentLeft`; `CardDelegate` paints the fade and the squircle. `ThreadSummary` gains `firstMessageSender`, filled by the worker walk that already fills `firstMessageId`.

**Tech Stack:** Qt 6.11 (`QCryptographicHash` from Qt Core, `QLinearGradient`/`QPainterPath` from Qt Gui), libnotmuch, C++17. Build with CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-08-26-card-avatars-design.md`. Read it before starting; this plan implements it and does not restate its reasoning.

---

## Before you start

**Never run a test binary without `QT_QPA_PLATFORM=offscreen`, and never launch `./build/src/qtmaildir`.** Running the application is the user's hand test. See `CLAUDE.md`; a direct run of `test_mainwindow` throws over a hundred windows onto the user's screen and they have asked for it to stop.

Build and test commands used throughout:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build -R <name> --output-on-failure
```

Commits are GPG-signed (`git commit -S`). Work directly on `master`.

## File Structure

**Created:**

- `src/avatar.h` / `src/avatar.cpp` — namespace `Avatar`. Initials, fill choice, and the two generated fills. No painting of its own beyond returning a `QPixmap`; no widget, no model.
- `src/businesssenders.h` / `src/businesssenders.cpp` — namespace `BusinessSenders`. Parsing, matching and appending candidates for `~/.config/qtmaildir/business-senders`.
- `tests/test_avatar.cpp`
- `tests/test_businesssenders.cpp`

**Modified:**

- `src/types.h` — one field on `ThreadSummary`.
- `src/notmuchworker.cpp` — fill that field in the existing walk; collect senders after a sync.
- `src/cardlayout.h` / `src/cardlayout.cpp` — `avatarRect`, gutter constants, `contentLeft`.
- `src/threadlistmodel.h` / `src/threadlistmodel.cpp` — two roles carrying the sender and the account address.
- `src/carddelegate.h` / `src/carddelegate.cpp` — the fade and the squircle.
- `src/mainwindow.cpp` — load the list at startup, append candidates at sync end.
- `src/CMakeLists.txt`, `tests/CMakeLists.txt` — register the new files.
- `CHANGELOG.md` — final task.

**Why two namespaces and not one:** `Avatar` is pure presentation of a value, `BusinessSenders` is file I/O. They change for different reasons and only one of them touches the disk.

---

### Task 1: `ThreadSummary::firstMessageSender`

The card has no address to hash today. `authors` is notmuch's summarised string and carries display names only, measured on the real index as `'Ryanair'`, `'The Hacker News tramite LinkedIn'`, with no `@` anywhere.

**Files:**
- Modify: `src/types.h`
- Modify: `src/notmuchworker.cpp`
- Test: `tests/test_notmuchworker.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_notmuchworker.cpp`, and declare it in the class's `private slots:` block:

```cpp
void TestNotmuchWorker::queryCarriesTheFirstMessageSender()
{
    NotmuchFixture fixture;
    fixture.addMessage("sender-probe@example.org", QStringLiteral("Probe subject"));
    fixture.index();

    NotmuchWorker worker(fixture.configPath());
    QVERIFY(worker.open());

    QSignalSpy spy(&worker, &NotmuchWorker::threadsReady);
    worker.runQuery(QStringLiteral("subject:\"Probe subject\""), 1,
                    NotmuchWorker::NewestFirst, false);
    QVERIFY(spy.count() > 0);

    const auto threads = spy.first().at(0).value<QVector<ThreadSummary>>();
    QCOMPARE(threads.size(), 1);
    // The bare address, not the display name and not notmuch's authors string.
    QCOMPARE(threads.first().firstMessageSender,
             QStringLiteral("sender-probe@example.org"));
}
```

Check the fixture's actual helper names first with `grep -n 'void addMessage\|QString configPath\|void index' tests/test_notmuchworker.cpp` and adapt the three calls above to match; the assertion is the part that matters.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ctest --test-dir build -R notmuchworker --output-on-failure
```

Expected: FAIL, `firstMessageSender` is not a member of `ThreadSummary` (compile error).

- [ ] **Step 3: Add the field**

In `src/types.h`, immediately after `firstMessageTags`:

```cpp
    /// That message's sender, as a BARE ADDRESS with no display name.
    ///
    /// `authors` above is notmuch's own summarised string and carries display
    /// names ONLY: measured against the real index, 'Ryanair' and 'The Hacker
    /// News tramite LinkedIn', with no `@` anywhere. A card therefore has no
    /// address to hash for its avatar and nothing for the business-sender list
    /// to match, which is why this exists (item 169).
    ///
    /// Hashing the display name instead was rejected: notmuch BUILDS those
    /// strings, so one sender's identity varies as the string does.
    ///
    /// Free, for the same reason `firstMessageId` and `firstMessageTags` are:
    /// the walk that finds that message is already happening and From is
    /// served from the INDEX, not the message file. Measured 2026-08-26 on the
    /// developer's database: 1322 distinct senders in 12 ms, 5105 messages
    /// enumerated in 76 ms. Do not move it behind a flag by analogy with
    /// `recipients`.
    QString firstMessageSender;
```

- [ ] **Step 4: Fill it in the worker walk**

In `src/notmuchworker.cpp`, find where `summary.firstMessageId` and `summary.firstMessageTags` are assigned from the resolved message (search for `firstMessageTags =`). Both branches, the `withRecipients` one and the ordinary one, resolve a message; assign beside them in each:

```cpp
        summary.firstMessageSender = senderAddressOf(message);
```

Add this helper in the anonymous namespace near `recipientsOf`:

```cpp
/// The bare address of a message's From, with any display name discarded.
///
/// Index-served, unlike recipientsOf() above, which is why this is not behind
/// the withRecipients flag: `From` is in notmuch's index and `To` is not.
///
/// The header is untrusted, so it is parsed rather than split: a display name
/// may legally contain an `@`, and "Ian <a@b>" split on `@` yields nonsense.
QString senderAddressOf(notmuch_message_t *message)
{
    const char *from = notmuch_message_get_header(message, "From");
    if (!from || !*from)
        return QString();

    InternetAddressList *list = internet_address_list_parse(nullptr, from);
    if (!list)
        return QString();

    QString address;
    const int count = internet_address_list_length(list);
    for (int i = 0; i < count; ++i) {
        InternetAddress *entry = internet_address_list_get_address(list, i);
        if (!entry || !INTERNET_ADDRESS_IS_MAILBOX(entry))
            continue;
        const char *addr =
            internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(entry));
        if (addr && *addr) {
            address = QString::fromUtf8(addr);
            break;
        }
    }
    g_object_unref(list);
    return address;
}
```

`notmuchworker.cpp` already includes gmime for `recipientSummary`'s neighbours; if it does not, add `#include <gmime/gmime.h>` **before every Qt header** in that file. glib declares a field named `signals`, which Qt defines as a macro, so the order is not stylistic.

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && ctest --test-dir build -R notmuchworker --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/types.h src/notmuchworker.cpp tests/test_notmuchworker.cpp
git commit -S -m "feat: carry the first message's sender address on a thread summary"
```

---

### Task 2: `Avatar::initialsFor()`

**Files:**
- Create: `src/avatar.h`, `src/avatar.cpp`
- Create: `tests/test_avatar.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_avatar.cpp`. Use the GPLv2 header from any existing test file verbatim, then:

```cpp
#include <QTest>

#include "avatar.h"

class TestAvatar : public QObject
{
    Q_OBJECT

private slots:
    void twoWordNameTakesOneLetterFromEach();
    void oneWordNameTakesItsFirstTwoLetters();
    void bareAddressTakesLocalAndDomain();
    void nothingUsableFallsBackToTheAccountLabel();
    void initialsAreAlwaysTwoLetters();
};

void TestAvatar::twoWordNameTakesOneLetterFromEach()
{
    QCOMPARE(Avatar::initialsFor(QStringLiteral("John Doe"),
                                 QStringLiteral("john@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("JD"));
    // Three words still take the FIRST two, not the first and last.
    QCOMPARE(Avatar::initialsFor(QStringLiteral("Maria Grazia Rossi"),
                                 QStringLiteral("maria@example.org"),
                                 QStringLiteral("Work")),
             QStringLiteral("MG"));
}

void TestAvatar::oneWordNameTakesItsFirstTwoLetters()
{
    QCOMPARE(Avatar::initialsFor(QStringLiteral("Cofidis"),
                                 QStringLiteral("noreply@cofidis.it"),
                                 QStringLiteral("Work")),
             QStringLiteral("CO"));
}

void TestAvatar::bareAddressTakesLocalAndDomain()
{
    QCOMPARE(Avatar::initialsFor(QString(),
                                 QStringLiteral("noreply@cofidis.it"),
                                 QStringLiteral("Work")),
             QStringLiteral("NC"));
}

void TestAvatar::nothingUsableFallsBackToTheAccountLabel()
{
    // No name and no address at all: the account's label is the last resort,
    // so a card always carries a squircle rather than a hole.
    QCOMPARE(Avatar::initialsFor(QString(), QString(),
                                 QStringLiteral("Work")),
             QStringLiteral("WO"));
    // And with nothing whatsoever, still two characters rather than empty.
    QCOMPARE(Avatar::initialsFor(QString(), QString(), QString()).size(), 2);
}

void TestAvatar::initialsAreAlwaysTwoLetters()
{
    // The shape is the point: every squircle reads the same. An address with
    // no domain, a one-letter local part and a name of one letter all still
    // produce two characters.
    const QStringList names { QString(), QStringLiteral("X"),
                              QStringLiteral("A B") };
    const QStringList addresses { QStringLiteral("a@b.org"),
                                  QStringLiteral("malformed"),
                                  QString() };
    for (const QString &name : names) {
        for (const QString &address : addresses) {
            const QString initials =
                Avatar::initialsFor(name, address, QStringLiteral("Acct"));
            QCOMPARE(initials.size(), 2);
        }
    }
}

QTEST_MAIN(TestAvatar)
#include "test_avatar.moc"
```

- [ ] **Step 2: Register the files and run the test to verify it fails**

Add `avatar.cpp` to the `qtmaildir_lib` list in `src/CMakeLists.txt` (alphabetically, before `busyindicator.cpp`), and `add_qtmaildir_test(avatar)` to `tests/CMakeLists.txt` beside the others.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

Expected: FAIL, `avatar.h` not found.

- [ ] **Step 3: Write the header**

Create `src/avatar.h` with the GPLv2 header, then:

```cpp
#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>

/// A card's sender avatar: which letters it carries and what fills it.
///
/// A NAMESPACE of free functions over values, deliberately, for the reason
/// CardLayout is a struct with no painter: the letters and the fill choice are
/// decisions with right answers, and they must be assertable without a widget,
/// a model or an exposed view. Only pixmapFor() touches a QPainter, and it
/// paints into an image it owns rather than onto a widget.
namespace Avatar
{

/// Which of the two generated fills a sender gets.
enum class Fill
{
    /// A 5x5 symmetric grid from the hash bits, under a darkening veil.
    Identicon,
    /// Two related hues from the hash, split at an angle, initials on a large
    /// flat field.
    TwoTone,
};

/// Always exactly two characters, upper-cased.
///
/// In order: a display name of two or more words gives one letter from each of
/// the first two; a one-word name gives its own first two; a bare address
/// gives the first of the local part and the first of the domain; and with
/// nothing usable, the account's label. The uniform length is the point, so
/// every squircle reads as the same shape.
QString initialsFor(const QString &displayName, const QString &address,
                    const QString &accountLabel);

/// Which fill, given whether the list claims this address as a business one.
///
/// The list wins first, then the presence of a display name. That order is
/// what lets `Ian Farrell <notifications@github.com>` read as a person while
/// a listed address stays a business whatever name it presents.
Fill fillFor(const QString &displayName, bool isBusinessSender);

/// A stable colour for an address. Same input, same colour, always.
///
/// Generated at a FIXED saturation and lightness so the initials keep their
/// contrast in both themes, exactly as TagColors::colourFor() does for a tag
/// with nothing configured.
QColor colourFor(const QString &address);

/// The finished squircle, `side` pixels a side, ready to draw.
///
/// `seed` is what the fill is generated from, normally the sender's address
/// and the account's own address when there is no sender.
QPixmap pixmapFor(const QString &seed, const QString &initials, Fill fill,
                  int side, const QFont &font);

} // namespace Avatar
```

- [ ] **Step 4: Implement `initialsFor` only**

Create `src/avatar.cpp` with the GPLv2 header, then:

```cpp
#include "avatar.h"

#include <QCryptographicHash>
#include <QPainter>
#include <QPainterPath>

namespace {

QString twoFrom(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() >= 2)
        return trimmed.left(2).toUpper();
    if (trimmed.size() == 1)
        return (trimmed + trimmed).toUpper();
    return QString();
}

} // namespace

namespace Avatar {

QString initialsFor(const QString &displayName, const QString &address,
                    const QString &accountLabel)
{
    const QStringList words = displayName.split(QLatin1Char(' '),
                                                Qt::SkipEmptyParts);
    if (words.size() >= 2) {
        return (words.at(0).left(1) + words.at(1).left(1)).toUpper();
    }
    if (words.size() == 1) {
        const QString one = twoFrom(words.at(0));
        if (!one.isEmpty())
            return one;
    }

    // No usable name. The local part and the domain each give one letter,
    // which never degrades to a single letter the way the local part alone
    // would, and never reads as a truncated word.
    const int at = address.indexOf(QLatin1Char('@'));
    if (at > 0) {
        const QString local = address.left(at).trimmed();
        const QString domain = address.mid(at + 1).trimmed();
        if (!local.isEmpty() && !domain.isEmpty())
            return (local.left(1) + domain.left(1)).toUpper();
    }
    // An address with no `@` is still something to show.
    const QString bare = twoFrom(address);
    if (!bare.isEmpty())
        return bare;

    const QString account = twoFrom(accountLabel);
    if (!account.isEmpty())
        return account;

    // Nothing at all. Two characters regardless, so the shape never breaks.
    return QStringLiteral("??");
}

} // namespace Avatar
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R avatar --output-on-failure
```

Expected: PASS, 5 tests.

- [ ] **Step 6: Commit**

```bash
git add src/avatar.h src/avatar.cpp src/CMakeLists.txt tests/test_avatar.cpp tests/CMakeLists.txt
git commit -S -m "feat: derive a sender's avatar initials"
```

---

### Task 3: `Avatar::fillFor()` and `Avatar::colourFor()`

**Files:**
- Modify: `src/avatar.cpp`
- Test: `tests/test_avatar.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_avatar.cpp`, declaring each in `private slots:`:

```cpp
void TestAvatar::aDisplayNameMeansAPerson()
{
    // The case the user asked for by name: a corporate address that presents
    // itself as a person reads as a person.
    QCOMPARE(Avatar::fillFor(QStringLiteral("Ian Farrell"), false),
             Avatar::Fill::Identicon);
    QCOMPARE(Avatar::fillFor(QString(), false), Avatar::Fill::TwoTone);
}

void TestAvatar::theListOverridesADisplayName()
{
    // A listed address stays a business even when it sets a friendly name.
    QCOMPARE(Avatar::fillFor(QStringLiteral("Cofidis"), true),
             Avatar::Fill::TwoTone);
}

void TestAvatar::aColourIsStablePerAddress()
{
    const QColor first = Avatar::colourFor(QStringLiteral("a@example.org"));
    const QColor again = Avatar::colourFor(QStringLiteral("a@example.org"));
    QCOMPARE(first, again);
    QVERIFY(first.isValid());
    QVERIFY(Avatar::colourFor(QStringLiteral("b@example.org")) != first);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `fillFor` and `colourFor` undefined (link error).

- [ ] **Step 3: Implement both**

Append to the `Avatar` namespace in `src/avatar.cpp`:

```cpp
Fill fillFor(const QString &displayName, bool isBusinessSender)
{
    // The list first: it is the user's explicit override and must beat the
    // heuristic, or a listed sender could never be pinned.
    if (isBusinessSender)
        return Fill::TwoTone;
    return displayName.trimmed().isEmpty() ? Fill::TwoTone : Fill::Identicon;
}

QColor colourFor(const QString &address)
{
    // The same construction TagColors::colourFor() uses for a tag with nothing
    // configured: hashed so it is stable, at a fixed saturation and lightness
    // so it cannot come out neon and cannot lose its contrast with the
    // initials. The lightness differs from that function's deliberately: a
    // chip carries dark text, a squircle carries white.
    const QByteArray digest =
        QCryptographicHash::hash(address.toUtf8(), QCryptographicHash::Md5);
    const int hue = static_cast<quint8>(digest.at(0)) * 360 / 256;
    return QColor::fromHsl(hue, 110, 95);
}
```

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R avatar --output-on-failure
```

Expected: PASS, 8 tests.

- [ ] **Step 5: Commit**

```bash
git add src/avatar.cpp tests/test_avatar.cpp
git commit -S -m "feat: choose an avatar fill and derive its colour"
```

---

### Task 4: `Avatar::pixmapFor()`

**Files:**
- Modify: `src/avatar.cpp`
- Test: `tests/test_avatar.cpp`

Note on what is asserted here. Per `CLAUDE.md`, counting lit pixels proves almost nothing and a rendering probe that reports "no ink" is more likely broken than the code. So this asserts **determinism and difference**, which a pixel comparison genuinely can establish, and leaves the appearance to the user's eye.

- [ ] **Step 1: Write the failing test**

```cpp
void TestAvatar::aPixmapIsStableAndDiffersPerSeed()
{
    const QFont font;
    const QPixmap first = Avatar::pixmapFor(QStringLiteral("a@example.org"),
                                            QStringLiteral("AE"),
                                            Avatar::Fill::Identicon, 44, font);
    QCOMPARE(first.size(), QSize(44, 44));
    QVERIFY(!first.isNull());

    const QPixmap again = Avatar::pixmapFor(QStringLiteral("a@example.org"),
                                            QStringLiteral("AE"),
                                            Avatar::Fill::Identicon, 44, font);
    // Same seed, same image, byte for byte: the identity must not drift
    // between repaints.
    QCOMPARE(first.toImage(), again.toImage());

    const QPixmap other = Avatar::pixmapFor(QStringLiteral("b@example.org"),
                                            QStringLiteral("AE"),
                                            Avatar::Fill::Identicon, 44, font);
    // Different sender, different image, even with identical initials.
    QVERIFY(first.toImage() != other.toImage());

    const QPixmap twoTone = Avatar::pixmapFor(QStringLiteral("a@example.org"),
                                              QStringLiteral("AE"),
                                              Avatar::Fill::TwoTone, 44, font);
    // The two fills are actually different renderings, not one with a flag.
    QVERIFY(first.toImage() != twoTone.toImage());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `pixmapFor` undefined.

- [ ] **Step 3: Implement it**

Append to the `Avatar` namespace in `src/avatar.cpp`:

```cpp
QPixmap pixmapFor(const QString &seed, const QString &initials, Fill fill,
                  int side, const QFont &font)
{
    QPixmap pixmap(side, side);
    pixmap.fill(Qt::transparent);

    const QByteArray digest =
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Md5);
    const QColor base = colourFor(seed);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The squircle. A rounded rect at ~30% of the side reads as one without
    // needing a superellipse, and clipping to it means neither fill has to
    // know the shape.
    QPainterPath squircle;
    squircle.addRoundedRect(QRectF(0, 0, side, side), side * 0.3, side * 0.3);
    painter.setClipPath(squircle);

    if (fill == Fill::Identicon) {
        // A 5x5 grid, mirrored about the vertical axis, so only the left
        // three columns come from the hash: 15 cells, one bit each, which is
        // two bytes of the digest. Symmetry is what makes the shape read as a
        // deliberate mark rather than as noise.
        painter.fillRect(QRect(0, 0, side, side), base.darker(220));
        const qreal cell = qreal(side) / 5.0;
        for (int col = 0; col < 3; ++col) {
            for (int row = 0; row < 5; ++row) {
                const int bit = col * 5 + row;
                const bool on =
                    (static_cast<quint8>(digest.at(bit / 8)) >> (bit % 8)) & 1;
                if (!on)
                    continue;
                painter.fillRect(QRectF(col * cell, row * cell, cell, cell),
                                 base);
                const int mirrored = 4 - col;
                painter.fillRect(
                    QRectF(mirrored * cell, row * cell, cell, cell), base);
            }
        }
        // The veil. Without it the initials sit on whatever the pattern
        // happens to do behind them, which is the classic legibility failure
        // this fill invites. Tune the opacity against the real font before
        // calling it done.
        painter.fillRect(QRect(0, 0, side, side), QColor(0, 0, 0, 77));
    } else {
        // Two related hues split at an angle, both from the hash. The field
        // behind the letters stays large and flat, which is the whole reason
        // this fill exists beside the identicon.
        const int angle = static_cast<quint8>(digest.at(1)) * 360 / 256;
        QLineF axis = QLineF::fromPolar(side, angle);
        axis.translate(side / 2.0, side / 2.0);
        QLinearGradient gradient(axis.p2(), axis.p1());
        gradient.setColorAt(0.0, base);
        gradient.setColorAt(0.499, base);
        gradient.setColorAt(0.5, base.darker(135));
        gradient.setColorAt(1.0, base.darker(135));
        painter.fillRect(QRect(0, 0, side, side), gradient);
    }

    // The letters. White with a soft shadow rather than a computed contrast
    // colour: the fills are generated at a fixed lightness precisely so one
    // choice works for all of them.
    QFont letters = font;
    letters.setBold(true);
    letters.setPixelSize(qMax(8, int(side * 0.36)));
    painter.setFont(letters);
    painter.setPen(QColor(0, 0, 0, 120));
    painter.drawText(QRect(1, 1, side, side), Qt::AlignCenter, initials);
    painter.setPen(Qt::white);
    painter.drawText(QRect(0, 0, side, side), Qt::AlignCenter, initials);

    return pixmap;
}
```

Add `#include <QLinearGradient>` and `#include <QLineF>` to the top of `src/avatar.cpp`.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R avatar --output-on-failure
```

Expected: PASS, 9 tests.

- [ ] **Step 5: Commit**

```bash
git add src/avatar.cpp tests/test_avatar.cpp
git commit -S -m "feat: paint the avatar squircle from a hashed seed"
```

---

### Task 5: `BusinessSenders` parsing and matching

**Files:**
- Create: `src/businesssenders.h`, `src/businesssenders.cpp`
- Create: `tests/test_businesssenders.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_businesssenders.cpp` with the GPLv2 header, then:

```cpp
#include <QTemporaryDir>
#include <QTest>

#include "businesssenders.h"

class TestBusinessSenders : public QObject
{
    Q_OBJECT

private slots:
    void anExactAddressMatches();
    void aDomainEntryMatchesEveryAddressUnderIt();
    void commentsAndBlankLinesAreIgnored();
    void whitespaceAroundAnEntryIsIgnored();
    void matchingIsCaseInsensitive();
    void anAbsentFileMatchesNothing();
};

void TestBusinessSenders::anExactAddressMatches()
{
    const BusinessSenders::List list = BusinessSenders::parse(
        QStringLiteral("noreply@cofidis.it\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("someone@cofidis.it")));
}

void TestBusinessSenders::aDomainEntryMatchesEveryAddressUnderIt()
{
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("@cofidis.it\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@cofidis.it")));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("a@example.org")));
}

void TestBusinessSenders::commentsAndBlankLinesAreIgnored()
{
    // A commented entry is the REJECT gesture: present in the file, not
    // applied. This is the property the whole file format rests on.
    const BusinessSenders::List list = BusinessSenders::parse(
        QStringLiteral("# noreply@cofidis.it (47 messages)\n"
                       "\n"
                       "   \n"
                       "billing@example.org\n"));
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("noreply@cofidis.it")));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@example.org")));
}

void TestBusinessSenders::whitespaceAroundAnEntryIsIgnored()
{
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("  billing@example.org  \n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("billing@example.org")));
}

void TestBusinessSenders::matchingIsCaseInsensitive()
{
    // Addresses arrive from headers in whatever case the sender used, so a
    // list entry that matched only one casing would look broken at random.
    const BusinessSenders::List list =
        BusinessSenders::parse(QStringLiteral("NoReply@Cofidis.IT\n"));
    QVERIFY(BusinessSenders::contains(list,
                                      QStringLiteral("noreply@cofidis.it")));
}

void TestBusinessSenders::anAbsentFileMatchesNothing()
{
    QTemporaryDir dir;
    const BusinessSenders::List list =
        BusinessSenders::load(dir.filePath(QStringLiteral("does-not-exist")));
    QVERIFY(!BusinessSenders::contains(list, QStringLiteral("a@example.org")));
}

QTEST_MAIN(TestBusinessSenders)
#include "test_businesssenders.moc"
```

- [ ] **Step 2: Register and run the test to verify it fails**

Add `businesssenders.cpp` to `src/CMakeLists.txt` and `add_qtmaildir_test(businesssenders)` to `tests/CMakeLists.txt`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

Expected: FAIL, `businesssenders.h` not found.

- [ ] **Step 3: Write the header**

Create `src/businesssenders.h` with the GPLv2 header, then:

```cpp
#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

/// The list of senders that read as businesses rather than people.
///
/// `~/.config/qtmaildir/business-senders`, plain text, one entry per line,
/// `#` comments, blank lines ignored. Deliberately NOT in qtmaildir.conf and
/// deliberately not INI: the user's stated workflow is grep-and-edit, QSettings
/// would fight a bare list, and the main config is already large.
///
/// An entry is an exact address (`noreply@cofidis.it`) or a whole domain
/// (`@cofidis.it`). No globs: a pattern language is a rule the user cannot grep
/// for literally, which defeats the file's purpose.
namespace BusinessSenders
{

/// Parsed entries, lower-cased. Two sets rather than one list so a lookup is a
/// hash probe per repaint rather than a walk.
struct List
{
    QSet<QString> addresses;
    QSet<QString> domains;   ///< Stored WITHOUT the leading '@'.
};

List parse(const QString &contents);

/// Reads `path`. A missing or unreadable file yields an empty list rather than
/// an error: the feature is cosmetic and must never block startup.
List load(const QString &path);

bool contains(const List &list, const QString &address);

/// `~/.config/qtmaildir/business-senders`, built from
/// QStandardPaths::GenericConfigLocation.
QString defaultPath();

} // namespace BusinessSenders
```

- [ ] **Step 4: Implement it**

Create `src/businesssenders.cpp` with the GPLv2 header, then:

```cpp
#include "businesssenders.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace BusinessSenders {

List parse(const QString &contents)
{
    List list;
    const QStringList lines = contents.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        // A commented entry is the reject gesture: it stays in the file so it
        // is never proposed again, and it is not applied.
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QString entry = line.toLower();
        if (entry.startsWith(QLatin1Char('@')))
            list.domains.insert(entry.mid(1));
        else
            list.addresses.insert(entry);
    }
    return list;
}

List load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return List();
    return parse(QString::fromUtf8(file.readAll()));
}

bool contains(const List &list, const QString &address)
{
    const QString lowered = address.trimmed().toLower();
    if (lowered.isEmpty())
        return false;
    if (list.addresses.contains(lowered))
        return true;

    const int at = lowered.indexOf(QLatin1Char('@'));
    if (at < 0)
        return false;
    return list.domains.contains(lowered.mid(at + 1));
}

QString defaultPath()
{
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);
    return QDir(base).filePath(
        QStringLiteral("qtmaildir/business-senders"));
}

} // namespace BusinessSenders
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R businesssenders --output-on-failure
```

Expected: PASS, 6 tests.

- [ ] **Step 6: Commit**

```bash
git add src/businesssenders.h src/businesssenders.cpp src/CMakeLists.txt tests/test_businesssenders.cpp tests/CMakeLists.txt
git commit -S -m "feat: read the business-senders list"
```

---

### Task 6: Appending candidates

This is the data-adjacent half and deserves the most care. Two rules: never write an uncommented entry, and never re-propose an address already present in any form.

**Files:**
- Modify: `src/businesssenders.h`, `src/businesssenders.cpp`
- Test: `tests/test_businesssenders.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestBusinessSenders::candidatesAreAppendedCommentedOut()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));

    QHash<QString, int> counts;
    counts.insert(QStringLiteral("noreply@cofidis.it"), 47);
    BusinessSenders::appendCandidates(path, counts);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString written = QString::fromUtf8(file.readAll());

    // Commented, and carrying the count so the user can judge it.
    QVERIFY(written.contains(QStringLiteral("# noreply@cofidis.it")));
    QVERIFY(written.contains(QStringLiteral("47")));

    // Nothing it wrote may take effect on its own.
    const BusinessSenders::List list = BusinessSenders::load(path);
    QVERIFY(!BusinessSenders::contains(list,
                                       QStringLiteral("noreply@cofidis.it")));
}

void TestBusinessSenders::anAddressAlreadyPresentIsNeverReproposed()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));

    // Both forms count as present: an active entry and a rejected one. The
    // rejected case is the one that matters, since re-proposing it would undo
    // the user's decision every ten minutes with no explanation.
    QFile seed(path);
    QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
    seed.write("billing@example.org\n# noreply@cofidis.it (47 messages)\n");
    seed.close();
    const qint64 sizeBefore = QFileInfo(path).size();

    QHash<QString, int> counts;
    counts.insert(QStringLiteral("noreply@cofidis.it"), 51);
    counts.insert(QStringLiteral("billing@example.org"), 12);
    BusinessSenders::appendCandidates(path, counts);

    QCOMPARE(QFileInfo(path).size(), sizeBefore);
}

void TestBusinessSenders::onlyBulkLookingLocalPartsAreProposed()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));

    QHash<QString, int> counts;
    counts.insert(QStringLiteral("noreply@a.org"), 3);
    counts.insert(QStringLiteral("john.doe@b.org"), 3);
    BusinessSenders::appendCandidates(path, counts);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString written = QString::fromUtf8(file.readAll());
    QVERIFY(written.contains(QStringLiteral("noreply@a.org")));
    QVERIFY(!written.contains(QStringLiteral("john.doe@b.org")));
}
```

Declare all three in `private slots:` and add `#include <QFileInfo>` to the test's includes.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `appendCandidates` undefined.

- [ ] **Step 3: Declare it**

In `src/businesssenders.h`, inside the namespace:

```cpp
/// True when a local part looks like bulk mail rather than a person.
///
/// A GUESS, and openly one. It misses senders and proposes wrong ones, which
/// is exactly why nothing it produces takes effect until the user uncomments
/// it.
bool looksLikeBulk(const QString &address);

/// Appends anything in `counts` that looks like bulk and is not already in the
/// file, COMMENTED OUT, with its message count.
///
/// Two rules, both load-bearing. It never writes an uncommented entry, so
/// nothing on screen changes until the user acts. And it skips an address
/// already present in ANY form, commented or not, so an entry the user
/// rejected is never re-proposed, and one they deleted only returns if that
/// sender writes again.
void appendCandidates(const QString &path, const QHash<QString, int> &counts);
```

Add `#include <QHash>` to the header.

- [ ] **Step 4: Implement it**

In `src/businesssenders.cpp`:

```cpp
bool looksLikeBulk(const QString &address)
{
    static const QStringList kBulkLocalParts {
        QStringLiteral("noreply"),     QStringLiteral("no-reply"),
        QStringLiteral("donotreply"),  QStringLiteral("do-not-reply"),
        QStringLiteral("info"),        QStringLiteral("support"),
        QStringLiteral("billing"),     QStringLiteral("newsletter"),
        QStringLiteral("notifications"), QStringLiteral("mailer-daemon"),
    };
    const int at = address.indexOf(QLatin1Char('@'));
    if (at <= 0)
        return false;
    const QString local = address.left(at).toLower();
    for (const QString &candidate : kBulkLocalParts) {
        if (local == candidate || local.startsWith(candidate))
            return true;
    }
    return false;
}

void appendCandidates(const QString &path, const QHash<QString, int> &counts)
{
    // Every address the file MENTIONS, active or rejected. Parsed separately
    // from parse() above, which deliberately drops comments: here a comment is
    // exactly what must be remembered.
    QSet<QString> mentioned;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines =
            QString::fromUtf8(existing.readAll()).split(QLatin1Char('\n'));
        for (const QString &raw : lines) {
            QString line = raw.trimmed();
            if (line.startsWith(QLatin1Char('#')))
                line = line.mid(1).trimmed();
            if (line.isEmpty())
                continue;
            // "noreply@cofidis.it (47 messages)" mentions the address before
            // its count.
            mentioned.insert(line.section(QLatin1Char(' '), 0, 0).toLower());
        }
        existing.close();
    }

    QStringList additions;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        const QString address = it.key().trimmed().toLower();
        if (address.isEmpty() || mentioned.contains(address))
            continue;
        if (!looksLikeBulk(address))
            continue;
        additions.append(QStringLiteral("# %1 (%2 messages)")
                             .arg(address)
                             .arg(it.value()));
    }
    if (additions.isEmpty())
        return;

    additions.sort();

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&file);
    for (const QString &line : additions)
        out << line << '\n';
}
```

Add `#include <QFileInfo>` to `src/businesssenders.cpp`.

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R businesssenders --output-on-failure
```

Expected: PASS, 9 tests.

- [ ] **Step 6: Commit**

```bash
git add src/businesssenders.h src/businesssenders.cpp tests/test_businesssenders.cpp
git commit -S -m "feat: propose business-sender candidates, always commented out"
```

---

### Task 7: `CardLayout::avatarRect`

**Files:**
- Modify: `src/cardlayout.h`, `src/cardlayout.cpp`
- Test: `tests/test_cardlayout.cpp`

- [ ] **Step 1: Write the failing test**

Add to `tests/test_cardlayout.cpp`, declaring each in `private slots:`:

```cpp
void TestCardLayout::everyRowCarriesAnAvatar()
{
    const QFont font;
    const QRect rect(0, 0, 600, CardLayout::heightFor(font));

    CardLayout::Input thread;
    const CardLayout rootCard = CardLayout::compute(thread, rect, font);
    QVERIFY(!rootCard.avatarRect.isEmpty());

    // A reply gets one too: it is the row where the sender actually changes.
    CardLayout::Input reply;
    reply.isMessage = true;
    reply.depth = 1;
    const CardLayout replyCard = CardLayout::compute(reply, rect, font);
    QVERIFY(!replyCard.avatarRect.isEmpty());
}

void TestCardLayout::theAvatarPushesTheContentRight()
{
    const QFont font;
    const QRect rect(0, 0, 600, CardLayout::heightFor(font));
    const CardLayout card = CardLayout::compute(CardLayout::Input(), rect, font);

    // The text starts after the squircle, never on it.
    QVERIFY(card.contentLeft >= card.avatarRect.right() + 1);
}

void TestCardLayout::theAvatarFollowsTheIndent()
{
    const QFont font;
    const QRect rect(0, 0, 600, CardLayout::heightFor(font));

    CardLayout::Input shallow;
    shallow.isMessage = true;
    shallow.depth = 1;
    CardLayout::Input deep;
    deep.isMessage = true;
    deep.depth = 3;

    const CardLayout shallowCard = CardLayout::compute(shallow, rect, font);
    const CardLayout deepCard = CardLayout::compute(deep, rect, font);

    // The squircle sits inside the card's own rect and moves with the nesting,
    // which is the same reason contentLeft does. Asserting on the RECT here is
    // safe precisely because it is CardLayout's own output, not a visualRect.
    QVERIFY(deepCard.avatarRect.left() > shallowCard.avatarRect.left());
}

void TestCardLayout::theAvatarIsSquareAndFitsTheCard()
{
    const QFont font;
    const QRect rect(0, 0, 600, CardLayout::heightFor(font));
    const CardLayout card = CardLayout::compute(CardLayout::Input(), rect, font);

    QCOMPARE(card.avatarRect.width(), card.avatarRect.height());
    QVERIFY(card.avatarRect.top() >= rect.top());
    QVERIFY(card.avatarRect.bottom() <= rect.bottom());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `avatarRect` is not a member of `CardLayout`.

- [ ] **Step 3: Add the rect and the constant**

In `src/cardlayout.h`, beside `accentRect`:

```cpp
    /// The sender's avatar squircle, in its own gutter before the text.
    ///
    /// On EVERY row, thread and reply alike: a reply is where the sender
    /// actually changes, so it is the row whose author is most worth seeing.
    /// Square, and inset vertically so it does not touch the card's edges.
    QRect avatarRect;

    /// Space between the avatar and the text that follows it.
    static constexpr int kAvatarGap = 8;
```

- [ ] **Step 4: Compute it**

In `src/cardlayout.cpp`, inside `compute()`, immediately after `out.contentLeft` is first assigned and **before** `right`, `lineOneTop` and the rects that use `contentLeft` are computed:

```cpp
    // The avatar, square, in the gutter between the indent and the text.
    // Sized from the card's HEIGHT rather than from a pixel constant, so it
    // follows the desktop's font exactly as markSide() does.
    const int avatarSide = qMax(0, rect.height() - kPaddingY * 2);
    out.avatarRect = QRect(out.contentLeft, rect.top() + kPaddingY,
                           avatarSide, avatarSide);
    // Everything after it starts past the squircle. This is what the item's
    // cost is: a deep reply loses the gutter on top of its indent.
    out.contentLeft = out.avatarRect.right() + 1 + kAvatarGap;
```

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R cardlayout --output-on-failure
```

Expected: PASS. If existing tests in this file assert absolute positions of `senderRect` or `subjectRect`, they will now fail correctly, since the content genuinely moved. Update those expectations to be relative to `contentLeft` rather than to fixed numbers, and note in the commit that they were adjusted.

- [ ] **Step 6: Commit**

```bash
git add src/cardlayout.h src/cardlayout.cpp tests/test_cardlayout.cpp
git commit -S -m "feat: reserve a card's avatar gutter"
```

---

### Task 8: Model roles for the sender and the account address

**Files:**
- Modify: `src/threadlistmodel.h`, `src/threadlistmodel.cpp`
- Test: `tests/test_threadlistmodel.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestThreadListModel::aRowCarriesItsSenderAndAccountAddress()
{
    ThreadListModel model;
    ThreadSummary summary;
    summary.threadId = QStringLiteral("t1");
    summary.subject = QStringLiteral("Subject");
    summary.authors = QStringLiteral("John Doe");
    summary.firstMessageId = QStringLiteral("m1");
    summary.firstMessageSender = QStringLiteral("john@example.org");
    model.setThreads({ summary });

    const QModelIndex index = model.index(0, 0);
    QCOMPARE(index.data(ThreadListModel::SenderAddressRole).toString(),
             QStringLiteral("john@example.org"));
    // The display name comes from `authors`, which is all notmuch gives.
    QCOMPARE(index.data(ThreadListModel::SenderNameRole).toString(),
             QStringLiteral("John Doe"));
}
```

Check the model's actual seeding helper (`setThreads` or equivalent) with `grep -n 'void setThreads\|void addThreads' src/threadlistmodel.h` and adapt.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `SenderAddressRole` is not a member.

- [ ] **Step 3: Add the roles**

In `src/threadlistmodel.h`, in the role enum beside `AccountColourRole`:

```cpp
        /// The bare address of the message this row stands for, for the
        /// avatar's hash and for the business-senders lookup. Empty when the
        /// query did not resolve one, which the delegate handles by falling
        /// back to the account.
        SenderAddressRole,
        /// The display name to take initials from. `authors` for a thread row,
        /// `recipients` in a flat view, matching what the card already shows.
        SenderNameRole,
```

- [ ] **Step 4: Serve them**

In `src/threadlistmodel.cpp`, in the thread-row branch of `data()`, beside the existing `AccountColourRole` case:

```cpp
    case SenderAddressRole:
        return thread.firstMessageSender;
    case SenderNameRole:
        // The same string the card's first line shows: recipients in a flat
        // view, where `authors` is the user on every row and says nothing.
        return !thread.recipients.isEmpty() ? thread.recipients
                                            : thread.authors;
```

**Add the same two cases to the MESSAGE-row branch**, which is a separate switch. `CLAUDE.md` records that a cue added to one branch and not the other is simply absent with nothing to flag it, and that this has already been missed once. For a message row, serve the node's own sender and name.

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R threadlistmodel --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/threadlistmodel.h src/threadlistmodel.cpp tests/test_threadlistmodel.cpp
git commit -S -m "feat: expose a row's sender to the delegate"
```

---

### Task 9: Painting the fade

**Files:**
- Modify: `src/carddelegate.h`, `src/carddelegate.cpp`
- Test: `tests/test_carddelegate.cpp`

The fade's geometry is asserted through a static helper rather than by counting pixels, per `CLAUDE.md`: a probe pointed at the function the production path calls into, rather than at the painter, is the one that a mutation cannot survive.

- [ ] **Step 1: Write the failing test**

```cpp
void TestCardDelegate::theFadeEndsAtSixtyPercentOfTheCard()
{
    const QRect card(0, 0, 500, 60);
    const QRect root = CardDelegate::fadeRectFor(card, QRect());
    QCOMPARE(root.left(), card.left());
    QCOMPARE(root.width(), 300);
}

void TestCardDelegate::aReplyFadeStartsAtItsOwnSpine()
{
    const QRect card(0, 0, 500, 60);
    // The innermost spine of a nested reply, which is its own coloured border.
    const QRect spine(80, 0, 2, 60);
    const QRect reply = CardDelegate::fadeRectFor(card, spine);

    // It hangs off the spine, not off the card's edge.
    QCOMPARE(reply.left(), spine.left());
    // And still ends at 60% of the CARD, so a deeper reply's wash is shorter
    // as well as further right.
    QCOMPARE(reply.right(), CardDelegate::fadeRectFor(card, QRect()).right());
    QVERIFY(reply.width() < CardDelegate::fadeRectFor(card, QRect()).width());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `fadeRectFor` is not a member of `CardDelegate`.

- [ ] **Step 3: Declare the helper**

In `src/carddelegate.h`, in the public static section beside `accentLineColour`:

```cpp
    /// Where the account's fade runs, given the card and the row's innermost
    /// spine (an empty rect for a thread root, which has none).
    ///
    /// A root's fade starts at the card's left edge; a reply's starts at its
    /// own spine, which IS its coloured left border, so the wash steps right
    /// with the nesting. Both end at 60% of the card's width, so a deeper
    /// reply's wash is shorter as well as further right.
    ///
    /// Static and rect-in, rect-out so the geometry is assertable without a
    /// painter, for the same reason CardLayout is.
    static QRect fadeRectFor(const QRect &card, const QRect &innermostSpine);

    /// How far across the card the account's colour reaches.
    static constexpr qreal kFadeFraction = 0.60;
```

- [ ] **Step 4: Implement it and paint**

In `src/carddelegate.cpp`:

```cpp
QRect CardDelegate::fadeRectFor(const QRect &card, const QRect &innermostSpine)
{
    // The EXCLUSIVE right edge, then a rect built from it: QRect::right() is
    // inclusive, which is the trap CardLayout already documents.
    const int end = card.left() + int(card.width() * kFadeFraction);
    const int start = innermostSpine.isEmpty() ? card.left()
                                               : innermostSpine.left();
    if (end <= start)
        return QRect();
    return QRect(start, card.top(), end - start, card.height());
}
```

In `paint()`, immediately **after** the chrome is drawn and **before** the accent bar (so the bar sits on top of its own fade):

```cpp
    // The account's fade. Under everything but the chrome, so the selection
    // highlight and the doomed-row tint still cover it: a selected row reading
    // mostly as selection is expected, not a fault.
    const QRect fade =
        fadeRectFor(option.rect,
                    card.spines.isEmpty() ? QRect() : card.spines.last());
    if (!fade.isEmpty() && accountColour.isValid()) {
        QColor from = lineColour;
        // A reply's wash is weaker than its root's, so an expanded thread
        // reads as one block with the root leading it.
        from.setAlphaF(card.accentRect.isEmpty() ? 0.14 : 0.30);
        QLinearGradient gradient(fade.topLeft(), fade.topRight());
        gradient.setColorAt(0.0, from);
        from.setAlphaF(0.0);
        gradient.setColorAt(1.0, from);
        painter->fillRect(fade, gradient);
    }
```

`card.spines.last()` is the innermost level, since `compute()` appends outermost first. Add `#include <QLinearGradient>` to `src/carddelegate.cpp`.

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R carddelegate --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/carddelegate.h src/carddelegate.cpp tests/test_carddelegate.cpp
git commit -S -m "feat: fade the account colour across a card"
```

---

### Task 10: Painting the avatar

**Files:**
- Modify: `src/carddelegate.h`, `src/carddelegate.cpp`
- Test: `tests/test_carddelegate.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestCardDelegate::theDelegateAsksForAScaledSquircle()
{
    // Asserted through the function the PRODUCTION path calls, not through
    // Avatar::pixmapFor() directly: a test pointed at the function being
    // called into proves what that function does and nothing about whether the
    // delegate asks it for the right thing. CLAUDE.md records a mutation that
    // survived exactly that mistake.
    const QRect card(0, 0, 500, 60);
    const QFont font;
    const CardLayout layout =
        CardLayout::compute(CardLayout::Input(), card, font);

    const QPixmap pixmap = CardDelegate::avatarFor(
        QStringLiteral("john@example.org"), QStringLiteral("John Doe"),
        QStringLiteral("me@example.org"), QStringLiteral("Work"), false,
        layout.avatarRect.width(), font);

    QCOMPARE(pixmap.size(),
             QSize(layout.avatarRect.width(), layout.avatarRect.width()));
}

void TestCardDelegate::aRowWithNoSenderFallsBackToTheAccount()
{
    const QFont font;
    // No sender address at all: the squircle is still drawn, seeded from the
    // account, so a card never shows a hole.
    const QPixmap fallback = CardDelegate::avatarFor(
        QString(), QString(), QStringLiteral("me@example.org"),
        QStringLiteral("Work"), false, 44, font);
    QVERIFY(!fallback.isNull());

    // And it is the ACCOUNT's identity, not an arbitrary one: seeding from the
    // same account twice agrees.
    const QPixmap again = CardDelegate::avatarFor(
        QString(), QString(), QStringLiteral("me@example.org"),
        QStringLiteral("Work"), false, 44, font);
    QCOMPARE(fallback.toImage(), again.toImage());
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `avatarFor` is not a member.

- [ ] **Step 3: Declare it**

In `src/carddelegate.h`:

```cpp
    /// The squircle for one row, resolved from what the model supplies.
    ///
    /// Falls back to the ACCOUNT when the row has no sender address, so every
    /// card carries an avatar rather than a hole: the seed becomes the
    /// account's own address and the letters come from its label.
    static QPixmap avatarFor(const QString &senderAddress,
                             const QString &senderName,
                             const QString &accountAddress,
                             const QString &accountLabel,
                             bool isBusinessSender, int side,
                             const QFont &font);
```

- [ ] **Step 4: Implement it and paint**

In `src/carddelegate.cpp`:

```cpp
QPixmap CardDelegate::avatarFor(const QString &senderAddress,
                                const QString &senderName,
                                const QString &accountAddress,
                                const QString &accountLabel,
                                bool isBusinessSender, int side,
                                const QFont &font)
{
    const bool haveSender = !senderAddress.trimmed().isEmpty();
    const QString seed = haveSender ? senderAddress : accountAddress;
    const QString initials =
        Avatar::initialsFor(senderName, senderAddress, accountLabel);
    const Avatar::Fill fill = Avatar::fillFor(senderName, isBusinessSender);
    return Avatar::pixmapFor(seed, initials, fill, side, font);
}
```

In `paint()`, after the fade and the accent bar:

```cpp
    if (!card.avatarRect.isEmpty()) {
        const QString senderAddress =
            index.data(ThreadListModel::SenderAddressRole).toString();
        const QString senderName =
            index.data(ThreadListModel::SenderNameRole).toString();
        painter->drawPixmap(
            card.avatarRect,
            avatarFor(senderAddress, senderName, m_accountAddress,
                      m_accountLabel,
                      BusinessSenders::contains(m_businessSenders,
                                                senderAddress),
                      card.avatarRect.width(), option.font));
    }
```

Add three members to `CardDelegate`, with a setter for each, defaulting empty: `m_accountAddress`, `m_accountLabel`, `m_businessSenders` (a `BusinessSenders::List`). `MainWindow` fills them in Task 11. Include `avatar.h` and `businesssenders.h` in `src/carddelegate.cpp`.

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R carddelegate --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/carddelegate.h src/carddelegate.cpp tests/test_carddelegate.cpp
git commit -S -m "feat: draw a sender's avatar on every card"
```

---

### Task 11: Wiring the list into the window

**Files:**
- Modify: `src/mainwindow.cpp`
- Test: `tests/test_mainwindow.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestMainWindow::theBusinessSenderListIsLoadedAtStartup()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("business-senders"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("@cofidis.it\n");
    file.close();

    MainWindow window;
    window.loadBusinessSenders(path);

    QVERIFY(window.businessSendersForTest().domains.contains(
        QStringLiteral("cofidis.it")));
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `loadBusinessSenders` is not a member.

- [ ] **Step 3: Implement**

In `MainWindow`, add:

```cpp
    /// Reads the business-senders list and hands it to the delegate.
    ///
    /// Once at startup and on an explicit reload, never per repaint and never
    /// stat-per-row: the file is small and the painting path runs on every
    /// row of every scroll.
    void loadBusinessSenders(const QString &path = QString());

    /// Test accessor, so the load can be asserted without reaching into the
    /// delegate.
    const BusinessSenders::List &businessSendersForTest() const
    {
        return m_businessSenders;
    }
```

```cpp
void MainWindow::loadBusinessSenders(const QString &path)
{
    m_businessSenders = BusinessSenders::load(
        path.isEmpty() ? BusinessSenders::defaultPath() : path);
    m_cardDelegate->setBusinessSenders(m_businessSenders);
}
```

Call it from the constructor, after the delegate is created. Also set the delegate's account address and label wherever the account selection is applied, so the fallback avatar has something to seed from; search for `AccountColourRole` in `mainwindow.cpp` for where account data already reaches the view.

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build && QT_QPA_PLATFORM=offscreen ctest --test-dir build -R mainwindow --output-on-failure
```

Expected: PASS. Note the suite takes about 25 seconds.

- [ ] **Step 5: Commit**

```bash
git add src/mainwindow.cpp src/mainwindow.h tests/test_mainwindow.cpp
git commit -S -m "feat: load the business-senders list at startup"
```

---

### Task 12: Proposing candidates after a sync

**Files:**
- Modify: `src/notmuchworker.h`, `src/notmuchworker.cpp`, `src/mainwindow.cpp`
- Test: `tests/test_notmuchworker.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
void TestNotmuchWorker::sendersAreCountedForTheCandidateList()
{
    NotmuchFixture fixture;
    fixture.addMessage("noreply@shop.example", QStringLiteral("Receipt one"));
    fixture.addMessage("noreply@shop.example", QStringLiteral("Receipt two"));
    fixture.addMessage("john@example.org", QStringLiteral("Hello"));
    fixture.index();

    NotmuchWorker worker(fixture.configPath());
    QVERIFY(worker.open());

    QSignalSpy spy(&worker, &NotmuchWorker::senderCountsReady);
    worker.countSenders(QStringLiteral("*"));
    QVERIFY(spy.count() > 0);

    const auto counts = spy.first().at(0).value<QHash<QString, int>>();
    QCOMPARE(counts.value(QStringLiteral("noreply@shop.example")), 2);
    QCOMPARE(counts.value(QStringLiteral("john@example.org")), 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build 2>&1 | tail -5
```

Expected: FAIL, `countSenders` is not a member.

- [ ] **Step 3: Implement the worker side**

In `src/notmuchworker.h`:

```cpp
public slots:
    /// Counts messages per sender address over `query`.
    ///
    /// Index-served, so it is cheap: measured 2026-08-26 on the developer's
    /// database, 1322 distinct senders in 12 ms over 5105 messages. It does
    /// NOT touch m_generation, which is the QUERY generation: bumping it would
    /// discard a thread load in flight and blank the message pane because the
    /// user synced. Item 169, following the same rule requestMessageCounts
    /// already follows.
    void countSenders(const QString &query);

signals:
    void senderCountsReady(const QHash<QString, int> &counts);
```

In `src/notmuchworker.cpp`:

```cpp
void NotmuchWorker::countSenders(const QString &query)
{
    QHash<QString, int> counts;
    if (!m_database) {
        emit senderCountsReady(counts);
        return;
    }

    NmQuery nmQuery(notmuch_query_create(m_database.get(),
                                         query.toUtf8().constData()));
    if (!nmQuery) {
        emit senderCountsReady(counts);
        return;
    }

    notmuch_messages_t *messages = nullptr;
    if (notmuch_query_search_messages(nmQuery.get(), &messages)
        != NOTMUCH_STATUS_SUCCESS) {
        emit senderCountsReady(counts);
        return;
    }

    for (; messages && notmuch_messages_valid(messages);
           notmuch_messages_move_to_next(messages)) {
        notmuch_message_t *message = notmuch_messages_get(messages);
        if (!message)
            continue;
        const QString sender = senderAddressOf(message);
        if (!sender.isEmpty())
            counts[sender.toLower()] += 1;
    }

    emit senderCountsReady(counts);
}
```

Register the metatype beside the others so a queued `QHash<QString, int>` is not dropped, exactly as `SortOrder` is: `qRegisterMetaType<QHash<QString, int>>("QHash<QString,int>");` in the same place. `Q_ENUM`-style registration is not enough for a queued argument, which `CLAUDE.md` records.

- [ ] **Step 4: Wire it to the sync**

In `MainWindow`, where a sync completes (search for where the unsynced count is cleared), request the counts, and on `senderCountsReady`:

```cpp
    connect(m_worker, &NotmuchWorker::senderCountsReady, this,
            [this](const QHash<QString, int> &counts) {
                // Never applies anything: appendCandidates writes commented
                // lines only, so nothing on screen changes until the user
                // uncomments one. The list is then reloaded so an entry they
                // uncommented by hand takes effect without a restart.
                BusinessSenders::appendCandidates(
                    BusinessSenders::defaultPath(), counts);
                loadBusinessSenders();
            });
```

Request the counts scoped to recently indexed mail rather than the whole database, so the step stays incremental: `countSenders(QStringLiteral("date:1week.."))`.

- [ ] **Step 5: Run test to verify it passes**

```bash
cmake --build build && ctest --test-dir build -R notmuchworker --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/notmuchworker.h src/notmuchworker.cpp src/mainwindow.cpp tests/test_notmuchworker.cpp
git commit -S -m "feat: propose business senders from newly synced mail"
```

---

### Task 13: Full suite, translations and documentation

**Files:**
- Modify: `translations/qtmaildir_it_IT.ts`
- Modify: `README.md`, `CHANGELOG.md`

- [ ] **Step 1: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: everything passes except `test_mainwindow undoMovesTheMessageBack`, which is backlog item 136 and pre-existing. Confirm that is the only failure; if anything else fails, it belongs to this work.

- [ ] **Step 2: Refresh the translation**

This feature adds no user-facing string if nothing was wrapped in `tr()`. Run it regardless, since the check is cheap and a missed string is now a regression:

```bash
lupdate-qt6 src/ -ts translations/qtmaildir_it_IT.ts -no-obsolete -locations none
ctest --test-dir build -R translations --output-on-failure
```

Expected: zero context warnings, and the translations test passes. If a new string appeared, translate it in the `.ts` file; `lrelease` silently DROPS an unfinished string and ships it as English inside an otherwise Italian UI.

- [ ] **Step 3: Document the list**

Add a section to `README.md` beside the existing configuration documentation:

```markdown
### `~/.config/qtmaildir/business-senders`

Addresses that should read as businesses rather than people, one per line.
A card's avatar takes its pattern from this: a listed address gets the
two-tone fill, anything presenting a display name gets the identicon.

    # a comment, and the form the application itself writes
    # noreply@cofidis.it (47 messages)
    billing@example.org
    @newsletter.example.com

An entry is either an exact address or a whole domain written `@example.com`.
Comments and blank lines are ignored.

After each sync the application appends addresses that look like bulk mail,
**always commented out**, so nothing changes appearance until you uncomment
it. Anything already in the file, commented or not, is never proposed again:
commenting a line out is therefore the permanent way to reject it, while
deleting it lets that sender be proposed again if they write to you.
```

- [ ] **Step 4: Update the changelog**

Under `## [Unreleased]`, in `### Added`:

```markdown
- Cards carry the sender's avatar: a squircle with their initials, filled with
  a pattern generated from their address so the same sender always looks the
  same. Senders that present a display name get an identicon, bulk senders a
  two-tone fill, and `~/.config/qtmaildir/business-senders` decides the
  borderline cases. Nothing is fetched from the network.
- The account's colour now fades across the left of a card instead of only
  marking its edge, and a reply's fade starts at its own indent.
```

- [ ] **Step 5: Commit**

```bash
git add README.md CHANGELOG.md translations/qtmaildir_it_IT.ts
git commit -S -m "docs: document card avatars and the business-senders list"
```

---

### Task 14: Hand off for the look

**This item is judged by looking, not by the suite.** Per the standing rule, tests cover what has a right answer; the appearance is the user's call.

- [ ] **Step 1: Tell the user what to look at**

Do not launch the application. Report that the work is ready and name what to check:

- Whether the avatar gutter costs too much subject on a deeply nested reply.
- Whether the initials stay legible over an identicon at the desktop's own font size, which is the veil's opacity (`QColor(0, 0, 0, 77)` in `avatar.cpp`).
- Whether the fade at 60% reads right on a maximised window as well as a narrow one.
- Whether a reply's weaker fade reads as belonging to its root.
- Whether the two fills are distinguishable enough to be worth having as two.

- [ ] **Step 2: Wait for the verdict before any tuning commit**

The constants most likely to move are the veil's alpha, `kFadeFraction`, the two fade alphas in `carddelegate.cpp`, and `Avatar::colourFor`'s saturation and lightness.

---

## Notes for whoever executes this

- **`ThreadSummary` fixtures need `firstMessageSender` now.** A hand-built summary without one produces a fallback avatar rather than a sender's, which is correct behaviour and a confusing test failure. `makeThread()` in `test_mainwindow.cpp` should set it.
- **Two branches in `data()`.** The thread-row and message-row switches are separate; a role added to one is silently absent from the other.
- **Do not assert an indent with `visualRect`.** `setIndentation(0)` means the view reports the same left edge for a thread and its reply. Assert on `CardLayout`'s own output.
- **`QRect::right()` is inclusive.** Both `CardLayout` and `fadeRectFor` carry exclusive right edges for this reason.
- **Never run a test binary without `QT_QPA_PLATFORM=offscreen`.**
