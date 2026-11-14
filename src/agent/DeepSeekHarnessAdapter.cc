// 
// Shijima-Qt - DeepSeek Harness (dsh) Agent Adapter Implementation
// 

#include "DeepSeekHarnessAdapter.hpp"
#include "AgentService.hpp"
#include "SensorManager.hpp"
#include "SkillManager.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QUuid>
#include <iostream>
#if defined(Q_OS_UNIX)
#include <signal.h>
#include <sys/types.h>
#endif

static QString findCompanionNodeDir(const QString &exePath) {
    if (exePath.isEmpty()) return "";

    // 1. 检查 exePath 自身所在目录
    QFileInfo exeFi(exePath);
    if (QFileInfo::exists(exeFi.dir().absoluteFilePath("node"))) {
        return exeFi.dir().absolutePath();
    }

    // 2. 检查 symlinkTarget 所在目录 (第一层软链接，例如 ~/.local/bin/dsh -> ~/.nvm/.../bin/dsh)
    if (exeFi.isSymLink()) {
        QString target = exeFi.symLinkTarget();
        if (!target.startsWith("/")) {
            target = exeFi.dir().absoluteFilePath(target);
        }
        QFileInfo targetFi(target);
        if (QFileInfo::exists(targetFi.dir().absoluteFilePath("node"))) {
            return targetFi.dir().absolutePath();
        }
    }

    // 3. 从 canonicalFilePath (真实物理路径，例如 .../node/v22.22.2/lib/node_modules/.../bin.js) 向上回溯查找包含 bin/node 或 node 的目录
    QString canon = exeFi.canonicalFilePath();
    if (!canon.isEmpty()) {
        QDir curDir = QFileInfo(canon).dir();
        while (!curDir.isRoot()) {
            if (QFileInfo::exists(curDir.absoluteFilePath("bin/node"))) {
                return curDir.absoluteFilePath("bin");
            }
            if (QFileInfo::exists(curDir.absoluteFilePath("node"))) {
                return curDir.absolutePath();
            }
            if (!curDir.cdUp()) break;
        }
    }

    return "";
}

static QProcessEnvironment buildProcessEnvironment(const QString &exePath, const QString &key, const QString &baseUrl, const QString &model) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString currentPath = env.value("PATH");
    QStringList pathList = currentPath.split(":", Qt::SkipEmptyParts);

    // 1. 常见系统 / 包管理器路径注入
    QString home = QDir::homePath();
    // 优先前置注入 Homebrew 与现代环境路径，确保 python3 优先选用现代版本 (如 Python 3.12/3.14 而非系统的 3.9)
    QStringList standardDirs = {
        "/opt/homebrew/bin",
        "/opt/homebrew/sbin",
        home + "/.local/bin",
        home + "/.cargo/bin",
        home + "/.bun/bin",
        home + "/.npm-global/bin",
        "/usr/local/bin",
        "/usr/local/sbin"
    };
    for (int i = standardDirs.size() - 1; i >= 0; --i) {
        const auto &p = standardDirs[i];
        if (QDir(p).exists()) {
            pathList.removeAll(p);
            pathList.prepend(p);
        }
    }

    // 2. 动态探测 nvm 目录并追加至 PATH (降序排列，优先较新版本)
    QDir nvmDir(home + "/.nvm/versions/node");
    if (nvmDir.exists()) {
        for (const auto &ver : nvmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
            QString bin = nvmDir.absoluteFilePath(ver + "/bin");
            if (!pathList.contains(bin)) pathList.append(bin);
        }
    }

    // 3. 关键：将与 dsh 绑定的配套 Node 解释器所在目录 (以及 exeDir) 置于 PATH 最顶端！
    // 杜绝被旧版本 Node (如 v22.14.0) 错误接管导致 silent exit 0 (无输出且退出码0)
    if (!exePath.isEmpty()) {
        QString exeDir = QFileInfo(exePath).absolutePath();
        if (!exeDir.isEmpty()) {
            pathList.removeAll(exeDir);
            pathList.prepend(exeDir);
        }

        QString nodeBinDir = findCompanionNodeDir(exePath);
        if (!nodeBinDir.isEmpty()) {
            pathList.removeAll(nodeBinDir);
            pathList.prepend(nodeBinDir);
        }
    }

    // 4. 将所有已启用的 Skills 目录追加至 PATH，方便 dsh 直接调用技能可执行脚本与自动化工具
    for (const auto &skillDir : SkillManager::instance()->getEnabledSkillDirectories()) {
        if (!pathList.contains(skillDir)) {
            pathList.append(skillDir);
        }
    }

    env.insert("PATH", pathList.join(":"));
    env.insert("DEEPSEEK_API_KEY", key);
    if (!baseUrl.isEmpty()) {
        env.insert("DEEPSEEK_BASE_URL", baseUrl);
        env.insert("DEEPSEEK_API_BASE", baseUrl);
    }
    env.insert("DEEPSEEK_MODEL", model);
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("LANG", "en_US.UTF-8");
    return env;
}

DeepSeekHarnessAdapter::DeepSeekHarnessAdapter()
{
}

DeepSeekHarnessAdapter::~DeepSeekHarnessAdapter() {
    cancelCurrentTask();
}

