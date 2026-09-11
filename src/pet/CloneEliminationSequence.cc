#include "CloneEliminationSequence.hpp"
#include "ShijimaWidget.hpp"
#include "ShijimaManager.hpp"
#include "TrashTargetWidget.hpp"
#include "BehaviorEngine.hpp"
#include "PersonaManager.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <QRandomGenerator>
#include <cmath>
#include <iostream>

CloneEliminationSequence *CloneEliminationSequence::instance() {
    static CloneEliminationSequence s_instance;
    return &s_instance;
}

CloneEliminationSequence::CloneEliminationSequence() {
    m_tickTimer = new QTimer(this);
    connect(m_tickTimer, &QTimer::timeout, this, &CloneEliminationSequence::step);
}

CloneEliminationSequence::~CloneEliminationSequence() {
    stop();
}

void CloneEliminationSequence::start(ShijimaWidget *mainPet, ShijimaWidget *targetClone, bool chainAll) {
    if (m_running) {
        return;
    }
    if (!mainPet) {
        mainPet = ShijimaManager::defaultManager()->mainPet();
    }
    if (!mainPet) {
        return;
    }

    m_mainPet = mainPet;
    m_chainAll = chainAll;

    if (!targetClone) {
        auto clones = ShijimaManager::defaultManager()->clonePets();
        for (auto *c : clones) {
            if (c && c != mainPet && !c->isMarkedForDeletion()) {
                targetClone = c;
                break;
            }
        }
    }

    if (!targetClone || targetClone == mainPet) {
        return;
    }

    m_targetClone = targetClone;
    m_running = true;
    m_inChasePhase = true;
    m_chaseStage = 0;
    m_chaseTicks = 0;
    m_stage = 0;
    m_stageTicks = 0;
    m_currentMethod = chooseMethod();

    // 暂停两者的自主 tick，由本序列接管
    m_mainPet->m_paused = true;
    m_targetClone->m_paused = true;

    m_initialMainPos = QPointF(m_mainPet->x(), m_mainPet->y());
    m_targetPos = QPointF(m_targetClone->x(), m_targetClone->y());

    // 确定逃跑初始方向 (克隆体远离主宠)
    double dir = (m_targetClone->x() >= m_mainPet->x()) ? 1.0 : -1.0;
    m_cloneVelocityX = dir * 7.5;

    // 追逐启幕：双向戏剧化喊话台词（主宠 vs 克隆体），统一采用萌系轻量气泡
    static const QStringList mainCatchLines = {
        "抓住你啦！桌面只要一个我就够了！🐾",
        "站住！不许抢主人的镜头！⚡",
        "休想逃跑！本尊来清理门户啦~ 💨",
        "克隆体别跑！看本尊把你收回！✨"
    };
    static const QStringList clonePanicLines = {
        "哇啊！本尊杀过来啦，快溜！💦",
        "救命啊！我才是本体！别追我呀~ 😱",
        "略略略~ 抓不到我抓不到我！💨",
        "别动手！大家都是同一个主人捏出来的！🏃‍♂️"
    };
    int rMain = QRandomGenerator::global()->bounded(static_cast<int>(mainCatchLines.size()));
    int rClone = QRandomGenerator::global()->bounded(static_cast<int>(clonePanicLines.size()));

    m_mainPet->showMessage(mainCatchLines[rMain], 2200, "", false, true);
    m_targetClone->showMessage(clonePanicLines[rClone], 2200, "", false, true);

    // 启动定时器 (30ms 刷新)
    m_tickTimer->start(30);
}

void CloneEliminationSequence::stop() {
    if (m_tickTimer && m_tickTimer->isActive()) {
        m_tickTimer->stop();
    }
    if (m_blackHoleWidget) {
        m_blackHoleWidget->dismiss();
        m_blackHoleWidget = nullptr;
    }
    if (m_mainPet) {
        m_mainPet->m_paused = false;
        m_mainPet->updateOffsets();
    }
    if (m_targetClone) {
        m_targetClone->m_paused = false;
    }
    m_running = false;
    m_inChasePhase = false;
}

EliminationMethod CloneEliminationSequence::chooseMethod() {
    // 循环轮换 4 种预设方式，保证丰富不单调
    int methodVal = m_methodCycleIndex % 4;
    m_methodCycleIndex++;
    return static_cast<EliminationMethod>(methodVal);
}

