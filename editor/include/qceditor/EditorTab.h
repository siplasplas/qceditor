#pragma once
#include <QWidget>
#include <QPalette>
#include <QPointer>
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

class QFrame;
class QLabel;
class QMenu;
class QVBoxLayout;
namespace qce::encoding { class EncodingGuard; }
namespace qce::spell { class CodeEditSpelling; class SpellingReviewDialog; class GrammarReviewDialog; }

namespace qceditor {

class FindBar;

class EditorTab : public QWidget
{
    Q_OBJECT
public:
    explicit EditorTab(QWidget* parent = nullptr);
    ~EditorTab() override;

    void revealRange(qce::TextCursor start, qce::TextCursor end);
    void showSearch();
    void showReplace();
    void findNext(bool backwards = false);
    /// Asks for line[:column] (columns in characters, as the status bar shows
    /// them) and moves the cursor there; recent positions are offered.
    void goToPosition();

    /// Inserts text at the cursor, replacing the selection, as one undo step;
    /// nothing in a read-only tab.
    void insertText(const QString& text);
    /// The date ("Sunday 2026-10-18"), the time ("16:21:05") or both at the cursor.
    void insertDate();
    void insertTime();
    void insertDateTime();

    /// A read-only tab shows the file (e.g. a viewer of small files).
    void setReadOnly(bool readOnly);
    bool isReadOnly() const;
    /// Long lines wrap at the window's width; on by default.
    void setWordWrap(bool wrap);
    bool wordWrap() const { return m_wordWrap; }
    /// Spaces, tabs and line ends drawn as marks.
    void setShowWhitespace(bool show);
    bool showWhitespace() const;
    /// The character after the cursor as "U+0105", for the status bar; empty
    /// at the end of a line.
    QString codePointAtCursor() const;

    /// Size of a text as the file stores it: bytes in the file's encoding
    /// (with its line breaks; the whole file also with BOM and final line
    /// break), code points (not UTF-16 units; CRLF counts two) and lines.
    struct Statistics { qint64 bytes = 0; qint64 codePoints = 0; int lines = 0; };
    Statistics fileStatistics() const;
    /// Of the selection; all zero without one.
    Statistics selectionStatistics() const;
    /// A window with the statistics of the file and of the selection, if any.
    void showStatistics();

    /// Opens a file; the undo history starts anew. A path that does not exist
    /// yet gives an empty UTF-8 document saved under it. An encrypted .een
    /// file asks for its password (any text, taken as UTF-8); files of EdiQ
    /// and of the older Editas are read, and saving always writes EdiQ's
    /// format, asking a new file for a password (visible ASCII only).
    bool loadFile(const QString& path);
    /// The last load was cancelled by the user (e.g. the password question),
    /// not failed: no error needs showing.
    bool loadCancelled() const { return m_loadCancelled; }

    /// The file is an encrypted .een file (by its name).
    static bool isEncryptedFile(const QString& path);
    bool isEncrypted() const;
    /// Asks for the current and a new password of an encrypted file; the new
    /// one is used when the file is saved next.
    bool changePassword();
    bool hasExternalChanges() const;
    /// Takes the file as it is on disk now as the known version, so that
    /// hasExternalChanges() reports only later changes (e.g. after the user
    /// declined loading it).
    void ignoreExternalChanges() { updateDiskStamp(); }
    /// Reads the file again: only the lines that changed on disk are changed,
    /// as one undo step, so Ctrl+Z brings back the text before the reload.
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
    /// The plausible encodings when detection was not confident on opening
    /// (best first); empty when it was, or the encoding was chosen.
    QList<qce::encoding::EncodingOption> uncertainEncodings() const { return m_uncertain.options; }
    /// Opens the encoding choice dialog and reopens the file with the choice.
    void chooseEncoding();
    /// Saves from now on in `encoding`; the text stays as it is.
    void setSaveEncoding(const QString& encoding);
    /// Encoding, BOM and line breaks of the file, for the status bar.
    qce::encoding::FileFormat fileFormat() const;
    /// Natural language of the file, detected in the background after
    /// loading ("Polish"); empty until known.
    QString languageName() const { return m_language.name; }
    /// Probability of that language (0..1); 0 until known.
    double languageConfidence() const { return m_language.confidence; }

