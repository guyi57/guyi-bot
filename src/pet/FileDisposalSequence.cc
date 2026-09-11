#include "FileDisposalSequence.hpp"
#include "ShijimaWidget.hpp"
#include "FloatingFileWidget.hpp"
#include "TrashTargetWidget.hpp"
#include "BehaviorEngine.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <cmath>
#include <iostream>

#include <QFile>

FileDisposalSequence* FileDisposalSequence::instance() {
    static FileDisposalSequence s_instance;
    return &s_instance;
}

FileDisposalSequence::FileDisposalSequence() {
    m_tickTimer = new QTimer(this);
    connect(m_tickTimer, &QTimer::timeout, this, &FileDisposalSequence::step);
}

FileDisposalSequence::~FileDisposalSequence() {
    if (m_tickTimer) {
        m_tickTimer->stop();
    }
}

void FileDisposalSequence::start(ShijimaWidget *pet, const QString &fileName, const QString &realFilePath, const QPointF &customSpawnPos) {
    if (!pet || m_running) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastStartTime < 3500) {
        return; // 3.5秒防重复启动
    }
    m_lastStartTime = now;

    m_pet = pet;
    m_fileName = fileName.isEmpty() ? "cache_trash.tmp" : fileName;
    m_realFilePath = realFilePath;
    m_running = true;
    m_stage = 0;
    m_stageTickCount = 0;

    auto screen = QGuiApplication::primaryScreen();
    QRect screenGeom = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);

    // 1. 确定垃圾文件的真实屏幕坐标
    double fileX = 0;
    double fileY = 0;

    if (customSpawnPos.x() > 0 && customSpawnPos.y() > 0) {
        fileX = customSpawnPos.x();
        fileY = customSpawnPos.y();
    } else {
        // 尝试使用当前鼠标光标位置（用户刚刚操作的位置）
        QPoint cursorPos = QCursor::pos();
        if (screenGeom.contains(cursorPos)) {
            fileX = cursorPos.x();
            fileY = cursorPos.y();
        } else {
            // 默认屏幕中央区域
            fileX = screenGeom.left() + screenGeom.width() * 0.55;
            fileY = screenGeom.top() + screenGeom.height() * 0.65;
        }
    }

    // 边界安全收缩
    fileX = std::max<double>(screenGeom.left() + 180, std::min<double>(screenGeom.right() - 180, fileX));
    fileY = std::max<double>(screenGeom.top() + 120, std::min<double>(screenGeom.bottom() - 120, fileY));
    m_fileSpawnPos = QPointF(fileX, fileY);

    // 2. 智能就近撕裂黑洞：根据文件所在屏幕左右半区决定黑洞方位
    int screenMidX = screenGeom.left() + screenGeom.width() / 2;
    if (fileX > screenMidX) {
        m_pushingToRight = false; // 向左推
        double holeX = fileX - 175;
        m_blackHolePos = QPointF(holeX, fileY);
        m_petTargetPos = QPointF(fileX + 65, fileY);
    } else {
        m_pushingToRight = true; // 向右推
        double holeX = fileX + 175;
        m_blackHolePos = QPointF(holeX, fileY);
        m_petTargetPos = QPointF(fileX - (pet->width() + 15), fileY);
    }

    // 3. 在文件位置展开浮动文件挂件
    m_fileWidget = new FloatingFileWidget(m_fileName);
    m_fileWidget->spawnAt(m_fileSpawnPos);

    // 4. 在文件侧方撕裂黑洞
    m_trashWidget = new TrashTargetWidget();
    m_trashWidget->showAt(m_blackHolePos);

    // 暂停日常自发漫游逻辑，转由本序列接管
    pet->m_paused = true;
    pet->showMessage(QString("👀 发现待处理文件！正在全速赶往现场~"), 2000);

    // 启动序列定时器 (30ms = ~33fps 丝滑刷新)
    m_tickTimer->start(30);
}

