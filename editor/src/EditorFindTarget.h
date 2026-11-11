#pragma once

#include "siplasplas/plugin/FindPlugin.h"

namespace qceditor {

class EditorTab;

/// An editor tab as the target of the find bar module: never huge, replacing
/// unless the tab is read-only, with the rows a bar on top needs.
class EditorFindTarget : public spp::FindTarget
{
public:
    explicit EditorFindTarget(EditorTab* tab) : m_tab(tab) {}

    QWidget* widget() override;
    bool canReplace() const override;
    bool isHuge() const override { return false; }
    QString selectedText() const override;

    QString text() const override;
    bool selection(spp::TextRange* range) const override;
    void setSelection(spp::TextRange range) override;
    void setHighlights(const QList<spp::TextRange>& ranges) override;
    void setScopeHighlights(const QList<spp::TextRange>& ranges) override;
    bool replace(const QList<spp::TextReplacement>& replacements, const QString& undoText) override;

    int rowHeight() const override;
    QRect coverableRect() const override;
    int textTop() const override;
    int firstVisibleRow() const override;
    void setFirstVisibleRow(int row) override;
    int rowOf(spp::TextPosition position) const override;

private:
    EditorTab* m_tab;
};

} // namespace qceditor
