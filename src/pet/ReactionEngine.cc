#include "ReactionEngine.hpp"
#include "../system/SensorManager.hpp"
#include <QRandomGenerator>
#include <QDateTime>
#include <iostream>

ReactionEngine *ReactionEngine::instance()
{
    static ReactionEngine s_instance;
    return &s_instance;
}

ReactionEngine::ReactionEngine()
{
    initDefaultRules();
}

void ReactionEngine::initDefaultRules()
{
    m_rules.clear();

    // 1. Agent 任务开始
    {
        ReactionRule r;
        r.eventType = "agent.task.started";
        r.actions = { PetActionType::LookAtCursor, PetActionType::Sit };
        r.speechOptions = { "又有活干了？", "开始写代码啦~", "看你这次写出几个Bug" };
        r.speechProbability = 0.35;
        r.boredomDelta = -20;
        m_rules.push_back(r);
    }

    // 2. Agent 任务完成（跳跃欢呼）
    {
        ReactionRule r;
        r.eventType = "agent.task.completed";
        r.actions = { PetActionType::Jump };
        r.speechOptions = { "搞定收工！", "这次写得挺快嘛。", "哼，勉强算你及格。" };
        r.speechProbability = 0.85;
        r.moodDelta = +15;
        r.affectionDelta = +2;
        r.boredomDelta = -30;
        m_rules.push_back(r);
    }

    // 3. Agent 任务失败
    {
        ReactionRule r;
        r.eventType = "agent.task.failed";
        r.actions = { PetActionType::Angry };
        r.speechOptions = { "翻车了吧...", "我就知道没那么简单。", "报错了，快去修！" };
        r.speechProbability = 0.75;
        r.moodDelta = -10;
        m_rules.push_back(r);
    }

    // 4. Agent 任务中止
    {
        ReactionRule r;
        r.eventType = "agent.task.aborted";
        r.actions = { PetActionType::Sit };
        r.speechOptions = { "怎么停下了？", "放弃了吗？", "下次再战吧。" };
        r.speechProbability = 0.6;
        r.moodDelta = -2;
        m_rules.push_back(r);
    }

    // 5. 用户点击桌宠 (普通状态下)
    {
        ReactionRule r;
        r.eventType = "user.click_pet";
        r.actions = { PetActionType::Jump, PetActionType::Happy };
        r.speechOptions = { 
            "戳我干嘛呀？想我了吗？💖", 
            "抓到你啦！今天工作还顺利嘛？✨", 
            "陪我玩会儿嘛~ (´▽`ʃ♡ƪ)", 
            "别光戳我，今天也要元气满满哦！🌸", 
            "再戳…再戳我就跳到你窗口顶上啦！🐾" 
        };
        r.speechProbability = 0.95;
        r.boredomDelta = -20;
        r.affectionDelta = +2;
        r.moodDelta = +6;
        m_rules.push_back(r);
    }

    // 6. 用户拖拽桌宠
    {
        ReactionRule r;
        r.eventType = "user.drag_pet";
        r.actions = { PetActionType::Angry };
        r.speechOptions = { 
            "哇啊！怎么把我拎起来了！💦", 
            "起飞咯~ (晃动小短手)", 
            "快放我下来，恐高啦！🐾", 
            "要带我去哪个新窗口看风景呀？✨" 
        };
        r.speechProbability = 0.75;
        r.moodDelta = -1;
        r.boredomDelta = -10;
        m_rules.push_back(r);
    }

    // 7. 系统休眠与合盖
    {
        ReactionRule r;
        r.eventType = "system.sleep";
        r.actions = { PetActionType::Sleep };
        r.speechOptions = { "呼噜噜...我也睡了~", "晚安！", "休息会儿~" };
        r.speechProbability = 0.8;
        m_rules.push_back(r);
    }

    // 8. 系统唤醒与亮屏
    {
        ReactionRule r;
        r.eventType = "system.wake";
        r.actions = { PetActionType::Jump, PetActionType::Happy };
        r.speechOptions = { "早呀！主人你回来啦~", "又是写代码的一天！", "元气满满~" };
        r.speechProbability = 0.8;
        r.energyDelta = +20;
        r.moodDelta = +5;
        m_rules.push_back(r);
    }

    // 9. 系统内存压力告警 (内核级共鸣)
    {
        ReactionRule r;
        r.eventType = "system.memory_pressure";
        r.actions = { PetActionType::Angry };
        r.speechOptions = { "电脑好像有点喘不过气了（内存好挤）… 💨", "⚠️ 系统内存告急，好卡呀！" };
        r.speechProbability = 1.0;
        r.moodDelta = -5;
        r.energyDelta = -10;
        m_rules.push_back(r);
    }

    // 10. 音乐播放与快捷键互动
    {
        ReactionRule r;
        r.eventType = "music.playing";
        r.actions = { PetActionType::Happy };
        r.speechOptions = { "切歌啦？这首好听！🎵", "跟着节奏摇摆~ 🎶", "心流模式启动！🎧" };
        r.speechProbability = 0.9;
        r.moodDelta = +5;
        r.boredomDelta = -20;
        m_rules.push_back(r);
    }

    // 11. 磁盘空间严重不足
    {
        ReactionRule r;
        r.eventType = "system.disk_low";
        r.actions = { PetActionType::Angry };
        r.speechOptions = { "⚠️ 磁盘剩余不到 10GB 啦，快清理垃圾！" };
        r.speechProbability = 1.0;
        m_rules.push_back(r);
    }
}

