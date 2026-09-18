#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>
#include "MusicFavoriteDb.hpp"

enum class MusicEngine {
    LX_Music = 0,   // 洛雪/全网聚合音源 (LX-Music 自定义源 + 全网 320k 直链)
    GDStudio = 1    // GD音乐台 (原版 Meting 协议)
};

class MusicApiService : public QObject
{
public:
    static MusicApiService* instance();

    // 引擎切换与配置
    MusicEngine currentEngine() const;
    void setEngine(MusicEngine engine);
    static QString engineDisplayName(MusicEngine engine);

    // 洛雪自定义 API 配置 (可选自建或第三方音源服务)
    QString lxApiUrl() const;
    void setLxApiUrl(const QString &url);

    QString lxRequestKey() const;
    void setLxRequestKey(const QString &key);

    // 恢复默认配置
    void resetToDefaults();

    // 引擎切换回调监听
    void setOnEngineChanged(std::function<void(MusicEngine)> callback) { m_onEngineChanged = callback; }

    // 测试音源连通性
    void testConnection(std::function<void(bool ok, const QString &msg)> callback);

    // 1. 搜索曲目接口 (根据当前 engine 智能分发)
    void search(const QString &keyword, 
                const QString &source = "netease", 
                int count = 20, 
                int page = 1, 
                std::function<void(bool success, const QVector<SongInfo>& songs, const QString &err)> callback = nullptr);

    // 2. 获取播放直链
    void fetchPlayUrl(const QString &source, 
                      const QString &id, 
                      int br = 999, 
                      std::function<void(bool success, const QString &url, int actualBr, const QString &err)> callback = nullptr);

    // 3. 获取专辑封面图片
    void fetchPicUrl(const QString &source, 
                     const QString &picId, 
                     int size = 500, 
                     std::function<void(bool success, const QString &picUrl, const QString &err)> callback = nullptr);

    // 4. 获取歌词接口 (返回 lrc 原文与 tlyric 中文翻译)
    void fetchLyric(const QString &source, 
                    const QString &lyricId, 
                    std::function<void(bool success, const QString &lrc, const QString &tlyric, const QString &err)> callback = nullptr);

    // 5. 便捷一站式解析：传入 SongInfo，自动填充 playUrl, picUrl, lyric
    void resolveSongDetails(SongInfo song, 
                            std::function<void(bool success, const SongInfo &resolvedSong, const QString &lrc, const QString &tlyric)> callback);

    // 可用音乐源列表
    static QStringList availableSources();
    static QString sourceDisplayName(const QString &source);

private:
    explicit MusicApiService(QObject *parent = nullptr);
    ~MusicApiService();

    void loadSettings();
    void saveSettings();

    // --- 原版 GDStudio (Meting) 模式实现 ---
    void searchGDStudio(const QString &keyword, const QString &source, int count, int page,
                        std::function<void(bool, const QVector<SongInfo>&, const QString&)> callback);
    void fetchPlayUrlGDStudio(const QString &source, const QString &id, int br,
                              std::function<void(bool, const QString&, int, const QString&)> callback);
    void fetchPicUrlGDStudio(const QString &source, const QString &picId, int size,
                             std::function<void(bool, const QString&, const QString&)> callback);
    void fetchLyricGDStudio(const QString &source, const QString &lyricId,
                            std::function<void(bool, const QString&, const QString&, const QString&)> callback);

    // --- 洛雪 / 全网聚合引擎实现 ---
    void searchLX(const QString &keyword, const QString &source, int count, int page,
                  std::function<void(bool, const QVector<SongInfo>&, const QString&)> callback);
    void fetchPlayUrlLX(const QString &source, const QString &id, int br,
                        std::function<void(bool, const QString&, int, const QString&)> callback);
    void fetchPicUrlLX(const QString &source, const QString &picId, int size,
                       std::function<void(bool, const QString&, const QString&)> callback);
    void fetchLyricLX(const QString &source, const QString &lyricId,
                      std::function<void(bool, const QString&, const QString&, const QString&)> callback);

    QNetworkAccessManager *m_netManager = nullptr;
    MusicEngine m_engine = MusicEngine::LX_Music; // 默认使用洛雪/聚合引擎
    QString m_gdstudioBaseUrl = "https://music-api.gdstudio.xyz/api.php";
    QString m_lxApiUrl;       // 用户可配置的自定义 LX 音源服务器地址
    QString m_lxRequestKey;   // 用户可配置的 X-Request-Key
    std::function<void(MusicEngine)> m_onEngineChanged = nullptr;
};
