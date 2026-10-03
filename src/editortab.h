#pragma once
#include <QWidget>
#include <QPalette>
#include <qce/kate/KateTheme.h>
#include <memory>
#include <qce/CodeEdit.h>
#include <qce/SimpleTextDocument.h>
#include <qce/margins/LineNumberGutter.h>
#include <qce/margins/FoldingGutter.h>
#include <qce/RulesHighlighter.h>
#include <qce/RuleBasedFoldingProvider.h>

class QVBoxLayout;
class FindBar;

class EditorTab : public QWidget
{
    Q_OBJECT
public:
    explicit EditorTab(QWidget* parent = nullptr);

    void revealRange(qce::TextCursor start, qce::TextCursor end);
    void showSearch();
    void showReplace();
    void findNext(bool backwards = false);

    bool loadFile(const QString& path);
    bool save();
    bool saveAs(const QString& path);

    QString filePath() const { return m_filePath; }
    bool    isModified() const { return m_modified; }

    /// Pick the highlighter again for the current file (e.g. after the Kate
    /// syntax data has been downloaded or updated).
    void reapplyHighlighter();

    qce::CodeEdit*           editor()   { return m_edit; }
    qce::SimpleTextDocument* document() { return m_doc; }

signals:
    void modificationChanged(bool modified);

private:
    void onDocumentChanged();
    void showContextMenu(const QPoint& position);
    void applyThemePalette();
    qce::TextAttribute themedAttribute(const QString& style, const qce::TextAttribute& fallback) const;
    void applyHighlighterForFile(const QString& path);
    void applyCppHighlighter();
    bool applyKateHighlighter(const QString& xmlPath);
    void clearHighlighter();

    qce::SimpleTextDocument*                    m_doc          = nullptr;
    qce::CodeEdit*                              m_edit         = nullptr;
    std::unique_ptr<qce::LineNumberGutter>      m_lineNumbers;
    std::unique_ptr<qce::FoldingGutter>         m_foldGutter;
    std::unique_ptr<qce::RulesHighlighter>      m_highlighter;
    std::unique_ptr<qce::RuleBasedFoldingProvider> m_foldProvider;

    enum class SyntaxMode { Automatic, PlainText, Manual };
    SyntaxMode m_syntaxMode = SyntaxMode::Automatic;
    QString m_syntaxFile;
    QString m_themeFile;
    KateTheme m_theme;
    QPalette m_defaultPalette;

    FindBar* m_findBar = nullptr;

    QString m_filePath;
    bool    m_modified = false;
};
