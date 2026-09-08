#include "InitiativeTrigger.hpp"
#include "AgentService.hpp"
#include <QDateTime>

InitiativeTrigger *InitiativeTrigger::instance()
{
    static InitiativeTrigger s_instance;
    return &s_instance;
}

InitiativeTrigger::InitiativeTrigger()
{
}

bool InitiativeTrigger::evaluateInitiative(PetState &state, const QJsonObject &contextInfo, int &calculatedScore, QString &triggerReason)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 检查每日重置
    QDateTime nowDate = QDateTime::fromMSecsSinceEpoch(now);
    QDateTime lastDate = QDateTime::fromMSecsSinceEpoch(state.lastDayResetTime);
    if (nowDate.date() != lastDate.date()) {
        state.initiativeCountToday = 0;
        state.lastDayResetTime = now;
    }

    // 每日上限保护
    if (state.initiativeCountToday >= m_dailyLimit) {
        calculatedScore = 0;
        return false;
    }

    // 读取设置中的搭讪频率
    auto cfg = AgentService::instance()->config();
    if (cfg.banterFrequencyLevel == 0) {
        calculatedScore = -999;
        return false;
    }

    int dynamicCooldown = m_cooldownSeconds;
    if (cfg.banterFrequencyLevel == 1) dynamicCooldown = 300;      // 5 分钟 (低频)
    else if (cfg.banterFrequencyLevel == 2) dynamicCooldown = 120; // 2 分钟 (适度贴心)
    else if (cfg.banterFrequencyLevel == 3) dynamicCooldown = 60;  // 1 分钟 (高频灵动)

    // 冷却时间检查（以秒为单位）
    if (state.lastTalkTime == 0) {
        // 首次登场：开机 3 秒缓冲后即可触发初次问候与小动作
        qint64 startupSec = (now - state.lastInteractionTime) / 1000;
        if (startupSec < 3) {
            return false;
        }
    } else {
        qint64 timeSinceLastTalkSec = (now - state.lastTalkTime) / 1000;
        if (timeSinceLastTalkSec < dynamicCooldown) {
            calculatedScore = -100;
            return false;
        }
    }

    int score = 0;
    QStringList reasons;

    // 0. 初次登场问候
    if (state.lastTalkTime == 0) {
        score += 35;
        reasons << "startup_greeting";
    }

    // 1. 连续高强度工作/写代码关怀
    int workMins = contextInfo["work_minutes"].toInt(0);
    if (cfg.enableContextualCare) {
        if (workMins >= 45) {
            score += 40;
            reasons << "continuous_work_strain";
        } else if (workMins >= 20) {
            score += 20;
            reasons << "work_focus";
        }
    }

    // 2. 摸鱼检测与俏皮抓包
    QString activeApp = contextInfo["active_app"].toString().toLower();
    QString windowTitle = contextInfo["window_title"].toString().toLower();
    if (activeApp.contains("bilibili") || windowTitle.contains("bilibili") ||
        activeApp.contains("youtube") || windowTitle.contains("youtube") ||
        activeApp.contains("weibo") || windowTitle.contains("微博") ||
        activeApp.contains("douyin") || windowTitle.contains("抖音")) {
        score += 35;
        reasons << "slacking_catch";
    }

    // 3. 深夜/特殊时段关怀
    int hour = nowDate.time().hour();
    if (hour >= 23 || hour <= 4) {
        score += 35;
        reasons << "late_night_sleep";
    } else if (hour >= 11 && hour <= 13) {
        score += 20;
        reasons << "lunch_time";
    } else if (hour >= 17 && hour <= 19) {
        score += 15;
        reasons << "dinner_offwork";
    }

    // 4. 无聊度与陪伴渴望
    if (state.boredom >= 25) {
        score += 25;
        reasons << "pet_bored";
    }

    // 5. 音乐陪伴 (分值提升至 25，听歌时随时可互动)
    if (contextInfo["is_music_playing"].toBool(false)) {
        score += 25;
        reasons << "music_sharing";
    }

    // 6. 应用探针感知与打断恢复交互 (Context Recovery)
    QJsonObject semanticCtx = contextInfo["app_semantic_context"].toObject();
    bool isContextRecovery = contextInfo["is_context_recovery"].toBool(false);
    if (isContextRecovery) {
        score += 45;
        reasons << "context_recovery";
    }

    // 7. 调试排错陪伴 (Debugging Follow-up)
    QString actDetail = semanticCtx["detail"].toString().toLower();
    QString actSemantic = semanticCtx["semantic_activity"].toString().toLower();
    if (actDetail.contains("error") || actDetail.contains("crash") || actDetail.contains("bug") ||
        actSemantic.contains("排查") || actSemantic.contains("调试") || actSemantic.contains("解决")) {
        score += 30;
        reasons << "debugging_followup";
    }

    // 8. 前台活跃应用感知 (Coding / Web Browsing / Terminal / Office)
    QString appType = semanticCtx["app_type"].toString().toLower();
    if (appType.contains("ide") || appType.contains("editor") ||
        activeApp.contains("code") || activeApp.contains("cursor") ||
        activeApp.contains("antigravity") || activeApp.contains("xcode") ||
        activeApp.contains("clion") || activeApp.contains("pycharm") ||
        activeApp.contains("intellij") || activeApp.contains("webstorm")) {
        score += 20;
        reasons << "coding_flow";
    } else if (appType.contains("browser") || activeApp.contains("chrome") ||
               activeApp.contains("safari") || activeApp.contains("edge") ||
               activeApp.contains("firefox") || activeApp.contains("arc")) {
        score += 15;
        reasons << "web_exploring";
    } else if (appType.contains("terminal") || activeApp.contains("terminal") ||
               activeApp.contains("iterm") || activeApp.contains("warp")) {
        score += 15;
        reasons << "terminal_hacking";
    } else if (!activeApp.isEmpty()) {
        score += 10;
        reasons << "app_active";
    }

    // 9. 近期聊天话题记忆呼应
    if (!AgentService::instance()->memoryHistory().isEmpty()) {
        score += 15;
        reasons << "recent_chat_resonance";
    }

    // 10. 用户当前实时操作热度 (鼠标/键盘活跃)
    int userIdleSec = contextInfo["user_idle_seconds"].toInt(0);
    if (userIdleSec < 30) {
        score += 10;
        reasons << "user_active";
    }

    // 基础活跃分
    score += 15;

    calculatedScore = score;
    triggerReason = reasons.join(", ");

    return (score >= m_triggerThreshold);
}

void InitiativeTrigger::recordTalkSuccess(PetState &state)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    state.lastTalkTime = now;
    state.initiativeCountToday++;
    state.boredom = std::max(0, state.boredom - 30);
    state.social = std::max(0, state.social - 20);
    state.affection = std::clamp(state.affection + 1, 0, 100);
}
