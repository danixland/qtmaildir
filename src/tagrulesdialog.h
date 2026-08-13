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
#include <QStringList>

#include "rulequery.h"
#include "tagrules.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

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

    /// Maildir folder paths, for the Folder row's dropdown, relative to the
    /// database root. Supplied by the caller rather than read here: they come
    /// from a scan of the tree under notmuch's database.path, which only
    /// NotmuchWorker can answer for, and this dialog reaches neither it nor
    /// Config.
    ///
    /// Arrives AFTER the dialog is on screen, since the scan crosses to the
    /// worker on a queued call, so this refills the rows that already exist.
    void setFolders(const QStringList &folders);

    /// Test seams. The builder's state is otherwise reachable only through
    /// synthetic clicks on widgets whose geometry the offscreen platform does
    /// not guarantee.
    int rowCountForTest() const { return m_rows.size(); }
    QString queryLineForTest() const;

    /// Selects the rule at `index` as a click on the list would.
    void selectRuleForTest(int index);

    bool textModeForTest() const;
    void setRowValueForTest(int index, const QString &value);
    /// Runs the Save path without showing the dialog.
    void saveForTest() { onSave(); }

    /// Types into the query field and toggles the mode, so the refusal path
    /// can be reached without a synthetic click. The refusal reports through
    /// the warning label rather than a modal precisely so this is testable.
    void setQueryTextForTest(const QString &text);
    void setTextModeForTest(bool on);
    QString warningTextForTest() const;

    /// Whether the text-mode toggle would be on screen. A toggle that hides
    /// itself when switched on is a one-way trip, and asserting only on the
    /// checked STATE passes against that, since the state is still readable
    /// when the widget is not.
    bool textModeToggleIsReachableForTest() const;

    /// Width of one rule-list column. The geometry restore is asserted on the
    /// saved and reread VALUE rather than on the resulting frame: item 46
    /// records that the offscreen platform does not honour a window resize,
    /// so a test comparing frames there passes or fails for reasons that have
    /// nothing to do with the code.
    int columnWidthForTest(int column) const;
    void setColumnWidthForTest(int column, int width);

    /// Repopulates the rule list, as adding or deleting a rule does. Exposed
    /// because a restored column width has to survive one of these, not only
    /// a close and reopen: `resizeColumnToContents` on every reload discarded
    /// the width the user had dragged.
    void reloadListForTest();

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

public slots:
    /// Saves the window's size and column widths on the way out. Overridden
    /// here rather than closeEvent because Save and Cancel do not send a
    /// close event at all, only the window manager's X button does.
    void done(int result) override;

private:
    void reloadList();
    void restoreUiState();
    void saveUiState();
    void showWarnings();
    int currentIndex() const;

    /// Writes one rule's summary onto its row. Shared by reloadList() and
    /// applyEditsToCurrentRule() so the two cannot render a rule differently.
    void fillItem(QTreeWidgetItem *item, const TagRule &rule) const;

    /// One builder row's widgets, so a row can be removed as a unit.
    struct Row
    {
        QWidget *container = nullptr;
        QComboBox *field = nullptr;
        QComboBox *op = nullptr;
        QLineEdit *value = nullptr;
        /// Shown in place of `value` for a Folder row. Never visible together.
        QComboBox *folder = nullptr;
    };

    /// Reads a row's value from whichever of its two widgets the field selects,
    /// so the read and write paths cannot disagree about where it lives.
    QString rowValue(const Row &row) const;
    void setRowValue(Row *row, const QString &value);

    Row *addRow(bool exclusion);
    void removeRow(bool exclusion, int index);
    void populateOperators(bool exclusion);
    void updateExclusionsVisibility();
    void syncQueryLine();

    void setTextMode(bool on);

    void rebuildRows(const RuleQuery &query);
    void applyTermToRow(Row *row, const RuleTerm &term);
    RuleQuery currentQueryFromRows() const;

    /// The query as parsed when the current rule was loaded. Task 10's save
    /// path compares against this to decide whether the stored string may be
    /// left alone.
    RuleQuery m_loadedQuery;

    QStringList m_folders;

    QList<Row> m_rows;
    QList<Row> m_exclusionRows;

    TagRules m_rules;
    QList<TagRule> m_working;   ///< Edited copy; written only on Save.

    /// True while reloadList() is repopulating the tree. The row it selects
    /// afterwards emits currentItemChanged, and onSelectionChanged() would
    /// then load a rule into the form while the form still holds the previous
    /// row's text, which applyEditsToCurrentRule() has no chance to flush.
    bool m_reloading = false;

    /// Each column is auto-sized to its contents ONCE, on its first fill.
    /// After that its width belongs to the user, whether it came from a
    /// restored header or from a drag, and resizeColumnToContents on every
    /// repopulate threw both away on the next add or delete. Two flags rather
    /// than one because the count column is filled later than the rest, by a
    /// reply from the worker.
    bool m_columnsSized = false;
    bool m_countColumnSized = false;

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

    QRadioButton *m_matchAll = nullptr;
    QRadioButton *m_matchAny = nullptr;
    QCheckBox *m_textMode = nullptr;
    QWidget *m_builder = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QVBoxLayout *m_exclusionsLayout = nullptr;
    QLabel *m_exclusionsHeader = nullptr;
    QPushButton *m_addExclusion = nullptr;
};
