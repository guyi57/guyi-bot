#include "BehaviorEngine.hpp"
#include "ReactionEngine.hpp"
#include "InitiativeTrigger.hpp"
#include "AgentService.hpp"
#include "ShijimaWidget.hpp"
#include "ShijimaManager.hpp"
#include "ScoreBadgeWidget.hpp"
#include "SystemObserver.hpp"
#include "SensorManager.hpp"
#include "MusicPlayerManager.hpp"
#include "PetDiaryManager.hpp"
#include <QRandomGenerator>
#include <QDateTime>
#include <QCursor>
#include <QCoreApplication>
#include <iostream>

BehaviorEngine *BehaviorEngine::instance()
{
    static BehaviorEngine s_instance;
    return &s_instance;
}

BehaviorEngine::BehaviorEngine()
    : m_tickTimer(new QTimer())
{
    QObject::connect(m_tickTimer, &QTimer::timeout, [this]() {
        onTick();
    });
    initEventListeners();
}

void BehaviorEngine::initEventListeners()
{
    PetEventBus::instance()->subscribe("*", [this](const PetEvent &event) {
        handleEvent(event);
    });
}

void BehaviorEngine::start()
{
    if (!m_tickTimer->isActive()) {
        m_tickTimer->start(1000); // 1秒一次决策心跳
    }
}

void BehaviorEngine::stop()
{
    m_tickTimer->stop();
}

void BehaviorEngine::setActiveWidget(ShijimaWidget *widget)
{
    m_activeWidget = widget;
}

MoodTier BehaviorEngine::moodTier() const
{
    if (m_state.mood >= 30) return MoodTier::High;
    if (m_state.mood >= -20) return MoodTier::Medium;
    if (m_state.mood >= -60) return MoodTier::Low;
    return MoodTier::ExtremelyLow;
}

void BehaviorEngine::handleEvent(const PetEvent &event)
{
    // 更新状态记录
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (event.type.startsWith("user.")) {
        m_state.lastInteractionTime = now;
        if (event.type == "user.click_pet" || event.type == "user.petting") {
            PetDiaryManager::instance()->recordPetting();
        }
    }
    if (event.type == "agent.task.completed") {
        m_state.lastTaskCompletedTime = now;
    }
    if (event.type == "music.playing") {
        PetDiaryManager::instance()->recordMusicPlayed();
        for (auto *w : ShijimaManager::defaultManager()->mascots()) {
            if (w != nullptr) {
                w->motionController().triggerMusicDance(6.0f);
            }
        }
    }
    if (event.type == "system.trash_cleaned") {
        PetDiaryManager::instance()->recordTrashCleaned();
    }

    // 1. 评估即时 Reaction 规则 (前台应用时序、系统内存、音乐播放等)
    PetActionCommand cmd;
    int moodDelta = 0, boredomDelta = 0, affectionDelta = 0;
    bool hasReaction = ReactionEngine::instance()->evaluateReaction(event, cmd, moodDelta, boredomDelta, affectionDelta);

    m_state.mood += moodDelta;
    m_state.boredom += boredomDelta;
    m_state.affection += affectionDelta;
    m_state.clamp();

    if (hasReaction) {
        executeAction(cmd);
    }
}

void BehaviorEngine::executeAction(const PetActionCommand &cmd)
{
    m_currentAction = cmd.type;
    m_actionEndTime = QDateTime::currentMSecsSinceEpoch() + (cmd.durationMs > 0 ? cmd.durationMs : 4000);

    auto execInMainThread = [this, cmd]() {
        ShijimaWidget *target = m_activeWidget;
        if (target == nullptr) {
            auto &list = ShijimaManager::defaultManager()->mascots();
            if (!list.empty()) {
                target = list.front();
            }
        }

        if (target != nullptr) {
            target->doAction(cmd);
        }
    };

    if (QCoreApplication::instance() != nullptr) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), execInMainThread, Qt::QueuedConnection);
    } else {
        execInMainThread();
    }
}

void BehaviorEngine::recordUserInteraction()
{
    m_state.lastInteractionTime = QDateTime::currentMSecsSinceEpoch();
    m_state.mood = std::clamp(m_state.mood + 1, -100, 100);
}

void BehaviorEngine::addAffection(int delta, int moodDelta)
{
    m_state.affection = std::clamp(m_state.affection + delta, 0, 100);
    if (moodDelta != 0) {
        m_state.mood = std::clamp(m_state.mood + moodDelta, -100, 100);
    }
    m_state.boredom = std::max(0, m_state.boredom - (delta * 5));
    m_state.lastInteractionTime = QDateTime::currentMSecsSinceEpoch();
    m_state.clamp();
    std::cout << "[互动亲密度] 亲密度 " << (delta >= 0 ? "+" : "") << delta 
              << ", 当前亲密度: " << m_state.affection << "%, 心情: " << m_state.mood << std::endl;
}

