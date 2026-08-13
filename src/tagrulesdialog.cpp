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

#include "tagrulesdialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

/// Columns of the rule list.
enum Column { ColumnEnabled = 0, ColumnStage, ColumnId, ColumnTags,
              ColumnCount };

QStringList splitTags(const QString &text)
{
    QStringList out;
    const QStringList parts = text.split(QLatin1Char(','));
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

struct FieldEntry { RuleTerm::Field field; const char *label; };

const FieldEntry kFields[] = {
    {RuleTerm::From,       QT_TR_NOOP("From")},
    {RuleTerm::To,         QT_TR_NOOP("To")},
    {RuleTerm::Cc,         QT_TR_NOOP("Cc")},
    {RuleTerm::Subject,    QT_TR_NOOP("Subject")},
    {RuleTerm::Tag,        QT_TR_NOOP("Tag")},
    {RuleTerm::Folder,     QT_TR_NOOP("Folder")},
    {RuleTerm::Attachment, QT_TR_NOOP("Attachment")},
    {RuleTerm::Date,       QT_TR_NOOP("Date")},
};

} // namespace

TagRulesDialog::TagRulesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tagging rules"));
    resize(760, 520);

    m_rules.load();
    m_working = m_rules.rules();

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Rules tag mail as it arrives, applied by the notmuch post-new "
           "hook. Editing them here changes what the next sync does; it does "
           "not retag mail you already have."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setVisible(false);
    layout->addWidget(m_warningLabel);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(ColumnCount + 1);
    m_list->setHeaderLabels({ tr("On"), tr("Stage"), tr("Rule"), tr("Tags"),
                              tr("Matches") });
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    layout->addWidget(m_list, 1);

    auto *form = new QFormLayout;
    m_id = new QLineEdit(this);
    m_stage = new QSpinBox(this);
    m_stage->setRange(0, 999);
    m_enabled = new QCheckBox(tr("Applied on every sync"), this);
    m_add = new QLineEdit(this);
    m_add->setPlaceholderText(tr("comma separated"));
    m_remove = new QLineEdit(this);
    m_remove->setPlaceholderText(tr("comma separated"));
    m_query = new QLineEdit(this);
    // Query syntax is wire format, never translated. Only the prose is.
    m_query->setPlaceholderText(QStringLiteral("from:sender@example.com"));
    m_note = new QPlainTextEdit(this);
    m_note->setMaximumHeight(70);

    form->addRow(tr("Name"), m_id);
    form->addRow(tr("Stage"), m_stage);
    form->addRow(QString(), m_enabled);
    form->addRow(tr("Add tags"), m_add);
    form->addRow(tr("Remove tags"), m_remove);
    m_builder = new QWidget(this);
    auto *builderLayout = new QVBoxLayout(m_builder);
    builderLayout->setContentsMargins(0, 0, 0, 0);

    auto *matchRow = new QHBoxLayout;
    m_matchAll = new QRadioButton(tr("Match &all"), m_builder);
    m_matchAny = new QRadioButton(tr("Match a&ny"), m_builder);
    m_matchAll->setChecked(true);
    auto *matchGroup = new QButtonGroup(this);
    matchGroup->addButton(m_matchAll);
    matchGroup->addButton(m_matchAny);
    m_textMode = new QCheckBox(tr("Edit as &text"), m_builder);
    m_textMode->setToolTip(
        tr("Edit the notmuch query directly. A rule too complex to show as "
           "rows opens this way."));
    matchRow->addWidget(m_matchAll);
    matchRow->addWidget(m_matchAny);
    matchRow->addStretch();
    matchRow->addWidget(m_textMode);
    builderLayout->addLayout(matchRow);

    m_rowsLayout = new QVBoxLayout;
    builderLayout->addLayout(m_rowsLayout);

    m_exclusionsHeader = new QLabel(tr("But not"), m_builder);
    builderLayout->addWidget(m_exclusionsHeader);
    m_exclusionsLayout = new QVBoxLayout;
    builderLayout->addLayout(m_exclusionsLayout);

    m_addExclusion = new QPushButton(tr("Add e&xclusion"), m_builder);
    builderLayout->addWidget(m_addExclusion, 0, Qt::AlignLeft);

    form->addRow(tr("Match"), m_builder);
    form->addRow(tr("Query"), m_query);

    // The query line shows what the rows compile to. Read-only in builder
    // mode: it is what actually ships to the hook, and watching it change is
    // what makes the rows trustworthy.
    m_query->setReadOnly(true);

    connect(m_matchAll, &QRadioButton::toggled,
            this, &TagRulesDialog::syncQueryLine);
    connect(m_textMode, &QCheckBox::toggled,
            this, &TagRulesDialog::setTextMode);
    connect(m_addExclusion, &QPushButton::clicked, this, [this] {
        addRow(true);
        syncQueryLine();
    });

    form->addRow(tr("Note"), m_note);
    layout->addLayout(form);

    auto *buttons = new QHBoxLayout;
    auto *addButton = new QPushButton(tr("&New"), this);
    auto *copyButton = new QPushButton(tr("&Copy"), this);
    auto *deleteButton = new QPushButton(tr("&Delete"), this);
    auto *refreshButton = new QPushButton(tr("Count &matches"), this);
    refreshButton->setToolTip(
        tr("Count the messages each rule's query matches across all mail. "
           "This does not tag anything."));
    buttons->addWidget(addButton);
    buttons->addWidget(copyButton);
    buttons->addWidget(deleteButton);
    buttons->addStretch();
    buttons->addWidget(refreshButton);
    layout->addLayout(buttons);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Save
                                         | QDialogButtonBox::Cancel,
                                     this);
    m_saveButton = box->button(QDialogButtonBox::Save);
    layout->addWidget(box);

    connect(m_list, &QTreeWidget::currentItemChanged,
            this, &TagRulesDialog::onSelectionChanged);
    connect(addButton, &QPushButton::clicked,
            this, &TagRulesDialog::onAddRule);
    connect(copyButton, &QPushButton::clicked,
            this, &TagRulesDialog::onCopyRule);
    connect(deleteButton, &QPushButton::clicked,
            this, &TagRulesDialog::onDeleteRule);
    connect(refreshButton, &QPushButton::clicked,
            this, &TagRulesDialog::countsRequested);
    connect(box, &QDialogButtonBox::accepted,
            this, &TagRulesDialog::onSave);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Edits land on the working copy as they are made, so switching rows does
    // not silently discard what was typed.
    connect(m_id, &QLineEdit::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_add, &QLineEdit::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_remove, &QLineEdit::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_query, &QLineEdit::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_stage, &QSpinBox::editingFinished,
            this, &TagRulesDialog::applyEditsToCurrentRule);
    connect(m_enabled, &QCheckBox::toggled,
            this, &TagRulesDialog::applyEditsToCurrentRule);

    // QPlainTextEdit has no editingFinished: it is a multi-line editor, where
    // Return inserts a newline rather than committing. Without this the note
    // would reach the working copy only for whichever row happened to be
    // current when Save was pressed, so a note typed on one rule and then
    // followed by a click on another would be silently discarded.
    connect(m_note, &QPlainTextEdit::textChanged,
            this, &TagRulesDialog::applyEditsToCurrentRule);

    addRow(false);
    updateExclusionsVisibility();

    reloadList();
    showWarnings();
}

