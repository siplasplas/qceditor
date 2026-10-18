#include <qceditor/EditorWindow.h>
#include <qceditor/EditorTab.h>
#include <qceditor/Settings.h>
#include <qceditor/SyntaxData.h>

#include <qxfiledialog.h>

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <qce/CodeEditArea.h>
#include <qce/CodePoints.h>
#include <qce/encoding/Encoding.h>
#include <qce/kate/KateDataDownloader.h>
#include <qce/kate/KatePaths.h>
#include <qce/kate/KateSyntaxVersion.h>

namespace qceditor {

namespace {

constexpr int kRecentFilesLimit = 20;
const char* kRecentFilesKey = "recentFiles";
const char* kWordWrapKey = "view/wordWrap";
const char* kShowWhitespaceKey = "view/showWhitespace";
const char* kAskReloadKey = "files/askToReloadModified";
const char* kDeclinedKey = "syntaxData/declinedDownload";
// The title a tab has without the modification mark.
const char* kTitleProperty = "qceditorTitle";

QString normalizedFilePath(const QString& path)
{
    const QFileInfo file(path);
    const QString canonical = file.canonicalFilePath();
    return canonical.isEmpty() ? file.absoluteFilePath() : canonical;
}

// The last two components of a path ("dir/file.txt"), for the Ctrl+Tab popup.
QString lastPathComponents(const QString& path)
{
    const QStringList parts = QDir::fromNativeSeparators(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.mid(qMax(0, int(parts.size()) - 2)).join(QLatin1Char('/'));
}

// Status bar texts for the file format reported by qcodeedit-encoding.
QString lineBreaksText(const qce::encoding::FileFormat& format, bool ignoreMixed = false)
{
    if (format.mixedLineBreaks && !ignoreMixed) return QObject::tr("Mixed");
    if (format.cr) return QObject::tr("Mac (CR)");
    if (format.crlf) return QObject::tr("Windows (CRLF)");
    return QObject::tr("Unix (LF)");
}

QString encodingText(const qce::encoding::FileFormat& format)
{
    static const QHash<QString, QString> unicode = {
        {"utf8", "UTF-8"}, {"utf16", "UTF-16LE"}, {"utf16be", "UTF-16BE"},
        {"utf32", "UTF-32LE"}, {"utf32be", "UTF-32BE"}};
    const QString name = unicode.value(format.encoding.toLower(), format.encoding);
    return format.bom ? name + " BOM" : name;
}

} // namespace

template <typename F>
void EditorWindow::forEachTab(F function) const
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (EditorTab* tab = tabAt(i)) function(tab);
}

EditorWindow::EditorWindow(const EditorWindowOptions& options, QWidget* parent)
    : QMainWindow(parent), m_options(options)
{
    resize(1024, 768);
    setMinimumSize(600, 400);

    m_tabs = new MruTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setTabLimit(m_options.tabLimit);
    setCentralWidget(m_tabs);

    // Status bar: line:column | code point | line breaks | encoding | language | tab width.
    for (QLabel** label : {&m_statusPos, &m_statusCodePoint, &m_statusLineBreaks, &m_statusEncoding,
                           &m_statusLanguage, &m_statusTab}) {
        *label = new QLabel(this);
        statusBar()->addPermanentWidget(*label);
    }
    m_statusCodePoint->setTextInteractionFlags(Qt::TextSelectableByMouse);

    createMenus();

    // Both write to their reference argument, so they need direct connections.
    connect(m_tabs, &MruTabWidget::tabAboutToClose,
            this, &EditorWindow::onTabAboutToClose, Qt::DirectConnection);
    connect(m_tabs, &MruTabWidget::tabLimitReached,
            this, &EditorWindow::onTabLimitReached, Qt::DirectConnection);
    connect(m_tabs, &MruTabWidget::tabClosing, this, [this](QWidget* page) {
        if (m_closingAll) return;  // closeAllTabs() records them in its own order
        if (auto* tab = qobject_cast<EditorTab*>(page)) addRecentFile(tab->filePath());
    });
    connect(m_tabs, &MruTabWidget::tabContextMenuRequested,
            this, &EditorWindow::extendTabContextMenu);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        updateWindowTitle();
        updateStatusBar(tabAt(index));
    });
    updateWindowTitle();
    updateStatusBar(nullptr);

    // Files changed by other programs: checked when the window becomes active
    // and every second while it is.
    m_fileWatch = new QTimer(this);
    m_fileWatch->setInterval(1000);
    connect(m_fileWatch, &QTimer::timeout, this, [this]() {
        if (isActiveWindow()) checkExternalChanges();
    });
    m_fileWatch->start();

    // After the window is shown: offer the first download if needed.
    QTimer::singleShot(0, this, &EditorWindow::offerSyntaxDownload);
}

