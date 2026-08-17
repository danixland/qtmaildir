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
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "mainwindow.h"

namespace {

/// Columns of the rule list.
///
/// Item 102 added ColumnNote, and the user asked for it LAST, after Matches:
/// a note is prose and the widest thing here, so it belongs at the end where
/// it can run on without pushing the narrow columns off screen.
///
/// "Matches" is the counts column and is the one that is NOT in this enum: it
/// sits at index ColumnCount, appended past the end. That is why Note is
/// declared before ColumnCount and still draws after Matches, and why
/// ColumnNote must be given its index explicitly rather than left to follow
/// ColumnTags. Get this wrong and the counts land in the Note column, which
/// no test would call an error since both hold text.
enum Column { ColumnEnabled = 0, ColumnStage, ColumnId, ColumnTags,
              ColumnCount,          ///< "Matches", filled by the preview
              ColumnNote,           ///< last, per the user
              ColumnTotal };

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

// QT_TRANSLATE_NOOP, naming the context explicitly, rather than QT_TR_NOOP.
// These literals sit in an anonymous namespace, where lupdate reports "tr()
// cannot be called without context" and extracts NOTHING, while the use site
// below calls TagRulesDialog::tr() on them. The result compiles, reads
// correctly, and leaves all eight labels untranslatable: no .ts file ever
// contained them. Q_DECLARE_TR_FUNCTIONS on a neighbouring class does not fix
// it either (measured: still 0 extracted) because lupdate needs the context on
// the literal itself. The context named here must stay TagRulesDialog to match
// the tr() that reads it.
const FieldEntry kFields[] = {
    {RuleTerm::From,       QT_TRANSLATE_NOOP("TagRulesDialog", "From")},
    {RuleTerm::To,         QT_TRANSLATE_NOOP("TagRulesDialog", "To")},
    {RuleTerm::Cc,         QT_TRANSLATE_NOOP("TagRulesDialog", "Cc")},
    {RuleTerm::Subject,    QT_TRANSLATE_NOOP("TagRulesDialog", "Subject")},
    {RuleTerm::Tag,        QT_TRANSLATE_NOOP("TagRulesDialog", "Tag")},
    {RuleTerm::Folder,     QT_TRANSLATE_NOOP("TagRulesDialog", "Folder")},
    {RuleTerm::Attachment, QT_TRANSLATE_NOOP("TagRulesDialog", "Attachment")},
    {RuleTerm::Date,       QT_TRANSLATE_NOOP("TagRulesDialog", "Date")},
};

} // namespace

TagRulesDialog::TagRulesDialog(QWidget *parent)
    : TagRulesDialog(TagRule(), parent)
{
}

