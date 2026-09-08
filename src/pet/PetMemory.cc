#include "PetMemory.hpp"
#include "LongTermMemoryEngine.hpp"
#include <QRegularExpression>
#include <QMutexLocker>

PetMemory *PetMemory::instance()
{
    static PetMemory s_instance;
    return &s_instance;
}

PetMemory::PetMemory()
{
    load();
}

void PetMemory::load(const QString &/* filePath */)
{
    // LongTermMemoryEngine 会自动初始化并迁移历史数据
    auto core = LongTermMemoryEngine::instance()->coreProfile();
    QMutexLocker locker(&m_mutex);
    m_profile.name = core.name;
    m_profile.occupation = core.occupation;
    m_profile.preferredLangs = core.preferredLangs;
    m_profile.musicTaste = core.musicTaste;
    m_profile.workHabits = core.workHabits;
    m_profile.notes = core.notes;
}

void PetMemory::save(const QString &/* filePath */)
{
    QMutexLocker locker(&m_mutex);
    CoreUserProfile core;
    core.name = m_profile.name;
    core.occupation = m_profile.occupation;
    core.preferredLangs = m_profile.preferredLangs;
    core.musicTaste = m_profile.musicTaste;
    core.workHabits = m_profile.workHabits;
    core.notes = m_profile.notes;
    LongTermMemoryEngine::instance()->updateCoreProfile(core);
}

void PetMemory::addMemory(const QString &type, const QString &content, int importance)
{
    LongTermMemoryEngine::instance()->addSemanticMemory(type, content, importance);
}

QList<MemoryItem> PetMemory::getTopMemories(int limit)
{
    auto records = LongTermMemoryEngine::instance()->getAllActiveMemories();
    QList<MemoryItem> res;
    int n = std::min(limit, (int)records.size());
    for (int i = 0; i < n; ++i) {
        MemoryItem item;
        item.id = records[i].id;
        item.type = records[i].category;
        item.content = records[i].content;
        item.importance = records[i].importance;
        item.createdAt = records[i].createdAt;
        res.append(item);
    }
    return res;
}

QString PetMemory::formatForPrompt(int limit)
{
    auto topList = getTopMemories(limit);
    if (topList.isEmpty()) {
        return "";
    }

    QStringList lines;
    for (const auto &item : topList) {
        lines << QString("- %1").arg(item.content);
    }
    return lines.join("\n");
}

UserProfile PetMemory::userProfile()
{
    auto core = LongTermMemoryEngine::instance()->coreProfile();
    QMutexLocker locker(&m_mutex);
    m_profile.name = core.name;
    m_profile.occupation = core.occupation;
    m_profile.preferredLangs = core.preferredLangs;
    m_profile.musicTaste = core.musicTaste;
    m_profile.workHabits = core.workHabits;
    m_profile.notes = core.notes;
    return m_profile;
}

void PetMemory::updateUserProfile(const UserProfile &profile)
{
    {
        QMutexLocker locker(&m_mutex);
        m_profile = profile;
    }
    save();
}

void PetMemory::updateProfileAttribute(const QString &key, const QString &val)
{
    if (val.trimmed().isEmpty()) return;
    {
        QMutexLocker locker(&m_mutex);
        if (key == "name") m_profile.name = val.trimmed();
        else if (key == "occupation") m_profile.occupation = val.trimmed();
        else if (key == "preferred_langs") m_profile.preferredLangs = val.trimmed();
        else if (key == "music_taste") m_profile.musicTaste = val.trimmed();
        else if (key == "work_habits") m_profile.workHabits = val.trimmed();
        else if (key == "notes") m_profile.notes = val.trimmed();
    }
    LongTermMemoryEngine::instance()->updateProfileAttribute(key, val);
}

QString PetMemory::formatProfileForPrompt()
{
    return LongTermMemoryEngine::instance()->formatProfileForPrompt();
}

void PetMemory::autoLearnFromChat(const QString &userMsg, const QString &assistantMsg)
{
    (void)assistantMsg;
    QString lower = userMsg.toLower();

    // 本地即时正则打底：零延迟提炼显式称谓与偏好
    if (lower.contains("叫我") || lower.contains("我的名字是") || lower.contains("我是")) {
        QRegularExpression re("(?:叫我|名字是|我是)([\u4e00-\u9fa5a-zA-Z0-9_]{2,10})");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString name = match.captured(1);
            if (name != "谁" && name != "什么" && name != "一个" && name != "这个") {
                updateProfileAttribute("name", name);
                addMemory("preference", QString("主人的称呼/名字是「%1」").arg(name), 3);
            }
        }
    }

    if (lower.contains("喜欢听") || lower.contains("爱听")) {
        QRegularExpression re("(?:喜欢听|爱听)([\u4e00-\u9fa5a-zA-Z0-9_]{2,12})");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString song = match.captured(1);
            addMemory("preference", QString("主人喜欢听「%1」").arg(song), 2);
        }
    }

    if (lower.contains("我是写") || lower.contains("我的技术栈") || lower.contains("我用")) {
        if (lower.contains("c++") || lower.contains("cpp")) updateProfileAttribute("preferred_langs", "C++, Qt");
        else if (lower.contains("python")) updateProfileAttribute("preferred_langs", "Python, AI");
        else if (lower.contains("rust")) updateProfileAttribute("preferred_langs", "Rust");
        else if (lower.contains("golang") || lower.contains("go语言")) updateProfileAttribute("preferred_langs", "Go, 后端");
    }
}