bool ReactionEngine::isCodeEditor(const QString &appName, const QString &bundleId) const
{
    QString target = (appName + " " + bundleId).toLower();
    return target.contains("cursor") ||
           target.contains("code") ||
           target.contains("xcode") ||
           target.contains("clion") ||
           target.contains("intellij") ||
           target.contains("pycharm") ||
           target.contains("webstorm") ||
           target.contains("sublime") ||
           target.contains("fleet") ||
           target.contains("zed") ||
           target.contains("android studio");
}

bool ReactionEngine::isGitTool(const QString &appName, const QString &bundleId) const
{
    QString target = (appName + " " + bundleId).toLower();
    return target.contains("sourcetree") ||
           target.contains("gitkraken") ||
           target.contains("fork") ||
           target.contains("tower") ||
           target.contains("github desktop");
}

bool ReactionEngine::isTerminal(const QString &appName, const QString &bundleId) const
{
    QString target = (appName + " " + bundleId).toLower();
    return target.contains("terminal") ||
           target.contains("iterm") ||
           target.contains("alacritty") ||
           target.contains("warp") ||
           target.contains("kitty") ||
           target.contains("hyper") ||
           target.contains("wezterm");
}

bool ReactionEngine::isChatApp(const QString &appName, const QString &bundleId) const
{
    QString target = (appName + " " + bundleId).toLower();
    return target.contains("wechat") ||
           target.contains("微信") ||
           target.contains("企业微信") ||
           target.contains("wework") ||
           target.contains("feishu") ||
           target.contains("飞书") ||
           target.contains("lark") ||
           target.contains("dingtalk") ||
           target.contains("钉钉") ||
           target.contains("qq") ||
           target.contains("telegram") ||
           target.contains("slack") ||
           target.contains("discord") ||
           target.contains("teams");
}

bool ReactionEngine::isBrowser(const QString &appName, const QString &bundleId) const
{
    QString target = (appName + " " + bundleId).toLower();
    return target.contains("chrome") ||
           target.contains("safari") ||
           target.contains("edge") ||
           target.contains("firefox") ||
           target.contains("arc") ||
           target.contains("brave") ||
           target.contains("opera") ||
           target.contains("vivaldi");
}

