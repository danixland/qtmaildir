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

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "config.h"
#include "mainwindow.h"
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
    void aRuleWithABadIdLoadsForRepairRatherThanVanishing();
    void aTypedNameIsSanitisedIntoAnId();
    void aSanitisedNameThatCollidesGetsItsOwnId();
    void savingIsRefusedWhenARuleWouldNotLoadBack();
    void aRepairedIdSurvivesASaveAndReload();
    void theWarningReadsAsAWarningAndSitsBesideSave();
    void aDismissedWarningComesBackWhenThereIsSomethingNewToSay();
    void aNameTypedWithSpacesIsSanitisedInTheField();
    void aRuleAddedAndNamedInTheDialogSurvivesAReopen();
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
    void aFolderRowUsesTheDropdownAndKeepsItsSuffix();
    void theTextModeToggleSurvivesBeingSwitchedOn();
    void theWindowSizeAndColumnWidthsSurviveAReopen();
    void theWindowSizeIsSavedOnEveryWayOutOfTheDialog();
    void aReloadDoesNotDiscardARestoredColumnWidth();
    void manyConditionRowsDoNotSqueezeTheRuleList();
    void previewEmitsTheRuleQueryAsStored();
    void previewClearsTheAccountScope();

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
    // One bad rule must not cost the others. The good rule sits first so a
    // parser that stops at the first problem is caught by the count rather
    // than by an empty list.
    //
    // A rule with nothing to run is still dropped: no query and no tags are
    // both unrepairable without inventing the user's intent. A rule whose only
    // fault is its ID is NOT dropped any more, see the next test.
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "good", "add": ["x"], "query": "from:a@example.com"},
        {"id": "no-query", "add": ["y"]},
        {"id": "no-tags", "query": "from:b@example.com"}
      ]
    })");

    TagRules rules;
    rules.load(path);

    QCOMPARE(rules.rules().size(), 1);
    QCOMPARE(rules.rules().first().id, QStringLiteral("good"));
    QCOMPARE(rules.warnings().size(), 2);
}

void TestTagRules::aRuleWithABadIdLoadsForRepairRatherThanVanishing()
{
    // The defect this whole change exists for. A rule saved with a space in
    // its id was written to the file correctly, dropped on every load, and so
    // was invisible in the dialog while still occupying the file. The next
    // save from the dialog would then have deleted it for good.
    //
    // It now loads, carrying its repaired id, so the dialog can show it and
    // the user can fix it. The warning still fires: the file on disk is not
    // what the hook will run until it is saved back.
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "justeat orders", "add": ["promo"],
         "query": "from:no-reply@order.example.com"}
      ]
    })");

    TagRules rules;
    rules.load(path);

    QCOMPARE(rules.rules().size(), 1);
    QCOMPARE(rules.rules().first().id, QStringLiteral("justeat-orders"));
    QCOMPARE(rules.rules().first().add, QStringList{ QStringLiteral("promo") });
    QCOMPARE(rules.warnings().size(), 1);
    QVERIFY(rules.warnings().first().contains(QStringLiteral("justeat orders")));
}

void TestTagRules::aTypedNameIsSanitisedIntoAnId()
{
    // Spaces, capitals and punctuation are what a person types into a field
    // labelled "Name". Each case here is one the user is likely to produce,
    // and every result has to satisfy ^[a-z0-9][a-z0-9-]*$ or the hook drops
    // it.
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("justeat orders")),
             QStringLiteral("justeat-orders"));
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("JustEat Orders")),
             QStringLiteral("justeat-orders"));
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("Notify: PayPal!")),
             QStringLiteral("notify-paypal"));
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("  spaced  out  ")),
             QStringLiteral("spaced-out"));
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("-leading-dash")),
             QStringLiteral("leading-dash"));

    // A run of dashes is collapsed only when the name needed sanitising at
    // all: "a---b" already satisfies the pattern and is left exactly as it is,
    // because rewriting legal ids would churn the file mailctl also reads.
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("a---b")),
             QStringLiteral("a---b"));
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("a - - b")),
             QStringLiteral("a-b"));
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("mailing-list/SBo")),
             QStringLiteral("mailing-list-sbo"));

    // An id may not START with a dash or a digit-less symbol run, and a name
    // made only of punctuation sanitises to nothing. Empty is not a legal id,
    // so the caller has to supply a fallback rather than writing one out.
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("!!!")), QString());
    QCOMPARE(TagRules::sanitiseId(QString()), QString());

    // Already valid ids pass through untouched, or every load would rewrite
    // the file and show mailctl a diff the user never made.
    QCOMPARE(TagRules::sanitiseId(QStringLiteral("notify-github")),
             QStringLiteral("notify-github"));
}

