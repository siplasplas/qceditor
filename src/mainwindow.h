#pragma once
#include <QMainWindow>
#include <QJsonObject>
#include <QStringList>

class MruTabWidget;
class EditorTab;
class QLabel;
class QMenu;
namespace qce::kate { class KateDataDownloader; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /// @p recentFilesPath: where the recent files are kept; empty: recent-files.json
    /// in the application's data directory (~/.local/share/siplasplas/qceditor).
    explicit MainWindow(const QString& recentFilesPath = QString(), QWidget* parent = nullptr);

    void openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void newFile();
    void openFileDialog();
    void saveFile();
    void saveFileAs();
    void goToPosition();
    void onTabAboutToClose(QWidget* page, bool askPin, bool& allowClose);
    void onCurrentTabChanged(int index);
    void onModificationChanged(bool modified);
    void updateSyntaxData();
    /// Lets the user choose the languages of files not in UTF-8.
    void chooseLegacyLanguages();

private:
    EditorTab* currentTab() const;
    EditorTab* tabAt(int index) const;
    EditorTab* createTab(const QString& title = QString());
    void       updateWindowTitle();
    void       updateStatusBar(EditorTab* tab);
    bool       confirmClose(EditorTab* tab);
    bool       saveTabAs(EditorTab* tab);
    void       offerSyntaxDownload();
    void       checkExternalChanges();
    void       loadRecentFiles();
    void       saveRecentFiles();
    void       updateRecentFilesMenu();
    bool       addRecentFile(const QString& path);
    qce::kate::KateDataDownloader* downloader();

    bool m_checkingExternalChanges = false;
    QMenu* m_recentFilesMenu = nullptr;
    QStringList m_recentFiles;
    QString m_recentFilesPath;
    QJsonObject m_recentFilesJson;

    MruTabWidget* m_tabs      = nullptr;
    QLabel*       m_statusPos = nullptr;        ///< line:column
    QLabel*       m_statusLineBreaks = nullptr; ///< Unix (LF), Windows (CRLF), Mac (CR), Mixed
    QLabel*       m_statusEncoding = nullptr;   ///< UTF-8 or the code page
    QLabel*       m_statusLanguage = nullptr;   ///< detected in the background
    QLabel*       m_statusTab = nullptr;        ///< tab width in spaces
    /// Positions recently entered in Go to (most recent first); this session only.
    QStringList   m_recentPositions;
    qce::kate::KateDataDownloader* m_downloader = nullptr;
};
