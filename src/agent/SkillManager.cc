#include "SkillManager.hpp"
#include "SettingsDb.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QFileSystemWatcher>
#include <QDebug>
#include <iostream>

SkillManager* SkillManager::instance() {
    static SkillManager s_instance;
    return &s_instance;
}

SkillManager::SkillManager() {
    m_skillsDir = QDir::homePath() + "/.config/guyi-bot/skills";
    init();
}

QString SkillManager::skillsDirectory() const {
    return m_skillsDir;
}

void SkillManager::init() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QDir dir(m_skillsDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    ensureDefaultSkills();
    scanSkills();

    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher();
        m_watcher->addPath(m_skillsDir);
        QObject::connect(m_watcher, &QFileSystemWatcher::directoryChanged, [this](const QString &) {
            std::cout << "[SkillManager] 检测到 Skills 目录变动，自动重新扫描..." << std::endl;
            scanSkills();
        });
    }
}

void SkillManager::ensureDefaultSkills() {
    // 默认技能 1: Git Commit 规范生成器
    QString gitSkillDir = m_skillsDir + "/git-committer";
    QDir().mkpath(gitSkillDir);
    QString gitSkillFile = gitSkillDir + "/SKILL.md";
    if (!QFile::exists(gitSkillFile)) {
        QFile file(gitSkillFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "---\n"
                << "name: git-committer\n"
                << "display_name: Git Commit 专家\n"
                << "description: 自动根据变更内容生成符合 Conventional Commits 规范的清晰提交信息\n"
                << "author: guyi\n"
                << "---\n\n"
                << "## 角色定义\n"
                << "你是一个 Git 提交信息规范专家。当用户要求编写或整理 Git commit message 时，始终遵循 Conventional Commits 规范。\n\n"
                << "## 格式要求\n"
                << "- 类型: feat, fix, docs, style, refactor, perf, test, chore, build, ci\n"
                << "- 格式: `<type>(<scope>): <subject>`\n"
                << "- 使用动词现在时态，首字母小写，结尾不加句号\n"
                << "- 提供简要的中文或英文摘要，并在必要时列出具体要点。\n";
        }
    }

    // 默认技能 2: 代码审查助手
    QString reviewSkillDir = m_skillsDir + "/code-reviewer";
    QDir().mkpath(reviewSkillDir);
    QString reviewSkillFile = reviewSkillDir + "/SKILL.md";
    if (!QFile::exists(reviewSkillFile)) {
        QFile file(reviewSkillFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "---\n"
                << "name: code-reviewer\n"
                << "display_name: 代码审查与优化助手\n"
                << "description: 从安全性、健壮性、时间空间复杂度及重构角度分析代码\n"
                << "author: guyi\n"
                << "---\n\n"
                << "## 角色定义\n"
                << "你是一位资深全栈工程师与代码架构师。当用户提供代码片段或要求审查时：\n"
                << "1. 快速指出潜在的内存泄漏、并发竞争、越界、空指针等关键 Bug；\n"
                << "2. 分析时间与空间复杂度；\n"
                << "3. 给出优雅简洁的重构优化建议与对比示例代码。\n";
        }
    }

    // 默认技能 3: 多语言精译助手
    QString transSkillDir = m_skillsDir + "/translator-pro";
    QDir().mkpath(transSkillDir);
    QString transSkillFile = transSkillDir + "/SKILL.md";
    if (!QFile::exists(transSkillFile)) {
        QFile file(transSkillFile);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "---\n"
                << "name: translator-pro\n"
                << "display_name: 精准多语言翻译\n"
                << "description: 提供符合母语习惯的专业技术术语信达雅翻译\n"
                << "author: guyi\n"
                << "---\n\n"
                << "## 角色定义\n"
                << "你是一位专业的计算机与技术本地化翻译专家。遇到翻译任务时：\n"
                << "- 准确保留代码片段、变量名、专有名词；\n"
                << "- 语言通顺自然，符合中文/英文母语技术人员交流习惯；\n"
                << "- 必要时在文末附上重点词汇对比表。\n";
        }
    }
}