QString CloneEliminationSequence::getCombatDialogue(EliminationMethod method, bool isStart) {
    QString persona = PersonaManager::instance()->activePersonaId();

    if (isStart) {
        switch (method) {
        case EliminationMethod::MeteorSquash:
            if (persona == "catgirl") return "喵！锁定多余克隆体~ 看喵的高空俯冲！🐾";
            if (persona == "architect") return "【清理协议】检测到冗余副本，执行高空重力清除程序。";
            if (persona == "maid") return "主人，桌面有小分身抢镜头呢，这就为主人打扫干净~ ✨";
            return "锁定目标！准备天降正义，接招吧~ ⚡";

        case EliminationMethod::BlackHole:
            if (persona == "catgirl") return "喵呜！撕裂虚空，回平行宇宙去吧分身喵！🕳️";
            if (persona == "architect") return "【维度折跃】开启时空奇点，收容多余克隆体。";
            return "打开时空虫洞！克隆体引力放逐~ 🌌";

        case EliminationMethod::KnockoutTackle:
            if (persona == "catgirl") return "喵爪飞踢蓄力中！吃本喵一记超能冲撞！💨";
            return "全速冲刺！这块桌面太挤了，看招！🚀";

        case EliminationMethod::IllusionPop:
            if (persona == "catgirl") return "悄悄绕到背后… 戳戳！喵？🎈";
            return "悄悄接近… 假分身一戳就破！🌟";
        }
    } else {
        // 胜利结算台词
        switch (method) {
        case EliminationMethod::MeteorSquash:
            if (persona == "catgirl") return "看本喵的泰山压顶！这片屏幕只有本喵能当主角！💥";
            return "泰山压顶成功！压成小纸片退散啦~ 💥";

        case EliminationMethod::BlackHole:
            return "克隆体已送往平行宇宙，一路顺风~ 🌌";

        case EliminationMethod::KnockoutTackle:
            return "全垒打！走你~ 屏幕终于清静啦 ⚾";

        case EliminationMethod::IllusionPop:
            return "啵~ 假的就是假的，一戳就破！🎈";
        }
    }
    return "清理完成！✨";
}

void CloneEliminationSequence::step() {
    if (!m_mainPet || !m_targetClone || !m_running) {
        finishCurrent(false);
        return;
    }

    if (m_inChasePhase) {
        stepChasePhase();
        return;
    }

    m_stageTicks++;

    switch (m_currentMethod) {
    case EliminationMethod::MeteorSquash:
        stepMeteorSquash();
        break;
    case EliminationMethod::BlackHole:
        stepBlackHole();
        break;
    case EliminationMethod::KnockoutTackle:
        stepKnockoutTackle();
        break;
    case EliminationMethod::IllusionPop:
        stepIllusionPop();
        break;
    }
}

