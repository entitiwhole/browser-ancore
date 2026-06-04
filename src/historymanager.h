#pragma once
#include <QObject>
#include <QVector>
#include <QString>
#include <QUrl>
#include <QDateTime>
#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

struct HistoryEntry {
    QUrl url;
    QString title;
    QDateTime visitTime;
    int visitCount = 1;
};

class HistoryManager : public QObject
{
    Q_OBJECT

public:
    explicit HistoryManager(QObject* parent = nullptr);

    void addVisit(const QUrl& url, const QString& title);
    void clear();

    QVector<HistoryEntry> recent(int count = 100) const;
    QVector<HistoryEntry> search(const QString& query) const;
    QStringList suggestions(const QString& prefix) const;

    void load();
    void save();

    void showDialog(QWidget* parent);

signals:
    void historyCleared();

private:
    QString storagePath() const;
    QVector<HistoryEntry> m_entries;
    QListWidget* m_listWidget = nullptr;
};
