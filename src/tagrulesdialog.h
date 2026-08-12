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

#pragma once

#include <QDialog>

#include "tagrules.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

/// Views and edits the shared auto-tagging rules.
///
/// Does NOT apply rules to existing mail. The counts are shown so a rule can be
/// judged before it runs, but the only thing that tags mail is the notmuch
/// post-new hook. Backfill is deliberately out of this version: a rule that is
/// safe against arrivals is not automatically safe unscoped, and the
/// confirmation story for a bulk write does not exist yet.
class TagRulesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TagRulesDialog(QWidget *parent = nullptr);

    /// The queries whose message counts the dialog wants, in the order its
    /// rows appear. MainWindow hands these to the worker; the dialog never
    /// touches the database itself, because NotmuchWorker owns the only
    /// handle and notmuch permits one per process.
    QStringList countQueries() const;

signals:
    /// Asks the owner to run countQueries() through the worker.
    void countsRequested();

public slots:
    /// Corpus counts, positionally paired with countQueries().
    void setCounts(const QVector<int> &counts);

private slots:
    void onSelectionChanged();
    void onAddRule();
    void onCopyRule();
    void onDeleteRule();
    void applyEditsToCurrentRule();
    void onSave();

private:
    void reloadList();
    void showWarnings();
    int currentIndex() const;

    /// Writes one rule's summary onto its row. Shared by reloadList() and
    /// applyEditsToCurrentRule() so the two cannot render a rule differently.
    void fillItem(QTreeWidgetItem *item, const TagRule &rule) const;

    TagRules m_rules;
    QList<TagRule> m_working;   ///< Edited copy; written only on Save.

    /// True while reloadList() is repopulating the tree. The row it selects
    /// afterwards emits currentItemChanged, and onSelectionChanged() would
    /// then load a rule into the form while the form still holds the previous
    /// row's text, which applyEditsToCurrentRule() has no chance to flush.
    bool m_reloading = false;

    QTreeWidget *m_list = nullptr;
    QLineEdit *m_id = nullptr;
    QLineEdit *m_add = nullptr;
    QLineEdit *m_remove = nullptr;
    QLineEdit *m_query = nullptr;
    QPlainTextEdit *m_note = nullptr;
    QSpinBox *m_stage = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_warningLabel = nullptr;
    QPushButton *m_saveButton = nullptr;
};