bool BehaviorEngine::handlePetClickedWhileResting(ShijimaWidget *)
{
    return false;
}

bool BehaviorEngine::handlePetClickedInPoutMode(ShijimaWidget *)
{
    return false;
}

void BehaviorEngine::updateFallRecoverySequence(qint64 now)
{
    if (m_fallRecoveryPhase == FallRecoveryPhase::None || m_activeWidget == nullptr) return;

    auto env = m_activeWidget->mascot().state ? m_activeWidget->mascot().state->env : nullptr;
    if (!env) return;

    const auto &anchor = m_activeWidget->mascot().state->anchor;
    bool isOnFloor = (anchor.y >= (env->floor.y - 25.0));

    switch (m_fallRecoveryPhase) {
        case FallRecoveryPhase::Falling:
            if (isOnFloor) {
                m_fallRecoveryPhase = FallRecoveryPhase::LieDownBreathing;
                m_fallPhaseStartTime = now;
                auto lie = m_activeWidget->mascot().initial_behavior_list().find("LieDown", false);
                if (lie != nullptr) m_activeWidget->mascot().next_behavior("LieDown");
            }
            break;
        case FallRecoveryPhase::LieDownBreathing:
            if ((now - m_fallPhaseStartTime) >= 2200) { // 趴在地上喘气 2.2 秒
                m_fallRecoveryPhase = FallRecoveryPhase::StandingUp;
                m_fallPhaseStartTime = now;
                auto stand = m_activeWidget->mascot().initial_behavior_list().find("StandUp", false);
                if (stand != nullptr) m_activeWidget->mascot().next_behavior("StandUp");
            }
            break;
        case FallRecoveryPhase::StandingUp:
            if ((now - m_fallPhaseStartTime) >= 1200) { // 爬起来拍拍灰 1.2 秒
                m_fallRecoveryPhase = FallRecoveryPhase::SittingResting;
                m_fallPhaseStartTime = now;
                auto sit = m_activeWidget->mascot().initial_behavior_list().find("SitDown", false);
                if (sit != nullptr) m_activeWidget->mascot().next_behavior("SitDown");
            }
            break;
        case FallRecoveryPhase::SittingResting:
            // 平稳进入休整
            break;
        default:
            break;
    }
}

