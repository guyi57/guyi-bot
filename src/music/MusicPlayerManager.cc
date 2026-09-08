#include "MusicPlayerManager.hpp"
#include "MusicApiService.hpp"
#include "BehaviorEngine.hpp"
#include "ShijimaWidget.hpp"
#include "PetEventBus.hpp"
#include "PetDiaryManager.hpp"
#include <QUrl>
#include <QTimer>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QJsonObject>
#include <iostream>


MusicPlayerManager* MusicPlayerManager::instance()
{
    static MusicPlayerManager s_instance;
    return &s_instance;
}

MusicPlayerManager::MusicPlayerManager(QObject *parent)
    : QObject(parent)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.85f);

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        onPlayerPositionChanged(pos);
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        onPlayerDurationChanged(dur);
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        onPlayerPlaybackStateChanged(state);
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        onMediaStatusChanged(status);
    });

    // 注册切歌时桌宠原地弹出气泡提示并向事件总线广播
    addSongChangedListener([](const SongInfo &song) {
        if (!song.id.isEmpty() && !song.name.isEmpty()) {
            QJsonObject payload;
            payload["song_name"] = song.name;
            payload["artist"] = song.artist;
            PetEventBus::instance()->emitEvent("music.playing", payload);
            PetDiaryManager::instance()->recordMusicPlayed();
        }
    });


    // 自动从 SQLite 恢复上次持久化的播放列表与索引
    int savedIdx = 0;
    m_playlist = MusicFavoriteDb::instance()->loadPlaylist(savedIdx);
    if (!m_playlist.isEmpty()) {
        m_currentIndex = (savedIdx >= 0 && savedIdx < m_playlist.size()) ? savedIdx : 0;
    }
}

MusicPlayerManager::~MusicPlayerManager()
{
}

void MusicPlayerManager::notifyPlaylistUpdated()
{
    for (auto &cb : m_playlistUpdatedListeners) if (cb) cb();
}

void MusicPlayerManager::notifyFavoriteStateChanged(bool isFav)
{
    for (auto &cb : m_favoriteStateListeners) if (cb) cb(isFav);
}

void MusicPlayerManager::notifyPositionChanged(qint64 pos, qint64 dur)
{
    for (auto &cb : m_positionListeners) if (cb) cb(pos, dur);
}

void MusicPlayerManager::notifyPlayStateChanged(bool isPlaying)
{
    for (auto &cb : m_playStateListeners) if (cb) cb(isPlaying);
}

void MusicPlayerManager::notifyLyricLineChanged(int idx, const QString &text, const QString &trans)
{
    for (auto &cb : m_lyricLineListeners) if (cb) cb(idx, text, trans);
}

void MusicPlayerManager::notifyErrorOccurred(const QString &err)
{
    for (auto &cb : m_errorOccurredListeners) if (cb) cb(err);
}

SongInfo MusicPlayerManager::currentSong() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
        return m_playlist[m_currentIndex];
    }
    return SongInfo();
}

