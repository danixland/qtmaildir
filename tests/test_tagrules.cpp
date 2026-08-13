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

#include <QTemporaryDir>
#include <QtTest>

#include "rulequery.h"
#include "tagrules.h"
#include "tagrulesdialog.h"

/// The risk in TagRules is the format, not painting: a field silently dropped
/// on save mis-tags real mail on the next sync, and does it quietly. These
/// tests are therefore about round-trips and rejections, and the file they
/// read is byte-for-byte what mailctl writes.
class TestTagRules : public QObject
{
    Q_OBJECT

private slots:
    void aRuleLoadsWithEveryField();
    void absentFieldsTakeTheirDefaults();
    void aMalformedRuleIsDroppedWithAWarning();
    void unknownFieldsSurviveASave();
    void stageOrderPutsAccountsFirst();
    void aQueryWithQuotesRoundTrips();
    void aMissingFileIsEmptyNotAnError();
    void aNewerVersionIsRefused();
    void openingARuleFillsTheBuilderRows();
    void switchingRulesDoesNotLeakRowsBetweenThem();
    void openingARuleWithoutEditingLeavesItByteIdentical();
    void anUnrepresentableRuleOpensInTextMode();
    void editingARowRewritesTheQuery();
    void aTextModeRuleStaysTextWhenAnotherRuleIsVisited();
    void leavingTextModeIsRefusedWhenTheQueryCannotBeShownAsRows();

private:
    QString writeRules(const QString &json);
    QTemporaryDir m_dir;
};

QString TestTagRules::writeRules(const QString &json)
{
    const QString path = m_dir.filePath(QStringLiteral("rules.json"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(json.toUtf8());
    file.close();
    return path;
}

void TestTagRules::aRuleLoadsWithEveryField()
{
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [{
        "id": "notify-forge",
        "stage": 50,
        "enabled": true,
        "add": ["notify/forge"],
        "remove": [],
        "query": "from:notifications@example.com",
        "note": "All repositories, not one project."
      }]
    })");

    TagRules rules;
    rules.load(path);

    QVERIFY2(rules.warnings().isEmpty(),
             qPrintable(rules.warnings().join(QStringLiteral("; "))));
    QCOMPARE(rules.rules().size(), 1);

    const TagRule &rule = rules.rules().first();
    QCOMPARE(rule.id, QStringLiteral("notify-forge"));
    QCOMPARE(rule.stage, 50);
    QVERIFY(rule.enabled);
    QCOMPARE(rule.add, QStringList{ QStringLiteral("notify/forge") });
    QVERIFY(rule.remove.isEmpty());
    QCOMPARE(rule.query, QStringLiteral("from:notifications@example.com"));
    QCOMPARE(rule.note, QStringLiteral("All repositories, not one project."));
}

void TestTagRules::absentFieldsTakeTheirDefaults()
{
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [{"id": "minimal", "add": ["x"],
                 "query": "from:a@example.com"}]
    })");

    TagRules rules;
    rules.load(path);

    QVERIFY(rules.warnings().isEmpty());
    const TagRule &rule = rules.rules().first();
    QCOMPARE(rule.stage, 50);
    QVERIFY(rule.enabled);
    QVERIFY(rule.remove.isEmpty());
    QVERIFY(rule.note.isEmpty());
}

void TestTagRules::aMalformedRuleIsDroppedWithAWarning()
{
    // One bad rule must not cost the others. Four separate defects, and the
    // good rule sits first so a parser that stops at the first problem is
    // caught by the count rather than by an empty list.
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "good", "add": ["x"], "query": "from:a@example.com"},
        {"id": "no-query", "add": ["y"]},
        {"id": "no-tags", "query": "from:b@example.com"},
        {"id": "Bad Id", "add": ["z"], "query": "from:c@example.com"}
      ]
    })");

    TagRules rules;
    rules.load(path);

    QCOMPARE(rules.rules().size(), 1);
    QCOMPARE(rules.rules().first().id, QStringLiteral("good"));
    QCOMPARE(rules.warnings().size(), 3);
}

void TestTagRules::unknownFieldsSurviveASave()
{
    // The neutrality guarantee. If qtmaildir strips a field mailctl wrote,
    // the file is qtmaildir's file that mailctl may read.
    const QString path = writeRules(R"({
      "version": 1,
      "future_top_level": {"set_by": "another tool"},
      "rules": [{
        "id": "keeper",
        "add": ["x"],
        "query": "from:a@example.com",
        "future_field": [1, 2, 3]
      }]
    })");

    TagRules rules;
    rules.load(path);
    QVERIFY(rules.save(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root =
        QJsonDocument::fromJson(file.readAll()).object();

    QCOMPARE(root.value(QStringLiteral("future_top_level"))
                 .toObject().value(QStringLiteral("set_by")).toString(),
             QStringLiteral("another tool"));

    const QJsonObject saved =
        root.value(QStringLiteral("rules")).toArray().first().toObject();
    QCOMPARE(saved.value(QStringLiteral("future_field")).toArray().size(), 3);
    QCOMPARE(saved.value(QStringLiteral("id")).toString(),
             QStringLiteral("keeper"));
}

void TestTagRules::stageOrderPutsAccountsFirst()
{
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "topic-b", "stage": 50, "add": ["b"],
         "query": "from:b@example.com"},
        {"id": "account", "stage": 10, "add": ["acct"],
         "query": "path:\"work/**\""},
        {"id": "topic-a", "stage": 50, "add": ["a"],
         "query": "from:a@example.com"},
        {"id": "off", "stage": 20, "add": ["c"],
         "query": "from:c@example.com", "enabled": false}
      ]
    })");

    TagRules rules;
    rules.load(path);

    QStringList ids;
    for (const TagRule &rule : rules.ordered())
        ids.append(rule.id);

    // Stage ascending, ties in file order, disabled excluded.
    QCOMPARE(ids, (QStringList{ QStringLiteral("account"),
                                QStringLiteral("topic-b"),
                                QStringLiteral("topic-a") }));
    // Still loaded, so the dialog can show and re-enable it.
    QCOMPARE(rules.rules().size(), 4);
}

