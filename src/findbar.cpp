#include "findbar.h"
#include "editortab.h"
#include <qce/CodeEditArea.h>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QShortcut>
#include <QRegularExpression>
#include <QStyle>
#include <QSignalBlocker>
#include <QEvent>
#include <algorithm>

FindBar::FindBar(EditorTab* tab) : QWidget(tab), m_tab(tab)
{
    setObjectName("findBar");
    tab->editor()->area()->installEventFilter(this);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(5);
    auto button = [this](const QString& text, const QString& tip, bool toggle = false) {
        auto* b = new QToolButton(this);
        b->setText(text); b->setToolTip(tip); b->setAutoRaise(true); b->setCheckable(toggle);
        return b;
    };
    auto* field = new QWidget(this);
    field->setObjectName("findField");
    field->setStyleSheet("#findField { border: 1px solid palette(mid); border-radius: 3px; background: palette(base); }");
    auto* fieldLayout = new QHBoxLayout(field);
    fieldLayout->setContentsMargins(5, 1, 3, 1); fieldLayout->setSpacing(1);
    fieldLayout->addWidget(new QLabel(QString::fromUtf8("⌕"), field));
    m_query = new QLineEdit(field);
    m_query->setObjectName("findQuery");
    m_query->setFrame(false); m_query->setClearButtonEnabled(true);
    m_query->setPlaceholderText(tr("Find"));
    fieldLayout->addWidget(m_query, 1);
    m_wrap = button(QString::fromUtf8("↩"), tr("Wrap around"), true); m_wrap->setChecked(true);
    m_case = button("Cc", tr("Match case"), true);
    m_words = button("W", tr("Whole words"), true);
    m_regex = button(".*", tr("Regular expression"), true);
    for (auto* b : {m_wrap, m_case, m_words, m_regex}) fieldLayout->addWidget(b);
    field->setMinimumWidth(260); field->setMaximumWidth(360);
    layout->addWidget(field, 1);
    m_count = new QLabel(this); m_count->setObjectName("findCount"); m_count->setMinimumWidth(65);
    layout->addWidget(m_count);
    m_previous = button(QString(), tr("Previous match (Shift+F3)"));
    m_previous->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_next = button(QString(), tr("Next match (F3)"));
    m_next->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    layout->addWidget(m_previous); layout->addWidget(m_next);
    m_scope = button(QString::fromUtf8("▽"), tr("Search in selection"), true);
    layout->addWidget(m_scope);
    layout->addStretch();
    auto* close = button(QString(), tr("Close search (Escape)"));
    close->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton)); layout->addWidget(close);
    connect(close, &QToolButton::clicked, this, &FindBar::closeSearch);
    connect(m_previous, &QToolButton::clicked, this, [this]() { findNext(true); });
    connect(m_next, &QToolButton::clicked, this, [this]() { findNext(); });
    connect(m_query, &QLineEdit::textChanged, this, [this]() { rebuild(); });
    connect(m_query, &QLineEdit::returnPressed, this, [this]() { findNext(); });
    for (auto* b : {m_case, m_words, m_regex}) connect(b, &QToolButton::toggled, this, [this]() { rebuild(); });
    connect(m_scope, &QToolButton::toggled, this, [this](bool enabled) {
        if (enabled) {
            auto* area = m_tab->editor()->area();
            if (!m_scopeAvailable && !area->hasSelection()) {
                QSignalBlocker block(m_scope); m_scope->setChecked(false); return;
            }
            if (!m_scopeAvailable) {
                m_scopeStart = offsetFor(area->selectionStart());
                m_scopeEnd = offsetFor(area->selectionEnd());
                m_scopeAvailable = true;
            }
        }
        rebuild();
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), tab);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &FindBar::closeSearch);
    auto* backwards = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), m_query);
    connect(backwards, &QShortcut::activated, this, [this]() { findNext(true); });
    auto* doc = tab->document();
    connect(doc, &qce::ITextDocument::linesChanged, this, [this]() { rebuild(false); });
    connect(doc, &qce::ITextDocument::linesInserted, this, [this]() { rebuild(false); });
    connect(doc, &qce::ITextDocument::linesRemoved, this, [this]() { rebuild(false); });
    connect(doc, &qce::ITextDocument::documentReset, this, [this]() { rebuild(false); });
    hide();
}
FindBar::~FindBar() = default;

bool FindBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_tab->editor()->area() && event->type() == QEvent::PaletteChange)
        updateHighlights();
    return QWidget::eventFilter(watched, event);
}

