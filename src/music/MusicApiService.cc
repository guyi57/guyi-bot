#include "MusicApiService.hpp"
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <iostream>

static QNetworkRequest buildRequest(const QUrl &url, const QString &customKey = QString())
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!customKey.isEmpty()) {
        request.setRawHeader("X-Request-Key", customKey.toUtf8());
    }
    return request;
}

MusicApiService* MusicApiService::instance()
{
    static MusicApiService s_instance;
    return &s_instance;
}

MusicApiService::MusicApiService(QObject *parent)
    : QObject(parent)
{
    m_netManager = new QNetworkAccessManager(this);
    loadSettings();
}

MusicApiService::~MusicApiService()
{
}

void MusicApiService::loadSettings()
{
    QSettings settings("XuanFu", "MusicPlayer");
    int engineVal = settings.value("music/engine", static_cast<int>(MusicEngine::LX_Music)).toInt();
    if (engineVal == static_cast<int>(MusicEngine::GDStudio)) {
        m_engine = MusicEngine::GDStudio;
    } else {
        m_engine = MusicEngine::LX_Music;
    }
    m_lxApiUrl = settings.value("music/lx_api_url", "").toString();
    m_lxRequestKey = settings.value("music/lx_request_key", "").toString();
}

void MusicApiService::saveSettings()
{
    QSettings settings("XuanFu", "MusicPlayer");
    settings.setValue("music/engine", static_cast<int>(m_engine));
    settings.setValue("music/lx_api_url", m_lxApiUrl);
    settings.setValue("music/lx_request_key", m_lxRequestKey);
}

MusicEngine MusicApiService::currentEngine() const
{
    return m_engine;
}

void MusicApiService::setEngine(MusicEngine engine)
{
    if (m_engine != engine) {
        m_engine = engine;
        saveSettings();
        if (m_onEngineChanged) m_onEngineChanged(m_engine);
    }
}

QString MusicApiService::engineDisplayName(MusicEngine engine)
{
    switch (engine) {
    case MusicEngine::LX_Music:
        return "洛雪/聚合音源 (推荐 · 全网320k直链)";
    case MusicEngine::GDStudio:
        return "GD音乐台 (原版 Meting 接口)";
    default:
        return "默认音源";
    }
}

QString MusicApiService::lxApiUrl() const
{
    return m_lxApiUrl;
}

void MusicApiService::setLxApiUrl(const QString &url)
{
    m_lxApiUrl = url.trimmed();
    saveSettings();
}

QString MusicApiService::lxRequestKey() const
{
    return m_lxRequestKey;
}

void MusicApiService::setLxRequestKey(const QString &key)
{
    m_lxRequestKey = key.trimmed();
    saveSettings();
}

void MusicApiService::resetToDefaults()
{
    m_engine = MusicEngine::LX_Music;
    m_lxApiUrl.clear();
    m_lxRequestKey.clear();
    saveSettings();
    if (m_onEngineChanged) m_onEngineChanged(m_engine);
}

QStringList MusicApiService::availableSources()
{
    return { "netease", "kuwo", "bilibili" };
}

QString MusicApiService::sourceDisplayName(const QString &source)
{
    if (source == "netease") return "网易云音乐 (推荐)";
    if (source == "kuwo") return "酷我音乐 (推荐 · VIP直链)";
    if (source == "bilibili") return "哔哩哔哩音频";
    if (source == "tencent" || source == "qq") return "QQ音乐 (接口维护中)";
    if (source == "apple") return "Apple Music (接口维护中)";
    if (source == "ytmusic") return "YouTube Music (接口维护中)";
    if (source == "spotify") return "Spotify (接口维护中)";
    return source;
}

// 统一对外路由方法
void MusicApiService::search(const QString &keyword, 
                             const QString &source, 
                             int count, 
                             int page, 
                             std::function<void(bool, const QVector<SongInfo>&, const QString&)> callback)
{
    if (m_engine == MusicEngine::GDStudio) {
        searchGDStudio(keyword, source, count, page, callback);
    } else {
        searchLX(keyword, source, count, page, callback);
    }
}

void MusicApiService::fetchPlayUrl(const QString &source, 
                                   const QString &id, 
                                   int br, 
                                   std::function<void(bool, const QString&, int, const QString&)> callback)
{
    if (m_engine == MusicEngine::GDStudio) {
        fetchPlayUrlGDStudio(source, id, br, callback);
    } else {
        fetchPlayUrlLX(source, id, br, callback);
    }
}