void BehaviorEngine::onTick()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    ShijimaWidget *target = m_activeWidget;
    if (target == nullptr) {
        auto &list = ShijimaManager::defaultManager()->mascots();
        if (!list.empty()) {
            target = list.front();
            m_activeWidget = target;
        }
    }

    // 1. 体力衰减与恢复动态计算
    if (target != nullptr) {
        QString curBehavior = target->currentBehaviorName();
        updateStamina(curBehavior);
        updateFallRecoverySequence(now);
        target->update(); // 触发头顶微盘实时刷新
    }

    // 2. 寂寞感温和衰减：超过 3 分钟无互动，每分钟心情 -1，最低保持在 35 分（健康基准，绝不下探到极低负分）
    static qint64 s_lastMoodDecayTime = 0;
    if (s_lastMoodDecayTime == 0) s_lastMoodDecayTime = now;
    if ((now - m_state.lastInteractionTime) >= 180000) {
        if ((now - s_lastMoodDecayTime) >= 60000) {
            s_lastMoodDecayTime = now;
            m_state.mood = std::clamp(m_state.mood - 1, 35, 100);
        }
    } else {
        s_lastMoodDecayTime = now;
    }

    // 3. 检查长时间驻留类彩蛋 (如 Chrome > 15分钟)
    PetActionCommand dwellCmd;
    int dwellMoodDelta = 0;
    if (ReactionEngine::instance()->checkContinuousDwell(dwellCmd, dwellMoodDelta)) {
        m_state.mood += dwellMoodDelta;
        m_state.clamp();
        executeAction(dwellCmd);
    }

    // 4. 检查是否正在执行专属外部长动作
    if (now < m_actionEndTime) {
        return;
    }

    // 弹窗展示中 或 等待 Agent 执行时：绝不主动打断原地姿态，保证用户稳定复制阅读
    if (target != nullptr) {
        if (target->isWaitingForAgent()) return;
        if (target->messageBubble() != nullptr && target->messageBubble()->hasMessage() && !target->messageBubble()->isCompactCuteMode()) {
            return;
        }
    }

    // =========================================================================
    // 5. 灵动桌面感知与自然巡逻 (不强制干涉 Shimeji 原生探索动作)
    // =========================================================================
    // 允许 Shimeji 原生状态机自由探索 (爬墙、天花板爬行、掉落、奔跑、散步)
    // 仅当桌宠长时间静止 (超过 8 秒)，轻微唤醒它继续巡逻探索
    if (target != nullptr) {
        static int s_idleNudgeTimer = 0;
        QString curBehavior = target->currentBehaviorName();
        bool isMoving = (
            curBehavior.contains("Walk", Qt::CaseInsensitive) ||
            curBehavior.contains("Run", Qt::CaseInsensitive) ||
            curBehavior.contains("Climb", Qt::CaseInsensitive) ||
            curBehavior.contains("Crawl", Qt::CaseInsensitive) ||
            curBehavior.contains("Jump", Qt::CaseInsensitive) ||
            curBehavior.contains("Fall", Qt::CaseInsensitive) ||
            curBehavior.contains("Throw", Qt::CaseInsensitive)
        );

        if (isMoving) {
            s_idleNudgeTimer = 0;
        } else {
            s_idleNudgeTimer++;
            // 原地发呆超过 8 秒 (200 ticks，每次 40ms)，给它一个轻盈随机动机
            if (s_idleNudgeTimer >= 200) {
                s_idleNudgeTimer = 0;
                auto env = target->env();
                auto state = target->mascot().state;
                if (env && state) {
                    bool onFloor = (state->anchor.y >= (env->floor.y - 25.0));
                    bool onWindowCeiling = (env->active_ie.visible() && std::abs(state->anchor.y - env->active_ie.top) <= 20.0);
                    if (onFloor) {
                        int r = QRandomGenerator::global()->bounded(100);
                        if (r < 35) target->mascot().next_behavior("WalkAlongWorkAreaFloor");
                        else if (r < 65) target->mascot().next_behavior("RunAlongWorkAreaFloor");
                        else if (r < 85) target->mascot().next_behavior("WalkAndGrabBottomLeftWall");
                        else target->mascot().next_behavior("WalkAndGrabBottomRightWall");
                    } else if (onWindowCeiling) {
                        int r = QRandomGenerator::global()->bounded(100);
                        if (r < 50) target->mascot().next_behavior("WalkAlongIECeiling");
                        else if (r < 80) target->mascot().next_behavior("RunAlongIECeiling");
                        else target->mascot().next_behavior("SitWhileDanglingLegs");
                    }
                }
            }
        }
    }

    // 6. 适度主动闲聊（高颜值紧凑气泡，零失焦）
    checkInitiativeChat();
}

void BehaviorEngine::updateStamina(const QString &curBehavior)
{
    // 简明而充满活力的生命力模型：
    // 桌宠运动时微量波动，静止或坐下时迅速回充，体力始终充沛健康（70%~100%）
    static int s_recoveryTick = 0;
    s_recoveryTick++;

    bool isMovingActive = (
        curBehavior.contains("Climb", Qt::CaseInsensitive) ||
        curBehavior.contains("Run", Qt::CaseInsensitive) ||
        curBehavior.contains("Jump", Qt::CaseInsensitive)
    );

    if (isMovingActive) {
        if (s_recoveryTick % 5 == 0 && m_state.stamina > 70) {
            m_state.stamina--;
        }
    } else {
        // 静止、端坐、发呆、看鼠标时，每秒自然恢复 2%
        if (s_recoveryTick % 2 == 0 && m_state.stamina < 100) {
            m_state.stamina = std::min(100, m_state.stamina + 1);
        }
    }
}

void BehaviorEngine::triggerRestInCorner()
{
}

void BehaviorEngine::triggerStaminaRecovered()
{
}

void BehaviorEngine::evaluateUtilityAI()
{
}

