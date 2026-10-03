#include "mainwindow.h"
#include "editortab.h"
#include <mrutabwidget.h>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFileInfo>
#include <QKeySequence>
#include <QSettings>
#include <QTimer>
#include <qce/CodeEditArea.h>
#include <qce/TextCursor.h>
#include <qce/kate/KateDataDownloader.h>
#include <qce/kate/KateSyntaxVersion.h>
#include "syntaxdata.h"

static const char* kDeclinedKey = "syntaxData/declinedDownload";

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("QCEditor");
    resize(1024, 768);

    m_tabs = new MruTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setTabLimit(20);
    setCentralWidget(m_tabs);

    m_statusPos = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusPos);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto addAction = [&](const QString& text, const QKeySequence& key, auto slot) {
        QAction* a = new QAction(text, this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        fileMenu->addAction(a);
        return a;
    };

    addAction(tr("&New"),        QKeySequence::New,   &MainWindow::newFile);
    addAction(tr("&Open..."),    QKeySequence::Open,  &MainWindow::openFileDialog);
    fileMenu->addSeparator();
    addAction(tr("&Save"),       QKeySequence::Save,  &MainWindow::saveFile);
    addAction(tr("Save &As..."), QKeySequence::SaveAs,&MainWindow::saveFileAs);
    fileMenu->addSeparator();

    {
        QAction* a = new QAction(tr("&Close Tab"), this);
        a->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
        connect(a, &QAction::triggered, this, [this]() {
            if (m_tabs->count() > 0)
                m_tabs->requestCloseTab(currentTab());
        });
        fileMenu->addAction(a);
    }

    fileMenu->addSeparator();
    addAction(tr("E&xit"), QKeySequence::Quit, &MainWindow::close);

    auto* searchMenu = menuBar()->addMenu(tr("&Search"));
    auto* find = searchMenu->addAction(tr("&Find..."));
    find->setShortcut(QKeySequence::Find);
    connect(find, &QAction::triggered, this, [this]() {
        if (auto* tab = currentTab()) tab->showSearch();
    });
    auto* replace = searchMenu->addAction(tr("&Replace..."));
    replace->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(replace, &QAction::triggered, this, [this]() {
        if (auto* tab = currentTab()) tab->showReplace();
    });
    auto* next = searchMenu->addAction(tr("Find &Next"));
    next->setShortcut(QKeySequence(Qt::Key_F3));
    connect(next, &QAction::triggered, this, [this]() {
        if (auto* tab = currentTab()) tab->findNext();
    });
    auto* previous = searchMenu->addAction(tr("Find &Previous"));
    previous->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    connect(previous, &QAction::triggered, this, [this]() {
        if (auto* tab = currentTab()) tab->findNext(true);
    });

    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("&Update Syntax Definitions"),
                         this, &MainWindow::updateSyntaxData);

    connect(m_tabs, &MruTabWidget::tabAboutToClose,
            this, &MainWindow::onTabAboutToClose, Qt::DirectConnection);
    connect(m_tabs, &QTabWidget::currentChanged,
            this, &MainWindow::onCurrentTabChanged);

    // After the window is shown: offer the first download if needed.
    QTimer::singleShot(0, this, &MainWindow::offerSyntaxDownload);
}

qce::kate::KateDataDownloader* MainWindow::downloader()
{
    if (m_downloader)
        return m_downloader;

    m_downloader = new qce::kate::KateDataDownloader(this);
    connect(m_downloader, &qce::kate::KateDataDownloader::progress,
            this, [this](int done, int total) {
        statusBar()->showMessage(tr("Downloading syntax definitions: %1/%2").arg(done).arg(total));
    });
    connect(m_downloader, &qce::kate::KateDataDownloader::finished,
            this, [this](bool ok, int downloaded, int failed) {
        SyntaxData::reload();
        for (int i = 0; i < m_tabs->count(); ++i)
            if (EditorTab* t = tabAt(i))
                t->reapplyHighlighter();
        statusBar()->showMessage(
            ok ? tr("Syntax definitions up to date (%1 files downloaded)").arg(downloaded)
               : tr("Syntax definitions incomplete: %1 downloaded, %2 failed")
                     .arg(downloaded).arg(failed),
            8000);
    });
    return m_downloader;
}