void CloneEliminationSequence::stepChasePhase() {
    auto mainState = m_mainPet->mascot().state;
    auto cloneState = m_targetClone->mascot().state;
    if (!mainState || !cloneState) {
        finishCurrent(false);
        return;
    }

    auto screen = QGuiApplication::primaryScreen();
    QRect screenGeom = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    int minX = screenGeom.left() + 50;
    int maxX = screenGeom.right() - 150;

    m_chaseTicks++;

    if (m_chaseStage == 0) {
        // 阶段 0: 惊觉与起跑准备 (约 0.6 秒)
        mainState->active_frame.name = "/shime20.png";
        mainState->active_frame.right_name = "";
        mainState->looking_right = (cloneState->anchor.x >= mainState->anchor.x);

        cloneState->active_frame.name = "/shime4.png";
        cloneState->active_frame.right_name = "";
        cloneState->looking_right = (m_cloneVelocityX > 0);
        m_targetClone->triggerEmote(PetEmoteType::DizzySwirl, 1.2f);

        if (m_chaseTicks >= 20) {
            m_chaseStage = 1;
            m_chaseTicks = 0;
        }
    } else if (m_chaseStage == 1) {
        // 阶段 1: 桌面全速追逐 (摆臂狂奔 + 边缘折返 + 中途喊话)
        int frameIdx = (m_chaseTicks / 4) % 3 + 1; // 轮换 1, 2, 3
        QString runFrame = QString("/shime%1.png").arg(frameIdx);
        mainState->active_frame.name = runFrame.toStdString();
        mainState->active_frame.right_name = "";
        cloneState->active_frame.name = runFrame.toStdString();
        cloneState->active_frame.right_name = "";

        // 克隆体奔跑与边界反弹
        cloneState->anchor.x += m_cloneVelocityX;
        if (cloneState->anchor.x >= maxX) {
            cloneState->anchor.x = maxX;
            m_cloneVelocityX = -std::abs(m_cloneVelocityX);
        } else if (cloneState->anchor.x <= minX) {
            cloneState->anchor.x = minX;
            m_cloneVelocityX = std::abs(m_cloneVelocityX);
        }
        cloneState->looking_right = (m_cloneVelocityX > 0);

        // 主宠全速追击 (速度更快，逐步逼近)
        double dx = cloneState->anchor.x - mainState->anchor.x;
        double mainSpeed = 11.0;
        double mainDir = (dx >= 0) ? 1.0 : -1.0;
        mainState->anchor.x += mainDir * mainSpeed;
        mainState->looking_right = (mainDir > 0);

        // 追逐途中喊话
        if (m_chaseTicks == 24) {
            m_targetClone->showMessage("呼呼… 跑不动了，腿要断了！🥵", 1800, "", false, true);
            m_mainPet->showMessage("哪里逃！看招——！⚡", 1800, "", false, true);
        }

        // 追上判定 (距离小于 65px 或超过 55 ticks)
        if (std::abs(cloneState->anchor.x - mainState->anchor.x) <= 65 || m_chaseTicks >= 55) {
            m_chaseStage = 2;
            m_chaseTicks = 0;
        }
    } else if (m_chaseStage == 2) {
        // 阶段 2: 逼停累瘫与求饶 (约 0.8 秒)
        mainState->active_frame.name = "/shime11.png"; // 气势汹汹叉腰
        mainState->active_frame.right_name = "";
        mainState->looking_right = (cloneState->anchor.x >= mainState->anchor.x);

        cloneState->active_frame.name = "/shime18.png"; // 瘫坐求饶
        cloneState->active_frame.right_name = "";
        cloneState->looking_right = (mainState->anchor.x >= cloneState->anchor.x);

        if (m_chaseTicks == 1) {
            static const QStringList surrenderLines = {
                "我投降！求轻点砸！呜呜呜~ 🏳️",
                "大侠饶命！我这就自我分解~ 😭",
                "本体威武！给留个全尸喵~ 🥺"
            };
            int rSur = QRandomGenerator::global()->bounded(static_cast<int>(surrenderLines.size()));
            m_targetClone->showMessage(surrenderLines[rSur], 2000, "", false, true);
            m_targetClone->triggerEmote(PetEmoteType::DizzySwirl, 2.0f);

            QString startLine = getCombatDialogue(m_currentMethod, true);
            m_mainPet->showMessage(startLine, 2200, "", false, true);
        }

        if (m_chaseTicks >= 26) {
            startEliminationExecution();
            return;
        }
    }

    m_mainPet->updateOffsets();
    m_mainPet->update();
    m_targetClone->updateOffsets();
    m_targetClone->update();
}

void CloneEliminationSequence::startEliminationExecution() {
    m_inChasePhase = false;
    m_stage = 0;
    m_stageTicks = 0;
    m_initialMainPos = QPointF(m_mainPet->x(), m_mainPet->y());
    m_targetPos = QPointF(m_targetClone->x(), m_targetClone->y());
}

