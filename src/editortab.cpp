#include "editortab.h"
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <qce/CodeEditArea.h>
#include <qce/FoldState.h>
#include <qce/kate/KateXmlReader.h>

static QString syntaxFileForExtension(const QString& ext)
{
    static const QMap<QString, QString> map = {
        {"c",   "c"},    {"cpp","cpp"},  {"cxx","cpp"},  {"cc","cpp"},
        {"h",   "cpp"},  {"hpp","cpp"},  {"hxx","cpp"},
        {"py",  "python"},
        {"js",  "javascript"},
        {"ts",  "typescript"},
        {"java","java"},
        {"xml", "xml"},  {"html","html"}, {"htm","html"},
        {"css", "css"},
        {"sh",  "bash"},
        {"cmake","cmake"},
        {"md",  "markdown"},
        {"json","json"},
        {"rs",  "rust"},
        {"go",  "go"},
    };
    return map.value(ext.toLower());
}

static bool isCppExtension(const QString& ext)
{
    static const QStringList cpp = {"c","cpp","cxx","cc","h","hpp","hxx"};
    return cpp.contains(ext.toLower());
}

static QStringList kateSyntaxSearchDirs()
{
    return {
        QDir::homePath() + "/.local/share/org.kde.syntax-highlighting/syntax",
        "/usr/share/katepart5/syntax",
        "/usr/share/kde4/apps/katepart/syntax",
        "/usr/share/ktexteditor5/syntax",
    };
}

static QString findKateXml(const QString& syntaxName)
{
    for (const QString& dir : kateSyntaxSearchDirs()) {
        QString path = dir + "/" + syntaxName + ".xml";
        if (QFile::exists(path))
            return path;
    }
    return {};
}

EditorTab::EditorTab(QWidget* parent)
    : QWidget(parent)
{
    m_doc  = new qce::SimpleTextDocument(this);
    m_edit = new qce::CodeEdit(this);
    m_edit->setDocument(m_doc);

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
    layout->addWidget(m_edit);

    connect(m_doc, &qce::SimpleTextDocument::linesChanged,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesInserted,
            this, &EditorTab::onDocumentChanged);
    connect(m_doc, &qce::SimpleTextDocument::linesRemoved,
            this, &EditorTab::onDocumentChanged);
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

    applyHighlighterForFile(path);
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

    m_filePath = path;
    m_modified = false;
    emit modificationChanged(false);
    return true;
}

void EditorTab::applyHighlighterForFile(const QString& path)
{
    QString ext        = QFileInfo(path).suffix().toLower();
    QString syntaxName = syntaxFileForExtension(ext);

    if (syntaxName.isEmpty()) {
        clearHighlighter();
        return;
    }

    QString xmlPath = findKateXml(syntaxName);

    if (!xmlPath.isEmpty()) {
        applyKateHighlighter(xmlPath);
        return;
    }

    // No Kate XML found — fall back to built-in programmatic highlighter for C/C++
    if (isCppExtension(ext)) {
        applyCppHighlighter();
        return;
    }

    clearHighlighter();
}

void EditorTab::applyCppHighlighter()
{
    using namespace qce;

    auto hl = std::make_unique<RulesHighlighter>();

    const int attrKw      = hl->addAttribute({QColor(0x00,0x00,0xAA), {}, true});
    const int attrType    = hl->addAttribute({QColor(0x00,0x80,0x80), {}, true});
    const int attrString  = hl->addAttribute({QColor(0xC0,0x10,0x10)});
    const int attrComment = hl->addAttribute({QColor(0x80,0x80,0x80), {}, false, true});
    const int attrNumber  = hl->addAttribute({QColor(0x80,0x40,0x00)});
    const int attrPP      = hl->addAttribute({QColor(0x60,0x00,0x80)});

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

void EditorTab::applyKateHighlighter(const QString& xmlPath)
{
    auto hl = KateXmlReader::load(xmlPath);
    if (!hl) { clearHighlighter(); return; }

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
}

void EditorTab::clearHighlighter()
{
    m_edit->area()->setHighlighter(nullptr);
    m_edit->area()->setFoldingProvider(nullptr);
    m_edit->area()->setWordWrap(false);
    m_highlighter.reset();
    m_foldProvider.reset();
}
