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
#include <mutex>
#include <algorithm>


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
                    target->showMessage("🎵 暂无可用播放源，已为你暂停播放~", 3500, "", false, true);
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

namespace {
    // 华语与流行/独立/民谣/欧美/日系 关联图谱 (Artist Affinity Graph)
    static const QMap<QString, QStringList>& getArtistAffinityMap() {
        static const QMap<QString, QStringList> map = {
            // 华语流行与创作天王
            {"周杰伦", {"方大同", "林俊杰", "陶喆", "南拳妈妈", "蛋堡", "韦礼安", "潘玮柏"}},
            {"林俊杰", {"周杰伦", "张敬轩", "李荣浩", "王力宏", "蔡健雅", "薛之谦"}},
            {"陶喆", {"方大同", "周杰伦", "袁娅维", "单依纯", "李泉"}},
            {"陈奕迅", {"张国荣", "张学友", "杨千嬅", "容祖儿", "古巨基", "谢安琪"}},
            {"李荣浩", {"毛不易", "薛之谦", "郭顶", "汪苏泷", "许嵩"}},
            {"郭顶", {"落日飞车", "裘德", "脆乐团", "告五人", "草东没有派对"}},

            // 华语民谣与人文诗性
            {"毛不易", {"朴树", "赵雷", "房东的猫", "陈粒", "老狼", "谢春花"}},
            {"朴树", {"许巍", "李健", "达达乐队", "张震岳", "痛仰乐队"}},
            {"李健", {"水木年华", "齐秦", "朴树", "郁可唯", "林志炫"}},
            {"赵雷", {"宋冬野", "马頔", "郝云", "尧十三", "丢火车"}},
            {"房东的猫", {"陈粒", "谢春花", "安来宁", "焦迈奇", "好妹妹"}},
            {"陈粒", {"房东的猫", "陈绮贞", "万能青年旅店", "花粥", "程璧"}},
            {"陈绮贞", {"张悬", "雷光夏", "曹方", "自然卷", "魏如萱", "苏打绿"}},
            {"苏打绿", {"吴青峰", "鱼丁糸", "棉花糖", "旺福", "落日飞车"}},
            {"吴青峰", {"苏打绿", "张韶涵", "田馥甄", "蔡依林", "徐佳莹"}},

            // 潮流独立与摇滚
            {"告五人", {"康姆士", "回春丹", "橘子海", "草东没有派对", "椅子乐团", "脆乐团"}},
            {"落日飞车", {"Deca Joins", "甜约翰", "温蒂漫步", "椅子乐团", "雀斑"}},
            {"草东没有派对", {"万能青年旅店", "回春丹", "康姆士", "九宝乐队", "痛仰乐队"}},
            {"回春丹", {"麻油叶", "二手玫瑰", "木马乐队", "面孔乐队", "新裤子"}},
            {"新裤子", {"痛仰乐队", "刺猬", "重塑雕像的权利", "达达乐队", "五条人"}},
            {"橘子海", {"夏日入侵企画", "霓虹花园", "岛屿心情", "棱镜乐队", "声音玩具"}},
            {"棱镜", {"夏日入侵企画", "房东的猫", "沈以诚", "焦迈奇", "留声玩具"}},

            // 欧美流行 / 独立创作
            {"Taylor Swift", {"Sabrina Carpenter", "Olivia Rodrigo", "Phoebe Bridgers", "Lana Del Rey", "Gracie Abrams", "Billie Eilish"}},
            {"Billie Eilish", {"FINNEAS", "Lorde", "Melanie Martinez", "Olivia Rodrigo", "Girl in Red"}},
            {"Ed Sheeran", {"Shawn Mendes", "James Arthur", "Lewis Capaldi", "Sam Smith", "Charlie Puth"}},
            {"Bruno Mars", {"The Weeknd", "Silk Sonic", "Anderson .Paak", "Michael Jackson", "Justin Timberlake"}},
            {"The Weeknd", {"Post Malone", "Dua Lipa", "Doja Cat", "Kendrick Lamar", "SZA"}},
            {"Lauv", {"LANY", "Troye Sivan", "Jeremy Zucker", "Conan Gray", "Alec Benjamin"}},
            {"Coldplay", {"Imagine Dragons", "OneRepublic", "The Script", "Keane", "Oasis"}},

            // 日系 / ACG / J-Pop
            {"YOASOBI", {"ZUTOMAYO", "Eve", "Ado", "Yorushika", "美波", "ReoNa"}},
            {"Yorushika", {"YOASOBI", "ZUTOMAYO", "ツユ", "majiko", "花谱"}},
            {"米津玄师", {"RADWIMPS", "King Gnu", "Fujii Kaze", "Vaundy", "back number"}},
            {"RADWIMPS", {"BUMP OF CHICKEN", "ONE OK ROCK", "back number", "SPYAIR", "ASIAN KUNG-FU GENERATION"}},
            {"久石让", {"坂本龙一", "宗次郎", "西村由纪江", "S.E.N.S.", "川井宪次"}},
            {"坂本龙一", {"久石让", "Ennio Morricone", "Ludovico Einaudi", "Max Richter"}},

            // 经典港乐
            {"张国荣", {"谭咏麟", "梅艳芳", "陈百强", "林子祥", "张学友"}},
            {"Beyond", {"黄贯中", "太极乐队", "黑豹乐队", "唐朝乐队", "许冠杰"}}
        };
        return map;
    }

