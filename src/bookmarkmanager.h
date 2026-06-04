#pragma once
#include <QObject>
#include <QMap>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>

class BookmarkManager : public QObject
{
    Q_OBJECT

public:
    explicit BookmarkManager(QObject* parent = nullptr);

    void add(const QString& name, const QString& url);
    void remove(const QString& name);
    bool contains(const QString& name) const;
    QString url(const QString& name) const;

    QStringList allNames() const;
    QMap<QString, QString> all() const { return m_bookmarks; }
    int count() const { return m_bookmarks.size(); }

    void load();
    void save();

signals:
    void bookmarkAdded(const QString& name, const QString& url);
    void bookmarkRemoved(const QString& name);

private:
    QString storagePath() const;
    QMap<QString, QString> m_bookmarks;
};