// Ask once on startup when no (complete) data set is present. A "No" is
// remembered; Tools > Update Syntax Definitions stays available.
void MainWindow::offerSyntaxDownload()
{
    if (!downloader()->mustDownload())
        return;
    QSettings settings;
    if (settings.value(kDeclinedKey, false).toBool())
        return;

    const auto answer = QMessageBox::question(
        this, tr("Syntax Definitions"),
        tr("Download Kate syntax definitions %1 and color themes from "
           "kate-editor.org and invent.kde.org?\n\nThey will be stored in:\n%2")
            .arg(qce::kate::supportedSyntaxVersion().toString(),
                 downloader()->dataDir()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        settings.setValue(kDeclinedKey, true);
        return;
    }
    updateSyntaxData();
}

void MainWindow::updateSyntaxData()
{
    QSettings().remove(kDeclinedKey);
    if (downloader()->busy())
        return;
    statusBar()->showMessage(tr("Downloading syntax definitions…"));
    downloader()->start();
}

EditorTab* MainWindow::currentTab() const
{
    return tabAt(m_tabs->currentIndex());
}

EditorTab* MainWindow::tabAt(int index) const
{
    if (index < 0 || index >= m_tabs->count())
        return nullptr;
    return qobject_cast<EditorTab*>(m_tabs->widget(index));
}

EditorTab* MainWindow::createTab(const QString& title)
{
    auto* tab = new EditorTab();
    QString label = title.isEmpty() ? tr("Untitled") : title;
    m_tabs->addTab(tab, label);
    m_tabs->setCurrentWidget(tab);

    connect(tab, &EditorTab::modificationChanged,
            this, &MainWindow::onModificationChanged);

    connect(tab->editor()->area(), &qce::CodeEditArea::cursorPositionChanged,
            this, [this, tab](qce::TextCursor c) {
        if (tab == currentTab())
            m_statusPos->setText(tr("Ln %1  Col %2").arg(c.line + 1).arg(c.column + 1));
    });

    return tab;
}

void MainWindow::newFile()
{
    createTab();
    updateWindowTitle();
}

void MainWindow::openFileDialog()
{
    QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open File"), QString(),
        tr("All Files (*);;Text Files (*.txt);;C/C++ (*.cpp *.cxx *.cc *.c *.h *.hpp)"));

    for (const QString& path : paths)
        openFile(path);
}

void MainWindow::openFile(const QString& path)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorTab* t = tabAt(i);
        if (t && t->filePath() == path) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    EditorTab* tab = createTab();
    if (!tab->loadFile(path)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Cannot open file:\n%1").arg(path));
        m_tabs->requestCloseTab(tab);
        return;
    }

    QString name = QFileInfo(path).fileName();
    int idx = m_tabs->indexOf(tab);
    m_tabs->setTabText(idx, name);
    m_tabs->setTabToolTip(idx, path);
    updateWindowTitle();
}

void MainWindow::saveFile()
{
    EditorTab* tab = currentTab();
    if (!tab) return;

    if (tab->filePath().isEmpty()) {
        saveFileAs();
        return;
    }
    if (!tab->save())
        QMessageBox::critical(this, tr("Error"),
                              tr("Cannot save file:\n%1").arg(tab->filePath()));
    updateWindowTitle();
}

void MainWindow::saveFileAs()
{
    saveTabAs(currentTab());
}

bool MainWindow::saveTabAs(EditorTab* tab)
{
    if (!tab) return false;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Save File As"), tab->filePath(), tr("All Files (*)"));
    if (path.isEmpty()) return false;

    if (!tab->saveAs(path)) {
        QMessageBox::critical(this, tr("Error"),
                              tr("Cannot save file:\n%1").arg(path));
        return false;
    }

    QString name = QFileInfo(path).fileName();
    int idx = m_tabs->indexOf(tab);
    m_tabs->setTabText(idx, name);
    m_tabs->setTabToolTip(idx, path);
    updateWindowTitle();
    return true;
}

bool MainWindow::confirmClose(EditorTab* tab)
{
    if (!tab || !tab->isModified())
        return true;

    QString name = tab->filePath().isEmpty()
                   ? tr("Untitled")
                   : QFileInfo(tab->filePath()).fileName();

    auto answer = QMessageBox::question(
        this, tr("Unsaved Changes"),
        tr("'%1' has unsaved changes. Save before closing?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (answer == QMessageBox::Save) {
        if (tab->filePath().isEmpty()) {
            return saveTabAs(tab);
        }
        return tab->save();
    }
    return answer == QMessageBox::Discard;
}

void MainWindow::onTabAboutToClose(QWidget* page, bool /*askPin*/, bool& allowClose)
{
    if (allowClose)
        allowClose = confirmClose(qobject_cast<EditorTab*>(page));
}

void MainWindow::onCurrentTabChanged(int index)
{
    updateWindowTitle();
    updateStatusBar(tabAt(index));
}

void MainWindow::onModificationChanged(bool /*modified*/)
{
    EditorTab* tab = qobject_cast<EditorTab*>(sender());
    if (!tab) return;

    int idx = m_tabs->indexOf(tab);
    if (idx < 0) return;

    QString name = tab->filePath().isEmpty()
                   ? tr("Untitled")
                   : QFileInfo(tab->filePath()).fileName();
    m_tabs->setTabText(idx, tab->isModified() ? name + "*" : name);
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    EditorTab* tab = currentTab();
    if (!tab) { setWindowTitle("QCEditor"); return; }

    QString name = tab->filePath().isEmpty()
                   ? tr("Untitled")
                   : QFileInfo(tab->filePath()).fileName();
    setWindowTitle(QString("%1%2 — QCEditor").arg(name, tab->isModified() ? "*" : ""));
}

void MainWindow::updateStatusBar(EditorTab* tab)
{
    if (!tab) { m_statusPos->clear(); return; }
    qce::TextCursor c = tab->editor()->area()->cursorPosition();
    m_statusPos->setText(tr("Ln %1  Col %2").arg(c.line + 1).arg(c.column + 1));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (!confirmClose(tabAt(i))) {
            event->ignore();
            return;
        }
    }
    event->accept();
}
