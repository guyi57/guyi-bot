#include "NewsFeedService.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRandomGenerator>
#include <QDateTime>
#include <iostream>

NewsFeedService *NewsFeedService::instance() {
    static NewsFeedService s_instance;
    return &s_instance;
}

NewsFeedService::NewsFeedService() {
    loadFromCacheFile();
    // 启动时若缓存为空或已超期 10 分钟，自动后台刷新一次
    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_cachedItems.isEmpty() || (now - m_lastLoadTime > 600)) {
        refreshFeedAsync();
    }
}

QString NewsFeedService::findScriptPath() const {
    QStringList candidates;
    QString appDir = QCoreApplication::applicationDirPath();
    
    // 1. macOS Bundle 内部资源
    candidates << appDir + "/../Resources/scripts/pet_news_feed.py";
    // 2. 本地开发目录
    candidates << appDir + "/scripts/pet_news_feed.py";
    candidates << QDir::currentPath() + "/scripts/pet_news_feed.py";
    // 3. 用户主目录配置
    candidates << QDir::homePath() + "/.config/guyi-bot/scripts/pet_news_feed.py";
    // 4. 用户 WorkBuddy 工作区历史备份
    candidates << "/Users/guyi/WorkBuddy/2026-11-03-14-42-52/pet_news_feed.py";

    for (const auto &path : candidates) {
        if (QFile::exists(path)) {
            return QDir::cleanPath(path);
        }
    }
    return "";
}

void NewsFeedService::loadFromCacheFile() {
    QString cachePath = QDir::homePath() + "/.pet_news_cache.json";
    QFile file(cachePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    QJsonObject root = doc.object();
    m_cachedItems.clear();

    struct SourceMeta {
        QString key;
        QString name;
        QString category;
    };

    static const QList<SourceMeta> s_sources = {
        {"60s", "60秒新闻", "news"},
        {"weibo", "微博热搜", "hot"},
        {"zhihu", "知乎热榜", "hot"},
        {"toutiao", "今日头条", "hot"},
        {"douyin", "抖音热点", "hot"},
        {"v2ex", "V2EX热门", "fun"},
        {"hitokoto", "每日微语", "fun"},
        {"hn_items", "HackerNews", "fun"}
    };

    for (const auto &meta : s_sources) {
        if (!root.contains(meta.key)) continue;
        QJsonObject block = root[meta.key].toObject();
        QJsonArray items = block["items"].toArray();
        for (const auto &val : items) {
            QJsonObject obj = val.toObject();
            QString text = obj["text"].toString().trimmed();
            if (text.length() < 2) continue;

            NewsFeedItem item;
            item.source = obj.contains("source") ? obj["source"].toString() : meta.name;
            item.category = meta.category;
            item.text = text;
            item.url = obj["url"].toString().trimmed();
            m_cachedItems.append(item);
        }
    }

    m_lastLoadTime = QDateTime::currentSecsSinceEpoch();
    std::cout << "[NewsFeedService] 从本地缓存载入新鲜资讯/热梗: " << m_cachedItems.size() << " 条" << std::endl;
}

void NewsFeedService::refreshFeedAsync() {
    if (m_isFetching) return;

    QString script = findScriptPath();
    if (script.isEmpty()) {
        std::cerr << "[NewsFeedService] 未找到 pet_news_feed.py 脚本路径" << std::endl;
        return;
    }

    m_isFetching = true;
    auto proc = new QProcess();
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, proc](int exitCode, QProcess::ExitStatus) {
        m_isFetching = false;
        if (exitCode == 0) {
            loadFromCacheFile();
        }
        proc->deleteLater();
    });

    proc->start("python3", QStringList() << script << "all");
}

NewsFeedItem NewsFeedService::getRandomItem(const QString &category) {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_cachedItems.isEmpty() || (now - m_lastLoadTime > 600)) {
        refreshFeedAsync();
    }

    // 若当前本地已有缓存，直接按分类过滤并秒级返回
    QList<NewsFeedItem> candidates;
    for (const auto &it : m_cachedItems) {
        if (category == "mix" || it.category == category) {
            candidates.append(it);
        }
    }

    if (!candidates.isEmpty()) {
        int idx = QRandomGenerator::global()->bounded(candidates.size());
        return candidates[idx];
    }

    if (!m_cachedItems.isEmpty()) {
        int idx = QRandomGenerator::global()->bounded(m_cachedItems.size());
        return m_cachedItems[idx];
    }

    // 若缓存完全为空，同步轻量调用一次生成单条
    QString script = findScriptPath();
    if (!script.isEmpty()) {
        QProcess proc;
        proc.start("python3", QStringList() << script << "json");
        if (proc.waitForFinished(2000)) {
            QByteArray out = proc.readAllStandardOutput().trimmed();
            QJsonDocument doc = QJsonDocument::fromJson(out);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                NewsFeedItem item;
                item.source = obj["source"].toString("全网热点");
                item.text = obj["text"].toString();
                item.url = obj["url"].toString();
                item.category = obj["category"].toString("mix");
                if (!item.text.isEmpty()) {
                    return item;
                }
            }
        }
    }

    // 默认回退保底
    NewsFeedItem fallback;
    fallback.source = "每日微语";
    fallback.text = "生活原本沉闷，但跑起来就会有风。主人今天也要元气满满哦~";
    fallback.category = "fun";
    return fallback;
}