void FindBar::showSearch()
{
    auto* area = m_tab->editor()->area();
    const auto selection = area->selectedText();
    // Capture the user's scope before navigation selects the first result.
    if (isHidden() && !m_scope->isChecked()) {
        m_scopeAvailable = area->hasSelection();
        if (m_scopeAvailable) {
            m_scopeStart = offsetFor(area->selectionStart());
            m_scopeEnd = offsetFor(area->selectionEnd());
        }
    }
    show();
    if (!selection.isEmpty() && !selection.contains('\n')) m_query->setText(selection);
    rebuild(); m_query->setFocus(); m_query->selectAll();
}
int FindBar::offsetFor(qce::TextCursor cursor) const
{
    int offset = cursor.column;
    for (int line = 0; line < cursor.line; ++line)
        offset += m_tab->document()->lineAt(line).size() + 1;
    return offset;
}
int FindBar::cursorOffset() const
{
    auto* area = m_tab->editor()->area();
    return offsetFor(area->hasSelection() ? area->selectionStart() : area->cursorPosition());
}
qce::TextCursor FindBar::positionFor(int offset) const
{
    const int line = int(std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), offset)
                         - m_lineStarts.begin()) - 1;
    return line < 0 ? qce::TextCursor{} : qce::TextCursor{line, offset - m_lineStarts[line]};
}
void FindBar::rebuild(bool moveCursor)
{
    m_matches.clear(); m_lineStarts.clear(); m_current = -1;
    QString text = m_tab->document()->toPlainText();
    int offset = 0;
    for (int i = 0; i < m_tab->document()->lineCount(); ++i) {
        m_lineStarts.append(offset); offset += m_tab->document()->lineAt(i).size() + 1;
    }
    m_query->setToolTip(QString());
    if (!isHidden() && !m_query->text().isEmpty()) {
        QString pattern = m_regex->isChecked() ? m_query->text() : QRegularExpression::escape(m_query->text());
        if (m_words->isChecked()) pattern = "(?<![\\p{L}\\p{N}_])(?:" + pattern + ")(?![\\p{L}\\p{N}_])";
        auto options = QRegularExpression::MultilineOption | QRegularExpression::UseUnicodePropertiesOption;
        if (!m_case->isChecked()) options |= QRegularExpression::CaseInsensitiveOption;
        QRegularExpression expression(pattern, options);
        if (!expression.isValid()) {
            m_count->setText(tr("Invalid pattern")); m_query->setToolTip(expression.errorString());
            m_next->setEnabled(false); m_previous->setEnabled(false); updateHighlights(); return;
        }
        auto results = expression.globalMatch(text);
        while (results.hasNext()) {
            const auto result = results.next();
            const int start = result.capturedStart(), end = result.capturedEnd();
            if (m_scope->isChecked() && (start < m_scopeStart || end > m_scopeEnd)) continue;
            m_matches.append({start, end});
        }
        if (!m_matches.isEmpty()) {
            const int cursor = cursorOffset();
            auto it = std::lower_bound(m_matches.begin(), m_matches.end(), cursor,
                                      [](const Match& match, int pos) { return match.start < pos; });
            m_current = it == m_matches.end() ? 0 : int(it - m_matches.begin());
        }
    }
    m_next->setEnabled(!m_matches.isEmpty()); m_previous->setEnabled(!m_matches.isEmpty());
    if (m_current >= 0 && moveCursor) activate(m_current);
    else updateHighlights();
}
void FindBar::activate(int index)
{
    m_current = index;
    const auto match = m_matches[index];
    m_tab->editor()->area()->setSelection(positionFor(match.start), positionFor(match.end));
    updateHighlights();
}
void FindBar::findNext(bool backwards)
{
    if (isHidden()) { show(); rebuild(); m_query->setFocus(); return; }
    if (m_matches.isEmpty()) return;
    int next = m_current + (backwards ? -1 : 1);
    if (next < 0 || next >= m_matches.size()) {
        if (!m_wrap->isChecked()) return;
        next = backwards ? m_matches.size() - 1 : 0;
    }
    activate(next);
}
void FindBar::updateHighlights()
{
    auto* area = m_tab->editor()->area();
    QVector<qce::ExtraSelection> selections;
    if (!isHidden()) {
        const bool dark = area->palette().color(QPalette::Base).lightness() < 128;
        const QColor background(dark ? "#685326" : "#ffe39a");
        const QColor activeBackground(dark ? "#8d4d13" : "#ffae52");
        selections.reserve(m_matches.size() + 1);
        for (const auto& match : m_matches)
            selections.append({positionFor(match.start), positionFor(match.end), background, {}});
        if (m_current >= 0) {
            const auto current = m_matches[m_current];
            selections.append({positionFor(current.start), positionFor(current.end), activeBackground, {}});
        }
    }
    area->setExtraSelections(selections);
    if (m_query->toolTip().isEmpty()) m_count->setText(m_query->text().isEmpty() ? QString() :
        tr("%1/%2").arg(m_current < 0 ? 0 : m_current + 1).arg(m_matches.size()));
}
void FindBar::closeSearch()
{
    hide(); m_matches.clear(); m_current = -1; updateHighlights(); m_tab->editor()->area()->setFocus();
}
