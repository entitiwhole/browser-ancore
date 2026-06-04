#include "adblocker.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QTimer>

AdBlocker::AdBlocker(QObject* parent)
    : QObject(parent)
{
    m_filterLists << "https://easylist.to/easylist/easylist.txt";
    addBuiltinFilters();
}

bool AdBlocker::shouldBlock(const QUrl& requestUrl, const QUrl& pageUrl) const
{
    if (!m_enabled) return false;

    QString url = requestUrl.toString();
    for (const auto& rule : m_rules) {
        if (!rule.enabled) continue;
        if (rule.pattern.match(url).hasMatch()) {
            m_blockedCount++;
            emit const_cast<AdBlocker*>(this)->requestBlocked(requestUrl);
            return true;
        }
    }
    return false;
}

void AdBlocker::setEnabled(bool enabled)
{
    m_enabled = enabled;
    save();
}

void AdBlocker::addBuiltinFilters()
{
    QStringList builtin = {
        // Common ad networks
        R"(doubleclick\.net)",
        R"(googleadservices\.com)",
        R"(googlesyndication\.com)",
        R"(google-analytics\.com)",
        R"(googletagmanager\.com)",
        R"(facebook\.com/tr)",
        R"(adservice\.google\.)",
        R"(adsrvr\.org)",
        R"(adnxs\.com)",
        R"(rubiconproject\.com)",
        R"(criteo\.com)",
        R"(taboola\.com)",
        R"(outbrain\.com)",
        R"(amazon-adsystem\.com)",
        R"(scorecardresearch\.com)",
        R"(quantserve\.com)",
        R"(exelator\.com)",
        R"(moatads\.com)",
        R"(adsafeprotected\.com)",

        // Common ad paths
        R"(/banner/)",
        R"(/ads/)",
        R"(/advert)",
        R"(/pagead/)",
        R"(/wp-content/uploads/\d+/\d+/.*banner)",
        R"(/affiliate)",
        R"(/sponsor)",
        R"(/promotion)",

        // Tracking
        R"(/analytics)",
        R"(/tracking)",
        R"(/pixel\.)",
        R"(/beacon\.)",
        R"(utm_source=)",
        R"(utm_medium=)",
        R"(utm_campaign=)",
    };

    for (const QString& pattern : builtin) {
        AdBlockRule rule;
        rule.original = pattern;
        rule.pattern = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
        if (rule.pattern.isValid())
            m_rules.append(rule);
    }
}

void AdBlocker::parseFilterList(const QString& content)
{
    const auto lines = content.split('\n');
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('!') || trimmed.startsWith('['))
            continue;

        QString pattern = trimmed;
        // Convert AdBlock syntax to regex (simplified)
        pattern.replace(QRegularExpression(R"(\.)"), "\\.");
        pattern.replace(QRegularExpression(R"(\*)"), ".*");
        pattern.replace(QRegularExpression(R"(\?)"), "\\?");
        pattern.replace(QRegularExpression(R"(\+)"), "\\+");
        pattern.replace(QRegularExpression(R"(\^)"), "[/:&?=,;.]");
        pattern.replace(QRegularExpression(R"(\|\|)"), "^https?://([^/]+\\.)?");
        pattern.replace(QRegularExpression(R"(\|)"), "");

        if (pattern.contains('\"') || pattern.contains('<'))
            continue;

        AdBlockRule rule;
        rule.original = trimmed;
        rule.pattern = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption);
        if (rule.pattern.isValid() && !rule.pattern.pattern().isEmpty())
            m_rules.append(rule);
    }
}

void AdBlocker::updateFilters()
{
    // Download filter lists in background
    for (const QString& url : m_filterLists) {
        QNetworkAccessManager manager;
        QNetworkReply* reply = manager.get(QNetworkRequest(QUrl(url)));

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(10000);
        loop.exec();

        if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
            QString content = QString::fromUtf8(reply->readAll());
            parseFilterList(content);
        }
        reply->deleteLater();
    }
    emit filterUpdated();
}

QString AdBlocker::storagePath() const
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(path);
    return path + "/adblock.json";
}

void AdBlocker::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();
    m_enabled = obj["enabled"].toBool(true);
    m_blockedCount = obj["blockedCount"].toInt(0);

    // Load custom filter lists
    QJsonArray lists = obj["filterLists"].toArray();
    if (!lists.isEmpty()) {
        m_filterLists.clear();
        for (const auto& val : lists)
            m_filterLists.append(val.toString());
    }
}

void AdBlocker::save()
{
    QJsonObject obj;
    obj["enabled"] = m_enabled;
    obj["blockedCount"] = m_blockedCount;
    QJsonArray lists;
    for (const QString& list : m_filterLists)
        lists.append(list);
    obj["filterLists"] = lists;

    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(obj).toJson());
    file.close();
}

void AdBlocker::setFilterLists(const QStringList& lists)
{
    m_filterLists = lists;
}
