#pragma once
#include <QWidget>
#include <QUrl>
#include <QString>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QProgressBar>
#include <QVBoxLayout>

class WebTab : public QWidget
{
    Q_OBJECT

public:
    explicit WebTab(bool incognito = false, QWidget* parent = nullptr);
    ~WebTab() override;

    void loadUrl(const QUrl& url);
    void loadUrl(const QString& url) { loadUrl(QUrl(url)); }

    QUrl url() const;
    QString title() const;
    QIcon icon() const;

    void navigateBack();
    void navigateForward();
    void reload();
    void stopLoading();

    bool canGoBack() const;
    bool canGoForward() const;
    bool isLoading() const;

    int loadProgress() const { return m_loadProgress; }

    QWebEngineView* webView() const { return m_webView; }

signals:
    void urlChanged(const QUrl& url);
    void titleChanged(const QString& title);
    void iconChanged(const QIcon& icon);
    void loadingStarted();
    void loadingProgress(int progress);
    void loadingFinished(bool success);
    void faviconChanged(const QIcon& icon);

private slots:
    void onUrlChanged(const QUrl& url);
    void onTitleChanged(const QString& title);
    void onIconChanged(const QIcon& icon);
    void onLoadProgress(int progress);
    void onLoadFinished(bool ok);

private:
    QWebEngineView* m_webView;
    QWebEngineProfile* m_profile;
    QProgressBar* m_progressBar;
    int m_loadProgress = 0;
    bool m_incognito;
};
