#include "WebSearchEngine.hpp"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QRegularExpression>
#include <QTimer>
#include <iostream>

WebSearchEngine* WebSearchEngine::instance()
{
    static WebSearchEngine s_instance;
    return &s_instance;
}

WebSearchEngine::WebSearchEngine()
{
}

WebSearchEngine::~WebSearchEngine()
{
}

QString WebSearchEngine::cleanHtml(const QString &rawHtml)
{
    QString text = rawHtml;
    // 移除脚本和样式
    text.remove(QRegularExpression("<script[^>]*>.*?</script>", QRegularExpression::DotMatchesEverythingOption));
    text.remove(QRegularExpression("<style[^>]*>.*?</style>", QRegularExpression::DotMatchesEverythingOption));
    // 移除 HTML 标签
    text.remove(QRegularExpression("<[^>]+>"));
    // 实体解码
    text.replace("&nbsp;", " ");
    text.replace("&ensp;", " ");
    text.replace("&emsp;", " ");
    text.replace("&middot;", "·");
    text.replace("&#0183;", "·");
    text.replace("&#183;", "·");
    text.replace("&quot;", "\"");
    text.replace("&amp;", "&");
    text.replace("&lt;", "<");
    text.replace("&gt;", ">");
    text.replace("&apos;", "'");
    text.replace("&#39;", "'");
    // 归一化空格与换行
    text.replace(QRegularExpression("\\s+"), " ");
    return text.trimmed();
}

void WebSearchEngine::querySo360(const QString &query, std::function<void(const QVector<WebSearchResult>& results)> callback)
{
    auto netMgr = new QNetworkAccessManager();
    QString encQuery = QString::fromUtf8(QUrl::toPercentEncoding(query));
    QUrl url(QString("https://www.so.com/s?q=%1").arg(encQuery));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");

    QNetworkReply *reply = netMgr->get(req);

    // 5秒超时保护
    auto timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    timer->start(5000);

    QObject::connect(reply, &QNetworkReply::finished, [reply, netMgr, callback]() {
        QVector<WebSearchResult> results;
        if (reply->error() == QNetworkReply::NoError) {
            QString html = QString::fromUtf8(reply->readAll());

            QRegularExpression itemRegex("<li class=\"res-list[^\"]*\"[^>]*>(.*?)</li>", QRegularExpression::DotMatchesEverythingOption);
            auto it = itemRegex.globalMatch(html);
            while (it.hasNext() && results.size() < 6) {
                auto match = it.next();
                QString block = match.captured(1);

                QString title;
                QRegularExpression titleRegex("<h3[^>]*>(.*?)</h3>", QRegularExpression::DotMatchesEverythingOption);
                auto tMatch = titleRegex.match(block);
                if (tMatch.hasMatch()) {
                    title = cleanHtml(tMatch.captured(1));
                }

                // 提取 URL
                QString linkUrl;
                QRegularExpression urlRegex("href=\"([^\"]+)\"");
                auto uMatch = urlRegex.match(block);
                if (uMatch.hasMatch()) {
                    linkUrl = uMatch.captured(1);
                }

                QString snippet = cleanHtml(block);
                if (title.isEmpty()) {
                    if (snippet.contains("百科")) {
                        title = "权威百科直达";
                    } else if (snippet.contains("相关消息")) {
                        title = "最新时政动态";
                    } else {
                        title = snippet.left(25);
                    }
                }

                if (snippet.length() > 250) {
                    snippet = snippet.left(250) + "...";
                }

                if (!snippet.isEmpty() && snippet.length() > 15 && !title.contains("短视频大全")) {
                    WebSearchResult item;
                    item.title = title;
                    item.snippet = snippet;
                    item.url = linkUrl;
                    item.source = "实时热点资讯";
                    results.append(item);
                }
            }
        }
        reply->deleteLater();
        netMgr->deleteLater();
        callback(results);
    });
}

void WebSearchEngine::queryBaidu(const QString &query, std::function<void(const QVector<WebSearchResult>& results)> callback)
{
    auto netMgr = new QNetworkAccessManager();
    QString encQuery = QString::fromUtf8(QUrl::toPercentEncoding(query));
    QUrl url(QString("https://www.baidu.com/s?wd=%1").arg(encQuery));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36");
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");

    QNetworkReply *reply = netMgr->get(req);

    // 5秒超时保护
    auto timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    timer->start(5000);

    QObject::connect(reply, &QNetworkReply::finished, [reply, netMgr, callback]() {
        QVector<WebSearchResult> results;
        if (reply->error() == QNetworkReply::NoError) {
            QString html = QString::fromUtf8(reply->readAll());

            // 匹配百度的 c-container 搜索条目容器
            QRegularExpression blockRegex(
                "<div[^>]*class=\"[^\"]*c-container[^\"]*\"[^>]*>(.*?)</div>\\s*</div>",
                QRegularExpression::DotMatchesEverythingOption
            );
            auto it = blockRegex.globalMatch(html);
            while (it.hasNext() && results.size() < 6) {
                auto match = it.next();
                QString block = match.captured(1);

                // 提取标题
                QString title;
                QRegularExpression titleRegex("<h3[^>]*>(.*?)</h3>", QRegularExpression::DotMatchesEverythingOption);
                auto tMatch = titleRegex.match(block);
                if (tMatch.hasMatch()) {
                    title = cleanHtml(tMatch.captured(1));
                }

                // 提取 URL
                QString linkUrl;
                QRegularExpression urlRegex("href=\"([^\"]+)\"");
                auto uMatch = urlRegex.match(block);
                if (uMatch.hasMatch()) {
                    linkUrl = uMatch.captured(1);
                }

                // 提取纯文本正文与摘要
                QString snippet = cleanHtml(block);
                if (snippet.length() > 220) {
                    snippet = snippet.left(220) + "...";
                }

                if (!title.isEmpty() && snippet.length() > 20 && !title.contains("百度图片")) {
                    WebSearchResult item;
                    item.title = title;
                    item.snippet = snippet;
                    item.url = linkUrl;
                    item.source = "百度搜索";
                    results.append(item);
                }
            }
        }
        reply->deleteLater();
        netMgr->deleteLater();
        callback(results);
    });
}

