#include "PetDiaryManager.hpp"
#include "SettingsDb.hpp"
#include "AgentService.hpp"
#include "PersonaManager.hpp"
#include "PetMemory.hpp"
#include "BehaviorEngine.hpp"
#include "ShijimaWidget.hpp"
#include "VocabLearningEngine.hpp"
#include "LongTermMemoryEngine.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>
#include <iostream>

PetDiaryManager* PetDiaryManager::instance()
{
    static PetDiaryManager s_instance;
    return &s_instance;
}

PetDiaryManager::PetDiaryManager()
{
    m_todayDateStr = QDate::currentDate().toString("yyyy-MM-dd");
    loadFromDb();
}

void PetDiaryManager::loadFromDb()
{
    QMutexLocker locker(&m_mutex);
    m_entries.clear();
    auto db = SettingsDb::instance();

    if (db->contains("pet.diary_entries")) {
        QJsonArray arr = db->getJsonArray("pet.diary_entries");
        for (const auto &val : arr) {
            QJsonObject obj = val.toObject();
            DiaryEntry entry;
            entry.id = obj["id"].toString();
            entry.date = obj["date"].toString();
            entry.title = obj["title"].toString();
            entry.content = obj["content"].toString();
            entry.mood = obj["mood"].toString("happy");
            entry.workMinutes = obj["work_minutes"].toInt(0);
            entry.musicCount = obj["music_count"].toInt(0);
            entry.pettingCount = obj["petting_count"].toInt(0);
            entry.chatCount = obj["chat_count"].toInt(0);
            entry.trashCount = obj["trash_count"].toInt(0);
            entry.createdAt = obj["created_at"].toVariant().toLongLong();
            m_entries.append(entry);
        }
    }

    // 检查今天是否已经有日记
    for (const auto &e : m_entries) {
        if (e.date == m_todayDateStr) {
            m_todayDiaryGenerated = true;
            break;
        }
    }

    // 读取今日活动累计缓存
    if (db->contains("pet.today_stats")) {
        QJsonObject s = db->getJsonObject("pet.today_stats");
        if (s["date"].toString() == m_todayDateStr) {
            m_todayWorkMinutes = s["work_minutes"].toInt(0);
            m_todayMusicCount = s["music_count"].toInt(0);
            m_todayPettingCount = s["petting_count"].toInt(0);
            m_todayChatCount = s["chat_count"].toInt(0);
            m_todayTrashCount = s["trash_count"].toInt(0);
        }
    }

    // 从真实事件数据库同步今日数据基线（防止应用重启后时长归零）
    auto todaySummary = LongTermMemoryEngine::instance()->getDailyActivitySummary(m_todayDateStr);
    int dbActiveMins = todaySummary["active_minutes"].toInt(0);
    if (dbActiveMins > m_todayWorkMinutes) {
        m_todayWorkMinutes = dbActiveMins;
    }
    int dbChats = todaySummary["user_chat_count"].toInt(0);
    if (dbChats > m_todayChatCount) {
        m_todayChatCount = dbChats;
    }

    // 扫描并修复历史上因早期空数据/默认模板生成的空白日记
    repairHistoricalEntries();
}

void PetDiaryManager::repairHistoricalEntries()
{
    bool modified = false;
    for (auto &e : m_entries) {
        if (e.workMinutes == 0) {
            auto summary = LongTermMemoryEngine::instance()->getDailyActivitySummary(e.date);
            int dbMins = summary["active_minutes"].toInt(0);
            int dbChats = summary["user_chat_count"].toInt(0);
            if (dbMins > 0) {
                e.workMinutes = dbMins;
                e.chatCount = std::max(e.chatCount, dbChats);
                modified = true;

                if (e.content.contains("摸了我 0 次脑袋") || e.content.contains("专注时长是零分钟") || e.content.contains("零分钟专注")) {
                    QJsonArray topApps = summary["top_apps"].toArray();
                    QStringList appNames;
                    for (auto v : topApps) {
                        QString name = v.toObject()["app"].toString();
                        if (!name.isEmpty() && name != "guyi-bot") appNames << name;
                    }
                    QString appStr = appNames.isEmpty() ? "各种工作软件" : appNames.join("、");
                    int hrs = e.workMinutes / 60;
                    int mins = e.workMinutes % 60;
                    QString durStr = (hrs > 0) ? QString("%1小时%2分钟").arg(hrs).arg(mins) : QString("%1分钟").arg(mins);

                    e.title = (e.workMinutes > 180) ? "码力全开的一天 🐾" : "充实专注的一天 🐾";
                    e.mood = (e.workMinutes > 180) ? "proud" : "caring";
                    e.content = QString(
                        "今天主人在电脑前专注奋战了整整 %1 呢！主要在 %2 中忙碌处理各项任务与代码。"
                        "键盘敲得啪嗒作响，专注的样子特别帅气。虽然忙忙碌碌，但每一分付出都有痕迹哦！"
                        "今晚一定要早点休息揉揉眼睛，明天本喵继续在桌面上守着你呀~ ✨"
                    ).arg(durStr, appStr);
                }
            }
        }
    }

    if (modified) {
        saveToDb();
    }
}

