#pragma once
#include <QWidget>
#include <memory>
#include <qce/CodeEdit.h>
#include <qce/SimpleTextDocument.h>
#include <qce/margins/LineNumberGutter.h>
#include <qce/margins/FoldingGutter.h>
#include <qce/RulesHighlighter.h>
#include <qce/RuleBasedFoldingProvider.h>

class QVBoxLayout;

class EditorTab : public QWidget
{
    Q_OBJECT
public:
    explicit EditorTab(QWidget* parent = nullptr);

    bool loadFile(const QString& path);
    bool save();
    bool saveAs(const QString& path);

    QString filePath() const { return m_filePath; }
    bool    isModified() const { return m_modified; }

    qce::CodeEdit*           editor()   { return m_edit; }
    qce::SimpleTextDocument* document() { return m_doc; }

signals:
    void modificationChanged(bool modified);

private:
    void onDocumentChanged();
    void applyHighlighterForFile(const QString& path);
    void applyCppHighlighter();
    void applyKateHighlighter(const QString& xmlPath);
    void clearHighlighter();

    qce::SimpleTextDocument*                    m_doc          = nullptr;
    qce::CodeEdit*                              m_edit         = nullptr;
    std::unique_ptr<qce::LineNumberGutter>      m_lineNumbers;
    std::unique_ptr<qce::FoldingGutter>         m_foldGutter;
    std::unique_ptr<qce::RulesHighlighter>      m_highlighter;
    std::unique_ptr<qce::RuleBasedFoldingProvider> m_foldProvider;

    QString m_filePath;
    bool    m_modified = false;
};
