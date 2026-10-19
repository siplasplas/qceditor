#pragma once
#include <QWidget>
#include <QVector>
#include <qce/TextCursor.h>
#include <QStringList>
class QLineEdit;
class QLabel;
class QToolButton;
class QPushButton;
class QShortcut;

namespace qceditor {

class EditorTab;

class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(EditorTab* tab);
    ~FindBar() override;
    void showSearch();
    void showReplace();
    void findNext(bool backwards = false);
    /// The space the tab's layout gives the bar while it pushes the lines down;
    /// otherwise the bar covers the top of the editor.
    QWidget* space() const { return m_space; }
private:
    struct Match { int start; int end; QStringList captures; };
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void rebuild(bool moveCursor = true);
    void updateHighlights();
    void activate(int index);
    void reveal(int row);
    void setCovering(bool covering);
    void place();
    void closeSearch();
    void replaceCurrent();
    void replaceAll();
    void excludeCurrent();
    QString replacementFor(const Match& match) const;
    void updateScope(const QString& text);
    int cursorOffset() const;
    int offsetFor(qce::TextCursor cursor) const;
    qce::TextCursor positionFor(int offset) const;
    EditorTab* m_tab;
    QLineEdit* m_query;
    QLineEdit* m_replacement;
    QWidget* m_replaceRow;
    QToolButton* m_expand;
    QToolButton* m_preserveCase;
    QPushButton* m_replace;
    QPushButton* m_replaceAll;
    QPushButton* m_exclude;
    QLabel* m_count;
    QToolButton* m_case;
    QToolButton* m_words;
    QToolButton* m_regex;
    QToolButton* m_wrap;
    QToolButton* m_scope;
    QToolButton* m_previous;
    QToolButton* m_next;
    QShortcut* m_escape = nullptr;
    QWidget* m_space;
    bool m_covering = false;
    bool m_placing = false;
    QVector<Match> m_matches;
    QVector<int> m_lineStarts;
    int m_current = -1;
    int m_scopeStart = 0;
    int m_scopeEnd = 0;
    bool m_scopeAvailable = false;
    bool m_replacing = false;
    QString m_snapshot;
    QStringList m_captureNames;
    QVector<Match> m_excluded;
};

} // namespace qceditor