void TestTagRules::aQueryWithQuotesRoundTrips()
{
    // Not hypothetical: every account rule is written path:"account/**".
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [{"id": "account", "add": ["acct"],
                 "query": "path:\"work-account/**\""}]
    })");

    TagRules rules;
    rules.load(path);
    QCOMPARE(rules.rules().first().query,
             QStringLiteral("path:\"work-account/**\""));

    QVERIFY(rules.save(path));

    TagRules reloaded;
    reloaded.load(path);
    QVERIFY(reloaded.warnings().isEmpty());
    QCOMPARE(reloaded.rules().first().query,
             QStringLiteral("path:\"work-account/**\""));
}

void TestTagRules::aMissingFileIsEmptyNotAnError()
{
    // qtmaildir must open on a machine that has never written this file.
    TagRules rules;
    rules.load(m_dir.filePath(QStringLiteral("absent.json")));
    QVERIFY(rules.rules().isEmpty());
    QVERIFY(rules.warnings().isEmpty());
    QVERIFY(rules.missing());
}

void TestTagRules::aNewerVersionIsRefused()
{
    const QString path = writeRules(R"({
      "version": 2,
      "rules": [{"id": "x", "add": ["a"], "query": "from:a@example.com"}]
    })");

    TagRules rules;
    rules.load(path);
    QVERIFY(rules.rules().isEmpty());
    QCOMPARE(rules.warnings().size(), 1);
}