void TagRulesDialog::showWarnings()
{
    const QStringList warnings = m_rules.warnings();
    if (warnings.isEmpty()) {
        m_warningLabel->setVisible(false);
        return;
    }
    m_warningLabel->setText(
        tr("%n rule(s) could not be read and were skipped: %1", "",
           warnings.size()).arg(warnings.join(QStringLiteral("; "))));
    m_warningLabel->setVisible(true);
}

void TagRulesDialog::fillItem(QTreeWidgetItem *item, const TagRule &rule) const
{
    item->setCheckState(ColumnEnabled,
                        rule.enabled ? Qt::Checked : Qt::Unchecked);
    item->setText(ColumnStage, QString::number(rule.stage));
    item->setText(ColumnId, rule.id);

    QStringList tags;
    for (const QString &tag : rule.add)
        tags.append(QStringLiteral("+") + tag);
    for (const QString &tag : rule.remove)
        tags.append(QStringLiteral("-") + tag);
    item->setText(ColumnTags, tags.join(QStringLiteral(" ")));
}

void TagRulesDialog::reloadList()
{
    // Guards the setCurrentItem below. Selecting a row emits
    // currentItemChanged, and onSelectionChanged() would load that rule into
    // the form; the counts column is also rebuilt from nothing here, so any
    // count already shown is dropped rather than left against the wrong row.
    m_reloading = true;
    m_list->clear();
    for (const TagRule &rule : m_working) {
        auto *item = new QTreeWidgetItem(m_list);
        fillItem(item, rule);
    }
    m_list->resizeColumnToContents(ColumnEnabled);
    m_list->resizeColumnToContents(ColumnStage);
    m_reloading = false;

    if (!m_working.isEmpty())
        m_list->setCurrentItem(m_list->topLevelItem(0));
}

