#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QPointF>
#include <QString>
#include <vector>

class ShijimaWidget;
class TrashTargetWidget;

enum class EliminationMethod {
    MeteorSquash = 0,   // 泰山压顶 (从高处落下砸扁)
    BlackHole = 1,      // 黑洞放逐 (虚空维度吞噬)
    KnockoutTackle = 2, // 极速冲撞 (棒球式轰飞出屏幕)
    IllusionPop = 3     // 幻影破灭 (气球戳破彩屑)
};

class CloneEliminationSequence : public QObject {
public:
    static CloneEliminationSequence *instance();

    // 启动淘汰序列。若 targetClone 为空则自动挑选一个克隆体；若 chainAll 为 true 则依次淘汰所有克隆体直至只留主宠
    void start(ShijimaWidget *mainPet, ShijimaWidget *targetClone = nullptr, bool chainAll = false);
    bool isRunning() const { return m_running; }
    void stop();

private:
    CloneEliminationSequence();
    ~CloneEliminationSequence() override;

    void step();
    void finishCurrent(bool success);
    void pickNextTarget();
    EliminationMethod chooseMethod();
    QString getCombatDialogue(EliminationMethod method, bool isStart);

    // 追逐阶段与各预设动作的具体步进
    void stepChasePhase();
    void startEliminationExecution();
    void stepMeteorSquash();
    void stepBlackHole();
    void stepKnockoutTackle();
    void stepIllusionPop();

    bool m_running = false;
    bool m_chainAll = false;
    EliminationMethod m_currentMethod = EliminationMethod::MeteorSquash;

    // 追逐阶段控制
    bool m_inChasePhase = true;
    int m_chaseStage = 0;
    int m_chaseTicks = 0;
    double m_cloneVelocityX = 0.0;

    int m_stage = 0;
    int m_stageTicks = 0;

    QPointer<ShijimaWidget> m_mainPet;
    QPointer<ShijimaWidget> m_targetClone;
    QPointer<TrashTargetWidget> m_blackHoleWidget;

    QTimer *m_tickTimer = nullptr;

    // 动量与位置临时状态
    QPointF m_initialMainPos;
    QPointF m_targetPos;
    double m_velocityY = 0.0;
    double m_velocityX = 0.0;
    int m_methodCycleIndex = 0;
};
