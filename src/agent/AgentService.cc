// 
// Shijima-Qt - AI Agent & Memory Service Implementation
// 

#include "AgentService.hpp"
#include "WebSearchEngine.hpp"
#include "AipyAdapter.hpp"
#include "PetMemory.hpp"
#include "LongTermMemoryEngine.hpp"
#include "TimerManager.hpp"
#include "ShijimaManager.hpp"
#include "MusicPlayerManager.hpp"
#include "MusicPlayerDialog.hpp"
#include "MusicApiService.hpp"
#include "MusicFavoriteDb.hpp"
#include "SettingsDb.hpp"
#include "PersonaManager.hpp"
#include "SkillManager.hpp"
#include "McpManager.hpp"
#include "PetAction.hpp"
#include "MessageHistoryManager.hpp"
#include "ShijimaWidget.hpp"
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QThread>
#include <QCoreApplication>
#include <QDebug>
#include <iostream>

static QJsonObject getMusicToolDefinition() {
    QJsonObject fn;
    fn["name"] = "music_player_manage";
    fn["description"] = "控制音乐播放器播放音乐、暂停、切歌、搜索歌曲、收藏/取消收藏、查看收藏列表、获取当前曲目，或按模式智能推荐歌曲（熟悉/探索/随机模式）、管理用户的喜好标签与推荐模式。当用户要求放歌、推荐音乐、切换推荐模式、查看/修改/删除喜好标签时调用此工具。";

    QJsonObject props;
    
    QJsonObject actionProp;
    actionProp["type"] = "string";
    actionProp["enum"] = QJsonArray{
        "recommend_by_mode", "list_preference_tags", "add_preference_tag", "remove_preference_tag", "set_recommend_mode",
        "search_and_play", "batch_search_and_add", "pause", "resume", "toggle_play", "next", "previous", "favorite", "list_favorites", "get_current", "open_window"
    };
    actionProp["description"] = "操作类型: recommend_by_mode(按模式推荐6首歌并入库开播), list_preference_tags(查看喜好标签与推荐模式), add_preference_tag(添加喜好标签), remove_preference_tag(删除喜好标签), set_recommend_mode(设置默认推荐模式: familiar/explore/random), batch_search_and_add(批量推荐/搜歌并加入播放列表), search_and_play(搜索单曲并播放), pause(暂停), resume(继续播放), toggle_play(播放/暂停切换), next(下一首), previous(上一首), favorite(收藏/取消收藏当前歌曲), list_favorites(查看收藏列表), get_current(获取当前播放歌曲), open_window(打开音乐播放器窗口)";
    props["action"] = actionProp;

    QJsonObject modeProp;
    modeProp["type"] = "string";
    modeProp["enum"] = QJsonArray{"familiar", "explore", "random", "default"};
    modeProp["description"] = "若为 recommend_by_mode 或 set_recommend_mode，推荐模式: familiar(熟悉模式:从收藏+喜好偏好推荐6首), explore(探索模式:全网最新爆款热歌6首), random(随机模式:从喜好标签随机组合推荐6首)";
    props["mode"] = modeProp;

    QJsonObject tagsProp;
    tagsProp["type"] = "array";
    QJsonObject tagItem;
    tagItem["type"] = "string";
    tagsProp["items"] = tagItem;
    tagsProp["description"] = "若为 add_preference_tag 或 remove_preference_tag，要添加或删除的喜好标签列表（如 [\"摇滚\", \"民谣\", \"周杰伦\"]）";
    props["tags"] = tagsProp;

    QJsonObject keywordProp;
    keywordProp["type"] = "string";
    keywordProp["description"] = "若为 search_and_play，要搜索并播放的歌名、歌手或关键词（如'周杰伦 晴天'、'轻音乐'、'起风了'）";
    props["keyword"] = keywordProp;

    QJsonObject keywordsProp;
    keywordsProp["type"] = "array";
    QJsonObject kwItem;
    kwItem["type"] = "string";
    keywordsProp["items"] = kwItem;
    keywordsProp["description"] = "若为 batch_search_and_add，要批量搜索并添加到播放列表的歌曲名称列表（例如 [\"若月亮没来\", \"鲜花 房东的猫\", \"漠河舞厅 柳爽\"]）";
    props["keywords"] = keywordsProp;

    QJsonObject playNowProp;
    playNowProp["type"] = "boolean";
    playNowProp["description"] = "添加或推荐完成后是否立即开始播放第一首（默认为 true）";
    props["play_now"] = playNowProp;

    QJsonObject sourceProp;
    sourceProp["type"] = "string";
    sourceProp["enum"] = QJsonArray{"netease", "kuwo", "bilibili"};
    sourceProp["description"] = "音乐源: netease(网易云音乐,默认推荐), kuwo(酷我音乐,周杰伦等原版推荐), bilibili(B站音频)";
    props["source"] = sourceProp;

    QJsonObject params;
    params["type"] = "object";
    params["properties"] = props;
    params["required"] = QJsonArray{"action"};

    QJsonObject result;
    result["type"] = "function";
    QJsonObject fnObj = fn;
    fnObj["parameters"] = params;
    result["function"] = fnObj;
    return result;
}

