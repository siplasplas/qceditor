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
#include <QFormLayout>
#include <QSpinBox>
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
#include <qce/spell/GrammarReview.h>
#include <qce/spell/SpellingReview.h>
#include <qce/spell/UserDictionaryDialog.h>
#include <QHash>
#include <algorithm>
#include <functional>
#include <qce/CodeEditArea.h>
#include <qce/encoding/Encoding.h>
#include <qce/encoding/EncodingGuard.h>
#include <qce/FoldState.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KatePaths.h>

namespace qceditor {

EditorTab::EditorTab(QWidget* parent)
    : QWidget(parent)
{
    m_doc  = new qce::SimpleTextDocument(this);
    m_edit = new qce::CodeEdit(this);
    m_edit->setDocument(m_doc);
    m_edit->area()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_edit->area(), &QWidget::customContextMenuRequested,
            this, &EditorTab::showContextMenu);

    // A reload changes only the lines that differ, as one undo step
    // (qcodeedit-diff; installed here as well in case the program's own
    // registration was not linked in).
    qce::diff::install();

    // Reads and saves the file in its encoding; its guard keeps typed and
    // pasted text storable in the file's code page.
    m_file = new qce::encoding::TextFile(m_edit->area(), m_doc, this);
    m_file->setReadFilter([this](const QString& path, QByteArray* bytes) {
        return decryptIfEncrypted(path, bytes) ? qce::encoding::TextFile::Result::Ok
                                               : qce::encoding::TextFile::Result::Cancelled;
    });
    m_file->setWriteFilter([this](const QString& path, QByteArray* bytes) {
        return encryptForSave(path, bytes);
    });
    connect(m_file, &qce::encoding::TextFile::modificationChanged, this, &EditorTab::updateModified);
    connect(m_file, &qce::encoding::TextFile::formatChanged, this, &EditorTab::statusChanged);
    // The user switched the encoding (e.g. to UTF-8): the guess is settled.
    connect(m_file->guard(), &qce::encoding::EncodingGuard::encodingChanged,
            this, &EditorTab::hideEncodingNotice);
    // The language is guessed in the background; the file is shown at once.
    connect(m_file, &qce::encoding::TextFile::loaded, this, [this](const QByteArray& content) {
        m_language = {};
        const int request = ++m_languageRequest;
        qce::encoding::detectLanguageAsync(content, this, [this, request](const qce::encoding::Language& language) {
            if (request != m_languageRequest) return;
            m_language = language;
            applySpellLanguages();
            emit statusChanged();
        });
    });

    // Wavy underlines under misspelled words in comments, strings and text;
    // the language follows the detected one until the user chooses.
    m_spelling = new qce::spell::CodeEditSpelling(m_edit->area(), this);
    m_spelling->setEnabled(spellCheckingEnabled());
    m_spelling->setCorrectWhileTyping(correctWhileTyping());
    m_spelling->setGrammarEnabled(grammarCheckingEnabled());
    connect(m_spelling, &qce::spell::CodeEditSpelling::corrected, this,
            [this](const QString& typed, const QString& correction) {
        emit message(tr("Corrected \"%1\" to \"%2\"; Ctrl+Z brings it back").arg(typed, correction));
    });
    applySpellLanguages();
    // Alt+Enter: the corrections of the word at the cursor, without the mouse.
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* corrections = new QShortcut(QKeySequence(Qt::ALT | key), m_edit->area());
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
    layout->addWidget(m_findBar->space());

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

    // The default theme for the desktop until a file chooses a syntax.
    m_syntax = std::make_unique<qce::kate::EditorSyntax>(m_edit);
}