void PetDiaryManager::saveToDb()
{
    QMutexLocker locker(&m_mutex);
    QJsonArray arr;
    for (const auto &entry : m_entries) {
        QJsonObject obj;
        obj["id"] = entry.id;
        obj["date"] = entry.date;
        obj["title"] = entry.title;
        obj["content"] = entry.content;
        obj["mood"] = entry.mood;
        obj["work_minutes"] = entry.workMinutes;
        obj["music_count"] = entry.musicCount;
        obj["petting_count"] = entry.pettingCount;
        obj["chat_count"] = entry.chatCount;
        obj["trash_count"] = entry.trashCount;
        obj["created_at"] = entry.createdAt;
        arr.append(obj);
    }
    SettingsDb::instance()->setJsonArray("pet.diary_entries", arr);

    QJsonObject s;
    s["date"] = m_todayDateStr;
    s["work_minutes"] = m_todayWorkMinutes;
    s["music_count"] = m_todayMusicCount;
    s["petting_count"] = m_todayPettingCount;
    s["chat_count"] = m_todayChatCount;
    s["trash_count"] = m_todayTrashCount;
    SettingsDb::instance()->setJsonObject("pet.today_stats", s);
}

void PetDiaryManager::recordWorkMinutes(int mins)
{
    if (mins <= 0) return;
    m_todayWorkMinutes += mins;
    saveToDb();
}

void PetDiaryManager::recordMusicPlayed()
{
    m_todayMusicCount++;
    saveToDb();
}

void PetDiaryManager::recordPetting()
{
    m_todayPettingCount++;
    saveToDb();
}

void PetDiaryManager::recordChat()
{
    m_todayChatCount++;
    saveToDb();
}

void PetDiaryManager::recordTrashCleaned()
{
    m_todayTrashCount++;
    saveToDb();
}

QList<DiaryEntry> PetDiaryManager::allEntries()
{
    QMutexLocker locker(&m_mutex);
    QList<DiaryEntry> list = m_entries;
    std::sort(list.begin(), list.end(), [](const DiaryEntry &a, const DiaryEntry &b) {
        return a.date > b.date; // 倒序，最新的在前
    });
    return list;
}

DiaryEntry PetDiaryManager::getEntryByDate(const QString &date)
{
    QMutexLocker locker(&m_mutex);
    for (const auto &e : m_entries) {
        if (e.date == date) return e;
    }
    return DiaryEntry();
}