void BehaviorEngine::checkInitiativeChat()
{
    if (m_isThinking) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    int userIdleSec = static_cast<int>((now - m_state.lastInteractionTime) / 1000);

    QString activeApp = SystemObserver::instance()->currentActiveAppName();
    QString windowTitle = SystemObserver::instance()->currentActiveWindowTitle();
    int workMins = SystemObserver::instance()->continuousWorkMinutes();
    bool isMusicPlaying = MusicPlayerManager::instance()->isPlaying();

    QJsonObject semanticContext = SystemObserver::instance()->currentSemanticActivity();
    QJsonObject prevWork = SensorManager::instance()->previousWorkContext();

    bool isContextRecovery = false;
    if (!prevWork.isEmpty()) {
        QString prevApp = prevWork["app_name"].toString();
        // 如果当前回到了高专注工作应用（如 Cursor, VSCode），且与上一工作区一致，而刚才曾切走
        if (!prevApp.isEmpty() && prevApp == activeApp && semanticContext["focus_level"].toString() == "high") {
            qint64 prevTime = prevWork["timestamp"].toVariant().toLongLong();
            // 距离上次离开在 1~30 分钟内切回，判定为打断后重返工作
            qint64 elapsedSec = (now - prevTime) / 1000;
            if (elapsedSec >= 60 && elapsedSec <= 1800) {
                isContextRecovery = true;
            }
        }
    }

    QJsonObject contextInfo;
    contextInfo["user_idle_seconds"] = userIdleSec;
    contextInfo["mood"] = m_state.mood;
    contextInfo["energy"] = m_state.energy;
    contextInfo["boredom"] = m_state.boredom;
    contextInfo["affection"] = m_state.affection;
    contextInfo["active_app"] = activeApp;
    contextInfo["window_title"] = windowTitle;
    contextInfo["work_minutes"] = workMins;
    contextInfo["is_music_playing"] = isMusicPlaying;
    contextInfo["hour"] = QTime::currentTime().hour();
    contextInfo["app_semantic_context"] = semanticContext;
    contextInfo["is_context_recovery"] = isContextRecovery;

    // 检查夜间自动写日记
    PetDiaryManager::instance()->checkAutoNightDiary();

    int score = 0;
    QString reason;
    if (InitiativeTrigger::instance()->evaluateInitiative(m_state, contextInfo, score, reason)) {
        m_isThinking = true;
        contextInfo["trigger_reason"] = reason;
        contextInfo["score"] = score;

        AgentService::instance()->requestPetIntent(contextInfo, [this](bool success, const AIBehaviorIntent &intent) {
            m_isThinking = false;
            if (success) {
                InitiativeTrigger::instance()->recordTalkSuccess(m_state);

                PetActionCommand cmd;
                cmd.type = PetActionType::Talk;
                cmd.speechText = intent.speech;
                cmd.durationMs = 6500;
                cmd.moveToCenter = false;

                // 解析情绪徽章与物理动作
                PetEmoteType emoteType = PetEmoteType::None;
                if (intent.emote == "💖" || intent.emotion == "happy") emoteType = PetEmoteType::HappyHeart;
                else if (intent.emote == "✨" || intent.intent == "celebrate") emoteType = PetEmoteType::Sparkle;
                else if (intent.emote == "💤" || intent.intent == "sleepy") emoteType = PetEmoteType::SleepZzz;
                else if (intent.emote == "💢" || intent.emotion == "angry") emoteType = PetEmoteType::AngryVein;
                else if (intent.emote == "💫") emoteType = PetEmoteType::DizzySwirl;
                else if (intent.emote == "💡") emoteType = PetEmoteType::ThinkingBulb;
                else if (intent.emote == "🎵") emoteType = PetEmoteType::MusicNote;

                cmd.emote = emoteType;

                // 配合 AI 意图生动执行对应小动作与 Q 弹形变
                if (m_activeWidget != nullptr) {
                    if (intent.action == "jump" || intent.action == "bounce") {
                        m_activeWidget->motionController().triggerStretch(0.90f, 1.15f);
                    }
                    if (intent.blush) {
                        m_activeWidget->motionController().spawnHeart(QPointF(0, -20.0f));
                    }
                    if (emoteType != PetEmoteType::None) {
                        m_activeWidget->motionController().triggerEmote(emoteType, 3.0f);
                    }

                    auto &mascot = m_activeWidget->mascot();
                    auto env = mascot.state ? mascot.state->env : nullptr;
                    if (env && mascot.state->anchor.y >= (env->floor.y - 25.0)) {
                        if (intent.action == "dangle" || intent.emotion == "happy" || intent.intent == "celebrate") {
                            mascot.next_behavior("SitWhileDanglingLegs");
                        } else if (intent.action == "sleep" || intent.emotion == "sleepy") {
                            mascot.next_behavior("LieDown");
                        } else if (intent.emotion == "curious" || intent.intent == "seek_attention") {
                            mascot.next_behavior("SitAndFaceMouse");
                        } else if (intent.emotion == "bored") {
                            mascot.next_behavior("SitAndSpinHead");
                        } else if (intent.intent == "explore" || intent.action == "walk") {
                            mascot.next_behavior("WalkAlongWorkAreaFloor");
                        }
                    }
                }

                PetDiaryManager::instance()->recordChat();
                executeAction(cmd);
            }
        });
    }
}