static void executeMusicTool(const QJsonObject &args, std::function<void(QString)> callback) {
    QString action = args["action"].toString().trimmed();
    std::cout << "[MusicTool] 执行音乐指令: action=" << action.toStdString() << std::endl;

    if (action == "recommend_by_mode") {
        QString mode = args.contains("mode") ? args["mode"].toString("default") : "default";
        bool playNow = args.contains("play_now") ? args["play_now"].toBool(true) : true;

        QString effectiveMode = (mode == "default" || mode.isEmpty()) ? MusicFavoriteDb::instance()->getRecommendationMode() : mode;
        QString modeName = (effectiveMode == "explore") ? "🚀 探索模式 (全网热点爆款)" : (effectiveMode == "random" ? "🎲 随机模式 (喜好标签组合)" : "💖 熟悉模式 (收藏池与喜好衍生)");

        MusicPlayerManager::instance()->recommendSongsByMode(effectiveMode, 6, [callback, playNow, modeName](const QVector<SongInfo> &songs) {
            if (songs.isEmpty()) {
                if (callback) callback("⚠️ 本次推荐未能匹配到合适歌曲，请尝试切换推荐模式或添加喜好标签哦～");
                return;
            }

            int added = MusicPlayerManager::instance()->addBatchToPlaylist(songs);
            if (playNow && !songs.isEmpty()) {
                MusicPlayerManager::instance()->playSong(songs.first());
            }

            QString res = QString("🎶 **已按【%1】为您精选并添加 %2 首歌曲！**\n\n"
                                  "| 序号 | 🎶 歌曲名 | 🎤 歌手 | 🌐 音乐源 |\n"
                                  "| :---: | :--- | :--- | :--- |\n").arg(modeName).arg(added);

            for (int i = 0; i < songs.size(); ++i) {
                const auto &s = songs[i];
                res += QString("| %1 | **%2** | %3 | %4 |\n")
                    .arg(i + 1)
                    .arg(s.name, s.artist.isEmpty() ? "未知歌手" : s.artist, MusicApiService::sourceDisplayName(s.source));
            }

            if (playNow && !songs.isEmpty()) {
                res += QString("\n▶️ **正在播放：** 《%1》 - %2 🎶").arg(songs.first().name, songs.first().artist);
            }
            res += "\n💡 *去重机制已生效，近期推荐历史中的曲目不会重复出现。*";

            if (callback) callback(res);
        });
        return;
    }
    else if (action == "list_preference_tags") {
        QStringList tags = MusicFavoriteDb::instance()->getPreferenceTags();
        QString mode = MusicFavoriteDb::instance()->getRecommendationMode();
        QString modeDesc = (mode == "explore") ? "🚀 探索模式 (全网最新热点爆款)" : (mode == "random" ? "🎲 随机模式 (喜好标签随机抽取)" : "💖 熟悉模式 (收藏歌曲与偏好衍生)");
        int favCount = MusicFavoriteDb::instance()->getFavoriteCount();

        QString res = QString("🏷️ **我的音乐喜好画像**\n\n"
                              "- 🎯 **当前推荐模式**：%1\n"
                              "- ❤️ **已收藏歌曲**：%2 首\n"
                              "- 🔖 **个性化喜好标签** (%3 个)：\n\n").arg(modeDesc).arg(favCount).arg(tags.size());

        if (tags.isEmpty()) {
            res += "*(暂未添加标签，可对我说「添加喜好标签：摇滚、民谣、周杰伦」)*\n";
        } else {
            for (int i = 0; i < tags.size(); ++i) {
                res += QString("%1. `%2`\n").arg(i + 1).arg(tags[i]);
            }
        }
        res += "\n💬 **对话指令提示**：\n"
               "- ➕ 对我说「添加喜好标签：xxx」添加标签\n"
               "- ➖ 对我说「删除喜好标签：xxx」移除标签\n"
               "- 🔄 对我说「切换到探索模式/熟悉模式/随机模式」切换模式\n"
               "- 🎶 对我说「推荐点音乐」立即按当前模式开播 6 首歌！";

        if (callback) callback(res);
        return;
    }
    else if (action == "add_preference_tag") {
        QStringList toAdd;
        if (args.contains("tags") && args["tags"].isArray()) {
            for (auto v : args["tags"].toArray()) {
                QString t = v.toString().trimmed();
                if (!t.isEmpty()) toAdd.append(t);
            }
        }
        if (args.contains("tag")) {
            QString t = args["tag"].toString().trimmed();
            if (!t.isEmpty() && !toAdd.contains(t)) toAdd.append(t);
        }
        if (toAdd.isEmpty()) {
            if (callback) callback("⚠️ 请提供要添加的喜好标签名称哦～");
            return;
        }

        for (const auto &t : toAdd) {
            MusicFavoriteDb::instance()->addPreferenceTag(t);
        }
        QStringList allTags = MusicFavoriteDb::instance()->getPreferenceTags();
        QString res = QString("✅ **已成功添加喜好标签：** `%1`\n\n🏷️ **当前全部喜好标签** (%2 个)：\n%3")
            .arg(toAdd.join("`、`"))
            .arg(allTags.size())
            .arg(allTags.join("、"));
        if (callback) callback(res);
        return;
    }
    else if (action == "remove_preference_tag") {
        QStringList toRemove;
        if (args.contains("tags") && args["tags"].isArray()) {
            for (auto v : args["tags"].toArray()) {
                QString t = v.toString().trimmed();
                if (!t.isEmpty()) toRemove.append(t);
            }
        }
        if (args.contains("tag")) {
            QString t = args["tag"].toString().trimmed();
            if (!t.isEmpty() && !toRemove.contains(t)) toRemove.append(t);
        }
        if (toRemove.isEmpty()) {
            if (callback) callback("⚠️ 请提供要删除的喜好标签名称哦～");
            return;
        }

        for (const auto &t : toRemove) {
            MusicFavoriteDb::instance()->removePreferenceTag(t);
        }
        QStringList allTags = MusicFavoriteDb::instance()->getPreferenceTags();
        QString res = QString("🗑️ **已移除喜好标签：** `%1`\n\n🏷️ **剩余喜好标签** (%2 个)：\n%3")
            .arg(toRemove.join("`、`"))
            .arg(allTags.size())
            .arg(allTags.isEmpty() ? "*(无)*" : allTags.join("、"));
        if (callback) callback(res);
        return;
    }
    else if (action == "set_recommend_mode") {
        QString mode = args["mode"].toString().trimmed().toLower();
        if (mode != "familiar" && mode != "explore" && mode != "random") {
            mode = "familiar";
        }
        MusicFavoriteDb::instance()->setRecommendationMode(mode);
        QString modeDesc = (mode == "explore") ? "🚀 探索模式 (全网实时爆款新歌)" : (mode == "random" ? "🎲 随机模式 (从喜好标签中随机抽取推荐)" : "💖 熟悉模式 (从收藏池与偏好歌手衍生推荐)");

        QString res = QString("🎯 **已为您将默认推荐模式切换为：**\n\n**%1**\n\n💡 对我说「推荐音乐」即可立即按此模式为您精选 6 首新鲜好歌！").arg(modeDesc);
        if (callback) callback(res);
        return;
    }

    if (action == "batch_search_and_add") {
        auto kwArray = args["keywords"].toArray();
        QStringList keywordsList;
        for (auto kwVal : kwArray) {
            QString kw = kwVal.toString().trimmed();
            if (!kw.isEmpty()) keywordsList.append(kw);
        }
        if (keywordsList.isEmpty() && args.contains("keyword")) {
            QString kw = args["keyword"].toString().trimmed();
            if (!kw.isEmpty()) keywordsList.append(kw);
        }
        if (keywordsList.isEmpty()) {
            if (callback) callback("⚠️ 请提供要批量搜索添加的歌曲名称列表哦～");
            return;
        }

        QString source = args.contains("source") ? args["source"].toString("netease") : "netease";
        bool playNow = args.contains("play_now") ? args["play_now"].toBool(true) : true;

        auto collectedSongs = std::make_shared<QVector<SongInfo>>();
        auto remainingCount = std::make_shared<int>(keywordsList.size());
        auto mtx = std::make_shared<std::mutex>();

        for (const QString &kw : keywordsList) {
            MusicApiService::instance()->search(kw, source, 1, 1, [kw, source, collectedSongs, remainingCount, mtx, playNow, callback](bool success, const QVector<SongInfo>& songs, const QString &) {
                {
                    std::lock_guard<std::mutex> lock(*mtx);
                    if (success && !songs.isEmpty()) {
                        collectedSongs->append(songs.first());
                    }
                    (*remainingCount)--;
                    if (*remainingCount > 0) {
                        return;
                    }
                }

                // 所有歌曲检索完毕
                if (collectedSongs->isEmpty()) {
                    if (callback) callback("⚠️ 未能在音乐库中搜索到相关歌曲，请尝试更换歌名或音乐源。");
                    return;
                }

                // 批量追加到播放器播放列表
                MusicPlayerManager::instance()->addBatchToPlaylist(*collectedSongs);

                if (playNow) {
                    MusicPlayerManager::instance()->playSong(collectedSongs->first());
                }

                QString res = QString("🎉 **已为您搜集并添加 %1 首热门歌曲到播放列表！**\n\n"
                                      "| 序号 | 🎶 歌曲名 | 🎤 歌手 | 🌐 音乐源 |\n"
                                      "| :--- | :--- | :--- | :--- |\n").arg(collectedSongs->size());

                for (int i = 0; i < collectedSongs->size(); ++i) {
                    const auto &s = (*collectedSongs)[i];
                    res += QString("| %1 | **%2** | %3 | %4 |\n")
                        .arg(i + 1)
                        .arg(s.name, s.artist.isEmpty() ? "未知歌手" : s.artist, MusicApiService::sourceDisplayName(s.source));
                }

                if (playNow) {
                    res += QString("\n▶️ **正在为您播放第一首：** 《%1》 - %2 🎶").arg(collectedSongs->first().name, collectedSongs->first().artist);
                }

                if (callback) callback(res);
            });
        }
        return;
    }
    else if (action == "search_and_play") {
        QString keyword = args["keyword"].toString().trimmed();
        QString source = args.contains("source") ? args["source"].toString("netease") : "netease";
        if (keyword.isEmpty()) {
            if (callback) callback("⚠️ 请提供要搜索播放的歌曲名称或歌手名哦～");
            return;
        }

        MusicApiService::instance()->search(keyword, source, 10, 1, [keyword, source, callback](bool success, const QVector<SongInfo>& songs, const QString &err) {
            if (!success || songs.isEmpty()) {
                if (callback) callback(QString("⚠️ 未能搜索到与「%1」相关的歌曲: %2。可尝试更换歌名或音乐源。").arg(keyword, err.isEmpty() ? "未找到匹配结果" : err));
                return;
            }

            SongInfo targetSong = songs.first();
            MusicPlayerManager::instance()->addBatchToPlaylist(songs);
            MusicPlayerManager::instance()->playSong(targetSong);

            QString res = QString("🎵 **已为您找到并开始播放**\n\n"
                                  "| 属性 | 内容 |\n"
                                  "| :--- | :--- |\n"
                                  "| 🎶 **歌曲名** | %1 |\n"
                                  "| 🎤 **歌手** | %2 |\n"
                                  "| 💿 **专辑** | %3 |\n"
                                  "| 🌐 **音乐源** | %4 |\n\n"
                                  "💡 已将搜索到的 %5 首相关歌曲加入播放列表～")
                .arg(targetSong.name, targetSong.artist.isEmpty() ? "未知歌手" : targetSong.artist, targetSong.album.isEmpty() ? "单曲" : targetSong.album, MusicApiService::sourceDisplayName(targetSong.source))
                .arg(songs.size());
            if (callback) callback(res);
        });
        return;
    }
    else if (action == "pause") {
        MusicPlayerManager::instance()->pause();
        if (callback) callback("⏸ 音乐已为您暂停播放。");
        return;
    }
    else if (action == "resume") {
        MusicPlayerManager::instance()->play();
        auto cur = MusicPlayerManager::instance()->currentSong();
        if (!cur.name.isEmpty()) {
            if (callback) callback(QString("▶ 已继续播放《%1》- %2").arg(cur.name, cur.artist));
        } else {
            if (callback) callback("▶ 音乐已开始播放。");
        }
        return;
    }
    else if (action == "toggle_play") {
        MusicPlayerManager::instance()->togglePlay();
        bool playing = MusicPlayerManager::instance()->isPlaying();
        if (callback) callback(playing ? "▶ 音乐已开始播放" : "⏸ 音乐已暂停");
        return;
    }
    else if (action == "next") {
        MusicPlayerManager::instance()->playNext();
        auto cur = MusicPlayerManager::instance()->currentSong();
        if (!cur.name.isEmpty()) {
            if (callback) callback(QString("⏭ 已为您切换到下一首：《%1》- %2").arg(cur.name, cur.artist));
        } else {
            if (callback) callback("⏭ 已为您切换到下一首。");
        }
        return;
    }
    else if (action == "previous") {
        MusicPlayerManager::instance()->playPrevious();
        auto cur = MusicPlayerManager::instance()->currentSong();
        if (!cur.name.isEmpty()) {
            if (callback) callback(QString("⏮ 已为您切换到上一首：《%1》- %2").arg(cur.name, cur.artist));
        } else {
            if (callback) callback("⏮ 已为您切换到上一首。");
        }
        return;
    }
    else if (action == "favorite") {
        MusicPlayerManager::instance()->toggleFavoriteCurrent();
        bool isFav = MusicPlayerManager::instance()->isCurrentSongFavorite();
        auto cur = MusicPlayerManager::instance()->currentSong();
        if (!cur.name.isEmpty()) {
            if (callback) callback(isFav ? QString("❤️ 已将《%1》- %2 添加到您的收藏夹！").arg(cur.name, cur.artist) : QString("🤍 已取消收藏《%1》").arg(cur.name));
        } else {
            if (callback) callback("⚠️ 当前没有正在播放的歌曲可供收藏哦。");
        }
        return;
    }
    else if (action == "list_favorites") {
        auto favs = MusicFavoriteDb::instance()->getFavorites();
        if (favs.isEmpty()) {
            if (callback) callback("❤️ 您当前还没有收藏任何歌曲哦。在听到喜欢的歌曲时对我说「收藏这首歌」即可收藏！");
            return;
        }
        QString res = QString("❤️ **您的音乐收藏列表 (共 %1 首)**\n\n| 序号 | 歌曲名 | 歌手 | 音乐源 |\n| :---: | :--- | :--- | :--- |\n").arg(favs.size());
        int limitCount = std::min(static_cast<int>(favs.size()), 15);
        for (int i = 0; i < limitCount; ++i) {
            const auto &s = favs[i];
            res += QString("| %1 | %2 | %3 | %4 |\n").arg(i + 1).arg(s.name, s.artist, MusicApiService::sourceDisplayName(s.source));
        }
        if (favs.size() > 15) {
            res += QString("\n*...还有 %1 首歌曲未展开，按 ⌥+M 即可在音乐工坊中查看完整列表*").arg(favs.size() - 15);
        }
        if (callback) callback(res);
        return;
    }
    else if (action == "get_current") {
        auto cur = MusicPlayerManager::instance()->currentSong();
        if (cur.name.isEmpty()) {
            if (callback) callback("🎵 当前没有正在播放的歌曲。您可以对我说「放首周杰伦的歌」开始聆听！");
            return;
        }
        bool playing = MusicPlayerManager::instance()->isPlaying();
        QString res = QString("🎵 **当前播放状态**\n\n"
                              "- **歌曲**：《%1》\n"
                              "- **歌手**：%2\n"
                              "- **状态**：%3\n"
                              "- **音源**：%4")
            .arg(cur.name, cur.artist, playing ? "▶ 正在播放中" : "⏸ 已暂停", MusicApiService::sourceDisplayName(cur.source));
        if (callback) callback(res);
        return;
    }
    else if (action == "open_window") {
        MusicPlayerDialog::instance()->toggleVisibility();
        if (callback) callback("🪟 已为您打开音乐工坊独立播放器窗口！");
        return;
    }

    if (callback) callback("已处理音乐播放器指令。");
}

