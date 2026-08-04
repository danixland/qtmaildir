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

#include "tagcolors.h"

class TestTagColors : public QObject
{
    Q_OBJECT
private slots:
    void builtInDefaultsExist();
    void prefixColoursWholeHierarchy();
    void exactTagBeatsItsPrefix();
    void configOverridesABuiltIn();
    void unknownTagStillGetsAColour();
    void accountTagsAreRecognised();
    void accountColourComesFromTheAccount();
    void accountLabelDefaultsToTheKey();
    void accountLabelCanBeOverridden();
    void malformedColourIsReported();
    void textContrastsWithItsBackground();
};

void TestTagColors::builtInDefaultsExist()
{
    // The common state tags must be styled out of the box: a user who never
    // writes a [tagcolors] section still needs flagged to stand out.
    TagColors colours;
    const QStringList expected = { QStringLiteral("flagged"),
                                   QStringLiteral("unread"),
                                   QStringLiteral("deleted"),
                                   QStringLiteral("spam"),
                                   QStringLiteral("attachment"),
                                   QStringLiteral("replied") };
    for (const QString &tag : expected) {
        QVERIFY2(colours.hasColour(tag),
                 qPrintable(QStringLiteral("no built-in colour for '%1'").arg(tag)));
    }
}

void TestTagColors::prefixColoursWholeHierarchy()
{
    // 96 tags, many of them shopping/foo and mailing-list/bar. Colouring by
    // top-level prefix is what keeps the config from listing every one.
    TagColors colours;
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("tagcolors"));
        s.setValue(QStringLiteral("shopping"), QStringLiteral("#3366cc"));
        s.endGroup();
    }
    QSettings s(path, QSettings::IniFormat);
    colours.load(s);

    QCOMPARE(colours.colourFor(QStringLiteral("shopping/amazon")),
             QColor(QStringLiteral("#3366cc")));
    QCOMPARE(colours.colourFor(QStringLiteral("shopping/nike")),
             QColor(QStringLiteral("#3366cc")));
    // The bare prefix itself is a tag too.
    QCOMPARE(colours.colourFor(QStringLiteral("shopping")),
             QColor(QStringLiteral("#3366cc")));
    // A different hierarchy is unaffected.
    QVERIFY(colours.colourFor(QStringLiteral("mailing-list/SBo"))
            != QColor(QStringLiteral("#3366cc")));
}

void TestTagColors::exactTagBeatsItsPrefix()
{
    // Specific beats general, or you could never single out one child tag.
    TagColors colours;
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("tagcolors"));
        s.setValue(QStringLiteral("shopping"), QStringLiteral("#3366cc"));
        s.setValue(QStringLiteral("shopping/amazon"), QStringLiteral("#ff9900"));
        s.endGroup();
    }
    QSettings s(path, QSettings::IniFormat);
    colours.load(s);

    QCOMPARE(colours.colourFor(QStringLiteral("shopping/amazon")),
             QColor(QStringLiteral("#ff9900")));
    QCOMPARE(colours.colourFor(QStringLiteral("shopping/nike")),
             QColor(QStringLiteral("#3366cc")));

    // Regression: QSettings treats '/' as a group separator, so a
    // hierarchical tag is a nested key that childKeys() never returns. Reading
    // the group with childKeys() silently dropped every tag with a '/' in it,
    // which is most of this user's, and they all fell through to their prefix.
    QVERIFY(colours.hasColour(QStringLiteral("shopping/amazon")));
}

void TestTagColors::configOverridesABuiltIn()
{
    TagColors colours;
    const QColor original = colours.colourFor(QStringLiteral("flagged"));

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("tagcolors"));
        s.setValue(QStringLiteral("flagged"), QStringLiteral("#00ff00"));
        s.endGroup();
    }
    QSettings s(path, QSettings::IniFormat);
    colours.load(s);

    QCOMPARE(colours.colourFor(QStringLiteral("flagged")),
             QColor(QStringLiteral("#00ff00")));
    QVERIFY(colours.colourFor(QStringLiteral("flagged")) != original);
}

void TestTagColors::unknownTagStillGetsAColour()
{
    // A chip with no colour would render as an invisible blank, so every tag
    // resolves to something even when nothing is configured for it.
    TagColors colours;
    const QColor colour = colours.colourFor(QStringLiteral("no-such-tag-anywhere"));
    QVERIFY(colour.isValid());

    // Stable across calls: a tag must not change colour as you scroll.
    QCOMPARE(colours.colourFor(QStringLiteral("no-such-tag-anywhere")), colour);
}

