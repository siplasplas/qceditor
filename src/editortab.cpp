#include "editortab.h"
#include "findbar.h"
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QMenu>
#include <QActionGroup>
#include <QMap>
#include <QMessageBox>
#include <algorithm>
#include <qce/CodeEditArea.h>
#include <qce/FoldState.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KatePaths.h>
#include <qce/kate/KateXmlReader.h>
#include "syntaxdata.h"

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
    layout->addWidget(m_edit);

    connect(m_doc, &qce::SimpleTextDocument::linesChanged,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesInserted,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesRemoved,
            this, &EditorTab::onDocumentChanged);
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

void EditorTab::showContextMenu(const QPoint& position)
{
    QMenu menu(this);
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

    menu.exec(m_edit->area()->viewport()->mapToGlobal(position));
}

void EditorTab::onDocumentChanged()
{
    if (!m_modified) {
        m_modified = true;
        emit modificationChanged(true);
    }
}

bool EditorTab::loadFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QString text = in.readAll();

    disconnect(m_doc, nullptr, this, nullptr);
    m_doc->setText(text);
    m_modified = false;
    m_filePath = path;

    connect(m_doc, &qce::SimpleTextDocument::linesChanged,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesInserted,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesRemoved,
            this, &EditorTab::onDocumentChanged);

    m_syntaxMode = SyntaxMode::Automatic;
    m_syntaxFile.clear();
    reapplyHighlighter();
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
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << m_doc->toPlainText();

    const bool renamed = path != m_filePath;
    m_filePath = path;
    m_modified = false;
    emit modificationChanged(false);
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
    m_edit->area()->viewport()->update();
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

    m_highlighter = std::move(hl);
    m_edit->area()->setHighlighter(m_highlighter.get());

    m_foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(m_highlighter.get());
    m_foldProvider->setPlaceholderFor("curly",   "{…}");
    m_foldProvider->setPlaceholderFor("Comment", "/*…*/");
    m_edit->area()->setFoldingProvider(m_foldProvider.get());
    m_edit->area()->setWordWrap(true); // fold gutter requires wrap mode to render arrows
}

bool EditorTab::applyKateHighlighter(const QString& xmlPath)
{
    // ##Lang includes (e.g. Doxygen inside C++) are resolved through the index.
    auto hl = KateXmlReader::load(xmlPath, m_theme, SyntaxData::index());
    if (!hl) return false;

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
    m_edit->area()->setWordWrap(true); // fold gutter requires wrap mode to render arrows
    return true;
}

void EditorTab::clearHighlighter()
{
    m_edit->area()->setHighlighter(nullptr);
    m_edit->area()->setFoldingProvider(nullptr);
    m_edit->area()->setWordWrap(false);
    m_highlighter.reset();
    m_foldProvider.reset();
}