QString DeepSeekHarnessAdapter::findDshExecutable() {
    // 1. 系统 PATH 环境变量查找
    QString exe = QStandardPaths::findExecutable("dsh");
    if (!exe.isEmpty()) return exe;

    // 2. 常见系统路径探查 (macOS / Linux / Windows)
    QStringList candidates = {
        "/usr/local/bin/dsh",
        "/opt/homebrew/bin/dsh",
        QDir::homePath() + "/.local/bin/dsh",
        QDir::homePath() + "/.cargo/bin/dsh",
        QDir::homePath() + "/.bun/bin/dsh",
        QDir::homePath() + "/.npm-global/bin/dsh",
        QDir::homePath() + "/Library/pnpm/dsh",
        QDir::homePath() + "/Library/pnpm/bin/dsh",
        QDir::homePath() + "/Library/Python/3.9/bin/dsh",
        QDir::homePath() + "/Library/Python/3.10/bin/dsh",
        QDir::homePath() + "/Library/Python/3.11/bin/dsh",
        QDir::homePath() + "/Library/Python/3.12/bin/dsh",
        QDir::homePath() + "/Library/Python/3.13/bin/dsh",
        // 本地打包内置路径
        QCoreApplication::applicationDirPath() + "/dsh",
        QCoreApplication::applicationDirPath() + "/../Resources/bin/dsh",
        QCoreApplication::applicationDirPath() + "/scripts/dsh"
    };

    // 3. 动态探测 NVM Node 版本路径
    QDir nvmDir(QDir::homePath() + "/.nvm/versions/node");
    if (nvmDir.exists()) {
        for (const auto &ver : nvmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
            candidates << nvmDir.absoluteFilePath(ver + "/bin/dsh");
        }
    }

    for (const auto &path : candidates) {
        if (QFileInfo::exists(path) && QFileInfo(path).isExecutable()) {
            return path;
        }
    }

    return "";
}

QString DeepSeekHarnessAdapter::effectiveDshPath() const {
    if (!m_dshPath.isEmpty() && QFileInfo::exists(m_dshPath)) {
        return m_dshPath;
    }
    return findDshExecutable();
}

QString DeepSeekHarnessAdapter::effectiveApiKey() const {
    if (!m_apiKey.isEmpty()) return m_apiKey;

    // 优先使用当前活动的大模型配置 (Active Profile)
    auto profile = AgentService::instance()->getActiveProfile();
    if (!profile.apiKey.trimmed().isEmpty()) {
        return profile.apiKey.trimmed();
    }
    // 从任意已配置的 deepseek profile 中寻找
    for (const auto &p : AgentService::instance()->config().modelProfiles) {
        if (!p.apiKey.trimmed().isEmpty() &&
            (p.id.contains("deepseek", Qt::CaseInsensitive) ||
             p.name.contains("deepseek", Qt::CaseInsensitive) ||
             p.apiBase.contains("deepseek", Qt::CaseInsensitive))) {
            return p.apiKey.trimmed();
        }
    }
    return "";
}

QString DeepSeekHarnessAdapter::effectiveModel() const {
    // 优先直接继承全局当前活动模型
    auto profile = AgentService::instance()->getActiveProfile();
    if (!profile.model.trimmed().isEmpty()) {
        return profile.model.trimmed();
    }
    if (!m_model.isEmpty()) return m_model;
    return "deepseek-chat";
}

QString DeepSeekHarnessAdapter::effectiveBaseUrl() const {
    auto profile = AgentService::instance()->getActiveProfile();
    if (!profile.apiBase.trimmed().isEmpty()) {
        return profile.apiBase.trimmed();
    }
    if (!m_baseUrl.isEmpty()) return m_baseUrl;
    return "https://api.deepseek.com";
}

QString DeepSeekHarnessAdapter::detectActiveWorkspace() const {
    auto ctx = SensorManager::instance()->currentContext();
    QString ws = ctx["workspace"].toString().trimmed();
    QString activeFile = ctx["active_file"].toString().trimmed();

    if (!activeFile.isEmpty()) {
        QFileInfo fi(activeFile);
        if (fi.exists()) {
            return fi.dir().absolutePath();
        }
    }

    if (!ws.isEmpty() && QDir(ws).exists()) {
        return QDir(ws).absolutePath();
    }

    return QDir::currentPath();
}

bool DeepSeekHarnessAdapter::hasActiveSession() const {
    if (m_lastTaskId.isEmpty()) return false;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return (now - m_lastTaskTime) < (15 * 60 * 1000); // 15 分钟内保持有效会话
}

void DeepSeekHarnessAdapter::resetSession() {
    m_lastTaskId.clear();
    m_lastTaskTitle.clear();
    m_lastTaskTime = 0;
    m_forceNewTask = false;
    cancelCurrentTask();
}

void DeepSeekHarnessAdapter::cancelCurrentTask() {
    if (m_activeProcess) {
        m_isManuallyCancelled = true;
        qint64 pid = m_activeProcess->processId();
        if (pid > 0) {
#if defined(Q_OS_UNIX)
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
#endif
        }
        m_activeProcess->kill();
        m_activeProcess->deleteLater();
        m_activeProcess = nullptr;
    }
}