// ----------------------------------------------------------------------------
// 方式 1: 泰山压顶 (从高处落下砸扁)
// ----------------------------------------------------------------------------
void CloneEliminationSequence::stepMeteorSquash() {
    auto mainState = m_mainPet->mascot().state;
    auto cloneState = m_targetClone->mascot().state;
    if (!mainState || !cloneState) { finishCurrent(false); return; }

    auto screen = QGuiApplication::primaryScreen();
    QRect screenGeom = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);

    // 阶段 0: 锁定与下蹲蓄力
    if (m_stage == 0) {
        mainState->active_frame.name = "/shime20.png"; // 下蹲蓄力
        mainState->active_frame.right_name = "";
        mainState->looking_right = (cloneState->anchor.x >= mainState->anchor.x);
        m_targetClone->triggerEmote(PetEmoteType::DizzySwirl, 1.5f);

        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (m_stageTicks >= 15) {
            m_stage = 1;
            m_stageTicks = 0;
            m_velocityY = -36.0; // 极速起跳冲上高空
        }
    }
    // 阶段 1: 冲天而起，飞向屏幕顶端高空
    else if (m_stage == 1) {
        mainState->active_frame.name = "/shime22.png"; // 空中飞跃
        mainState->anchor.y += m_velocityY;
        m_velocityY += 1.2; // 微量重力衰减，保持冲出高空

        // 同步向克隆体水平坐标靠拢
        double dx = cloneState->anchor.x - mainState->anchor.x;
        mainState->anchor.x += (dx > 0 ? std::min(18.0, dx) : std::max(-18.0, dx));
        mainState->looking_right = (dx >= 0);

        m_mainPet->updateOffsets();
        m_mainPet->update();

        // 升至屏幕高位
        if (mainState->anchor.y <= screenGeom.top() + 60 || m_stageTicks >= 25) {
            m_stage = 2;
            m_stageTicks = 0;
            m_velocityY = 6.0; // 开始转为急坠
        }
    }
    // 阶段 2: 空中瞄准与锁定克隆体
    else if (m_stage == 2) {
        mainState->active_frame.name = "/shime22.png";
        // 精确对齐克隆体 X
        mainState->anchor.x = cloneState->anchor.x;
        cloneState->active_frame.name = "/shime4.png"; // 克隆体抬头惊恐
        cloneState->active_frame.right_name = "";

        m_mainPet->updateOffsets();
        m_mainPet->update();
        m_targetClone->updateOffsets();
        m_targetClone->update();

        if (m_stageTicks >= 8) {
            m_stage = 3;
            m_stageTicks = 0;
            m_velocityY = 18.0; // 极速俯冲初速度
        }
    }
    // 阶段 3: 泰山压顶急速下坠！
    else if (m_stage == 3) {
        mainState->active_frame.name = "/shime22.png";
        m_velocityY += 4.5; // 重力大加速度急速俯冲
        mainState->anchor.y += m_velocityY;
        mainState->anchor.x = cloneState->anchor.x;

        m_mainPet->updateOffsets();
        m_mainPet->update();

        // 撞击判定：主宠触碰到克隆体高度
        if (mainState->anchor.y >= cloneState->anchor.y - 10) {
            mainState->anchor.y = cloneState->anchor.y;
            m_stage = 4;
            m_stageTicks = 0;

            // 撞击特效：克隆体扁平化压成纸片
            m_targetClone->motionController().triggerSquash(2.5f, 0.10f);
            m_targetClone->triggerEmote(PetEmoteType::DizzySwirl, 2.0f);

            // 主宠反作用力 Q 弹拉伸弹起
            m_mainPet->motionController().triggerStretch(0.85f, 1.30f);
            m_mainPet->motionController().spawnSparkle(QPointF(0, -25));

            m_targetClone->updateOffsets();
            m_targetClone->update();
            m_mainPet->updateOffsets();
            m_mainPet->update();
        }
    }
    // 阶段 4: 砸扁消散与主宠落地胜利
    else if (m_stage == 4) {
        if (m_stageTicks == 5) {
            // 克隆体化为纸片消散，从屏幕移除
            m_targetClone->markForDeletion();
            QString winLine = getCombatDialogue(m_currentMethod, false);
            m_mainPet->showMessage(winLine, 3000, "", false, true);
            m_mainPet->motionController().spawnSparkle(QPointF(0, -20));
        }

        mainState->active_frame.name = "/shime1.png";
        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (m_stageTicks >= 20) {
            finishCurrent(true);
        }
    }
}