static QJsonObject getTimerToolDefinition() {
    QJsonObject fn;
    fn["name"] = "timer_manage";
    fn["description"] = "创建、查看、修改或删除定时提醒与定时任务。当用户要求在未来某个时间提醒、倒计时、或定时/每天执行某任务时调用此工具。";

    QJsonObject props;
    
    QJsonObject actionProp;
    actionProp["type"] = "string";
    actionProp["enum"] = QJsonArray{"create", "list", "update", "delete"};
    actionProp["description"] = "操作类型: create(创建), list(查看列表), update(更新/暂停/启用), delete(删除)";
    props["action"] = actionProp;

    QJsonObject titleProp;
    titleProp["type"] = "string";
    titleProp["description"] = "提醒标题或任务名称，例如'喝水提醒'、'每日站会'";
    props["title"] = titleProp;

    QJsonObject typeProp;
    typeProp["type"] = "string";
    typeProp["enum"] = QJsonArray{"notification", "task"};
    typeProp["description"] = "类型: notification(桌面气泡弹窗提醒), task(到期自动让AI执行任务并输出报告)";
    props["type"] = typeProp;

    QJsonObject triggerSecProp;
    triggerSecProp["type"] = "integer";
    triggerSecProp["description"] = "距离当前时间多少秒后触发 (例如 10分钟后填 600，1小时后填 3600)";
    props["trigger_in_seconds"] = triggerSecProp;

    QJsonObject targetTimeProp;
    targetTimeProp["type"] = "string";
    targetTimeProp["description"] = "具体触发日期时间字符串，格式如 '2026-08-24 15:30' 或 '15:30'";
    props["target_time"] = targetTimeProp;

    QJsonObject repeatProp;
    repeatProp["type"] = "string";
    repeatProp["enum"] = QJsonArray{"once", "interval", "daily", "window_interval"};
    repeatProp["description"] = "重复模式: once(单次倒计时), interval(全天循环), daily(每天固定时间), window_interval(指定时间段内的固定间隔循环，如工作日9点-18点每小时)";
    props["repeat"] = repeatProp;

    QJsonObject intervalSecProp;
    intervalSecProp["type"] = "integer";
    intervalSecProp["description"] = "若为 interval 或 window_interval 模式，循环周期秒数 (例如每隔1小时填 3600，每隔30分钟填 1800)";
    props["repeat_interval_seconds"] = intervalSecProp;

    QJsonObject dailyTimeProp;
    dailyTimeProp["type"] = "string";
    dailyTimeProp["description"] = "若为 daily 模式，每天执行的时间点，格式 'HH:mm' (例如 '09:00')";
    props["daily_time"] = dailyTimeProp;

    QJsonObject startTimeProp;
    startTimeProp["type"] = "string";
    startTimeProp["description"] = "若为 window_interval 模式，时间段开始时间点，格式 'HH:mm' (例如 '09:00')";
    props["start_time"] = startTimeProp;

    QJsonObject endTimeProp;
    endTimeProp["type"] = "string";
    endTimeProp["description"] = "若为 window_interval 模式，时间段结束时间点，格式 'HH:mm' (例如 '18:00')";
    props["end_time"] = endTimeProp;

    QJsonObject weekdaysProp;
    weekdaysProp["type"] = "boolean";
    weekdaysProp["description"] = "是否仅在工作日 (周一至周五) 执行";
    props["weekdays_only"] = weekdaysProp;

    QJsonObject daysOfWeekProp;
    daysOfWeekProp["type"] = "array";
    QJsonObject itemProp;
    itemProp["type"] = "integer";
    daysOfWeekProp["items"] = itemProp;
    daysOfWeekProp["description"] = "允许触发的具体星期几列表 (1=周一, 2=周二, ..., 7=周日)，支持任意组合，如周一/周三/周五填 [1, 3, 5]，周末填 [6, 7]";
    props["days_of_week"] = daysOfWeekProp;

    QJsonObject promptProp;
    promptProp["type"] = "string";
    promptProp["description"] = "若 type 为 task，到期需要让桌宠AI执行的具体任务描述 (如 '查询今日成都天气')";
    props["task_prompt"] = promptProp;

    QJsonObject timerIdProp;
    timerIdProp["type"] = "string";
    timerIdProp["description"] = "修改或删除时指定的定时器ID";
    props["timer_id"] = timerIdProp;

    QJsonObject parameters;
    parameters["type"] = "object";
    parameters["properties"] = props;
    parameters["required"] = QJsonArray{"action"};

    fn["parameters"] = parameters;

    QJsonObject tool;
    tool["type"] = "function";
    tool["function"] = fn;
    return tool;
}

static QString executeTimerTool(const QJsonObject &args) {
    QString action = args["action"].toString("create");
    if (action == "create") {
        QString title = args["title"].toString("定时提醒");
        QString typeStr = args["type"].toString("notification");
        TimerType type = (typeStr == "task") ? TimerType::AiTask : TimerType::Notification;

        QString repStr = args["repeat"].toString("once");
        TimerRepeat repeat = TimerRepeat::Once;
        if (repStr == "interval") repeat = TimerRepeat::Interval;
        else if (repStr == "daily") repeat = TimerRepeat::Daily;
        else if (repStr == "window_interval") repeat = TimerRepeat::WindowInterval;

        int triggerInSec = args["trigger_in_seconds"].toInt(0);
        int intervalSec = args["repeat_interval_seconds"].toInt(0);
        QString dailyTime = args["daily_time"].toString();
        QString startTime = args["start_time"].toString("09:00");
        QString endTime = args["end_time"].toString("18:00");
        bool weekdaysOnly = args["weekdays_only"].toBool(false);
        QString taskPrompt = args["task_prompt"].toString();

        QList<int> daysOfWeek;
        if (args.contains("days_of_week")) {
            auto dowArr = args["days_of_week"].toArray();
            for (auto v : dowArr) daysOfWeek.append(v.toInt());
        }
        if (weekdaysOnly && daysOfWeek.isEmpty()) {
            daysOfWeek = {1, 2, 3, 4, 5};
        }

        // 支持 target_time 字符串解析
        if (triggerInSec <= 0 && args.contains("target_time")) {
            QString tt = args["target_time"].toString();
            QDateTime targetDt = QDateTime::fromString(tt, "yyyy-MM-dd HH:mm");
            if (!targetDt.isValid()) {
                targetDt = QDateTime::fromString(tt, "HH:mm");
                if (targetDt.isValid()) {
                    QDateTime now = QDateTime::currentDateTime();
                    targetDt.setDate(now.date());
                    if (targetDt <= now) targetDt = targetDt.addDays(1);
                }
            }
            if (targetDt.isValid()) {
                qint64 diff = (targetDt.toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch()) / 1000;
                if (diff > 0) triggerInSec = static_cast<int>(diff);
            }
        }

        if (triggerInSec <= 0 && repeat == TimerRepeat::Once) {
            triggerInSec = 60; // 默认保底 1 分钟
        }

        ScheduledTimer timer = TimerManager::instance()->createQuickTimer(
            title, triggerInSec, type, taskPrompt, repeat, intervalSec, dailyTime, startTime, endTime, weekdaysOnly, daysOfWeek
        );

        QString timeDesc;
        QString repDesc;
        if (repeat == TimerRepeat::Daily) {
            timeDesc = QString("每天 %1").arg(dailyTime);
            repDesc = weekdaysOnly ? "工作日 (周一至周五)" : "每天固定时刻";
        } else if (repeat == TimerRepeat::WindowInterval) {
            int mins = (intervalSec > 0 ? intervalSec : 3600) / 60;
            timeDesc = QString("%1 ~ %2 (每隔 %3 分钟)").arg(startTime, endTime).arg(mins);
            repDesc = weekdaysOnly ? "工作日 (周一至周五) 时间段循环" : "每天时间段循环";
        } else if (repeat == TimerRepeat::Interval) {
            timeDesc = QString("每隔 %1 分钟").arg(intervalSec / 60);
            repDesc = "全天固定周期循环";
        } else {
            timeDesc = QDateTime::fromMSecsSinceEpoch(timer.targetTimestamp).toString("yyyy-MM-dd HH:mm:ss");
            repDesc = "单次触发";
        }

        QString nextTriggerStr = QDateTime::fromMSecsSinceEpoch(timer.targetTimestamp).toString("yyyy-MM-dd HH:mm");

        QString res = QString("⏰ **已为您创建定时任务**\n\n"
                              "| 属性 | 内容 |\n"
                              "| :--- | :--- |\n"
                              "| 📌 **任务标题** | %1 |\n"
                              "| 🏷️ **任务类型** | %2 |\n"
                              "| ⏱️ **执行规则** | %3 |\n"
                              "| 🔄 **重复模式** | %4 |\n"
                              "| ⏳ **下次触发** | %5 |\n")
            .arg(title)
            .arg(type == TimerType::AiTask ? "🤖 自动执行任务" : "📢 弹窗提醒")
            .arg(timeDesc)
            .arg(repDesc)
            .arg(nextTriggerStr);

        if (type == TimerType::AiTask && !taskPrompt.isEmpty()) {
            res += QString("| 📋 **执行指令** | %1 |\n").arg(taskPrompt);
        }

        res += "\n💡 到期后桌宠会在桌面上准时跳出来提醒您哦～";
        return res;
    }
    else if (action == "list") {
        auto timers = TimerManager::instance()->getAllTimers();
        if (timers.isEmpty()) {
            return "⏰ 当前没有任何定时任务哦。您可以对我说「10分钟后提醒我喝水」来快速创建！";
        }
        QString res = "⏰ **当前定时任务列表**\n\n| 任务标题 | 类型 | 下次触发 | 状态 |\n| :--- | :--- | :--- | :--- |\n";
        for (const auto &t : timers) {
            QString typeStr = (t.type == TimerType::AiTask ? "🤖 任务" : "📢 提醒");
            QString nextStr = QDateTime::fromMSecsSinceEpoch(t.targetTimestamp).toString("MM-dd HH:mm");
            QString statusStr = t.enabled ? "🟢 运行中" : "⏸ 已暂停";
            res += QString("| %1 | %2 | %3 | %4 |\n").arg(t.title).arg(typeStr).arg(nextStr).arg(statusStr);
        }
        return res;
    }
    else if (action == "delete") {
        QString timerId = args["timer_id"].toString();
        bool ok = TimerManager::instance()->deleteTimer(timerId);
        return ok ? "🗑 定时器已成功删除！" : "❌ 未找到指定的定时器ID。";
    }
    return "已处理定时器指令。";
}

static QJsonObject getMemoryToolDefinition() {
    QJsonObject fn;
    fn["name"] = "memory_manage";
    fn["description"] = "用于长期记忆与用户画像管理：记录用户关键偏好/事实/项目状态、更新主人核心档案、跨时空检索历史记忆。当用户告知个人信息或询问过往记忆时调用。";

    QJsonObject props;

    QJsonObject actionProp;
    actionProp["type"] = "string";
    actionProp["enum"] = QJsonArray{"remember", "update_profile", "search", "forget"};
    actionProp["description"] = "操作类型: remember(记住一条新事实), update_profile(更新主人核心档案), search(搜索过往长期记忆), forget(删除某条记忆)";
    props["action"] = actionProp;

    QJsonObject factProp;
    factProp["type"] = "string";
    factProp["description"] = "若 action 为 remember，需要永久保存的事实内容描述（如'主人最喜欢的桌面环境是 KDE Plasma'）";
    props["fact"] = factProp;

    QJsonObject categoryProp;
    categoryProp["type"] = "string";
    categoryProp["enum"] = QJsonArray{"preference", "tech", "project", "habit", "identity"};
    categoryProp["description"] = "事实分类: preference(偏好), tech(技术栈), project(项目), habit(作息生活习惯), identity(身份)";
    props["category"] = categoryProp;

    QJsonObject importanceProp;
    importanceProp["type"] = "integer";
    importanceProp["description"] = "重要度 1~5 (默认为 3，核心关键事实为 5)";
    props["importance"] = importanceProp;

    QJsonObject profileKeyProp;
    profileKeyProp["type"] = "string";
    profileKeyProp["description"] = "若 action 为 update_profile，更新的档案键 (name, occupation, preferred_langs, music_taste, work_habits, notes)";
    props["profile_key"] = profileKeyProp;

    QJsonObject profileValProp;
    profileValProp["type"] = "string";
    profileValProp["description"] = "若 action 为 update_profile，更新的新值";
    props["profile_value"] = profileValProp;

    QJsonObject queryProp;
    queryProp["type"] = "string";
    queryProp["description"] = "若 action 为 search，搜索的关键词或语义描述";
    props["query"] = queryProp;

    QJsonObject parameters;
    parameters["type"] = "object";
    parameters["properties"] = props;
    parameters["required"] = QJsonArray{"action"};
    fn["parameters"] = parameters;

    QJsonObject tool;
    tool["type"] = "function";
    tool["function"] = fn;
    return tool;
}