    // 探索模式 16 大细分品味雷达频道
    static const QStringList& getDiscoveryRadarChannels() {
        static const QStringList channels = {
            "City Pop 宝藏", "落日飞车 风格", "独立摇滚 宝藏", "浪漫微醺 流行", "清新独立民谣",
            "不插电 吉他 治愈", "木吉他 纯净民谣", "温暖民谣 治愈", "雨天咖啡馆 爵士",
            "Lofi Hip Hop 放空", "Chillhop 专注", "深夜卧室独立流行", "氛围电子 梦幻",
            "华语新歌 宝藏", "网易云飙升榜 新歌", "小众神曲 惊喜", "宝藏新人 原创流行",
            "J-Pop 宝藏新曲", "日系清新 治愈", "动漫良曲 OST"
        };
        return channels;
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
        // 1. 基础去重
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

        // 2. 限制同位歌手出现频次（同一歌手不超过 2 首），提升曲库丰富度
        QMap<QString, int> artistCounts;
        QVector<SongInfo> diversePool;
        for (const auto &s : uniquePool) {
            QString art = s.artist.trimmed().toLower();
            if (art.isEmpty() || art == "未知歌手") {
                diversePool.append(s);
            } else if (artistCounts[art] < 2) {
                artistCounts[art]++;
                diversePool.append(s);
            }
        }
        if (diversePool.size() >= targetCount) {
            uniquePool = diversePool;
        }

        std::shuffle(uniquePool.begin(), uniquePool.end(), *QRandomGenerator::global());
        if (uniquePool.size() > targetCount) {
            uniquePool.resize(targetCount);
        }

        // 记录到最近推荐历史防重缓存中
        MusicFavoriteDb::instance()->recordRecentRecommendations(uniquePool);

        if (callback) callback(uniquePool);
    };

    struct SearchTask {
        QString keyword;
        int page;
    };
    QVector<SearchTask> tasks;
    auto pool = std::make_shared<QVector<SongInfo>>();

