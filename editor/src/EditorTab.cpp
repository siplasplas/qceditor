#include <qceditor/EditorTab.h>
#include <qceditor/FindBar.h>
#include <qceditor/EncodingChoiceDialog.h>
#include <qceditor/Settings.h>
#include <qceditor/SpellLanguageDialog.h>
#include <qceditor/SyntaxData.h>
#include <QLocale>
#include <qce/spell/CodeEditSpelling.h>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QMenu>
#include <QActionGroup>
#include <QMap>
#include <QMessageBox>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QUndoStack>
#include <optional>
#include <qce/CodePoints.h>
#include <qce/diff/LineDiff.h>
#include <QGuiApplication>
#include <QShortcut>
#include "EenFile.h"
#include <algorithm>
#include <functional>
#include <qce/CodeEditArea.h>
#include <qce/encoding/Encoding.h>
#include <qce/encoding/EncodingGuard.h>
#include <qce/FoldState.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KatePaths.h>
#include <qce/kate/KateXmlReader.h>

namespace qceditor {

// Used only when no Kate definition matches (e.g. data not downloaded yet).
static bool isCppExtension(const QString& ext)
{
    static const QStringList cpp = {"c","cpp","cxx","cc","h","hpp","hxx"};
    return cpp.contains(ext.toLower());
}

EditorTab::EditorTab(QWidget* parent)
    : QWidget(parent)
{
    m_doc  = new qce::SimpleTextDocument(this);
    m_edit = new qce::CodeEdit(this);
    m_edit->setDocument(m_doc);
    m_defaultPalette = m_edit->area()->palette();
    m_edit->area()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_edit->area(), &QWidget::customContextMenuRequested,
            this, &EditorTab::showContextMenu);

    // A reload changes only the lines that differ, as one undo step
    // (qcodeedit-diff; installed here as well in case the program's own
    // registration was not linked in).
    qce::diff::install();
    connect(m_edit->area()->undoStack(), &QUndoStack::cleanChanged,
            this, &EditorTab::updateModified);

    // Keeps typed and pasted text storable in the file's code page.
    m_encoding = new qce::encoding::EncodingGuard(m_edit->area(), this);
    connect(m_encoding, &qce::encoding::EncodingGuard::encodingChanged,
            this, &EditorTab::statusChanged);
    // Switching to UTF-8 is a change that needs saving.
    connect(m_encoding, &qce::encoding::EncodingGuard::encodingChanged, this, [this]() {
        m_formatChanged = true;
        hideEncodingNotice();
        updateModified();
    });

    // Wavy underlines under misspelled words in comments, strings and text;
    // the language follows the detected one until the user chooses.
    m_spelling = new qce::spell::CodeEditSpelling(m_edit->area(), this);
    m_spelling->setEnabled(spellCheckingEnabled());
    applySpellLanguages();
    // Ctrl+Enter: the corrections of the word at the cursor, without the mouse.
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* corrections = new QShortcut(QKeySequence(Qt::CTRL | key), m_edit->area());
        corrections->setContext(Qt::WidgetShortcut);
        connect(corrections, &QShortcut::activated, this, &EditorTab::showSpellingCorrections);
    }

    m_lineNumbers = std::make_unique<qce::LineNumberGutter>(m_doc);
    m_lineNumbers->setFont(m_edit->area()->font());
    m_edit->addLeftMargin(m_lineNumbers.get());

    // FoldingGutter is added once and stays; it reads foldState() which is
    // updated whenever a new FoldingProvider is set.
    m_foldGutter = std::make_unique<qce::FoldingGutter>(
        &m_edit->area()->foldState(),
        [this](int line) { m_edit->area()->toggleFoldAt(line); });
    m_edit->addLeftMargin(m_foldGutter.get());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_findBar = new FindBar(this);
    layout->addWidget(m_findBar);

    // Non-modal notice when the encoding was guessed without confidence: the
    // file is already open with the most likely one.
    m_encodingNotice = new QFrame(this);
    m_encodingNotice->setFrameShape(QFrame::StyledPanel);
    m_encodingNotice->setAutoFillBackground(true);
    QPalette noticePalette = m_encodingNotice->palette();
    noticePalette.setColor(QPalette::Window, noticePalette.color(QPalette::ToolTipBase));
    noticePalette.setColor(QPalette::WindowText, noticePalette.color(QPalette::ToolTipText));
    m_encodingNotice->setPalette(noticePalette);
    auto* noticeLayout = new QHBoxLayout(m_encodingNotice);
    noticeLayout->setContentsMargins(8, 3, 4, 3);
    m_encodingNoticeText = new QLabel(m_encodingNotice);
    noticeLayout->addWidget(m_encodingNoticeText, 1);
    auto* choose = new QPushButton(tr("Choose…"), m_encodingNotice);
    auto* keep = new QPushButton(tr("Keep"), m_encodingNotice);
    noticeLayout->addWidget(choose);
    noticeLayout->addWidget(keep);
    connect(choose, &QPushButton::clicked, this, &EditorTab::chooseEncoding);
    connect(keep, &QPushButton::clicked, this, &EditorTab::hideEncodingNotice);
    m_encodingNotice->hide();
    layout->addWidget(m_encodingNotice);

    layout->addWidget(m_edit);

    connect(m_doc, &qce::SimpleTextDocument::linesChanged,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesInserted,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesRemoved,
            this, &EditorTab::onDocumentChanged);

    m_theme = KateTheme::loadDefault();
    applyThemePalette();
}

// The editor and its undo stack go only after this object's own part; what
// they report while going must not reach it.
EditorTab::~EditorTab()
{
    disconnect(m_edit->area()->undoStack(), nullptr, this, nullptr);
    disconnect(m_doc, nullptr, this, nullptr);
    disconnect(m_encoding, nullptr, this, nullptr);
    forgetPassword();
}

void EditorTab::revealRange(qce::TextCursor start, qce::TextCursor end)
{
    auto* area = m_edit->area();
    auto& folds = area->foldState();
    int refreshLine = -1;
    const auto& regions = folds.regions();
    for (int i = 0; i < regions.size(); ++i) {
        if (!folds.isCollapsed(i)) continue;
        const auto& region = regions[i];
        const qce::TextCursor hiddenStart{region.startLine, region.startColumn};
        // Folding hides the header suffix and every following line through
        // endLine, including text after the closing marker on that line.
        const bool overlaps = start.line <= region.endLine
            && (start == end ? hiddenStart <= start : hiddenStart < end);
        if (!overlaps) continue;
        folds.setCollapsed(i, false);
        refreshLine = region.startLine;
    }
    if (refreshLine >= 0) {
        // The public toggle refreshes layout, scrollbars and the viewport.
        // It addresses the first region on a line; invert that region first
        // so the toggle restores its desired state, including same-line folds.
        const int first = folds.regionStartingAt(refreshLine);
        folds.setCollapsed(first, !folds.isCollapsed(first));
        area->toggleFoldAt(refreshLine);
    }
}