bool ReactionEngine::handleAppActivated(const PetEvent &event, PetActionCommand &outCommand, int &moodDelta, int &boredomDelta, int &affectionDelta)
{
    QString appName = event.payload["app_name"].toString().trimmed();
    QString bundleId = event.payload["bundle_id"].toString().trimmed();
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (appName.isEmpty() || appName == "Unknown" || appName.contains("Shijima", Qt::CaseInsensitive)) {
        return false;
    }

    // 记录切换历史
    m_appHistory.push_back({ appName, bundleId, now });
    // 裁剪超过 5 分钟 (300,000ms) 的过时历史，或保留最多 25 条
    while (!m_appHistory.empty() && ((now - m_appHistory.front().timestamp) > 300000 || m_appHistory.size() > 25)) {
        m_appHistory.pop_front();
    }

    if (m_currentFrontmostApp != appName) {
        m_currentFrontmostApp = appName;
        m_currentFrontmostBundle = bundleId;
        m_currentAppStartTime = now;
    }

    outCommand.moveToCenter = false;
    outCommand.durationMs = 4000;

    // =========================================================================
    // 1. 开发与提交链条响应：Cursor/VSCode -> Sourcetree/Git -> Terminal
    // =========================================================================
    if (isTerminal(appName, bundleId)) {
        bool hasGit = false;
        bool hasCode = false;
        qint64 gitTime = 0;

        for (auto it = m_appHistory.rbegin(); it != m_appHistory.rend(); ++it) {
            if (!hasGit && isGitTool(it->appName, it->bundleId)) {
                hasGit = true;
                gitTime = it->timestamp;
            } else if (hasGit && isCodeEditor(it->appName, it->bundleId)) {
                if (it->timestamp <= gitTime) {
                    hasCode = true;
                    break;
                }
            }
        }

        if (hasGit && hasCode && (now - m_lastDevChainTime) >= 300000) { // 5分钟冷却
            m_lastDevChainTime = now;
            outCommand.type = PetActionType::Happy;
            auto prevWork = SensorManager::instance()->previousWorkContext();
            QString activeFile = prevWork["active_file"].toString();
            QString ws = prevWork["workspace"].toString();
            if (!activeFile.isEmpty()) {
                outCommand.speechText = QString("改完 %1、提交 Git、终端部署一条龙，效率爆棚！🚀").arg(activeFile);
            } else if (!ws.isEmpty()) {
                outCommand.speechText = QString("%1 项目：代码、Git、终端一条龙，今天效率爆棚！🚀").arg(ws);
            } else {
                outCommand.speechText = "准备提交代码了嘛？记得检查有没有写 console.log~ 🚀";
            }
            moodDelta = +5;
            affectionDelta = +2;
            boredomDelta = -20;
            std::cout << "[情境感知] 命中开发与部署链条 (Code -> Git -> Terminal)" << std::endl;
            return true;
        }
    }

    // =========================================================================
    // 2. 频繁在 Cursor 和 微信/企业微信 之间反复横跳（高频切屏）
    // =========================================================================
    if (isChatApp(appName, bundleId) || isCodeEditor(appName, bundleId)) {
        int toggleCount = 0;
        bool lastWasChat = false;
        bool hasInitialized = false;

        for (auto it = m_appHistory.rbegin(); it != m_appHistory.rend(); ++it) {
            if ((now - it->timestamp) > 45000) break; // 仅看最近 45 秒

            bool curIsChat = isChatApp(it->appName, it->bundleId);
            bool curIsCode = isCodeEditor(it->appName, it->bundleId);

            if (curIsChat || curIsCode) {
                if (!hasInitialized) {
                    lastWasChat = curIsChat;
                    hasInitialized = true;
                } else if (lastWasChat != curIsChat) {
                    toggleCount++;
                    lastWasChat = curIsChat;
                }
            }
        }

        if (toggleCount >= 3 && (now - m_lastChatSpamTime) >= 180000) { // 3分钟冷却
            m_lastChatSpamTime = now;
            outCommand.type = PetActionType::Angry;
            auto curCtx = SensorManager::instance()->currentContext();
            auto prevWork = SensorManager::instance()->previousWorkContext();
            QString activeFile = curCtx["active_file"].toString();
            if (activeFile.isEmpty()) activeFile = prevWork["active_file"].toString();
            QString workspace = curCtx["workspace"].toString();
            if (workspace.isEmpty()) workspace = prevWork["workspace"].toString();

            if (!activeFile.isEmpty()) {
                outCommand.speechText = QString("一会儿改 %1 一会儿回消息，注意别把配置发错窗口啦 🤫").arg(activeFile);
            } else if (!workspace.isEmpty()) {
                outCommand.speechText = QString("在 %1 工程和聊天群反复横跳，打工人太忙啦 😵💫").arg(workspace);
            } else {
                outCommand.speechText = "打工人好忙，一会儿改代码一会儿回消息 😵💫";
            }
            moodDelta = -2;
            boredomDelta = -15;
            std::cout << "[情境感知] 命中频繁切屏高频横跳" << std::endl;
            return true;
        }
    }

    // =========================================================================
    // 3. 特定工具专属彩蛋 (带防打扰冷却)
    // =========================================================================
    if (isGitTool(appName, bundleId)) {
        if ((now - m_lastSourcetreeEggTime) >= 300000) { // 5分钟冷却
            m_lastSourcetreeEggTime = now;
            outCommand.type = PetActionType::LookAtCursor;
            auto curCtx = SensorManager::instance()->currentContext();
            QString repo = curCtx["workspace"].toString();
            if (repo.isEmpty()) repo = curCtx["detail"].toString();
            if (repo.startsWith("仓库: ")) repo = repo.mid(4);
            if (!repo.isEmpty()) {
                outCommand.speechText = QString("在看 %1 仓库的 Git 提交与分支吗？仔细检查冲突哦~ 🌿").arg(repo);
            } else {
                outCommand.speechText = "又要解决冲突了吗… 仔细核对下变更哦 🧗";
            }
            moodDelta = -1;
            std::cout << "[情境感知] 触发 Git 工具专属彩蛋" << std::endl;
            return true;
        }
    } else if (isTerminal(appName, bundleId)) {
        if ((now - m_lastTerminalEggTime) >= 300000) { // 5分钟冷却
            m_lastTerminalEggTime = now;
            outCommand.type = PetActionType::Sit;
            auto prevWork = SensorManager::instance()->previousWorkContext();
            QString prevFile = prevWork["active_file"].toString();
            QString prevWs = prevWork["workspace"].toString();
            if (!prevFile.isEmpty()) {
                outCommand.speechText = QString("刚编辑完 %1，要在终端里编译测试吗？慢慢敲别手滑~ 💻").arg(prevFile);
            } else if (!prevWs.isEmpty()) {
                outCommand.speechText = QString("在终端里跑 %1 相关的命令吗？不要手滑输 rm -rf 呀！⚠️").arg(prevWs);
            } else {
                outCommand.speechText = "不要手滑输 rm -rf 呀！⚠️";
            }
            std::cout << "[情境感知] 触发 Terminal 工具专属彩蛋" << std::endl;
            return true;
        }
    }

    return false;
}