void TestTagRules::aSanitisedNameThatCollidesGetsItsOwnId()
{
    // Sanitising maps many names onto one id, so it can manufacture the exact
    // duplicate that load() drops. "Justeat Orders" and "justeat orders" both
    // reduce to justeat-orders; the second must not silently become the first.
    const QStringList taken{ QStringLiteral("justeat-orders"),
                             QStringLiteral("justeat-orders-2") };

    QCOMPARE(TagRules::uniqueId(QStringLiteral("Justeat Orders"), taken),
             QStringLiteral("justeat-orders-3"));

    // No collision means no suffix.
    QCOMPARE(TagRules::uniqueId(QStringLiteral("promo"), taken),
             QStringLiteral("promo"));

    // A name that sanitises to nothing still has to produce a legal id.
    const QString fallback = TagRules::uniqueId(QStringLiteral("!!!"), taken);
    QVERIFY(!fallback.isEmpty());
    QVERIFY(TagRules::isValidId(fallback));
}

void TestTagRules::savingIsRefusedWhenARuleWouldNotLoadBack()
{
    // The asymmetry that caused the bug: save wrote anything, load validated.
    // validate() is the one predicate both sides now use, so a rule that
    // would not survive a reload is reported BEFORE it reaches the file.
    TagRule good;
    good.id = QStringLiteral("good");
    good.query = QStringLiteral("from:a@example.com");
    good.add = { QStringLiteral("x") };

    TagRule noQuery;
    noQuery.id = QStringLiteral("no-query");
    noQuery.add = { QStringLiteral("y") };

    TagRule noTags;
    noTags.id = QStringLiteral("no-tags");
    noTags.query = QStringLiteral("from:b@example.com");

    TagRule badId;
    badId.id = QStringLiteral("Bad Id");
    badId.query = QStringLiteral("from:c@example.com");
    badId.add = { QStringLiteral("z") };

    QVERIFY(TagRules::validate({ good }).isEmpty());

    const QStringList problems =
        TagRules::validate({ good, noQuery, noTags, badId });
    QCOMPARE(problems.size(), 3);
    QVERIFY(problems.join(QChar(' ')).contains(QStringLiteral("no-query")));
    QVERIFY(problems.join(QChar(' ')).contains(QStringLiteral("no-tags")));
    QVERIFY(problems.join(QChar(' ')).contains(QStringLiteral("Bad Id")));

    // A duplicate id survives a save and is then dropped on load, so it is a
    // save-time problem too even though each rule is fine on its own.
    TagRule twin = good;
    QCOMPARE(TagRules::validate({ good, twin }).size(), 1);
}

void TestTagRules::aRepairedIdSurvivesASaveAndReload()
{
    // End to end, and the assertion that matters to the user: the rule they
    // could not keep is still there after the round trip, with its tags, its
    // query and its note intact.
    const QString path = writeRules(R"({
      "version": 1,
      "rules": [
        {"id": "justeat orders", "add": ["promo"], "stage": 50,
         "note": "kept", "query": "from:no-reply@order.example.com"}
      ]
    })");

    TagRules loaded;
    loaded.load(path);
    QCOMPARE(loaded.rules().size(), 1);
    QVERIFY(loaded.save(path));

    TagRules reread;
    reread.load(path);
    QCOMPARE(reread.rules().size(), 1);
    QCOMPARE(reread.rules().first().id, QStringLiteral("justeat-orders"));
    QCOMPARE(reread.rules().first().note, QStringLiteral("kept"));
    QCOMPARE(reread.rules().first().query,
             QStringLiteral("from:no-reply@order.example.com"));

    // Repaired on the way in, so the second read has nothing left to complain
    // about. A warning that never clears trains the user to ignore it.
    QVERIFY(reread.warnings().isEmpty());
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
    // The row widgets are created during the select and their size hints are
    // not valid until the layout has run, which needs the event loop: without
    // this the builder reports the same height for one row and for eight, and
    // the test passes against the bug.
    QCoreApplication::processEvents();
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

