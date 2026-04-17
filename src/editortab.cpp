#include "editortab.h"
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <qce/CodeEditArea.h>
#include <qce/kate/KateXmlReader.h>

static QString syntaxFileForExtension(const QString& ext)
{
    static const QMap<QString, QString> map = {
        {"cpp", "cpp"},  {"cxx", "cpp"},  {"cc", "cpp"},   {"c", "c"},
        {"h",   "cpp"},  {"hpp", "cpp"},
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

EditorTab::EditorTab(QWidget* parent)
    : QWidget(parent)
{
    m_doc  = new qce::SimpleTextDocument(this);
    m_edit = new qce::CodeEdit(this);
    m_edit->setDocument(m_doc);

    m_lineNumbers = std::make_unique<qce::LineNumberGutter>(m_doc);
    m_lineNumbers->setFont(m_edit->area()->font());
    m_edit->addLeftMargin(m_lineNumbers.get());

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
    QString ext      = QFileInfo(path).suffix();
    QString syntaxName = syntaxFileForExtension(ext);

    if (syntaxName.isEmpty()) {
        m_edit->area()->setHighlighter(nullptr);
        m_highlighter.reset();
        return;
    }

    QStringList searchDirs = {
        "/usr/share/katepart5/syntax",
        "/usr/share/kde4/apps/katepart/syntax",
        "/usr/share/ktexteditor5/syntax",
        QDir::homePath() + "/.local/share/org.kde.syntax-highlighting/syntax",
    };

    QString xmlPath;
    for (const QString& dir : searchDirs) {
        QString candidate = dir + "/" + syntaxName + ".xml";
        if (QFile::exists(candidate)) {
            xmlPath = candidate;
            break;
        }
    }

    if (xmlPath.isEmpty()) {
        m_edit->area()->setHighlighter(nullptr);
        m_highlighter.reset();
        return;
    }

    auto hl = KateXmlReader::load(xmlPath);
    if (!hl) return;

    m_highlighter = std::move(hl);
    m_edit->area()->setHighlighter(m_highlighter.get());
}