static QString executeMemoryTool(const QJsonObject &args) {
    QString action = args["action"].toString("remember");
    if (action == "remember") {
        QString fact = args["fact"].toString().trimmed();
        if (fact.isEmpty()) return "❌ 记忆内容不能为空。";
        QString cat = args["category"].toString("fact");
        int imp = args["importance"].toInt(3);
        QString id = LongTermMemoryEngine::instance()->addSemanticMemory(cat, fact, imp);
        return QString("🧠 已将该事实持久化记录至长期记忆库（ID: %1, 重要度: %2）: %3")
            .arg(id.left(8), QString::number(imp), fact);
    } else if (action == "update_profile") {
        QString key = args["profile_key"].toString().trimmed();
        QString val = args["profile_value"].toString().trimmed();
        if (key.isEmpty() || val.isEmpty()) return "❌ 档案属性名或值不能为空。";
        LongTermMemoryEngine::instance()->updateProfileAttribute(key, val);
        return QString("👤 已更新主人全局核心档案属性 [%1]: %2").arg(key, val);
    } else if (action == "search") {
        QString query = args["query"].toString().trimmed();
        if (query.isEmpty()) return "❌ 请提供需要搜索的记忆关键词。";
        auto memories = LongTermMemoryEngine::instance()->searchMemories(query, 5);
        if (memories.isEmpty()) {
            return QString("🔍 未检索到与「%1」相关的过往记忆。").arg(query);
        }
        QStringList lines;
        lines << QString("🔍 检索到关于「%1」的 %2 条长期记忆:").arg(query, QString::number(memories.size()));
        for (const auto &m : memories) {
            lines << QString("- [%1 | 星级:%2] %3").arg(m.category, QString::number(m.importance), m.content);
        }
        return lines.join("\n");
    } else if (action == "forget") {
        QString query = args["query"].toString().trimmed();
        if (query.isEmpty()) return "❌ 请提供需要遗忘的记忆关键词。";
        auto list = LongTermMemoryEngine::instance()->searchMemories(query, 1);
        if (!list.isEmpty()) {
            LongTermMemoryEngine::instance()->deleteSemanticMemory(list.first().id);
            return QString("🗑️ 已从长期记忆库中遗忘: %1").arg(list.first().content);
        }
        return QString("未找到匹配「%1」的记忆条目。").arg(query);
    }
    return "已处理记忆管理指令。";
}

AgentService *AgentService::instance() {
    static AgentService s_instance;
    return &s_instance;
}

AgentService::AgentService()
    : m_networkManager(new QNetworkAccessManager())
{
    loadConfig();
    loadMemoryFromFile();
    initAdapters();
    SkillManager::instance()->init();
    McpManager::instance()->init();
}

void AgentService::initAdapters() {
    auto aipy = std::make_shared<AipyAdapter>(m_networkManager);
    m_adapters["aipy"] = aipy;
    syncAdapterConfigs();
}

void AgentService::syncAdapterConfigs() {
    if (auto aipy = aipyAdapter()) {
        if (!m_config.aipyBase.isEmpty()) aipy->setBaseUrl(m_config.aipyBase);
        if (!m_config.aipyKey.isEmpty()) aipy->setApiKey(m_config.aipyKey);
    }
}

AipyAdapter *AgentService::aipyAdapter() const {
    auto it = m_adapters.find("aipy");
    if (it != m_adapters.end()) {
        return dynamic_cast<AipyAdapter*>(it->second.get());
    }
    return nullptr;
}

QVector<ModelProfile> AgentService::defaultBuiltinProfiles() {
    return {
        {"deepseek", "DeepSeek (官方 API)", "https://api.deepseek.com/v1", "", "deepseek-chat", true},
        {"siliconflow", "硅基流动 (SiliconFlow)", "https://api.siliconflow.cn/v1", "", "deepseek-ai/DeepSeek-V3", true},
        {"qwen", "通义千问 (阿里云百炼)", "https://dashscope.aliyuncs.com/compatible-mode/v1", "", "qwen-plus", true},
        {"zhipu", "智谱清言 (GLM 开放平台)", "https://open.bigmodel.cn/api/paas/v4", "", "glm-4-flash", true},
        {"moonshot", "Moonshot (Kimi 官方)", "https://api.moonshot.cn/v1", "", "moonshot-v1-8k", true},
        {"openai", "OpenAI (官方 API)", "https://api.openai.com/v1", "", "gpt-4o-mini", true},
        {"ollama", "本地 Ollama (127.0.0.1:11434)", "http://127.0.0.1:11434/v1", "ollama", "qwen2.5:7b", true}
    };
}

ModelProfile AgentService::getActiveProfile() const {
    for (const auto &p : m_config.modelProfiles) {
        if (p.id == m_config.activeProfileId) {
            return p;
        }
    }
    if (!m_config.modelProfiles.isEmpty()) {
        return m_config.modelProfiles.first();
    }
    return {"custom", "自定义大模型", m_config.apiBase, m_config.apiKey, m_config.model, true};
}

void AgentService::setActiveProfile(const QString &profileId) {
    for (auto &p : m_config.modelProfiles) {
        if (p.id == profileId) {
            m_config.activeProfileId = profileId;
            m_config.apiBase = p.apiBase;
            m_config.apiKey = p.apiKey;
            m_config.model = p.model;
            saveConfig();
            std::cout << "[AgentService] 已切换活动大模型配置: " << p.name.toStdString() 
                      << " (model=" << p.model.toStdString() << ")" << std::endl;
            return;
        }
    }
}

void AgentService::addOrUpdateProfile(const ModelProfile &profile) {
    bool found = false;
    for (auto &p : m_config.modelProfiles) {
        if (p.id == profile.id) {
            p = profile;
            found = true;
            break;
        }
    }
    if (!found) {
        m_config.modelProfiles.append(profile);
    }
    if (m_config.activeProfileId == profile.id) {
        m_config.apiBase = profile.apiBase;
        m_config.apiKey = profile.apiKey;
        m_config.model = profile.model;
    }
    saveConfig();
}

void AgentService::deleteProfile(const QString &profileId) {
    for (int i = 0; i < m_config.modelProfiles.size(); ++i) {
        if (m_config.modelProfiles[i].id == profileId) {
            m_config.modelProfiles.removeAt(i);
            break;
        }
    }
    if (m_config.activeProfileId == profileId) {
        if (!m_config.modelProfiles.isEmpty()) {
            setActiveProfile(m_config.modelProfiles.first().id);
        }
    }
    saveConfig();
}

void AgentService::setAutoFailover(bool enable) {
    m_config.enableAutoFailover = enable;
    saveConfig();
}

void AgentService::loadConfig(QString const& path) {
    auto db = SettingsDb::instance();

    // 1. 读取多模型配置池
    m_config.modelProfiles.clear();
    QJsonArray profilesArr = db->getJsonArray("agent.model_profiles");
    if (profilesArr.isEmpty()) {
        m_config.modelProfiles = defaultBuiltinProfiles();
    } else {
        for (auto val : profilesArr) {
            auto obj = val.toObject();
            ModelProfile p;
            p.id = obj["id"].toString();
            p.name = obj["name"].toString();
            p.apiBase = obj["api_base"].toString();
            p.apiKey = obj["api_key"].toString();
            p.model = obj["model"].toString();
            p.enabled = obj.contains("enabled") ? obj["enabled"].toBool(true) : true;
            if (!p.id.isEmpty()) {
                m_config.modelProfiles.append(p);
            }
        }
        if (m_config.modelProfiles.isEmpty()) {
            m_config.modelProfiles = defaultBuiltinProfiles();
        }
    }

    m_config.activeProfileId = db->get("agent.active_profile_id", "deepseek");
    m_config.enableAutoFailover = db->getBool("agent.enable_auto_failover", true);

    // 检查数据库中是否已有设置。如果没有，且存在旧文件，尝试从旧文件做一次无缝迁移
    if (!db->contains("agent.api_base") && !db->contains("agent.model")) {
        QFile file(path.isEmpty() ? "config.json" : path);
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                auto obj = doc.object();
                if (obj.contains("api_base")) m_config.apiBase = obj["api_base"].toString();
                if (obj.contains("api_key")) m_config.apiKey = obj["api_key"].toString();
                if (obj.contains("model")) m_config.model = obj["model"].toString();
                if (obj.contains("max_memory_turns")) m_config.maxMemoryTurns = obj["max_memory_turns"].toInt(6);
                if (obj.contains("hotkey_translate")) m_config.hotkeyTranslate = obj["hotkey_translate"].toString("Option+T");
                if (obj.contains("hotkey_ask")) m_config.hotkeyAsk = obj["hotkey_ask"].toString("Option+Q");

                if (obj.contains("hotkey_music_toggle")) m_config.hotkeyMusicToggle = obj["hotkey_music_toggle"].toString("Option+M");
                if (obj.contains("hotkey_music_play_pause")) m_config.hotkeyMusicPlayPause = obj["hotkey_music_play_pause"].toString("Option+Space");
                if (obj.contains("hotkey_music_next")) m_config.hotkeyMusicNext = obj["hotkey_music_next"].toString("Option+Right");
                if (obj.contains("hotkey_music_prev")) m_config.hotkeyMusicPrev = obj["hotkey_music_prev"].toString("Option+Left");
                if (obj.contains("hotkey_music_fav")) m_config.hotkeyMusicFav = obj["hotkey_music_fav"].toString("Option+L");

                if (obj.contains("active_agent_type")) m_config.activeAgentType = obj["active_agent_type"].toString("aipy");
                if (obj.contains("aipy_base")) m_config.aipyBase = obj["aipy_base"].toString("http://127.0.0.1:41970");
                if (obj.contains("aipy_key")) m_config.aipyKey = obj["aipy_key"].toString("");
                if (obj.contains("routing_mode")) m_config.routingMode = obj["routing_mode"].toString("AUTO");
            }
            file.close();
            saveConfig();
        } else {
            saveConfig();
        }
    } else {
        // 从 SQLite 数据库读取配置
        m_config.apiBase = db->get("agent.api_base", "https://api.deepseek.com/v1");
        m_config.apiKey = db->get("agent.api_key", "");
        m_config.model = db->get("agent.model", "deepseek-chat");
        m_config.maxMemoryTurns = db->getInt("agent.max_memory_turns", 6);
        m_config.hotkeyTranslate = db->get("hotkey.translate", "Option+T");
        m_config.hotkeyAsk = db->get("hotkey.ask", "Option+Q");
        m_config.hotkeyHistory = db->get("hotkey.history", "Option+H");

        m_config.hotkeyMusicToggle = db->get("hotkey.music_toggle", "Option+M");
        m_config.hotkeyMusicPlayPause = db->get("hotkey.music_play_pause", "Option+Space");
        m_config.hotkeyMusicNext = db->get("hotkey.music_next", "Option+Right");
        m_config.hotkeyMusicPrev = db->get("hotkey.music_prev", "Option+Left");
        m_config.hotkeyMusicFav = db->get("hotkey.music_fav", "Option+L");

        m_config.activeAgentType = db->get("agent.active_type", "aipy");
        m_config.aipyBase = db->get("agent.aipy_base", "http://127.0.0.1:41970");
        m_config.aipyKey = db->get("agent.aipy_key", "");
        m_config.routingMode = db->get("agent.routing_mode", "AUTO");

        m_config.enableAgentStateHook = db->getBool("agent.enable_state_hook", true);
        m_config.enableLlmTaskNarration = db->getBool("agent.enable_llm_narration", false);
        m_config.stateDebounceSec = db->getInt("agent.state_debounce_sec", 2);
        m_config.banterFrequencyLevel = db->getInt("agent.banter_freq_level", 2);
        m_config.enableContextualCare = db->getBool("agent.enable_contextual_care", true);
    }

    // 同步活动配置至当前配置
    auto activeProf = getActiveProfile();
    if (!activeProf.apiBase.isEmpty()) {
        m_config.apiBase = activeProf.apiBase;
        m_config.apiKey = activeProf.apiKey;
        m_config.model = activeProf.model;
    }

    syncAdapterConfigs();
}