void WebSearchEngine::queryBing(const QString &query, std::function<void(const QVector<WebSearchResult>& results)> callback)
{
    auto netMgr = new QNetworkAccessManager();
    QString encQuery = QString::fromUtf8(QUrl::toPercentEncoding(query));
    QUrl url(QString("https://cn.bing.com/search?q=%1").arg(encQuery));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36");
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");

    QNetworkReply *reply = netMgr->get(req);

    // 5秒超时保护
    auto timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    timer->start(5000);

    QObject::connect(reply, &QNetworkReply::finished, [reply, netMgr, callback]() {
        QVector<WebSearchResult> results;
        if (reply->error() == QNetworkReply::NoError) {
            QString html = QString::fromUtf8(reply->readAll());

            // 匹配必应的 b_algo 搜索结果项
            QRegularExpression itemRegex("<li class=\"b_algo\"[^>]*>(.*?)</li>", QRegularExpression::DotMatchesEverythingOption);
            auto it = itemRegex.globalMatch(html);
            while (it.hasNext() && results.size() < 6) {
                auto match = it.next();
                QString block = match.captured(1);

                // 提取标题
                QString title;
                QRegularExpression titleRegex("<h2[^>]*>(.*?)</h2>", QRegularExpression::DotMatchesEverythingOption);
                auto tMatch = titleRegex.match(block);
                if (tMatch.hasMatch()) {
                    title = cleanHtml(tMatch.captured(1));
                }

                // 提取 URL
                QString linkUrl;
                QRegularExpression urlRegex("href=\"([^\"]+)\"");
                auto uMatch = urlRegex.match(block);
                if (uMatch.hasMatch()) {
                    linkUrl = uMatch.captured(1);
                }

                // 提取摘要
                QString snippet;
                QRegularExpression snippetRegex("<p[^>]*>(.*?)</p>", QRegularExpression::DotMatchesEverythingOption);
                auto sMatch = snippetRegex.match(block);
                if (sMatch.hasMatch()) {
                    snippet = cleanHtml(sMatch.captured(1));
                }

                if (!title.isEmpty() && !snippet.isEmpty()) {
                    WebSearchResult item;
                    item.title = title;
                    item.snippet = snippet;
                    item.url = linkUrl;
                    item.source = "必应综合搜索";
                    results.append(item);
                }
            }
        }
        reply->deleteLater();
        netMgr->deleteLater();
        callback(results);
    });
}

void WebSearchEngine::search(const QString &query, std::function<void(bool success, const QVector<WebSearchResult>& results, const QString &formattedMarkdown)> callback)
{
    QString trimmedQ = query.trimmed();
    if (trimmedQ.isEmpty()) {
        if (callback) callback(false, {}, "搜索关键词为空。");
        return;
    }

    std::cout << "[WebSearchEngine] 正在多源并发实时检索 (360 实时资讯 + Bing 综合): " << trimmedQ.toStdString() << std::endl;

    auto allResults = std::make_shared<QVector<WebSearchResult>>();
    auto pending = std::make_shared<int>(2);

    auto onEngineDone = [trimmedQ, allResults, pending, callback](const QVector<WebSearchResult>& res) {
        for (const auto &item : res) {
            // 简单去重：标题相似或摘要前缀相同则跳过
            bool exists = false;
            for (const auto &existing : *allResults) {
                if (existing.title == item.title || (!existing.snippet.isEmpty() && existing.snippet.left(30) == item.snippet.left(30))) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                allResults->append(item);
            }
        }

        (*pending)--;
        if (*pending == 0) {
            bool success = !allResults->isEmpty();
            QString markdown;

            if (success) {
                markdown = QString("🌐 互联网实时搜索结果（关键词：「%1」）:\n\n").arg(trimmedQ);
                int count = std::min<int>(6, allResults->size());
                for (int i = 0; i < count; ++i) {
                    const auto &r = (*allResults)[i];
                    markdown += QString("%1. 【%2】%3\n   %4\n").arg(
                        QString::number(i + 1),
                        r.source,
                        r.title,
                        r.snippet
                    );
                }
                markdown += "\n💡 提示: 请结合上述最新的实时检索事实，准确、客观、条理清晰地回答用户的问题。";
            } else {
                markdown = QString("🌐 互联网搜索「%1」未获取到有效的实时结果，请尝试更换关键词。").arg(trimmedQ);
            }

            if (callback) {
                callback(success, *allResults, markdown);
            }
        }
    };

    // 并发启动 360 实时资讯搜索与必应搜索
    querySo360(trimmedQ, onEngineDone);
    queryBing(trimmedQ, onEngineDone);
}