void TestTagRules::theWarningReadsAsAWarningAndSitsBesideSave()
{
    // The label was correct and unread: same font and colour as the intro
    // prose two lines above it, so it looked like more explanation. The user
    // opened this dialog repeatedly, with the warning showing every time,
    // while hunting the rule it was telling them about.
    //
    // Asserted on the widget's own properties, not on a render. CLAUDE.md
    // records why a pixel probe cannot carry this: counting lit pixels cannot
    // tell one colour from another reliably, and viewport()->render() returns
    // blank often enough that a probe reporting "no red anywhere" says more
    // about the probe than the code.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    const QString stored = configHome.filePath(
        QStringLiteral("mailrules/rules.json"));
    QFile out(stored);
    QVERIFY(out.open(QIODevice::WriteOnly));
    // A rule that warns on load, so the label is populated by opening alone.
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "justeat orders", "query": "from:no-reply@order.example.com",
         "add": ["promo"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRulesDialog dialog;

    // The guard. Everything below asserts about a warning that is showing, and
    // all of it would pass vacuously against a label that never appears.
    QVERIFY2(!dialog.warningTextForTest().isEmpty(),
             "a repaired rule must warn, or this test proves nothing");

    QVERIFY2(dialog.warningTextForTest().contains(QStringLiteral("justeat")),
             "the warning must name the rule it is about");

    const QString style = dialog.warningStyleForTest();
    QVERIFY2(style.contains(QStringLiteral("background-color")),
             "a warning that is not filled reads as ordinary prose");
    QVERIFY2(style.contains(QStringLiteral("bold")), "and it must be bold");

    // Below the rule list, next to the button whose outcome it reports. The
    // intro sits at the top, so comparing against it pins the move: this
    // assertion fails if the label drifts back under the header.
    QVERIFY2(dialog.warningIsBelowTheRuleListForTest(),
             "the warning belongs beside Save, not under the intro text");

    // Plain text, because the strings interpolate ids and queries read from
    // the file. A query holding '<' would otherwise be swallowed as markup.
    QCOMPARE(dialog.warningTextFormatForTest(), Qt::PlainText);
}