void MusicApiService::fetchPicUrl(const QString &source, 
                                  const QString &picId, 
                                  int size, 
                                  std::function<void(bool, const QString&, const QString&)> callback)
{
    if (m_engine == MusicEngine::GDStudio) {
        fetchPicUrlGDStudio(source, picId, size, callback);
    } else {
        fetchPicUrlLX(source, picId, size, callback);
    }
}

void MusicApiService::fetchLyric(const QString &source, 
                                 const QString &lyricId, 
                                 std::function<void(bool, const QString&, const QString&, const QString&)> callback)
{
    if (m_engine == MusicEngine::GDStudio) {
        fetchLyricGDStudio(source, lyricId, callback);
    } else {
        fetchLyricLX(source, lyricId, callback);
    }
}

// =========================================================================
// 原版 GDStudio (Meting) 接口实现 (完全保留原功能)
// =========================================================================
void MusicApiService::searchGDStudio(const QString &keyword, 
                                     const QString &source, 
                                     int count, 
                                     int page, 
                                     std::function<void(bool, const QVector<SongInfo>&, const QString&)> callback)
{
    if (keyword.trimmed().isEmpty()) {
        if (callback) callback(false, {}, "搜索关键词不能为空");
        return;
    }

    QString actualSource = source.trimmed().toLower();
    if (actualSource == "qq" || actualSource == "tencent") {
        actualSource = "netease";
    } else if (actualSource.isEmpty()) {
        actualSource = "netease";
    }

    QUrl url(m_gdstudioBaseUrl);
    QUrlQuery query;
    query.addQueryItem("types", "search");
    query.addQueryItem("source", actualSource);
    query.addQueryItem("name", keyword.trimmed());
    query.addQueryItem("count", QString::number(count <= 0 ? 20 : count));
    query.addQueryItem("pages", QString::number(page <= 0 ? 1 : page));
    url.setQuery(query);

    std::cout << "[GDStudio] 发起搜索: " << url.toString().toStdString() << std::endl;

    QNetworkReply *reply = m_netManager->get(buildRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, callback, actualSource]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            QString errDetail;
            auto errDoc = QJsonDocument::fromJson(data);
            if (errDoc.isObject() && errDoc.object().contains("detail")) {
                errDetail = errDoc.object()["detail"].toString();
            }
            QString err = errDetail.isEmpty() ? reply->errorString() : QString("GD音源提示: %1").arg(errDetail);
            if (callback) callback(false, {}, err);
            return;
        }

        QByteArray data = reply->readAll();
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isArray()) {
            if (callback) callback(false, {}, "GD响应解析失败: " + parseErr.errorString());
            return;
        }

        QVector<SongInfo> songs;
        for (const auto &val : doc.array()) {
            if (!val.isObject()) continue;
            QJsonObject obj = val.toObject();

            SongInfo s;
            s.id = obj["id"].toVariant().toString();
            s.name = obj["name"].toString();
            s.source = obj.contains("source") ? obj["source"].toString() : actualSource;
            s.album = obj["album"].toString();
            s.picId = obj["pic_id"].toVariant().toString();
            s.lyricId = obj["lyric_id"].toVariant().toString();
            if (s.lyricId.isEmpty()) s.lyricId = s.id;

            if (obj["artist"].isArray()) {
                QStringList artList;
                for (const auto &artVal : obj["artist"].toArray()) {
                    artList << artVal.toString();
                }
                s.artist = artList.join(" / ");
            } else {
                s.artist = obj["artist"].toString();
            }

            if (!s.id.isEmpty() && !s.name.isEmpty()) {
                songs.append(s);
            }
        }

        if (callback) callback(true, songs, "");
    });
}

void MusicApiService::fetchPlayUrlGDStudio(const QString &source, 
                                           const QString &id, 
                                           int br, 
                                           std::function<void(bool, const QString&, int, const QString&)> callback)
{
    QString actualSource = source.isEmpty() ? "netease" : source;
    QUrl url(m_gdstudioBaseUrl);
    QUrlQuery query;
    query.addQueryItem("types", "url");
    query.addQueryItem("source", actualSource);
    query.addQueryItem("id", id);
    query.addQueryItem("br", QString::number(br > 0 ? br : 999));
    url.setQuery(query);

    QNetworkReply *reply = m_netManager->get(buildRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, callback, actualSource, id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (actualSource == "netease") {
                QString fallbackUrl = QString("https://music.163.com/song/media/outer/url?id=%1.mp3").arg(id);
                if (callback) callback(true, fallbackUrl, 320, "");
                return;
            }
            if (callback) callback(false, "", 0, reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            if (actualSource == "netease") {
                QString fallbackUrl = QString("https://music.163.com/song/media/outer/url?id=%1.mp3").arg(id);
                if (callback) callback(true, fallbackUrl, 320, "");
                return;
            }
            if (callback) callback(false, "", 0, "无法解析GD直链响应");
            return;
        }

        QJsonObject obj = doc.object();
        QString playUrl = obj["url"].toString();
        int actualBr = obj["br"].toInt(999);

        if (playUrl.isEmpty()) {
            if (actualSource == "netease") {
                QString fallbackUrl = QString("https://music.163.com/song/media/outer/url?id=%1.mp3").arg(id);
                if (callback) callback(true, fallbackUrl, 320, "");
                return;
            }
            if (callback) callback(false, "", 0, "GD未返回有效直链");
            return;
        }

        if (callback) callback(true, playUrl, actualBr, "");
    });
}