int TagRulesDialog::currentIndex() const
{
    return m_list->currentItem()
               ? m_list->indexOfTopLevelItem(m_list->currentItem())
               : -1;
}

void TagRulesDialog::onSelectionChanged()
{
    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;
    // By value: every setter below can reach applyEditsToCurrentRule(), which
    // writes into m_working, and a reference into that list would then be read
    // back half overwritten.
    const TagRule rule = m_working.at(index);

    // Populating the form emits change signals whose handlers write the form
    // back onto the working copy, so the rule just loaded would be written over
    // whichever rule is now current. Harmless when they are the same rule,
    // destructive when the selection is what changed. m_enabled::toggled and
    // the note's textChanged both do this, and the widgets are populated in an
    // order where m_enabled fires while m_query still holds the PREVIOUS rule's
    // text, which emptied the query of the first rule opened. The reloading
    // flag is the existing guard for exactly this, so it covers the whole load,
    // including rebuildRows: populating combo boxes and line edits fires
    // currentIndexChanged, which runs syncQueryLine.
    const bool wasReloading = m_reloading;
    m_reloading = true;

    m_id->setText(rule.id);
    m_stage->setValue(rule.stage);
    m_enabled->setChecked(rule.enabled);
    m_add->setText(rule.add.join(QStringLiteral(", ")));
    m_remove->setText(rule.remove.join(QStringLiteral(", ")));
    m_query->setText(rule.query);
    m_note->setPlainText(rule.note);

    // Parse once, on load, and keep it: the save path compares against this to
    // decide whether the stored string may be left alone, so that opening a
    // rule and closing it cannot rewrite the file mailctl also reads.
    m_loadedQuery = RuleQuery::parse(rule.query);

    // A rule the builder cannot show opens as text, and one it can show
    // returns to the builder. Blocked, because letting setChecked run
    // setTextMode() here would recompile and overwrite m_query mid-load.
    {
        const QSignalBlocker blockTextMode(m_textMode);
        m_textMode->setChecked(!m_loadedQuery.parsed);
    }
    m_builder->setVisible(m_loadedQuery.parsed);
    m_query->setReadOnly(m_loadedQuery.parsed);

    if (m_loadedQuery.parsed)
        rebuildRows(m_loadedQuery);

    m_reloading = wasReloading;
}

void TagRulesDialog::applyEditsToCurrentRule()
{
    if (m_reloading)
        return;

    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;

    TagRule &rule = m_working[index];
    rule.id = m_id->text().trimmed();
    rule.stage = m_stage->value();
    rule.enabled = m_enabled->isChecked();
    rule.add = splitTags(m_add->text());
    rule.remove = splitTags(m_remove->text());
    if (m_textMode->isChecked()) {
        rule.query = m_query->text().trimmed();
    } else {
        const RuleQuery current = currentQueryFromRows();
        // Unchanged rows mean the stored string is left exactly as it was
        // read. Recompiling an untouched rule would churn a file the
        // companion tool also reads, showing a diff the user never made.
        if (!(current == m_loadedQuery))
            rule.query = current.compile();
    }
    rule.note = m_note->toPlainText();

    fillItem(m_list->topLevelItem(index), rule);
}

void TagRulesDialog::onAddRule()
{
    // Flushed first: reloadList() rebuilds every row from m_working, so an
    // edit still sitting in the form would be painted away by its own reload.
    applyEditsToCurrentRule();

    TagRule rule;
    rule.id = QStringLiteral("new-rule");
    rule.query = QStringLiteral("from:sender@example.com");
    rule.add = { QStringLiteral("tag") };
    m_working.append(rule);
    reloadList();
    m_list->setCurrentItem(m_list->topLevelItem(m_working.size() - 1));
}

void TagRulesDialog::onCopyRule()
{
    applyEditsToCurrentRule();

    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;
    TagRule copy = m_working.at(index);
    copy.id += QStringLiteral("-copy");
    m_working.insert(index + 1, copy);
    reloadList();
    m_list->setCurrentItem(m_list->topLevelItem(index + 1));
}

