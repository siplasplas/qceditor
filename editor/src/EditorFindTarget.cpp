#include "EditorFindTarget.h"

#include <qceditor/EditorTab.h>
#include <qceditor/FindPlugin.h>

#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>

#include <QScrollBar>

namespace qceditor {

namespace {

spp::FindPlugin* g_findPlugin = nullptr;

// Layers of the area's decorations: the selection searched in beneath the
// matches.
constexpr int kScopeLayer = 120;
constexpr int kMatchesLayer = 121;

qce::TextCursor cursorOf(spp::TextPosition position)
{
    return {position.line, position.column};
}

bool isDark(const qce::CodeEditArea* area)
{
    return area->palette().color(QPalette::Base).lightness() < 128;
}

QVector<qce::ExtraSelection> selectionsOf(const QList<spp::TextRange>& ranges, const QColor& color)
{
    QVector<qce::ExtraSelection> selections;
    selections.reserve(ranges.size());
    for (const spp::TextRange& range : ranges)
        selections.append({cursorOf(range.start), cursorOf(range.end), color, {}});
    return selections;
}

} // namespace

void setFindPlugin(spp::FindPlugin* plugin)
{
    g_findPlugin = plugin;
}

spp::FindPlugin* findPlugin()
{
    return g_findPlugin;
}

QWidget* EditorFindTarget::widget()
{
    return m_tab->editor();
}

bool EditorFindTarget::canReplace() const
{
    return !m_tab->editor()->area()->readOnly();
}

QString EditorFindTarget::selectedText() const
{
    const QString selected = m_tab->editor()->area()->selectedText();
    return selected.contains(QLatin1Char('\n')) ? QString() : selected;
}

QString EditorFindTarget::text() const
{
    return m_tab->document()->toPlainText();
}

bool EditorFindTarget::selection(spp::TextRange* range) const
{
    const qce::CodeEditArea* area = m_tab->editor()->area();
    const qce::TextCursor start = area->selectionStart();
    const qce::TextCursor end = area->selectionEnd();
    *range = {{start.line, start.column}, {end.line, end.column}};
    return area->hasSelection();
}

void EditorFindTarget::setSelection(spp::TextRange range)
{
    // A match in folded lines is unfolded first.
    m_tab->revealRange(cursorOf(range.start), cursorOf(range.end));
    m_tab->editor()->area()->setSelection(cursorOf(range.start), cursorOf(range.end));
}

void EditorFindTarget::setHighlights(const QList<spp::TextRange>& ranges)
{
    // Orange, as qceditor's own bar shows the current match; the current one
    // is the selection.
    qce::CodeEditArea* area = m_tab->editor()->area();
    area->setExtraSelections(kMatchesLayer,
                             selectionsOf(ranges, QColor(isDark(area) ? "#8d4d13" : "#ffae52")));
}

void EditorFindTarget::setScopeHighlights(const QList<spp::TextRange>& ranges)
{
    qce::CodeEditArea* area = m_tab->editor()->area();
    area->setExtraSelections(kScopeLayer, selectionsOf(ranges, QColor(isDark(area) ? "#33404f" : "#e3ecf7")));
}

bool EditorFindTarget::replace(const QList<spp::TextReplacement>& replacements, const QString& undoText)
{
    QList<qce::CodeEditArea::TextReplacement> edits;
    edits.reserve(replacements.size());
    for (const spp::TextReplacement& replacement : replacements)
        edits.append({cursorOf(replacement.range.start), cursorOf(replacement.range.end), replacement.text});
    return m_tab->editor()->area()->replaceRanges(edits, undoText);
}

int EditorFindTarget::rowHeight() const
{
    return m_tab->editor()->area()->viewportState().lineHeight;
}

QRect EditorFindTarget::coverableRect() const
{
    // The editor with its gutters; the scroll bar stays usable beside the bar.
    QWidget* edit = m_tab->editor();
    QRect rect(QPoint(0, 0), edit->size());
    const QScrollBar* scrollBar = m_tab->editor()->area()->verticalScrollBar();
    if (scrollBar->isVisible()) rect.setWidth(rect.width() - scrollBar->width());
    return rect;
}

int EditorFindTarget::textTop() const
{
    return m_tab->editor()->area()->viewport()->mapTo(m_tab->editor(), QPoint()).y();
}

int EditorFindTarget::firstVisibleRow() const
{
    return m_tab->editor()->area()->verticalScrollBar()->value();
}

void EditorFindTarget::setFirstVisibleRow(int row)
{
    m_tab->editor()->area()->verticalScrollBar()->setValue(row);
}

int EditorFindTarget::rowOf(spp::TextPosition position) const
{
    return m_tab->editor()->area()->visualRow(cursorOf(position));
}

} // namespace qceditor
