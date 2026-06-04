#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QAction>
#include <QUrl>
#include <QCompleter>
#include <QStringListModel>

class AddressBar : public QWidget
{
    Q_OBJECT

public:
    explicit AddressBar(QWidget* parent = nullptr);

    void setUrl(const QUrl& url);
    QUrl url() const;

    void setLoading(bool loading);
    void setSecure(bool secure);
    void setNavigationEnabled(bool canGoBack, bool canGoForward);
    void setStatusText(const QString& text);

signals:
    void navigateToUrl(const QUrl& url);
    void reloadRequested();
    void stopRequested();
    void backRequested();
    void forwardRequested();
    void bookmarkToggled();

public slots:
    void focusAddressBar();

private:
    void setupUI();
    void updateIcons();
    void onReturnPressed();

    QToolButton* m_btnBack;
    QToolButton* m_btnForward;
    QToolButton* m_btnReload;
    QToolButton* m_btnBookmark;
    QLabel* m_lockIcon;
    QLineEdit* m_addressInput;
    QLabel* m_statusLabel;
    QCompleter* m_completer;
    QStringListModel* m_completerModel;
    bool m_isLoading = false;
    bool m_isSecure = false;
    QUrl m_currentUrl;
};
