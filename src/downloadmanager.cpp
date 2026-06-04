#include "downloadmanager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QHeaderView>
#include <QDateTime>
#include <QUuid>

DownloadManager::DownloadManager(QObject* parent)
    : QObject(parent)
{
}

void DownloadManager::addDownload(const QUrl& url, const QString& path)
{
    DownloadItem item;
    item.id = QUuid::createUuid().toString(QUuid::Id128);
    item.url = url;
    item.path = path;
    item.fileName = path.section('/', -1);
    item.startTime = QDateTime::currentDateTime();
    m_downloads.append(item);
    emit downloadAdded(item);
    save();
}

void DownloadManager::removeDownload(const QString& id)
{
    m_downloads.erase(
        std::remove_if(m_downloads.begin(), m_downloads.end(),
            [&](const DownloadItem& item) { return item.id == id; }),
        m_downloads.end());
    save();
}

void DownloadManager::cancelDownload(const QString& id)
{
    for (auto& item : m_downloads) {
        if (item.id == id) {
            item.cancelled = true;
            item.finished = true;
            emit downloadFinished(id, false);
            save();
            break;
        }
    }
}

void DownloadManager::updateProgress(const QString& id, qint64 received, qint64 total)
{
    for (auto& item : m_downloads) {
        if (item.id == id) {
            item.receivedBytes = received;
            item.totalBytes = total;
            emit downloadProgress(id, received, total);
            break;
        }
    }
}

QString DownloadManager::storagePath() const
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/downloads.json";
}

void DownloadManager::save()
{
    QJsonArray arr;
    for (const auto& item : m_downloads) {
        QJsonObject obj;
        obj["id"] = item.id;
        obj["url"] = item.url.toString();
        obj["path"] = item.path;
        obj["fileName"] = item.fileName;
        obj["totalBytes"] = item.totalBytes;
        obj["receivedBytes"] = item.receivedBytes;
        obj["finished"] = item.finished;
        obj["cancelled"] = item.cancelled;
        obj["startTime"] = item.startTime.toString(Qt::ISODate);
        arr.append(obj);
    }

    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(arr).toJson());
    file.close();
}

void DownloadManager::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) return;
    for (const auto& val : doc.array()) {
        QJsonObject obj = val.toObject();
        DownloadItem item;
        item.id = obj["id"].toString();
        item.url = QUrl(obj["url"].toString());
        item.path = obj["path"].toString();
        item.fileName = obj["fileName"].toString();
        item.totalBytes = obj["totalBytes"].toInteger();
        item.receivedBytes = obj["receivedBytes"].toInteger();
        item.finished = obj["finished"].toBool();
        item.cancelled = obj["cancelled"].toBool();
        item.startTime = QDateTime::fromString(obj["startTime"].toString(), Qt::ISODate);
        m_downloads.append(item);
    }
}

void DownloadManager::showDialog(QWidget* parent)
{
    if (m_dialog) {
        m_dialog->raise();
        m_dialog->activateWindow();
        return;
    }

    m_dialog = new QDialog(parent);
    m_dialog->setWindowTitle("Загрузки");
    m_dialog->resize(600, 400);

    auto* layout = new QVBoxLayout(m_dialog);

    m_table = new QTableWidget(m_dialog);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Файл", "Прогресс", "Размер", "Статус"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_table->setRowCount(m_downloads.size());
    for (int i = 0; i < m_downloads.size(); ++i) {
        const auto& item = m_downloads[i];
        m_table->setItem(i, 0, new QTableWidgetItem(item.fileName));
        m_table->setItem(i, 1, new QTableWidgetItem(
            item.finished ? "100%" :
            QString("%1%").arg(item.totalBytes > 0 ? item.receivedBytes * 100 / item.totalBytes : 0)));
        m_table->setItem(i, 2, new QTableWidgetItem(
            QString("%1 / %2").arg(item.receivedBytes / 1024).arg(item.totalBytes / 1024)));
        m_table->setItem(i, 3, new QTableWidgetItem(
            item.cancelled ? "Отменено" : item.finished ? "Завершено" : "В процессе"));
    }

    auto* closeBtn = new QPushButton("Закрыть", m_dialog);
    connect(closeBtn, &QPushButton::clicked, m_dialog, &QDialog::close);

    layout->addWidget(m_table, 1);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    connect(m_dialog, &QDialog::finished, this, [this]() {
        m_dialog->deleteLater();
        m_dialog = nullptr;
        m_table = nullptr;
    });

    m_dialog->show();
}