void MusicApiService::fetchPicUrlGDStudio(const QString &source, 
                                          const QString &picId, 
                                          int size, 
                                          std::function<void(bool, const QString&, const QString&)> callback)
{
    if (picId.isEmpty()) {
        if (callback) callback(false, "", "picId 为空");
        return;
    }

    QUrl url(m_gdstudioBaseUrl);
    QUrlQuery query;
    query.addQueryItem("types", "pic");
    query.addQueryItem("source", source.isEmpty() ? "netease" : source);
    query.addQueryItem("id", picId);
    query.addQueryItem("size", QString::number(size > 0 ? size : 500));
    url.setQuery(query);

    QNetworkReply *reply = m_netManager->get(buildRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, "", reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            if (callback) callback(false, "", "无法解析封面图片响应");
            return;
        }

        QJsonObject obj = doc.object();
        QString picUrl = obj["url"].toString();
        if (picUrl.isEmpty()) {
            if (callback) callback(false, "", "未获取到封面直链");
            return;
        }

        if (callback) callback(true, picUrl, "");
    });
}

void MusicApiService::fetchLyricGDStudio(const QString &source, 
                                         const QString &lyricId, 
                                         std::function<void(bool, const QString&, const QString&, const QString&)> callback)
{
    if (lyricId.isEmpty()) {
        if (callback) callback(false, "", "", "lyricId 为空");
        return;
    }

    QUrl url(m_gdstudioBaseUrl);
    QUrlQuery query;
    query.addQueryItem("types", "lyric");
    query.addQueryItem("source", source.isEmpty() ? "netease" : source);
    query.addQueryItem("id", lyricId);
    url.setQuery(query);

    QNetworkReply *reply = m_netManager->get(buildRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, "", "", reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            if (callback) callback(false, "", "", "无法解析歌词响应");
            return;
        }

        QJsonObject obj = doc.object();
        QString lrc = obj["lyric"].toString();
        QString tlyric = obj["tlyric"].toString();

        if (callback) callback(true, lrc, tlyric, "");
    });
}

// =========================================================================
// 洛雪 / 聚合解析引擎实现 (LX-Music Custom Source + Native Fallback)
// =========================================================================

// 辅助：清洗酷我单引号伪JSON为标准JSON
static QByteArray kuwoToJson(const QByteArray &raw)
{
    QString s = QString::fromUtf8(raw);
    s.replace("'", "\"");
    return s.toUtf8();
}