bool MusicPlayerManager::isPlaying() const
{
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

qint64 MusicPlayerManager::position() const
{
    return m_player->position();
}

qint64 MusicPlayerManager::duration() const
{
    return m_player->duration();
}

float MusicPlayerManager::volume() const
{
    return m_audioOutput->volume();
}

void MusicPlayerManager::setVolume(float volume)
{
    m_audioOutput->setVolume(std::clamp(volume, 0.0f, 1.0f));
}

void MusicPlayerManager::setPlaybackMode(PlaybackMode mode)
{
    m_mode = mode;
}

void MusicPlayerManager::playSong(const SongInfo &song)
{
    // 如果已经在列表中，找到索引并播放；否则插入并播放
    int foundIndex = -1;
    for (int i = 0; i < m_playlist.size(); ++i) {
        if (m_playlist[i].source == song.source && m_playlist[i].id == song.id) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex != -1) {
        m_currentIndex = foundIndex;
    } else {
        m_playlist.append(song);
        m_currentIndex = m_playlist.size() - 1;
        notifyPlaylistUpdated();
    }

    MusicFavoriteDb::instance()->savePlaylist(m_playlist, m_currentIndex);

    SongInfo current = m_playlist[m_currentIndex];
    m_isCurrentSongFav = MusicFavoriteDb::instance()->isFavorite(current.source, current.id);
    notifyFavoriteStateChanged(m_isCurrentSongFav);

    std::cout << "[MusicPlayer] 开始准备播放: " << current.name.toStdString() << " - " << current.artist.toStdString() << std::endl;

    // 解析详情 (PlayUrl, PicUrl, Lyric)
    MusicApiService::instance()->resolveSongDetails(current, [this, current](bool success, const SongInfo &resolvedSong, const QString &lrc, const QString &tlyric) {
        if (!success || resolvedSong.playUrl.isEmpty()) {
            std::cerr << "[MusicPlayer] 获取播放链接失败: " << resolvedSong.name.toStdString() << std::endl;
            m_consecutiveErrors++;
            if (m_consecutiveErrors >= 3) {
                m_consecutiveErrors = 0;
                m_player->stop();
                notifyErrorOccurred("连续多首歌曲暂无可用音频源，已暂停播放");
                ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
                if (target != nullptr) {
                    target->showMessage("🎵 暂无可用播放源，已为你暂停播放~", 3500);
                }
                return;
            }

            notifyErrorOccurred(QString("《%1》暂无可用播放源，正在切换下一首...").arg(resolvedSong.name));
            QTimer::singleShot(800, this, [this]() {
                playNext();
            });
            return;
        }

        m_consecutiveErrors = 0;

        if (m_currentIndex >= 0 && m_currentIndex < m_playlist.size() && 
            m_playlist[m_currentIndex].id == resolvedSong.id) {
            m_playlist[m_currentIndex] = resolvedSong;
        }

        parseLrc(lrc, tlyric);
        for (auto &cb : m_songChangedListeners) {
            if (cb) cb(resolvedSong);
        }

        std::cout << "[MusicPlayer] 加载音频流: " << resolvedSong.playUrl.toStdString() << std::endl;
        m_player->setSource(QUrl(resolvedSong.playUrl));
        m_player->play();
    });
}


void MusicPlayerManager::playPlaylist(const QVector<SongInfo> &list, int startIndex)
{
    if (list.isEmpty()) return;
    m_playlist = list;
    notifyPlaylistUpdated();
    if (startIndex >= 0 && startIndex < m_playlist.size()) {
        m_currentIndex = startIndex;
        MusicFavoriteDb::instance()->savePlaylist(m_playlist, m_currentIndex);
        playSong(m_playlist[m_currentIndex]);
    } else {
        MusicFavoriteDb::instance()->savePlaylist(m_playlist, 0);
    }
}

void MusicPlayerManager::addToPlaylist(const SongInfo &song)
{
    for (const auto &item : m_playlist) {
        if (item.source == song.source && item.id == song.id) return;
    }
    m_playlist.append(song);
    MusicFavoriteDb::instance()->savePlaylist(m_playlist, m_currentIndex);
    notifyPlaylistUpdated();
}

int MusicPlayerManager::addBatchToPlaylist(const QVector<SongInfo> &songs)
{
    int addedCount = 0;
    for (const auto &song : songs) {
        bool exists = false;
        for (const auto &item : m_playlist) {
            if (item.source == song.source && item.id == song.id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_playlist.append(song);
            addedCount++;
        }
    }
    if (addedCount > 0) {
        MusicFavoriteDb::instance()->savePlaylist(m_playlist, m_currentIndex);
        notifyPlaylistUpdated();
    }
    return addedCount;
}

void MusicPlayerManager::removeFromPlaylist(int index)
{
    if (index < 0 || index >= m_playlist.size()) return;

    bool isRemovingCurrent = (index == m_currentIndex);
    m_playlist.removeAt(index);

    if (m_playlist.isEmpty()) {
        autoRefillRecommendationsIfNeeded(true);
        return;
    }

    if (isRemovingCurrent) {
        if (m_currentIndex >= m_playlist.size()) {
            m_currentIndex = 0;
        }
        MusicFavoriteDb::instance()->savePlaylist(m_playlist, m_currentIndex);
        notifyPlaylistUpdated();
        playSong(m_playlist[m_currentIndex]);
    } else {
        if (index < m_currentIndex) {
            m_currentIndex--;
        }
        MusicFavoriteDb::instance()->savePlaylist(m_playlist, m_currentIndex);
        notifyPlaylistUpdated();
    }

    // 少于 3 首提前无缝续接
    if (m_playlist.size() < 3) {
        autoRefillRecommendationsIfNeeded(false);
    }
}

void MusicPlayerManager::clearPlaylist()
{
    m_player->stop();
    m_playlist.clear();
    m_currentIndex = -1;
    m_parsedLyrics.clear();
    m_currentLyricIndex = -1;
    MusicFavoriteDb::instance()->savePlaylist(m_playlist, -1);
    notifyPlaylistUpdated();
    for (auto &cb : m_songChangedListeners) {
        if (cb) cb(SongInfo());
    }
}

void MusicPlayerManager::recommendSongsByMode(const QString &modeInput, int targetCount, std::function<void(const QVector<SongInfo>&)> callback)
{
    if (targetCount <= 0) targetCount = 6;
    QString mode = modeInput.trimmed().toLower();
    if (mode.isEmpty() || mode == "default") {
        mode = MusicFavoriteDb::instance()->getRecommendationMode();
    }
    if (mode != "familiar" && mode != "explore" && mode != "random") {
        mode = "familiar";
    }

    QSet<QString> recentKeys = MusicFavoriteDb::instance()->getRecentRecommendationKeys();

    auto isExcluded = [this, recentKeys](const SongInfo &s) -> bool {
        QString n = s.name.trimmed().toLower();
        QString a = s.artist.trimmed().toLower();
        if (n.isEmpty()) return true;
        if (recentKeys.contains(n + "||" + a) || recentKeys.contains(n)) {
            return true;
        }
        for (const auto &item : m_playlist) {
            if ((!s.id.isEmpty() && item.id == s.id && item.source == s.source) ||
                (item.name.trimmed().toLower() == n && item.artist.trimmed().toLower() == a)) {
                return true;
            }
        }
        return false;
    };

    auto finalizeAndReturn = [targetCount, callback](QVector<SongInfo> &pool) {
        // 去重池
        QVector<SongInfo> uniquePool;
        for (const auto &s : pool) {
            bool exists = false;
            for (const auto &u : uniquePool) {
                if ((!s.id.isEmpty() && u.id == s.id && u.source == s.source) ||
                    (u.name.trimmed().toLower() == s.name.trimmed().toLower() &&
                     u.artist.trimmed().toLower() == s.artist.trimmed().toLower())) {
                    exists = true;
                    break;
                }
            }
            if (!exists) uniquePool.append(s);
        }

        std::shuffle(uniquePool.begin(), uniquePool.end(), *QRandomGenerator::global());
        if (uniquePool.size() > targetCount) {
            uniquePool.resize(targetCount);
        }

        // 记录到最近推荐历史防重缓存中
        MusicFavoriteDb::instance()->recordRecentRecommendations(uniquePool);

        if (callback) callback(uniquePool);
    };

    if (mode == "familiar") {
        // 1. 熟悉模式：从收藏池 + 偏好歌手衍生推荐（共 6 首）
        QVector<SongInfo> favorites = MusicFavoriteDb::instance()->getFavorites();
        std::vector<SongInfo> shuffledFav(favorites.begin(), favorites.end());
        std::shuffle(shuffledFav.begin(), shuffledFav.end(), *QRandomGenerator::global());

        QVector<SongInfo> pool;
        // 抽取未在近期推荐中的收藏歌曲
        for (const auto &s : shuffledFav) {
            if (!isExcluded(s)) {
                pool.append(s);
                if (pool.size() >= targetCount / 2) break;
            }
        }
        // 若全部收藏都被排除过，则容错从收藏打乱中提取
        if (pool.isEmpty() && !shuffledFav.empty()) {
            for (size_t i = 0; i < shuffledFav.size() && pool.size() < targetCount / 2; ++i) {
                pool.append(shuffledFav[i]);
            }
        }

        // 提取喜好歌手或偏好标签进行衍生推荐
        QStringList artists;
        for (const auto &s : shuffledFav) {
            QString art = s.artist.trimmed();
            if (!art.isEmpty() && art != "未知歌手" && !artists.contains(art)) {
                artists.append(art);
            }
        }
        QStringList tags = MusicFavoriteDb::instance()->getPreferenceTags();
        for (const auto &t : tags) {
            if (!artists.contains(t)) artists.append(t);
        }

        QString searchKw = artists.isEmpty() ? "流行热歌" : artists[QRandomGenerator::global()->bounded(artists.size())];
        MusicApiService::instance()->search(searchKw, "netease", 15, 1, [pool, targetCount, isExcluded, finalizeAndReturn](bool success, const QVector<SongInfo> &searchResult, const QString &) mutable {
            if (success) {
                for (const auto &s : searchResult) {
                    if (!isExcluded(s)) {
                        pool.append(s);
                        if (pool.size() >= targetCount) break;
                    }
                }
                // 若过滤后仍不足 targetCount，放宽条件补充
                if (pool.size() < targetCount) {
                    for (const auto &s : searchResult) {
                        pool.append(s);
                        if (pool.size() >= targetCount) break;
                    }
                }
            }
            finalizeAndReturn(pool);
        });
    }
    else if (mode == "explore") {
        // 2. 探索模式：直接检索全网实时热榜与爆款（共 6 首）
        static const QStringList trendingKeywords = {
            "2026热歌榜", "抖音热歌", "网络流行新歌", "抖音爆款民谣", "华语新歌榜", "飙升榜", "流行流行榜", "最热歌曲"
        };
        QString kw = trendingKeywords[QRandomGenerator::global()->bounded(static_cast<int>(trendingKeywords.size()))];
        MusicApiService::instance()->search(kw, "netease", 20, 1, [targetCount, isExcluded, finalizeAndReturn](bool success, const QVector<SongInfo> &searchResult, const QString &) {
            QVector<SongInfo> pool;
            if (success) {
                for (const auto &s : searchResult) {
                    if (!isExcluded(s)) {
                        pool.append(s);
                        if (pool.size() >= targetCount) break;
                    }
                }
                if (pool.size() < targetCount) {
                    for (const auto &s : searchResult) {
                        pool.append(s);
                        if (pool.size() >= targetCount) break;
                    }
                }
            }
            finalizeAndReturn(pool);
        });
    }
    else {
        // 3. 随机模式：按照用户的喜好标签随机抽取组合推荐（共 6 首）
        QStringList tags = MusicFavoriteDb::instance()->getPreferenceTags();
        if (tags.isEmpty()) {
            tags = { "流行", "民谣", "周杰伦", "轻音乐", "古风", "摇滚", "治愈" };
        }
        std::vector<QString> shuffledTags(tags.begin(), tags.end());
        std::shuffle(shuffledTags.begin(), shuffledTags.end(), *QRandomGenerator::global());

        QString tag1 = shuffledTags[0];
        QString tag2 = shuffledTags.size() > 1 ? shuffledTags[1] : tag1;

        auto collectedPool = std::make_shared<QVector<SongInfo>>();
        auto remaining = std::make_shared<int>(2);
        auto mtx = std::make_shared<std::mutex>();

        auto onSearchDone = [tag1, tag2, collectedPool, remaining, mtx, targetCount, isExcluded, finalizeAndReturn](bool success, const QVector<SongInfo> &songs) {
            {
                std::lock_guard<std::mutex> lock(*mtx);
                if (success) {
                    for (const auto &s : songs) {
                        if (!isExcluded(s)) {
                            collectedPool->append(s);
                        }
                    }
                }
                (*remaining)--;
                if (*remaining > 0) return;
            }

            // 若过滤后仍少于 targetCount，补齐
            if (collectedPool->size() < targetCount && success && !songs.isEmpty()) {
                for (const auto &s : songs) {
                    collectedPool->append(s);
                }
            }
            finalizeAndReturn(*collectedPool);
        };

        MusicApiService::instance()->search(tag1, "netease", 10, 1, [onSearchDone](bool s, const QVector<SongInfo> &res, const QString &) {
            onSearchDone(s, res);
        });
        MusicApiService::instance()->search(tag2, "netease", 10, 1, [onSearchDone](bool s, const QVector<SongInfo> &res, const QString &) {
            onSearchDone(s, res);
        });
    }
}

void MusicPlayerManager::autoRefillRecommendationsIfNeeded(bool autoPlay)
{
    if (m_playlist.size() >= 3 || m_isRefilling) return;
    m_isRefilling = true;

    QString mode = MusicFavoriteDb::instance()->getRecommendationMode();
    QString modeName = (mode == "explore") ? "探索模式" : (mode == "random" ? "随机模式" : "熟悉模式");

    std::cout << "[MusicPlayer] 播放列表剩余少于 3 首 (当前 " << m_playlist.size() << " 首)，正在按「" << modeName.toStdString() << "」推荐 6 首不重复曲目..." << std::endl;

    recommendSongsByMode(mode, 6, [this, autoPlay, modeName](const QVector<SongInfo> &recommended) {
        m_isRefilling = false;
        if (recommended.isEmpty()) {
            if (m_playlist.isEmpty()) {
                clearPlaylist();
            }
            return;
        }

        bool wasEmpty = m_playlist.isEmpty();

        // 智能追加到列表末尾
        int added = addBatchToPlaylist(recommended);

        if (wasEmpty) {
            m_currentIndex = 0;
        }

        MusicFavoriteDb::instance()->savePlaylist(m_playlist, m_currentIndex);
        notifyPlaylistUpdated();

        // 提示桌宠气泡
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target != nullptr) {
            if (wasEmpty) {
                target->showMessage(QString("🎵 播放列表已空，根据「%1」为你推荐了 %2 首好歌~ ✨").arg(modeName).arg(added), 4000);
            } else {
                target->showMessage(QString("🎵 待播曲目快见底啦，根据「%1」已为你续上 %2 首新鲜好歌~ ✨").arg(modeName).arg(added), 4000);
            }
        }

        if (autoPlay && wasEmpty && !m_playlist.isEmpty()) {
            playSong(m_playlist[0]);
        }
    });
}



void MusicPlayerManager::play()
{
    if (m_player->playbackState() == QMediaPlayer::PausedState) {
        m_player->play();
    } else if (m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
        playSong(m_playlist[m_currentIndex]);
    } else if (!m_playlist.isEmpty()) {
        playSong(m_playlist[0]);
    }
}

void MusicPlayerManager::pause()
{
    m_player->pause();
}

void MusicPlayerManager::togglePlay()
{
    if (isPlaying()) {
        pause();
    } else {
        play();
    }
}

void MusicPlayerManager::playNext()
{
    if (m_playlist.isEmpty()) return;

    if (m_mode == PlaybackMode::Random && m_playlist.size() > 1) {
        int nextIdx = m_currentIndex;
        while (nextIdx == m_currentIndex) {
            nextIdx = QRandomGenerator::global()->bounded(m_playlist.size());
        }
        m_currentIndex = nextIdx;
    } else {
        m_currentIndex = (m_currentIndex + 1) % m_playlist.size();
    }

    playSong(m_playlist[m_currentIndex]);
}

void MusicPlayerManager::playPrevious()
{
    if (m_playlist.isEmpty()) return;

    if (m_mode == PlaybackMode::Random && m_playlist.size() > 1) {
        int prevIdx = m_currentIndex;
        while (prevIdx == m_currentIndex) {
            prevIdx = QRandomGenerator::global()->bounded(m_playlist.size());
        }
        m_currentIndex = prevIdx;
    } else {
        m_currentIndex = (m_currentIndex - 1 + m_playlist.size()) % m_playlist.size();
    }

    playSong(m_playlist[m_currentIndex]);
}

void MusicPlayerManager::seek(qint64 positionMs)
{
    m_player->setPosition(positionMs);
}

void MusicPlayerManager::toggleFavoriteCurrent()
{
    SongInfo cur = currentSong();
    if (cur.id.isEmpty()) return;

    if (m_isCurrentSongFav) {
        MusicFavoriteDb::instance()->removeFavorite(cur.source, cur.id);
        m_isCurrentSongFav = false;
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target) {
            target->showMessage(QString("已将《%1》从收藏移除 💔").arg(cur.name), 2500);
        }
    } else {
        MusicFavoriteDb::instance()->addFavorite(cur);
        m_isCurrentSongFav = true;
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target) {
            target->motionController().spawnHeart(QPointF(0, -20.0f));
            target->motionController().triggerEmote(PetEmoteType::HappyHeart, 2.8f);
            target->motionController().triggerStretch(0.92f, 1.15f);
            target->showMessage(QString("💖 已将《%1》加入私藏歌单！我也超喜欢这首~").arg(cur.name), 3500);
        }
    }
    notifyFavoriteStateChanged(m_isCurrentSongFav);
}