void PetDiaryManager::generateDailyDiary(bool forceRegenerate, std::function<void(bool success, const DiaryEntry &entry)> callback, const QString &targetDate)
{
    QString today = targetDate.trimmed().isEmpty() ? QDate::currentDate().toString("yyyy-MM-dd") : targetDate.trimmed();
    if (!forceRegenerate) {
        DiaryEntry existing = getEntryByDate(today);
        if (!existing.id.isEmpty() && existing.workMinutes > 0 && !existing.content.contains("摸了我 0 次脑袋")) {
            if (callback) callback(true, existing);
            return;
        }
    }

    auto activePersona = PersonaManager::instance()->currentPersona();
    auto profile = PetMemory::instance()->userProfile();
    auto vocabStats = VocabLearningEngine::instance()->stats();

    auto dbSummary = LongTermMemoryEngine::instance()->getDailyActivitySummary(today);
    int dbMins = dbSummary["active_minutes"].toInt(0);
    int finalWorkMinutes = (today == m_todayDateStr) ? std::max(m_todayWorkMinutes, dbMins) : dbMins;
    int dbChats = dbSummary["user_chat_count"].toInt(0);
    int finalChatCount = (today == m_todayDateStr) ? std::max(m_todayChatCount, dbChats) : dbChats;
    int finalMusicCount = (today == m_todayDateStr) ? m_todayMusicCount : 0;
    int finalPettingCount = (today == m_todayDateStr) ? m_todayPettingCount : 0;
    int finalTrashCount = (today == m_todayDateStr) ? m_todayTrashCount : 0;

    QString vocabInfo;
    if (VocabLearningEngine::instance()->isEnabled()) {
        vocabInfo = QString("- 英语背词学习: 今日伴学复习了 %1 个单词，累计已掌握熟记 %2 个单词\n")
            .arg(vocabStats.todayReviewedCount).arg(vocabStats.masteredCount);
    }

    // 组装前台软件排行
    QJsonArray topAppsArr = dbSummary["top_apps"].toArray();
    QStringList topAppsList;
    for (auto v : topAppsArr) {
        QJsonObject o = v.toObject();
        topAppsList << QString("%1（交互%2次）").arg(o["app"].toString()).arg(o["count"].toInt());
    }
    QString topAppsStr = topAppsList.isEmpty() ? "各种工作软件" : topAppsList.join("、");

    // 组装代码任务
    QJsonArray tasksArr = dbSummary["recent_tasks"].toArray();
    QString tasksSection;
    if (!tasksArr.isEmpty()) {
        tasksSection = "【今日在电脑上推进的代码/智能体工作摘录】\n";
        for (int i = 0; i < std::min<int>(tasksArr.size(), 5); ++i) {
            tasksSection += QString("- %1\n").arg(tasksArr[i].toString().left(70));
        }
        tasksSection += "\n";
    }

    // 组装讨论话题
    QJsonArray topicsArr = dbSummary["recent_topics"].toArray();
    QString topicsSection;
    if (!topicsArr.isEmpty()) {
        topicsSection = "【今日主人向我提问/探讨的话题摘录】\n";
        for (int i = 0; i < std::min<int>(topicsArr.size(), 4); ++i) {
            topicsSection += QString("- 提问: %1\n").arg(topicsArr[i].toString().left(50));
        }
        topicsSection += "\n";
    }

    QString timeRange = "";
    QString fTime = dbSummary["first_active_time"].toString();
    QString lTime = dbSummary["last_active_time"].toString();
    if (!fTime.isEmpty() && !lTime.isEmpty()) {
        timeRange = QString("（今日活跃时段: %1 ~ %2）").arg(fTime, lTime);
    }

    int hrs = finalWorkMinutes / 60;
    int mins = finalWorkMinutes % 60;
    QString durStr = (hrs > 0) ? QString("%1 小时 %2 分钟").arg(hrs).arg(mins) : QString("%1 分钟").arg(mins);

    QString prompt = QString(
        "你是运行在主人电脑桌面上的桌宠伴侣。\n"
        "【当前人格】: %1\n"
        "【人设口吻】: %2\n"
        "【主人画像】: 称呼「%3」, 职业「%4」, 常用「%5」, 作息「%6」\n\n"
        "【今天（%7）主人的真实活动与工作统计】\n"
        "- 连续敲代码/工作专注时长: 约 %8（共 %9 分钟）%10\n"
        "- 今日主要使用的前台工具/软件: %11\n"
        "- 听歌陪伴: %12 首\n"
        "- 摸头互动: %13 次\n"
        "- 与我对话: %14 次\n"
        "%15"
        "%16"
        "%17\n"
        "【任务要求】\n"
        "请以第一人称写一篇 100~180 字的桌宠秘密观察日记，真实记录今天陪伴主人工作的点滴、小情绪（可以傲娇、可以感动、可以心疼主人辛苦）与对明天的期待。\n"
        "必须严格输出 JSON 格式（不要包含任何额外对话解释）：\n"
        "{\n"
        "  \"title\": \"（8字以内可爱日记标题）\",\n"
        "  \"mood\": \"happy\" | \"proud\" | \"sleepy\" | \"caring\" | \"playful\",\n"
        "  \"content\": \"（100~180字的日记正文）\"\n"
        "}"
    ).arg(activePersona.name,
         activePersona.defaultSystemPrompt,
         profile.name,
         profile.occupation,
         profile.preferredLangs,
         profile.workHabits,
         today,
         durStr,
         QString::number(finalWorkMinutes),
         timeRange,
         topAppsStr)
     .arg(finalMusicCount)
     .arg(finalPettingCount)
     .arg(finalChatCount)
     .arg(vocabInfo)
     .arg(tasksSection)
     .arg(topicsSection);

    QJsonArray messages;
    QJsonObject sysMsg, usrMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = "你是一位极富情感的二次元桌面宠物伴侣，擅长用细腻生动的视角写日常观察日记。";
    usrMsg["role"] = "user";
    usrMsg["content"] = prompt;
    messages.append(sysMsg);
    messages.append(usrMsg);

    AgentService::instance()->sendChatCompletion(messages, [this, today, finalWorkMinutes, finalMusicCount, finalPettingCount, finalChatCount, finalTrashCount, durStr, topAppsStr, callback](bool success, QString const& result) {
        DiaryEntry entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.date = today;
        entry.workMinutes = finalWorkMinutes;
        entry.musicCount = finalMusicCount;
        entry.pettingCount = finalPettingCount;
        entry.chatCount = finalChatCount;
        entry.trashCount = finalTrashCount;
        entry.createdAt = QDateTime::currentMSecsSinceEpoch();

        if (success) {
            QString clean = result.trimmed();
            int firstBrace = clean.indexOf('{');
            int lastBrace = clean.lastIndexOf('}');
            if (firstBrace != -1 && lastBrace != -1 && lastBrace > firstBrace) {
                clean = clean.mid(firstBrace, lastBrace - firstBrace + 1);
            }
            auto doc = QJsonDocument::fromJson(clean.toUtf8());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                entry.title = obj["title"].toString("今天也是充实的一天~");
                entry.mood = obj["mood"].toString("happy");
                entry.content = obj["content"].toString();
            }
        }

        // 智能保底生成机制（结合真实数据，绝不出现0次0首的死板空话）
        if (entry.content.isEmpty()) {
            entry.title = (finalWorkMinutes > 180) ? "码力全开的一天 🐾" : "温馨陪伴的一天 🐾";
            entry.mood = (finalWorkMinutes > 180) ? "proud" : ((finalPettingCount > 3) ? "happy" : "caring");
            if (finalWorkMinutes > 0) {
                entry.content = QString(
                    "今天主人在电脑前专注忙碌了整整 %1 呢！主要在 %2 中处理各项代码与事务。"
                    "我一直静静地蹲在屏幕角落守着，看着主人认真的侧脸。今天虽然辛苦，但每一步努力都算数哦！"
                    "今晚记得早点休息揉揉眼睛，明天本喵继续在桌面上陪着你呀~ ✨"
                ).arg(durStr, topAppsStr);
            } else {
                entry.content = QString(
                    "今天主人的电脑屏幕很安静呢，我趴在任务栏上打了个长长的盹。"
                    "虽然今天敲键盘的声音不多，但能这么静静地陪在主人身边也很安心呀。"
                    "今晚好好放松一下，明天又是元气满满的一天啦！🐾"
                );
            }
        }

        {
            QMutexLocker locker(&m_mutex);
            for (int i = 0; i < m_entries.size(); ++i) {
                if (m_entries[i].date == today) {
                    m_entries.removeAt(i);
                    break;
                }
            }
            m_entries.append(entry);
            if (today == m_todayDateStr) {
                m_todayDiaryGenerated = true;
            }
        }

        saveToDb();
        if (callback) callback(true, entry);
    });
}

void PetDiaryManager::checkAutoNightDiary()
{
    int hour = QTime::currentTime().hour();

    // 仅在夜间 21:00 之后自动写日记/收官复盘，杜绝上午 9 点单次闲聊过早抢跑冻结
    if (hour >= 21) {
        if (!m_todayNightFinalized) {
            m_todayNightFinalized = true;
            generateDailyDiary(true /* 强制用全天完整数据重绘收官日记 */, [](bool success, const DiaryEntry &e) {
                if (success) {
                    std::cout << "[PetDiaryManager] 今日收官私密日记已生成: " << e.title.toStdString() << std::endl;
                    ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
                    if (target) {
                        target->showMessage("🐾 呼~ 夜深啦！今天的私密日记我已经为你写好复盘啦，右键我点「📖 桌宠私密日记」可以偷看哦~ ✨", 6000);
                    }
                }
            });
        }
    }
}