    /// Languages of files not in UTF-8 (cpg keys, e.g. "pl", "cz"): only their
    /// code pages are considered when detecting; empty = all languages.
    /// Kept in the application's settings.
    static QStringList legacyLanguages();
    static void setLegacyLanguages(const QStringList& languages);

    /// Spaces and tabs at the ends of lines removed when saving, as one undo
    /// step, never in the line with the cursor (Tools > Remove Trailing
    /// Whitespace on Save; kept in the settings, ChangedLines by default).
    enum class TrimWhitespace { Off, ChangedLines, AllLines };
    static TrimWhitespace trimWhitespace();
    static void setTrimWhitespace(TrimWhitespace mode);

    /// Spell checking (Tools > Check Spelling), saved in the settings; on by default.
    static bool spellCheckingEnabled();
    static void setSpellCheckingEnabled(bool enabled);
    /// A misspelled word with a single correction is corrected when a space
    /// or punctuation is typed after it (Tools > Correct Spelling While
    /// Typing), saved in the settings; off by default.
    static bool correctWhileTyping();
    static void setCorrectWhileTyping(bool correct);
    /// Grammar checking (Tools > Check Grammar), saved in the settings; on by
    /// default. Works when the grammar's data files are installed (see
    /// qcodeedit's docs/SPELL.md); underlines in blue.
    static bool grammarCheckingEnabled();
    static void setGrammarCheckingEnabled(bool enabled);
    /// The spelling dictionary of a new text, and of one too short to tell
    /// its language (Tools > Default Spelling Language), e.g. "pl_PL";
    /// empty: the desktop's language. Saved in the settings.
    static QString defaultSpellLanguage();
    static void setDefaultSpellLanguage(const QString& dictionary);
    /// Applies the settings to this tab.
    void updateSpellChecking();
    /// The corrections of the misspelled word at the cursor in a menu below
    /// it, to choose with the keyboard (Alt+Enter).
    void showSpellingCorrections();
    /// The grammar mistakes of the selection's lines (or the whole text) in a
    /// window to choose their corrections (Tools > Review Grammar, Ctrl+F6).
    /// The window is not modal; the tab is read-only while it is open.
    void reviewGrammar();
    /// Reviews the grammar of a block of lines (Tools > Review Grammar of
    /// Lines, Ctrl+Shift+F6), chosen as for reviewSpellingBlock().
    void reviewGrammarBlock();
    /// The misspelled words of the selection (or the whole text) in a window
    /// to confirm their corrections, applied as one undo step (Tools > Review
    /// Spelling). The window is not modal; the tab is read-only while it is
    /// open.
    void reviewSpelling();
    /// Reviews a block of lines (Tools > Review Spelling of Lines, Shift+F6):
    /// asks which, showing how many lines the text has, a block of a size
    /// the user sets (1000 by default, kept in the settings) by its lines
    /// ("1001–2000"); the block chosen last in this tab is shown again, so
    /// Up and Enter go on to the next. Selects those lines and reviews them.
    void reviewSpellingBlock();
    /// The user's own words and corrections, to look over or remove (Tools >
    /// User Dictionary); recheck the tabs afterwards.
    void showUserDictionary();
    /// Its Review Spelling or Review Grammar window is open (the tab is
    /// read-only, and is not reloaded from disk meanwhile).
    bool spellingReviewOpen() const;
    /// Checks the visible text again (e.g. after the user dictionary changed).
    void recheckSpelling();
    /// Spelling dictionaries chosen for this document (e.g. en_US, pl_PL);
    /// empty = the detected language.
    void setSpellLanguages(const QStringList& dictionaries);

    QString filePath() const { return m_filePath; }
    /// Differs from the file: edited (undoing back to the saved text is not a
    /// change) or with another encoding to save in.
    bool    isModified() const;