void TestTagRules::aDismissedWarningComesBackWhenThereIsSomethingNewToSay()
{
    // Dismissal is per-appearance. The warning most often says the file is not
    // yet what the hook runs, so a persistent "do not show again" would rehide
    // the exact problem that went unnoticed for a session. Closing it clears
    // this one; the next thing worth saying shows it again.
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
        {"id": "justeat orders", "query": "from:no-reply@order.example.com",
         "add": ["promo"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRulesDialog dialog;
    QVERIFY2(!dialog.warningTextForTest().isEmpty(),
             "the repaired rule must warn, or the dismissal proves nothing");

    dialog.dismissWarningForTest();
    QVERIFY2(dialog.warningTextForTest().isEmpty(),
             "the X must actually clear the warning");

    // Something new to say: a rule that cannot be saved. The dismissal must
    // not have latched the banner shut.
    dialog.setNameForTest(QStringLiteral("second"));
    dialog.addRuleForTest();
    dialog.setTagsForTest(QString());
    dialog.setTextModeForTest(true);
    dialog.setQueryTextForTest(QString());
    dialog.saveForTest();

    QVERIFY2(!dialog.warningTextForTest().isEmpty(),
             "a refusal after a dismissal must still be shown");
}

void TestTagRules::aNameTypedWithSpacesIsSanitisedInTheField()
{
    // The field is labelled "Name", so a person types prose into it. What the
    // field SHOWS after the edit is committed is the assertion: sanitising
    // silently on save would leave the user looking at a name that is not the
    // one being written.
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
    dialog.setNameForTest(QStringLiteral("Justeat orders"));
    QCOMPARE(dialog.nameLineForTest(), QStringLiteral("justeat-orders"));

    dialog.saveForTest();

    TagRules reloaded;
    reloaded.load(stored);
    QCOMPARE(reloaded.rules().size(), 1);
    QCOMPARE(reloaded.rules().first().id, QStringLiteral("justeat-orders"));
    QVERIFY2(reloaded.warnings().isEmpty(),
             "a rule saved from the dialog must load back without complaint");
}

void TestTagRules::aRuleAddedAndNamedInTheDialogSurvivesAReopen()
{
    // The user's session, end to end: add a rule, name it in prose, fill in
    // the query and tags, save, reopen. Before the fix the rule was written to
    // the file with a space in its id and dropped by every reader, so the
    // dialog came back without it and the file kept a rule nothing would run.
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
        QCOMPARE(dialog.ruleCountForTest(), 1);

        dialog.addRuleForTest();
        QCOMPARE(dialog.ruleCountForTest(), 2);

        dialog.setNameForTest(QStringLiteral("Justeat orders"));
        dialog.setTextModeForTest(true);
        dialog.setQueryTextForTest(
            QStringLiteral("from:no-reply@order.example.com"));
        dialog.setTagsForTest(QStringLiteral("promo, notify/justeat"));
        dialog.saveForTest();

        QVERIFY2(dialog.warningTextForTest().isEmpty(),
                 "a complete rule must not be refused");
    }

    TagRules reloaded;
    reloaded.load(stored);
    QCOMPARE(reloaded.rules().size(), 2);

    const TagRule added = reloaded.rules().at(1);
    QCOMPARE(added.id, QStringLiteral("justeat-orders"));
    QCOMPARE(added.query,
             QStringLiteral("from:no-reply@order.example.com"));
    QCOMPARE(added.add, (QStringList{ QStringLiteral("promo"),
                                      QStringLiteral("notify/justeat") }));
    QVERIFY(reloaded.warnings().isEmpty());
}

void TestTagRules::aFolderRowUsesTheDropdownAndKeepsItsSuffix()
{
    // A path: without its suffix matches nothing and notmuch says nothing
    // about it, so the suffix must never depend on the user typing it.
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
        {"id": "account", "query": "path:\"account-one/**\"",
         "add": ["account-one"], "stage": 10, "enabled": true}
      ]
    })");
    out.close();

    TagRulesDialog dialog;
    dialog.setFolders({QStringLiteral("account-one"),
                       QStringLiteral("account-two")});

    // The stored rule round-trips: the row holds the bare name, and the
    // query keeps the suffix.
    QCOMPARE(dialog.rowCountForTest(), 1);
    QCOMPARE(dialog.queryLineForTest(),
             QStringLiteral("path:\"account-one/**\""));

    dialog.setRowValueForTest(0, QStringLiteral("account-two"));
    QCOMPARE(dialog.queryLineForTest(),
             QStringLiteral("path:\"account-two/**\""));

    dialog.saveForTest();

    TagRules reloaded;
    reloaded.load(stored);
    QCOMPARE(reloaded.rules().size(), 1);
    QCOMPARE(reloaded.rules().at(0).query,
             QStringLiteral("path:\"account-two/**\""));
}