// The editor and its undo stack go only after this object's own part; what
// they report while going must not reach it.
EditorTab::~EditorTab()
{
    disconnect(m_doc, nullptr, this, nullptr);
    disconnect(m_file, nullptr, this, nullptr);
    disconnect(m_file->guard(), nullptr, this, nullptr);
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
    return een::isEenPath(filePath());
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
    const QString name = filePath().isEmpty() ? tr("the file") : QFileInfo(filePath()).fileName();
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
    m_passwordChanged = true;
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
    return m_passwordChanged || m_file->isModified();
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

    using Mode = qce::kate::EditorSyntax::Mode;
    auto* automatic = addChoice(syntaxMenu, tr("Automatic"), m_syntax->mode() == Mode::Automatic);
    connect(automatic, &QAction::triggered, this, [this]() {
        m_syntax->setAutomatic();
        reapplyHighlighter();
    });
    auto* plainText = addChoice(syntaxMenu, tr("Plain Text"), m_syntax->mode() == Mode::PlainText);
    connect(plainText, &QAction::triggered, this, [this]() {
        m_syntax->setPlainText();
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
                m_syntax->mode() == Mode::Manual && m_syntax->syntaxFile() == entry.file);
            connect(action, &QAction::triggered, this, [this, file = entry.file]() {
                // A definition that cannot be loaded leaves the syntax as it was.
                const Mode mode = m_syntax->mode();
                const QString previous = m_syntax->syntaxFile();
                m_syntax->setManual(file);
                m_syntax->apply();
                if (m_edit->area()->highlighter()) return;
                QMessageBox::warning(this, tr("Syntax"), tr("Cannot load syntax definition: %1").arg(file));
                if (mode == Mode::Manual) m_syntax->setManual(previous);
                else if (mode == Mode::PlainText) m_syntax->setPlainText();
                else m_syntax->setAutomatic();
                m_syntax->apply();
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
        action->setChecked(m_syntax->themeFile() == path);
        themeChoices->addAction(action);
        connect(action, &QAction::triggered, this, [this, path]() {
            if (!path.isEmpty() && !KateTheme::load(path).isValid()) {
                QMessageBox::warning(this, tr("Theme"), tr("Cannot load theme: %1").arg(path));
                return;
            }
            m_syntax->setThemeFile(path);
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
        if (withDetection && !uncertainEncodings().isEmpty()) {
            for (const auto& option : uncertainEncodings()) {
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
static const char* kCorrectWhileTypingKey = "spelling/correctWhileTyping";
static const char* kReviewCheckedAtFirstKey = "spelling/reviewCheckedAtFirst";
static const char* kGrammarEnabledKey = "spelling/grammar";
static const char* kReviewBlockLinesKey = "spelling/reviewBlockLines";

namespace {

// A block of lines by its number, shown as its lines ("1001–2000"): Up goes
// to the next block.
class LineBlockSpinBox : public QSpinBox {
public:
    LineBlockSpinBox(int lineCount, QWidget* parent) : QSpinBox(parent), m_lineCount(lineCount) {}
    void setBlockLines(int lines) {
        m_blockLines = qMax(1, lines);
        setMaximum(qMax(0, (m_lineCount - 1) / m_blockLines));
        // The text follows the size even when the number stays.
        const int value = this->value();
        blockSignals(true);
        setValue(value + 1);
        setValue(value);
        blockSignals(false);
        lineEdit()->setText(textFromValue(this->value()));
    }
    int firstLine() const { return value() * m_blockLines; }  // 0-based
    int lastLine() const { return qMin(m_lineCount, firstLine() + m_blockLines) - 1; }

protected:
    QString textFromValue(int value) const override {
        const int first = value * m_blockLines + 1;
        return QStringLiteral("%1–%2").arg(first).arg(qMin(m_lineCount, first + m_blockLines - 1));
    }
    int valueFromText(const QString& text) const override {
        // The first line typed (or the only number): its block.
        const int line = text.section(QChar(u'–'), 0, 0).section(QLatin1Char('-'), 0, 0).trimmed().toInt();
        return qMax(0, line - 1) / m_blockLines;
    }
    QValidator::State validate(QString&, int&) const override { return QValidator::Acceptable; }

private:
    int m_lineCount;
    int m_blockLines = 1000;
};

} // namespace
static const char* kDefaultLanguageKey = "spelling/defaultLanguage";
static constexpr int kRecentSpellingCount = 6;

bool EditorTab::spellCheckingEnabled()
{
    return settings().value(kSpellingEnabledKey, true).toBool();
}

void EditorTab::setSpellCheckingEnabled(bool enabled)
{
    settings().setValue(kSpellingEnabledKey, enabled);
}

bool EditorTab::correctWhileTyping()
{
    return settings().value(kCorrectWhileTypingKey, false).toBool();
}

void EditorTab::setCorrectWhileTyping(bool correct)
{
    settings().setValue(kCorrectWhileTypingKey, correct);
}

bool EditorTab::grammarCheckingEnabled()
{
    return settings().value(kGrammarEnabledKey, true).toBool();
}

void EditorTab::setGrammarCheckingEnabled(bool enabled)
{
    settings().setValue(kGrammarEnabledKey, enabled);
}

QString EditorTab::defaultSpellLanguage()
{
    return settings().value(kDefaultLanguageKey).toString();
}

void EditorTab::setDefaultSpellLanguage(const QString& dictionary)
{
    if (dictionary.isEmpty()) settings().remove(kDefaultLanguageKey);
    else settings().setValue(kDefaultLanguageKey, dictionary);
}

void EditorTab::updateSpellChecking()
{
    m_spelling->setEnabled(spellCheckingEnabled());
    m_spelling->setCorrectWhileTyping(correctWhileTyping());
    m_spelling->setGrammarEnabled(grammarCheckingEnabled());
    applySpellLanguages();
}

QStringList EditorTab::spellingDictionaries() const
{
    return m_spelling->isEnabled() ? m_spelling->dictionaries() : QStringList();
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
    // Chosen, else the detected language, else the default one (a new file,
    // or too little text to tell), else the desktop's.
    QStringList languages = m_spellLanguages;
    if (languages.isEmpty() && !m_language.code.isEmpty()) languages << m_language.code;
    if (languages.isEmpty() && !defaultSpellLanguage().isEmpty()) languages << defaultSpellLanguage();
    if (languages.isEmpty()) languages << QLocale::system().name().section('_', 0, 0);
    m_spelling->setLanguages(languages);
    emit spellingDictionariesChanged();
    if (m_spelling->isReady() || !m_spelling->isEnabled()) return;
    // Said once per language: which dictionary package would add it.
    const QString missing = languages.join(',');
    if (missing == m_spellMissing) return;
    m_spellMissing = missing;
    const QString name = qce::encoding::languageName(languages.front());
    emit message(tr("No spelling dictionary for %1; install one with hunspell, e.g. sudo apt install %2")
                     .arg(name.isEmpty() ? languages.front() : name, qce::spell::packageHint(languages.front())));
}

// "da sie → d asie": a moved space with the words it changes.
QString EditorTab::spaceCorrectionText(const QString& typed, const QString& replacement)
{
    return QStringLiteral("%1 → %2").arg(typed, replacement).replace("&", "&&");
}

bool EditorTab::addSpellingCorrections(QMenu& menu, qce::TextCursor position)
{
    if (!m_spelling->isEnabled()) return false;
    const auto word = m_spelling->misspelledWordAt(position);
    if (!word) return false;
    const QStringList suggestions = m_spelling->suggestions(word->word, 8);
    const auto phrases = m_spelling->phraseCorrections(*word);
    if (suggestions.isEmpty() && phrases.isEmpty())
        menu.addAction(tr("No suggestions"))->setEnabled(false);
    for (const QString& suggestion : suggestions) {
        auto* action = menu.addAction(QString(suggestion).replace("&", "&&"));
        connect(action, &QAction::triggered, this, [this, w = *word, suggestion]() {
            m_spelling->replaceWord(w, suggestion);
        });
    }
    // A space one letter off after the word's own suggestions, shown with
    // both words so that it is not taken for a correction of this word alone.
    for (const auto& phrase : phrases) {
        auto* action = menu.addAction(spaceCorrectionText(phrase.range.word, phrase.replacement));
        connect(action, &QAction::triggered, this, [this, phrase]() {
            m_spelling->replaceWord(phrase.range, phrase.replacement);
        });
    }
    menu.addSeparator();
    connect(menu.addAction(tr("Add to Dictionary")), &QAction::triggered, this,
            [this, w = word->word]() { m_spelling->addToDictionary(w); });
    connect(menu.addAction(tr("Ignore")), &QAction::triggered, this,
            [this, w = word->word]() { m_spelling->ignore(w); });
    return true;
}

bool EditorTab::addGrammarCorrections(QMenu& menu, qce::TextCursor position)
{
    if (!m_spelling->isEnabled()) return false;
    const auto mistake = m_spelling->grammarMistakeAt(position);
    if (!mistake) return false;
    // The problem first, as a title.
    menu.addAction(QString(mistake->issue.message.isEmpty() ? tr("Grammar") : mistake->issue.message)
                       .replace("&", "&&"))->setEnabled(false);
    if (mistake->corrections.isEmpty())
        menu.addAction(tr("No suggestions"))->setEnabled(false);
    for (const auto& [range, replacement] : mistake->corrections) {
        auto* action = menu.addAction(spaceCorrectionText(range.word, replacement));
        connect(action, &QAction::triggered, this, [this, range, replacement]() {
            m_spelling->replaceWord(range, replacement);
        });
    }
    return true;
}

// Alt+Enter: the corrections of the misspelled word (or grammar mistake) at
// the cursor, below it, to choose with the keyboard.
void EditorTab::showSpellingCorrections()
{
    auto* area = m_edit->area();
    QMenu menu(this);
    if (!addSpellingCorrections(menu, area->cursorPosition())
            && !addGrammarCorrections(menu, area->cursorPosition())) {
        emit message(tr("No misspelled word or grammar mistake at the cursor"));
        return;
    }
    const QRect caret = area->inputMethodQuery(Qt::ImCursorRectangle).toRect();
    menu.exec(area->mapToGlobal(caret.bottomLeft()));
}

void EditorTab::showUserDictionary()
{
    qce::spell::UserDictionaryDialog(m_spelling->checker(), this).exec();
}

bool EditorTab::spellingReviewOpen() const
{
    return !m_review.isNull() || !m_grammarReview.isNull();
}

void EditorTab::reviewGrammar()
{
    // One window a tab: a second request brings it to the front.
    if (m_grammarReview) {
        m_grammarReview->raise();
        m_grammarReview->activateWindow();
        return;
    }
    if (m_review) {
        emit message(tr("Close the Review Spelling window first"));
        return;
    }
    m_grammarReview = qce::spell::openGrammarReview(*m_spelling, this);
    if (!m_grammarReview) return;
    connect(m_grammarReview, &qce::spell::GrammarReviewDialog::reviewFinished, this, [this](int corrected) {
        if (corrected > 0) emit message(tr("%n mistake(s) corrected; Ctrl+Z undoes it", nullptr, corrected));
    });
}

void EditorTab::recheckSpelling()
{
    m_spelling->recheck();
}

void EditorTab::reviewSpelling()
{
    // One window a tab: a second request brings it to the front.
    if (m_review) {
        m_review->raise();
        m_review->activateWindow();
        return;
    }
    if (m_grammarReview) {
        emit message(tr("Close the Review Grammar window first"));
        return;
    }
    // What the window checks at first is the program's setting.
    const auto checkedAtFirst =
        qce::spell::checkedAtFirstFromName(settings().value(kReviewCheckedAtFirstKey).toString());
    m_review = qce::spell::openSpellingReview(*m_spelling, checkedAtFirst, this);
    if (!m_review) return;
    connect(m_review, &qce::spell::SpellingReviewDialog::reviewFinished, this,
            [this](int corrected, qce::spell::CheckedAtFirst chosen) {
        settings().setValue(kReviewCheckedAtFirstKey, qce::spell::checkedAtFirstName(chosen));
        if (corrected > 0) emit message(tr("%n word(s) corrected; Ctrl+Z undoes it", nullptr, corrected));
    });
}

void EditorTab::reviewSpellingBlock()
{
    reviewBlock(false);
}

void EditorTab::reviewGrammarBlock()
{
    reviewBlock(true);
}

void EditorTab::reviewBlock(bool grammar)
{
    const auto review = [this, grammar]() { grammar ? reviewGrammar() : reviewSpelling(); };
    if (grammar ? !m_grammarReview.isNull() : !m_review.isNull()) {
        review();
        return;
    }
    qce::CodeEditArea* area = m_edit->area();
    const qce::ITextDocument* doc = area->document();
    if (!doc) return;
    // Lines as the user counts them: not the empty one after a final newline.
    int lineCount = doc->lineCount();
    if (lineCount > 1 && doc->lineAt(lineCount - 1).isEmpty()) --lineCount;

    QDialog ask(this);
    ask.setWindowTitle(grammar ? tr("Review Grammar of Lines") : tr("Review Spelling of Lines"));
    auto* layout = new QVBoxLayout(&ask);
    layout->addWidget(new QLabel(tr("The text has %n line(s).", nullptr, lineCount), &ask));
    auto* form = new QFormLayout;
    auto* size = new QSpinBox(&ask);
    size->setRange(1, 1000000);
    size->setValue(settings().value(kReviewBlockLinesKey, 1000).toInt());
    form->addRow(tr("Lines in a &block:"), size);
    auto* block = new LineBlockSpinBox(lineCount, &ask);
    block->setToolTip(tr("Up: the next block, Down: the one before; Enter reviews it"));
    block->setBlockLines(size->value());
    block->setValue(m_reviewBlock);
    form->addRow(tr("&Lines:"), block);
    layout->addLayout(form);
    connect(size, &QSpinBox::valueChanged, block, [block](int lines) { block->setBlockLines(lines); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("&Review"));
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
    layout->addWidget(buttons);
    // The block chosen last is shown again: Up and Enter go on to the next.
    block->setFocus();
    block->selectAll();
    if (ask.exec() != QDialog::Accepted) return;
    settings().setValue(kReviewBlockLinesKey, size->value());
    m_reviewBlock = block->value();
    // Those lines selected, from their start (in sight), and reviewed; the
    // window has found its words when it opens, so the selection goes then:
    // nothing typed or corrected meanwhile lands on a selected block.
    const int first = block->firstLine(), last = block->lastLine();
    area->setSelection({last, int(doc->lineAt(last).size())}, {first, 0});
    review();
    area->setCursorPosition({first, 0});
}

void EditorTab::addSpellingMenu(QMenu& menu, const QPoint& position)
{
    // customContextMenuRequested gives the click in the viewport's coordinates.
    const qce::TextCursor at = m_edit->area()->cursorAt(position);
    if (addSpellingCorrections(menu, at) || addGrammarCorrections(menu, at))
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

void EditorTab::reopenAsking(const QString& name)
{
    if (filePath().isEmpty()) return;
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
    for (const auto& option : uncertainEncodings())
        options << QStringLiteral("%1 %2 %").arg(option.encoding).arg(qRound(option.confidence * 100));
    m_encodingNoticeText->setText(tr("Encoding uncertain: %1").arg(options.join(QStringLiteral(", "))));
    m_encodingNoticeText->setToolTip(tr("The file is shown as %1, the most likely encoding. "
                                        "Choose… compares the encodings on the lines where they differ.")
                                         .arg(uncertainEncodings().value(0).encoding));
    m_encodingNotice->show();
}

void EditorTab::hideEncodingNotice()
{
    m_encodingNotice->hide();
}

void EditorTab::chooseEncoding()
{
    QFile file(filePath());
    if (filePath().isEmpty() || !file.open(QIODevice::ReadOnly)) return;
    QByteArray bytes = file.readAll();
    if (!decryptIfEncrypted(filePath(), &bytes)) return;
    // The options as detected on opening; detect again if the file has none.
    QList<qce::encoding::EncodingOption> options = uncertainEncodings();
    if (options.isEmpty()) options = qce::encoding::detectEncoding(bytes).options;
    EncodingChoiceDialog dialog(bytes, options, encoding(), m_edit->area()->font(), this);
    if (dialog.exec() != QDialog::Accepted || dialog.chosenEncoding().isEmpty()) return;
    reopenAsking(dialog.chosenEncoding());
}

namespace {

// `text` has "\n" line breaks; those of `format` may be longer.
EditorTab::Statistics textStatistics(const QString& text, const qce::encoding::FileFormat& format, int lines)
{
    EditorTab::Statistics statistics;
    statistics.lines = lines;
    statistics.bytes = qce::encoding::encode(text, format, true).bytes.size();
    for (const QChar ch : text)
        if (!ch.isLowSurrogate()) ++statistics.codePoints;
    const int breaks = int(text.count(QLatin1Char('\n'))) + (format.finalNewline && !text.isEmpty() ? 1 : 0);
    if (format.crlf && !format.cr) statistics.codePoints += breaks;
    if (format.finalNewline && !text.isEmpty()) ++statistics.codePoints;
    return statistics;
}

} // namespace

EditorTab::Statistics EditorTab::fileStatistics() const
{
    const QString text = m_doc->toPlainText();
    return textStatistics(text, fileFormat(), text.isEmpty() ? 0 : m_doc->lineCount());
}

EditorTab::Statistics EditorTab::selectionStatistics() const
{
    auto* area = m_edit->area();
    if (!area->hasSelection()) return {};
    qce::encoding::FileFormat format = fileFormat();
    format.bom = false;
    format.finalNewline = false;
    const auto start = area->selectionStart(), end = area->selectionEnd();
    // A selection ending at the start of a line does not take that line.
    const int lines = end.line - start.line + (end.column > 0 || end.line == start.line ? 1 : 0);
    return textStatistics(area->selectedText(), format, lines);
}

void EditorTab::showStatistics()
{
    const QLocale locale;
    const auto file = fileStatistics();
    const bool selection = m_edit->area()->hasSelection();
    const auto selected = selectionStatistics();
    auto row = [&](const QString& name, qint64 inFile, qint64 inSelection) {
        QString html = QStringLiteral("<tr><td>%1</td><td align=right>&nbsp;&nbsp;%2</td>")
            .arg(name, locale.toString(inFile));
        if (selection) html += QStringLiteral("<td align=right>&nbsp;&nbsp;%1</td>").arg(locale.toString(inSelection));
        return html + QStringLiteral("</tr>");
    };
    QString html = QStringLiteral("<table><tr><td></td><th align=right>%1</th>").arg(tr("File"));
    if (selection) html += QStringLiteral("<th align=right>&nbsp;&nbsp;%1</th>").arg(tr("Selection"));
    html += QStringLiteral("</tr>");
    html += row(tr("Bytes (%1)").arg(encoding()), file.bytes, selected.bytes);
    html += row(tr("Code points"), file.codePoints, selected.codePoints);
    html += row(tr("Lines"), file.lines, selected.lines);
    html += QStringLiteral("</table>");
    QMessageBox box(QMessageBox::Information, tr("Statistics"), html, QMessageBox::Ok, this);
    box.setTextFormat(Qt::RichText);
    box.exec();
}

qce::encoding::FileFormat EditorTab::fileFormat() const
{
    return m_file->format();
}

QString EditorTab::encoding() const
{
    return m_file->encoding();
}

bool EditorTab::reopenWithEncoding(const QString& encoding)
{
    // Not an undo step: undoing it would bring back text decoded in the
    // other encoding while saving in this one.
    if (filePath().isEmpty()) return false;
    m_loadCancelled = false;
    m_file->setLegacyLanguages(legacyLanguages());
    if (!finishRead(m_file->reopen(encoding), false)) return false;
    reapplyHighlighter();
    return true;
}

void EditorTab::setSaveEncoding(const QString& encoding)
{
    m_file->setSaveEncoding(encoding);
}

void EditorTab::onDocumentChanged()
{
    // Editing keeps the encoding the file is shown in. Whether the text
    // differs from the file follows the undo history (updateModified()).
    hideEncodingNotice();
}

bool EditorTab::loadFile(const QString& path)
{
    m_loadCancelled = false;
    const bool exists = QFileInfo::exists(path);
    if (!exists) {
        // A name that does not exist yet (e.g. a new file named in a file
        // manager): an empty UTF-8 document, saved there (a .een file asks
        // for a password then). Nothing to guess its language from.
        forgetPassword();
        m_language = {};
        ++m_languageRequest;
    }
    m_file->setLegacyLanguages(legacyLanguages());
    if (!finishRead(m_file->open(path), exists)) return false;
    m_syntax->setAutomatic();
    reapplyHighlighter();
    return true;
}

bool EditorTab::finishRead(qce::encoding::TextFile::Result result, bool detected)
{
    if (result != qce::encoding::TextFile::Result::Ok) {
        // A cancelled password question needs no error message.
        if (result == qce::encoding::TextFile::Result::Cancelled) m_loadCancelled = true;
        return false;
    }
    // Detected without confidence: shown with the most likely encoding at
    // once, and a notice offers the choice. A given encoding (chosen, or kept
    // on reload) keeps the options for the Reopen menu but needs no notice.
    if (detected && !uncertainEncodings().isEmpty())
        showEncodingNotice();
    else
        hideEncodingNotice();
    m_passwordChanged = false;
    // The words ignored in the file are kept for it.
    m_spelling->setDocumentPath(filePath());
    updateModified();
    emit statusChanged();
    return true;
}

bool EditorTab::hasExternalChanges() const
{
    return m_file->hasExternalChanges();
}

bool EditorTab::reloadFromDisk()
{
    if (filePath().isEmpty()) return false;
    // Keep the encoding the file was opened in; detect again if it no longer
    // fits. The cursor and the selection follow the text they were in.
    m_loadCancelled = false;
    m_file->setLegacyLanguages(legacyLanguages());
    if (!finishRead(m_file->reload(), false))
        return false;
    reapplyHighlighter();
    auto* area = m_edit->area();
    const auto cursor = area->cursorPosition();
    const auto anchor = cursor == area->selectionStart() ? area->selectionEnd() : area->selectionStart();
    revealRange(area->selectionStart(), area->selectionEnd());
    area->setSelection(anchor, cursor);
    return true;
}

bool EditorTab::save()
{
    if (filePath().isEmpty())
        return false;
    return saveAs(filePath());
}

// Encrypted, always in the newer (EdiQ) format; a new file asks for a password.
qce::encoding::TextFile::Result EditorTab::encryptForSave(const QString& path, QByteArray* bytes)
{
    using Result = qce::encoding::TextFile::Result;
    if (een::isEenPath(path)) {
        if (m_eenPassword.isEmpty() && !askNewPassword()) return Result::Cancelled;
        QGuiApplication::setOverrideCursor(Qt::WaitCursor);
        *bytes = een::encrypt(*bytes, m_eenPassword);
        QGuiApplication::restoreOverrideCursor();
    } else if (!m_eenPassword.isEmpty()) {
        // The decrypted text would be readable by anyone.
        if (QMessageBox::question(this, tr("Save Without Encryption"),
                                  tr("%1 is not an encrypted .een file: the text will be saved unencrypted. "
                                     "Continue?").arg(QFileInfo(path).fileName()))
            != QMessageBox::Yes)
            return Result::Cancelled;
        forgetPassword();
    }
    return Result::Ok;
}

bool EditorTab::saveAs(const QString& path)
{
    // In the file's encoding; characters it cannot store bring up a choice
    // (write '?', switch to UTF-8, or cancel the save), and so may the
    // encryption of a .een file.
    m_saveCancelled = false;
    using Trim = qce::encoding::TextFile::TrimWhitespace;
    const TrimWhitespace trim = trimWhitespace();
    m_file->setTrimWhitespace(trim == TrimWhitespace::Off        ? Trim::Off
                              : trim == TrimWhitespace::AllLines ? Trim::AllLines
                                                                 : Trim::ChangedLines);
    const QString previous = filePath();
    const auto result = m_file->saveAs(path);
    if (result != qce::encoding::TextFile::Result::Ok) {
        m_saveCancelled = result == qce::encoding::TextFile::Result::Cancelled;
        return false;
    }
    const bool renamed = path != previous;
    // Saved as another file: the words ignored in the text go with it too.
    if (renamed && !previous.isEmpty()) qce::spell::copySpellingData(previous, path);
    m_spelling->setDocumentPath(path);
    m_passwordChanged = false;
    updateModified();
    emit statusChanged();
    if (renamed)
        reapplyHighlighter();
    return true;
}

void EditorTab::reapplyHighlighter()
{
    // Theme and definition read again: by the file's name unless chosen.
    m_syntax->setFileName(filePath());
    m_syntax->apply();
    // The tab's word wrap (on by default) reaches the area here.
    m_edit->area()->setWordWrap(m_wordWrap);
}

} // namespace qceditor