bool MusicPlayerManager::isCurrentSongFavorite() const
{
    return m_isCurrentSongFav;
}

void MusicPlayerManager::onPlayerPositionChanged(qint64 position)
{
    updateLyricLine(position);
    notifyPositionChanged(position, m_player->duration());
}

void MusicPlayerManager::onPlayerDurationChanged(qint64 duration)
{
    notifyPositionChanged(m_player->position(), duration);
}

void MusicPlayerManager::onPlayerPlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    if (state == QMediaPlayer::PlayingState) {
        m_consecutiveErrors = 0;
    }
    notifyPlayStateChanged(state == QMediaPlayer::PlayingState);
}


void MusicPlayerManager::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::EndOfMedia) {
        std::cout << "[MusicPlayer] 当前曲目播放结束" << std::endl;
        if (m_mode == PlaybackMode::SingleLoop) {
            m_player->setPosition(0);
            m_player->play();
        } else if (m_autoRemovePlayed && m_currentIndex >= 0 && m_currentIndex < m_playlist.size()) {
            // 消费型待播队列：播完自动移出列表
            std::cout << "[MusicPlayer] 播完自动移出播放列表: " << m_playlist[m_currentIndex].name.toStdString() << std::endl;
            m_playlist.removeAt(m_currentIndex);
            if (m_playlist.isEmpty()) {
                autoRefillRecommendationsIfNeeded(true);
            } else {
                if (m_currentIndex >= m_playlist.size()) {
                    m_currentIndex = 0;
                }
                MusicFavoriteDb::instance()->savePlaylist(m_playlist, m_currentIndex);
                notifyPlaylistUpdated();
                playSong(m_playlist[m_currentIndex]);

                // 若剩余待播曲目少于 3 首，提前触发推荐补充，实现无缝续接！
                if (m_playlist.size() < 3) {
                    autoRefillRecommendationsIfNeeded(false);
                }
            }
        } else {
            playNext();
            if (m_playlist.size() < 3) {
                autoRefillRecommendationsIfNeeded(false);
            }
        }

    }
}