TagRulesDialog::TagRulesDialog(const TagRule &seed, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Tagging rules"));
    // The fallback for a first run. restoreUiState() overwrites it when a
    // size was saved, and is called at the end of this constructor because
    // the header state cannot be restored before the columns exist.
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

    m_list = new QTreeWidget(this);
    // ColumnTotal, not ColumnCount + 1: the counts column used to be the one
    // past the end, and since item 102 put Note after it there are two.
    m_list->setColumnCount(ColumnTotal);
    m_list->setHeaderLabels({ tr("On"), tr("Stage"), tr("Rule"), tr("Tags"),
                              tr("Matches"), tr("Note") });
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);

    // The list and the editor go in a splitter, and the editor's own widget
    // holds the form. A plain QVBoxLayout gave the list stretch 1 and still
    // let a rule with eight condition rows squeeze it to about one visible
    // row: a stretch factor only shares out space ABOVE each widget's
    // minimum, and the form's grew with every row. Measured before the fix,
    // the editor asked for 120px with one row and 414px with eight.
    auto *editor = new QWidget(this);
    auto *editorLayout = new QVBoxLayout(editor);
    editorLayout->setContentsMargins(0, 0, 0, 0);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(m_list);
    m_splitter->addWidget(editor);
    // Neither pane collapses to nothing by dragging the handle past the end,
    // which would hide the thing the user was trying to make room for.
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    layout->addWidget(m_splitter, 1);

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
    matchRow->addWidget(m_matchAll);
    matchRow->addWidget(m_matchAny);
    matchRow->addStretch();
    builderLayout->addLayout(matchRow);

    m_rowsLayout = new QVBoxLayout;
    builderLayout->addLayout(m_rowsLayout);

    m_exclusionsHeader = new QLabel(tr("But not"), m_builder);
    builderLayout->addWidget(m_exclusionsHeader);
    m_exclusionsLayout = new QVBoxLayout;
    builderLayout->addLayout(m_exclusionsLayout);

    m_addExclusion = new QPushButton(tr("Add e&xclusion"), m_builder);
    builderLayout->addWidget(m_addExclusion, 0, Qt::AlignLeft);

    // The rows scroll rather than growing without bound. The splitter alone
    // fixes the squeeze, but only until the user drags the handle down; this
    // caps what the editor can ever demand, so a rule with thirty senders
    // stays as workable as one with two.
    m_builderScroll = new QScrollArea(this);
    m_builderScroll->setWidget(m_builder);
    m_builderScroll->setWidgetResizable(true);
    m_builderScroll->setFrameShape(QFrame::NoFrame);
    m_builderScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Roughly four rows. Enough that the common rule needs no scrolling at
    // all, small enough that the list keeps most of the window.
    m_builderScroll->setMaximumHeight(190);
    form->addRow(tr("Match"), m_builderScroll);

    // The toggle sits with the QUERY line, not inside m_builder, because
    // switching to text mode HIDES m_builder. A checkbox parented there
    // vanishes with the rows it governs, leaving no way back except closing
    // the dialog, which is exactly what shipped in the first draft of this
    // builder. The query row is visible in both modes, so the toggle is
    // always reachable.
    auto *queryRow = new QHBoxLayout;
    m_textMode = new QCheckBox(tr("Edit as &text"), this);
    m_textMode->setToolTip(
        tr("Edit the notmuch query directly. A rule too complex to show as "
           "rows opens this way."));
    queryRow->addWidget(m_query, 1);
    queryRow->addWidget(m_textMode);
    form->addRow(tr("Query"), queryRow);

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
    editorLayout->addLayout(form);

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
    m_previewButton = new QPushButton(tr("&Preview in list"), this);
    m_previewButton->setToolTip(
        tr("Run this rule's query in the main window, to see which mail it "
           "collects. This does not tag anything."));
    buttons->addWidget(m_previewButton);
    buttons->addWidget(refreshButton);
    layout->addLayout(buttons);

    // Beside Save, not under the intro. It sat below the header in the same
    // font and colour as the prose around it, so it read as more explanatory
    // text: the user missed "1 rule could not be read and was skipped" on
    // every open of this dialog while hunting the rule it was telling them
    // about. Down here it is next to the button whose outcome it reports, and
    // it is the only red thing in the window.
    //
    // A palette role would be theme-correct and is not usable: the warning has
    // to stand out against BOTH a light and a dark desktop, and no role means
    // "alarming" in both. The colours are therefore literal, chosen to pass
    // contrast either way, which is the same reasoning the tag chips use.
    // The label and its dismiss button share a banner widget, so hiding the
    // warning takes the button with it. Visibility is the banner's; the label
    // itself stays visible inside it and setWarning() is still the one route.
    m_warningBanner = new QWidget(this);
    m_warningBanner->setVisible(false);
    m_warningBanner->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #b3261e; border-radius: 4px; }"));

    auto *warningRow = new QHBoxLayout(m_warningBanner);
    warningRow->setContentsMargins(8, 6, 6, 6);

    m_warningLabel = new QLabel(m_warningBanner);
    m_warningLabel->setWordWrap(true);
    // Plain text: these strings interpolate ids and queries read from the
    // file, and a query holding '<' would otherwise be eaten as markup.
    m_warningLabel->setTextFormat(Qt::PlainText);
    m_warningLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; font-weight: bold; background: transparent; }"));
    warningRow->addWidget(m_warningLabel, 1);

    m_warningClose = new QPushButton(QStringLiteral("✕"), m_warningBanner);
    m_warningClose->setToolTip(tr("Dismiss"));
    m_warningClose->setFlat(true);
    m_warningClose->setCursor(Qt::ArrowCursor);
    m_warningClose->setFixedSize(22, 22);
    // Focus would put a highlight ring on the banner and let Space dismiss a
    // warning the user is only tabbing past.
    m_warningClose->setFocusPolicy(Qt::NoFocus);
    m_warningClose->setStyleSheet(QStringLiteral(
        "QPushButton { color: #ffffff; background: transparent; border: none;"
        " font-weight: bold; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 60);"
        " border-radius: 11px; }"));
    warningRow->addWidget(m_warningClose, 0, Qt::AlignTop);

    // Dismissed for THIS appearance only, never persistently. The message it
    // most often carries is that the file on disk is not what the hook runs,
    // and a stored "do not show again" would re-hide exactly the problem that
    // went unnoticed for a whole session. The next warning shows it again,
    // including on the next open with the file still unrepaired.
    connect(m_warningClose, &QPushButton::clicked,
            this, [this] { setWarning(QString()); });

    layout->addWidget(m_warningBanner);

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
    connect(m_previewButton, &QPushButton::clicked,
            this, &TagRulesDialog::previewForTest);
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

    // Last, and after reloadList(): a header state cannot be applied before
    // the columns it describes exist, and reloadList is what fills them.
    restoreUiState();

    // After restoreUiState, so the seeded rule's selection is not overwritten
    // by anything the restore does to the list.
    if (!seed.query.isEmpty())
        seedRule(seed);
}