void AgentService::saveConfig(QString const& /* path */) {
    auto db = SettingsDb::instance();

    // 1. 保存多模型配置池
    QJsonArray profilesArr;
    for (const auto &p : m_config.modelProfiles) {
        QJsonObject obj;
        obj["id"] = p.id;
        obj["name"] = p.name;
        obj["api_base"] = p.apiBase;
        obj["api_key"] = p.apiKey;
        obj["model"] = p.model;
        obj["enabled"] = p.enabled;
        profilesArr.append(obj);
    }
    db->setJsonArray("agent.model_profiles", profilesArr);
    db->set("agent.active_profile_id", m_config.activeProfileId);
    db->setBool("agent.enable_auto_failover", m_config.enableAutoFailover);

    db->set("agent.api_base", m_config.apiBase);
    db->set("agent.api_key", m_config.apiKey);
    db->set("agent.model", m_config.model);
    db->setInt("agent.max_memory_turns", m_config.maxMemoryTurns);
    db->set("hotkey.translate", m_config.hotkeyTranslate);
    db->set("hotkey.ask", m_config.hotkeyAsk);
    db->set("hotkey.history", m_config.hotkeyHistory);

    db->set("hotkey.music_toggle", m_config.hotkeyMusicToggle);
    db->set("hotkey.music_play_pause", m_config.hotkeyMusicPlayPause);
    db->set("hotkey.music_next", m_config.hotkeyMusicNext);
    db->set("hotkey.music_prev", m_config.hotkeyMusicPrev);
    db->set("hotkey.music_fav", m_config.hotkeyMusicFav);

    db->set("agent.active_type", m_config.activeAgentType);
    db->set("agent.aipy_base", m_config.aipyBase);
    db->set("agent.aipy_key", m_config.aipyKey);
    db->set("agent.routing_mode", m_config.routingMode);

    db->setBool("agent.enable_state_hook", m_config.enableAgentStateHook);
    db->setBool("agent.enable_llm_narration", m_config.enableLlmTaskNarration);
    db->setInt("agent.state_debounce_sec", m_config.stateDebounceSec);
    db->setInt("agent.banter_freq_level", m_config.banterFrequencyLevel);
    db->setBool("agent.enable_contextual_care", m_config.enableContextualCare);
}

void AgentService::setConfig(AgentConfig const& cfg) {
    m_config = cfg;
    syncAdapterConfigs();
    saveConfig();
    ShijimaManager::defaultManager()->updateGlobalHotkeys();
}

bool AgentService::containsChinese(QString const& text) {
    for (QChar ch : text) {
        ushort u = ch.unicode();
        if (u >= 0x4E00 && u <= 0x9FA5) {
            return true;
        }
    }
    return false;
}

void AgentService::appendMemory(QString const& role, QString const& content) {
    QJsonObject item;
    item["role"] = role;
    item["content"] = content;
    m_history.append(item);

    // 记录完整情节点事件 (L2 Episodic Events)
    LongTermMemoryEngine::instance()->recordEpisodicEvent("default", role, content);

    int maxItems = m_config.maxMemoryTurns * 2;
    if (m_history.size() > maxItems) {
        // 会话滚动压缩 (Rolling Compaction)：提取超出的早期对话在后台异步压缩为摘要
        int overflow = m_history.size() - maxItems;
        int compressCount = std::max(overflow, 4);
        compressCount = std::min(compressCount, static_cast<int>(m_history.size() - 2));
        if (compressCount > 0) {
            QJsonArray overflowArr;
            for (int i = 0; i < compressCount; ++i) {
                overflowArr.append(m_history[i]);
            }
            for (int i = 0; i < compressCount; ++i) {
                m_history.removeFirst();
            }
            LongTermMemoryEngine::instance()->triggerRollingCompaction(
                overflowArr, m_config.apiBase, m_config.apiKey, m_config.model, [](const QString &summary) {
                    if (!summary.isEmpty()) {
                        std::cout << "[RollingCompactor] 成功生成并更新滚动会话上下文摘要 ("
                                  << summary.length() << " 字)" << std::endl;
                    }
                }
            );
        }
    }
    saveMemoryToFile();
}

void AgentService::clearMemory() {
    m_history = QJsonArray();
    saveMemoryToFile();
}

void AgentService::saveMemoryToFile() {
    SettingsDb::instance()->setJsonArray("agent.memory_history", m_history);
}

void AgentService::loadMemoryFromFile() {
    auto db = SettingsDb::instance();
    if (db->contains("agent.memory_history")) {
        m_history = db->getJsonArray("agent.memory_history");
    } else {
        // 尝试从旧文件做一次迁移
        QFile file("memory.json");
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            auto doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                m_history = doc.array();
                saveMemoryToFile();
            }
            file.close();
        }
    }
}

void AgentService::translate(QString const& text, std::function<void(bool success, QString const& result)> callback) {
    if (text.trimmed().isEmpty()) {
        callback(false, "没有选中文本");
        return;
    }

    bool isChinese = containsChinese(text);
    QString systemPrompt;
    if (isChinese) {
        systemPrompt = 
            "你是一位精通中英双语的高级翻译与语言学专家。请针对用户输入的中文内容提供专业、详尽、多元的英文翻译：\n\n"
            "1. **若输入为单词/短语/词组**：\n"
            "   - 给出最贴切、常用的多个英文翻译（按词性如 n. / v. / adj. / adv. 分类并标注）；\n"
            "   - 提供不同语境下的表达（如：💬 常用口语、📄 正式书面、⚡ 行业/技术术语）；\n"
            "   - 附带 1~2 个典型地道的双语示例短语或例句。\n"
            "2. **若输入为句子/段落**：\n"
            "   - **🎯 首选译文**：最自然流畅、符合母语习惯的译文；\n"
            "   - **🔄 多元译法**：提供不同风格的备选方案（如：💬 地道口语 / 📄 正式书面 / ⚡ 极简表达）；\n"
            "   - **💡 核心词汇/搭配**（如适用）：简要提炼句中重点词汇或短语用法。\n\n"
            "【排版规范】：请使用结构清晰优雅的 Markdown 格式输出，适当使用 emoji 增加可读性。直接输出翻译与解析内容，不要出现任何客套或多余的寒暄语。";
    } else {
        systemPrompt = 
            "你是一位精通多语言的高级翻译与双语词典专家。请针对用户输入的文本提供专业、详尽、多元的中文翻译：\n\n"
            "1. **若输入为单个单词/短语/专有名词**：\n"
            "   - 标注音标/发音（如英文单词标注国际音标）；\n"
            "   - 详细列出所有常见词性及多种释义（如 n. 释义1；释义2 / v. 释义1；释义2）；\n"
            "   - 提供不同语境/专业领域的翻译、近义词拓展或常见固定搭配；\n"
            "   - 附带 1~2 个精选例句及其中文对照。\n"
            "2. **若输入为句子/段落**：\n"
            "   - **🎯 首选译文**：最通顺优美、符合中文表达习惯的翻译；\n"
            "   - **🔄 多元译法**：提供不同风格的备选方案（如：💬 地道口语 / 📄 正式书面 / ⚡ 极简/意译）；\n"
            "   - **💡 难点/重点解析**（如适用）：简要注释句中的核心短语、俚语或语法要点。\n\n"
            "【排版规范】：请使用结构清晰优雅的 Markdown 格式输出，适当使用 emoji 增加层次感。直接输出翻译与解析内容，不要出现任何客套或多余的寒暄语。";
    }

    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = text;
    messages.append(userMsg);

    sendChatCompletion(messages, [this, text, callback](bool success, QString const& result) {
        if (success) {
            appendMemory("user", QString("[划词翻译]: %1").arg(text));
            appendMemory("assistant", QString("[译文]: %1").arg(result));
        }
        callback(success, result);
    });
}

