#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <functional>
#include "MusicFavoriteDb.hpp"

enum class PlaybackMode {
    ListLoop = 0,   // 列表循环
    SingleLoop = 1, // 单曲循环
    Random = 2      // 随机播放
};

struct LyricLine {
    qint64 timestampMs = 0;
    QString text;
    QString translation;
};

class MusicPlayerManager : public QObject
{
public:
    static MusicPlayerManager* instance();

    // 播放列表操作
    void playSong(const SongInfo &song);
    void playPlaylist(const QVector<SongInfo> &list, int startIndex = 0);
    void addToPlaylist(const SongInfo &song);
    int addBatchToPlaylist(const QVector<SongInfo> &songs);
    void removeFromPlaylist(int index);
    void clearPlaylist();
    void autoRefillRecommendationsIfNeeded(bool autoPlay = true);
    void recommendSongsByMode(const QString &mode, int count, std::function<void(const QVector<SongInfo>&)> callback);
    const QVector<SongInfo>& playlist() const { return m_playlist; }
    int currentIndex() const { return m_currentIndex; }
    SongInfo currentSong() const;


    // 播放控制
    void play();
    void pause();
    void togglePlay();
    void playNext();
    void playPrevious();
    void seek(qint64 positionMs);
    void setVolume(float volume); // 0.0 ~ 1.0
    float volume() const;
    void setPlaybackMode(PlaybackMode mode);
    PlaybackMode playbackMode() const { return m_mode; }
    void setAutoRemovePlayed(bool enable) { m_autoRemovePlayed = enable; }
    bool autoRemovePlayed() const { return m_autoRemovePlayed; }

    // 收藏控制
    void toggleFavoriteCurrent();
    bool isCurrentSongFavorite() const;

    // 当前状态
    bool isPlaying() const;
    qint64 position() const;
    qint64 duration() const;
    const QVector<LyricLine>& lyrics() const { return m_parsedLyrics; }
    int currentLyricIndex() const { return m_currentLyricIndex; }

    // 回调注册接口 (支持多监听者广播)
    void addSongChangedListener(std::function<void(const SongInfo&)> cb) { m_songChangedListeners.append(cb); }
    void addPlayStateListener(std::function<void(bool)> cb) { m_playStateListeners.append(cb); }
    void addPositionListener(std::function<void(qint64, qint64)> cb) { m_positionListeners.append(cb); }
    void addLyricLineListener(std::function<void(int, const QString&, const QString&)> cb) { m_lyricLineListeners.append(cb); }
    void addPlaylistUpdatedListener(std::function<void()> cb) { m_playlistUpdatedListeners.append(cb); }
    void addFavoriteStateListener(std::function<void(bool)> cb) { m_favoriteStateListeners.append(cb); }
    void addErrorOccurredListener(std::function<void(const QString&)> cb) { m_errorOccurredListeners.append(cb); }

    // 兼容原有单接口绑定
    void setOnSongChanged(std::function<void(const SongInfo&)> cb) { addSongChangedListener(cb); }
    void setOnPlayStateChanged(std::function<void(bool)> cb) { addPlayStateListener(cb); }
    void setOnPositionChanged(std::function<void(qint64, qint64)> cb) { addPositionListener(cb); }
    void setOnLyricLineChanged(std::function<void(int, const QString&, const QString&)> cb) { addLyricLineListener(cb); }
    void setOnPlaylistUpdated(std::function<void()> cb) { addPlaylistUpdatedListener(cb); }
    void setOnFavoriteStateChanged(std::function<void(bool)> cb) { addFavoriteStateListener(cb); }
    void setOnErrorOccurred(std::function<void(const QString&)> cb) { addErrorOccurredListener(cb); }

private:
    explicit MusicPlayerManager(QObject *parent = nullptr);
    ~MusicPlayerManager();

    void onPlayerPositionChanged(qint64 position);
    void onPlayerDurationChanged(qint64 duration);
    void onPlayerPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);

    void parseLrc(const QString &lrc, const QString &tlyric);
    void updateLyricLine(qint64 positionMs);

    void notifyPlaylistUpdated();
    void notifyFavoriteStateChanged(bool isFav);
    void notifyPositionChanged(qint64 pos, qint64 dur);
    void notifyPlayStateChanged(bool isPlaying);
    void notifyLyricLineChanged(int idx, const QString &text, const QString &trans);
    void notifyErrorOccurred(const QString &err);

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;

    QVector<SongInfo> m_playlist;
    int m_currentIndex = -1;
    PlaybackMode m_mode = PlaybackMode::ListLoop;

    QVector<LyricLine> m_parsedLyrics;
    int m_currentLyricIndex = -1;
    bool m_isCurrentSongFav = false;
    bool m_autoRemovePlayed = true;
    bool m_isRefilling = false;
    int m_consecutiveErrors = 0;

    // 观察者多监听者回调列表
    QVector<std::function<void(const SongInfo&)>> m_songChangedListeners;
    QVector<std::function<void(bool)>> m_playStateListeners;
    QVector<std::function<void(qint64, qint64)>> m_positionListeners;
    QVector<std::function<void(int, const QString&, const QString&)>> m_lyricLineListeners;
    QVector<std::function<void()>> m_playlistUpdatedListeners;
    QVector<std::function<void(bool)>> m_favoriteStateListeners;
    QVector<std::function<void(const QString&)>> m_errorOccurredListeners;
};
