#include "SpellingReviewDialog.h"

#include <qceditor/Settings.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace qceditor {

namespace {

enum Column { WordColumn, CorrectionColumn, LineColumn, ContextColumn };

// Whether every word is checked at first, not only those with a single correction.
const char* kCheckAllKey = "spelling/reviewChecksEveryWord";

// The line around the word, shortened at both ends.
QString context(const QString& line, int start, int end)
{
    constexpr int kAround = 30;
    const int from = qMax(0, start - kAround);
    const int to = qMin(int(line.size()), end + kAround);
    QString text = line.mid(from, to - from).simplified();
    if (from > 0) text.prepend(QStringLiteral("…"));
    if (to < line.size()) text.append(QStringLiteral("…"));
    return text;
}

} // namespace

SpellingReviewDialog::SpellingReviewDialog(const QList<SpellingReviewItem>& items, bool inSelection,
                                           QWidget* parent)
    : QDialog(parent), m_items(items)
{
    setWindowTitle(tr("Review Spelling"));
    resize(760, 480);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(inSelection ? tr("Misspelled words in the selection. The checked ones are corrected.")
                                         : tr("Misspelled words in the text. The checked ones are corrected."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("Checked at first:"), this));
    m_checkAtFirst = new QComboBox(this);
    m_checkAtFirst->addItem(tr("Sure corrections only"));
    m_checkAtFirst->addItem(tr("Every word, with its first correction"));
    m_checkAtFirst->setCurrentIndex(settings().value(kCheckAllKey, false).toBool() ? 1 : 0);
    options->addWidget(m_checkAtFirst);
    options->addStretch();
    m_onlyChecked = new QCheckBox(tr("Show &only checked words"), this);
    m_onlyChecked->setToolTip(tr("Hides the unchecked words, to look over the corrections that will be made"));
    options->addWidget(m_onlyChecked);
    layout->addLayout(options);

    m_list = new QTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setHeaderLabels({tr("Word"), tr("Correction"), tr("Line"), tr("Context")});
    m_list->header()->setStretchLastSection(true);
    for (int i = 0; i < m_items.size(); ++i) {
        const SpellingReviewItem& item = m_items[i];
        auto* row = new QTreeWidgetItem(m_list);
        row->setText(WordColumn, item.word.word);
        row->setText(LineColumn, QString::number(item.word.start.line + 1));
        row->setTextAlignment(LineColumn, Qt::AlignRight | Qt::AlignVCenter);
        row->setText(ContextColumn, context(item.lineText, item.word.start.column, item.word.end.column));
        row->setToolTip(ContextColumn, item.lineText);
        row->setData(WordColumn, Qt::UserRole, i);
        if (item.choices.isEmpty()) {
            row->setText(CorrectionColumn, tr("No suggestions"));
            row->setFlags(row->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable));
            continue;
        }
        row->setCheckState(WordColumn, Qt::Unchecked);
        auto* choice = new QComboBox(m_list);
        for (const auto& correction : item.choices) choice->addItem(correction.label);
        // Picking a correction means correcting the word.
        connect(choice, &QComboBox::activated, this, [row]() { row->setCheckState(WordColumn, Qt::Checked); });
        m_list->setItemWidget(row, CorrectionColumn, choice);
    }
    m_list->resizeColumnToContents(WordColumn);
    m_list->setColumnWidth(CorrectionColumn, qMax(160, m_list->columnWidth(WordColumn) + 40));
    m_list->resizeColumnToContents(LineColumn);
    layout->addWidget(m_list, 1);
    checkAtFirst();
    connect(m_list, &QTreeWidget::itemChanged, this, &SpellingReviewDialog::updateSummary);
    connect(m_checkAtFirst, &QComboBox::activated, this, [this](int index) {
        settings().setValue(kCheckAllKey, index == 1);
        checkAtFirst();
        filter();
    });
    // Applied when switched: a word unchecked while looking them over stays
    // in sight until then.
    connect(m_onlyChecked, &QCheckBox::toggled, this, &SpellingReviewDialog::filter);

    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("&Correct"));
    auto* all = buttons->addButton(tr("Check &All"), QDialogButtonBox::ActionRole);
    auto* none = buttons->addButton(tr("Check &None"), QDialogButtonBox::ActionRole);
    const auto checkAll = [this](Qt::CheckState state) {
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            QTreeWidgetItem* row = m_list->topLevelItem(i);
            if (row->flags() & Qt::ItemIsUserCheckable) row->setCheckState(WordColumn, state);
        }
    };
    connect(all, &QPushButton::clicked, this, [checkAll]() { checkAll(Qt::Checked); });
    connect(none, &QPushButton::clicked, this, [checkAll]() { checkAll(Qt::Unchecked); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    updateSummary();
}

QList<QPair<SpellingReviewItem::Range, QString>> SpellingReviewDialog::chosenReplacements() const
{
    QList<QPair<SpellingReviewItem::Range, QString>> result;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        if (row->checkState(WordColumn) != Qt::Checked) continue;
        const SpellingReviewItem& item = m_items[row->data(WordColumn, Qt::UserRole).toInt()];
        const auto* choice = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn));
        const int index = choice ? choice->currentIndex() : 0;
        if (index < 0 || index >= item.choices.size()) continue;
        result.append({item.choices[index].range, item.choices[index].replacement});
    }
    return result;
}

void SpellingReviewDialog::checkAtFirst()
{
    const bool every = m_checkAtFirst->currentIndex() == 1;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        if (!(row->flags() & Qt::ItemIsUserCheckable)) continue;
        const SpellingReviewItem& item = m_items[row->data(WordColumn, Qt::UserRole).toInt()];
        row->setCheckState(WordColumn, every || item.confident ? Qt::Checked : Qt::Unchecked);
        if (auto* choice = qobject_cast<QComboBox*>(m_list->itemWidget(row, CorrectionColumn)))
            choice->setCurrentIndex(0);
    }
}

void SpellingReviewDialog::filter()
{
    const bool only = m_onlyChecked->isChecked();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* row = m_list->topLevelItem(i);
        row->setHidden(only && row->checkState(WordColumn) != Qt::Checked);
    }
}

void SpellingReviewDialog::updateSummary()
{
    int checked = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->checkState(WordColumn) == Qt::Checked) ++checked;
    m_summary->setText(tr("%1 of %2 words to correct").arg(checked).arg(m_items.size()));
}

} // namespace qceditor