void TestTagRules::theTextModeToggleSurvivesBeingSwitchedOn()
{
    // The toggle governs the builder, so it must not live INSIDE the builder:
    // switching to text mode hides that widget, and a checkbox parented there
    // disappears along with the rows, leaving no way back except closing the
    // dialog. That shipped in the first draft and a user found it by hand.
    //
    // Asserting on the checked state alone passes against the bug, because a
    // hidden widget still reports its state perfectly well. The question is
    // reachability.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    QFile out(configHome.filePath(QStringLiteral("mailrules/rules.json")));
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
    QVERIFY(dialog.textModeToggleIsReachableForTest());

    dialog.setTextModeForTest(true);
    QVERIFY2(dialog.textModeToggleIsReachableForTest(),
             "the toggle must survive switching to text, or there is no "
             "way back to the rows");

    // And the round trip works, which is the behaviour the user wanted.
    dialog.setTextModeForTest(false);
    QVERIFY(!dialog.textModeForTest());
    QVERIFY(dialog.textModeToggleIsReachableForTest());
    QCOMPARE(dialog.rowCountForTest(), 1);
    QCOMPARE(dialog.queryLineForTest(),
             QStringLiteral("from:vendor.example.org"));
}

namespace {

/// Writes a two-rule file under a throwaway XDG_CONFIG_HOME. Two rules rather
/// than one because the column-width tests reload the list, and a list with a
/// single row hides an off-by-one in the repopulate.
void writeTwoRules(const QTemporaryDir &configHome)
{
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));
    QFile out(configHome.filePath(QStringLiteral("mailrules/rules.json")));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "vendor", "query": "from:vendor.example.org",
         "add": ["vendor"], "stage": 50, "enabled": true},
        {"id": "lists", "query": "to:list.example.org",
         "add": ["lists"], "stage": 60, "enabled": true}
      ]
    })");
    out.close();
}

} // namespace

void TestTagRules::theWindowSizeAndColumnWidthsSurviveAReopen()
{
    // The window opened at 760x520 whatever size it was left at, and the
    // columns reset to their computed widths on every open.
    //
    // XDG_STATE_HOME is redirected as well as XDG_CONFIG_HOME: the state file
    // is where this writes, and a test must not touch the user's real
    // ~/.local/state/qtmaildir/uistate.conf.
    QTemporaryDir configHome;
    QTemporaryDir stateHome;
    QVERIFY(configHome.isValid());
    QVERIFY(stateHome.isValid());
    const QByteArray previousState = qgetenv("XDG_STATE_HOME");
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    qputenv("XDG_STATE_HOME", stateHome.path().toUtf8());
    writeTwoRules(configHome);

    {
        TagRulesDialog dialog;
        dialog.resize(900, 640);
        dialog.setColumnWidthForTest(0, 123);
        // CANCEL, not close(). The first version of this saved from
        // closeEvent and asserted with close(), which passes while the real
        // dialog forgets everything: Cancel calls reject() and Save calls
        // accept(), and neither sends a QCloseEvent. Only the window
        // manager's X button does, so the test exercised the one path the
        // buttons never take. The user found it by hand in one try.
        dialog.reject();
    }

    // Asserted on the stored VALUE, not on the reopened frame. Item 46: the
    // offscreen platform does not honour a resize, so a frame comparison here
    // would report a failure the code did not cause.
    //
    // And on a TILING compositor the frame is not the dialog's to restore at
    // all. saveGeometry stores frameGeometry beside normalGeometry, and
    // restoreGeometry restores the NORMAL one; under Hyprland the window is
    // tiled to fill its slot, so the size the user drags belongs to the tile
    // while normalGeometry stays at whatever the code last resize()d it to.
    // Measured against the real state file: frame 2248x806, normal 760x664.
    // Restoring 760 there is correct behaviour, not the bug it looks like.
    QSettings state(MainWindow::uiStatePath(), QSettings::IniFormat);
    QCOMPARE(state.value(QStringLiteral("tagrules/geometry")).toByteArray()
                 .isEmpty(), false);

    {
        TagRulesDialog reopened;
        QCOMPARE(reopened.columnWidthForTest(0), 123);
    }

    if (previousState.isEmpty())
        qunsetenv("XDG_STATE_HOME");
    else
        qputenv("XDG_STATE_HOME", previousState);
}