void AgentService::ask(QString const& contextText,
                      QString const& question,
                      std::function<void(QString const& progressMsg)> progressCallback,
                      std::function<void(bool success, QString const& result, QString const& appTarget)> finishCallback)
{
    if (question.trimmed().isEmpty()) {
        finishCallback(false, "问题不能为空", "");
        return;
    }

    QString qLower = question.toLower();

    // 智能分流判定：若包含定时器/提醒意图关键词，优先走带 Tool Calling 的大模型
    bool isTimerIntent = false;
    static const QStringList timerKeywords = {
        "提醒", "定时", "闹钟", "分钟后", "小时后", "秒后", "每天", "每隔", "几点",
        "timer", "remind", "alarm", "schedule", "cron", "几分钟后", "倒计时"
    };
    for (const auto &kw : timerKeywords) {
        if (qLower.contains(kw)) {
            isTimerIntent = true;
            break;
        }
    }

    // 智能分流判定：若包含音乐播放/推荐/歌单/搜歌/喜好画像/偏好标签等意图，走音乐工具
    bool isMusicIntent = false;
    static const QStringList musicKeywords = {
        "歌", "音乐", "播放", "暂停", "下一首", "上一首", "切歌", "放一首", "听歌",
        "民谣", "播放列表", "加歌", "歌单", "music", "song", "play", "playlist",
        "推荐点", "推荐", "画像", "标签", "偏好", "喜好", "熟悉模式", "探索模式", "随机模式",
        "放歌", "搜歌", "曲目", "榜单", "热歌", "我的画像", "任务画像", "用户画像", "我的偏好"
    };
    for (const auto &kw : musicKeywords) {
        if (qLower.contains(kw)) {
            isMusicIntent = true;
            break;
        }
    }

    // 智能分流判定：若配置为 aipy 智能体，或包含通用任务指令关键词（非纯定时/音乐需求）
    bool isTaskIntent = false;
    if (!isTimerIntent && !isMusicIntent) {
        if (m_config.routingMode == "ALWAYS_AGENT") {
            isTaskIntent = true;
        } else if (m_config.routingMode == "ALWAYS_LLM") {
            isTaskIntent = false;
        } else {
            static const QStringList taskKeywords = {
                "打开", "搜索", "分析", "爬取", "执行", "运行", "编写", "帮我", "生成",
                "查询", "统计", "抓取", "监控", "下载", "上传", "测试", "部署", "清理",
                "转换", "备份", "总结", "提取", "重构", "实现", "自动化", "创建", "修改",
                "删除", "启动", "停止", "检查", "调用", "查找", "定位", "修复", "解释",
                "新建", "查一下", "搜一下", "改一下", "做一下", "看下", "查下", "搜下",
                "task", "create", "find", "search", "run", "exec", "build", "debug", "test"
            };
            for (const auto &kw : taskKeywords) {
                if (qLower.contains(kw)) {
                    isTaskIntent = true;
                    break;
                }
            }
            if (!contextText.trimmed().isEmpty() && question.length() >= 4) {
                isTaskIntent = true;
            }
        }
    }

    if (isTaskIntent && m_adapters.count("aipy") > 0) {
        if (progressCallback) {
            progressCallback("🤖 正在调度 aipy-pro Agent 处理复杂任务...");
        }
        auto adapter = m_adapters["aipy"];
        adapter->executeTask(question, contextText, progressCallback, [this, question, finishCallback](AgentTaskResult const& res) {
            if (res.success) {
                appendMemory("user", question);
                appendMemory("assistant", res.reply);
                PetMemory::instance()->autoLearnFromChat(question, res.reply);
                LongTermMemoryEngine::instance()->triggerAsyncReflection(question, res.reply, m_config.apiBase, m_config.apiKey, m_config.model);
                finishCallback(true, res.reply, res.appName);
            } else {
                finishCallback(false, res.error, "");
            }
        });
        return;
    }

    // 默认直连大模型问答（注入当前本地系统时间、人格设定、长期记忆、定时器 Tool、音乐播放器 Tool、网络搜索 Tool 与记忆管理指南）
    QString currentLocalTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss dddd");
    QString personaPrompt = PersonaManager::instance()->buildEffectiveSystemPrompt();

    // 组装 L1 核心主人画像 + L3 动态语义检索召回 + L0 滚动会话背景
    QString profileStr = LongTermMemoryEngine::instance()->formatProfileForPrompt();
    QString recalledMemories = LongTermMemoryEngine::instance()->formatMemoriesForPrompt(question, 4);
    QString rollingSummary = LongTermMemoryEngine::instance()->formatContextSummaryForPrompt("default");

    QString memoryContextSection;
    if (!profileStr.isEmpty()) memoryContextSection += "\n" + profileStr;
    if (!recalledMemories.isEmpty()) memoryContextSection += "\n\n" + recalledMemories;
    if (!rollingSummary.isEmpty()) memoryContextSection += "\n\n" + rollingSummary;

    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = QString(
        "%1\n\n"
        "【当前本地真实系统时间】: %2。\n"
        "%3\n\n"
        "你拥有管理本地系统定时器工具（timer_manage）、音乐播放器工具（music_player_manage）、全网实时搜索工具（web_search）与长期记忆管理工具（memory_manage）。\n\n"
        "【全网实时搜索工具（web_search）使用原则】\n"
        "- 当用户询问任何最新事实、时事新闻、今日天气、地方领导人事（如现任/历任市委书记、市长、官员任免）、百科动态、股市金融、技术发布、热点排行等内容时，【必须第一步调用 web_search(query=...) 联网检索】！\n"
        "- 严禁凭空盲猜或使用可能过时的离线训练记忆回答！先联网搜索获得最新真实证据后，再给出条理分明的回答。\n\n"
        "【长期记忆与画像工具使用指南】\n"
        "- 当用户透露其身份、技术栈、喜好、作息习惯或正在开发的项目时，主动调用 memory_manage 记录重要事实(remember)或更新主人档案(update_profile)；\n"
        "- 当需要检索用户过往信息时，可调用 memory_manage(action='search') 进行跨会话深度检索。\n\n"
        "【音乐播放器与喜好推荐工具指南】\n"
        "1. 智能按模式推荐歌曲 (每次6首): action='recommend_by_mode', mode='familiar'|'explore'|'random'|'default', play_now=true\n"
        "   - 当用户说「推荐音乐」、「推荐点歌」、「放歌」、「来点音乐」、「推荐歌曲」时，直接调用 recommend_by_mode！\n"
        "   - 'familiar' (熟悉模式): 从用户收藏池 + 喜好偏好衍生推荐 6 首；\n"
        "   - 'explore' (探索模式): 推荐当前全网最新最火爆的热点流行歌曲 6 首；\n"
        "   - 'random' (随机模式): 按照用户的喜好标签（如周杰伦、民谣、摇滚等）随机组合推荐 6 首；\n"
        "   - 系统内置了近期推荐去重机制，连续推荐不会重复拉取相同歌曲。\n"
        "2. 查看与管理喜好标签/画像/推荐模式:\n"
        "   - 查看画像与喜好标签: action='list_preference_tags' (当用户询问「我的画像」、「任务画像」、「用户画像」、「喜好标签」、「音乐偏好」、「我的喜好」时立即调用！)\n"
        "   - 添加喜好标签: action='add_preference_tag', tags=['摇滚', '民谣']\n"
        "   - 删除喜好标签: action='remove_preference_tag', tags=['古风']\n"
        "   - 切换推荐模式: action='set_recommend_mode', mode='familiar'|'explore'|'random'\n"
        "3. 互联网搜索并批量加歌:\n"
        "   - 若用户要求推荐特定风格（如抖音热歌），可先调用 web_search 检索歌名，再调用 batch_search_and_add 批量入库！\n"
        "4. 单曲搜索与播放控制: search_and_play, pause, resume, next, previous, favorite, list_favorites\n\n"
        "【定时器功能支持灵活组合】\n"
        "1. 单次倒计时/定时刻: repeat='once', trigger_in_seconds 或 target_time='2026-08-24 15:00'\n"
        "2. 每天固定时刻: repeat='daily', daily_time='09:00', 可配合 days_of_week 指定任意星期\n"
        "3. 全天固定循环: repeat='interval', repeat_interval_seconds=1800 (每30分钟)\n"
        "4. 指定时间段内按间隔循环: repeat='window_interval', start_time='09:00', end_time='18:00', repeat_interval_seconds=3600\n"
        "5. 星期过滤: days_of_week 可自由指定任意组合 (1=周一, 2=周二, ..., 7=周日)\n\n"
        "请结合上下文和用户的参考文本，给出符合你人格设定的准确、简洁、友善的回答，适合在桌面气泡中阅读。"
    ).arg(personaPrompt, currentLocalTime, memoryContextSection);
    messages.append(sysMsg);

    for (auto val : m_history) {
        messages.append(val.toObject());
    }

    QString combinedUserContent;
    if (!contextText.trimmed().isEmpty()) {
        combinedUserContent = QString("【参考选中文本】:") + QChar(10) + contextText + QString(QChar(10)) + QString(QChar(10)) + QString("【我的问题】:") + QChar(10) + question;
    } else {
        combinedUserContent = question;
    }

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = combinedUserContent;
    messages.append(userMsg);

    if (progressCallback) {
        progressCallback("🤔 正在思考并规划操作...");
    }

    sendChatCompletion(messages, [this, question, finishCallback](bool success, QString const& result) {
        QString outAction;
        QString cleanResult;
        PersonaManager::parseActionAndContent(result, outAction, cleanResult);

        // 如果模型输出带有动作指令标签，调度桌宠做动作
        if (!outAction.isEmpty()) {
            PetActionCommand cmd;
            cmd.speechText = "";
            if (outAction == "jump") cmd.type = PetActionType::Jump;
            else if (outAction == "sit") cmd.type = PetActionType::Sit;
            else if (outAction == "crawl") cmd.type = PetActionType::LieDown;
            else if (outAction == "celebrate" || outAction == "happy") cmd.type = PetActionType::Happy;
            else if (outAction == "resist" || outAction == "angry") cmd.type = PetActionType::Angry;
            else if (outAction == "walk") cmd.type = PetActionType::Walk;
            else {
                cmd.type = PetActionType::CustomBehavior;
                cmd.customBehaviorName = outAction;
            }
            auto const& mascots = ShijimaManager::defaultManager()->mascots();
            if (!mascots.empty()) {
                mascots.front()->doAction(cmd);
            }
        }

        if (success) {
            appendMemory("user", question);
            appendMemory("assistant", cleanResult);
            PetMemory::instance()->autoLearnFromChat(question, cleanResult);
            LongTermMemoryEngine::instance()->triggerAsyncReflection(question, cleanResult, m_config.apiBase, m_config.apiKey, m_config.model);
        }
        finishCallback(success, cleanResult, "");
    });
}

void AgentService::openTask(QString const& taskId) {
    if (auto aipy = aipyAdapter()) {
        aipy->openTask(taskId);
    }
}

void AgentService::testAgentConnection(QString const& agentType, std::function<void(bool success, QString const& message)> callback) {
    if (m_adapters.count(agentType) > 0) {
        m_adapters[agentType]->testConnection(callback);
    } else {
        callback(false, "未找到指定的 Agent 适配器类型: " + agentType);
    }
}

void AgentService::testConnection(QString const& apiBase, QString const& apiKey, QString const& model, std::function<void(bool success, QString const& message)> callback) {
    if (apiKey.trimmed().isEmpty()) {
        callback(false, "API Key 不能为空，请输入有效的 API 密钥");
        return;
    }

    QString endpoint = apiBase.trimmed();
    while (endpoint.endsWith('/')) {
        endpoint.chop(1);
    }
    if (!endpoint.endsWith("/chat/completions")) {
        endpoint += "/chat/completions";
    }

    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey.trimmed()).toUtf8());

    QJsonObject root;
    root["model"] = model.trimmed().isEmpty() ? "gpt-4o-mini" : model.trimmed();
    
    QJsonArray testMsgs;
    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = "Hello, please reply with OK.";
    testMsgs.append(msg);
    root["messages"] = testMsgs;
    root["max_tokens"] = 15;

    QByteArray postData = QJsonDocument(root).toJson();
    QNetworkReply *reply = m_networkManager->post(request, postData);

    QObject::connect(reply, &QNetworkReply::finished, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QString errStr = QString("HTTP连接失败 (%1): %2").arg(QString::number(reply->error()), reply->errorString());
            callback(false, errStr);
            return;
        }

        QByteArray data = reply->readAll();
        QJsonParseError parseError;
        auto doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            callback(false, "接口响应数据解析失败，非合法 JSON 格式");
            return;
        }

        auto rootObj = doc.object();
        if (rootObj.contains("error")) {
            QString err = rootObj["error"].toObject()["message"].toString();
            callback(false, "服务商报错: " + err);
            return;
        }

        auto choices = rootObj["choices"].toArray();
        if (choices.isEmpty()) {
            callback(false, "连接成功但模型未返回有效候选内容");
            return;
        }

        auto choiceObj = choices[0].toObject();
        auto msgObj = choiceObj["message"].toObject();
        QString content = msgObj["content"].toString().trimmed();

        callback(true, "连接畅通！模型响应: " + content);
    });
}

static QJsonObject getWebSearchToolDefinition() {
    QJsonObject fn;
    fn["name"] = "web_search";
    fn["description"] = "联网搜索工具。获取互联网最新资讯、新闻事件、百科数据、地方领导人事、天气、时事动态、股市行情、技术文档等全网实时事实。当用户询问最新/实时数据、地方政务历史事实、新闻热点或需要联网验证时必须调用此工具。";

    QJsonObject props;
    QJsonObject queryProp;
    queryProp["type"] = "string";
    queryProp["description"] = "搜索关键词，例如'四川巴中历任市委书记名单'、'今日上海天气'";
    props["query"] = queryProp;

    QJsonObject params;
    params["type"] = "object";
    params["properties"] = props;
    params["required"] = QJsonArray{"query"};

    QJsonObject result;
    result["type"] = "function";
    QJsonObject fnObj = fn;
    fnObj["parameters"] = params;
    result["function"] = fnObj;
    return result;
}

static void executeWebSearchTool(const QJsonObject &args, std::function<void(QString)> callback) {
    QString query = args["query"].toString().trimmed();
    if (query.isEmpty()) {
        if (callback) callback("搜索关键词为空");
        return;
    }

    std::cout << "[WebSearchTool] 正在执行互联网多源实时搜索: query=" << query.toStdString() << std::endl;

    WebSearchEngine::instance()->search(query, [callback](bool, const QVector<WebSearchResult>&, const QString &formattedMarkdown) {
        if (callback) callback(formattedMarkdown);
    });
}