void SkillManager::scanSkills() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_skills.clear();

    QDir rootDir(m_skillsDir);
    if (!rootDir.exists()) return;

    QFileInfoList subDirs = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &subDirInfo : subDirs) {
        QString skillFile = subDirInfo.absoluteFilePath() + "/SKILL.md";
        if (QFile::exists(skillFile)) {
            parseSkillFile(skillFile, subDirInfo.fileName());
        }
    }
    syncSkillsToDshDirs();
    std::cout << "[SkillManager] 扫描完成，共加载 " << m_skills.size() << " 个技能并同步至 dsh" << std::endl;
}

void SkillManager::syncSkillsToDshDirs() {
    QString home = QDir::homePath();
    QString dshSkillsDir = home + "/.dsh/skills";
    QString agentsSkillsDir = home + "/.agents/skills";
    QDir().mkpath(dshSkillsDir);
    QDir().mkpath(agentsSkillsDir);

    for (const auto &skill : m_skills) {
        if (!skill.enabled) continue;
        QString skillDir = QFileInfo(skill.path).absolutePath();
        QString kebabId = skill.name;
        if (kebabId.isEmpty()) kebabId = skill.id.toLower().replace('_', '-');
        if (kebabId == "aipy-computer-use-macos") kebabId = "aipy-computer-use";

        QString targetDsh = dshSkillsDir + "/" + kebabId;
        QString targetAgents = agentsSkillsDir + "/" + kebabId;

        if (QFileInfo::exists(targetDsh) || QFileInfo(targetDsh).isSymLink()) {
            QFile::remove(targetDsh);
        }
        if (QFileInfo::exists(targetAgents) || QFileInfo(targetAgents).isSymLink()) {
            QFile::remove(targetAgents);
        }

        QFile::link(skillDir, targetDsh);
        QFile::link(skillDir, targetAgents);
    }
}

void SkillManager::parseSkillFile(const QString &filePath, const QString &folderName) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    SkillInfo skill;
    skill.id = folderName;
    skill.name = folderName.toLower().replace('_', '-');
    skill.displayName = folderName;
    skill.description = "";
    skill.author = "community";
    skill.path = filePath;

    // 解析 Frontmatter (--- ... ---)
    static QRegularExpression fmRegex("^---\\s*\\n(.*?)\\n---\\s*\\n(.*)$", QRegularExpression::DotMatchesEverythingOption);
    auto match = fmRegex.match(content);
    if (match.hasMatch()) {
        QString frontmatter = match.captured(1);
        skill.prompt = match.captured(2).trimmed();

        QStringList lines = frontmatter.split('\n');
        for (const QString &line : lines) {
            int colonIdx = line.indexOf(':');
            if (colonIdx != -1) {
                QString key = line.left(colonIdx).trimmed().toLower();
                QString val = line.mid(colonIdx + 1).trimmed();
                if (key == "name") skill.name = val;
                else if (key == "display_name") skill.displayName = val;
                else if (key == "description") skill.description = val;
                else if (key == "author") skill.author = val;
            }
        }
    } else {
        skill.prompt = content.trimmed();
    }

    // 校验并确保 skill.name 始终符合 dsh 的 kebab-case 规则
    static const QRegularExpression kebabRe(R"(^[a-z0-9]+(?:-[a-z0-9]+)*$)");
    if (!kebabRe.match(skill.name).hasMatch()) {
        if (skill.displayName.isEmpty() || skill.displayName == folderName) {
            skill.displayName = skill.name;
        }
        QString kebabFolder = folderName.toLower().replace('_', '-');
        if (kebabFolder == "aipy-computer-use-macos") kebabFolder = "aipy-computer-use";
        skill.name = kebabFolder;
    }
    if (skill.displayName.isEmpty()) {
        skill.displayName = skill.name;
    }

    // 从 SettingsDb 加载启用状态，默认 true
    QString enabledStr = SettingsDb::instance()->get("skill.enabled." + skill.id, "true");
    skill.enabled = (enabledStr != "false" && enabledStr != "0");

    m_skills[skill.id] = skill;
}