void EditorTab::showSearch()
{
    m_findBar->showSearch();
}

void EditorTab::showReplace()
{
    m_findBar->showReplace();
}

void EditorTab::findNext(bool backwards)
{
    m_findBar->findNext(backwards);
}

void EditorTab::goToPosition()
{
    auto* area = m_edit->area();
    const auto cursor = area->cursorPosition();
    const int lineCount = qMax(1, m_doc->lineCount());
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Go to Line/Column"));
    dialog.setLabelText(tr("Line[:column] (1–%1):").arg(lineCount));
    // Editable combo: the current position, then positions entered before in
    // any tab (this session only, not saved).
    static QStringList recentPositions;
    // Columns in characters (code points), as the status bar shows them.
    const int currentColumn = qce::codepoints::codePointColumn(m_doc->lineAt(cursor.line), cursor.column);
    QStringList items{QString("%1:%2").arg(cursor.line + 1).arg(currentColumn + 1)};
    for (const QString& recent : recentPositions)
        if (!items.contains(recent)) items << recent;
    dialog.setComboBoxItems(items);
    dialog.setComboBoxEditable(true);
    dialog.setTextValue(items.first());

    auto position = [this, lineCount](const QString& text) -> std::optional<qce::TextCursor> {
        static const QRegularExpression pattern(QStringLiteral("^\\s*([1-9][0-9]*)\\s*(?::\\s*([1-9][0-9]*)\\s*)?$"));
        const auto match = pattern.match(text);
        if (!match.hasMatch()) return std::nullopt;
        bool lineOk = false, columnOk = true;
        const int line = match.captured(1).toInt(&lineOk);
        const int column = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt(&columnOk);
        if (!lineOk || !columnOk || line > lineCount)
            return std::nullopt;
        // The column counts characters (code points): walk to its UTF-16 index.
        const QString lineText = m_doc->lineAt(line - 1);
        int index = 0;
        for (int i = 1; i < column; ++i) {
            if (index >= lineText.size()) return std::nullopt;
            index = qce::codepoints::nextIndex(lineText, index);
        }
        return qce::TextCursor{line - 1, index};
    };
    auto validate = [&dialog, position]() {
        if (auto* buttons = dialog.findChild<QDialogButtonBox*>())
            buttons->button(QDialogButtonBox::Ok)->setEnabled(position(dialog.textValue()).has_value());
    };
    connect(&dialog, &QInputDialog::textValueChanged, &dialog, validate);
    QTimer::singleShot(0, &dialog, [&dialog, validate]() {
        if (auto* input = dialog.findChild<QLineEdit*>()) input->selectAll();
        validate();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    if (const auto target = position(dialog.textValue())) {
        // Remembered without spaces, most recent first.
        QString entered = dialog.textValue();
        entered.remove(QRegularExpression(QStringLiteral("\\s")));
        recentPositions.removeAll(entered);
        recentPositions.prepend(entered);
        while (recentPositions.size() > 20) recentPositions.removeLast();
        revealRange(*target, *target);
        area->setCursorPosition(*target);
        area->setFocus();
    }
}

void EditorTab::insertText(const QString& text)
{
    if (!isReadOnly()) m_edit->area()->insertText(text);
}

void EditorTab::insertDate()
{
    const QDate date = QDate::currentDate();
    insertText(QLocale::c().dayName(date.dayOfWeek()) + QLatin1Char(' ')
               + date.toString(QStringLiteral("yyyy-MM-dd")));
}

void EditorTab::insertTime()
{
    insertText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
}

void EditorTab::insertDateTime()
{
    const QDateTime now = QDateTime::currentDateTime();
    insertText(QLocale::c().dayName(now.date().dayOfWeek()) + QLatin1Char(' ')
               + now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}

bool EditorTab::isEncryptedFile(const QString& path)
{
    return een::isEenPath(path);
}

bool EditorTab::isEncrypted() const
{
    return een::isEenPath(m_filePath);
}

// The text of an encrypted .een file, asking for its password (the one it
// was opened with first, e.g. on a reload). A .een file without the header
// (new, or saved by another program) is plain text.
bool EditorTab::decryptIfEncrypted(const QString& path, QByteArray* bytes)
{
    if (!een::isEenPath(path) || een::format(*bytes) == een::Format::None)
        return true;
    QString password = m_eenPassword;
    const QString name = QFileInfo(path).fileName();
    for (;;) {
        if (password.isEmpty()) {
            bool ok = false;
            password = QInputDialog::getText(this, tr("Encrypted File"), tr("Password for %1:").arg(name),
                                             QLineEdit::Password, QString(), &ok);
            if (!ok) {
                m_loadCancelled = true;
                return false;
            }
        }
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        const een::Decrypted result = een::decrypt(*bytes, password);
        QGuiApplication::restoreOverrideCursor();
        if (result.status == een::Status::Ok) {
            *bytes = result.data;
            m_eenPassword = password;
            return true;
        }
        if (result.status == een::Status::Invalid) {
            QMessageBox::warning(this, tr("Encrypted File"), tr("%1 is damaged or of an unknown version.").arg(name));
            m_loadCancelled = true;
            return false;
        }
        QMessageBox::warning(this, tr("Encrypted File"), tr("Wrong password for %1 (check Caps Lock).").arg(name));
        password.clear();
    }
}

bool EditorTab::askNewPassword()
{
    bool ok = false;
    const QString name = m_filePath.isEmpty() ? tr("the file") : QFileInfo(m_filePath).fileName();
    const QString first = QInputDialog::getText(this, tr("Set Password"),
                                                tr("New password for %1:").arg(name),
                                                QLineEdit::Password, QString(), &ok);
    if (!ok) return false;
    if (first.isEmpty()) {
        QMessageBox::warning(this, tr("Set Password"), tr("The password is empty."));
        return false;
    }
    // A new password is visible ASCII only (no spaces, no line breaks), the
    // same bytes for every program and keyboard; any password opens a file.
    const bool visibleAscii = std::all_of(first.cbegin(), first.cend(), [](QChar c) {
        return c.unicode() > 0x20 && c.unicode() < 0x7F;
    });
    if (!visibleAscii) {
        QMessageBox::warning(this, tr("Set Password"),
                             tr("A new password may contain only ASCII letters, digits and punctuation, "
                                "without spaces."));
        return false;
    }
    const QString second = QInputDialog::getText(this, tr("Set Password"), tr("Repeat the password:"),
                                                 QLineEdit::Password, QString(), &ok);
    if (!ok) return false;
    if (second != first) {
        QMessageBox::warning(this, tr("Set Password"), tr("The two passwords differ."));
        return false;
    }
    forgetPassword();
    m_eenPassword = first;
    return true;
}

bool EditorTab::changePassword()
{
    if (!isEncrypted()) return false;
    if (!m_eenPassword.isEmpty()) {
        bool ok = false;
        const QString current = QInputDialog::getText(this, tr("Change Password"), tr("Current password:"),
                                                      QLineEdit::Password, QString(), &ok);
        if (!ok) return false;
        if (current != m_eenPassword) {
            QMessageBox::warning(this, tr("Change Password"),
                                 tr("Wrong password. If you have forgotten it, copy the text into a new file."));
            return false;
        }
    }
    if (!askNewPassword()) return false;
    // Takes effect when the file is saved.
    m_formatChanged = true;
    updateModified();
    return true;
}

void EditorTab::forgetPassword()
{
    m_eenPassword.fill(QChar(0));
    m_eenPassword.clear();
}

void EditorTab::setReadOnly(bool readOnly)
{
    m_edit->area()->setReadOnly(readOnly);
}

bool EditorTab::isReadOnly() const
{
    return m_edit->area()->readOnly();
}

void EditorTab::setWordWrap(bool wrap)
{
    m_wordWrap = wrap;
    m_edit->area()->setWordWrap(wrap);
}

void EditorTab::setShowWhitespace(bool show)
{
    m_edit->area()->setShowWhitespace(show);
}

bool EditorTab::showWhitespace() const
{
    return m_edit->area()->showWhitespace();
}

QString EditorTab::codePointAtCursor() const
{
    const auto cursor = m_edit->area()->cursorPosition();
    const QString line = m_doc->lineAt(cursor.line);
    if (cursor.column < 0 || cursor.column >= line.size())
        return {};
    char32_t codePoint = line.at(cursor.column).unicode();
    if (QChar::isHighSurrogate(codePoint) && cursor.column + 1 < line.size()
        && line.at(cursor.column + 1).isLowSurrogate())
        codePoint = QChar::surrogateToUcs4(line.at(cursor.column), line.at(cursor.column + 1));
    return QStringLiteral("U+%1").arg(static_cast<uint>(codePoint), 4, 16, QLatin1Char('0')).toUpper();
}

bool EditorTab::isModified() const
{
    return m_formatChanged || !m_edit->area()->undoStack()->isClean();
}

void EditorTab::updateModified()
{
    const bool modified = isModified();
    if (modified == m_reportedModified) return;
    m_reportedModified = modified;
    emit modificationChanged(modified);
}

void EditorTab::showContextMenu(const QPoint& position)
{
    QMenu menu(this);
    addSpellingMenu(menu, position);
    // Undo, Redo, Cut, Copy, Paste, Delete and Select All.
    const std::unique_ptr<QMenu> standard(m_edit->area()->createStandardContextMenu(&menu));
    menu.addActions(standard->actions());
    menu.addSeparator();
    auto* syntaxMenu = menu.addMenu(tr("Syntax"));
    auto* choices = new QActionGroup(&menu);
    choices->setExclusive(true);

    auto addChoice = [choices](QMenu* parent, const QString& text, bool checked) {
        auto* action = parent->addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        choices->addAction(action);
        return action;
    };

    auto* automatic = addChoice(syntaxMenu, tr("Automatic"),
                                m_syntaxMode == SyntaxMode::Automatic);
    connect(automatic, &QAction::triggered, this, [this]() {
        m_syntaxMode = SyntaxMode::Automatic;
        m_syntaxFile.clear();
        reapplyHighlighter();
    });
    auto* plainText = addChoice(syntaxMenu, tr("Plain Text"),
                                m_syntaxMode == SyntaxMode::PlainText);
    connect(plainText, &QAction::triggered, this, [this]() {
        m_syntaxMode = SyntaxMode::PlainText;
        m_syntaxFile.clear();
        reapplyHighlighter();
    });
    syntaxMenu->addSeparator();

    QMap<QString, QList<qce::kate::LanguageEntry>> sections;
    for (const auto& entry : SyntaxData::index().languages()) {
        if (entry.hidden || entry.unsupported)
            continue;
        sections[entry.section.isEmpty() ? tr("Other") : entry.section].append(entry);
    }
    auto sectionNames = sections.keys();
    std::sort(sectionNames.begin(), sectionNames.end(), [](const QString& a, const QString& b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    for (const auto& section : sectionNames) {
        // Index names are literal labels, not Qt mnemonic strings.
        auto* group = syntaxMenu->addMenu(QString(section).replace("&", "&&"));
        auto& entries = sections[section];
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });
        for (const auto& entry : entries) {
            auto* action = addChoice(group, QString(entry.name).replace("&", "&&"),
                m_syntaxMode == SyntaxMode::Manual && m_syntaxFile == entry.file);
            connect(action, &QAction::triggered, this, [this, file = entry.file]() {
                const auto& index = SyntaxData::index();
                const auto* language = index.byFile(file);
                if (!language || language->unsupported
                    || !applyKateHighlighter(index.filePath(*language))) {
                    QMessageBox::warning(this, tr("Syntax"),
                                         tr("Cannot load syntax definition: %1").arg(file));
                    return;
                }
                m_syntaxMode = SyntaxMode::Manual;
                m_syntaxFile = file;
            });
        }
    }
    if (sections.isEmpty())
        syntaxMenu->addAction(tr("No syntax definitions installed"))->setEnabled(false);

    auto* themeMenu = menu.addMenu(tr("Theme"));
    auto* themeChoices = new QActionGroup(&menu);
    themeChoices->setExclusive(true);
    auto addTheme = [&](const QString& name, const QString& path) {
        auto* action = themeMenu->addAction(QString(name).replace("&", "&&"));
        action->setCheckable(true);
        action->setChecked(m_themeFile == path);
        themeChoices->addAction(action);
        connect(action, &QAction::triggered, this, [this, path]() {
            KateTheme theme;
            if (!path.isEmpty()) {
                theme = KateTheme::load(path);
                if (!theme.isValid()) {
                    QMessageBox::warning(this, tr("Theme"),
                                         tr("Cannot load theme: %1").arg(path));
                    return;
                }
            }
            m_themeFile = path;
            m_theme = theme;
            reapplyHighlighter();
        });
    };
    addTheme(tr("Default"), QString());
    themeMenu->addSeparator();
    const auto themes = KateTheme::listThemes(qce::kate::themesDir());
    for (const auto& theme : themes)
        addTheme(theme.first, theme.second);
    if (themes.isEmpty())
        themeMenu->addAction(tr("No themes installed"))->setEnabled(false);

    addEncodingMenu(menu);

    menu.exec(m_edit->area()->viewport()->mapToGlobal(position));
}

void EditorTab::addEncodingMenu(QMenu& menu)
{
    // Common encodings first; the rest of cpg's list under "More".
    static const QStringList common = {
        "utf8", "cp1250", "iso-8859-2", "cp852", "mazovia",
        "cp1252", "iso-8859-1", "cp1251", "utf16", "utf16be"};
    const QString current = encoding();
    auto* encodingMenu = menu.addMenu(tr("Encoding: %1").arg(current));

    auto fill = [&](QMenu* target, bool withDetection, const std::function<void(const QString&)>& apply) {
        auto* group = new QActionGroup(target);
        auto add = [&](QMenu* parent, const QString& name, const QString& text) {
            auto* action = parent->addAction(text);
            action->setCheckable(true);
            action->setChecked(name == current);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [apply, name]() { apply(name); });
        };
        // When detection was not sure: its options with their probability first.
        if (withDetection && !m_uncertain.options.isEmpty()) {
            for (const auto& option : m_uncertain.options) {
                QString text = option.encoding;
                const QString language = qce::encoding::languageName(option.language);
                if (!language.isEmpty()) text += QStringLiteral(" — ") + language;
                text += QStringLiteral(" — %1 %").arg(qRound(option.confidence * 100));
                add(target, option.encoding, text);
            }
            target->addSeparator();
        }
        for (const QString& name : common) add(target, name, name);
        auto* more = target->addMenu(tr("More"));
        for (const QString& name : qce::encoding::availableEncodings())
            if (!common.contains(name)) add(more, name, name);
    };

    fill(encodingMenu->addMenu(tr("Reopen with Encoding")), true,
         [this](const QString& name) { reopenAsking(name); });
    fill(encodingMenu->addMenu(tr("Save with Encoding")), false, [this](const QString& name) {
        setSaveEncoding(name);
    });
}

static const char* kSpellingEnabledKey = "spelling/enabled";
static const char* kRecentSpellingKey = "spelling/recentDictionaries";
static constexpr int kRecentSpellingCount = 6;

bool EditorTab::spellCheckingEnabled()
{
    return settings().value(kSpellingEnabledKey, true).toBool();
}

void EditorTab::setSpellCheckingEnabled(bool enabled)
{
    settings().setValue(kSpellingEnabledKey, enabled);
}

void EditorTab::updateSpellChecking()
{
    m_spelling->setEnabled(spellCheckingEnabled());
}

void EditorTab::setSpellLanguages(const QStringList& dictionaries)
{
    m_spellLanguages = dictionaries;
    // The chosen ones become the most recent in the Spelling Language menu.
    QStringList recent = settings().value(kRecentSpellingKey).toStringList();
    for (auto it = dictionaries.crbegin(); it != dictionaries.crend(); ++it) {
        recent.removeAll(*it);
        recent.prepend(*it);
    }
    settings().setValue(kRecentSpellingKey, recent.mid(0, kRecentSpellingCount));
    applySpellLanguages();
}

void EditorTab::applySpellLanguages()
{
    // Chosen, else the detected language, else the desktop's (e.g. a new file).
    QStringList languages = m_spellLanguages;
    if (languages.isEmpty())
        languages << (m_language.code.isEmpty() ? QLocale::system().name().section('_', 0, 0) : m_language.code);
    m_spelling->setLanguages(languages);
    if (m_spelling->isReady() || !m_spelling->isEnabled()) return;
    // Said once per language: which dictionary package would add it.
    const QString missing = languages.join(',');
    if (missing == m_spellMissing) return;
    m_spellMissing = missing;
    const QString name = qce::encoding::languageName(languages.front());
    emit message(tr("No spelling dictionary for %1; install one with hunspell, e.g. sudo apt install %2")
                     .arg(name.isEmpty() ? languages.front() : name, qce::spell::packageHint(languages.front())));
}

bool EditorTab::addSpellingCorrections(QMenu& menu, qce::TextCursor position)
{
    if (!m_spelling->isEnabled()) return false;
    const auto word = m_spelling->misspelledWordAt(position);
    if (!word) return false;
    const QStringList suggestions = m_spelling->suggestions(word->word, 8);
    if (suggestions.isEmpty())
        menu.addAction(tr("No suggestions"))->setEnabled(false);
    for (const QString& suggestion : suggestions) {
        auto* action = menu.addAction(QString(suggestion).replace("&", "&&"));
        connect(action, &QAction::triggered, this, [this, w = *word, suggestion]() {
            m_spelling->replaceWord(w, suggestion);
        });
    }
    menu.addSeparator();
    connect(menu.addAction(tr("Add to Dictionary")), &QAction::triggered, this,
            [this, w = word->word]() { m_spelling->addToDictionary(w); });
    connect(menu.addAction(tr("Ignore")), &QAction::triggered, this,
            [this, w = word->word]() { m_spelling->ignore(w); });
    return true;
}

// Ctrl+Enter: the corrections of the misspelled word at the cursor, below
// it, to choose with the keyboard.
void EditorTab::showSpellingCorrections()
{
    auto* area = m_edit->area();
    QMenu menu(this);
    if (!addSpellingCorrections(menu, area->cursorPosition())) {
        emit message(tr("No misspelled word at the cursor"));
        return;
    }
    const QRect caret = area->inputMethodQuery(Qt::ImCursorRectangle).toRect();
    menu.exec(area->mapToGlobal(caret.bottomLeft()));
}

void EditorTab::addSpellingMenu(QMenu& menu, const QPoint& position)
{
    // customContextMenuRequested gives the click in the viewport's coordinates.
    if (addSpellingCorrections(menu, m_edit->area()->cursorAt(position)))
        menu.addSeparator();

    // Recent dictionaries and the ones in use; up to two at once.
    auto* languageMenu = menu.addMenu(tr("Spelling Language"));
    QHash<QString, QString> names;
    for (const auto& dictionary : qce::spell::installedDictionaries())
        names.insert(dictionary.name, dictionary.displayName);
    auto* automatic = languageMenu->addAction(m_language.name.isEmpty()
        ? tr("Automatic") : tr("Automatic (%1)").arg(m_language.name));
    automatic->setCheckable(true);
    automatic->setChecked(m_spellLanguages.isEmpty());
    connect(automatic, &QAction::triggered, this, [this]() { setSpellLanguages({}); });
    languageMenu->addSeparator();
    QStringList candidates = settings().value(kRecentSpellingKey).toStringList();
    for (const QString& name : m_spelling->dictionaries())
        if (!candidates.contains(name)) candidates << name;
    for (const QString& name : candidates) {
        if (!names.contains(name)) continue;
        auto* action = languageMenu->addAction(QString(names.value(name)).replace("&", "&&"));
        action->setCheckable(true);
        action->setChecked(m_spellLanguages.contains(name));
        connect(action, &QAction::triggered, this, [this, name](bool checked) {
            QStringList chosen = m_spellLanguages;
            chosen.removeAll(name);
            if (checked) chosen << name;
            while (chosen.size() > SpellLanguageDialog::MaxLanguages) chosen.removeFirst();
            setSpellLanguages(chosen);
        });
    }
    languageMenu->addSeparator();
    connect(languageMenu->addAction(tr("More...")), &QAction::triggered, this, [this]() {
        SpellLanguageDialog dialog(m_spellLanguages.isEmpty() ? m_spelling->dictionaries() : m_spellLanguages, this);
        if (dialog.exec() == QDialog::Accepted) setSpellLanguages(dialog.chosenDictionaries());
    });
    menu.addSeparator();
}

static const char* kLegacyLanguagesKey = "encoding/legacyLanguages";

QStringList EditorTab::legacyLanguages()
{
    return settings().value(kLegacyLanguagesKey).toStringList();
}

void EditorTab::setLegacyLanguages(const QStringList& languages)
{
    if (languages.isEmpty()) settings().remove(kLegacyLanguagesKey);
    else settings().setValue(kLegacyLanguagesKey, languages);
}

// "off", "changed" or "all" in the settings.
static const char* kTrimWhitespaceKey = "editing/trimTrailingWhitespace";

EditorTab::TrimWhitespace EditorTab::trimWhitespace()
{
    const QString mode = settings().value(kTrimWhitespaceKey, QStringLiteral("changed")).toString();
    if (mode == QLatin1String("off")) return TrimWhitespace::Off;
    if (mode == QLatin1String("all")) return TrimWhitespace::AllLines;
    return TrimWhitespace::ChangedLines;
}

void EditorTab::setTrimWhitespace(TrimWhitespace mode)
{
    settings().setValue(kTrimWhitespaceKey, mode == TrimWhitespace::Off ? QStringLiteral("off")
                                            : mode == TrimWhitespace::AllLines ? QStringLiteral("all")
                                                                               : QStringLiteral("changed"));
}

// One undo step: the new text goes through setText(), which with
// qcodeedit-diff changes only the trimmed lines. The cursor's line is left
// alone, so that typing on after a space goes on as typed.
void EditorTab::trimTrailingWhitespace()
{
    const TrimWhitespace mode = trimWhitespace();
    if (mode == TrimWhitespace::Off || !qce::lineDiff()) return;
    QStringList lines = m_doc->toPlainText().split(QLatin1Char('\n'));
    QVector<bool> trim(lines.size(), mode == TrimWhitespace::AllLines);
    if (mode == TrimWhitespace::ChangedLines) {
        // Lines nobody touched keep their whitespace (no stray changes, e.g. in git).
        const QStringList saved = m_savedText.split(QLatin1Char('\n'));
        for (const qce::LineChange& change : qce::lineDiff()(saved, lines))
            for (int i = change.newStart; i < change.newStart + change.newCount && i < lines.size(); ++i)
                trim[i] = true;
    }
    const int cursorLine = m_edit->area()->cursorPosition().line;
    bool changed = false;
    for (int i = 0; i < lines.size(); ++i) {
        if (!trim[i] || i == cursorLine) continue;
        QString& line = lines[i];
        int end = int(line.size());
        while (end > 0 && (line.at(end - 1) == QLatin1Char(' ') || line.at(end - 1) == QLatin1Char('\t')))
            --end;
        if (end == line.size()) continue;
        line.truncate(end);
        changed = true;
    }
    // setText() drops one final line break: the extra one keeps an empty last line.
    if (changed) m_doc->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
}

void EditorTab::reopenAsking(const QString& name)
{
    if (m_filePath.isEmpty()) return;
    if (isModified() && QMessageBox::question(this, tr("Reopen with Encoding"),
            tr("Discard unsaved edits and read the file again as %1?").arg(name))
            != QMessageBox::Yes)
        return;
    if (!reopenWithEncoding(name))
        QMessageBox::warning(this, tr("Reopen with Encoding"),
                             tr("The file cannot be read as %1.").arg(name));
}

void EditorTab::showEncodingNotice()
{
    QStringList options;
    for (const auto& option : m_uncertain.options)
        options << QStringLiteral("%1 %2 %").arg(option.encoding).arg(qRound(option.confidence * 100));
    m_encodingNoticeText->setText(tr("Encoding uncertain: %1").arg(options.join(QStringLiteral(", "))));
    m_encodingNoticeText->setToolTip(tr("The file is shown as %1, the most likely encoding. "
                                        "Choose… compares the encodings on the lines where they differ.")
                                         .arg(m_uncertain.options.value(0).encoding));
    m_encodingNotice->show();
}

void EditorTab::hideEncodingNotice()
{
    m_encodingNotice->hide();
}

void EditorTab::chooseEncoding()
{
    QFile file(m_filePath);
    if (m_filePath.isEmpty() || !file.open(QIODevice::ReadOnly)) return;
    QByteArray bytes = file.readAll();
    if (!decryptIfEncrypted(m_filePath, &bytes)) return;
    // The options as detected on opening; detect again if the file has none.
    QList<qce::encoding::EncodingOption> options = m_uncertain.options;
    if (options.isEmpty()) options = qce::encoding::detectEncoding(bytes).options;
    EncodingChoiceDialog dialog(bytes, options, encoding(), m_edit->area()->font(), this);
    if (dialog.exec() != QDialog::Accepted || dialog.chosenEncoding().isEmpty()) return;
    reopenAsking(dialog.chosenEncoding());
}

qce::encoding::FileFormat EditorTab::fileFormat() const
{
    return m_encoding->format();
}

QString EditorTab::encoding() const
{
    return m_encoding->encoding();
}

bool EditorTab::reopenWithEncoding(const QString& encoding)
{
    // Not an undo step: undoing it would bring back text decoded in the
    // other encoding while saving in this one.
    return !m_filePath.isEmpty() && readFile(m_filePath, false, encoding);
}

void EditorTab::setSaveEncoding(const QString& encoding)
{
    m_encoding->setEncoding(encoding);
}

void EditorTab::onDocumentChanged()
{
    // Editing keeps the encoding the file is shown in. Whether the text
    // differs from the file follows the undo history (updateModified()).
    hideEncodingNotice();
}

bool EditorTab::loadFile(const QString& path)
{
    if (QFileInfo::exists(path))
        return readFile(path, true);
    // A name that does not exist yet (e.g. a new file named in a file manager):
    // an empty UTF-8 document, saved there (a .een file asks for a password then).
    forgetPassword();
    m_doc->setText(QString());
    m_savedText.clear();
    m_edit->area()->undoStack()->clear();
    {
        const QSignalBlocker block(m_encoding);
        m_encoding->setFormat({QStringLiteral("utf8"), false, false, true});
    }
    hideEncodingNotice();
    m_uncertain = {};
    m_language = {};
    ++m_languageRequest;
    m_filePath = path;
    m_diskModified = {};
    m_diskSize = -1;
    m_formatChanged = false;
    m_syntaxMode = SyntaxMode::Automatic;
    m_syntaxFile.clear();
    reapplyHighlighter();
    emit statusChanged();
    m_reportedModified = false;
    emit modificationChanged(false);
    return true;
}

bool EditorTab::hasExternalChanges() const
{
    if (m_filePath.isEmpty()) return false;
    const QFileInfo file(m_filePath);
    return file.isFile() && (file.lastModified() != m_diskModified || file.size() != m_diskSize);
}

void EditorTab::updateDiskStamp()
{
    const QFileInfo file(m_filePath);
    m_diskModified = file.lastModified();
    m_diskSize = file.size();
}

bool EditorTab::reloadFromDisk()
{
    if (m_filePath.isEmpty()) return false;
    // Keep the encoding the file was opened in; detect again if it no longer
    // fits. The cursor and the selection follow the text they were in.
    if (!readFile(m_filePath, false, encoding(), true) && !readFile(m_filePath, false, {}, true))
        return false;
    auto* area = m_edit->area();
    const auto cursor = area->cursorPosition();
    const auto anchor = cursor == area->selectionStart() ? area->selectionEnd() : area->selectionStart();
    revealRange(area->selectionStart(), area->selectionEnd());
    area->setSelection(anchor, cursor);
    return true;
}

bool EditorTab::readFile(const QString& path, bool resetSyntax, const QString& encoding, bool asEdit)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QFileInfo stamp(f);
    const auto modified = stamp.lastModified();
    const auto size = stamp.size();
    QByteArray bytes = f.readAll();
    if (f.error() != QFileDevice::NoError) return false;
    m_loadCancelled = false;
    if (!decryptIfEncrypted(path, &bytes)) return false;
    // Code page or UTF, detected unless given; saved back the same way.
    const auto decoded = qce::encoding::decode(bytes, encoding, legacyLanguages());
    if (!decoded.ok) return false;
    // Detected without confidence: shown with the most likely encoding at once,
    // and a notice offers the choice. A given encoding (chosen, or kept on
    // reload) keeps the options for the Reopen menu but needs no notice.
    if (encoding.isEmpty())
        m_uncertain = decoded.detection.confident ? qce::encoding::EncodingDetection() : decoded.detection;
    if (encoding.isEmpty() && !m_uncertain.options.isEmpty())
        showEncodingNotice();
    else
        hideEncodingNotice();

    disconnect(m_doc, nullptr, this, nullptr);
    // With qcodeedit-diff only the lines that differ change, as one undo step.
    m_doc->setText(decoded.text);
    m_savedText = m_doc->toPlainText();
    if (asEdit)
        m_edit->area()->undoStack()->setClean();
    else
        m_edit->area()->undoStack()->clear();
    {
        // Loading is not an edit: no "modified" from the encoding change.
        const QSignalBlocker block(m_encoding);
        m_encoding->setFormat(decoded.format);
    }
    // The language is guessed in the background; the file is shown at once.
    m_language = {};
    const int request = ++m_languageRequest;
    qce::encoding::detectLanguageAsync(bytes, this, [this, request](const qce::encoding::Language& language) {
        if (request != m_languageRequest) return;
        m_language = language;
        applySpellLanguages();
        emit statusChanged();
    });
    emit statusChanged();
    m_formatChanged = false;
    m_filePath = path;
    m_diskModified = modified;
    m_diskSize = size;

    connect(m_doc, &qce::SimpleTextDocument::linesChanged,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesInserted,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesRemoved,
            this, &EditorTab::onDocumentChanged);

    if (resetSyntax) {
        m_syntaxMode = SyntaxMode::Automatic;
        m_syntaxFile.clear();
    }
    reapplyHighlighter();
    m_reportedModified = false;
    emit modificationChanged(false);
    return true;
}

bool EditorTab::save()
{
    if (m_filePath.isEmpty())
        return false;
    return saveAs(m_filePath);
}

bool EditorTab::saveAs(const QString& path)
{
    // In the file's encoding; characters it cannot store bring up a choice
    // (write '?', switch to UTF-8, or cancel the save).
    m_saveCancelled = false;
    trimTrailingWhitespace();
    QByteArray bytes;
    if (!m_encoding->encodeForSave(m_doc->toPlainText(), &bytes)) {
        m_saveCancelled = true;
        return false;
    }
    if (een::isEenPath(path)) {
        // Encrypted, always in the newer (EdiQ) format; a new file asks for a password.
        if (m_eenPassword.isEmpty() && !askNewPassword()) {
            m_saveCancelled = true;
            return false;
        }
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        bytes = een::encrypt(bytes, m_eenPassword);
        QGuiApplication::restoreOverrideCursor();
    } else if (!m_eenPassword.isEmpty()) {
        // The decrypted text would be readable by anyone.
        if (QMessageBox::question(this, tr("Save Without Encryption"),
                                  tr("%1 is not an encrypted .een file: the text will be saved unencrypted. "
                                     "Continue?").arg(QFileInfo(path).fileName()))
            != QMessageBox::Yes) {
            m_saveCancelled = true;
            return false;
        }
        forgetPassword();
    }
    QSaveFile f(path);
    f.setDirectWriteFallback(true);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
        return false;

    const bool renamed = path != m_filePath;
    m_filePath = path;
    updateDiskStamp();
    m_savedText = m_doc->toPlainText();
    m_formatChanged = false;
    m_edit->area()->undoStack()->setClean();
    updateModified();
    if (m_encoding->format().mixedLineBreaks) {
        // Saved with one kind of line break for every line.
        auto format = m_encoding->format();
        format.mixedLineBreaks = false;
        const QSignalBlocker block(m_encoding);
        m_encoding->setFormat(format);
    }
    emit statusChanged();
    if (renamed)
        reapplyHighlighter();
    return true;
}

void EditorTab::applyThemePalette()
{
    QPalette palette = m_defaultPalette;
    if (m_theme.editorBackground.isValid()) {
        palette.setColor(QPalette::Base, m_theme.editorBackground);
        palette.setColor(QPalette::Window, m_theme.editorBackground);
    }
    const auto normal = m_theme.styles.constFind(QStringLiteral("Normal"));
    if (normal != m_theme.styles.constEnd() && normal->fg.isValid()) {
        palette.setColor(QPalette::Text, normal->fg);
        palette.setColor(QPalette::WindowText, normal->fg);
    }
    m_edit->setPalette(palette);
    m_edit->area()->setPalette(palette);
    m_edit->area()->viewport()->setPalette(palette);
    // The line with the caret: the background a little darker (white gives
    // #F8F8F8), or lighter on a dark theme; the theme's tint stays.
    const QColor base = palette.color(QPalette::Base);
    m_edit->area()->setCurrentLineColor(base.lightness() > 128 ? base.darker(103)
                                                               : base.lighter(140));
    m_edit->area()->viewport()->update();
    // Invalid colours (no theme, or missing in it) are derived from the palette.
    m_edit->setGutterColors({m_theme.iconBorder, m_theme.lineNumbers, m_theme.separator});
}

qce::TextAttribute EditorTab::themedAttribute(const QString& style,
                                             const qce::TextAttribute& fallback) const
{
    const auto entry = m_theme.styles.constFind(style);
    if (entry == m_theme.styles.constEnd())
        return fallback;
    return {entry->fg.isValid() ? entry->fg : fallback.foreground,
            entry->bg, entry->bold, entry->italic, entry->underline};
}

void EditorTab::reapplyHighlighter()
{
    if (!m_themeFile.isEmpty()) {
        const auto theme = KateTheme::load(m_themeFile);
        if (theme.isValid())
            m_theme = theme;
    } else {
        // "Default": Breeze Light/Dark to match the desktop, or the editor's
        // built-in black on white when the themes are not downloaded.
        m_theme = KateTheme::loadDefault();
    }
    applyThemePalette();
    if (m_syntaxMode == SyntaxMode::PlainText) {
        clearHighlighter();
    } else if (m_syntaxMode == SyntaxMode::Manual) {
        const auto& index = SyntaxData::index();
        const auto* entry = index.byFile(m_syntaxFile);
        if (!entry || entry->unsupported || !applyKateHighlighter(index.filePath(*entry)))
            clearHighlighter();
    } else {
        applyHighlighterForFile(m_filePath);
    }
}

void EditorTab::applyHighlighterForFile(const QString& path)
{
    // Kate definitions downloaded by qcodeedit, matched by file name and
    // sorted by priority (definitions needing a newer Kate are skipped).
    const auto& index   = SyntaxData::index();
    const auto  matches = index.forFileName(path);
    if (!matches.isEmpty()) {
        if (!applyKateHighlighter(index.filePath(*matches.first())))
            clearHighlighter();
        return;
    }

    // No Kate definition — fall back to built-in programmatic highlighter for C/C++
    if (isCppExtension(QFileInfo(path).suffix())) {
        applyCppHighlighter();
        return;
    }

    clearHighlighter();
}

void EditorTab::applyCppHighlighter()
{
    using namespace qce;

    auto hl = std::make_unique<RulesHighlighter>();

    const int attrKw      = hl->addAttribute(themedAttribute(QStringLiteral("Keyword"), {QColor(0x00,0x00,0xAA), {}, true}));
    const int attrType    = hl->addAttribute(themedAttribute(QStringLiteral("DataType"), {QColor(0x00,0x80,0x80), {}, true}));
    const int attrString  = hl->addAttribute(themedAttribute(QStringLiteral("String"), {QColor(0xC0,0x10,0x10)}));
    const int attrComment = hl->addAttribute(themedAttribute(QStringLiteral("Comment"), {QColor(0x80,0x80,0x80), {}, false, true}));
    const int attrNumber  = hl->addAttribute(themedAttribute(QStringLiteral("DecVal"), {QColor(0x80,0x40,0x00)}));
    const int attrPP      = hl->addAttribute(themedAttribute(QStringLiteral("Preprocessor"), {QColor(0x60,0x00,0x80)}));

    const int klKeywords = hl->addKeywordList({"keywords", {
        "if","else","return","for","while","do","break","continue",
        "switch","case","default","goto","sizeof","alignof","noexcept",
        "try","catch","throw","new","delete","operator","template",
        "typename","namespace","using","class","struct","enum","union",
        "public","protected","private","virtual","override","final",
        "static","inline","explicit","friend","mutable","volatile",
        "extern","register","typedef","nullptr","true","false","this",
    }, true});

    const int klTypes = hl->addKeywordList({"types", {
        "int","void","char","bool","float","double","long","short",
        "unsigned","signed","size_t","auto","const","constexpr",
        "int8_t","int16_t","int32_t","int64_t",
        "uint8_t","uint16_t","uint32_t","uint64_t",
        "wchar_t","char8_t","char16_t","char32_t",
    }, true});

    // Contexts
    HighlightContext ctxN{"Normal",       -1,          -1, 0, false, -1, {}};
    HighlightContext ctxS{"String",       attrString,  -1, 0, false, -1, {}};
    HighlightContext ctxC{"Char",         attrString,  -1, 0, false, -1, {}};
    HighlightContext ctxB{"BlockComment", attrComment, -1, 0, false, -1, {}};
    HighlightContext ctxL{"LineComment",  attrComment,  0, 1, false, -1, {}};
    HighlightContext ctxP{"Preprocessor", attrPP,      0, 1, false, -1, {}};

    const int normal       = hl->addContext(ctxN);
    const int strCtx       = hl->addContext(ctxS);
    const int charCtx      = hl->addContext(ctxC);
    const int blockComment = hl->addContext(ctxB);
    const int lineComment  = hl->addContext(ctxL);
    const int preprocessor = hl->addContext(ctxP);

    // Folding region IDs
    const int rgCurly = hl->regionIdForName("curly");
    const int rgComm  = hl->regionIdForName("Comment");

    auto& norm = hl->contextRef(normal);

    // Preprocessor lines (#include, #define, …)
    norm.rules.push_back({HighlightRule::DetectChar, '#', {}, {}, true, {}, -1, -1,
                          attrPP, preprocessor, 0, true/*firstNonSpace*/, false});
    // Line comment //
    norm.rules.push_back({HighlightRule::Detect2Chars, '/', '/', {}, true, {}, -1, -1,
                          attrComment, lineComment, 0, false, false});
    // Block comment /* — also starts Comment fold region
    {
        HighlightRule r;
        r.kind = HighlightRule::Detect2Chars;
        r.ch = '/'; r.ch1 = '*';
        r.attributeId = attrComment; r.nextContextId = blockComment;
        r.beginRegionId = rgComm;
        norm.rules.push_back(r);
    }
    // String "
    norm.rules.push_back({HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
                          attrString, strCtx, 0, false, false});
    // Char '
    norm.rules.push_back({HighlightRule::DetectChar, '\'', {}, {}, true, {}, -1, -1,
                          attrString, charCtx, 0, false, false});
    // Types (before keywords — both match identifiers)
    norm.rules.push_back({HighlightRule::Keyword, {}, {}, {}, true, {}, klTypes, -1,
                          attrType, -1, 0, false, false});
    // Keywords
    norm.rules.push_back({HighlightRule::Keyword, {}, {}, {}, true, {}, klKeywords, -1,
                          attrKw, -1, 0, false, false});
    // Numbers
    { HighlightRule r; r.kind = HighlightRule::Int;   r.attributeId = attrNumber; norm.rules.push_back(r); }
    { HighlightRule r; r.kind = HighlightRule::Float; r.attributeId = attrNumber; norm.rules.push_back(r); }
    // Opening brace { — starts curly fold region
    {
        HighlightRule r;
        r.kind = HighlightRule::DetectChar; r.ch = '{';
        r.beginRegionId = rgCurly;
        norm.rules.push_back(r);
    }
    // Closing brace } — ends curly fold region
    {
        HighlightRule r;
        r.kind = HighlightRule::DetectChar; r.ch = '}';
        r.endRegionId = rgCurly;
        norm.rules.push_back(r);
    }

    // String context
    auto& str = hl->contextRef(strCtx);
    str.rules.push_back({HighlightRule::HlCStringChar, {}, {}, {}, true, {}, -1, -1,
                         -1, -1, 0, false, false});
    str.rules.push_back({HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
                         attrString, -1, 1, false, false});

    // Char context
    auto& ch = hl->contextRef(charCtx);
    ch.rules.push_back({HighlightRule::HlCStringChar, {}, {}, {}, true, {}, -1, -1,
                        -1, -1, 0, false, false});
    ch.rules.push_back({HighlightRule::DetectChar, '\'', {}, {}, true, {}, -1, -1,
                        attrString, -1, 1, false, false});

    // Block comment context — closing */ ends Comment fold region
    {
        HighlightRule r;
        r.kind = HighlightRule::Detect2Chars; r.ch = '*'; r.ch1 = '/';
        r.attributeId = attrComment; r.nextContextId = -1; r.popCount = 1;
        r.endRegionId = rgComm;
        hl->contextRef(blockComment).rules.push_back(r);
    }

    hl->setInitialContextId(normal);

    // Detach the old provider before its highlighter goes away.
    m_edit->area()->setFoldingProvider(nullptr);
    m_highlighter = std::move(hl);
    m_edit->area()->setHighlighter(m_highlighter.get());

    m_foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(m_highlighter.get());
    m_foldProvider->setPlaceholderFor("curly",   "{…}");
    m_foldProvider->setPlaceholderFor("Comment", "/*…*/");
    m_edit->area()->setFoldingProvider(m_foldProvider.get());
    m_edit->area()->setWordWrap(m_wordWrap);
}

bool EditorTab::applyKateHighlighter(const QString& xmlPath)
{
    // ##Lang includes (e.g. Doxygen inside C++) are resolved through the index.
    auto hl = KateXmlReader::load(xmlPath, m_theme, SyntaxData::index());
    if (!hl) return false;

    // Detach the old provider before its highlighter goes away.
    m_edit->area()->setFoldingProvider(nullptr);
    m_highlighter = std::move(hl);
    m_edit->area()->setHighlighter(m_highlighter.get());

    m_foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(m_highlighter.get());
    m_foldProvider->setPlaceholderFor("Brace1",  "{…}");
    m_foldProvider->setPlaceholderFor("brace",   "{…}");
    m_foldProvider->setPlaceholderFor("curly",   "{…}");
    m_foldProvider->setPlaceholderFor("square",  "[…]");
    m_foldProvider->setPlaceholderFor("paren",   "(…)");
    m_foldProvider->setPlaceholderFor("Comment", "/*…*/");
    m_foldProvider->setPlaceholderFor("Region1", "//BEGIN…END");
    m_edit->area()->setFoldingProvider(m_foldProvider.get());
    m_edit->area()->setWordWrap(m_wordWrap);
    return true;
}

void EditorTab::clearHighlighter()
{
    m_edit->area()->setHighlighter(nullptr);
    m_edit->area()->setFoldingProvider(nullptr);
    m_edit->area()->setWordWrap(m_wordWrap);
    m_highlighter.reset();
    m_foldProvider.reset();
}

} // namespace qceditor
