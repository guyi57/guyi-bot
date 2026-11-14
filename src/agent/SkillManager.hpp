#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <mutex>

struct SkillInfo {
    QString id;              // 文件夹名，例如 "git-committer"
    QString name;            // 技能 kebab-case 标识（用于 dsh 工具入参，如 "aipy-computer-use"）
    QString displayName;     // 技能展示名称（如 "Aipy Computer Use" / "Git Commit 专家"）
    QString description;     // 技能简要描述
    QString author;          // 作者
    QString prompt;          // 核心 System Prompt 指令内容
    QString path;            // SKILL.md 绝对路径
    bool enabled = true;     // 是否启用
};

class SkillManager {
public:
    static SkillManager* instance();

    void init();
    void scanSkills();
    QList<SkillInfo> getAllSkills() const;
    SkillInfo getSkill(const QString &id, bool *found = nullptr) const;
    void setSkillEnabled(const QString &id, bool enabled);
    
    // 获取所有已启用 Skill 聚合生成的 System Prompt 补充片段
    QString getSystemPromptExtensions() const;

    // 为 DeepSeek Harness (dsh) 生成结构化技能库指引与工具调用指引
    QString getDshSkillsInstruction() const;

    // 获取所有已启用技能的绝对目录路径
    QStringList getEnabledSkillDirectories() const;

    // 将已启用的技能自动同步并软链接至 dsh 与 agents 规范根目录 (~/.dsh/skills, ~/.agents/skills)
    void syncSkillsToDshDirs();

    // 获取 Skills 存储主目录
    QString skillsDirectory() const;

private:
    SkillManager();
    ~SkillManager() = default;

    void ensureDefaultSkills();
    void parseSkillFile(const QString &filePath, const QString &folderName);

    mutable std::recursive_mutex m_mutex;
    QMap<QString, SkillInfo> m_skills;
    QString m_skillsDir;
    class QFileSystemWatcher *m_watcher = nullptr;
};