/// Reads the window size and the rule list's header layout back.
///
/// The same file MainWindow uses, under keys of its own. Machine-written
/// state, never the hand-edited config: a column width is not something
/// anyone edits by hand, and mixing the two puts a blob in a file the user
/// reads.
void TagRulesDialog::restoreUiState()
{
    QSettings state(MainWindow::uiStatePath(), QSettings::IniFormat);

    const QByteArray geometry =
        state.value(QStringLiteral("tagrules/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    const QByteArray splitter =
        state.value(QStringLiteral("tagrules/splitter")).toByteArray();
    if (!splitter.isEmpty())
        m_splitter->restoreState(splitter);

    const QByteArray header =
        state.value(QStringLiteral("tagrules/header")).toByteArray();
    // Only when the restore actually took. QHeaderView::restoreState REFUSES a
    // state saved with a different column count, returning false and leaving
    // the header untouched, which is exactly what every existing state file
    // does now that item 102 added a column. Setting the sized flags anyway
    // would spend the one auto-size each column gets on a restore that did
    // nothing, and the new Note column would open at whatever width it
    // defaulted to, once, for good.
    if (!header.isEmpty() && m_list->header()->restoreState(header)) {
        // Counts as the one auto-size each column gets, so the restore is not
        // immediately overwritten. reloadList() runs before this in the
        // constructor and has already sized the first two; the count column
        // has not been filled yet, and would otherwise resize over the
        // restored width as soon as the first counts arrived.
        m_columnsSized = true;
        m_countColumnSized = true;
    }
}

void TagRulesDialog::saveUiState()
{
    QDir().mkpath(QFileInfo(MainWindow::uiStatePath()).absolutePath());
    QSettings state(MainWindow::uiStatePath(), QSettings::IniFormat);
    state.setValue(QStringLiteral("tagrules/geometry"), saveGeometry());
    state.setValue(QStringLiteral("tagrules/header"),
                   m_list->header()->saveState());
    state.setValue(QStringLiteral("tagrules/splitter"),
                   m_splitter->saveState());
}

/// Saves on the way out, whichever way that is.
///
/// done() rather than closeEvent, and this distinction shipped broken: Cancel
/// calls reject() and Save calls accept(), and NEITHER sends a QCloseEvent.
/// Only the window manager's X button does. Saving from closeEvent therefore
/// kept the size for the one route the buttons never take, which is how a
/// resize followed by Cancel came back forgotten. Both buttons funnel through
/// done(), and QWidget::close() reaches it too.
///
/// On every route, not only on accept: the window's shape is not part of the
/// edit being confirmed, so Cancel should discard the rule changes and keep
/// the size.
void TagRulesDialog::done(int result)
{
    saveUiState();
    QDialog::done(result);
}

int TagRulesDialog::heightDemandedBelowListForTest() const
{
    // The builder's PREFERRED height, which is what grows with each condition
    // row and what the list ends up paying for. minimumSizeHint is the wrong
    // measure and reads 580 either way: a QFormLayout's minimum does not
    // track its rows, so a test on it passes against the bug.
    // The EDITOR PANE's minimum, which is what the splitter refuses to
    // shrink below and therefore what the rule list actually pays. Not the
    // builder's size hint: that grows with every row by design and is capped
    // by the scroll area rather than reduced. Not minimumSizeHint on the
    // dialog either, which does not track form rows at all and reads the
    // same whether the bug is present or not.
    return m_splitter->widget(1)->minimumSizeHint().height();
}

void TagRulesDialog::previewForTest()
{
    // Flush any half-typed edit first, so previewing shows what the rule
    // says NOW rather than what it said when the row was selected.
    applyEditsToCurrentRule();

    const int index = currentIndex();
    if (index < 0 || index >= m_working.size())
        return;

    const QString query = m_working.at(index).query;
    if (query.isEmpty())
        return;

    emit previewRequested(query);
}

int TagRulesDialog::conditionAreaHeightForTest() const
{
    // The CAP itself, not a qMin against the scroll area's own size hint: a
    // QScrollArea reports a small hint whether or not it is capped, and an
    // uncapped maximumHeight is QWIDGETSIZE_MAX, so qMin picked the hint and
    // the assertion passed with the cap removed.
    return m_builderScroll->maximumHeight();
}

int TagRulesDialog::columnWidthForTest(int column) const
{
    return m_list->columnWidth(column);
}

void TagRulesDialog::setColumnWidthForTest(int column, int width)
{
    m_list->setColumnWidth(column, width);
}

QString TagRulesDialog::listCellTextForTest(int row, int column) const
{
    const QTreeWidgetItem *item = m_list->topLevelItem(row);
    return item ? item->text(column) : QString();
}

QString TagRulesDialog::listCellToolTipForTest(int row, int column) const
{
    const QTreeWidgetItem *item = m_list->topLevelItem(row);
    return item ? item->toolTip(column) : QString();
}

QStringList TagRulesDialog::listHeaderLabelsForTest() const
{
    QStringList labels;
    for (int column = 0; column < m_list->columnCount(); ++column)
        labels.append(m_list->headerItem()->text(column));
    return labels;
}

void TagRulesDialog::reloadListForTest()
{
    reloadList();
}

void TagRulesDialog::setWarning(const QString &text)
{
    if (text.isEmpty()) {
        m_warningLabel->clear();
        m_warningBanner->setVisible(false);
        return;
    }
    // One route in, so the icon and the styling cannot drift apart between the
    // load path and the save refusal. The glyph is part of the string rather
    // than a second widget: it has to survive word wrap without leaving an
    // icon stranded beside an empty line.
    m_warningLabel->setText(QStringLiteral("⚠  ") + text);
    m_warningBanner->setVisible(true);
}

void TagRulesDialog::showWarnings()
{
    const QStringList warnings = m_rules.warnings();
    if (warnings.isEmpty()) {
        setWarning(QString());
        return;
    }
    // Not "skipped" any more: a rule with a bad name is loaded repaired, and
    // saying it was skipped would send the user looking for something that is
    // in front of them. What is true of every warning here is that the file on
    // disk is not yet what the hook will run.
    setWarning(tr("%n rule(s) in the file need attention: %1. Save to write "
                  "them back.", "", warnings.size())
                   .arg(warnings.join(QStringLiteral("; "))));
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

    // Item 102. The note explains why a rule is shaped the way it is, and was
    // reachable only by selecting the rule and reading the form. simplified()
    // because a note is free text and a newline in a tree cell truncates the
    // row at it; the full text stays in the tooltip and in the editor.
    const QString note = rule.note.simplified();
    item->setText(ColumnNote, note);
    item->setToolTip(ColumnNote, rule.note);
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
    // Auto-sized on the FIRST fill only. After that the widths belong to the
    // user, whether they came from a restored header or from a drag in this
    // session, and resizing on every repopulate threw both away on the next
    // add or delete.
    if (!m_columnsSized) {
        m_list->resizeColumnToContents(ColumnEnabled);
        m_list->resizeColumnToContents(ColumnStage);
        m_columnsSized = true;
    }
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
    m_builderScroll->setVisible(m_loadedQuery.parsed);
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

    // Sanitised as it is committed, not on save, so what the field shows is
    // what reaches the file. The field is labelled "Name" and a person types
    // "Justeat orders" into it; an id with a space is written happily and then
    // dropped by every reader, which is the defect this answers.
    QStringList taken;
    for (int other = 0; other < m_working.size(); ++other) {
        if (other != index)
            taken.append(m_working.at(other).id);
    }
    const QString typed = m_id->text().trimmed();
    rule.id = TagRules::isValidId(typed) ? typed
                                         : TagRules::uniqueId(typed, taken);
    if (rule.id != typed) {
        const QSignalBlocker blocker(m_id);
        m_id->setText(rule.id);
    }

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

void TagRulesDialog::seedRule(const TagRule &seed)
{
    // Flushed first, as onAddRule does: reloadList() repaints every row from
    // m_working, so an edit still sitting in the form would be lost.
    applyEditsToCurrentRule();

    TagRule rule = seed;

    // enabled and stage are TagRule's own defaults (true, 50), matching what
    // Add rule produces. An inconsistent default between two ways of making
    // the same thing is worse than either default.

    // Against the ids already present, not only against the file: the working
    // list may hold unsaved rules whose ids would collide just as hard.
    QStringList taken;
    for (const TagRule &existing : m_working)
        taken.append(existing.id);
    rule.id = TagRules::uniqueId(rule.id, taken);

    m_working.append(rule);
    reloadList();
    m_list->setCurrentItem(m_list->topLevelItem(m_working.size() - 1));

    // The one field the user must supply. A rule that tags nothing fails
    // validate(), so Save refuses it rather than writing a rule the hook
    // would ignore.
    m_add->setFocus();
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
    // Once, like the columns in reloadList. The counts arrive after the first
    // fill, so this column gets its own flag rather than sharing that one.
    if (!m_countColumnSized) {
        m_list->resizeColumnToContents(ColumnCount);
        m_countColumnSized = true;
    }
}

void TagRulesDialog::onSave()
{
    applyEditsToCurrentRule();

    // Validated against the same predicate load() uses. Writing a rule that
    // cannot be read back is what made a rule disappear: the file was correct,
    // every reader dropped it, and nothing said so at the point of the write.
    // Reported through the warning label rather than a modal, as the text-mode
    // refusal already is: a QMessageBox inside onSave() would hang the suite,
    // which drives this path directly through saveForTest().
    const QStringList problems = TagRules::validate(m_working);
    if (!problems.isEmpty()) {
        setWarning(tr("%n rule(s) cannot be saved as they are: %1", "",
                      problems.size())
                       .arg(problems.join(QStringLiteral("; "))));
        return;
    }

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

void TagRulesDialog::setNameForTest(const QString &name)
{
    m_id->setText(name);
    // editingFinished is what leaving the field emits, and it is where the
    // sanitiser hangs. setText() alone does not emit it.
    emit m_id->editingFinished();
}

QString TagRulesDialog::nameLineForTest() const
{
    return m_id->text();
}

QString TagRulesDialog::warningStyleForTest() const
{
    // Both: the fill is the banner's and the text colour is the label's, so
    // reading only one of them would miss half the styling.
    return m_warningBanner->styleSheet() + m_warningLabel->styleSheet();
}

void TagRulesDialog::dismissWarningForTest()
{
    m_warningClose->click();
}

Qt::TextFormat TagRulesDialog::warningTextFormatForTest() const
{
    return m_warningLabel->textFormat();
}

bool TagRulesDialog::warningIsBelowTheRuleListForTest() const
{
    // By layout position rather than by coordinates: the offscreen platform
    // does not lay a dialog out the way a real one is, so a y() comparison
    // would assert about the platform. indexOf() on the shared parent layout
    // is exact and true in both.
    auto *parent = qobject_cast<QVBoxLayout *>(layout());
    if (!parent)
        return false;
    return parent->indexOf(m_warningBanner) > parent->indexOf(m_splitter);
}

int TagRulesDialog::ruleCountForTest() const
{
    return m_working.size();
}

bool TagRulesDialog::currentRuleEnabledForTest() const
{
    return m_enabled->isChecked();
}

void TagRulesDialog::setTagsForTest(const QString &tags)
{
    m_add->setText(tags);
    emit m_add->editingFinished();
}

bool TagRulesDialog::textModeToggleIsReachableForTest() const
{
    // isVisibleTo rather than isVisible: nothing is isVisible() on a dialog
    // that was never shown, so that would report unreachable in both the
    // working and the broken case.
    return m_textMode->isVisibleTo(this);
}

QString TagRulesDialog::warningTextForTest() const
{
    // isVisible() is false for every child of a dialog that was never shown,
    // so it would report no warning whatever the label held. isVisibleTo()
    // answers the question actually being asked: would this be on screen if
    // the dialog were.
    // The BANNER carries the visibility now: the label stays visible inside it
    // and would report a dismissed warning as still showing.
    return m_warningBanner->isVisibleTo(this) ? m_warningLabel->text()
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
        m_builderScroll->setVisible(false);
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
        setWarning(
            tr("This query is more than the builder can show, so it stays as "
               "text. It is still saved and applied normally."));
        return;
    }

    const bool wasReloading = m_reloading;
    m_reloading = true;
    rebuildRows(parsed);
    m_reloading = wasReloading;

    m_loadedQuery = parsed;
    m_builderScroll->setVisible(true);
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