EditorWindow::~EditorWindow() = default;

void EditorWindow::createMenus()
{
    m_fileMenu = menuBar()->addMenu(tr("&File"));
    auto addAction = [this](QMenu* menu, const QString& text, const QKeySequence& key, auto slot) {
        QAction* action = menu->addAction(text);
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    addAction(m_fileMenu, tr("&New"), QKeySequence::New, &EditorWindow::newFile);
    addAction(m_fileMenu, tr("&Open..."), QKeySequence::Open, &EditorWindow::openFileDialog);
    m_recentFilesMenu = m_fileMenu->addMenu(tr("Recent &Files"));
    m_recentFilesMenu->setObjectName("recentFilesMenu");
    updateRecentFilesMenu();
    connect(m_recentFilesMenu, &QMenu::aboutToShow, this, &EditorWindow::updateRecentFilesMenu);
    m_fileMenu->addSeparator();
    addAction(m_fileMenu, tr("&Save"), QKeySequence::Save, [this]() { saveTab(currentTab()); });
    addAction(m_fileMenu, tr("Save &As..."), QKeySequence::SaveAs, [this]() { saveTabAs(currentTab()); });
    // For encrypted .een files; the new password is used when the file is saved.
    auto* changePassword = m_fileMenu->addAction(tr("Change &Password..."), this, [this]() {
        if (auto* tab = currentTab()) tab->changePassword();
    });
    connect(m_fileMenu, &QMenu::aboutToShow, this, [this, changePassword]() {
        changePassword->setEnabled(currentTab() && currentTab()->isEncrypted());
    });
    m_fileMenu->addSeparator();
    addAction(m_fileMenu, tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), [this]() {
        if (m_tabs->count() > 0) m_tabs->requestCloseTab(m_tabs->currentIndex());
    });
    m_fileMenu->addSeparator();
    addAction(m_fileMenu, tr("E&xit"), QKeySequence::Quit, &EditorWindow::close);

    m_searchMenu = menuBar()->addMenu(tr("&Search"));
    addAction(m_searchMenu, tr("&Find..."), QKeySequence::Find, [this]() {
        if (auto* tab = currentTab()) tab->showSearch();
    });
    auto* replace = addAction(m_searchMenu, tr("&Replace..."), QKeySequence(Qt::CTRL | Qt::Key_R), [this]() {
        if (auto* tab = currentTab()) tab->showReplace();
    });
    replace->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::CTRL | Qt::Key_H)});
    addAction(m_searchMenu, tr("Find &Next"), QKeySequence(Qt::Key_F3), [this]() {
        if (auto* tab = currentTab()) tab->findNext();
    });
    addAction(m_searchMenu, tr("Find &Previous"), QKeySequence(Qt::SHIFT | Qt::Key_F3), [this]() {
        if (auto* tab = currentTab()) tab->findNext(true);
    });
    m_searchMenu->addSeparator();
    addAction(m_searchMenu, tr("&Go to Line/Column..."), QKeySequence(Qt::CTRL | Qt::Key_G), [this]() {
        if (auto* tab = currentTab()) tab->goToPosition();
    });

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_wordWrap = m_viewMenu->addAction(tr("&Wrap Lines"));
    m_wordWrap->setCheckable(true);
    m_wordWrap->setChecked(settings().value(kWordWrapKey, true).toBool());
    connect(m_wordWrap, &QAction::toggled, this, [this](bool wrap) {
        forEachTab([wrap](EditorTab* tab) { tab->setWordWrap(wrap); });
        settings().setValue(kWordWrapKey, wrap);
    });
    m_showWhitespace = m_viewMenu->addAction(tr("Show &Special Characters"));
    m_showWhitespace->setCheckable(true);
    m_showWhitespace->setChecked(settings().value(kShowWhitespaceKey, false).toBool());
    connect(m_showWhitespace, &QAction::toggled, this, [this](bool show) {
        forEachTab([show](EditorTab* tab) { tab->setShowWhitespace(show); });
        settings().setValue(kShowWhitespaceKey, show);
    });

    m_toolsMenu = menuBar()->addMenu(tr("&Tools"));
    m_toolsMenu->addAction(tr("Insert &Date"), this, [this]() {
        if (auto* tab = currentTab()) tab->insertDate();
    });
    m_toolsMenu->addAction(tr("Insert &Time"), this, [this]() {
        if (auto* tab = currentTab()) tab->insertTime();
    });
    m_toolsMenu->addAction(tr("Insert &Both Date and Time"), this, [this]() {
        if (auto* tab = currentTab()) tab->insertDateTime();
    });
    m_toolsMenu->addSeparator();
    m_toolsMenu->addAction(tr("&Update Syntax Definitions"), this, &EditorWindow::updateSyntaxData);
    m_toolsMenu->addAction(tr("&Languages of Files Not in UTF-8..."), this, &EditorWindow::chooseLegacyLanguages);
    m_toolsMenu->addSeparator();
    auto* spelling = m_toolsMenu->addAction(tr("Check &Spelling"));
    spelling->setCheckable(true);
    spelling->setChecked(EditorTab::spellCheckingEnabled());
    connect(spelling, &QAction::toggled, this, [this](bool enabled) {
        EditorTab::setSpellCheckingEnabled(enabled);
        forEachTab([](EditorTab* tab) { tab->updateSpellChecking(); });
    });
    auto* askReload = m_toolsMenu->addAction(tr("&Ask to Reload Modified Files"));
    askReload->setCheckable(true);
    askReload->setChecked(askToReloadModified());
    askReload->setToolTip(tr("A file with unsaved edits that changed on disk asks whether to load the "
                             "disk version; otherwise it is left as it is"));
    connect(askReload, &QAction::toggled, this, [](bool ask) { settings().setValue(kAskReloadKey, ask); });

    m_helpMenu = menuBar()->addMenu(tr("&Help"));
    m_helpMenu->addAction(tr("&About"), this, &EditorWindow::showAbout);
}

