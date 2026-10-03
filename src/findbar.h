#pragma once
#include <QWidget>
#include <QVector>
#include <qce/TextCursor.h>
class QLineEdit;
class QLabel;
class QToolButton;
class EditorTab;

class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(EditorTab* tab);
    ~FindBar() override;
    void showSearch();
    void findNext(bool backwards = false);
private:
    struct Match { int start; int end; };
    bool eventFilter(QObject* watched, QEvent* event) override;
    void rebuild(bool moveCursor = true);
    void updateHighlights();
    void activate(int index);
    void closeSearch();
    int cursorOffset() const;
    int offsetFor(qce::TextCursor cursor) const;
    qce::TextCursor positionFor(int offset) const;
    EditorTab* m_tab;
    QLineEdit* m_query;
    QLabel* m_count;
    QToolButton* m_case;
    QToolButton* m_words;
    QToolButton* m_regex;
    QToolButton* m_wrap;
    QToolButton* m_scope;
    QToolButton* m_previous;
    QToolButton* m_next;
    QVector<Match> m_matches;
    QVector<int> m_lineStarts;
    int m_current = -1;
    int m_scopeStart = 0;
    int m_scopeEnd = 0;
    bool m_scopeAvailable = false;
};
