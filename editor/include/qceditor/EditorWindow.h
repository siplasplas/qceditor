#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <mrutabwidget.h>

class QAction;
class QLabel;
class QMenu;
class QTimer;
namespace qce::kate { class KateDataDownloader; }

namespace qceditor {

class EditorTab;

/// What a program chooses for its editor window.
struct EditorWindowOptions
{
    /// Unpinned tabs at most; the least recently used one gives way to a new one.
    int tabLimit = 20;
    /// Default of Tools > Ask to Reload Modified Files: whether a tab with
    /// unsaved edits whose file changed on disk asks to load the disk version
    /// (otherwise it is left alone). The user's choice is kept in the settings.
    bool askToReloadModified = true;
};

/// The editor's window: tabs (MruTabWidget), menus, status bar, recent files,
/// the check for files changed on disk and the syntax data download. Shared
/// by qceditor and other programs, which add their own menus and actions
/// (menu accessors, tabContextMenuRequested, setSaveValidator()).
class EditorWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit EditorWindow(const EditorWindowOptions& options = {}, QWidget* parent = nullptr);
    ~EditorWindow() override;

    /// Opens a file in a new tab, or shows its tab (reloaded first when it is
    /// unmodified and changed on disk). A path that does not exist yet gives
    /// an empty document saved there.
    void openFile(const QString& path);
    void newFile();

    EditorTab* currentTab() const;
    EditorTab* tabAt(int index) const;
    MruTabWidget* tabWidget() const { return m_tabs; }

    /// Saves a tab (asking for a name if it has none); false when it was not
    /// saved (failed, cancelled, or refused by the save validator).
    bool saveTab(EditorTab* tab);
    bool saveTabAs(EditorTab* tab);

    /// Called before a tab is saved; returning false cancels the save (the
    /// validator tells the user why). E.g. gemini-commander checks the syntax
    /// of its config.toml.
    using SaveValidator = std::function<bool(EditorTab* tab, const QString& path)>;
    void setSaveValidator(SaveValidator validator) { m_saveValidator = std::move(validator); }

    QMenu* fileMenu() const { return m_fileMenu; }
    QMenu* searchMenu() const { return m_searchMenu; }
    QMenu* viewMenu() const { return m_viewMenu; }
    QMenu* toolsMenu() const { return m_toolsMenu; }
    QMenu* helpMenu() const { return m_helpMenu; }

    /// Closes the window's tabs, asking about unsaved ones; false when the
    /// user cancelled.
    bool closeAllTabs();

    /// Compares the open files with the disk now: unmodified tabs whose file
    /// changed are reloaded (an undo step); a modified one asks or is left
    /// alone (Tools > Ask to Reload Modified Files). Done by itself when the
    /// window becomes active and every second while it is.
    void checkExternalChanges();

signals:
    /// A tab was saved (e.g. to apply a configuration file).
    void tabSaved(EditorTab* tab, const QString& path);
    /// The tab bar's context menu, after the window's own items, for the
    /// program's items.
    void tabContextMenuRequested(QWidget* page, QMenu* menu);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void createMenus();
    EditorTab* addTab(EditorTab* tab, const QString& path);
    void setTabTitle(EditorTab* tab, const QString& path);
    QString uniqueTabTitle(const QString& path, const EditorTab* except) const;
    void updateTabText(EditorTab* tab);
    void updateWindowTitle();
    void updateStatusBar(EditorTab* tab);
    void showCursorPosition(EditorTab* tab);
    bool confirmClose(EditorTab* tab, bool askPin);
    void onTabAboutToClose(QWidget* page, bool askPin, bool& allowClose);
    void onTabLimitReached(QWidget* page, MruTabWidget::LimitAction& action);
    void extendTabContextMenu(QWidget* page, QMenu* menu);
    bool askToReloadModified() const;
    void openFileDialog();
    void chooseLegacyLanguages();
    void showAbout();

    QStringList recentFiles() const;
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();

    qce::kate::KateDataDownloader* downloader();
    void offerSyntaxDownload();
    void updateSyntaxData();

    template <typename F> void forEachTab(F function) const;

    EditorWindowOptions m_options;
    MruTabWidget* m_tabs = nullptr;
    QMenu* m_fileMenu = nullptr;
    QMenu* m_searchMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QMenu* m_toolsMenu = nullptr;
    QMenu* m_helpMenu = nullptr;
    QMenu* m_recentFilesMenu = nullptr;
    QAction* m_wordWrap = nullptr;
    QAction* m_showWhitespace = nullptr;

    QLabel* m_statusPos = nullptr;        ///< line:column, the column in characters
    QLabel* m_statusCodePoint = nullptr;  ///< U+XXXX of the character after the caret
    QLabel* m_statusLineBreaks = nullptr; ///< Unix (LF), Windows (CRLF), Mac (CR), Mixed
    QLabel* m_statusEncoding = nullptr;   ///< UTF-8 or the code page
    QLabel* m_statusLanguage = nullptr;   ///< detected in the background
    QLabel* m_statusTab = nullptr;        ///< tab width in spaces

    SaveValidator m_saveValidator;
    QTimer* m_fileWatch = nullptr;
    bool m_checkingExternalChanges = false;
    bool m_closingAll = false;
    /// Tab that the tab limit question decided to close; not asked about again.
    QPointer<QWidget> m_limitClosePage;
    qce::kate::KateDataDownloader* m_downloader = nullptr;
};

} // namespace qceditor