// ----------------------------------------------------------------------------
// 方式 2: 黑洞放逐 (虚空维度吞噬)
// ----------------------------------------------------------------------------
void CloneEliminationSequence::stepBlackHole() {
    auto mainState = m_mainPet->mascot().state;
    auto cloneState = m_targetClone->mascot().state;
    if (!mainState || !cloneState) { finishCurrent(false); return; }

    // 阶段 0: 主宠施法，克隆体下方撕裂黑洞
    if (m_stage == 0) {
        mainState->active_frame.name = "/shime11.png"; // 挥手施法
        mainState->active_frame.right_name = "";
        mainState->looking_right = (cloneState->anchor.x >= mainState->anchor.x);
        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (!m_blackHoleWidget) {
            m_blackHoleWidget = new TrashTargetWidget();
            QPointF holePos(m_targetClone->x() + m_targetClone->width() / 2,
                            m_targetClone->y() + m_targetClone->height() * 0.75);
            m_blackHoleWidget->showAt(holePos);
        }

        if (m_stageTicks >= 18) {
            m_stage = 1;
            m_stageTicks = 0;
        }
    }
    // 阶段 1: 克隆体被吸向黑洞中心，旋转并缩小
    else if (m_stage == 1) {
        m_targetClone->motionController().triggerSquash(0.65f, 0.65f);
        m_targetClone->triggerEmote(PetEmoteType::DizzySwirl, 1.0f);
        cloneState->anchor.y += 1.5; // 缓缓沉入黑洞

        m_targetClone->updateOffsets();
        m_targetClone->update();

        if (m_stageTicks >= 25) {
            m_stage = 2;
            m_stageTicks = 0;
            if (m_blackHoleWidget) {
                m_blackHoleWidget->playAbsorbEffect();
            }
        }
    }
    // 阶段 2: 吞噬湮灭与黑洞关闭
    else if (m_stage == 2) {
        if (m_stageTicks == 6) {
            m_targetClone->markForDeletion();
            if (m_blackHoleWidget) {
                m_blackHoleWidget->dismiss();
                m_blackHoleWidget = nullptr;
            }
            QString winLine = getCombatDialogue(m_currentMethod, false);
            m_mainPet->showMessage(winLine, 3000, "", false, true);
            m_mainPet->motionController().spawnSparkle(QPointF(0, -20));
        }

        mainState->active_frame.name = "/shime1.png";
        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (m_stageTicks >= 20) {
            finishCurrent(true);
        }
    }
}

// ----------------------------------------------------------------------------
// 方式 3: 极速冲撞 (棒球式轰飞出屏幕)
// ----------------------------------------------------------------------------
void CloneEliminationSequence::stepKnockoutTackle() {
    auto mainState = m_mainPet->mascot().state;
    auto cloneState = m_targetClone->mascot().state;
    if (!mainState || !cloneState) { finishCurrent(false); return; }

    auto screen = QGuiApplication::primaryScreen();
    QRect screenGeom = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);

    // 阶段 0: 冲刺蓄力
    if (m_stage == 0) {
        mainState->active_frame.name = "/shime20.png";
        mainState->looking_right = (cloneState->anchor.x >= mainState->anchor.x);
        m_mainPet->motionController().triggerEmote(PetEmoteType::AngryVein, 1.0f);
        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (m_stageTicks >= 12) {
            m_stage = 1;
            m_stageTicks = 0;
        }
    }
    // 阶段 1: 全速奔跑飞冲
    else if (m_stage == 1) {
        double dx = cloneState->anchor.x - mainState->anchor.x;
        bool toRight = (dx > 0);
        mainState->looking_right = toRight;

        int runFrame = (m_stageTicks / 2) % 2;
        mainState->active_frame.name = (runFrame == 0 ? "/shime2.png" : "/shime3.png");

        double speed = 24.0;
        mainState->anchor.x += (toRight ? speed : -speed);

        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (std::abs(dx) <= 30.0 || m_stageTicks >= 35) {
            m_stage = 2;
            m_stageTicks = 0;
            // 飞踢命中！赋予克隆体超高击飞速度
            m_velocityX = toRight ? 42.0 : -42.0;
            m_velocityY = -16.0;

            mainState->active_frame.name = "/shime22.png"; // 飞踢姿态
            m_mainPet->motionController().triggerStretch(1.25f, 0.85f);
            m_mainPet->motionController().spawnSparkle(QPointF(0, -15));

            m_targetClone->motionController().triggerSquash(0.8f, 1.2f);
            m_targetClone->triggerEmote(PetEmoteType::DizzySwirl, 2.0f);
        }
    }
    // 阶段 2: 克隆体呈抛物线高速轰飞出屏幕外
    else if (m_stage == 2) {
        cloneState->anchor.x += m_velocityX;
        cloneState->anchor.y += m_velocityY;
        m_velocityY += 2.0; // 重力

        m_targetClone->updateOffsets();
        m_targetClone->update();

        bool outOfScreen = (cloneState->anchor.x < screenGeom.left() - 100 ||
                            cloneState->anchor.x > screenGeom.right() + 100 ||
                            cloneState->anchor.y > screenGeom.bottom() + 100);

        if (outOfScreen || m_stageTicks >= 25) {
            m_targetClone->markForDeletion();
            m_stage = 3;
            m_stageTicks = 0;
            QString winLine = getCombatDialogue(m_currentMethod, false);
            m_mainPet->showMessage(winLine, 3000, "", false, true);
        }
    }
    // 阶段 3: 主宠收势落地
    else if (m_stage == 3) {
        mainState->active_frame.name = "/shime1.png";
        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (m_stageTicks >= 15) {
            finishCurrent(true);
        }
    }
}