bool ReactionEngine::checkContinuousDwell(PetActionCommand &outCommand, int &moodDelta)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    moodDelta = 0;

    // Chrome / 浏览器连续停留超过 15 分钟 (900,000ms)
    if (!m_currentFrontmostApp.isEmpty() && isBrowser(m_currentFrontmostApp, m_currentFrontmostBundle)) {
        if ((now - m_currentAppStartTime) >= 900000 && (now - m_lastBrowserEggTime) >= 1800000) { // 30分钟冷却
            m_lastBrowserEggTime = now;
            outCommand.type = PetActionType::LookAtCursor;
            auto curCtx = SensorManager::instance()->currentContext();
            QString url = curCtx["url"].toString();
            QString title = curCtx["detail"].toString();
            if (url.contains("gitlab") || url.contains("github")) {
                outCommand.speechText = "在看代码仓库和 PR 呢，仔细 review 辛苦啦 ☕";
            } else if (!title.isEmpty() && title.length() < 25) {
                outCommand.speechText = QString("在看「%1」吗？别忘了喝口水揉揉眼睛哦 🍵").arg(title);
            } else {
                outCommand.speechText = "是在查文档还是在摸鱼看视频呢？🍵";
            }
            outCommand.durationMs = 4500;
            outCommand.moveToCenter = false;
            std::cout << "[情境感知] 触发浏览器长时间驻留彩蛋" << std::endl;
            return true;
        }
    }

    return false;
}