void MusicApiService::searchLX(const QString &keyword, 
                               const QString &source, 
                               int count, 
                               int page, 
                               std::function<void(bool, const QVector<SongInfo>&, const QString&)> callback)
{
    if (keyword.trimmed().isEmpty()) {
        if (callback) callback(false, {}, "搜索关键词不能为空");
        return;
    }

    QString actualSource = source.trimmed().toLower();

    // 1. 酷我检索通道 (全面支持 VIP 曲目原声与专辑图)
    if (actualSource == "kuwo") {
        QUrl url("http://search.kuwo.cn/r.s");
        QUrlQuery query;
        query.addQueryItem("client", "kt");
        query.addQueryItem("all", keyword.trimmed());
        query.addQueryItem("ft", "music");
        query.addQueryItem("cluster", "0");
        query.addQueryItem("strategy", "2012");
        query.addQueryItem("encoding", "utf8");
        query.addQueryItem("rformat", "json");
        query.addQueryItem("vipver", "1");
        query.addQueryItem("p", QString::number(page > 0 ? page - 1 : 0));
        query.addQueryItem("rn", QString::number(count <= 0 ? 20 : count));
        url.setQuery(query);

        std::cout << "[LX/Kuwo] 搜索: " << url.toString().toStdString() << std::endl;

        QNetworkReply *reply = m_netManager->get(buildRequest(url));
        connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                if (callback) callback(false, {}, reply->errorString());
                return;
            }

            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(kuwoToJson(data));
            if (!doc.isObject()) {
                if (callback) callback(false, {}, "酷我返回格式无法解析");
                return;
            }

            QVector<SongInfo> songs;
            QJsonArray arr = doc.object()["abslist"].toArray();
            for (const auto &val : arr) {
                if (!val.isObject()) continue;
                QJsonObject obj = val.toObject();

                SongInfo s;
                s.id = obj["DC_TARGETID"].toString();
                if (s.id.isEmpty()) s.id = obj["MUSICRID"].toString().remove("MUSIC_");
                s.source = "kuwo";
                s.name = obj["SONGNAME"].toString().replace("&nbsp;", " ");
                s.artist = obj["ARTIST"].toString().replace("&nbsp;", " ");
                s.album = obj["ALBUM"].toString().replace("&nbsp;", " ");
                s.lyricId = s.id;
                
                // 提取图片
                if (obj.contains("hts_MVPIC") && !obj["hts_MVPIC"].toString().isEmpty()) {
                    s.picUrl = obj["hts_MVPIC"].toString();
                } else if (obj.contains("web_albumpic_short") && !obj["web_albumpic_short"].toString().isEmpty()) {
                    s.picUrl = "https://img1.kuwo.cn/star/albumcover/" + obj["web_albumpic_short"].toString();
                }

                if (!s.id.isEmpty() && !s.name.isEmpty()) {
                    songs.append(s);
                }
            }

            std::cout << "[LX/Kuwo] 搜索成功，找到 " << songs.size() << " 首歌" << std::endl;
            if (callback) callback(true, songs, "");
        });
        return;
    }

    // 2. 默认网易云开放检索通道
    QUrl url("https://music.163.com/api/search/get/web");
    QUrlQuery query;
    query.addQueryItem("s", keyword.trimmed());
    query.addQueryItem("type", "1");
    query.addQueryItem("offset", QString::number(page > 0 ? (page - 1) * count : 0));
    query.addQueryItem("limit", QString::number(count <= 0 ? 20 : count));
    url.setQuery(query);

    std::cout << "[LX/Netease] 搜索: " << url.toString().toStdString() << std::endl;

    QNetworkReply *reply = m_netManager->get(buildRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, callback, keyword]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, {}, reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject() || !doc.object().contains("result")) {
            if (callback) callback(false, {}, "网易云搜索结果解析失败");
            return;
        }

        QVector<SongInfo> songs;
        QJsonArray arr = doc.object()["result"].toObject()["songs"].toArray();
        for (const auto &val : arr) {
            if (!val.isObject()) continue;
            QJsonObject obj = val.toObject();

            SongInfo s;
            s.id = QString::number(obj["id"].toVariant().toLongLong());
            s.name = obj["name"].toString();
            s.source = "netease";
            s.lyricId = s.id;

            // 专辑与封面
            if (obj.contains("album") && obj["album"].isObject()) {
                QJsonObject alb = obj["album"].toObject();
                s.album = alb["name"].toString();
                if (alb.contains("picUrl") && !alb["picUrl"].toString().isEmpty()) {
                    s.picUrl = alb["picUrl"].toString();
                } else if (alb.contains("artist") && alb["artist"].isObject()) {
                    s.picUrl = alb["artist"].toObject()["img1v1Url"].toString();
                }
            }

            // 歌手处理
            if (obj["artists"].isArray()) {
                QStringList artList;
                for (const auto &artVal : obj["artists"].toArray()) {
                    artList << artVal.toObject()["name"].toString();
                }
                s.artist = artList.join(" / ");
            }

            if (!s.id.isEmpty() && !s.name.isEmpty()) {
                songs.append(s);
            }
        }

        std::cout << "[LX/Netease] 搜索成功，找到 " << songs.size() << " 首歌" << std::endl;
        if (callback) callback(true, songs, "");
    });
}