void TagRulesDialog::onDeleteRule()
{
    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;
    m_working.removeAt(index);
    reloadList();
}

QStringList TagRulesDialog::countQueries() const
{
    QStringList queries;
    for (const TagRule &rule : m_working)
        queries.append(rule.query);
    return queries;
}

void TagRulesDialog::setCounts(const QVector<int> &counts)
{
    for (int i = 0; i < counts.size() && i < m_list->topLevelItemCount(); ++i) {
        // -1 is the worker's "this query could not be run", which for a rule
        // means the query is malformed. Saying so beats showing a number.
        m_list->topLevelItem(i)->setText(
            ColumnCount,
            counts.at(i) < 0 ? tr("invalid")
                             : QString::number(counts.at(i)));
    }
    m_list->resizeColumnToContents(ColumnCount);
}

void TagRulesDialog::onSave()
{
    applyEditsToCurrentRule();

    m_rules.setRules(m_working);
    if (!m_rules.save()) {
        QMessageBox::warning(this, tr("Tagging rules"),
                             tr("Could not write %1.")
                                 .arg(TagRules::defaultPath()));
        return;
    }
    accept();
}

QString TagRulesDialog::queryLineForTest() const
{
    return m_query->text();
}

bool TagRulesDialog::textModeForTest() const
{
    return m_textMode->isChecked();
}

void TagRulesDialog::setRowValueForTest(int index, const QString &value)
{
    if (index < 0 || index >= m_rows.size())
        return;
    setRowValue(&m_rows[index], value);
    syncQueryLine();
}

void TagRulesDialog::setQueryTextForTest(const QString &text)
{
    m_query->setText(text);
}

void TagRulesDialog::setTextModeForTest(bool on)
{
    m_textMode->setChecked(on);
}

QString TagRulesDialog::warningTextForTest() const
{
    // isVisible() is false for every child of a dialog that was never shown,
    // so it would report no warning whatever the label held. isVisibleTo()
    // answers the question actually being asked: would this be on screen if
    // the dialog were.
    return m_warningLabel->isVisibleTo(this) ? m_warningLabel->text()
                                             : QString();
}

void TagRulesDialog::selectRuleForTest(int index)
{
    if (index >= 0 && index < m_list->topLevelItemCount())
        m_list->setCurrentItem(m_list->topLevelItem(index));
}

QString TagRulesDialog::rowValue(const Row &row) const
{
    const bool isFolder =
        RuleTerm::Field(row.field->currentData().toInt()) == RuleTerm::Folder;
    return (isFolder ? row.folder->currentText() : row.value->text()).trimmed();
}

void TagRulesDialog::setRowValue(Row *row, const QString &value)
{
    if (RuleTerm::Field(row->field->currentData().toInt()) == RuleTerm::Folder)
        row->folder->setCurrentText(value);
    else
        row->value->setText(value);
}

void TagRulesDialog::setFolders(const QStringList &folders)
{
    m_folders = folders;

    // Rows already exist by the time this is called: the constructor loads the
    // first rule and builds its rows before the caller can hand the list over.
    // Repopulating them here rather than only in addRow() is what stops the row
    // on screen from opening with an empty dropdown. The current text is
    // preserved across the refill, since the combo is editable and may hold a
    // folder the config does not list.
    const auto refill = [&folders](const QList<Row> &rows) {
        for (const Row &row : rows) {
            const QString had = row.folder->currentText();
            QSignalBlocker block(row.folder);
            row.folder->clear();
            row.folder->addItems(folders);
            row.folder->setCurrentText(had);
        }
    };
    refill(m_rows);
    refill(m_exclusionRows);
}

