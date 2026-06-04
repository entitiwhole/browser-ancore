#include "historymanager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <algorithm>

HistoryManager::HistoryManager(QObject* parent)
    : QObject(parent)
{
}

void HistoryManager::addVisit(const QUrl& url, const QString& title)
{
    if (url.scheme() == "ancore") return;

    for (auto& entry : m_entries) {
        if (entry.url == url) {
            entry.title = title.isEmpty() ? entry.title : title;
            entry.visitTime = QDateTime::currentDateTime();
            entry.visitCount++;
            save();
            return;
        }
    }

    HistoryEntry entry;
    entry.url = url;
    entry.title = title;
    entry.visitTime = QDateTime::currentDateTime();
    m_entries.prepend(entry);
    save();
}

void HistoryManager::clear()
{
    m_entries.clear();
    emit historyCleared();
    save();
}

QVector<HistoryEntry> HistoryManager::recent(int count) const
{
    QVector<HistoryEntry> sorted = m_entries;
    std::sort(sorted.begin(), sorted.end(), [](const HistoryEntry& a, const HistoryEntry& b) {
        return a.visitTime > b.visitTime;
    });
    if (sorted.size() > count)
        sorted.resize(count);
    return sorted;
}

QVector<HistoryEntry> HistoryManager::search(const QString& query) const
{
    QVector<HistoryEntry> results;
    for (const auto& entry : m_entries) {
        if (entry.url.toString().contains(query, Qt::CaseInsensitive) ||
            entry.title.contains(query, Qt::CaseInsensitive)) {
            results.append(entry);
        }
    }
    return results;
}

QStringList HistoryManager::suggestions(const QString& prefix) const
{
    QStringList result;
    for (const auto& entry : m_entries) {
        if (entry.url.toString().startsWith(prefix, Qt::CaseInsensitive)) {
            result.append(entry.url.toString());
            if (result.size() >= 10) break;
        }
    }
    return result;
}

QString HistoryManager::storagePath() const
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/history.json";
}

void HistoryManager::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) return;
    for (const auto& val : doc.array()) {
        QJsonObject obj = val.toObject();
        HistoryEntry entry;
        entry.url = QUrl(obj["url"].toString());
        entry.title = obj["title"].toString();
        entry.visitTime = QDateTime::fromString(obj["visitTime"].toString(), Qt::ISODate);
        entry.visitCount = obj["visitCount"].toInt(1);
        m_entries.append(entry);
    }
}

void HistoryManager::save()
{
    QJsonArray arr;
    for (const auto& entry : m_entries) {
        QJsonObject obj;
        obj["url"] = entry.url.toString();
        obj["title"] = entry.title;
        obj["visitTime"] = entry.visitTime.toString(Qt::ISODate);
        obj["visitCount"] = entry.visitCount;
        arr.append(obj);
    }

    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(arr).toJson());
    file.close();
}

void HistoryManager::showDialog(QWidget* parent)
{
    auto* dialog = new QDialog(parent);
    dialog->setWindowTitle("История");
    dialog->resize(700, 500);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(dialog);

    auto* searchLayout = new QHBoxLayout();
    auto* searchInput = new QLineEdit(dialog);
    searchInput->setPlaceholderText("Поиск в истории...");
    searchLayout->addWidget(searchInput, 1);

    auto* clearBtn = new QPushButton("Очистить историю", dialog);
    searchLayout->addWidget(clearBtn);
    layout->addLayout(searchLayout);

    m_listWidget = new QListWidget(dialog);
    layout->addWidget(m_listWidget, 1);

    auto* openUrl = new QString(); // will store URL to open
    auto closeBtn = new QPushButton("Закрыть", dialog);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    auto updateList = [this]() {
        m_listWidget->clear();
        auto entries = recent(200);
        for (const auto& entry : entries) {
            QString text = QString("%1 — %2")
                .arg(entry.title.isEmpty() ? entry.url.toString() : entry.title)
                .arg(entry.url.toString());
            auto* item = new QListWidgetItem(text);
            item->setData(Qt::UserRole, entry.url.toString());
            item->setToolTip(entry.url.toString());
            m_listWidget->addItem(item);
        }
    };

    connect(searchInput, &QLineEdit::textChanged, this, [updateList](const QString&) {
        updateList();
    });

    connect(clearBtn, &QPushButton::clicked, this, [this, dialog]() {
        auto result = QMessageBox::question(dialog, "Очистить историю",
            "Вы уверены, что хотите очистить всю историю?");
        if (result == QMessageBox::Yes) {
            clear();
            dialog->close();
        }
    });

    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [openUrl](QListWidgetItem* item) {
        *openUrl = item->data(Qt::UserRole).toString();
    });

    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::close);

    updateList();
    dialog->exec();

    if (!openUrl->isEmpty()) {
        auto* mw = parent->window();
        if (mw) {
            QMetaObject::invokeMethod(mw, "navigateToUrl",
                Q_ARG(QUrl, QUrl(*openUrl)));
        }
    }
    delete openUrl;
}
