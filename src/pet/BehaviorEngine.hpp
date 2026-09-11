#pragma once

#include <QTimer>
#include <QPointer>
#include <memory>
#include "PetState.hpp"
#include "PetAction.hpp"
#include "PetEventBus.hpp"

class ShijimaWidget;

enum class MoodTier {
    ExtremelyLow, // -100 ~ -60 (闹别扭 / 背对 / 概率躲避)
    Low,          // -60 ~ -20  (委屈 / 幽怨 / 屏幕边缘低调)
    Medium,       // -20 ~ +30  (日常巡逻 / 偶尔看光标)
    High          // +30 ~ +100 (元气满满 / 追逐光标 / 活跃窗口攀爬)
};

enum class FallRecoveryPhase {
    None,
    Falling,
    LieDownBreathing, // 落地趴下喘气
    StandingUp,       // 爬起来拍拍灰
    SittingResting    // 原地坐下平稳回血
};

class BehaviorEngine
{
public:
    static BehaviorEngine *instance();

    void start();
    void stop();

    // 绑定当前处于前台交互的桌宠
    void setActiveWidget(ShijimaWidget *widget);
    ShijimaWidget *activeWidget() const { return m_activeWidget.data(); }

    PetState &state() { return m_state; }
    const PetState &state() const { return m_state; }

    MoodTier moodTier() const;

    // 强制触发一次动作指令
    void executeAction(const PetActionCommand &cmd);
    void recordUserInteraction();
    void addAffection(int delta, int moodDelta = 0);

    // 用户点击时的特定情境响应
    bool handlePetClickedWhileResting(ShijimaWidget *target);
    bool handlePetClickedInPoutMode(ShijimaWidget *target);

    // 跌倒/抛掷落地恢复序列
    void startFallRecovery(ShijimaWidget *widget);

    void onTick();
    void handleEvent(const PetEvent &event);

private:
    BehaviorEngine();
    void initEventListeners();
    void evaluateUtilityAI();
    void checkInitiativeChat();

    void updateStamina(const QString &currentBehavior);
    void updateFallRecoverySequence(qint64 now);
    void triggerRestInCorner();
    void triggerStaminaRecovered();

    QTimer *m_tickTimer;
    PetState m_state;
    QPointer<ShijimaWidget> m_activeWidget;
    PetActionType m_currentAction = PetActionType::Idle;
    qint64 m_actionEndTime = 0;
    bool m_isThinking = false;

    FallRecoveryPhase m_fallRecoveryPhase = FallRecoveryPhase::None;
    qint64 m_fallPhaseStartTime = 0;
};