static QString humanizeStepAction(const QString &desc, const QString &cmd, const QString &tool) {
    if (tool == "skill") {
        QString target = !cmd.isEmpty() ? cmd : desc;
        if (target.isEmpty()) target = "扩展能力";
        return QString("调用扩展技能 [%1]").arg(target);
    }

    QString c = cmd.trimmed();
    if (!c.isEmpty()) {
        if (c.contains("aipy-computer-use call start_session")) return "启动 aipy-computer-use 会话与应用捕获";
        if (c.contains("aipy-computer-use call observe")) return "通过 aipy-computer-use 观察目标窗口与界面元素";
        if (c.contains("aipy-computer-use call check_permissions")) return "检查 aipy 系统辅助功能与自动化权限";
        if (c.contains("aipy-computer-use call bring_to_front")) return "将目标窗口唤起至桌面最前台";
        if (c.contains("aipy-computer-use call list_windows") || c.contains("list-tools")) return "查询 aipy 可用窗口清单与工具链";
        if (c.contains("aipy-computer-use")) return "调用 aipy 桌面自动化技能处理界面任务";
        if (c.contains("df -h") || c.contains("df -i")) return "检查磁盘分区整体情况";
        if (c.contains("du -sh") && c.contains("~/*")) return "测量用户主目录占用排行";
        if (c.contains("du -sh") && c.contains("Library")) return "分析 Library 应用支持与缓存目录";
        if (c.contains("find") && c.contains("node_modules")) return "检索项目中的 node_modules 目录";
        if (c.contains("find") && c.contains("+500M")) return "扫描系统中大于 500M 的大文件";
        if (c.contains("git status") || c.contains("git log")) return "检查 Git 仓库版本与代码变动";
        if (c.contains("git diff")) return "比对代码差异与修改细节";
        if (c.contains("make") || c.contains("cmake") || c.contains("npm run")) return "执行工程编译与构建流程";
        if (c.contains("ls -l") || c.contains("ls -la")) return "查看目录结构与文件清单";
        if (c.contains("cat ") || c.contains("head ") || c.contains("tail ")) return "读取并解析相关数据文件";
        QString firstCmd = c.split('\n').first();
        if (firstCmd.length() > 40) firstCmd = firstCmd.left(37) + "...";
        return firstCmd;
    }

    QString d = desc.trimmed();
    if (!d.isEmpty()) {
        QString lower = d.toLower();
        if (lower.contains("disk partition") || lower.contains("df -h")) return "检查系统磁盘分区与挂载概况";
        if (lower.contains("home directory") || lower.contains("top-level")) return "扫描用户主目录顶层空间占用排行";
        if (lower.contains("break down ~/library") || lower.contains("library usage")) return "深入拆解 ~/Library 应用数据与缓存细项";
        if (lower.contains("drill into application support") || lower.contains("application support")) return "排查 Application Support 与 Containers 容器大头";
        if (lower.contains("node_modules") || lower.contains("downloads")) return "扫描项目依赖 node_modules 与大文件下载";
        if (lower.contains("verify docker") || lower.contains("docker raw")) return "核实 Docker 稀疏镜像实际磁盘分配";
        if (lower.contains("list cleanable") || lower.contains("cleanable cache")) return "定位可清理的临时缓存与日志碎片";
        if (lower.contains("drill into cursor") || lower.contains("cursor/user")) return "拆解 Cursor 历史库与工作区状态";
        return d;
    }

    if (!tool.isEmpty()) {
        if (tool == "bash") return "执行终端系统指令";
        if (tool == "readFile" || tool == "read_file") return "读取目标文件内容";
        if (tool == "writeFile" || tool == "write_file") return "写入或修改目标文件";
        if (tool == "search" || tool == "grep") return "检索代码与匹配关键字";
        return QString("调度工具 [%1]").arg(tool);
    }

    return "深度推演与分析下一步行动";
}