void FileDisposalSequence::step() {
    if (!m_pet || !m_running) {
        finish();
        return;
    }

    auto state = m_pet->mascot().state;
    if (!state) {
        finish();
        return;
    }

    m_stageTickCount++;

    // =========================================================================
    // 阶段 0: 跨屏幕大跳跃飞奔接近文件位置 (Jumping & Leaping Approach)
    // =========================================================================
    if (m_stage == 0) {
        double curPetX = m_pet->x();
        double curPetY = m_pet->y();
        double dx = m_petTargetPos.x() - curPetX;
        double dy = m_petTargetPos.y() - curPetY;

        state->looking_right = (dx > 0);

        // 动感空中跳跃与跨步帧循环 (shime22.png 大跳，shime2~3 跑动)
        int frameCycle = (m_stageTickCount / 3) % 4;
        if (frameCycle == 0) state->active_frame.name = "/shime22.png"; // 空中大跳
        else if (frameCycle == 1) state->active_frame.name = "/shime2.png";
        else if (frameCycle == 2) state->active_frame.name = "/shime22.png";
        else state->active_frame.name = "/shime3.png";
        state->active_frame.right_name = "";

        // 动感跳跃曲线飞向文件
        double stepSpeedX = std::min(16.0, std::abs(dx));
        double stepSpeedY = std::min(14.0, std::abs(dy));
        if (std::abs(dx) > 1.0) state->anchor.x += (dx > 0 ? stepSpeedX : -stepSpeedX);
        if (std::abs(dy) > 1.0) state->anchor.y += (dy > 0 ? stepSpeedY : -stepSpeedY);

        // 叠加上下跳跃弧线
        double jumpBounce = std::sin(m_stageTickCount * 0.35) * 16.0;
        state->anchor.y -= jumpBounce;

        bool reachedPetTarget = (std::abs(dx) <= 20.0 && std::abs(dy) <= 25.0);
        if (reachedPetTarget || m_stageTickCount >= 85) { // 约 2.5s 超时强制就位
            m_stage = 1;
            m_stageTickCount = 0;
            state->anchor.y = m_petTargetPos.y();
            m_pet->showMessage(m_pushingToRight ? "🦵 连环飞踢推进黑洞！>>>" : "<<< 🦵 连环飞踢推进黑洞！", 1500);
        }
    }
    // =========================================================================
    // 阶段 1: 踢腿就位准备 (Kicking Leg Pose Setup)
    // =========================================================================
    else if (m_stage == 1) {
        state->looking_right = m_pushingToRight;
        state->anchor.y = m_petTargetPos.y();
        state->active_frame.name = "/shime30.png"; // 坐姿抬腿准备
        state->active_frame.right_name = "";

        if (m_stageTickCount >= 8) { // 240ms 准备完毕
            m_stage = 2;
            m_stageTickCount = 0;
        }
    }
    // =========================================================================
    // 阶段 2: 连环踢腿推文件向前移动 (Dynamic Kicking Push)
    // =========================================================================
    else if (m_stage == 2) {
        state->looking_right = m_pushingToRight;
        state->anchor.y = m_petTargetPos.y();
        
        // 连环踢腿帧切换 (shime31.png 伸腿, shime32.png 蹬腿, shime33.png 踹击, shime37.png 飞踢)
        int kickIndex = (m_stageTickCount / 3) % 4;
        if (kickIndex == 0) state->active_frame.name = "/shime31.png";
        else if (kickIndex == 1) state->active_frame.name = "/shime32.png";
        else if (kickIndex == 2) state->active_frame.name = "/shime33.png";
        else state->active_frame.name = "/shime37.png";
        state->active_frame.right_name = "";

        double pushSpeed = 5.5;
        state->anchor.x += (m_pushingToRight ? pushSpeed : -pushSpeed);

        // 将文件平移绑定在桌宠踢腿的前方
        if (m_fileWidget) {
            int handX = m_pushingToRight ? (m_pet->x() + m_pet->width() - 10) : (m_pet->x() - 55);
            int handY = m_pet->y() + (m_pet->height() / 2) - 10;
            m_fileWidget->attachToScreen(QPointF(handX, handY));
        }

        // 判定是否推进到了黑洞事件视界边缘
        double curFileX = m_fileWidget ? m_fileWidget->pos().x() : m_pet->x();
        bool reachedHole = false;
        if (m_pushingToRight) {
            reachedHole = (curFileX >= m_blackHolePos.x() - 40);
        } else {
            reachedHole = (curFileX <= m_blackHolePos.x() + 20);
        }

        if (reachedHole || m_stageTickCount >= 65) {
            m_stage = 3;
            m_stageTickCount = 0;
        }
    }
    // =========================================================================
    // 阶段 3: 终极飞踢将文件踢入黑洞视界 (Final Kick Toss & absorb)
    // =========================================================================
    else if (m_stage == 3) {
        state->anchor.y = m_petTargetPos.y();
        state->active_frame.name = "/shime37.png"; // 终极暴扣大飞踢
        state->active_frame.right_name = "";

        if (m_stageTickCount == 1) {
            // 触发文件旋转缩小并被黑洞引力吸入
            if (m_fileWidget) {
                m_fileWidget->tossTo(m_blackHolePos, [this]() {
                    if (m_trashWidget) {
                        m_trashWidget->playAbsorbEffect();
                    }
                    if (!m_realFilePath.isEmpty() && QFile::exists(m_realFilePath)) {
                        QFile::moveToTrash(m_realFilePath);
                    }
                    m_fileWidget = nullptr;
                });
            }
        }

        if (m_stageTickCount >= 22) { // 约 660ms 吞噬吸收完成
            m_stage = 4;
            m_stageTickCount = 0;
            m_pet->showMessage(QString("✨ 🕳️《%1》已被黑洞吞噬移入系统废纸篓！").arg(m_fileName), 3000);
        }
    }
    // =========================================================================
    // 阶段 4: 庆祝与收尾恢复
    // =========================================================================
    else if (m_stage == 4) {
        state->anchor.y = m_petTargetPos.y();
        // 庆祝转头摇头 (shime26~29.png)
        int spinIdx = (m_stageTickCount / 4) % 4;
        if (spinIdx == 0) state->active_frame.name = "/shime26.png";
        else if (spinIdx == 1) state->active_frame.name = "/shime27.png";
        else if (spinIdx == 2) state->active_frame.name = "/shime28.png";
        else state->active_frame.name = "/shime29.png";
        state->active_frame.right_name = "";

        if (m_stageTickCount >= 28) {
            finish();
            return;
        }
    }

    // 核心保证：直接根据 active_frame 平移重绘，彻底避免底层 Fall 覆盖！
    if (m_pet) {
        m_pet->updateOffsets();
        m_pet->update();
    }
}

void FileDisposalSequence::finish() {
    m_tickTimer->stop();
    m_running = false;
    m_stage = 0;
    m_stageTickCount = 0;

    if (m_fileWidget) {
        m_fileWidget->close();
        m_fileWidget = nullptr;
    }

    if (m_trashWidget) {
        m_trashWidget->dismiss();
        m_trashWidget = nullptr;
    }

    if (m_pet) {
        if (!m_pet->trySetBehavior("SitAndFaceMouse")) {
            m_pet->trySetBehavior("StandUp");
        }
        m_pet->m_paused = false;
        m_pet = nullptr;
    }
}
