#pragma once
#include <QTabWidget>
#include <QTimer>
#include <QVector>
#include <QMap>
#include <QPointer>

class QListWidget;
class QListWidgetItem;
class QDialog;
class QAbstractButton;
class QMenu;

class MruTabWidget : public QTabWidget
{
    Q_OBJECT
public:
    explicit MruTabWidget(QWidget* parent = nullptr);
    ~MruTabWidget() override;

    void setTabLimit(int limit);
    void setMinimalTabCount(int minCount);
    bool canCloseTabs() const;

    bool requestCloseTab(int index, bool askPin = false);
    bool requestCloseAllTabs();

    bool isTabPinned(int tabIndex) const;
    void setTabPinned(int tabIndex, bool pinned);
    int  pinnedTabCount() const;

    void setPinIconUri(const QString& uri) { m_pinIconUri = uri; }
    void setSequentialTabSwitching(bool seq) { m_sequentialTabSwitching = seq; }

signals:
    void tabAboutToClose(int index, bool askPin, bool& allowClose);
    void actionsBeforeTabClose(int index);
    void tabCountChanged(int count);
    void tabContextMenuRequested(int tabIndex, QMenu* menu);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void tabRemoved(int index) override;
    void tabInserted(int index) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onCurrentChanged(int index);
    void handleCtrlTabTimeout();
    void onPopupListItemActivated(QListWidgetItem* item);
    void onTabContextMenuRequested(const QPoint& pos);

private:
    void installTabBarEventFilter();
    void removeTabBarEventFilter();
    void updateMruOrder(int index);
    void showMruPopup();
    void hideMruPopup();
    void cycleMruPopup(bool forward);
    void activateSelectedMruTab();
    void performDirectSwitch();
    bool handleCtrlTabEvent(QKeyEvent* keyEvent);
    void mapCloseButtonsToTabs();
    void updateCloseButtonVisibility();
    void closeOtherTabs(int keepIndex);
    void closeTabsToLeft(int fromIndex);
    void closeTabsToRight(int fromIndex);
    void updateTabButton(int index);
    int  enforceTabLimit();
    QVector<QWidget*> findLeastRecentlyUsedUnpinnedTabs(int atMost) const;

    QList<int>  m_mruOrder;
    bool        m_ctrlHeld            = false;
    bool        m_expectingPopup      = false;
    bool        m_shiftHeldOnTabPress = false;
    bool        m_sequentialTabSwitching = false;
    bool        m_isTabBarFilterInstalled = false;
    int         m_hoveredTabIndex     = -1;
    int         m_tabLimit            = 0;
    int         m_minimalTabCount     = 0;
    QString     m_pinIconUri;

    QTimer      m_ctrlTabTimer;
    QPointer<QDialog>      m_mruPopup;
    QPointer<QListWidget>  m_mruListWidget;

    QVector<bool>                    m_pinnedTabs;
    QMap<int, QAbstractButton*>      m_tabIndexToCloseButtonMap;
};