    if (mode == "familiar") {
        // 1. 熟悉模式：基于喜好图谱衍生挖掘新歌（最多保留 1 首收藏作为记忆引子，其余全部挖掘关联新歌与深层曲目）
        QVector<SongInfo> favorites = MusicFavoriteDb::instance()->getFavorites();
        std::vector<SongInfo> shuffledFav(favorites.begin(), favorites.end());
        std::shuffle(shuffledFav.begin(), shuffledFav.end(), *QRandomGenerator::global());

        // 挑选至多 1 首未被排除的收藏旧歌作为引子
        for (const auto &s : shuffledFav) {
            if (!isExcluded(s)) {
                pool->append(s);
                break;
            }
        }

        // 提取收藏夹中歌手
        QStringList favoriteArtists;
        for (const auto &s : shuffledFav) {
            QString art = s.artist.trimmed();
            if (!art.isEmpty() && art != "未知歌手" && !favoriteArtists.contains(art)) {
                favoriteArtists.append(art);
            }
        }

        // 查找关联歌手图谱
        QStringList relatedArtists;
        const auto &affinityMap = getArtistAffinityMap();
        for (const auto &art : favoriteArtists) {
            if (affinityMap.contains(art)) {
                for (const auto &rel : affinityMap[art]) {
                    if (!relatedArtists.contains(rel) && !favoriteArtists.contains(rel)) {
                        relatedArtists.append(rel);
                    }
                }
            }
        }
        std::shuffle(relatedArtists.begin(), relatedArtists.end(), *QRandomGenerator::global());

        // 生成多路并发搜索任务（采用深分页随机采样，彻底告别旧热歌循环）
        if (!relatedArtists.isEmpty()) {
            tasks.append({relatedArtists[0], QRandomGenerator::global()->bounded(1, 5)});
            if (relatedArtists.size() > 1) {
                tasks.append({relatedArtists[1], QRandomGenerator::global()->bounded(1, 4)});
            }
        }
        if (!favoriteArtists.isEmpty()) {
            QString favArt = favoriteArtists[QRandomGenerator::global()->bounded(favoriteArtists.size())];
            // 针对用户已有喜好歌手，跳过前 10 首听过的主打歌，使用深分页 2~6 挖掘宝藏冷门佳作
            tasks.append({favArt, QRandomGenerator::global()->bounded(2, 6)});
        }

        // 若用户尚无收藏，降级回用户偏好标签或默认新声频道
        if (tasks.isEmpty()) {
            QStringList tags = MusicFavoriteDb::instance()->getPreferenceTags();
            if (tags.isEmpty()) tags = {"流行", "民谣", "City Pop", "轻音乐", "治愈"};
            std::shuffle(tags.begin(), tags.end(), *QRandomGenerator::global());
            tasks.append({tags[0], QRandomGenerator::global()->bounded(1, 6)});
            if (tags.size() > 1) {
                tasks.append({tags[1], QRandomGenerator::global()->bounded(1, 5)});
            }
        }
    }
    else if (mode == "explore") {
        // 2. 探索模式：抽取 2 大细分品味雷达频道，深分页采样推荐全网新声
        QStringList channels = getDiscoveryRadarChannels();
        std::shuffle(channels.begin(), channels.end(), *QRandomGenerator::global());
        tasks.append({channels[0], QRandomGenerator::global()->bounded(1, 5)});
        tasks.append({channels[1], QRandomGenerator::global()->bounded(1, 5)});
    }
    else {
        // 3. 随机模式：用户个性化喜好标签 + 探索雷达双通道混合打散
        QStringList tags = MusicFavoriteDb::instance()->getPreferenceTags();
        if (tags.isEmpty()) tags = {"流行", "民谣", "City Pop", "轻音乐", "摇滚", "治愈"};
        std::shuffle(tags.begin(), tags.end(), *QRandomGenerator::global());

        QStringList channels = getDiscoveryRadarChannels();
        std::shuffle(channels.begin(), channels.end(), *QRandomGenerator::global());

        tasks.append({tags[0], QRandomGenerator::global()->bounded(1, 6)});
        tasks.append({channels[0], QRandomGenerator::global()->bounded(1, 5)});
    }

    // 并发执行搜索任务并聚合
    auto remaining = std::make_shared<int>(tasks.size());
    auto mtx = std::make_shared<std::mutex>();

    for (const auto &t : tasks) {
        MusicApiService::instance()->search(t.keyword, "netease", 15, t.page,
            [pool, remaining, mtx, targetCount, isExcluded, finalizeAndReturn](bool success, const QVector<SongInfo> &songs, const QString &) {
                {
                    std::lock_guard<std::mutex> lock(*mtx);
                    if (success) {
                        for (const auto &s : songs) {
                            if (!isExcluded(s)) {
                                pool->append(s);
                            }
                        }
                    }
                    (*remaining)--;
                    if (*remaining > 0) return;
                }

                // 所有搜索分支已就绪
                // 若过滤后数量仍少于 targetCount，适度放宽做兜底补充
                if (pool->size() < targetCount && success && !songs.isEmpty()) {
                    for (const auto &s : songs) {
                        pool->append(s);
                        if (pool->size() >= targetCount) break;
                    }
                }
                finalizeAndReturn(*pool);
            }
        );
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

        // 提示桌宠气泡 (使用萌系轻量胶囊小气泡)
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target != nullptr) {
            if (wasEmpty) {
                target->showMessage(QString("🎵 播放列表已空，根据「%1」为你推荐了 %2 首好歌~ ✨").arg(modeName).arg(added), 4000, "", false, true);
            } else {
                target->showMessage(QString("🎵 待播曲目快见底啦，根据「%1」已为你续上 %2 首新鲜好歌~ ✨").arg(modeName).arg(added), 4000, "", false, true);
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
            target->showMessage(QString("已将《%1》从收藏移除 💔").arg(cur.name), 2500, "", false, true);
        }
    } else {
        MusicFavoriteDb::instance()->addFavorite(cur);
        m_isCurrentSongFav = true;
        ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
        if (target) {
            target->motionController().spawnHeart(QPointF(0, -20.0f));
            target->motionController().triggerEmote(PetEmoteType::HappyHeart, 2.8f);
            target->motionController().triggerStretch(0.92f, 1.15f);
            target->showMessage(QString("💖 已将《%1》加入私藏歌单！我也超喜欢这首~").arg(cur.name), 3500, "", false, true);
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