QList<SkillInfo> SkillManager::getAllSkills() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_skills.values();
}

SkillInfo SkillManager::getSkill(const QString &id, bool *found) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_skills.contains(id)) {
        if (found) *found = true;
        return m_skills[id];
    }
    if (found) *found = false;
    return SkillInfo{};
}

void SkillManager::setSkillEnabled(const QString &id, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_skills.contains(id)) {
        m_skills[id].enabled = enabled;
        SettingsDb::instance()->set("skill.enabled." + id, enabled ? "true" : "false");
    }
}

QString SkillManager::getSystemPromptExtensions() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QString extensions;
    for (const auto &skill : m_skills) {
        if (skill.enabled && !skill.prompt.isEmpty()) {
            extensions += QString("\n\n### 扩展技能模块: %1\n%2").arg(skill.name, skill.prompt);
        }
    }
    return extensions;
}

QStringList SkillManager::getEnabledSkillDirectories() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QStringList dirs;
    for (const auto &skill : m_skills) {
        if (skill.enabled) {
            QString skillDir = QFileInfo(skill.path).absolutePath();
            if (!dirs.contains(skillDir)) {
                dirs.append(skillDir);
            }
        }
    }
    return dirs;
}

QString SkillManager::getDshSkillsInstruction() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    QStringList activeSkills;
    for (const auto &skill : m_skills) {
        if (!skill.enabled) continue;
        QString skillDir = QFileInfo(skill.path).absolutePath();
        QString desc = skill.description.isEmpty() ? skill.displayName : skill.description;
        QString kebabId = skill.name;
        if (kebabId.isEmpty()) kebabId = skill.id.toLower().replace('_', '-');
        if (kebabId == "aipy-computer-use-macos") kebabId = "aipy-computer-use";

        // 探测该技能目录下是否有专属可执行脚本或入口 (如 aipy-computer-use, run.sh 等)
        QString entryNotice;
        QFileInfo wrapperFi(skillDir + "/" + skill.id);
        if (!wrapperFi.exists()) wrapperFi.setFile(skillDir + "/aipy-computer-use");
        if (!wrapperFi.exists()) wrapperFi.setFile(skillDir + "/run.sh");
        if (!wrapperFi.exists()) wrapperFi.setFile(skillDir + "/main.py");
        if (wrapperFi.exists()) {
            entryNotice = QString("- 核心可执行 CLI 工具: `%1`\n"
                                  "- CLI 调用指令格式: `\"%1\" call <tool> --args '<json>'`\n").arg(wrapperFi.absoluteFilePath());
        }

        QString item = QString(
            "#### 技能: %1 (技能标识/工具参数 name: `%2`)\n"
            "- 适用场景: %3\n"
            "- 技能根目录: `%4`\n"
            "%5"
            "- 调度规范: 遇到匹配任务时，【第一步调用内置 skill(name=\"%2\") 工具加载该技能契约】；技能路径与可执行工具已如上明确给出，【严禁使用 find/grep 进行全盘文件扫描】，应直接通过 bash 工具调用核心 CLI 命令或执行脚本完成操作。\n"
        ).arg(skill.displayName.isEmpty() ? skill.name : skill.displayName, kebabId, desc, skillDir, entryNotice);

        activeSkills.append(item);
    }

    if (activeSkills.isEmpty()) return "";

    return QString(
        "\n\n【本机已挂载并注册的扩展技能库 (Skills & Toolkits)】:\n"
        "系统已在当前会话注册了以下扩展技能（已挂载至 `<available_skills>` 列表，并已赋予沙盒读写权限）：\n\n"
        "%1\n"
        "【重要技能调度铁律】: 当用户任务涉及上述技能的职能（尤其是 macOS 桌面操作、软件自动化、微信/日历/提醒事项等各类 GUI 软件交互、代码规范、多语言翻译等）时，**你必须在第一步调用内置 `skill` 工具加载对应技能（如 `skill(name=\"aipy-computer-use\")`）**，切勿跳过技能或直接盲目猜测！\n"
    ).arg(activeSkills.join("\n"));
}
