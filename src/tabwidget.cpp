#include "tabwidget.h"
#include "webtab.h"
#include "browsercore.h"

#include <QHBoxLayout>
#include <QIcon>

TabWidget::TabWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    addTab(BrowserCore::instance()->homePage());
}

void TabWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* tabBarLayout = new QHBoxLayout();
    tabBarLayout->setContentsMargins(0, 0, 0, 0);
    tabBarLayout->setSpacing(0);

    m_tabBar = new QTabBar(this);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setStyleSheet(
        "QTabBar::tab { padding: 6px 16px; min-width: 80px; max-width: 200px;"
        " border: none; border-bottom: 2px solid transparent;"
        " font-size: 12px; color: #666; }"
        "QTabBar::tab:selected { color: #333; border-bottom: 2px solid #1a73e8; background: transparent; }"
        "QTabBar::tab:hover:!selected { background: #f0f0f0; }"
        "QTabBar::close-button { width: 16px; height: 16px; margin: 2px; }"
    );

    m_btnNewTab = new QPushButton(this);
    m_btnNewTab->setObjectName("btnNewTab");
    m_btnNewTab->setFixedSize(32, 28);
    m_btnNewTab->setToolTip("Новая вкладка");
    m_btnNewTab->setIcon(QIcon(":/icons/new_tab"));
    m_btnNewTab->setIconSize(QSize(18, 18));

    tabBarLayout->addWidget(m_tabBar, 1);
    tabBarLayout->addWidget(m_btnNewTab);

    m_stack = new QStackedWidget(this);
    m_contextMenu = new QMenu(this);
    m_contextMenu->addAction("Закрыть вкладку", this, [this]() {
        if (m_contextMenu->property("tabIndex").isValid())
            closeTab(m_contextMenu->property("tabIndex").toInt());
    });
    m_contextMenu->addAction("Закрыть другие", this, [this]() {
        int idx = m_contextMenu->property("tabIndex").toInt();
        for (int i = count() - 1; i >= 0; --i)
            if (i != idx) closeTab(i);
    });
    m_contextMenu->addSeparator();
    m_contextMenu->addAction("Новая вкладка", this, [this]() { addTab(); });

    mainLayout->addLayout(tabBarLayout);
    mainLayout->addWidget(m_stack, 1);

    connect(m_tabBar, &QTabBar::currentChanged, this, &TabWidget::onTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &TabWidget::onTabCloseRequested);
    connect(m_tabBar, &QTabBar::customContextMenuRequested, this, &TabWidget::showTabContextMenu);
    connect(m_btnNewTab, &QPushButton::clicked, this, [this]() { addTab(); });
}

WebTab* TabWidget::currentTab() const
{
    int idx = m_tabBar->currentIndex();
    if (idx >= 0 && idx < m_tabs.size())
        return m_tabs[idx];
    return nullptr;
}

WebTab* TabWidget::tabAt(int index) const
{
    if (index >= 0 && index < m_tabs.size())
        return m_tabs[index];
    return nullptr;
}

int TabWidget::indexOf(WebTab* tab) const
{
    return m_tabs.indexOf(tab);
}

WebTab* TabWidget::addTab(bool incognito)
{
    auto* tab = new WebTab(incognito, this);
    m_tabs.append(tab);
    int idx = m_stack->addWidget(tab);
    m_tabBar->insertTab(idx, "Новая вкладка");

    connect(tab, &WebTab::titleChanged, this, [this, tab](const QString& title) {
        int i = m_tabs.indexOf(tab);
        if (i >= 0) m_tabBar->setTabText(i, title);
    });
    connect(tab, &WebTab::iconChanged, this, [this, tab](const QIcon& icon) {
        int i = m_tabs.indexOf(tab);
        if (i >= 0) m_tabBar->setTabIcon(i, icon);
    });

    m_tabBar->setCurrentIndex(idx);
    emit tabAdded(tab);
    return tab;
}

WebTab* TabWidget::addTab(const QUrl& url, bool incognito)
{
    WebTab* tab = addTab(incognito);
    tab->loadUrl(url);
    return tab;
}

void TabWidget::closeTab(int index)
{
    if (index < 0 || index >= m_tabs.size()) return;

    WebTab* tab = m_tabs[index];
    m_tabs.remove(index);
    m_stack->removeWidget(tab);
    m_tabBar->removeTab(index);
    tab->deleteLater();

    emit tabClosed(index);

    if (m_tabs.isEmpty())
        addTab();
}

void TabWidget::closeCurrentTab()
{
    closeTab(m_tabBar->currentIndex());
}

void TabWidget::moveTab(int from, int to)
{
    m_tabs.move(from, to);
    m_stack->removeWidget(m_tabs[to]);
    m_stack->insertWidget(to, m_tabs[to]);
}

void TabWidget::setCurrentIndex(int index)
{
    m_tabBar->setCurrentIndex(index);
}

void TabWidget::onTabChanged(int index)
{
    m_stack->setCurrentIndex(index);
    emit currentChanged(index);
}

void TabWidget::onTabCloseRequested(int index)
{
    closeTab(index);
}

void TabWidget::showTabContextMenu(const QPoint& pos)
{
    int idx = m_tabBar->tabAt(pos);
    m_contextMenu->setProperty("tabIndex", idx);
    m_contextMenu->popup(m_tabBar->mapToGlobal(pos));
}
