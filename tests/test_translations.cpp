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

// Guards the shipped translation against the two ways it rots silently: a new
// user-facing string added without a translation, and the extraction defect
// item 22 exists to fix, where a literal is invisible to lupdate and therefore
// untranslatable while the source looks correct.
//
// This asserts on the .ts file rather than on a running UI deliberately. The
// backlog entry is explicit that lupdate output is the evidence here, not
// reading: the eight rule-builder labels below were wrapped in QT_TR_NOOP,
// compiled, ran, and were still unreachable by any translation.

#include <QtTest>

#include <QFile>
#include <QSet>
#include <QString>
#include <QXmlStreamReader>

class TestTranslations : public QObject
{
    Q_OBJECT

private slots:
    void everyStringIsTranslated();
    void everyStringIsTranslated_data();

    void theRuleBuilderFieldLabelsAreExtracted();
    void theFileIsWellFormedAndItalian();

private:
    static QString tsPath()
    {
        return QStringLiteral(TRANSLATIONS_DIR "/qtmaildir_it_IT.ts");
    }
};

// Reads every <message> as (context, source, translation, unfinished).
struct Entry {
    QString context;
    QString source;
    QString translation;
    bool unfinished = false;
};

static QList<Entry> readEntries(const QString &path, QString *error)
{
    QList<Entry> entries;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return entries;
    }

    QXmlStreamReader xml(&file);
    QString context;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        // <name> appears inside both <context> and <message>; only the one
        // directly under <context> names the class.
        if (xml.name() == QLatin1String("name")) {
            context = xml.readElementText();
            continue;
        }
        if (xml.name() != QLatin1String("message"))
            continue;

        Entry entry;
        entry.context = context;
        const bool numerus =
            xml.attributes().value(QLatin1String("numerus")) == QLatin1String("yes");

        while (!(xml.isEndElement() && xml.name() == QLatin1String("message"))
               && !xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            if (xml.name() == QLatin1String("source")) {
                entry.source = xml.readElementText();
            } else if (xml.name() == QLatin1String("translation")) {
                entry.unfinished =
                    xml.attributes().value(QLatin1String("type"))
                        == QLatin1String("unfinished");
                if (!numerus) {
                    entry.translation = xml.readElementText();
                } else {
                    // A numerus message carries one <numerusform> per plural
                    // form. Italian has two, and an empty one is as untranslated
                    // as an empty <translation>.
                    QStringList forms;
                    while (!(xml.isEndElement()
                             && xml.name() == QLatin1String("translation"))
                           && !xml.atEnd()) {
                        xml.readNext();
                        if (xml.isStartElement()
                            && xml.name() == QLatin1String("numerusform"))
                            forms << xml.readElementText();
                    }
                    entry.translation = forms.join(QLatin1Char('\x1f'));
                    if (forms.size() != 2 || forms.contains(QString()))
                        entry.translation.clear();
                }
            }
        }
        entries.append(entry);
    }

    if (xml.hasError())
        *error = xml.errorString();
    return entries;
}

void TestTranslations::everyStringIsTranslated_data()
{
    QTest::addColumn<QString>("context");
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("translation");
    QTest::addColumn<bool>("unfinished");

    QString error;
    const QList<Entry> entries = readEntries(tsPath(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // A file that parsed to nothing would let every row-driven check below pass
    // by never running, which is the rendering-probe trap in a different shape.
    QVERIFY2(entries.size() > 300,
             qPrintable(QStringLiteral("only %1 entries; the .ts looks truncated")
                            .arg(entries.size())));

    for (const Entry &entry : entries) {
        const QByteArray tag =
            (entry.context + QLatin1String(" :: ") + entry.source).toUtf8();
        QTest::newRow(tag.constData())
            << entry.context << entry.source << entry.translation
            << entry.unfinished;
    }
}

void TestTranslations::everyStringIsTranslated()
{
    QFETCH(QString, translation);
    QFETCH(bool, unfinished);

    // lrelease drops anything still flagged unfinished, so such a string ships
    // as English inside an otherwise Italian UI rather than failing the build.
    QVERIFY2(!unfinished, "still marked type=\"unfinished\"");
    QVERIFY2(!translation.trimmed().isEmpty(), "no translation");
}

void TestTranslations::theRuleBuilderFieldLabelsAreExtracted()
{
    QString error;
    const QList<Entry> entries = readEntries(tsPath(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QSet<QString> found;
    for (const Entry &entry : entries) {
        if (entry.context == QLatin1String("TagRulesDialog"))
            found.insert(entry.source);
    }

    // These eight sat in an anonymous namespace under QT_TR_NOOP, where lupdate
    // reports "tr() cannot be called without context" and extracts nothing,
    // while TagRulesDialog::tr() read them at runtime. Every one was
    // untranslatable and the source looked right. QT_TRANSLATE_NOOP, naming the
    // context explicitly, is what fixed it; Q_DECLARE_TR_FUNCTIONS on a
    // neighbouring class does NOT, measured at 0 extracted.
    //
    // The context asserted here must stay TagRulesDialog: it is what the
    // reading tr() resolves against, so a mismatch is untranslated at runtime
    // with a perfectly populated .ts.
    for (const QString &label : { QStringLiteral("From"), QStringLiteral("To"),
                                  QStringLiteral("Cc"), QStringLiteral("Subject"),
                                  QStringLiteral("Tag"), QStringLiteral("Folder"),
                                  QStringLiteral("Attachment"),
                                  QStringLiteral("Date") }) {
        QVERIFY2(found.contains(label),
                 qPrintable(QStringLiteral(
                     "TagRulesDialog/%1 is missing from the .ts: lupdate cannot "
                     "see it, so it can never be translated").arg(label)));
    }
}

void TestTranslations::theFileIsWellFormedAndItalian()
{
    QFile file(tsPath());
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(file.errorString()));

    QXmlStreamReader xml(&file);
    QString language;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("TS")) {
            language = xml.attributes().value(QLatin1String("language")).toString();
            break;
        }
    }
    QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));

    // QTranslator::load() derives the file from the locale, so the language
    // attribute is what pairs this file with LANG=it_IT.
    QCOMPARE(language, QStringLiteral("it_IT"));
}

QTEST_MAIN(TestTranslations)
#include "test_translations.moc"
