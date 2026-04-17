#pragma once
#include <QMainWindow>

class MruTabWidget;
class EditorTab;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newFile();
    void openFileDialog();
    void saveFile();
    void saveFileAs();
    void onTabAboutToClose(int index, bool askPin, bool& allowClose);
    void onCurrentTabChanged(int index);
    void onModificationChanged(bool modified);

private:
    EditorTab* currentTab() const;
    EditorTab* tabAt(int index) const;
    EditorTab* createTab(const QString& title = QString());
    void       updateWindowTitle();
    void       updateStatusBar(EditorTab* tab);
    bool       confirmClose(EditorTab* tab);

    MruTabWidget* m_tabs      = nullptr;
    QLabel*       m_statusPos = nullptr;
};
