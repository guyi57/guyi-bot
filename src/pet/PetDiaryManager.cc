#include "PetDiaryManager.hpp"
#include "SettingsDb.hpp"
#include "AgentService.hpp"
#include "PersonaManager.hpp"
#include "PetMemory.hpp"
#include "BehaviorEngine.hpp"
#include "ShijimaWidget.hpp"
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

void PetDiaryManager::generateDailyDiary(bool forceRegenerate, std::function<void(bool success, const DiaryEntry &entry)> callback)
{
    QString today = QDate::currentDate().toString("yyyy-MM-dd");
    if (!forceRegenerate) {
        DiaryEntry existing = getEntryByDate(today);
        if (!existing.id.isEmpty()) {
            if (callback) callback(true, existing);
            return;
        }
    }

    auto activePersona = PersonaManager::instance()->currentPersona();
    auto profile = PetMemory::instance()->userProfile();

    QString prompt = QString(
        "你是运行在主人电脑桌面上的桌宠伴侣。\n"
        "【当前人格】: %1\n"
        "【人设口吻】: %2\n"
        "【主人画像】: 称呼「%3」, 职业「%4」, 常用「%5」, 作息「%6」\n\n"
        "【今天（%7）主人的数据统计】\n"
        "- 连续敲代码/工作专注时长: 约 %8 分钟\n"
        "- 听歌陪伴: %9 首\n"
        "- 摸头互动: %10 次\n"
        "- 与我对话: %11 次\n"
        "- 清理桌面文件: %12 个\n\n"
        "【任务要求】\n"
        "请以第一人称写一篇 80~150 字的桌宠秘密观察日记，记录今天陪伴主人的点滴、小情绪（可以傲娇、可以感动、可以吐槽主人辛苦）与对明天的期待。\n"
        "请严格输出 JSON 格式（不要输出 markdown 代码块）：\n"
        "{\n"
        "  \"title\": \"（8字以内可爱日记标题）\",\n"
        "  \"mood\": \"happy\" | \"proud\" | \"sleepy\" | \"caring\" | \"playful\",\n"
        "  \"content\": \"（80~150字的日记正文）\"\n"
        "}"
    ).arg(activePersona.name,
         activePersona.defaultSystemPrompt,
         profile.name,
         profile.occupation,
         profile.preferredLangs,
         profile.workHabits,
         today)
     .arg(m_todayWorkMinutes)
     .arg(m_todayMusicCount)
     .arg(m_todayPettingCount)
     .arg(m_todayChatCount)
     .arg(m_todayTrashCount);

    QJsonArray messages;
    QJsonObject sysMsg, usrMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = "你是一位极富情感的二次元桌面宠物伴侣，擅长用细腻生动的视角写日常观察日记。";
    usrMsg["role"] = "user";
    usrMsg["content"] = prompt;
    messages.append(sysMsg);
    messages.append(usrMsg);

    AgentService::instance()->sendChatCompletion(messages, [this, today, callback](bool success, QString const& result) {
        DiaryEntry entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.date = today;
        entry.workMinutes = m_todayWorkMinutes;
        entry.musicCount = m_todayMusicCount;
        entry.pettingCount = m_todayPettingCount;
        entry.chatCount = m_todayChatCount;
        entry.trashCount = m_todayTrashCount;
        entry.createdAt = QDateTime::currentMSecsSinceEpoch();

        if (success) {
            QString clean = result.trimmed();
            if (clean.startsWith("```json")) clean = clean.mid(7);
            else if (clean.startsWith("```")) clean = clean.mid(3);
            if (clean.endsWith("```")) clean.chop(3);
            clean = clean.trimmed();

            auto doc = QJsonDocument::fromJson(clean.toUtf8());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                entry.title = obj["title"].toString("今天也是充实的一天~");
                entry.mood = obj["mood"].toString("happy");
                entry.content = obj["content"].toString();
            }
        }

        // 本地保底生成机制
        if (entry.content.isEmpty()) {
            entry.title = "安静陪伴的一天 🐾";
            entry.mood = m_todayPettingCount > 5 ? "happy" : "caring";
            entry.content = QString(
                "今天主人在电脑前忙了很久呢，敲键盘的声音哒哒作响。我一直蹲在窗口旁边看着，"
                "中途主人摸了我 %1 次脑袋，还放了 %2 首歌。看着主人专注的样子我也觉得很开心，"
                "今晚要早点休息，明天我也继续在桌面上陪着你哦！✨"
            ).arg(m_todayPettingCount).arg(m_todayMusicCount);
        }

        {
            QMutexLocker locker(&m_mutex);
            // 移除同日期的旧日记
            for (int i = 0; i < m_entries.size(); ++i) {
                if (m_entries[i].date == today) {
                    m_entries.removeAt(i);
                    break;
                }
            }
            m_entries.append(entry);
            m_todayDiaryGenerated = true;
        }

        saveToDb();
        if (callback) callback(true, entry);
    });
}

void PetDiaryManager::checkAutoNightDiary()
{
    if (m_todayDiaryGenerated) return;

    int hour = QTime::currentTime().hour();
    bool shouldGenerate = false;

    // 1. 夜间 21:00 之后自动写日记
    if (hour >= 21) {
        shouldGenerate = true;
    }
    // 2. 白天只要累积了一定互动量（摸头>=2 或 听歌>=2 或 对话>=1），也自动触发生成初版日记并提醒主人偷看
    else if (m_todayPettingCount >= 2 || m_todayMusicCount >= 2 || m_todayChatCount >= 1) {
        shouldGenerate = true;
    }

    if (shouldGenerate) {
        generateDailyDiary(false, [](bool success, const DiaryEntry &e) {
            if (success) {
                std::cout << "[PetDiaryManager] 今日私密日记已生成: " << e.title.toStdString() << std::endl;
                ShijimaWidget *target = BehaviorEngine::instance()->activeWidget();
                if (target) {
                    target->showMessage("🐾 悄悄告诉你：今天的私密日记我已经写好一段啦！右键我点「📖 桌宠私密日记」可以偷看哦~ ✨", 6000);
                }
            }
        });
    }
}