// 洛雪 / 聚合获取直链
void MusicApiService::fetchPlayUrlLX(const QString &source, 
                                     const QString &id, 
                                     int br, 
                                     std::function<void(bool, const QString&, int, const QString&)> callback)
{
    QString actualSource = source.trimmed().toLower();
    if (actualSource.isEmpty()) actualSource = "netease";

    // 辅助本地直链解析通道 (Native Stream Fallback)
    auto resolveNative = [this, actualSource, id, br, callback]() {
        // 酷我 320k 纯净全曲原声 (车载高品质通道，无免费时长语音水印)
        if (actualSource == "kuwo") {
            // 通道 1: haitangw 车载原声解析服务
            QUrl url(QString("https://musicapi.haitangw.net/music/kw.php?id=%1&level=exhigh&type=json").arg(id));
            QNetworkReply *reply = m_netManager->get(buildRequest(url));
            connect(reply, &QNetworkReply::finished, this, [this, reply, id, callback]() {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
                    if (doc.isObject()) {
                        QJsonObject obj = doc.object();
                        QString playUrl;
                        if (obj.contains("data") && obj["data"].isObject()) {
                            playUrl = obj["data"].toObject()["url"].toString();
                        } else if (obj.contains("url")) {
                            playUrl = obj["url"].toString();
                        }
                        if (!playUrl.isEmpty() && !playUrl.contains("404")) {
                            std::cout << "[LX/Kuwo] 320k 车载纯净全曲解析成功: " << playUrl.toStdString() << std::endl;
                            if (callback) callback(true, playUrl, 320, "");
                            return;
                        }
                    }
                }

                // 备用通道 2: nxinxz 车载原声解析
                QUrl url2(QString("http://music.nxinxz.com/kw.php?id=%1&level=exhigh&type=json").arg(id));
                QNetworkReply *reply2 = m_netManager->get(buildRequest(url2));
                connect(reply2, &QNetworkReply::finished, this, [reply2, callback]() {
                    reply2->deleteLater();
                    if (reply2->error() == QNetworkReply::NoError) {
                        QJsonDocument doc2 = QJsonDocument::fromJson(reply2->readAll());
                        if (doc2.isObject() && doc2.object().contains("data")) {
                            QString playUrl2 = doc2.object()["data"].toObject()["url"].toString();
                            if (!playUrl2.isEmpty() && !playUrl2.contains("404")) {
                                std::cout << "[LX/Kuwo] 320k 备用通道解析成功: " << playUrl2.toStdString() << std::endl;
                                if (callback) callback(true, playUrl2, 320, "");
                                return;
                            }
                        }
                    }
                    if (callback) callback(false, "", 0, "酷我纯净音源解析失败");
                });
            });
            return;
        }

        // 网易云官方直链 (非 VIP 歌曲直接播放，VIP 歌曲重定向到 404)
        if (actualSource == "netease") {
            QString playUrl = QString("https://music.163.com/song/media/outer/url?id=%1.mp3").arg(id);
            if (callback) callback(true, playUrl, 320, "");
            return;
        }

        if (callback) callback(false, "", 0, "暂不支持该源的本地原生解析");
    };

    // 如果用户配置了自定义 LX-Music 音源接口，优先走 LX-Music 自定义协议
    if (!m_lxApiUrl.isEmpty()) {
        QString lxSource = "wy";
        if (actualSource == "kuwo") lxSource = "kw";
        else if (actualSource == "netease") lxSource = "wy";
        else if (actualSource == "tencent" || actualSource == "qq") lxSource = "tx";
        else if (actualSource == "kugou") lxSource = "kg";
        else if (actualSource == "migu") lxSource = "mg";

        QString quality = (br >= 320) ? "320k" : "128k";
        QString baseUrl = m_lxApiUrl;
        while (baseUrl.endsWith('/')) baseUrl.chop(1);

        QUrl url(QString("%1/url/%2/%3/%4").arg(baseUrl, lxSource, id, quality));
        std::cout << "[LX-Music] 调用自定义接口: " << url.toString().toStdString() << std::endl;

        QNetworkRequest req = buildRequest(url, m_lxRequestKey);
        QNetworkReply *reply = m_netManager->get(req);

        connect(reply, &QNetworkReply::finished, this, [reply, callback, resolveNative]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(data);
                QString playUrl;
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("data") && obj["data"].isObject()) {
                        playUrl = obj["data"].toObject()["url"].toString();
                    } else if (obj.contains("data") && obj["data"].isString()) {
                        playUrl = obj["data"].toString();
                    } else if (obj.contains("url")) {
                        playUrl = obj["url"].toString();
                    }
                }
                if (!playUrl.isEmpty()) {
                    std::cout << "[LX-Music] 获得自定义音源直链: " << playUrl.toStdString() << std::endl;
                    if (callback) callback(true, playUrl, 320, "");
                    return;
                }
            }

            // 自定义 LX 接口解析失败时，无缝降级到本地直链通道
            std::cout << "[LX-Music] 自定义音源未返回链接，自动降级至全网直链通道..." << std::endl;
            resolveNative();
        });
        return;
    }

    // 未配置自定义 LX 接口时，直接走内建高码率直链通道
    resolveNative();
}