void TestTagRules::theWindowSizeIsSavedOnEveryWayOutOfTheDialog()
{
    // There are three ways out and they take different code paths: Cancel
    // calls reject(), Save calls accept(), and the window manager's X button
    // sends a QCloseEvent. Saving from closeEvent alone covers only the
    // third, which is how the first version of this shipped and forgot the
    // size on both buttons. done(int) is the funnel the two buttons share and
    // close() also reaches, so all three are asserted here rather than
    // trusting one to stand for the others.
    QTemporaryDir configHome;
    QTemporaryDir stateHome;
    QVERIFY(configHome.isValid());
    QVERIFY(stateHome.isValid());
    const QByteArray previousState = qgetenv("XDG_STATE_HOME");
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    qputenv("XDG_STATE_HOME", stateHome.path().toUtf8());
    writeTwoRules(configHome);

    const auto widthAfter = [&](int width, const char *how) {
        QFile::remove(MainWindow::uiStatePath());
        TagRulesDialog dialog;
        // Shown, because QWidget::close() on a widget that was never visible
        // returns early without reaching done(). The X button it stands for
        // only exists on a window that is on screen, so testing the closed
        // path from a hidden dialog proves nothing about it.
        dialog.show();
        dialog.setColumnWidthForTest(0, width);
        if (qstrcmp(how, "reject") == 0)
            dialog.reject();
        else if (qstrcmp(how, "accept") == 0)
            dialog.saveForTest();
        else
            dialog.close();

        TagRulesDialog reopened;
        return reopened.columnWidthForTest(0);
    };

    QCOMPARE(widthAfter(121, "reject"), 121);
    QCOMPARE(widthAfter(122, "accept"), 122);
    QCOMPARE(widthAfter(123, "close"), 123);

    if (previousState.isEmpty())
        qunsetenv("XDG_STATE_HOME");
    else
        qputenv("XDG_STATE_HOME", previousState);
}

void TestTagRules::aReloadDoesNotDiscardARestoredColumnWidth()
{
    // The width did not survive a close, and it did not survive an ADD or a
    // DELETE either: reloadList called resizeColumnToContents on every
    // repopulate, so a restore was undone by the first thing the user did in
    // the window. Restoring on open and reverting on the next click is worse
    // than never restoring at all, because it looks like the setting is
    // broken rather than absent.
    QTemporaryDir configHome;
    QTemporaryDir stateHome;
    QVERIFY(configHome.isValid());
    QVERIFY(stateHome.isValid());
    const QByteArray previousState = qgetenv("XDG_STATE_HOME");
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    qputenv("XDG_STATE_HOME", stateHome.path().toUtf8());
    writeTwoRules(configHome);

    TagRulesDialog dialog;
    dialog.setColumnWidthForTest(0, 137);
    dialog.reloadListForTest();
    QCOMPARE(dialog.columnWidthForTest(0), 137);

    if (previousState.isEmpty())
        qunsetenv("XDG_STATE_HOME");
    else
        qputenv("XDG_STATE_HOME", previousState);
}