bool ReactionEngine::evaluateReaction(const PetEvent &event, PetActionCommand &outCommand, int &moodDelta, int &boredomDelta, int &affectionDelta)
{
    moodDelta = 0;
    boredomDelta = 0;
    affectionDelta = 0;

    // 前台应用切换事件优先通过时序状态机处理
    if (event.type == "system.app_activated") {
        return handleAppActivated(event, outCommand, moodDelta, boredomDelta, affectionDelta);
    }

    for (const auto &rule : m_rules) {
        if (rule.eventType == event.type) {
            moodDelta = rule.moodDelta;
            boredomDelta = rule.boredomDelta;
            affectionDelta = rule.affectionDelta;

            // 选取动作
            if (!rule.actions.empty()) {
                int idx = QRandomGenerator::global()->bounded(static_cast<int>(rule.actions.size()));
                outCommand.type = rule.actions[idx];
            } else {
                outCommand.type = PetActionType::Idle;
            }

            // 选取台词：优先取 payload 中的 reply/text/song_name，否则取规则预设台词（带概率控制）
            if (event.payload.contains("reply") && !event.payload["reply"].toString().isEmpty()) {
                outCommand.speechText = event.payload["reply"].toString();
            } else if (event.payload.contains("text") && !event.payload["text"].toString().isEmpty()) {
                outCommand.speechText = event.payload["text"].toString();
            } else if (event.type == "music.playing" && event.payload.contains("song_name") && !event.payload["song_name"].toString().isEmpty()) {
                QString sname = event.payload["song_name"].toString().trimmed();
                QString artist = event.payload["artist"].toString().trimmed();
                outCommand.speechText = QString("🎵 正在播放：《%1》%2").arg(sname, artist.isEmpty() ? "" : "- " + artist);
            } else if (!rule.speechOptions.empty()) {
                double dice = QRandomGenerator::global()->generateDouble();
                if (dice <= rule.speechProbability) {
                    int idx = QRandomGenerator::global()->bounded(static_cast<int>(rule.speechOptions.size()));
                    outCommand.speechText = rule.speechOptions[idx];
                } else {
                    outCommand.speechText = "";
                }
            } else {
                outCommand.speechText = "";
            }


            if (event.payload.contains("appTarget")) {
                outCommand.appTarget = event.payload["appTarget"].toString();
            } else if (event.payload.contains("app")) {
                outCommand.appTarget = event.payload["app"].toString();
            }

            outCommand.moodDelta = moodDelta;
            outCommand.affectionDelta = affectionDelta;
            outCommand.durationMs = 4000;

            if (event.type == "music.playing") {
                outCommand.emote = PetEmoteType::MusicNote;
            } else if (event.type == "agent.task.completed" || event.type == "system.wake") {
                outCommand.emote = PetEmoteType::Sparkle;
            } else if (event.type == "agent.task.failed" || event.type == "system.disk_low") {
                outCommand.emote = PetEmoteType::AngryVein;
            } else if (event.type == "agent.task.started") {
                outCommand.emote = PetEmoteType::ThinkingBulb;
            } else if (event.type == "user.click_pet") {
                outCommand.emote = PetEmoteType::HappyHeart;
            } else if (event.type == "system.sleep") {
                outCommand.emote = PetEmoteType::SleepZzz;
            } else if (event.type == "system.memory_pressure") {
                outCommand.emote = PetEmoteType::DizzySwirl;
            }

            if (event.type.startsWith("agent.task.")) {
                outCommand.moveToCenter = true; // 外部 Agent 重要推送通知才跳到中央
            } else {
                outCommand.moveToCenter = false; // 其他交互直接在原地头上弹出
            }

            return true;
        }
    }

    return false;
}

