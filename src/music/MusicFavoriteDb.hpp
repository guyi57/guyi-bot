#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QRecursiveMutex>

struct SongInfo {
    QString id;         // track_id
    QString source;     // netease, tencent, kuwo, apple, etc.
    QString name;       // 歌曲名
    QString artist;     // 歌手
    QString album;      // 专辑
    QString picId;      // 封面 ID
    QString picUrl;     // 封面图片 URL (可选缓存)
    QString lyricId;    // 歌词 ID
    QString playUrl;    // 播放音频 URL (动态解析)
    int br = 999;       // 音质
    qint64 createdAt = 0; // 收藏时间

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["source"] = source;
        obj["name"] = name;
        obj["artist"] = artist;
        obj["album"] = album;
        obj["pic_id"] = picId;
        obj["pic_url"] = picUrl;
        obj["lyric_id"] = lyricId;
        obj["play_url"] = playUrl;
        obj["br"] = br;
        obj["created_at"] = createdAt;
        return obj;
    }

    static SongInfo fromJson(const QJsonObject &obj) {
        SongInfo s;
        s.id = obj["id"].toString();
        s.source = obj["source"].toString();
        s.name = obj["name"].toString();
        s.artist = obj["artist"].toString();
        s.album = obj["album"].toString();
        s.picId = obj["pic_id"].toString();
        s.picUrl = obj["pic_url"].toString();
        s.lyricId = obj["lyric_id"].toString();
        s.playUrl = obj["play_url"].toString();
        s.br = obj["br"].toInt(999);
        s.createdAt = obj["created_at"].toVariant().toLongLong();
        return s;
    }
};

class MusicFavoriteDb {
public:
    static MusicFavoriteDb* instance();

    bool initDb();
    bool addFavorite(const SongInfo &song);
    bool removeFavorite(const QString &source, const QString &id);
    bool isFavorite(const QString &source, const QString &id);
    QVector<SongInfo> getFavorites(const QString &keyword = "");
    QJsonArray getAllFavoritesJson();
    int getFavoriteCount();

    // 播放列表持久化
    void savePlaylist(const QVector<SongInfo> &playlist, int currentIndex);
    QVector<SongInfo> loadPlaylist(int &outCurrentIndex);

    // 搜索历史管理
    void addSearchHistory(const QString &keyword);
    QStringList getSearchHistories(int limit = 15);
    void clearSearchHistory();
    void removeSearchHistory(const QString &keyword);

    // 喜好标签（Preference Tags）管理
    QStringList getPreferenceTags();
    bool addPreferenceTag(const QString &tag);
    bool removePreferenceTag(const QString &tag);
    void setPreferenceTags(const QStringList &tags);
    void clearPreferenceTags();

    // 推荐模式 (familiar: 熟悉模式, explore: 探索模式, random: 随机模式)
    QString getRecommendationMode();
    void setRecommendationMode(const QString &mode);

    // 近期推荐历史防重缓存（保留最近 100 首）
    void recordRecentRecommendations(const QVector<SongInfo> &songs);
    bool isRecentlyRecommended(const QString &songName, const QString &artist = "");
    QSet<QString> getRecentRecommendationKeys();
    void clearRecentRecommendations();

private:
    MusicFavoriteDb();
    ~MusicFavoriteDb();

    void *m_sqliteHandle = nullptr;
    QString m_dbPath;
    mutable QRecursiveMutex m_mutex;
};
