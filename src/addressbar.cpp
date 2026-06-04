#include "addressbar.h"
#include "browsercore.h"
#include "bookmarkmanager.h"
#include <QApplication>
#include <QStyle>
#include <QIcon>

AddressBar::AddressBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("addressBar");
    setupUI();
    setFixedHeight(44);
}

void AddressBar::setupUI()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);

    m_btnBack = new QToolButton(this);
    m_btnBack->setToolTip("Назад");
    m_btnBack->setFixedSize(32, 32);
    m_btnBack->setIcon(QIcon(":/icons/back"));
    m_btnBack->setIconSize(QSize(20, 20));
    m_btnBack->setEnabled(false);
    m_btnBack->setAutoRaise(true);

    m_btnForward = new QToolButton(this);
    m_btnForward->setToolTip("Вперёд");
    m_btnForward->setFixedSize(32, 32);
    m_btnForward->setIcon(QIcon(":/icons/forward"));
    m_btnForward->setIconSize(QSize(20, 20));
    m_btnForward->setEnabled(false);
    m_btnForward->setAutoRaise(true);

    m_btnReload = new QToolButton(this);
    m_btnReload->setToolTip("Обновить");
    m_btnReload->setFixedSize(32, 32);
    m_btnReload->setIcon(QIcon(":/icons/refresh"));
    m_btnReload->setIconSize(QSize(20, 20));
    m_btnReload->setAutoRaise(true);

    m_lockIcon = new QLabel(this);
    m_lockIcon->setFixedSize(20, 20);
    m_lockIcon->setPixmap(QIcon(":/icons/lock").pixmap(16, 16));
    m_lockIcon->setVisible(false);

    m_addressInput = new QLineEdit(this);
    m_addressInput->setObjectName("addressInput");
    m_addressInput->setPlaceholderText("Поиск или введите адрес...");
    m_addressInput->setClearButtonEnabled(true);

    m_completerModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completerModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_addressInput->setCompleter(m_completer);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("QLabel { color: #888; font-size: 11px; padding-right: 8px; }");
    m_statusLabel->setVisible(false);

    m_btnBookmark = new QToolButton(this);
    m_btnBookmark->setToolTip("Добавить в закладки");
    m_btnBookmark->setFixedSize(32, 32);
    m_btnBookmark->setIcon(QIcon(":/icons/bookmark"));
    m_btnBookmark->setIconSize(QSize(20, 20));
    m_btnBookmark->setAutoRaise(true);

    layout->addWidget(m_btnBack);
    layout->addWidget(m_btnForward);
    layout->addWidget(m_btnReload);
    layout->addSpacing(4);
    layout->addWidget(m_lockIcon);
    layout->addWidget(m_addressInput, 1);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_btnBookmark);

    connect(m_btnBack, &QToolButton::clicked, this, &AddressBar::backRequested);
    connect(m_btnForward, &QToolButton::clicked, this, &AddressBar::forwardRequested);
    connect(m_btnReload, &QToolButton::clicked, this, [this]() {
        if (m_isLoading) emit stopRequested(); else emit reloadRequested();
    });
    connect(m_btnBookmark, &QToolButton::clicked, this, &AddressBar::bookmarkToggled);
    connect(m_addressInput, &QLineEdit::returnPressed, this, &AddressBar::onReturnPressed);
}

void AddressBar::setUrl(const QUrl& url)
{
    m_currentUrl = url;
    if (!url.isEmpty() && url.scheme() != "ancore") {
        m_addressInput->setText(url.toDisplayString());
        m_addressInput->setCursorPosition(0);
    }
    bool https = url.scheme() == "https";
    if (https != m_isSecure) {
        m_isSecure = https;
        m_lockIcon->setVisible(https);
    }
}

QUrl AddressBar::url() const
{
    return QUrl::fromUserInput(m_addressInput->text());
}

void AddressBar::setLoading(bool loading)
{
    m_isLoading = loading;
    m_btnReload->setIcon(QIcon(loading ? ":/icons/stop" : ":/icons/refresh"));
    m_btnReload->setToolTip(loading ? "Остановить" : "Обновить");
}

void AddressBar::setSecure(bool secure)
{
    m_isSecure = secure;
    m_lockIcon->setVisible(secure);
}

void AddressBar::setNavigationEnabled(bool canGoBack, bool canGoForward)
{
    m_btnBack->setEnabled(canGoBack);
    m_btnForward->setEnabled(canGoForward);
}

void AddressBar::setStatusText(const QString& text)
{
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
}

void AddressBar::focusAddressBar()
{
    m_addressInput->setFocus();
    m_addressInput->selectAll();
}

void AddressBar::onReturnPressed()
{
    QString text = m_addressInput->text().trimmed();
    if (text.isEmpty()) return;
    QString resolved = BrowserCore::instance()->resolveSearchQuery(text);
    emit navigateToUrl(QUrl(resolved));
}

void AddressBar::updateIcons()
{
}