// --- Tabs ------------------------------------------------------------------

EditorTab* EditorWindow::currentTab() const
{
    return tabAt(m_tabs->currentIndex());
}

EditorTab* EditorWindow::tabAt(int index) const
{
    if (index < 0 || index >= m_tabs->count())
        return nullptr;
    return qobject_cast<EditorTab*>(m_tabs->widget(index));
}

EditorTab* EditorWindow::addTab(EditorTab* tab, const QString& path)
{
    tab->setWordWrap(m_wordWrap->isChecked());
    tab->setShowWhitespace(m_showWhitespace->isChecked());
    const int index = m_tabs->addTab(tab, QString());
    setTabTitle(tab, path);
    m_tabs->setCurrentIndex(index);

    connect(tab, &EditorTab::modificationChanged, this, [this, tab]() {
        updateTabText(tab);
        if (tab == currentTab()) updateWindowTitle();
    });
    connect(tab->editor()->area(), &qce::CodeEditArea::cursorPositionChanged, this, [this, tab]() {
        if (tab == currentTab()) showCursorPosition(tab);
    });
    connect(tab, &EditorTab::statusChanged, this, [this, tab]() {
        if (tab == currentTab()) updateStatusBar(tab);
    });
    connect(tab, &EditorTab::message, this, [this](const QString& text) {
        statusBar()->showMessage(text, 10000);
    });
    updateWindowTitle();
    updateStatusBar(tab);
    tab->editor()->area()->setFocus();
    return tab;
}

// The file name, made unique among the tabs ("name (2)" for another file of
// that name); "Untitled-N" without a file.
QString EditorWindow::uniqueTabTitle(const QString& path, const EditorTab* except) const
{
    auto taken = [this, except](const QString& title, const QString& forPath) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            const EditorTab* tab = tabAt(i);
            if (!tab || tab == except) continue;
            if (m_tabs->widget(i)->property(kTitleProperty).toString() == title
                && (forPath.isEmpty() || tab->filePath() != forPath))
                return true;
        }
        return false;
    };
    if (path.isEmpty()) {
        for (int counter = 1;; ++counter) {
            const QString candidate = tr("Untitled-%1").arg(counter);
            if (!taken(candidate, QString())) return candidate;
        }
    }
    const QString base = QFileInfo(path).fileName();
    QString title = base;
    for (int counter = 2; taken(title, path); ++counter)
        title = QStringLiteral("%1 (%2)").arg(base).arg(counter);
    return title;
}