void DeepSeekHarnessAdapter::executeTask(
    QString const& instruction,
    QString const& contextText,
    std::function<void(QString const& progressMsg)> progressCallback,
    std::function<void(AgentTaskResult const& result)> finishCallback)
{
    QString exePath = effectiveDshPath();
    if (exePath.isEmpty()) {
        AgentTaskResult res;
        res.success = false;
        res.error = "未在系统中检测到 deepseek-harness (dsh) 命令。\n"
                    "请通过终端运行 `npm install -g @deepseek-ai/dsh@next` 安装，\n"
                    "或在「设置 -> 模型与 Agent」中手动指定 dsh 的可执行路径。";
        finishCallback(res);
        return;
    }

    QString key = effectiveApiKey();
    if (key.isEmpty()) {
        AgentTaskResult res;
        res.success = false;
        res.error = "未检测到 DeepSeek API 密钥，请在设置中配置 DeepSeek API Key 后使用。";
        finishCallback(res);
        return;
    }

    // 终止可能仍在执行的上一个任务，并明确重置手动终止标记
    cancelCurrentTask();
    m_isManuallyCancelled = false;

    // 组装格式化与排版指导
    QString formattingNotice = 
        "\n\n【排版与回答核心要求】: 请务必输出详细完整的分析总结、排查数据或执行报告：\n"
        "- 保持段落与小标题层次清晰；\n"
        "- 步骤与要点使用列表分行展示；\n"
        "- 涉及系统/磁盘排查等命令时，避免全盘递归扫描（如对 / 或 ~ 递归扫描整盘耗时过长），优先检查常见关键目录与分区（如 `df -h`，`du -sh ~/Library/Caches ~/Downloads ~/Library/Application\\ Support` 等），确保高效反馈；\n"
        "- 严禁仅执行工具或返回空回复，必须给出针对任务的明确结论、具体排查数据和可操作建议。";

    QString fullInstruction;
    if (!contextText.trimmed().isEmpty()) {
        fullInstruction = QString("【参考上下文/选中文本】:\n%1\n\n【任务需求】:\n%2%3")
                              .arg(contextText.trimmed(), instruction.trimmed(), formattingNotice);
    } else {
        fullInstruction = instruction.trimmed() + formattingNotice;
    }

    // 动态同步扫描并注入本机已启用的自定义技能 (如 Aipy Computer Use, Git Committer 等)
    SkillManager::instance()->scanSkills();
    QString skillGuidance = SkillManager::instance()->getDshSkillsInstruction();
    if (!skillGuidance.isEmpty()) {
        fullInstruction += skillGuidance;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool shouldContinueSession = false;

    QString insLower = instruction.trimmed().toLower();
    bool isExplicitNewTask = (
        insLower.startsWith("/new") ||
        insLower.contains("新建任务") ||
        insLower.contains("新任务") ||
        insLower.contains("重新开始") ||
        insLower.contains("重置会话") ||
        insLower.contains("清空上下文")
    );

    if (m_forceNewTask || isExplicitNewTask) {
        m_lastTaskId.clear();
        m_lastTaskTitle.clear();
        shouldContinueSession = false;
    } else if (!m_lastTaskId.isEmpty() && (now - m_lastTaskTime) < (15 * 60 * 1000)) {
        shouldContinueSession = true;
    }
    m_forceNewTask = false;

    if (!shouldContinueSession) {
        m_lastTaskId.clear();
        m_lastTaskTitle = instruction.left(40).trimmed();
    }
    m_lastTaskTime = now;

    QString workspaceDir = detectActiveWorkspace();
    std::cout << "[DeepSeekHarness] 启动任务: " << instruction.left(50).toStdString()
              << " | 会话: " << (m_lastTaskId.isEmpty() ? "(新建会话)" : m_lastTaskId.toStdString())
              << " | 工作区: " << workspaceDir.toStdString() << std::endl;

    auto proc = new QProcess();
    m_activeProcess = proc;

    QString model = effectiveModel();
    QString baseUrl = effectiveBaseUrl();

    // 动态生成 patch 文件将所选模型同步到 dsh 的 profile 中
    QString patchPath = QDir::tempPath() + "/dsh_model_patch.yml";
    QFile patchFile(patchPath);
    if (patchFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&patchFile);
        out << "- id: agent-default-model\n"
            << "  config:\n"
            << "    provider: deepseek-official\n"
            << "    model: " << model << "\n";
        patchFile.close();
    }

    // 设置环境变量 (包含完整 Node / PATH 探查与注入)
    QProcessEnvironment env = buildProcessEnvironment(exePath, key, baseUrl, model);
    proc->setProcessEnvironment(env);

    if (QDir(workspaceDir).exists()) {
        proc->setWorkingDirectory(workspaceDir);
    }

    // 组装命令行参数: dsh --profile headless [--patch ...] --permission-mode danger-full-access --json [--approval-policy never] [--session-id <id>] "<instruction>"
    QStringList args;
    args << "--profile" << "headless";
    if (QFile::exists(patchPath)) {
        args << "--patch" << patchPath;
    }
    args << "--permission-mode" << "danger-full-access";
    args << "--json";
    if (m_autoApproval) {
        args << "--approval-policy" << "never";
    }
    // 授予 dsh 对所有已启用 Skills 目录的沙箱读写与执行权限
    for (const auto &skillDir : SkillManager::instance()->getEnabledSkillDirectories()) {
        args << "--add-dir" << skillDir;
    }
    if (shouldContinueSession && !m_lastTaskId.isEmpty()) {
        args << "--session-id" << m_lastTaskId;
    }
    args << fullInstruction;

    struct ToolJournalItem {
        QString tool;
        QString callId;
        QString commandOrDesc;
        QString result;
    };
    auto toolJournal = std::make_shared<QVector<ToolJournalItem>>();
    auto accumulatedThinking = std::make_shared<QString>();
    auto accumulatedReply = std::make_shared<QString>();
    auto finalParsedReply = std::make_shared<QString>();
    auto lastToolResult = std::make_shared<QString>();
    auto stderrBuffer = std::make_shared<QString>();
    auto stdoutBuffer = std::make_shared<QByteArray>();

    struct StepDetailItem {
        int stepNum = 1;
        QString tool;
        QString actionTitle;
        QString command;
        QString resultSummary;
        bool success = true;
        bool isRunning = false;
    };

    struct StepProgressTracker {
        int currentTurn = 1;
        int currentStep = 1;
        int estimatedSteps = 6;
        QString currentActionTitle;
        QString currentTool;
        QString currentCommand;
        QString currentThoughtSnippet;
        QString lastResultSummary;
        bool lastStepSuccess = true;
        QStringList activeSkills;
        QVector<StepDetailItem> steps;

        void advanceStep(int newStep) {
            if (newStep <= 0) return;
            if (!steps.isEmpty() && steps.last().isRunning) {
                steps.last().isRunning = false;
                if (steps.last().resultSummary.isEmpty()) {
                    steps.last().resultSummary = lastResultSummary.isEmpty() ? "已完成" : lastResultSummary;
                    steps.last().success = lastStepSuccess;
                }
            }
            currentStep = newStep;
            currentActionTitle.clear();
            currentTool.clear();
            currentCommand.clear();
            currentThoughtSnippet.clear();
            lastResultSummary.clear();
            lastStepSuccess = true;
        }

        void recordToolCall(int step, const QString &tool, const QString &action, const QString &cmd) {
            if (step > currentStep) {
                advanceStep(step);
            }
            currentTool = tool;
            currentCommand = cmd;
            currentActionTitle = action;

            bool found = false;
            for (auto &st : steps) {
                if (st.stepNum == currentStep && st.isRunning) {
                    st.tool = tool;
                    st.actionTitle = action;
                    st.command = cmd;
                    found = true;
                    break;
                }
            }
            if (!found) {
                StepDetailItem item;
                item.stepNum = currentStep;
                item.tool = tool;
                item.actionTitle = action;
                item.command = cmd;
                item.isRunning = true;
                item.success = true;
                steps.append(item);
            }
        }

        void recordResult(const QString &summary, bool isError) {
            lastResultSummary = summary;
            lastStepSuccess = !isError;
            if (!steps.isEmpty()) {
                auto &last = steps.last();
                last.resultSummary = summary;
                last.success = !isError;
                last.isRunning = false;
            }
        }

        QString renderCard() const {
            int step = std::max(1, currentStep);
            int estTotal = std::max(step + 1, estimatedSteps);
            int percent = std::clamp((step * 100) / estTotal, 15, 95);

            // 绘制精美字符渐变进度条 (10 格)
            int filled = (percent * 10) / 100;
            QString bar;
            for (int i = 0; i < 10; ++i) {
                bar += (i < filled) ? "▰" : "▱";
            }

            QString card = QString("⚡ **DeepSeek Harness 智能体执行中** · `第 %1 步`\n").arg(step);
            card += QString("`[ %1 ] %2%`\n\n").arg(bar, QString::number(percent));

            if (!activeSkills.isEmpty()) {
                card += QString("> 🧩 **已挂载扩展技能**: `%1`\n").arg(activeSkills.join("`, `"));
            }

            QString action = currentActionTitle;
            if (action.isEmpty()) {
                if (!currentTool.isEmpty()) action = QString("调度工具 [%1]").arg(currentTool);
                else if (!currentThoughtSnippet.isEmpty()) action = currentThoughtSnippet;
                else action = "正在深度推演与规划执行步骤...";
            }
            card += QString("> 🎯 **当前操作**: %1\n").arg(action);

            if (!currentCommand.isEmpty()) {
                QString cmd = currentCommand.trimmed();
                if (cmd.length() > 60) cmd = cmd.left(57) + "...";
                cmd.replace('\n', ' ');
                card += QString("> 🔨 **执行命令**: `%1`\n").arg(cmd);
            }

            if (!currentThoughtSnippet.isEmpty() && currentThoughtSnippet != action) {
                QString thk = currentThoughtSnippet.trimmed();
                if (thk.length() > 60) thk = thk.left(57) + "...";
                thk.replace('\n', ' ');
                card += QString("> 💭 **推理意图**: %1\n").arg(thk);
            }

            if (!lastResultSummary.isEmpty()) {
                card += QString("> %1 **上步产出**: %2\n").arg(lastStepSuccess ? "📊" : "⚠️", lastResultSummary);
            }

            // 类似 dsh desktop 的任务执行流水详情 (Task Details)
            if (!steps.isEmpty()) {
                card += "\n---\n#### 📋 任务执行详情流水 (Task Details):\n";
                int startIdx = std::max(0, (int)steps.size() - 4);
                for (int i = startIdx; i < steps.size(); ++i) {
                    const auto &st = steps[i];
                    QString statusIcon = st.isRunning ? "🔄" : (st.success ? "✔" : "❌");
                    QString statusText = st.isRunning ? "(执行中...)" : (st.success ? "(成功)" : "(失败)");

                    QString cmdPreview = st.command.trimmed();
                    if (cmdPreview.length() > 36) cmdPreview = cmdPreview.left(33) + "...";
                    cmdPreview.replace('\n', ' ');

                    QString desc = !st.actionTitle.isEmpty() ? st.actionTitle : st.tool;
                    if (!cmdPreview.isEmpty() && !desc.contains(cmdPreview)) {
                        desc += QString(" · `%1`").arg(cmdPreview);
                    }

                    if (!st.resultSummary.isEmpty() && !st.isRunning) {
                        QString resPrev = st.resultSummary;
                        if (resPrev.length() > 32) resPrev = resPrev.left(29) + "...";
                        resPrev.replace('\n', ' ');
                        card += QString("- %1 **步骤 %2** `[%3]`: %4 → %5 %6\n")
                            .arg(statusIcon, QString::number(st.stepNum), st.tool.isEmpty() ? "task" : st.tool, desc, resPrev, statusText);
                    } else {
                        card += QString("- %1 **步骤 %2** `[%3]`: %4 %5\n")
                            .arg(statusIcon, QString::number(st.stepNum), st.tool.isEmpty() ? "task" : st.tool, desc, statusText);
                    }
                }
            }

            card += "\n[⏹ 终止任务](agent://cancel)";
            return card;
        }
    };
    auto tracker = std::make_shared<StepProgressTracker>();
    tracker->currentActionTitle = QString("正在准备工作区与调度环境 (%1)...").arg(QFileInfo(workspaceDir).fileName());

    auto lastProgressDispatchTime = std::make_shared<qint64>(0);
    auto pendingProgressDispatch = std::make_shared<bool>(false);

    auto dispatchProgress = [progressCallback, tracker, lastProgressDispatchTime, pendingProgressDispatch](bool force = false) {
        if (!progressCallback) return;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (force || (now - *lastProgressDispatchTime >= 250)) {
            *lastProgressDispatchTime = now;
            *pendingProgressDispatch = false;
            progressCallback(tracker->renderCard());
        } else {
            *pendingProgressDispatch = true;
        }
    };

    dispatchProgress(true);

    auto parseJsonLine = [this, dispatchProgress, toolJournal, accumulatedThinking, accumulatedReply, finalParsedReply, lastToolResult, stderrBuffer, tracker](const QByteArray &rawLine) {
        if (rawLine.isEmpty()) return;
        QString lineStr = QString::fromUtf8(rawLine);
        QJsonDocument doc = QJsonDocument::fromJson(rawLine);

        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            QString type = obj["type"].toString();
            QJsonObject srcObj = obj.contains("data") && obj["data"].isObject() ? obj["data"].toObject() : obj;

            if (type == "session") {
                QString sid = srcObj["sessionId"].toString();
                if (sid.isEmpty()) sid = srcObj["id"].toString();
                if (!sid.isEmpty()) {
                    m_lastTaskId = sid;
                }
            } else if (type == "step/start" || type == "step_start" || type == "step" || type == "status") {
                int newTurn = srcObj["turn"].toInt(0);
                if (newTurn > 0) tracker->currentTurn = newTurn;

                int newStep = srcObj["step"].toInt(0);
                if (newStep == 0) newStep = srcObj["stepIndex"].toInt(0);

                QString phase = srcObj["phase"].toString();
                QString msg = srcObj["message"].toString();

                if (newStep > 0 && newStep > tracker->currentStep) {
                    tracker->advanceStep(newStep);
                } else if (phase == "step_start" && newStep > 0) {
                    tracker->advanceStep(newStep);
                } else if (!msg.isEmpty()) {
                    tracker->currentActionTitle = msg;
                }
                dispatchProgress(false);
            } else if (type == "job/registered" || type == "job_registered") {
                QString label = srcObj["label"].toString().trimmed();
                QString kind = srcObj["kind"].toString();
                if (!label.isEmpty()) {
                    tracker->currentCommand = label;
                    if (tracker->currentActionTitle.isEmpty()) {
                        tracker->currentActionTitle = humanizeStepAction("", label, kind);
                    }
                    dispatchProgress(false);
                }
            } else if (type == "job/settled" || type == "job_settled") {
                QString status = srcObj["status"].toString(); // "completed", "failed"
                QString detail = srcObj["detail"].toString(); // "exit code: 0", etc.
                bool isErr = (status == "failed" || detail.contains("code: 1") || detail.contains("code: 2") || detail.contains("error"));
                QString summary = detail.isEmpty() ? (isErr ? "执行失败" : "执行成功") : detail;
                tracker->recordResult(summary, isErr);
                dispatchProgress(false);
            } else if (type == "assistant/message" || type == "assistant_message") {
                if (srcObj.contains("message") && srcObj["message"].isObject()) {
                    QJsonObject msgObj = srcObj["message"].toObject();
                    QJsonArray contentArr = msgObj["content"].toArray();
                    for (const auto &cVal : contentArr) {
                        QJsonObject cObj = cVal.toObject();
                        if (cObj["type"].toString() == "reasoning") {
                            QString rText = cObj["text"].toString().trimmed();
                            if (!rText.isEmpty()) {
                                accumulatedThinking->append(rText + "\n");
                                QString first = rText.split('\n').first().trimmed();
                                if (first.length() > 50) first = first.left(47) + "...";
                                tracker->currentThoughtSnippet = first;
                                if (tracker->currentActionTitle.isEmpty()) {
                                    tracker->currentActionTitle = first;
                                }
                                dispatchProgress(false);
                            }
                        }
                    }
                }
            } else if (type == "reasoning-chunks") {
                QJsonArray texts = obj["texts"].toArray();
                QString chunkText;
                for (const auto &t : texts) chunkText += t.toString();
                chunkText = chunkText.trimmed();
                if (!chunkText.isEmpty() && tracker->currentThoughtSnippet.isEmpty()) {
                    if (chunkText.length() > 50) chunkText = chunkText.left(47) + "...";
                    tracker->currentThoughtSnippet = chunkText;
                    dispatchProgress(false);
                }
            } else if (type == "thinking") {
                QString thk = obj["text"].toString();
                if (!thk.isEmpty()) {
                    accumulatedThinking->append(thk + "\n");
                    if (tracker->currentThoughtSnippet.isEmpty()) {
                        QString first = thk.trimmed().split('\n').first();
                        if (first.length() > 40) first = first.left(37) + "...";
                        tracker->currentThoughtSnippet = first;
                        if (tracker->currentActionTitle.isEmpty()) {
                            tracker->currentActionTitle = first;
                        }
                        dispatchProgress(false);
                    }
                }
            } else if (type == "tool_call" || type == "tool" || type == "tool_use" || type == "tool/call") {
                QString tool = srcObj["tool"].toString();
                if (tool.isEmpty()) tool = srcObj["name"].toString();
                QString callId = srcObj["callId"].toString();
                if (callId.isEmpty()) callId = srcObj["id"].toString();
                int callStep = srcObj["step"].toInt(0);
                if (callStep > tracker->currentStep) {
                    tracker->advanceStep(callStep);
                }

                QString desc;
                QString cmd;
                if (srcObj.contains("input")) {
                    QJsonObject inObj = srcObj["input"].toObject();
                    desc = inObj["description"].toString();
                    cmd = inObj["command"].toString();
                    if (cmd.isEmpty()) cmd = inObj["instruction"].toString();
                    if (cmd.isEmpty()) cmd = inObj["path"].toString();
                    if (cmd.isEmpty()) cmd = inObj["name"].toString();
                } else if (srcObj.contains("arguments")) {
                    QJsonValue argVal = srcObj["arguments"];
                    if (argVal.isObject()) {
                        QJsonObject argObj = argVal.toObject();
                        cmd = argObj["name"].toString();
                        if (cmd.isEmpty()) cmd = argObj["command"].toString();
                        if (cmd.isEmpty()) desc = argObj["description"].toString();
                    } else if (argVal.isString()) {
                        QJsonDocument argDoc = QJsonDocument::fromJson(argVal.toString().toUtf8());
                        if (argDoc.isObject()) {
                            QJsonObject argObj = argDoc.object();
                            cmd = argObj["name"].toString();
                            if (cmd.isEmpty()) cmd = argObj["command"].toString();
                            if (cmd.isEmpty()) desc = argObj["description"].toString();
                        }
                    }
                }

                if (tool == "skill") {
                    QString sName = !cmd.isEmpty() ? cmd : desc;
                    if (!sName.isEmpty() && !tracker->activeSkills.contains(sName)) {
                        tracker->activeSkills.append(sName);
                    }
                } else if (cmd.contains("aipy-computer-use") && !tracker->activeSkills.contains("aipy-computer-use")) {
                    tracker->activeSkills.append("aipy-computer-use");
                }

                QString actionTitle = humanizeStepAction(desc, cmd, tool);
                tracker->recordToolCall(tracker->currentStep, tool, actionTitle, cmd);

                ToolJournalItem item;
                item.tool = tool;
                item.callId = callId;
                item.commandOrDesc = actionTitle;
                toolJournal->append(item);

                dispatchProgress(false);
            } else if (type == "tool_result" || type == "tool/result") {
                QString callId = srcObj["callId"].toString();
                if (callId.isEmpty() && srcObj.contains("message")) {
                    QJsonObject msgObj = srcObj["message"].toObject();
                    callId = msgObj["toolCallId"].toString();
                }
                bool isErr = false;
                if (srcObj.contains("isError")) isErr = srcObj["isError"].toBool();
                if (srcObj.contains("message") && srcObj["message"].isObject()) {
                    QJsonObject msgObj = srcObj["message"].toObject();
                    if (msgObj.contains("isError")) isErr = msgObj["isError"].toBool();
                }

                QString resStr = srcObj["result"].toString().trimmed();
                if (resStr.isEmpty() && srcObj.contains("message")) {
                    QJsonObject msgObj = srcObj["message"].toObject();
                    QJsonArray contentArr = msgObj["content"].toArray();
                    for (const auto &cVal : contentArr) {
                        resStr += cVal.toObject()["text"].toString();
                    }
                    resStr = resStr.trimmed();
                }
                if (!resStr.isEmpty()) {
                    bool matched = false;
                    if (!callId.isEmpty()) {
                        for (auto &it : *toolJournal) {
                            if (it.callId == callId) {
                                it.result = resStr;
                                matched = true;
                                break;
                            }
                        }
                    }
                    if (!matched && !toolJournal->isEmpty()) {
                        toolJournal->last().result = resStr;
                    }
                    lastToolResult->append(resStr + "\n");

                    // 提炼上步产出简报
                    QString firstLine = resStr.split('\n').first().trimmed();
                    if (firstLine.length() > 45) firstLine = firstLine.left(42) + "...";
                    firstLine.replace('\n', ' ');
                    tracker->recordResult(firstLine, isErr);

                    dispatchProgress(false);
                }
            } else if (type == "final" || type == "result" || type == "final_reply" || type == "answer") {
                QString rep = obj["text"].toString();
                if (rep.isEmpty()) rep = obj["reply"].toString();
                if (rep.isEmpty()) rep = obj["content"].toString();
                if (!rep.isEmpty()) {
                    *finalParsedReply = rep;
                }
                if (obj.contains("reason")) {
                    QJsonObject reasonObj = obj["reason"].toObject();
                    if (reasonObj["kind"].toString() == "error") {
                        QJsonObject errObj = reasonObj["error"].toObject();
                        QString errMsg = errObj["message"].toString();
                        if (!errMsg.isEmpty()) {
                            stderrBuffer->append(errMsg);
                        }
                    }
                }
            } else if (type == "text" || obj.contains("text")) {
                QString txt = obj["text"].toString().trimmed();
                if (!txt.isEmpty()) {
                    accumulatedReply->append(txt + "\n");
                    if (txt.length() > 6 && txt.length() < 90 && tracker->currentActionTitle.isEmpty()) {
                        tracker->currentActionTitle = txt.left(45);
                        dispatchProgress(false);
                    }
                }
            } else if (obj.contains("message")) {
                QString msg = obj["message"].toString();
                if (!msg.isEmpty()) {
                    tracker->currentActionTitle = msg;
                    dispatchProgress(false);
                }
            } else if (obj.contains("content")) {
                QString cnt = obj["content"].toString();
                if (!cnt.isEmpty()) accumulatedReply->append(cnt);
            }
        } else {
            // 非 JSON 输出，作为纯文本累积
            accumulatedReply->append(lineStr + "\n");
            if (lineStr.contains("Running") || lineStr.contains("Executing") || lineStr.contains("Plan")) {
                tracker->currentActionTitle = lineStr.left(45);
                dispatchProgress(false);
            }
        }
    };

    QObject::connect(proc, &QProcess::readyReadStandardOutput, [proc, stdoutBuffer, parseJsonLine, dispatchProgress, pendingProgressDispatch]() {
        QByteArray chunk = proc->readAllStandardOutput();
        std::cout << "[DeepSeekHarness stdout chunk: " << chunk.size() << " bytes]" << std::endl;
        stdoutBuffer->append(chunk);
        while (true) {
            int newlineIdx = stdoutBuffer->indexOf('\n');
            if (newlineIdx == -1) break;
            QByteArray rawLine = stdoutBuffer->left(newlineIdx).trimmed();
            stdoutBuffer->remove(0, newlineIdx + 1);
            if (!rawLine.isEmpty()) {
                std::cout << "[DeepSeekHarness line] " << rawLine.toStdString() << std::endl;
                parseJsonLine(rawLine);
            }
        }
        if (*pendingProgressDispatch) {
            dispatchProgress(false);
        }
    });

    QObject::connect(proc, &QProcess::readyReadStandardError, [proc, stderrBuffer]() {
        QByteArray errData = proc->readAllStandardError();
        std::cerr << "[DeepSeekHarness stderr] " << errData.toStdString() << std::endl;
        stderrBuffer->append(QString::fromUtf8(errData));
    });

    QObject::connect(proc, &QProcess::errorOccurred, [proc, stderrBuffer](QProcess::ProcessError error) {
        std::cerr << "[DeepSeekHarness errorOccurred] " << error << " (" << proc->errorString().toStdString() << ")" << std::endl;
        stderrBuffer->append(proc->errorString());
    });

    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [this, proc, finishCallback, toolJournal, accumulatedThinking, accumulatedReply, finalParsedReply, lastToolResult, stderrBuffer, stdoutBuffer, parseJsonLine, dispatchProgress](int exitCode, QProcess::ExitStatus exitStatus) {
        std::cout << "[DeepSeekHarness finished] exitCode=" << exitCode << " exitStatus=" << exitStatus << std::endl;
        // 读取残留输出并彻底清空缓冲区
        stdoutBuffer->append(proc->readAllStandardOutput());
        if (!stdoutBuffer->isEmpty()) {
            for (const auto &line : stdoutBuffer->split('\n')) {
                QByteArray trimmed = line.trimmed();
                if (!trimmed.isEmpty()) parseJsonLine(trimmed);
            }
            stdoutBuffer->clear();
        }
        dispatchProgress(true);
        QByteArray remainingErr = proc->readAllStandardError();
        if (!remainingErr.isEmpty()) {
            stderrBuffer->append(QString::fromUtf8(remainingErr));
        }

        AgentTaskResult res;
        res.appName = "DeepSeek Harness";
        res.taskId = m_lastTaskId;
        res.launchTarget = detectActiveWorkspace();

        if (m_isManuallyCancelled) {
            m_isManuallyCancelled = false;
            res.success = false;
            res.error = "🛑 任务已被主人手动终止。";
        } else if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            res.success = true;
            QString chosenReply;
            if (!finalParsedReply->trimmed().isEmpty()) {
                chosenReply = finalParsedReply->trimmed();
            } else if (!accumulatedReply->trimmed().isEmpty()) {
                chosenReply = accumulatedReply->trimmed();
            }

            // 统计并提取本次任务中实际调用的所有扩展技能与 CLI
            QStringList skillsUsed;
            for (const auto &item : *toolJournal) {
                if (item.tool == "skill") {
                    QString sName = item.commandOrDesc;
                    sName.remove("调用扩展技能 [").remove("]");
                    sName = sName.trimmed();
                    if (!sName.isEmpty() && !skillsUsed.contains(sName)) {
                        skillsUsed.append(sName);
                    }
                } else if (item.commandOrDesc.contains("aipy-computer-use") || item.commandOrDesc.contains(".dsh/skills")) {
                    if (!skillsUsed.contains("aipy-computer-use")) {
                        skillsUsed.append("aipy-computer-use");
                    }
                }
            }

            // 若最终文本为空，由执行流水与思考推演生成结构化汇报
            if (chosenReply.isEmpty()) {
                QString synthesized;
                if (!toolJournal->isEmpty()) {
                    synthesized += "### 📋 智能体执行过程与数据汇总\n\n";
                    for (const auto &item : *toolJournal) {
                        synthesized += QString("#### 🔧 工具: `%1` (%2)\n").arg(item.tool, item.commandOrDesc);
                        if (!item.result.trimmed().isEmpty()) {
                            synthesized += QString("```text\n%1\n```\n\n").arg(item.result.trimmed());
                        }
                    }
                }
                if (!accumulatedThinking->trimmed().isEmpty()) {
                    synthesized += "\n### 💭 推理与分析总结\n" + accumulatedThinking->trimmed().right(1000) + "\n";
                }
                if (synthesized.trimmed().isEmpty() && !lastToolResult->trimmed().isEmpty()) {
                    synthesized = QString("📊 **执行数据**:\n```\n%1\n```").arg(lastToolResult->trimmed());
                }
                if (!synthesized.trimmed().isEmpty()) {
                    chosenReply = synthesized.trimmed();
                } else {
                    chosenReply = "✅ DeepSeek Harness 任务已执行完成。";
                }
            } else if (chosenReply.length() < 50 && !toolJournal->isEmpty()) {
                // 如果回复非常简短，自动附上执行数据详情
                QString toolDetails = "\n\n### 📊 详细执行数据:\n";
                for (const auto &item : *toolJournal) {
                    if (!item.result.trimmed().isEmpty()) {
                        toolDetails += QString("```text\n# [%1] %2\n%3\n```\n").arg(item.tool, item.commandOrDesc, item.result.trimmed());
                    }
                }
                chosenReply += toolDetails;
            }

            // 关键：将调用的扩展技能明确置顶在最终交付结果中，让用户一眼确证技能已被正确调度与执行
            if (!skillsUsed.isEmpty()) {
                QString badge = QString("> 🧩 **本次任务已成功联动扩展技能**: `%1`\n\n").arg(skillsUsed.join("`, `"));
                if (!chosenReply.startsWith("> 🧩")) {
                    chosenReply = badge + chosenReply;
                }
            }

            res.reply = chosenReply;
        } else {
            res.success = false;
            QString err = stderrBuffer->trimmed();
            if (err.isEmpty()) err = accumulatedReply->trimmed();
            if (err.isEmpty()) err = QString("dsh 进程异常退出 (退出码: %1)").arg(exitCode);
            res.error = err;

            // 若因会话失效导致失败，重置 taskId 避免下次继续失败
            if (err.contains("does not exist") || err.contains("unknown id") || err.contains("Session")) {
                m_lastTaskId.clear();
            }
        }

        if (m_activeProcess == proc) {
            m_activeProcess = nullptr;
        }
        proc->deleteLater();

        finishCallback(res);
    });

    std::cout << "[DeepSeekHarness] 启动进程: " << exePath.toStdString() << " " << args.join(" ").toStdString() << std::endl;
    std::cout << "[DeepSeekHarness PATH]: " << env.value("PATH").toStdString() << std::endl;
    proc->start(exePath, args);
}