    /// Pick the highlighter again for the current file (e.g. after the Kate
    /// syntax data has been downloaded or updated).
    void reapplyHighlighter();

    /// The spelling dictionaries in use (e.g. "pl_PL"); empty while spell
    /// checking is off.
    QStringList spellingDictionaries() const;

    qce::CodeEdit*           editor()   { return m_edit; }
    qce::SimpleTextDocument* document() { return m_doc; }

signals:
    /// A message for the status bar (e.g. a missing spelling dictionary).
    void message(const QString& text);
    void modificationChanged(bool modified);
    /// Encoding, line breaks or language changed (status bar refresh).
    void statusChanged();
    /// The spelling dictionaries in use changed (e.g. the detected language),
    /// or spell checking was turned on or off; see spellingDictionaries().
    void spellingDictionariesChanged();

private:
    /// Empty `encoding` detects it. A reload (`asEdit`) keeps the undo
    /// history: the new text is one undo step.
    bool readFile(const QString& path, bool resetSyntax, const QString& encoding = {}, bool asEdit = false);
    /// Emits modificationChanged() when isModified() has changed.
    void updateModified();
    /// Replaces the bytes of an encrypted .een file with its text, asking for
    /// the password; false when cancelled or unreadable.
    bool decryptIfEncrypted(const QString& path, QByteArray* bytes);
    bool askNewPassword();
    void forgetPassword();
    /// Removes trailing whitespace before saving, as trimWhitespace() says.
    void trimTrailingWhitespace();
    void addEncodingMenu(QMenu& menu);
    /// Reopens as `encoding`, asking first when there are unsaved edits.
    void reopenAsking(const QString& encoding);
    void showEncodingNotice();
    void hideEncodingNotice();
    /// The dictionaries for this document: chosen, or the detected language.
    void applySpellLanguages();
    void addSpellingMenu(QMenu& menu, const QPoint& position);
    /// Suggestions, Add to Dictionary and Ignore for the misspelled word at
    /// `position`; false when there is none.
    bool addSpellingCorrections(QMenu& menu, qce::TextCursor position);
    /// The problem and the corrections of the grammar mistake at `position`;
    /// false when there is none.
    bool addGrammarCorrections(QMenu& menu, qce::TextCursor position);
    /// Asks for a block of lines, selects it and reviews its spelling or grammar.
    void reviewBlock(bool grammar);
    static QString spaceCorrectionText(const QString& typed, const QString& replacement);
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
    /// Shown above the editor when detection was not confident.
    QFrame* m_encodingNotice = nullptr;
    QLabel* m_encodingNoticeText = nullptr;
    qce::encoding::EncodingDetection m_uncertain;
    qce::spell::CodeEditSpelling* m_spelling = nullptr;
    /// The open Review Spelling window (not modal; the tab is read-only meanwhile).
    QPointer<qce::spell::SpellingReviewDialog> m_review;
    QPointer<qce::spell::GrammarReviewDialog> m_grammarReview;
    int m_reviewBlock = 0;  ///< the block of lines reviewed last (reviewSpellingBlock())
    QStringList m_spellLanguages;  ///< chosen for this document; empty = detected
    QString m_spellMissing;        ///< language whose missing dictionary was reported
    qce::encoding::EncodingGuard* m_encoding = nullptr;
    bool m_saveCancelled = false;
    qce::encoding::Language m_language;
    int m_languageRequest = 0; ///< ignores results for an older load

    QDateTime m_diskModified;
    qint64 m_diskSize = -1;
    QString m_filePath;
    /// The encoding to save in was changed (not an undo step).
    bool    m_formatChanged = false;
    /// isModified() as last reported by modificationChanged().
    bool    m_reportedModified = false;
    bool    m_wordWrap = true;
    /// Password of an encrypted .een file, kept for saving and reloading.
    QString m_eenPassword;
    bool    m_loadCancelled = false;
    /// The text as last read or saved, to tell the lines changed since.
    QString m_savedText;
};

} // namespace qceditor