void TestTagColors::accountTagsAreRecognised()
{
    // Account tags are a different taxonomy from functional tags: which
    // mailbox a thread came from, not what state it is in. They are shown
    // separately, so they have to be identifiable.
    QVERIFY(TagColors::isAccountTag(QStringLiteral("account-webmail-personal")));
    QVERIFY(!TagColors::isAccountTag(QStringLiteral("flagged")));
    QVERIFY(!TagColors::isAccountTag(QStringLiteral("shopping/amazon")));

    // The INI key for [account.webmail-personal] is what follows "account-".
    QCOMPARE(TagColors::accountKeyForTag(QStringLiteral("account-webmail-personal")),
             QStringLiteral("webmail-personal"));
    QVERIFY(TagColors::accountKeyForTag(QStringLiteral("flagged")).isEmpty());

    // Round trip, since the mapping is derived rather than configured.
    QCOMPARE(TagColors::tagForAccountKey(QStringLiteral("webmail-personal")),
             QStringLiteral("account-webmail-personal"));
}

void TestTagColors::accountColourComesFromTheAccount()
{
    // Per the account stanza, not [tagcolors]: the colour belongs to the
    // account, and the tag name is derived from its key.
    TagColors colours;
    colours.setAccountColour(QStringLiteral("webmail-personal"),
                             QColor(QStringLiteral("#cc0000")));

    QCOMPARE(colours.colourFor(QStringLiteral("account-webmail-personal")),
             QColor(QStringLiteral("#cc0000")));
}

void TestTagColors::accountLabelDefaultsToTheKey()
{
    // Without a configured label the chip shows the account key, which is what
    // it did before labels existed.
    TagColors colours;
    QCOMPARE(colours.labelForAccountTag(QStringLiteral("account-webmail-personal")),
             QStringLiteral("webmail-personal"));

    // Not an account tag: nothing to label.
    QVERIFY(colours.labelForAccountTag(QStringLiteral("flagged")).isEmpty());
}

void TestTagColors::accountLabelCanBeOverridden()
{
    // A real account tag can run to 33 characters of chip for what is really
    // one bit of information, so the label is configurable.
    TagColors colours;
    colours.setAccountLabel(QStringLiteral("webmail-personal"),
                            QStringLiteral("WM-personal"));
    colours.setAccountLabel(QStringLiteral("provider-work"),
                            QStringLiteral("PR-work"));

    QCOMPARE(colours.labelForAccountTag(QStringLiteral("account-webmail-personal")),
             QStringLiteral("WM-personal"));
    QCOMPARE(colours.labelForAccountTag(QStringLiteral("account-provider-work")),
             QStringLiteral("PR-work"));

    // An account left unlabelled still falls back to its key.
    QCOMPARE(colours.labelForAccountTag(QStringLiteral("account-work")),
             QStringLiteral("work"));

    // An empty label is not an override: it would render a blank chip.
    colours.setAccountLabel(QStringLiteral("webmail-personal"), QString());
    QCOMPARE(colours.labelForAccountTag(QStringLiteral("account-webmail-personal")),
             QStringLiteral("WM-personal"));
}

void TestTagColors::malformedColourIsReported()
{
    // A typo must be visible rather than silently ignored, matching how the
    // rest of the config reports its problems.
    TagColors colours;
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.conf"));
    {
        QSettings s(path, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("tagcolors"));
        s.setValue(QStringLiteral("flagged"), QStringLiteral("not-a-colour"));
        s.endGroup();
    }
    QSettings s(path, QSettings::IniFormat);
    colours.load(s);

    QCOMPARE(colours.warnings().size(), 1);
    QVERIFY(colours.warnings().first().contains(QStringLiteral("flagged")));
    // The built-in survives, so one bad line does not leave the tag unstyled.
    QVERIFY(colours.colourFor(QStringLiteral("flagged")).isValid());
}

void TestTagColors::textContrastsWithItsBackground()
{
    // A chip is coloured text on a coloured fill, so the pair has to stay
    // legible whatever colour the user picks.
    QCOMPARE(TagColors::textColourOn(QColor(Qt::black)), QColor(Qt::white));
    QCOMPARE(TagColors::textColourOn(QColor(Qt::white)), QColor(Qt::black));
    QCOMPARE(TagColors::textColourOn(QColor(QStringLiteral("#8b2c2c"))),
             QColor(Qt::white));
    QCOMPARE(TagColors::textColourOn(QColor(QStringLiteral("#ffee88"))),
             QColor(Qt::black));
}

QTEST_MAIN(TestTagColors)
#include "test_tagcolors.moc"
