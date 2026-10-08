#pragma once
#include <QWidget>
#include <QPalette>
#include <QDateTime>
#include <qce/kate/KateTheme.h>
#include <memory>
#include <qce/CodeEdit.h>
#include <qce/SimpleTextDocument.h>
#include <qce/margins/LineNumberGutter.h>
#include <qce/margins/FoldingGutter.h>
#include <qce/RulesHighlighter.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/encoding/Encoding.h>

class QMenu;
class QVBoxLayout;
class FindBar;
namespace qce::encoding { class EncodingGuard; }

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
    bool hasExternalChanges() const;
    bool reloadFromDisk();
    bool save();
    bool saveAs(const QString& path);
    /// True when the last save was cancelled in the encoding question
    /// (characters the file's code page cannot store), not failed.
    bool saveCancelled() const { return m_saveCancelled; }

    /// Encoding the file was read in and is saved in (cpg name, e.g. cp1250).
    QString encoding() const;
    /// Reads the file again, decoding it as `encoding`.
    bool reopenWithEncoding(const QString& encoding);
    /// Saves from now on in `encoding`; the text stays as it is.
    void setSaveEncoding(const QString& encoding);
    /// Encoding, BOM and line breaks of the file, for the status bar.
    qce::encoding::FileFormat fileFormat() const;
    /// Natural language of the file, detected in the background after
    /// loading ("Polish"); empty until known.
    QString languageName() const { return m_language.name; }

    QString filePath() const { return m_filePath; }
    bool    isModified() const { return m_modified; }

    /// Pick the highlighter again for the current file (e.g. after the Kate
    /// syntax data has been downloaded or updated).
    void reapplyHighlighter();

    qce::CodeEdit*           editor()   { return m_edit; }
    qce::SimpleTextDocument* document() { return m_doc; }

signals:
    void modificationChanged(bool modified);
    /// Encoding, line breaks or language changed (status bar refresh).
    void statusChanged();

private:
    /// Empty `encoding` detects it.
    bool readFile(const QString& path, bool resetSyntax, const QString& encoding = {});
    void addEncodingMenu(QMenu& menu);
    void updateDiskStamp();
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
    qce::encoding::EncodingGuard* m_encoding = nullptr;
    bool m_saveCancelled = false;
    qce::encoding::Language m_language;
    int m_languageRequest = 0; ///< ignores results for an older load

    QDateTime m_diskModified;
    qint64 m_diskSize = -1;
    QString m_filePath;
    bool    m_modified = false;
};
