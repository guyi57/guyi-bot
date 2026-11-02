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
    m_profile.favoriteFood = core.favoriteFood;
    m_profile.birthday = core.birthday;
    m_profile.currentProject = core.currentProject;
    m_profile.hobbies = core.hobbies;
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
    core.favoriteFood = m_profile.favoriteFood;
    core.birthday = m_profile.birthday;
    core.currentProject = m_profile.currentProject;
    core.hobbies = m_profile.hobbies;
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
    m_profile.favoriteFood = core.favoriteFood;
    m_profile.birthday = core.birthday;
    m_profile.currentProject = core.currentProject;
    m_profile.hobbies = core.hobbies;
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
        else if (key == "favorite_food") m_profile.favoriteFood = val.trimmed();
        else if (key == "birthday") m_profile.birthday = val.trimmed();
        else if (key == "current_project") m_profile.currentProject = val.trimmed();
        else if (key == "hobbies") m_profile.hobbies = val.trimmed();
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

    // 1. 称谓/姓名提炼
    if (lower.contains("叫我") || lower.contains("我的名字是") || lower.contains("我是") || lower.contains("你可以称呼我")) {
        QRegularExpression re("(?:叫我|名字是|我是|你可以称呼我)([\u4e00-\u9fa5a-zA-Z0-9_]{2,10})");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString name = match.captured(1);
            if (name != "谁" && name != "什么" && name != "一个" && name != "这个" && name != "在写" && name != "在看") {
                updateProfileAttribute("name", name);
                addMemory("identity", QString("主人的称呼/名字是「%1」").arg(name), 3);
            }
        }
    }

    // 2. 音乐偏好
    if (lower.contains("喜欢听") || lower.contains("爱听") || lower.contains("常听")) {
        QRegularExpression re("(?:喜欢听|爱听|常听)([\u4e00-\u9fa5a-zA-Z0-9_]{2,14})");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString song = match.captured(1);
            updateProfileAttribute("music_taste", song);
            addMemory("preference", QString("主人喜欢听「%1」").arg(song), 2);
        }
    }

    // 3. 饮食喜好与忌口
    if (lower.contains("喜欢吃") || lower.contains("爱吃") || lower.contains("最爱吃")) {
        QRegularExpression re("(?:喜欢吃|爱吃|最爱吃)([\u4e00-\u9fa5a-zA-Z0-9_]{2,14})");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString food = match.captured(1);
            updateProfileAttribute("favorite_food", food);
            addMemory("preference", QString("主人喜欢吃「%1」").arg(food), 3);
        }
    }
    if (lower.contains("不吃") || lower.contains("忌口") || lower.contains("讨厌吃") || lower.contains("不碰")) {
        QRegularExpression re("(?:不吃|忌口|讨厌吃|不碰)([\u4e00-\u9fa5a-zA-Z0-9_]{2,12})");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString taboo = match.captured(1);
            addMemory("preference", QString("主人饮食忌口/不吃「%1」").arg(taboo), 3);
        }
    }

    // 4. 生日与星座
    if (lower.contains("生日") || lower.contains("座")) {
        QRegularExpression re("(?:生日是|生日在|我是)([0-9]{1,2}月[0-9]{1,2}[日号]?|[\u4e00-\u9fa5]{2,3}座)");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString bday = match.captured(1);
            updateProfileAttribute("birthday", bday);
            addMemory("identity", QString("主人的生日/星座是「%1」").arg(bday), 3);
        }
    }

    // 5. 闲暇爱好
    if (lower.contains("平时喜欢") || lower.contains("周末我") || lower.contains("爱玩") || lower.contains("爱好是")) {
        QRegularExpression re("(?:平时喜欢|周末我|爱玩|爱好是)([\u4e00-\u9fa5a-zA-Z0-9_]{2,15})");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString hobby = match.captured(1);
            updateProfileAttribute("hobbies", hobby);
            addMemory("habit", QString("主人的爱好包括「%1」").arg(hobby), 2);
        }
    }

    // 6. 当前项目
    if (lower.contains("最近在做") || lower.contains("正在写") || lower.contains("手头的项目")) {
        QRegularExpression re("(?:最近在做|正在写|手头的项目是?)([\u4e00-\u9fa5a-zA-Z0-9_]{2,18})");
        auto match = re.match(userMsg);
        if (match.hasMatch()) {
            QString proj = match.captured(1);
            updateProfileAttribute("current_project", proj);
            addMemory("project", QString("主人最近在攻坚推进的项目是「%1」").arg(proj), 3);
        }
    }

    // 7. 技术栈
    if (lower.contains("我是写") || lower.contains("我的技术栈") || lower.contains("我用")) {
        if (lower.contains("c++") || lower.contains("cpp")) updateProfileAttribute("preferred_langs", "C++, Qt");
        else if (lower.contains("python")) updateProfileAttribute("preferred_langs", "Python, AI");
        else if (lower.contains("rust")) updateProfileAttribute("preferred_langs", "Rust");
        else if (lower.contains("golang") || lower.contains("go语言")) updateProfileAttribute("preferred_langs", "Go, 后端");
    }
}
