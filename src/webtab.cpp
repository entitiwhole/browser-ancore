#include "webtab.h"
#include "adblocker.h"
#include "browsercore.h"
#include <QVBoxLayout>
#include <QWebEngineHttpRequest>
#include <QWebEngineHistory>

WebTab::WebTab(bool incognito, QWidget* parent)
    : QWidget(parent)
    , m_incognito(incognito)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    if (incognito) {
        m_profile = new QWebEngineProfile(this);
        m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
        m_profile->setPersistentStoragePath(QString());
    } else {
        m_profile = QWebEngineProfile::defaultProfile();
    }

    m_webView = new QWebEngineView(this);
    m_webView->setPage(new QWebEnginePage(m_profile, m_webView));

    auto* settings = m_webView->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, !incognito);
    settings->setAttribute(QWebEngineSettings::WebGLEnabled, false);
    settings->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    settings->setAttribute(QWebEngineSettings::ErrorPageEnabled, true);
    settings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumHeight(3);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background: transparent; border: none; }"
        "QProgressBar::chunk { background: #1a73e8; }"
    );

    layout->addWidget(m_webView);
    layout->addWidget(m_progressBar);

    connect(m_webView, &QWebEngineView::urlChanged, this, &WebTab::onUrlChanged);
    connect(m_webView, &QWebEngineView::titleChanged, this, &WebTab::onTitleChanged);
    connect(m_webView, &QWebEngineView::iconChanged, this, &WebTab::onIconChanged);
    connect(m_webView, &QWebEngineView::loadProgress, this, &WebTab::onLoadProgress);
    connect(m_webView, &QWebEngineView::loadFinished, this, &WebTab::onLoadFinished);
}

WebTab::~WebTab() = default;

void WebTab::loadUrl(const QUrl& url)
{
    m_webView->load(url);
}

QUrl WebTab::url() const
{
    return m_webView->url();
}

QString WebTab::title() const
{
    QString t = m_webView->title();
    return t.isEmpty() ? "Новая вкладка" : t;
}

QIcon WebTab::icon() const
{
    return m_webView->icon();
}

void WebTab::navigateBack() { m_webView->back(); }
void WebTab::navigateForward() { m_webView->forward(); }
void WebTab::reload() { m_webView->reload(); }
void WebTab::stopLoading() { m_webView->stop(); }

bool WebTab::canGoBack() const { return m_webView->page()->history()->canGoBack(); }
bool WebTab::canGoForward() const { return m_webView->page()->history()->canGoForward(); }
bool WebTab::isLoading() const { return m_loadProgress > 0 && m_loadProgress < 100; }

void WebTab::onUrlChanged(const QUrl& url)
{
    if (url.scheme() == "ancore")
        return;
    emit urlChanged(url);
}

void WebTab::onTitleChanged(const QString& title)
{
    if (!title.isEmpty())
        emit titleChanged(title);
}

void WebTab::onIconChanged(const QIcon& icon)
{
    emit iconChanged(icon);
}

void WebTab::onLoadProgress(int progress)
{
    m_loadProgress = progress;
    if (progress > 0 && progress < 100) {
        m_progressBar->setValue(progress);
        m_progressBar->show();
    } else {
        m_progressBar->hide();
    }
    emit loadingProgress(progress);
    if (progress > 0 && progress < 100)
        emit loadingStarted();
}

void WebTab::onLoadFinished(bool ok)
{
    m_loadProgress = ok ? 100 : 0;
    m_progressBar->hide();
    emit loadingFinished(ok);
}
