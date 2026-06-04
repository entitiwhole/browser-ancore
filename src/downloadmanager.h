#pragma once
#include <QObject>
#include <QVector>
#include <QString>
#include <QUrl>
#include <QDateTime>
#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QPushButton>

struct DownloadItem {
    QString id;
    QUrl url;
    QString path;
    QString fileName;
    qint64 totalBytes = 0;
    qint64 receivedBytes = 0;
    bool finished = false;
    bool cancelled = false;
    QDateTime startTime;
};

class DownloadManager : public QObject
{
    Q_OBJECT

public:
    explicit DownloadManager(QObject* parent = nullptr);

    void addDownload(const QUrl& url, const QString& path);
    void removeDownload(const QString& id);
    void cancelDownload(const QString& id);

    void save();
    void load();

    void showDialog(QWidget* parent);

    const QVector<DownloadItem>& downloads() const { return m_downloads; }

signals:
    void downloadAdded(const DownloadItem& item);
    void downloadProgress(const QString& id, qint64 received, qint64 total);
    void downloadFinished(const QString& id, bool success);

private:
    QString storagePath() const;
    void updateProgress(const QString& id, qint64 received, qint64 total);

    QVector<DownloadItem> m_downloads;
    QDialog* m_dialog = nullptr;
    QTableWidget* m_table = nullptr;
};