void TestTagRules::openingARuleFillsTheBuilderRows()
{
    // The dialog reads the shared store from its default path, so point the
    // whole process at a temporary one. XDG_CONFIG_HOME is what
    // TagRules::defaultPath() honours.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    QFile out(configHome.filePath(QStringLiteral("mailrules/rules.json")));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "vendor",
         "query": "from:vendor.example.org and subject:receipt",
         "add": ["vendor"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRulesDialog dialog;

    QCOMPARE(dialog.rowCountForTest(), 2);
    QCOMPARE(dialog.queryLineForTest(),
             QStringLiteral("from:vendor.example.org and subject:receipt"));
}

void TestTagRules::switchingRulesDoesNotLeakRowsBetweenThem()
{
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    QFile out(configHome.filePath(QStringLiteral("mailrules/rules.json")));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "one", "query": "from:one.example.org",
         "add": ["one"], "stage": 50, "enabled": true},
        {"id": "two",
         "query": "from:two.example.org or from:three.example.org",
         "add": ["two"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRulesDialog dialog;

    // The first rule is selected on open: one row, joined All by default.
    QCOMPARE(dialog.rowCountForTest(), 1);
    QCOMPARE(dialog.queryLineForTest(), QStringLiteral("from:one.example.org"));

    dialog.selectRuleForTest(1);
    QCOMPARE(dialog.rowCountForTest(), 2);
    QCOMPARE(dialog.queryLineForTest(),
             QStringLiteral("from:two.example.org or from:three.example.org"));

    // And back, to prove the first rule was not overwritten by loading the
    // second.
    dialog.selectRuleForTest(0);
    QCOMPARE(dialog.rowCountForTest(), 1);
    QCOMPARE(dialog.queryLineForTest(), QStringLiteral("from:one.example.org"));
}

void TestTagRules::openingARuleWithoutEditingLeavesItByteIdentical()
{
    // Recompiling on open would rewrite the shared file for no reason, and
    // the companion tool would see a diff the user never made. Semantically
    // equal is not enough: the bytes must match.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "handwritten",
         "query": "not subject:receipt  and   from:plain.example.net",
         "add": ["handwritten"], "stage": 50, "enabled": true},
        {"id": "vendor",
         "query": "(from:vendor.example.org or from:vendor.example.net) and not subject:receipt",
         "add": ["vendor"], "stage": 50, "enabled": true},
        {"id": "plain", "query": "from:plain.example.org",
         "add": ["plain"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    {
        TagRulesDialog dialog;
        dialog.selectRuleForTest(2);
        dialog.selectRuleForTest(1);
        dialog.selectRuleForTest(0);
        dialog.saveForTest();
    }

    TagRules reloaded;
    reloaded.load(stored);
    QCOMPARE(reloaded.rules().size(), 3);
    // Hand-written spacing and an exclusion ahead of the positive term. Both
    // are things compile() normalises away, and this rule is deliberately the
    // one left current at Save, since that is the only rule the save path
    // writes at all. The two below round trip byte for byte on their own, so
    // neither could catch a save path that recompiles regardless.
    QCOMPARE(reloaded.rules().at(0).query,
             QStringLiteral("not subject:receipt  and   "
                            "from:plain.example.net"));
    QCOMPARE(reloaded.rules().at(1).query,
             QStringLiteral("(from:vendor.example.org or "
                            "from:vendor.example.net) and not subject:receipt"));
    QCOMPARE(reloaded.rules().at(2).query,
             QStringLiteral("from:plain.example.org"));
}

void TestTagRules::anUnrepresentableRuleOpensInTextMode()
{
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    // body: is a perfectly good notmuch prefix this builder does not model.
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "deep", "query": "body:receipt",
         "add": ["deep"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    {
        TagRulesDialog dialog;
        QVERIFY(dialog.textModeForTest());
        dialog.saveForTest();
    }

    // Unrepresentable is not invalid: it must survive a save untouched.
    TagRules reloaded;
    reloaded.load(stored);
    QCOMPARE(reloaded.rules().size(), 1);
    QCOMPARE(reloaded.rules().at(0).query, QStringLiteral("body:receipt"));
}

void TestTagRules::editingARowRewritesTheQuery()
{
    // The other half of the guarantee: when rows DO change, the stored query
    // must follow.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "vendor", "query": "from:vendor.example.org",
         "add": ["vendor"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    {
        TagRulesDialog dialog;
        dialog.setRowValueForTest(0, QStringLiteral("other.example.org"));
        dialog.saveForTest();
    }

    TagRules reloaded;
    reloaded.load(stored);
    QCOMPARE(reloaded.rules().size(), 1);
    QCOMPARE(reloaded.rules().at(0).query,
             QStringLiteral("from:other.example.org"));
}

void TestTagRules::aTextModeRuleStaysTextWhenAnotherRuleIsVisited()
{
    // The cross-rule question, asked directly. Text mode and m_loadedQuery are
    // per-rule state on a dialog that has one set of widgets, so visiting a
    // representable rule and coming back must not leave the unrepresentable one
    // holding the other rule's mode or its parsed query. Getting that wrong
    // recompiles a query the builder never modelled, which is data loss.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "deep", "query": "body:receipt",
         "add": ["deep"], "stage": 50, "enabled": true},
        {"id": "plain", "query": "from:plain.example.org",
         "add": ["plain"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    {
        TagRulesDialog dialog;
        QVERIFY(dialog.textModeForTest());

        dialog.selectRuleForTest(1);
        QVERIFY2(!dialog.textModeForTest(),
                 "a representable rule must return to the builder");
        QCOMPARE(dialog.queryLineForTest(),
                 QStringLiteral("from:plain.example.org"));

        dialog.selectRuleForTest(0);
        QVERIFY2(dialog.textModeForTest(),
                 "coming back to an unrepresentable rule must be text again");
        QCOMPARE(dialog.queryLineForTest(), QStringLiteral("body:receipt"));

        dialog.saveForTest();
    }

    TagRules reloaded;
    reloaded.load(stored);
    QCOMPARE(reloaded.rules().size(), 2);
    QCOMPARE(reloaded.rules().at(0).query, QStringLiteral("body:receipt"));
    QCOMPARE(reloaded.rules().at(1).query,
             QStringLiteral("from:plain.example.org"));
}

void TestTagRules::leavingTextModeIsRefusedWhenTheQueryCannotBeShownAsRows()
{
    // The refusal is the only path that can strand a user, so it is the one
    // most worth pinning. It reports through the warning label rather than a
    // modal, which is what lets this test exist at all: a modal would block
    // here and the branch would ship unverified.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "vendor", "query": "from:vendor.example.org",
         "add": ["vendor"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRulesDialog dialog;
    QVERIFY(!dialog.textModeForTest());

    dialog.setTextModeForTest(true);
    QVERIFY(dialog.textModeForTest());

    // Type something notmuch accepts and this builder does not model.
    dialog.setQueryTextForTest(QStringLiteral("body:receipt"));
    dialog.setTextModeForTest(false);

    QVERIFY2(dialog.textModeForTest(),
             "the checkbox must refuse to clear: no rows mean this query");
    QVERIFY2(!dialog.warningTextForTest().isEmpty(),
             "the refusal must say why, not fail silently");

    // And a representable query lets the builder back, clearing the warning.
    dialog.setQueryTextForTest(QStringLiteral("from:other.example.org"));
    dialog.setTextModeForTest(false);

    QVERIFY2(!dialog.textModeForTest(), "a representable query must return");
    QCOMPARE(dialog.rowCountForTest(), 1);
    QVERIFY2(dialog.warningTextForTest().isEmpty(),
             "a stale refusal must not outlive the query that caused it");
}

QTEST_MAIN(TestTagRules)
#include "test_tagrules.moc"