void MusicApiService::fetchPicUrlLX(const QString &source, 
                                    const QString &picId, 
                                    int, 
                                    std::function<void(bool, const QString&, const QString&)> callback)
{
    if (picId.startsWith("http://") || picId.startsWith("https://")) {
        if (callback) callback(true, picId, "");
        return;
    }

    if (source == "netease" && !picId.isEmpty()) {
        QUrl url(QString("https://music.163.com/api/song/detail/?id=%1&ids=[%1]").arg(picId));
        QNetworkReply *reply = m_netManager->get(buildRequest(url));
        connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                if (callback) callback(false, "", reply->errorString());
                return;
            }

            QByteArray data = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject() && doc.object().contains("songs")) {
                QJsonArray songs = doc.object()["songs"].toArray();
                if (!songs.isEmpty()) {
                    QString picUrl = songs[0].toObject()["album"].toObject()["picUrl"].toString();
                    if (!picUrl.isEmpty()) {
                        if (callback) callback(true, picUrl, "");
                        return;
                    }
                }
            }
            if (callback) callback(false, "", "未提取到封面图");
        });
        return;
    }

    if (callback) callback(false, "", "无有效封面信息");
}

void MusicApiService::fetchLyricLX(const QString &source, 
                                   const QString &lyricId, 
                                   std::function<void(bool, const QString&, const QString&, const QString&)> callback)
{
    if (lyricId.isEmpty()) {
        if (callback) callback(false, "", "", "歌词 ID 为空");
        return;
    }

    // 网易云开放歌词服务（支持原文与中文翻译，覆盖全网曲库）
    QUrl url(QString("https://music.163.com/api/song/lyric?os=pc&id=%1&lv=-1&kv=-1&tv=-1").arg(lyricId));
    QNetworkReply *reply = m_netManager->get(buildRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (callback) callback(false, "", "", reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) {
            if (callback) callback(false, "", "", "歌词响应解析失败");
            return;
        }

        QJsonObject obj = doc.object();
        QString lrc, tlyric;
        if (obj.contains("lrc") && obj["lrc"].isObject()) {
            lrc = obj["lrc"].toObject()["lyric"].toString();
        }
        if (obj.contains("tlyric") && obj["tlyric"].isObject()) {
            tlyric = obj["tlyric"].toObject()["lyric"].toString();
        }

        if (callback) callback(true, lrc, tlyric, "");
    });
}