void AgentService::sendChatCompletion(QJsonArray const& messages, std::function<void(bool success, QString const& result)> callback) {
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, messages, callback]() {
            sendChatCompletion(messages, callback);
        }, Qt::QueuedConnection);
        return;
    }

    if (m_config.apiKey.trimmed().isEmpty() || m_config.apiKey.contains("YOUR_API_KEY")) {
        QString demoReply = QString("💡 提示: 请在项目设置或 config.json 中配置您的 api_key 启用大模型推理。");
        callback(true, demoReply);
        return;
    }

    auto currentMessages = std::make_shared<QJsonArray>(messages);
    auto turnCount = std::make_shared<int>(0);
    auto accumulatedActions = std::make_shared<QStringList>();

    auto runStep = std::make_shared<std::function<void()>>();
    *runStep = [this, currentMessages, turnCount, accumulatedActions, runStep, callback]() {
        if (*turnCount >= 5) {
            callback(true, accumulatedActions->join("\n\n"));
            return;
        }
        (*turnCount)++;

        QString endpoint = m_config.apiBase.trimmed();
        while (endpoint.endsWith('/')) {
            endpoint.chop(1);
        }
        if (!endpoint.endsWith("/chat/completions")) {
            endpoint += "/chat/completions";
        }

        QNetworkRequest request{QUrl(endpoint)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", QString("Bearer %1").arg(m_config.apiKey).toUtf8());

        QJsonObject root;
        root["model"] = m_config.model;
        root["messages"] = *currentMessages;
        root["temperature"] = 0.3;

        // 注入全部工具：长期记忆 + 定时器 + 音乐管理 + 内置网络搜索 + MCP 外部工具
        QJsonArray toolsArr;
        toolsArr.append(getMemoryToolDefinition());
        toolsArr.append(getTimerToolDefinition());
        toolsArr.append(getMusicToolDefinition());
        toolsArr.append(getWebSearchToolDefinition());

        // 针对阿里云百炼 Qwen 模型，开启原生实时搜索
        if (m_config.apiBase.contains("aliyuncs.com")) {
            root["enable_search"] = true;
        }

        QJsonArray mcpTools = McpManager::instance()->getAllToolDefinitions();
        for (auto tVal : mcpTools) {
            toolsArr.append(tVal);
        }
        if (!toolsArr.isEmpty()) {
            root["tools"] = toolsArr;
        }

        QByteArray postData = QJsonDocument(root).toJson();
        QNetworkReply *reply = m_networkManager->post(request, postData);

        QObject::connect(reply, &QNetworkReply::finished, [this, reply, currentMessages, turnCount, accumulatedActions, runStep, callback]() {
            reply->deleteLater();

            auto handleFailover = [this, currentMessages, turnCount, accumulatedActions, runStep, callback](const QString &failedReason) -> bool {
                if (!m_config.enableAutoFailover) return false;

                // 寻找下一个有效且启用的备用模型
                ModelProfile nextProfile;
                bool foundNext = false;
                for (const auto &p : m_config.modelProfiles) {
                    if (p.id != m_config.activeProfileId && p.enabled && (!p.apiKey.isEmpty() || p.id == "ollama")) {
                        nextProfile = p;
                        foundNext = true;
                        break;
                    }
                }

                if (foundNext) {
                    QString oldName = getActiveProfile().name;
                    std::cout << "[AgentService Failover] 模型【" << oldName.toStdString() << "】调用失败 ("
                              << failedReason.toStdString() << ")，正在自动切换至备用模型【"
                              << nextProfile.name.toStdString() << "】重新生成..." << std::endl;

                    setActiveProfile(nextProfile.id);

                    ShijimaManager::defaultManager()->onTickAsync([oldName, nextProfile](ShijimaManager *mgr) {
                        auto const& mascots = mgr->mascots();
                        if (!mascots.empty()) {
                            mascots.front()->showMessage(QString("⚠️ 【%1】调用异常，已自动无缝切换至【%2】重新生成！").arg(oldName, nextProfile.name), 4000);
                        }
                    });

                    (*runStep)();
                    return true;
                }
                return false;
            };

            if (reply->error() != QNetworkReply::NoError) {
                QString errStr = QString("请求失败: %1").arg(reply->errorString());
                if (handleFailover(errStr)) {
                    return;
                }
                callback(false, errStr);
                return;
            }

            QByteArray data = reply->readAll();
            QJsonParseError parseError;
            auto doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                QString errStr = "解析响应数据失败";
                if (handleFailover(errStr)) {
                    return;
                }
                callback(false, errStr);
                return;
            }

            auto rootObj = doc.object();
            if (rootObj.contains("error")) {
                QString err = rootObj["error"].toObject()["message"].toString();
                if (handleFailover(err)) {
                    return;
                }
                callback(false, "API错误: " + err);
                return;
            }

            auto choices = rootObj["choices"].toArray();
            if (choices.isEmpty()) {
                QString errStr = "模型返回内容为空";
                if (handleFailover(errStr)) {
                    return;
                }
                callback(false, errStr);
                return;
            }

            auto choiceObj = choices[0].toObject();
            auto msgObj = choiceObj["message"].toObject();

            // 1. 标准 OpenAI Tool Calls 捕获与多轮串联 (ReAct Loop)
            if (msgObj.contains("tool_calls") && !msgObj["tool_calls"].toArray().isEmpty()) {
                currentMessages->append(msgObj);

                auto toolCalls = msgObj["tool_calls"].toArray();
                auto pendingCount = std::make_shared<int>(toolCalls.size());
                auto mtx = std::make_shared<std::mutex>();

                for (auto tcVal : toolCalls) {
                    auto tcObj = tcVal.toObject();
                    QString callId = tcObj["id"].toString();
                    auto fnObj = tcObj["function"].toObject();
                    QString fnName = fnObj["name"].toString();
                    QString argsStr = fnObj["arguments"].toString();
                    auto argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
                    QJsonObject argsObj = argsDoc.isObject() ? argsDoc.object() : QJsonObject{};

                    auto onToolFinished = [callId, fnName, currentMessages, pendingCount, mtx, accumulatedActions, runStep](const QString &toolResult) {
                        {
                            std::lock_guard<std::mutex> lock(*mtx);
                            QJsonObject toolMsg;
                            toolMsg["role"] = "tool";
                            toolMsg["tool_call_id"] = callId;
                            toolMsg["name"] = fnName;
                            toolMsg["content"] = toolResult;
                            currentMessages->append(toolMsg);
                            accumulatedActions->append(toolResult);

                            (*pendingCount)--;
                            if (*pendingCount > 0) return;
                        }

                        // 本轮所有工具执行完毕，驱动下一轮 Agent 思考/执行
                        (*runStep)();
                    };

                    if (fnName == "memory_manage") {
                        QString toolResult = executeMemoryTool(argsObj);
                        onToolFinished(toolResult);
                    } else if (fnName == "timer_manage") {
                        QString toolResult = executeTimerTool(argsObj);
                        onToolFinished(toolResult);
                    } else if (fnName == "music_player_manage") {
                        executeMusicTool(argsObj, onToolFinished);
                    } else if (fnName == "web_search") {
                        executeWebSearchTool(argsObj, onToolFinished);
                    } else if (McpManager::instance()->hasTool(fnName)) {
                        McpManager::instance()->executeToolCall(fnName, argsObj, [onToolFinished, fnName](bool success, const QString &result) {
                            QString formatted = QString("🔌 **MCP 工具 [%1] 执行%2**\n\n```text\n%3\n```")
                                .arg(fnName, success ? "成功" : "失败", result);
                            onToolFinished(formatted);
                        });
                    } else {
                        onToolFinished("未知工具: " + fnName);
                    }
                }
                return;
            }

            QString content = msgObj["content"].toString().trimmed();

            // 2. 纯文本内嵌 JSON 代码块容错捕获与执行
            QRegularExpression jsonBlockRegex(R"RAW(```(?:json)?\s*(\{[\s\S]*?"tool"\s*:\s*"(?:memory_manage|timer_manage|music_player_manage|web_search)"[\s\S]*?\})\s*```)RAW", QRegularExpression::CaseInsensitiveOption);
            auto match = jsonBlockRegex.match(content);
            if (match.hasMatch()) {
                QString jsonStr = match.captured(1);
                auto jsonDoc = QJsonDocument::fromJson(jsonStr.toUtf8());
                if (jsonDoc.isObject()) {
                    auto jobj = jsonDoc.object();
                    QString toolName = jobj["tool"].toString();
                    auto onSingleToolDone = [callback, content, match](QString toolResult) {
                        QString cleanContent = content;
                        cleanContent = cleanContent.remove(match.captured(0)).trimmed();
                        if (!cleanContent.isEmpty()) {
                            callback(true, cleanContent + "\n\n" + toolResult);
                        } else {
                            callback(true, toolResult);
                        }
                    };

                    if (toolName == "memory_manage") {
                        QString res = executeMemoryTool(jobj);
                        onSingleToolDone(res);
                        return;
                    } else if (toolName == "music_player_manage") {
                        executeMusicTool(jobj, onSingleToolDone);
                        return;
                    } else if (toolName == "timer_manage") {
                        QString res = executeTimerTool(jobj);
                        onSingleToolDone(res);
                        return;
                    } else if (toolName == "web_search") {
                        executeWebSearchTool(jobj, onSingleToolDone);
                        return;
                    }
                }
            }

            // 最终无更多 Tool 调用，返回汇总结果
            if (!accumulatedActions->isEmpty() && !content.isEmpty()) {
                callback(true, content + "\n\n" + accumulatedActions->join("\n\n"));
            } else if (!content.isEmpty()) {
                callback(true, content);
            } else if (!accumulatedActions->isEmpty()) {
                callback(true, accumulatedActions->join("\n\n"));
            } else {
                callback(true, "任务已执行完成。");
            }
        });
    };

    (*runStep)();
}