void DeepSeekHarnessAdapter::testConnection(std::function<void(bool success, QString const& message)> callback) {
    QString exePath = effectiveDshPath();
    if (exePath.isEmpty()) {
        callback(false, "未检测到 deepseek-harness (dsh) 命令。\n请通过 `npm install -g @deepseek-ai/dsh@next` (或 `pnpm add -g @deepseek-ai/dsh@next`) 安装，或手动指定可执行路径。");
        return;
    }

    QString key = effectiveApiKey();
    QString model = effectiveModel();
    QString baseUrl = effectiveBaseUrl();
    auto profile = AgentService::instance()->getActiveProfile();

    auto proc = new QProcess();
    QProcessEnvironment env = buildProcessEnvironment(exePath, key, baseUrl, model);
    proc->setProcessEnvironment(env);

    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [proc, callback, exePath, key, model, profile](int exitCode, QProcess::ExitStatus exitStatus) {
        QString verInfo = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        if (verInfo.isEmpty()) verInfo = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        proc->deleteLater();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            if (key.isEmpty()) {
                callback(true, QString("✅ 成功定位到 dsh 命令: %1 (%2)\n⚠️ 注意: 活动模型「%3」尚未配置 API Key，请在上方基础大模型配置池中填入密钥。")
                                   .arg(exePath, verInfo, profile.name.isEmpty() ? "当前配置" : profile.name));
            } else {
                callback(true, QString("🎉 DeepSeek Harness 适配器就绪！\n"
                                       "• 命令路径: %1\n"
                                       "• 工具版本: %2\n"
                                       "• 联动活动模型: %3 (模型名: %4)\n"
                                       "• API 密钥: 已检测到并就绪")
                                   .arg(exePath, verInfo, profile.name.isEmpty() ? "当前活动配置" : profile.name, model));
            }
        } else {
            callback(false, QString("dsh 命令运行测试失败 (退出码: %1): %2").arg(QString::number(exitCode), verInfo));
        }
    });

    proc->start(exePath, QStringList() << "--version");
}

void DeepSeekHarnessAdapter::openTask(QString const& /* taskId */) {
    QString ws = detectActiveWorkspace();
    if (!ws.isEmpty() && QDir(ws).exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ws));
    }
}
