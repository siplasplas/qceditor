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
    explicit MainWindow(QWidget* parent = nullptr);

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
    QString m_configPath;
    QJsonObject m_config;

    MruTabWidget* m_tabs      = nullptr;
    QLabel*       m_statusPos = nullptr;
    QLabel*       m_statusEncoding = nullptr;
    qce::kate::KateDataDownloader* m_downloader = nullptr;
};