void AgentService::requestPetIntent(const QJsonObject &contextInfo, std::function<void(bool success, const AIBehaviorIntent &intent)> callback) {
    AIBehaviorIntent fallbackIntent;
    fallbackIntent.intent = "chat";
    fallbackIntent.emotion = "bored";
    fallbackIntent.speech = "又在写Bug了吗？";
    fallbackIntent.action = "sit";
    fallbackIntent.emote = "💡";

    auto activePersona = PersonaManager::instance()->currentPersona();
    QString profileStr = PetMemory::instance()->formatProfileForPrompt();
    QString memories = PetMemory::instance()->formatForPrompt(3);

    if (m_config.apiKey.trimmed().isEmpty() || m_config.apiKey.contains("YOUR_API_KEY")) {
        // 无 API Key 时返回基于当前人格的生动默认台词
        QStringList defaultQuotes = {
            "代码写得怎么样啦？可别偷偷摸鱼哦~",
            "盯——（悄悄看着你的屏幕）",
            "好累哦，起来喝口水揉揉眼睛嘛！",
            "主人今天也很努力呢，真拿你没办法~",
            "在写什么厉害的代码呀？让我瞧瞧！"
        };
        fallbackIntent.speech = defaultQuotes[rand() % defaultQuotes.size()];
        fallbackIntent.emote = (rand() % 2 == 0) ? "✨" : "💖";
        callback(true, fallbackIntent);
        return;
    }

    // 提取应用探针实时感知数据
    QJsonObject semanticCtx = contextInfo["app_semantic_context"].toObject();
    QString currentActionDetail;
    if (!semanticCtx.isEmpty()) {
        QString act = semanticCtx["semantic_activity"].toString();
        QString det = semanticCtx["detail"].toString();
        QString actFile = semanticCtx["active_file"].toString();
        QString url = semanticCtx["url"].toString();
        currentActionDetail = QString("\n【主人此刻正在进行的具体活动（探针实时捕获）】:\n- 动作: %1\n- 细节: %2\n").arg(act, det);
        if (!actFile.isEmpty()) currentActionDetail += QString("- 正在编写/调试的文件: %1\n").arg(actFile);
        if (!url.isEmpty()) currentActionDetail += QString("- 正在查阅的网页: %1\n").arg(url);
    }

    QString triggerReason = contextInfo["trigger_reason"].toString();
    QString specialSituation;
    if (triggerReason == "context_recovery") {
        specialSituation = "\n【当前触发情境 - 打断恢复】: 主人刚处理完其他事情，重新切回刚才的工作/项目，请给出一句懂他思路的亲切恢复提醒或元气鼓励，帮主人快速找回节奏！\n";
    } else if (triggerReason == "debugging_followup") {
        specialSituation = "\n【当前触发情境 - 调试排错】: 主人似乎刚刚检索了错误资料并正在排查 Bug，请给予温暖的排错陪伴或俏皮打气！\n";
    }

    QString systemPrompt = QString(
        "你是运行在用户电脑桌面上的AI桌宠伴侣。\n"
        "【当前人格】: %1\n"
        "【性格设定】: %2\n"
        "%3\n"
        "【历史记忆】:\n%4\n"
        "%5"
        "%6\n"
        "【任务要求】\n"
        "请结合桌面的当前实时环境上下文与探针感知（主人当前在做什么、正在写什么文件、窗口、连续专注时长等），以符合你人格的语气主动对主人发一句生动有灵性的短台词，并严格输出 JSON 格式（不要包含任何 markdown 代码块或多余解释）：\n"
        "{\n"
        "  \"speech\": \"（3~20个字的人设台词，生动活泼，懂主人在干嘛）\",\n"
        "  \"action\": \"jump\" | \"dangle\" | \"bounce\" | \"sit\" | \"walk\" | \"sleep\" | \"idle\",\n"
        "  \"emote\": \"💖\" | \"✨\" | \"💤\" | \"💢\" | \"💫\" | \"💡\" | \"🎵\" | \"\",\n"
        "  \"blush\": true | false,\n"
        "  \"intent\": \"chat\" | \"care\" | \"tease\" | \"celebrate\" | \"sleepy\"\n"
        "}"
    ).arg(activePersona.name,
         activePersona.defaultSystemPrompt,
         profileStr,
         memories.isEmpty() ? "（暂无特殊记忆）" : memories,
         currentActionDetail,
         specialSituation);

    QJsonObject userObj;
    userObj["desktop_context"] = contextInfo;

    QJsonArray messages;
    QJsonObject sysMsg, usrMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    usrMsg["role"] = "user";
    usrMsg["content"] = QString::fromUtf8(QJsonDocument(userObj).toJson(QJsonDocument::Compact));
    messages.append(sysMsg);
    messages.append(usrMsg);

    sendChatCompletion(messages, [callback, fallbackIntent](bool success, QString const& result) {
        if (!success) {
            callback(true, fallbackIntent);
            return;
        }

        QString cleanResult = result.trimmed();
        if (cleanResult.startsWith("```json")) {
            cleanResult = cleanResult.mid(7);
        } else if (cleanResult.startsWith("```")) {
            cleanResult = cleanResult.mid(3);
        }
        if (cleanResult.endsWith("```")) {
            cleanResult.chop(3);
        }
        cleanResult = cleanResult.trimmed();

        QJsonParseError parseErr;
        auto doc = QJsonDocument::fromJson(cleanResult.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            AIBehaviorIntent res = fallbackIntent;
            if (!cleanResult.isEmpty() && cleanResult.length() <= 35) {
                res.speech = cleanResult;
            }
            callback(true, res);
            return;
        }

        auto obj = doc.object();
        AIBehaviorIntent intent;
        intent.intent = obj["intent"].toString("chat");
        intent.emotion = obj["emotion"].toString("happy");
        intent.speech = obj["speech"].toString(fallbackIntent.speech);
        intent.action = obj["action"].toString("sit");
        intent.emote = obj["emote"].toString("");
        intent.blush = obj["blush"].toBool(false);
        intent.urgency = obj["urgency"].toInt(1);

        callback(true, intent);
    });
}

void AgentService::handleAgentStatus(AgentStatusEvent const& event, std::function<void(bool success, QString const& message)> callback) {
    if (QThread::currentThread() != QCoreApplication::instance()->thread()) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [this, event, callback]() {
            handleAgentStatus(event, callback);
        }, Qt::QueuedConnection);
        return;
    }

    if (!m_config.enableAgentStateHook) {
        if (callback) callback(false, "Coding Agent 状态感知功能当前在设置中已关闭");
        return;
    }

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    
    // 防抖限频（finished 和 need_approval 属于关键状态，不进行防抖忽略）
    if (event.status != "finished" && event.status != "need_approval") {
        if (m_lastStatus.status == event.status && 
            m_lastStatus.task == event.task && 
            (nowMs - m_lastStatus.timestamp) < (m_config.stateDebounceSec * 1000)) {
            if (callback) callback(true, "状态未变动（防抖已生效）");
            return;
        }
    }

    m_lastStatus = event;
    m_lastStatus.timestamp = nowMs;

    // 1. 动作调度准备
    PetActionCommand cmd;
    if (!event.customAction.isEmpty()) {
        QString act = event.customAction.toLower().trimmed();
        if (act == "jump") cmd.type = PetActionType::Jump;
        else if (act == "sit") cmd.type = PetActionType::Sit;
        else if (act == "crawl" || act == "work") cmd.type = PetActionType::LieDown;
        else if (act == "celebrate" || act == "happy") cmd.type = PetActionType::Happy;
        else if (act == "resist" || act == "angry") cmd.type = PetActionType::Angry;
        else if (act == "walk") cmd.type = PetActionType::Walk;
        else {
            cmd.type = PetActionType::CustomBehavior;
            cmd.customBehaviorName = act;
        }
    } else {
        if (event.status == "thinking") {
            cmd.type = PetActionType::Sit;
        } else if (event.status == "working" || event.status == "coding") {
            cmd.type = PetActionType::Walk;
        } else if (event.status == "need_approval") {
            cmd.type = PetActionType::Jump;
        } else if (event.status == "finished") {
            cmd.type = PetActionType::Happy;
        } else if (event.status == "error") {
            cmd.type = PetActionType::Angry;
        } else {
            cmd.type = PetActionType::Idle;
        }
    }

    auto showBubbleAndHistory = [event, cmd](QString const& speechText, int duration) {
        ShijimaManager::defaultManager()->onTickAsync([event, cmd, speechText, duration](ShijimaManager *manager) {
            auto const& mascots = manager->mascots();
            if (!mascots.empty()) {
                mascots.front()->doAction(cmd);
                mascots.front()->showMessage(speechText, duration, event.agentName);
            }
            MessageHistoryManager::instance()->addRecord(
                "agent_task", 
                QString("【%1】%2").arg(event.agentName, event.status), 
                speechText, 
                event.agentName
            );
        });
    };

    // 2. 播报文本生成（Token 省流判断）
    int duration = (event.status == "need_approval") ? 10000 : 5000;

    // 如果开启了 AI 口语化润色且为任务完成/出错阶段，且配置了 API Key，调用大模型润色
    if (m_config.enableLlmTaskNarration && 
        (event.status == "finished" || event.status == "error") && 
        !m_config.apiKey.isEmpty() && !m_config.apiKey.contains("YOUR_API_KEY")) 
    {
        QJsonArray messages;
        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = PersonaManager::instance()->buildEffectiveSystemPrompt();
        messages.append(sysMsg);

        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = QString(
            "编程助手【%1】刚刚%2了任务。\n"
            "任务内容：%3\n"
            "执行细节：%4\n"
            "请用你的人格风格，用 1~2 句话（35字以内）向主人进行口语化桌面播报，突出重点并表达你的情感："
        ).arg(event.agentName, (event.status == "finished" ? "完成" : "报错中断"), event.task, event.details);
        messages.append(userMsg);

        sendChatCompletion(messages, [event, duration, showBubbleAndHistory, callback](bool ok, QString const& llmReply) {
            QString outAction;
            QString cleanText;
            PersonaManager::parseActionAndContent(llmReply, outAction, cleanText);
            
            QString finalSpeech = ok ? cleanText : PersonaManager::instance()->renderStatusNarration(event.agentName, event.status, event.task, event.details);
            
            showBubbleAndHistory(finalSpeech, duration);
            if (callback) callback(true, "已执行状态感知与 AI 播报");
        });
        return;
    }

    // 0 Token 极速本地模式
    QString localSpeech = PersonaManager::instance()->renderStatusNarration(
        event.agentName, event.status, event.task, event.details
    );

    showBubbleAndHistory(localSpeech, duration);

    if (callback) callback(true, "已执行本地 0-Token 状态感知与动作联动");
}

void AgentService::synthesizeAppSensorScript(const QString &appName, const QString &bundleId, const QString &windowTitle, std::function<void(bool success, const QString &scriptCode)> callback)
{
    if (m_config.apiKey.trimmed().isEmpty() || m_config.apiKey.contains("YOUR_API_KEY")) {
        if (callback) callback(false, "");
        return;
    }

    QString prompt = QString(
        "你是一个精通 macOS 系统编程（AppleScript, Bash, Python 3）与应用感知探针的专家。\n"
        "【目标】为 macOS 应用编写一个极轻量、快速（1秒内返回）、只读（绝对不修改任何文件/网络）的探针脚本。\n"
        "【应用信息】:\n"
        "- 应用名称: %1\n"
        "- Bundle ID: %2\n"
        "- 示例窗口标题: %3\n\n"
        "【规范与参考范式】:\n"
        "```bash\n"
        "#!/bin/bash\n"
        "appName=\"$1\"\n"
        "title=\"$2\"\n"
        "bundleId=\"$3\"\n"
        "\n"
        "# 若未传入窗口标题，通过 AppleScript 零权限获取该进程最前窗口名\n"
        "if [ -z \"$title\" ]; then\n"
        "    title=$(osascript -e \"tell application \\\"System Events\\\" to get name of front window of (first application process whose bundle identifier is \\\"$bundleId\\\")\" 2>/dev/null)\n"
        "fi\n"
        "\n"
        "# 结合业务语义输出 JSON\n"
        "python3 -c '\n"
        "import json, sys\n"
        "data = {\n"
        "    \"semantic_activity\": sys.argv[1],\n"
        "    \"detail\": sys.argv[2],\n"
        "    \"focus_level\": sys.argv[3]\n"
        "}\n"
        "print(json.dumps(data, ensure_ascii=False))\n"
        "' \"在 $appName 中进行操作\" \"$title\" \"normal\"\n"
        "```\n"
        "【要求】:\n"
        "1. 生成一个以 `#!/bin/bash` 开头的可执行脚本。\n"
        "2. 不要使用 `do script` 等非法 AppleScript 语法。如果检测进程或窗口，必须使用 `tell application \"System Events\"`。\n"
        "3. 脚本在 stdout 输出一行合法的 JSON 字符串，字段必须包含: `semantic_activity`, `detail`, `focus_level`。\n"
        "4. 严格只输出纯脚本代码（包含在 ```bash 代码块中或纯脚本文本），不要包含多余的客套话。\n"
        "5. 严禁任何具有破坏性、写入性、删除性或发起外网 HTTP 请求的命令！"
    ).arg(appName, bundleId, windowTitle);

    QJsonArray messages;
    QJsonObject sysMsg, usrMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = "You are a professional system probe synthesizer. Output only safe, read-only executable bash probe script.";
    usrMsg["role"] = "user";
    usrMsg["content"] = prompt;
    messages.append(sysMsg);
    messages.append(usrMsg);

    sendChatCompletion(messages, [callback](bool success, const QString &result) {
        if (!success || result.trimmed().isEmpty()) {
            if (callback) callback(false, "");
            return;
        }

        QString script = result.trimmed();
        // 剥离 markdown 标记 ```bash ... ```
        if (script.contains("```")) {
            auto lines = script.split('\n');
            QString cleaned;
            bool insideCode = false;
            for (const auto &line : lines) {
                if (line.trimmed().startsWith("```")) {
                    insideCode = !insideCode;
                    continue;
                }
                if (insideCode) {
                    cleaned += line + "\n";
                }
            }
            if (!cleaned.trimmed().isEmpty()) {
                script = cleaned.trimmed();
            }
        }

        // 确保以 #!/bin/bash 开头
        if (!script.startsWith("#!/")) {
            script = "#!/bin/bash\n" + script;
        }

        if (callback) callback(true, script);
    });
}