TagRulesDialog::Row *TagRulesDialog::addRow(bool exclusion)
{
    Row row;
    row.container = new QWidget(m_builder);
    auto *layout = new QHBoxLayout(row.container);
    layout->setContentsMargins(0, 0, 0, 0);

    row.field = new QComboBox(row.container);
    for (const FieldEntry &entry : kFields)
        row.field->addItem(tr(entry.label), int(entry.field));

    row.op = new QComboBox(row.container);
    row.value = new QLineEdit(row.container);

    row.folder = new QComboBox(row.container);
    // Editable so a folder present in the file but absent from the config
    // still displays and still saves, rather than being silently blanked.
    row.folder->setEditable(true);
    row.folder->addItems(m_folders);
    row.folder->setVisible(false);

    auto *plus = new QPushButton(QStringLiteral("+"), row.container);
    auto *minus = new QPushButton(QStringLiteral("-"), row.container);
    plus->setFixedWidth(30);
    minus->setFixedWidth(30);

    layout->addWidget(row.field);
    layout->addWidget(row.op);
    layout->addWidget(row.value, 1);
    layout->addWidget(row.folder, 1);
    layout->addWidget(plus);
    layout->addWidget(minus);

    QList<Row> &rows = exclusion ? m_exclusionRows : m_rows;
    QVBoxLayout *target = exclusion ? m_exclusionsLayout : m_rowsLayout;
    rows.append(row);
    target->addWidget(row.container);

    // The three widgets by pointer, never the Row by value: the row lives in a
    // QList that reallocates as rows are added, so a copy taken here would be
    // compared against, or written through, after that list has moved.
    QComboBox *field = row.field;
    QLineEdit *value = row.value;
    QComboBox *folder = row.folder;
    connect(row.field, &QComboBox::currentIndexChanged, this,
            [this, exclusion, field, value, folder](int) {
                populateOperators(exclusion);
                const bool isFolder =
                    RuleTerm::Field(field->currentData().toInt())
                    == RuleTerm::Folder;
                value->setVisible(!isFolder);
                folder->setVisible(isFolder);
                syncQueryLine();
            });
    connect(row.op, &QComboBox::currentIndexChanged,
            this, [this](int) { syncQueryLine(); });
    connect(row.value, &QLineEdit::textEdited,
            this, [this](const QString &) { syncQueryLine(); });
    connect(row.folder, &QComboBox::currentTextChanged,
            this, [this](const QString &) { syncQueryLine(); });
    connect(plus, &QPushButton::clicked, this, [this, exclusion] {
        addRow(exclusion);
        syncQueryLine();
    });

    QWidget *container = row.container;
    connect(minus, &QPushButton::clicked, this, [this, exclusion, container] {
        const QList<Row> &list = exclusion ? m_exclusionRows : m_rows;
        for (int i = 0; i < list.size(); ++i) {
            if (list.at(i).container == container) {
                removeRow(exclusion, i);
                break;
            }
        }
        syncQueryLine();
    });

    populateOperators(exclusion);
    updateExclusionsVisibility();
    return &rows.last();
}

void TagRulesDialog::removeRow(bool exclusion, int index)
{
    QList<Row> &rows = exclusion ? m_exclusionRows : m_rows;
    if (index < 0 || index >= rows.size())
        return;

    // The positive section keeps at least one row: a rule with no rows has an
    // empty query, which is reachable by clearing the value rather than by
    // deleting the last row out from under the user.
    if (!exclusion && rows.size() == 1) {
        setRowValue(&rows[0], QString());
        return;
    }

    delete rows.at(index).container;
    rows.removeAt(index);
    updateExclusionsVisibility();
}

void TagRulesDialog::updateExclusionsVisibility()
{
    // Most rules have no exclusions, so an empty block on every rule is noise.
    const bool any = !m_exclusionRows.isEmpty();
    m_exclusionsHeader->setVisible(any);
}

void TagRulesDialog::populateOperators(bool exclusion)
{
    const QList<Row> &rows = exclusion ? m_exclusionRows : m_rows;
    for (const Row &row : rows) {
        const auto field = RuleTerm::Field(row.field->currentData().toInt());
        const QString had = row.op->currentText();
        QSignalBlocker block(row.op);
        row.op->clear();

        switch (field) {
        case RuleTerm::Tag:
        case RuleTerm::Folder:
            row.op->addItem(tr("is"), int(RuleTerm::Is));
            row.op->addItem(tr("is not"), int(RuleTerm::IsNot));
            break;
        case RuleTerm::Attachment:
            row.op->addItem(tr("has"), int(RuleTerm::Has));
            row.op->addItem(tr("has not"), int(RuleTerm::HasNot));
            break;
        case RuleTerm::Date:
            row.op->addItem(tr("before"), int(RuleTerm::Before));
            row.op->addItem(tr("after"), int(RuleTerm::After));
            break;
        default:
            row.op->addItem(tr("contains"), int(RuleTerm::Contains));
            row.op->addItem(tr("contains not"), int(RuleTerm::ContainsNot));
            row.op->addItem(tr("is"), int(RuleTerm::Is));
            row.op->addItem(tr("is not"), int(RuleTerm::IsNot));
            break;
        }

        const int restored = row.op->findText(had);
        if (restored >= 0)
            row.op->setCurrentIndex(restored);
    }
}

