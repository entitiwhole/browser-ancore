#pragma once
#include <QTabBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QPushButton>
#include <QMenu>
#include <QVector>

class WebTab;

class TabWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TabWidget(QWidget* parent = nullptr);

    WebTab* currentTab() const;
    int currentIndex() const { return m_tabBar->currentIndex(); }
    int count() const { return m_tabs.size(); }

    WebTab* tabAt(int index) const;
    int indexOf(WebTab* tab) const;

    WebTab* addTab(bool incognito = false);
    WebTab* addTab(const QUrl& url, bool incognito = false);
    void closeTab(int index);
    void closeCurrentTab();
    void moveTab(int from, int to);

    void setCurrentIndex(int index);

signals:
    void currentChanged(int index);
    void tabAdded(WebTab* tab);
    void tabClosed(int index);

private:
    void setupUI();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void showTabContextMenu(const QPoint& pos);

    QTabBar* m_tabBar;
    QStackedWidget* m_stack;
    QVector<WebTab*> m_tabs;
    QPushButton* m_btnNewTab;
    QMenu* m_contextMenu;
};