void EditorWindow::setTabTitle(EditorTab* tab, const QString& path)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0) return;
    const QString title = uniqueTabTitle(path, tab);
    tab->setProperty(kTitleProperty, title);
    m_tabs->setTabToolTip(index, path);
    // The Ctrl+Tab popup shows the last two path components (e.g. dir/file.txt).
    m_tabs->setTabPopupText(index, path.isEmpty() ? title : lastPathComponents(path));
    updateTabText(tab);
}

void EditorWindow::updateTabText(EditorTab* tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0) return;
    const QString title = tab->property(kTitleProperty).toString();
    m_tabs->setTabText(index, tab->isModified() ? title + QLatin1Char('*') : title);
}

void EditorWindow::newFile()
{
    // Room first, so that Cancel in the tab limit question opens nothing.
    if (!m_tabs->makeRoomForNewTab()) return;
    addTab(new EditorTab(), QString());
}

void EditorWindow::openFile(const QString& requestedPath)
{
    const QString path = normalizedFilePath(requestedPath);
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorTab* tab = tabAt(i);
        if (tab && !tab->filePath().isEmpty() && normalizedFilePath(tab->filePath()) == path) {
            m_tabs->setCurrentIndex(i);
            if (!tab->isModified() && tab->hasExternalChanges()) tab->reloadFromDisk();
            return;
        }
    }
    if (!m_tabs->makeRoomForNewTab()) return;
    auto* tab = new EditorTab();
    if (!tab->loadFile(path)) {
        // Cancelled (e.g. the password question) needs no error.
        if (!tab->loadCancelled())
            QMessageBox::critical(this, tr("Error"), tr("Cannot open file:\n%1").arg(path));
        delete tab;
        return;
    }
    addTab(tab, path);
}

void EditorWindow::openFileDialog()
{
    const QStringList paths = QxFileDialog::getOpenFileNames(
        this, tr("Open File"), QString(),
        tr("All Files (*);;Text Files (*.txt);;C/C++ (*.cpp *.cxx *.cc *.c *.h *.hpp)"), recentFiles());
    for (const QString& path : paths)
        openFile(path);
}

bool EditorWindow::saveTab(EditorTab* tab)
{
    if (!tab) return false;
    if (tab->filePath().isEmpty()) return saveTabAs(tab);
    if (m_saveValidator && !m_saveValidator(tab, tab->filePath())) return false;
    if (!tab->save()) {
        if (!tab->saveCancelled())
            QMessageBox::critical(this, tr("Error"), tr("Cannot save file:\n%1").arg(tab->filePath()));
        return false;
    }
    updateWindowTitle();
    emit tabSaved(tab, tab->filePath());
    return true;
}

bool EditorWindow::saveTabAs(EditorTab* tab)
{
    if (!tab) return false;
    const QString path = QxFileDialog::getSaveFileName(this, tr("Save File As"), tab->filePath(),
                                                      tr("All Files (*)"));
    if (path.isEmpty()) return false;
    if (m_saveValidator && !m_saveValidator(tab, path)) return false;
    if (!tab->saveAs(path)) {
        if (!tab->saveCancelled())
            QMessageBox::critical(this, tr("Error"), tr("Cannot save file:\n%1").arg(path));
        return false;
    }
    setTabTitle(tab, tab->filePath());
    updateWindowTitle();
    emit tabSaved(tab, tab->filePath());
    return true;
}

// Unsaved edits, or a file deleted on disk while open (its text would be lost).
bool EditorWindow::confirmClose(EditorTab* tab, bool askPin)
{
    if (!tab) return true;
    const QString name = tab->property(kTitleProperty).toString();
    const bool deleted = !tab->filePath().isEmpty() && !QFileInfo::exists(tab->filePath());
    if (!tab->isModified() && !deleted) return true;
    QString message = tab->isModified()
        ? tr("'%1' has unsaved changes. Save before closing?").arg(name)
        : tr("'%1' has been deleted on disk. Save it back before closing?").arg(name);
    if (askPin)
        message = tr("The tab limit closes the least recently used tab.") + QStringLiteral("\n\n") + message
                  + QStringLiteral("\n") + tr("(Cancel keeps the tab open and pins it.)");
    const auto answer = QMessageBox::question(
        this, tab->isModified() ? tr("Unsaved Changes") : tr("File Deleted"), message,
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) return saveTab(tab);
    return answer == QMessageBox::Discard;
}