void MusicPlayerManager::parseLrc(const QString &lrc, const QString &tlyric)
{
    m_parsedLyrics.clear();
    m_currentLyricIndex = -1;

    if (lrc.isEmpty()) return;

    // 解析翻译歌词映射
    QMap<qint64, QString> transMap;
    if (!tlyric.isEmpty()) {
        QRegularExpression lrcRe(R"(\[(\d{2}):(\d{2})\.(\d{2,3})\](.*))");
        for (const QString &line : tlyric.split('\n')) {
            auto match = lrcRe.match(line.trimmed());
            if (match.hasMatch()) {
                qint64 min = match.captured(1).toLongLong();
                qint64 sec = match.captured(2).toLongLong();
                qint64 ms = match.captured(3).toLongLong();
                if (match.captured(3).length() == 2) ms *= 10;
                qint64 totalMs = min * 60000 + sec * 1000 + ms;
                transMap[totalMs] = match.captured(4).trimmed();
            }
        }
    }

    // 解析主歌词
    QRegularExpression lrcRe(R"(\[(\d{2}):(\d{2})\.(\d{2,3})\](.*))");
    for (const QString &line : lrc.split('\n')) {
        auto match = lrcRe.match(line.trimmed());
        if (match.hasMatch()) {
            qint64 min = match.captured(1).toLongLong();
            qint64 sec = match.captured(2).toLongLong();
            qint64 ms = match.captured(3).toLongLong();
            if (match.captured(3).length() == 2) ms *= 10;
            qint64 totalMs = min * 60000 + sec * 1000 + ms;

            QString text = match.captured(4).trimmed();
            if (!text.isEmpty()) {
                LyricLine ll;
                ll.timestampMs = totalMs;
                ll.text = text;
                if (transMap.contains(totalMs)) {
                    ll.translation = transMap[totalMs];
                }
                m_parsedLyrics.append(ll);
            }
        }
    }

    // 按时间排序
    std::sort(m_parsedLyrics.begin(), m_parsedLyrics.end(), [](const LyricLine &a, const LyricLine &b) {
        return a.timestampMs < b.timestampMs;
    });
}

void MusicPlayerManager::updateLyricLine(qint64 positionMs)
{
    if (m_parsedLyrics.isEmpty()) return;

    int activeIdx = -1;
    for (int i = 0; i < m_parsedLyrics.size(); ++i) {
        if (positionMs >= m_parsedLyrics[i].timestampMs) {
            activeIdx = i;
        } else {
            break;
        }
    }

    if (activeIdx != m_currentLyricIndex && activeIdx >= 0 && activeIdx < m_parsedLyrics.size()) {
        m_currentLyricIndex = activeIdx;
        notifyLyricLineChanged(activeIdx, m_parsedLyrics[activeIdx].text, m_parsedLyrics[activeIdx].translation);
    }
}
