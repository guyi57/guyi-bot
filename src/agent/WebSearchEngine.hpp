#pragma once

#include <QString>
#include <QVector>
#include <functional>

struct WebSearchResult {
    QString title;
    QString snippet;
    QString url;
    QString source; // "百度搜索" 或 "必应搜索"
};

class WebSearchEngine {
public:
    static WebSearchEngine* instance();

    // 统一免 Key 异步搜索：自动并发查询 Baidu 与 Bing 双引擎，合并去重并格式化
    void search(const QString &query, std::function<void(bool success, const QVector<WebSearchResult>& results, const QString &formattedMarkdown)> callback);

private:
    WebSearchEngine();
    ~WebSearchEngine();

    void querySo360(const QString &query, std::function<void(const QVector<WebSearchResult>& results)> callback);
    void queryBing(const QString &query, std::function<void(const QVector<WebSearchResult>& results)> callback);
    void queryBaidu(const QString &query, std::function<void(const QVector<WebSearchResult>& results)> callback);
    static QString cleanHtml(const QString &rawHtml);
};
