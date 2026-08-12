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

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
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
    form->addRow(tr("Query"), m_query);
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
    const TagRule &rule = m_working.at(index);

    // The note's textChanged fires from setPlainText below, which would then
    // write the rule just loaded back over the rule now current. Harmless when
    // they are the same rule, destructive when the selection is what changed.
    const QSignalBlocker blockNote(m_note);

    m_id->setText(rule.id);
    m_stage->setValue(rule.stage);
    m_enabled->setChecked(rule.enabled);
    m_add->setText(rule.add.join(QStringLiteral(", ")));
    m_remove->setText(rule.remove.join(QStringLiteral(", ")));
    m_query->setText(rule.query);
    m_note->setPlainText(rule.note);
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
    rule.query = m_query->text().trimmed();
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