// =========================================================================
// 一站式曲目详情解析 (智能并发 + VIP 跨源自动兜底)
// =========================================================================
void MusicApiService::resolveSongDetails(SongInfo song, 
                                         std::function<void(bool, const SongInfo&, const QString&, const QString&)> callback)
{
    struct Context {
        SongInfo song;
        QString lyric;
        QString tlyric;
        bool playUrlDone = false;
        bool picUrlDone = false;
        bool lyricDone = false;
        bool playUrlSuccess = false;
        bool isCrossResolved = false;
    };

    auto ctx = std::make_shared<Context>();
    ctx->song = song;

    auto checkFinished = [this, ctx, callback]() {
        if (ctx->playUrlDone && ctx->picUrlDone && ctx->lyricDone) {
            // 如果解析成功且有直链
            if (ctx->playUrlSuccess && !ctx->song.playUrl.isEmpty()) {
                if (callback) callback(true, ctx->song, ctx->lyric, ctx->tlyric);
            } else {
                if (callback) callback(false, ctx->song, "", "");
            }
        }
    };

    // 跨源兜底函数：如果网易云等歌曲是 VIP 歌曲播放不了，自动跨源查找酷我 320k 音频
    auto tryKuwoCrossFallback = [this, ctx, checkFinished]() {
        if (ctx->isCrossResolved) {
            ctx->playUrlDone = true;
            ctx->playUrlSuccess = false;
            checkFinished();
            return;
        }
        ctx->isCrossResolved = true;

        std::cout << "[LX-Music] 启动酷我 320k 跨源兜底检索: " << ctx->song.name.toStdString() << " " << ctx->song.artist.toStdString() << std::endl;
        QString kwKw = QString("%1 %2").arg(ctx->song.name, ctx->song.artist).trimmed();

        searchLX(kwKw, "kuwo", 1, 1, [this, ctx, checkFinished](bool ok, const QVector<SongInfo>& songs, const QString &) {
            if (ok && !songs.isEmpty()) {
                fetchPlayUrlLX("kuwo", songs[0].id, 320, [ctx, checkFinished](bool pOk, const QString &pUrl, int br, const QString &) {
                    ctx->playUrlDone = true;
                    ctx->playUrlSuccess = pOk;
                    if (pOk) {
                        ctx->song.playUrl = pUrl;
                        ctx->song.br = br;
                        std::cout << "[LX-Music] 酷我 320k 跨源兜底成功: " << pUrl.toStdString() << std::endl;
                    }
                    checkFinished();
                });
            } else {
                // 如果歌名+歌手检索为空，尝试仅使用歌名检索
                searchLX(ctx->song.name.trimmed(), "kuwo", 1, 1, [this, ctx, checkFinished](bool ok2, const QVector<SongInfo>& songs2, const QString &) {
                    if (ok2 && !songs2.isEmpty()) {
                        fetchPlayUrlLX("kuwo", songs2[0].id, 320, [ctx, checkFinished](bool pOk, const QString &pUrl, int br, const QString &) {
                            ctx->playUrlDone = true;
                            ctx->playUrlSuccess = pOk;
                            if (pOk) {
                                ctx->song.playUrl = pUrl;
                                ctx->song.br = br;
                                std::cout << "[LX-Music] 酷我 320k 歌名兜底成功: " << pUrl.toStdString() << std::endl;
                            }
                            checkFinished();
                        });
                    } else {
                        ctx->playUrlDone = true;
                        ctx->playUrlSuccess = false;
                        checkFinished();
                    }
                });
            }
        });
    };

    // 1. 获取 PlayUrl
    fetchPlayUrl(song.source, song.id, song.br, [this, ctx, song, checkFinished, tryKuwoCrossFallback](bool success, const QString &url, int br, const QString &) {
        if (success && !url.isEmpty()) {
            // 如果是网易云 outer URL，进行探测是否为 VIP (404)
            if (m_engine == MusicEngine::LX_Music && song.source == "netease" && url.contains("outer/url")) {
                QNetworkRequest headReq(url);
                headReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
                QNetworkReply *headReply = m_netManager->head(headReq);
                connect(headReply, &QNetworkReply::finished, this, [headReply, ctx, url, br, checkFinished, tryKuwoCrossFallback]() {
                    headReply->deleteLater();
                    QString rawLoc = QString::fromUtf8(headReply->rawHeader("Location"));
                    QString hdrLoc = headReply->header(QNetworkRequest::LocationHeader).toString();
                    QUrl redUrl = headReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
                    int statusCode = headReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

                    QString targetLoc = !rawLoc.isEmpty() ? rawLoc : (!hdrLoc.isEmpty() ? hdrLoc : redUrl.toString());

                    std::cout << "[LX-Music] 网易云 outer URL 探测: status=" << statusCode << ", Location=" << targetLoc.toStdString() << std::endl;

                    if (targetLoc.contains("404") || statusCode == 404 || targetLoc.isEmpty()) {
                        std::cout << "[LX-Music] 网易云音频为 VIP 版权曲目 (404)，自动触发酷我 320k 跨源原声解析..." << std::endl;
                        tryKuwoCrossFallback();
                        return;
                    }

                    ctx->playUrlDone = true;
                    ctx->playUrlSuccess = true;
                    ctx->song.playUrl = targetLoc;
                    ctx->song.br = br;
                    std::cout << "[LX-Music] 网易云非 VIP 音频直链解析成功: " << targetLoc.toStdString() << std::endl;
                    checkFinished();
                });
                return;
            }

            ctx->playUrlDone = true;
            ctx->playUrlSuccess = true;
            ctx->song.playUrl = url;
            ctx->song.br = br;
            checkFinished();
        } else {
            if (m_engine == MusicEngine::LX_Music && song.source != "kuwo") {
                tryKuwoCrossFallback();
            } else {
                ctx->playUrlDone = true;
                ctx->playUrlSuccess = false;
                checkFinished();
            }
        }
    });

    // 2. 获取 PicUrl
    if (!song.picUrl.isEmpty()) {
        ctx->picUrlDone = true;
        checkFinished();
    } else if (!song.picId.isEmpty()) {
        fetchPicUrl(song.source, song.picId, 500, [ctx, checkFinished](bool success, const QString &picUrl, const QString &) {
            ctx->picUrlDone = true;
            if (success) ctx->song.picUrl = picUrl;
            checkFinished();
        });
    } else {
        ctx->picUrlDone = true;
        checkFinished();
    }

    // 3. 获取 Lyric
    QString lid = song.lyricId.isEmpty() ? song.id : song.lyricId;
    if (m_engine == MusicEngine::LX_Music && song.source == "kuwo") {
        // 如果是酷我歌曲，跨源检索网易云获取精准同步歌词与中文翻译
        QString lkw = QString("%1 %2").arg(song.name, song.artist).trimmed();
        QUrl sUrl("https://music.163.com/api/search/get/web");
        QUrlQuery sQuery;
        sQuery.addQueryItem("s", lkw);
        sQuery.addQueryItem("type", "1");
        sQuery.addQueryItem("offset", "0");
        sQuery.addQueryItem("limit", "1");
        sUrl.setQuery(sQuery);

        QNetworkReply *sReply = m_netManager->get(buildRequest(sUrl));
        connect(sReply, &QNetworkReply::finished, this, [this, sReply, ctx, checkFinished]() {
            sReply->deleteLater();
            QString neteaseId;
            if (sReply->error() == QNetworkReply::NoError) {
                QJsonDocument sDoc = QJsonDocument::fromJson(sReply->readAll());
                if (sDoc.isObject() && sDoc.object().contains("result")) {
                    QJsonArray sArr = sDoc.object()["result"].toObject()["songs"].toArray();
                    if (!sArr.isEmpty()) {
                        neteaseId = QString::number(sArr[0].toObject()["id"].toVariant().toLongLong());
                    }
                }
            }

            if (!neteaseId.isEmpty()) {
                fetchLyricLX("netease", neteaseId, [ctx, checkFinished](bool success, const QString &lrc, const QString &tlyric, const QString &) {
                    ctx->lyricDone = true;
                    if (success) {
                        ctx->lyric = lrc;
                        ctx->tlyric = tlyric;
                    }
                    checkFinished();
                });
            } else {
                ctx->lyricDone = true;
                checkFinished();
            }
        });
    } else {
        fetchLyric(song.source, lid, [ctx, checkFinished](bool success, const QString &lrc, const QString &tlyric, const QString &) {
            ctx->lyricDone = true;
            if (success) {
                ctx->lyric = lrc;
                ctx->tlyric = tlyric;
            }
            checkFinished();
        });
    }
}

