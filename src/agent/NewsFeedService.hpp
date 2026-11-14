#pragma once

#include <QString>
#include <QList>
#include <QJsonObject>
#include <functional>

struct NewsFeedItem {
    QString source;    // e.g. "微博热搜", "60秒新闻", "知乎热榜", "今日头条", "抖音热点", "V2EX热门", "HackerNews", "每日微语"
    QString text;      // 标题或内容文本
    QString url;       // 原文链接
    QString category;  // "news", "hot", "fun"
};

class NewsFeedService {
public:
    static NewsFeedService *instance();

    // 同步随机获取一条热点（优先从本地缓存秒级读取，若过期自动触发后台静默预取）
    NewsFeedItem getRandomItem(const QString &category = "mix");

    // 异步触发刷新聚合抓取
    void refreshFeedAsync();

    // 查找脚本路径
    QString findScriptPath() const;

private:
    NewsFeedService();
    ~NewsFeedService() = default;

    void loadFromCacheFile();

    QList<NewsFeedItem> m_cachedItems;
    qint64 m_lastLoadTime = 0;
    bool m_isFetching = false;
};