// ----------------------------------------------------------------------------
// 方式 4: 幻影破灭 (气球戳破彩屑)
// ----------------------------------------------------------------------------
void CloneEliminationSequence::stepIllusionPop() {
    auto mainState = m_mainPet->mascot().state;
    auto cloneState = m_targetClone->mascot().state;
    if (!mainState || !cloneState) { finishCurrent(false); return; }

    // 阶段 0: 悄悄潜行接近
    if (m_stage == 0) {
        double dx = cloneState->anchor.x - mainState->anchor.x;
        mainState->looking_right = (dx > 0);
        mainState->active_frame.name = ((m_stageTicks / 4) % 2 == 0 ? "/shime2.png" : "/shime1.png");

        double approachSpeed = std::min(8.0, std::abs(dx));
        mainState->anchor.x += (dx > 0 ? approachSpeed : -approachSpeed);

        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (std::abs(dx) <= 25.0 || m_stageTicks >= 30) {
            m_stage = 1;
            m_stageTicks = 0;
        }
    }
    // 阶段 1: 伸爪一戳，克隆体剧烈膨胀颤抖
    else if (m_stage == 1) {
        mainState->active_frame.name = "/shime11.png"; // 伸手
        // 克隆体膨胀如气球
        float balloonScale = 1.0f + (m_stageTicks * 0.05f);
        m_targetClone->motionController().triggerStretch(balloonScale, balloonScale);
        m_targetClone->triggerEmote(PetEmoteType::DizzySwirl, 1.0f);

        m_mainPet->updateOffsets();
        m_mainPet->update();
        m_targetClone->updateOffsets();
        m_targetClone->update();

        if (m_stageTicks >= 12) {
            m_stage = 2;
            m_stageTicks = 0;
            // 砰！气球破灭，爆出彩色音符与彩屑
            m_targetClone->markForDeletion();
            m_mainPet->motionController().spawnSparkle(QPointF(20, -20));
            m_mainPet->motionController().spawnHeart(QPointF(15, -35));
            m_mainPet->motionController().spawnMusicNote(QPointF(25, -25));

            QString winLine = getCombatDialogue(m_currentMethod, false);
            m_mainPet->showMessage(winLine, 3000, "", false, true);
        }
    }
    // 阶段 2: 胜利摆动
    else if (m_stage == 2) {
        mainState->active_frame.name = "/shime1.png";
        m_mainPet->updateOffsets();
        m_mainPet->update();

        if (m_stageTicks >= 15) {
            finishCurrent(true);
        }
    }
}

void CloneEliminationSequence::finishCurrent(bool success) {
    if (m_tickTimer && m_tickTimer->isActive()) {
        m_tickTimer->stop();
    }
    if (m_blackHoleWidget) {
        m_blackHoleWidget->dismiss();
        m_blackHoleWidget = nullptr;
    }
    if (m_mainPet) {
        m_mainPet->m_paused = false;
        m_mainPet->updateOffsets();
    }
    if (m_targetClone && !m_targetClone->isMarkedForDeletion()) {
        m_targetClone->m_paused = false;
    }

    m_running = false;

    // 如果指定连续淘汰所有克隆体，检查是否还有剩余克隆体
    if (m_chainAll && success && m_mainPet) {
        pickNextTarget();
    }
}

void CloneEliminationSequence::pickNextTarget() {
    auto clones = ShijimaManager::defaultManager()->clonePets();
    ShijimaWidget *nextClone = nullptr;
    for (auto *c : clones) {
        if (c && c != m_mainPet && !c->isMarkedForDeletion()) {
            nextClone = c;
            break;
        }
    }

    if (nextClone) {
        // 间隔 1.2 秒后启动下一个克隆体淘汰，显得节奏自然生动
        QPointer<ShijimaWidget> pet = m_mainPet;
        QPointer<ShijimaWidget> target = nextClone;
        QTimer::singleShot(1200, this, [this, pet, target]() {
            if (pet && target && !target->isMarkedForDeletion()) {
                start(pet.data(), target.data(), true);
            }
        });
    }
}