void EditorWindow::onTabAboutToClose(QWidget* page, bool askPin, bool& allowClose)
{
    if (!allowClose) return;
    if (page == m_limitClosePage) {
        // Already decided in the tab limit question.
        m_limitClosePage = nullptr;
        return;
    }
    allowClose = confirmClose(qobject_cast<EditorTab*>(page), askPin);
}

void EditorWindow::onTabLimitReached(QWidget* page, MruTabWidget::LimitAction& action)
{
    auto* tab = qobject_cast<EditorTab*>(page);
    const bool deleted = tab && !tab->filePath().isEmpty() && !QFileInfo::exists(tab->filePath());
    if (!tab || (!tab->isModified() && !deleted)) {
        // Nothing would be lost: close it without asking.
        m_limitClosePage = page;
        return;
    }
    // Show the tab in question, so the user sees what is being decided about.
    m_tabs->setCurrentWidget(page);
    const QString name = tab->property(kTitleProperty).toString();
    const QString reason = tab->isModified() ? tr("\"%1\" has unsaved changes.").arg(name)
                                             : tr("\"%1\" has been deleted on disk.").arg(name);
    QMessageBox box(QMessageBox::Question, tr("Too Many Tabs"),
                    tr("The new tab needs room: the least recently used tab has to close.\n\n%1").arg(reason),
                    QMessageBox::NoButton, this);
    QPushButton* save = box.addButton(tr("&Save and Close"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(tr("Close &Without Saving"), QMessageBox::DestructiveRole);
    QPushButton* keep = box.addButton(tr("&Keep Open (Pin)"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(save);
    box.exec();
    QAbstractButton* clicked = box.clickedButton();
    if (clicked == keep)
        action = MruTabWidget::LimitAction::Keep;
    else if (clicked == discard || (clicked == save && saveTab(tab)))
        m_limitClosePage = page;
    else
        // Cancel, Escape, or saving failed: the new tab is not opened.
        action = MruTabWidget::LimitAction::Cancel;
}

void EditorWindow::extendTabContextMenu(QWidget* page, QMenu* menu)
{
    menu->addSeparator();
    menu->addAction(tr("Close Unmodified Tabs"), this, [this]() {
        for (int i = m_tabs->count() - 1; i >= 0; --i)
            if (EditorTab* tab = tabAt(i); tab && !tab->isModified()) m_tabs->requestCloseTab(i);
    });
    if (auto* tab = qobject_cast<EditorTab*>(page); tab && !tab->filePath().isEmpty()) {
        const QString path = tab->filePath();
        menu->addAction(tr("Copy File Name"), this, [path]() {
            QGuiApplication::clipboard()->setText(QFileInfo(path).fileName());
        });
        menu->addAction(tr("Copy Path"), this, [path]() { QGuiApplication::clipboard()->setText(path); });
    }
    emit tabContextMenuRequested(page, menu);
}

bool EditorWindow::closeAllTabs()
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (!confirmClose(tabAt(i), false)) return false;
    // Recorded only once every question was answered; the active file first.
    EditorTab* active = currentTab();
    forEachTab([this, active](EditorTab* tab) {
        if (tab != active) addRecentFile(tab->filePath());
    });
    if (active) addRecentFile(active->filePath());
    // Already asked: the tabs close without asking again.
    QScopedValueRollback<bool> closing(m_closingAll, true);
    for (int i = m_tabs->count() - 1; i >= 0; --i) {
        m_limitClosePage = m_tabs->widget(i);
        m_tabs->requestCloseTab(i);
    }
    m_limitClosePage = nullptr;
    return true;
}

void EditorWindow::closeEvent(QCloseEvent* event)
{
    if (closeAllTabs()) event->accept();
    else event->ignore();
}

// --- Window title and status bar ---------------------------------------------

// Qt adds the application's display name, when the program has set one.
void EditorWindow::updateWindowTitle()
{
    EditorTab* tab = currentTab();
    if (!tab) {
        setWindowTitle(QString());
        return;
    }
    QString title = tab->filePath().isEmpty() ? tab->property(kTitleProperty).toString() : tab->filePath();
    if (tab->isModified()) title += QStringLiteral(" *");
    setWindowTitle(title);
}

// Line and column, the column in characters (code points) as Go to takes it.
void EditorWindow::showCursorPosition(EditorTab* tab)
{
    const qce::TextCursor c = tab->editor()->area()->cursorPosition();
    const int column = qce::codepoints::codePointColumn(tab->document()->lineAt(c.line), c.column);
    m_statusPos->setText(QString("%1:%2").arg(c.line + 1).arg(column + 1));
    m_statusCodePoint->setText(tab->codePointAtCursor());
}

void EditorWindow::updateStatusBar(EditorTab* tab)
{
    if (!tab) {
        for (QLabel* label : {m_statusPos, m_statusCodePoint, m_statusLineBreaks, m_statusEncoding,
                              m_statusLanguage, m_statusTab})
            label->clear();
        return;
    }
    showCursorPosition(tab);
    const auto format = tab->fileFormat();
    m_statusLineBreaks->setText(lineBreaksText(format));
    // A mixed file is saved with its most frequent kind of line break.
    m_statusLineBreaks->setToolTip(format.mixedLineBreaks
        ? tr("Mixed line breaks; saving uses %1").arg(lineBreaksText(format, true))
        : QString());
    m_statusEncoding->setText(encodingText(format));
    m_statusLanguage->setText(tab->languageName());
    m_statusLanguage->setToolTip(tab->languageConfidence() > 0
        ? tr("Language detected from the text (%1 %)").arg(qRound(tab->languageConfidence() * 100))
        : tr("Language detected from the text"));
    m_statusTab->setText(tr("Tab: %1").arg(tab->editor()->area()->tabWidth()));
}

// --- Files changed on disk ---------------------------------------------------

void EditorWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        checkExternalChanges();
}

bool EditorWindow::askToReloadModified() const
{
    return settings().value(kAskReloadKey, m_options.askToReloadModified).toBool();
}

// An unmodified tab is reloaded at once (an undo step); a modified one asks,
// or is left alone, as Tools > Ask to Reload Modified Files says. After No it
// is not asked again until the file changes once more.
void EditorWindow::checkExternalChanges()
{
    if (m_checkingExternalChanges) return;
    QScopedValueRollback<bool> checking(m_checkingExternalChanges, true);
    bool reloaded = false;
    for (int i = 0; i < m_tabs->count(); ++i) {
        QPointer<EditorTab> tab = tabAt(i);
        if (!tab || !tab->hasExternalChanges()) continue;
        if (tab->isModified()) {
            if (!askToReloadModified()) continue;
            m_tabs->setCurrentWidget(tab);
            const auto answer = QMessageBox::question(this, tr("File Changed on Disk"),
                tr("%1 changed on disk and has unsaved edits. Load the disk version? "
                   "(Ctrl+Z brings the edits back.)").arg(tab->filePath()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (!tab) continue;
            if (answer != QMessageBox::Yes) {
                tab->ignoreExternalChanges();
                continue;
            }
        }
        if (tab->reloadFromDisk()) reloaded = true;
        else statusBar()->showMessage(tr("Cannot reload file: %1").arg(tab->filePath()), 8000);
    }
    if (reloaded) {
        updateWindowTitle();
        updateStatusBar(currentTab());
    }
}

// --- Recent files ------------------------------------------------------------

QStringList EditorWindow::recentFiles() const
{
    return settings().value(kRecentFilesKey).toStringList();
}

void EditorWindow::addRecentFile(const QString& path)
{
    if (path.isEmpty()) return;
    const QString normalized = normalizedFilePath(path);
    QStringList files = recentFiles();
    files.removeAll(normalized);
    files.prepend(normalized);
    while (files.size() > kRecentFilesLimit) files.removeLast();
    settings().setValue(kRecentFilesKey, files);
}

void EditorWindow::updateRecentFilesMenu()
{
    m_recentFilesMenu->clear();
    const QStringList files = recentFiles();
    if (files.isEmpty()) {
        m_recentFilesMenu->addAction(tr("No recently closed files"))->setEnabled(false);
        return;
    }
    for (const QString& path : files) {
        auto* action = m_recentFilesMenu->addAction(QString(path).replace("&", "&&"));
        action->setData(path);
        action->setToolTip(path);
        action->setEnabled(QFileInfo(path).isFile());
        connect(action, &QAction::triggered, this, [this, path]() { openFile(path); });
    }
}

// --- Tools ---------------------------------------------------------------------

qce::kate::KateDataDownloader* EditorWindow::downloader()
{
    if (m_downloader) return m_downloader;
    m_downloader = new qce::kate::KateDataDownloader(this);
    connect(m_downloader, &qce::kate::KateDataDownloader::progress, this, [this](int done, int total) {
        statusBar()->showMessage(tr("Downloading syntax definitions: %1/%2").arg(done).arg(total));
    });
    connect(m_downloader, &qce::kate::KateDataDownloader::finished,
            this, [this](bool ok, int downloaded, int failed) {
        SyntaxData::reload();
        forEachTab([](EditorTab* tab) { tab->reapplyHighlighter(); });
        statusBar()->showMessage(
            ok ? tr("Syntax definitions up to date (%1 files downloaded)").arg(downloaded)
               : tr("Syntax definitions incomplete: %1 downloaded, %2 failed").arg(downloaded).arg(failed),
            8000);
    });
    return m_downloader;
}

// Asked once when no (complete) data set is present. A "No" is remembered;
// Tools > Update Syntax Definitions stays available.
void EditorWindow::offerSyntaxDownload()
{
    if (!downloader()->mustDownload() || settings().value(kDeclinedKey, false).toBool())
        return;
    const auto answer = QMessageBox::question(
        this, tr("Syntax Definitions"),
        tr("Download Kate syntax definitions %1 and color themes from "
           "kate-editor.org and invent.kde.org?\n\nThey will be stored in:\n%2")
            .arg(qce::kate::supportedSyntaxVersion().toString(), downloader()->dataDir()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        settings().setValue(kDeclinedKey, true);
        return;
    }
    updateSyntaxData();
}

void EditorWindow::updateSyntaxData()
{
    settings().remove(kDeclinedKey);
    if (downloader()->busy()) return;
    statusBar()->showMessage(tr("Downloading syntax definitions…"));
    downloader()->start();
}

// Only the code pages of the chosen languages are considered for files not in
// UTF-8: faster, and a few national letters in comments are not misread as
// another language. None chosen: all languages.
void EditorWindow::chooseLegacyLanguages()
{
    const auto languages = qce::encoding::availableLanguages();
    if (languages.isEmpty()) {
        QMessageBox::information(this, tr("Languages of Files Not in UTF-8"), tr("No language data is available."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Languages of Files Not in UTF-8"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(tr("Files not in UTF-8 are read in a code page of the checked languages "
                                "(e.g. Polish: cp1250, ISO 8859-2, cp852, …). With none checked, the "
                                "language is guessed from the text. Applies to files opened from now on."),
                             &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* list = new QListWidget(&dialog);
    const QStringList chosen = EditorTab::legacyLanguages();
    for (const auto& language : languages) {
        auto* item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(language.name, language.code), list);
        item->setData(Qt::UserRole, language.code);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(chosen.contains(language.code) ? Qt::Checked : Qt::Unchecked);
    }
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(420, 480);
    if (dialog.exec() != QDialog::Accepted) return;
    QStringList result;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            result << list->item(i)->data(Qt::UserRole).toString();
    EditorTab::setLegacyLanguages(result);
}

void EditorWindow::showAbout()
{
    QString text = QGuiApplication::applicationDisplayName();
    if (!QCoreApplication::applicationVersion().isEmpty())
        text += QLatin1Char(' ') + QCoreApplication::applicationVersion();
    text += QLatin1Char('\n') + tr("Qt version: %1").arg(QString::fromUtf8(qVersion()));
    text += QLatin1Char('\n') + tr("Editor: qceditor with qcodeedit");
    text += QLatin1Char('\n') + tr("Syntax definitions: %1").arg(qce::kate::dataDir());
    QMessageBox::about(this, tr("About"), text);
}

} // namespace qceditor