void TestTagRules::manyConditionRowsDoNotSqueezeTheRuleList()
{
    // A rule with eight senders left the rule list showing about one and a
    // half rows: the list had stretch 1, but a stretch factor only shares out
    // space ABOVE each widget's minimum, and the form below it has no ceiling,
    // so every condition row added to the minimum the list had to give up.
    //
    // Measured as the height the layout demands below the list. A rule with
    // many rows must not demand materially more than a rule with one; what it
    // needs beyond that belongs in the builder's own scroll area.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    QFile out(configHome.filePath(QStringLiteral("mailrules/rules.json")));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "one", "query": "from:a.example.org",
         "add": ["x"], "stage": 50, "enabled": true},
        {"id": "many", "query":
          "from:a.example.org or from:b.example.org or from:c.example.org or from:d.example.org or from:e.example.org or from:f.example.org or from:g.example.org or from:h.example.org",
         "add": ["y"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRulesDialog dialog;
    dialog.show();

    dialog.selectRuleForTest(0);
    // The row widgets are created during the select and their size hints are
    // not valid until the layout has run, which needs the event loop: without
    // this the builder reports the same height for one row and for eight, and
    // the test passes against the bug.
    QCoreApplication::processEvents();
    QCOMPARE(dialog.rowCountForTest(), 1);
    const int withOneRow = dialog.heightDemandedBelowListForTest();

    dialog.selectRuleForTest(1);
    QCoreApplication::processEvents();
    // Guard: the fixture must actually produce the many-row case, or this
    // test passes by measuring the same rule twice.
    QCOMPARE(dialog.rowCountForTest(), 8);
    const int withEightRows = dialog.heightDemandedBelowListForTest();

    // Seven extra rows at roughly 30px each would be over 200px of growth.
    // A small increase is fine (the scroll area still has a minimum), a
    // proportional one is the bug.
    QVERIFY2(withEightRows - withOneRow < 100,
             qPrintable(QStringLiteral("one row demands %1, eight demand %2")
                            .arg(withOneRow).arg(withEightRows)));

    // And the rows are CAPPED, not merely allowed to grow inside a scroll
    // area that has no ceiling. Asserted separately because removing the cap
    // leaves the assertion above green: the editor's minimum stays flat
    // either way, so only the visible height of the row area distinguishes
    // them. Without a cap a thirty-sender rule fills the window again, this
    // time scrolling instead of squeezing.
    QVERIFY2(dialog.conditionAreaHeightForTest() <= 200,
             qPrintable(QStringLiteral("condition area is %1px tall")
                            .arg(dialog.conditionAreaHeightForTest())));
}

void TestTagRules::previewEmitsTheRuleQueryAsStored()
{
    // The query goes out EXACTLY as stored: no tag:new, no wrapping
    // parentheses. The hook adds both when it applies a rule, and a preview
    // that copied it would show nothing at all outside a sync window, since
    // tag:new is only set on mail that has just arrived.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());
    QVERIFY(QDir().mkpath(configHome.filePath(QStringLiteral("mailrules"))));

    QFile out(configHome.filePath(QStringLiteral("mailrules/rules.json")));
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(R"({
      "version": 1,
      "rules": [
        {"id": "promo", "query": "from:a.example.org or from:b.example.org",
         "add": ["promo"], "stage": 50, "enabled": true}
      ]
    })");
    out.close();

    TagRulesDialog dialog;
    QSignalSpy spy(&dialog, &TagRulesDialog::previewRequested);

    dialog.selectRuleForTest(0);
    dialog.previewForTest();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(),
             QStringLiteral("from:a.example.org or from:b.example.org"));
}

void TestTagRules::previewClearsTheAccountScope()
{
    // runQuery() wraps the bar's text in the selected account's scope. A rule
    // query usually names its own path already (path:"work/**"), so previewing
    // one while an account is selected would scope it twice and show nothing,
    // which reads as "the rule matches no mail" rather than as a UI fault.
    QTemporaryDir configHome;
    QVERIFY(configHome.isValid());
    qputenv("XDG_CONFIG_HOME", configHome.path().toUtf8());

    // An account that can actually BE selected. With the default empty config
    // the selector holds only "All accounts", so it sits at index 0 already
    // and the assertion below passes whether or not the preview clears it:
    // measured, the mutation removing the reset survived until this config
    // was added.
    const QString confPath = configHome.filePath(QStringLiteral("q.conf"));
    QFile conf(confPath);
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    conf.write("[account.work]\nmaildir=work-mail\n");
    conf.close();

    Config config;
    config.load(confPath);
    QCOMPARE(config.accounts().size(), 1);

    MainWindow window(config);
    window.selectAccountForTesting(QStringLiteral("work"));
    QCOMPARE(window.selectedAccountForTesting(), QStringLiteral("work"));

    window.previewRuleQueryForTesting(QStringLiteral("from:a.example.org"));

    QCOMPARE(window.queryTextForTesting(),
             QStringLiteral("from:a.example.org"));
    QVERIFY2(window.selectedAccountForTesting().isEmpty(),
             "a preview must run unscoped, or an account-scoped rule query "
             "is wrapped twice and matches nothing");
}

QTEST_MAIN(TestTagRules)
#include "test_tagrules.moc"
