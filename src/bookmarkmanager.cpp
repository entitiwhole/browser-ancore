#include "bookmarkmanager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>

BookmarkManager::BookmarkManager(QObject* parent)
    : QObject(parent)
{
}

void BookmarkManager::add(const QString& name, const QString& url)
{
    m_bookmarks[name] = url;
    emit bookmarkAdded(name, url);
    save();
}

void BookmarkManager::remove(const QString& name)
{
    m_bookmarks.remove(name);
    emit bookmarkRemoved(name);
    save();
}

bool BookmarkManager::contains(const QString& name) const
{
    return m_bookmarks.contains(name);
}

QString BookmarkManager::url(const QString& name) const
{
    return m_bookmarks.value(name);
}

QStringList BookmarkManager::allNames() const
{
    return m_bookmarks.keys();
}

QString BookmarkManager::storagePath() const
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/bookmarks.json";
}

void BookmarkManager::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        m_bookmarks[it.key()] = it.value().toString();
    }
}

void BookmarkManager::save()
{
    QJsonObject obj;
    for (auto it = m_bookmarks.begin(); it != m_bookmarks.end(); ++it) {
        obj[it.key()] = it.value();
    }

    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(obj).toJson());
    file.close();
}
