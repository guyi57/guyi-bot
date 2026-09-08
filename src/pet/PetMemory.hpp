#pragma once

#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>

struct MemoryItem {
    QString id;
    QString type;       // "preference", "event", "fact"
    QString content;
    int importance = 1; // 1~5
    qint64 createdAt = 0;
};

struct UserProfile {
    QString name = "主人";             // 称呼/昵称
    QString occupation = "开发工程师";   // 职业身份
    QString preferredLangs = "C++, Qt"; // 常用语言与工具
    QString musicTaste = "华语流行, 轻音乐"; // 音乐喜好
    QString workHabits = "经常高强度专注, 偶尔熬夜"; // 作息习惯
    QString notes = "";                // 附加备忘
};

class PetMemory
{
public:
    static PetMemory *instance();

    void load(const QString &filePath = "memory.json");
    void save(const QString &filePath = "memory.json");

    void addMemory(const QString &type, const QString &content, int importance = 1);
    QList<MemoryItem> getTopMemories(int limit = 5);
    QString formatForPrompt(int limit = 5);

    // 主人专属画像
    UserProfile userProfile();
    void updateUserProfile(const UserProfile &profile);
    void updateProfileAttribute(const QString &key, const QString &val);
    QString formatProfileForPrompt();

    // 智能提取与分析对话中的个人特征
    void autoLearnFromChat(const QString &userMsg, const QString &assistantMsg);

private:
    PetMemory();
    QMutex m_mutex;
    UserProfile m_profile;
};