RuleQuery TagRulesDialog::currentQueryFromRows() const
{
    RuleQuery query;
    query.parsed = true;
    query.join = m_matchAny->isChecked() ? RuleQuery::Any : RuleQuery::All;

    for (const Row &row : m_rows) {
        const QString value = rowValue(row);
        if (value.isEmpty())
            continue;
        query.terms.append({RuleTerm::Field(row.field->currentData().toInt()),
                            RuleTerm::Op(row.op->currentData().toInt()),
                            value});
    }
    for (const Row &row : m_exclusionRows) {
        const QString value = rowValue(row);
        if (value.isEmpty())
            continue;
        query.exclusions.append(
            {RuleTerm::Field(row.field->currentData().toInt()),
             RuleTerm::Op(row.op->currentData().toInt()),
             value});
    }
    return query;
}

void TagRulesDialog::setTextMode(bool on)
{
    if (on) {
        // Show what the rows currently mean, then hand the string over.
        if (m_loadedQuery.parsed)
            m_query->setText(currentQueryFromRows().compile());
        m_builder->setVisible(false);
        m_query->setReadOnly(false);
        return;
    }

    // Going back needs the typed query to be representable. If it is not, the
    // checkbox cannot clear: there are no rows that mean this query.
    //
    // Said in the warning label rather than a modal. A modal here would block
    // any test that reaches this branch, which is how a refusal path ends up
    // shipping unverified, and it interrupts someone who is mid-edit to tell
    // them something the label can hold while they keep typing.
    const RuleQuery parsed = RuleQuery::parse(m_query->text().trimmed());
    if (!parsed.parsed) {
        const QSignalBlocker block(m_textMode);
        m_textMode->setChecked(true);
        m_warningLabel->setText(
            tr("This query is more than the builder can show, so it stays as "
               "text. It is still saved and applied normally."));
        m_warningLabel->setVisible(true);
        return;
    }

    const bool wasReloading = m_reloading;
    m_reloading = true;
    rebuildRows(parsed);
    m_reloading = wasReloading;

    m_loadedQuery = parsed;
    m_builder->setVisible(true);
    m_query->setReadOnly(true);

    // The refusal above writes into the same label the load warnings use, so
    // a successful return to the rows must clear it or a stale complaint
    // outlives the query that caused it. showWarnings() restores whatever the
    // file itself had to say.
    showWarnings();
}

void TagRulesDialog::rebuildRows(const RuleQuery &query)
{
    while (!m_rows.isEmpty())
        delete m_rows.takeLast().container;
    while (!m_exclusionRows.isEmpty())
        delete m_exclusionRows.takeLast().container;

    m_matchAll->setChecked(query.join == RuleQuery::All);
    m_matchAny->setChecked(query.join == RuleQuery::Any);

    for (const RuleTerm &term : query.terms)
        applyTermToRow(addRow(false), term);
    for (const RuleTerm &term : query.exclusions)
        applyTermToRow(addRow(true), term);

    if (m_rows.isEmpty())
        addRow(false);   // Always one row to type into.

    updateExclusionsVisibility();
}

void TagRulesDialog::applyTermToRow(Row *row, const RuleTerm &term)
{
    const QSignalBlocker blockField(row->field);
    const QSignalBlocker blockOp(row->op);
    const QSignalBlocker blockValue(row->value);
    const QSignalBlocker blockFolder(row->folder);

    const int fieldIndex = row->field->findData(int(term.field));
    if (fieldIndex >= 0)
        row->field->setCurrentIndex(fieldIndex);

    // The field's own handler is blocked here, so the swap it would have done
    // has to happen explicitly or a Folder row opens showing the line edit.
    const bool isFolder = term.field == RuleTerm::Folder;
    row->value->setVisible(!isFolder);
    row->folder->setVisible(isFolder);

    // The operator list depends on the field just set, so it must be rebuilt
    // before the operator can be found in it.
    populateOperators(false);
    populateOperators(true);

    const int opIndex = row->op->findData(int(term.op));
    if (opIndex >= 0)
        row->op->setCurrentIndex(opIndex);

    setRowValue(row, term.value);
}

void TagRulesDialog::syncQueryLine()
{
    if (m_reloading)
        return;
    m_query->setText(currentQueryFromRows().compile());
    applyEditsToCurrentRule();
}
