#include <QtTest>

#include "threadcidmap.h"

/// The one piece of new security-relevant logic in the message pane: flattening
/// every message's inline parts into a single namespaced map. Two messages in a
/// thread commonly share a Content-ID, and getting this wrong shows one
/// message's image inside another.
class TestThreadCidMap : public QObject
{
    Q_OBJECT
private slots:
    void emptyThreadYieldsEmptyMap();
    void singleMessagePartsAreNamespaced();
    void sharedContentIdsDoNotCollide();
    void allowedCidsMatchMapKeys();
    void prefixWithBangIsSanitized();
    void sanitizedPrefixesStayDistinct();
    void hostileContentIdCannotForgeAnotherPrefix();
    void messagesWithoutInlinePartsAreSkipped();
};

static ThreadRenderItem makeItem(const QString &prefix,
                                 const QStringList &contentIds)
{
    ThreadRenderItem item;
    item.cidPrefix = prefix;
    for (const QString &id : contentIds) {
        InlinePart part;
        part.mimeType = QStringLiteral("image/png");
        part.data = id.toUtf8();  // Stand-in payload, unique per id.
        item.message.inlineParts.insert(id, part);
    }
    return item;
}

void TestThreadCidMap::emptyThreadYieldsEmptyMap()
{
    const ThreadCidMap map = buildThreadCidMap({});
    QVERIFY(map.parts.isEmpty());
    QVERIFY(map.allowedCids.isEmpty());
}

void TestThreadCidMap::singleMessagePartsAreNamespaced()
{
    const ThreadCidMap map = buildThreadCidMap(
        { makeItem(QStringLiteral("m0"), { QStringLiteral("logo@example.org") }) });

    QCOMPARE(map.parts.size(), 1);
    QVERIFY(map.parts.contains(QStringLiteral("m0!logo@example.org")));
    // The raw, un-namespaced id must not be servable: HtmlBuilder rewrites
    // every reference, so a request for the bare id is a forged one.
    QVERIFY(!map.parts.contains(QStringLiteral("logo@example.org")));
}

void TestThreadCidMap::sharedContentIdsDoNotCollide()
{
    // The case the namespacing exists for: two newsletters using cid:logo.
    const ThreadCidMap map = buildThreadCidMap({
        makeItem(QStringLiteral("m0"), { QStringLiteral("logo@example.org") }),
        makeItem(QStringLiteral("m1"), { QStringLiteral("logo@example.org") }),
    });

    QCOMPARE(map.parts.size(), 2);
    const InlinePart first = map.parts.value(QStringLiteral("m0!logo@example.org"));
    const InlinePart second = map.parts.value(QStringLiteral("m1!logo@example.org"));
    QCOMPARE(first.data, second.data);  // Same stand-in payload by construction,
    QVERIFY(map.parts.contains(QStringLiteral("m0!logo@example.org")));
    QVERIFY(map.parts.contains(QStringLiteral("m1!logo@example.org")));
}

void TestThreadCidMap::allowedCidsMatchMapKeys()
{
    // The interceptor allows a set of cids; the handler serves a map. If those
    // disagree, either an image 404s or one is servable that policy never
    // approved.
    const ThreadCidMap map = buildThreadCidMap({
        makeItem(QStringLiteral("m0"), { QStringLiteral("a@x"), QStringLiteral("b@x") }),
        makeItem(QStringLiteral("m1"), { QStringLiteral("a@x") }),
    });

    QCOMPARE(map.allowedCids.size(), map.parts.size());
    for (const QString &key : map.parts.keys())
        QVERIFY(map.allowedCids.contains(key));
}

void TestThreadCidMap::prefixWithBangIsSanitized()
{
    // Q_ASSERT is compiled out in release, so a malformed prefix from the
    // caller must degrade safely rather than corrupt the key space.
    const ThreadCidMap map = buildThreadCidMap(
        { makeItem(QStringLiteral("m0!evil"), { QStringLiteral("logo@x") }) });

    QCOMPARE(map.parts.size(), 1);
    const QString key = map.parts.keys().first();

    // Whatever the sanitizer produces, the invariant is that the key splits at
    // its FIRST '!' back to a prefix that itself contains no '!'.
    const int separator = key.indexOf(QLatin1Char('!'));
    QVERIFY(separator > 0);
    QVERIFY(!key.left(separator).contains(QLatin1Char('!')));
}

void TestThreadCidMap::sanitizedPrefixesStayDistinct()
{
    // Sanitizing must not merge two different messages into one key space: if
    // "a!b" and "a_b" both became "a_b", one message's image would resolve for
    // the other, which is the exact bug namespacing prevents.
    const ThreadCidMap map = buildThreadCidMap({
        makeItem(QStringLiteral("m0!x"), { QStringLiteral("logo@x") }),
        makeItem(QStringLiteral("m0_x"), { QStringLiteral("logo@x") }),
    });

    QCOMPARE(map.parts.size(), 2);
}

void TestThreadCidMap::hostileContentIdCannotForgeAnotherPrefix()
{
    // The id half is attacker-controlled and may contain '!'. A message with
    // prefix m0 must not be able to name a key belonging to m1.
    const ThreadCidMap map = buildThreadCidMap({
        makeItem(QStringLiteral("m0"), { QStringLiteral("m1!logo@x") }),
        makeItem(QStringLiteral("m1"), { QStringLiteral("logo@x") }),
    });

    QCOMPARE(map.parts.size(), 2);
    QVERIFY(map.parts.contains(QStringLiteral("m0!m1!logo@x")));
    QVERIFY(map.parts.contains(QStringLiteral("m1!logo@x")));

    // Splitting at the first '!' is what keeps these apart.
    const QString forged = QStringLiteral("m0!m1!logo@x");
    QCOMPARE(forged.left(forged.indexOf(QLatin1Char('!'))), QStringLiteral("m0"));
}

void TestThreadCidMap::messagesWithoutInlinePartsAreSkipped()
{
    const ThreadCidMap map = buildThreadCidMap({
        makeItem(QStringLiteral("m0"), {}),
        makeItem(QStringLiteral("m1"), { QStringLiteral("logo@x") }),
        makeItem(QStringLiteral("m2"), {}),
    });

    QCOMPARE(map.parts.size(), 1);
    QVERIFY(map.parts.contains(QStringLiteral("m1!logo@x")));
}

QTEST_MAIN(TestThreadCidMap)
#include "test_threadcidmap.moc"
