#pragma once

#include <QString>
#include <QList>
#include <QJsonObject>
#include <QDateTime>
#include <functional>
#include <QMutex>

struct DiaryEntry {
    QString id;
    QString date;           // e.g. "2026-09-03"
    QString title;          // 日记标题
    QString content;        // 桌宠第一人称日记正文
    QString mood;           // "happy", "proud", "sleepy", "caring", "curious"
    int workMinutes = 0;    // 主人今日工作/编码时长
    int musicCount = 0;     // 听歌次数
    int pettingCount = 0;   // 摸头抚摸次数
    int chatCount = 0;      // 聊天问答次数
    int trashCount = 0;     // 黑洞/丢垃圾次数
    qint64 createdAt = 0;
};

class PetDiaryManager
{
public:
    static PetDiaryManager* instance();

    // 统计记录日内事件
    void recordWorkMinutes(int mins);
    void recordMusicPlayed();
    void recordPetting();
    void recordChat();
    void recordTrashCleaned();

    // 读取与管理日记列表
    QList<DiaryEntry> allEntries();
    DiaryEntry getEntryByDate(const QString &date);

    // 驱动大模型生成日记（若已生成则返回当日日记）
    void generateDailyDiary(bool forceRegenerate, std::function<void(bool success, const DiaryEntry &entry)> callback);

    // 夜间自动写日记检测 (21:00 ~ 23:59 触发一次)
    void checkAutoNightDiary();

private:
    PetDiaryManager();
    void loadFromDb();
    void saveToDb();

    QMutex m_mutex;
    QList<DiaryEntry> m_entries;

    // 当日实时累计统计
    QString m_todayDateStr;
    int m_todayWorkMinutes = 0;
    int m_todayMusicCount = 0;
    int m_todayPettingCount = 0;
    int m_todayChatCount = 0;
    int m_todayTrashCount = 0;
    bool m_todayDiaryGenerated = false;
};
