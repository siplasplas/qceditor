#pragma once
#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <qce/spell/CodeEditSpelling.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QTreeWidget;

namespace qceditor {

/// One misspelled word of the text, with its corrections.
struct SpellingReviewItem
{
    using Range = qce::spell::CodeEditSpelling::Misspelling;
    struct Choice
    {
        Range range;          ///< the word, or the word with its neighbour for a moved space
        QString replacement;
    };
    Range word;
    QString lineText;
    QList<Choice> choices;    ///< best first; a moved space before the word's own suggestions
};

/// The misspelled words of the text or the selection, each with a correction
/// to choose. Checked at first: the words with a single correction, or every
/// word with its first correction (the choice is remembered); the user checks
/// or unchecks the others (and may pick another correction), and can show
/// only the checked words to look them over. The checked ones are replaced
/// together (CodeEditSpelling::replaceWords(), one undo step).
class SpellingReviewDialog : public QDialog
{
    Q_OBJECT
public:
    SpellingReviewDialog(const QList<SpellingReviewItem>& items, bool inSelection, QWidget* parent = nullptr);

    /// The checked words with the chosen corrections.
    QList<QPair<SpellingReviewItem::Range, QString>> chosenReplacements() const;

private:
    void updateSummary();
    /// Checks the words as the "Checked at first" choice says.
    void checkAtFirst();
    /// Hides the unchecked words when "Only checked" is on.
    void filter();

    QList<SpellingReviewItem> m_items;
    QTreeWidget* m_list = nullptr;
    QLabel* m_summary = nullptr;
    QComboBox* m_checkAtFirst = nullptr;
    QCheckBox* m_onlyChecked = nullptr;
};

} // namespace qceditor