// 测试连通性
void MusicApiService::testConnection(std::function<void(bool, const QString&)> callback)
{
    QElapsedTimer *timer = new QElapsedTimer();
    timer->start();

    if (m_engine == MusicEngine::GDStudio) {
        QUrl url(m_gdstudioBaseUrl + "?types=search&source=netease&name=test&count=1");
        QNetworkReply *reply = m_netManager->get(buildRequest(url));
        connect(reply, &QNetworkReply::finished, this, [reply, callback, timer]() {
            reply->deleteLater();
            qint64 elapsed = timer->elapsed();
            delete timer;
            if (reply->error() == QNetworkReply::NoError) {
                if (callback) callback(true, QString("GD音乐台连接正常 (延迟: %1 ms)").arg(elapsed));
            } else {
                if (callback) callback(false, QString("GD音乐台请求失败: %1 (延迟: %2 ms)").arg(reply->errorString()).arg(elapsed));
            }
        });
        return;
    }

    // 洛雪 / 聚合引擎测试
    if (!m_lxApiUrl.isEmpty()) {
        QString baseUrl = m_lxApiUrl;
        while (baseUrl.endsWith('/')) baseUrl.chop(1);
        QUrl url(QString("%1/url/kw/228908/128k").arg(baseUrl));
        QNetworkRequest req = buildRequest(url, m_lxRequestKey);
        QNetworkReply *reply = m_netManager->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, callback, timer]() {
            reply->deleteLater();
            qint64 elapsed = timer->elapsed();
            delete timer;
            if (reply->error() == QNetworkReply::NoError) {
                if (callback) callback(true, QString("洛雪自定义音源连接成功 (延迟: %1 ms)").arg(elapsed));
            } else {
                if (callback) callback(false, QString("洛雪自定义接口响应异常: %1 (耗时: %2 ms)").arg(reply->errorString()).arg(elapsed));
            }
        });
        return;
    }

    // 测试内建酷我 320k 纯净原声接口 (车载原声通道)
    QUrl url("https://musicapi.haitangw.net/music/kw.php?id=228908&level=exhigh&type=json");
    QNetworkReply *reply = m_netManager->get(buildRequest(url));
    connect(reply, &QNetworkReply::finished, this, [reply, callback, timer]() {
        reply->deleteLater();
        qint64 elapsed = timer->elapsed();
        delete timer;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject() && doc.object().contains("data") && !doc.object()["data"].toObject()["url"].toString().isEmpty()) {
                if (callback) callback(true, QString("洛雪/车载原声纯净通道畅通 (周杰伦《晴天》全曲原声秒开，延迟: %1 ms)").arg(elapsed));
                return;
            }
        }
        if (callback) callback(false, QString("通道响应异常: %1 (耗时: %2 ms)").arg(reply->errorString()).arg(elapsed));
    });
}
