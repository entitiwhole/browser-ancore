#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QVector>
#include <QUrl>

struct AdBlockRule {
    QString original;
    QRegularExpression pattern;
    bool enabled = true;
};

class AdBlocker : public QObject
{
    Q_OBJECT

public:
    explicit AdBlocker(QObject* parent = nullptr);

    bool shouldBlock(const QUrl& requestUrl, const QUrl& pageUrl) const;
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    void load();
    void save();
    void updateFilters();

    int blockedCount() const { return m_blockedCount; }
    void resetCount() { m_blockedCount = 0; }

    QStringList filterLists() const { return m_filterLists; }
    void setFilterLists(const QStringList& lists);

signals:
    void filterUpdated();
    void requestBlocked(const QUrl& url);

private:
    void parseFilterList(const QString& content);
    void addBuiltinFilters();

    QVector<AdBlockRule> m_rules;
    bool m_enabled = true;
    mutable int m_blockedCount = 0;
    QStringList m_filterLists;
    QString storagePath() const;
};
