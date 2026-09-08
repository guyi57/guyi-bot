#pragma once

// 
// Shijima-Qt - Cross-platform shimeji simulation app for desktop
// Copyright (C) 2025 pixelomer
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
// 

#include <QWidget>
#include <memory>
#include <QRegion>
#include <QTimer>
#include <QVariantAnimation>
#include <QEasingCurve>
#include "Asset.hpp"
#include "SoundEffectManager.hpp"
#include <shijima/mascot/manager.hpp>
#include <shijima/mascot/environment.hpp>
#include "PlatformWidget.hpp"
#include "MascotData.hpp"
#include "MessageBubble.hpp"
#include "PetAction.hpp"

class QPushButton;
class QPaintEvent;
class QMouseEvent;
class QCloseEvent;
class ShijimaContextMenu;
class ShimejiInspectorDialog;
class SelectionToolbar;
class AskDialog;
class AgentSettingsDialog;
class PetStatusBarWidget;

class ShijimaWidget : public PlatformWidget<QWidget>
{
public:
    friend class ShijimaContextMenu;
    friend class FileDisposalSequence;
    explicit ShijimaWidget(MascotData *mascotData,
        std::unique_ptr<shijima::mascot::manager> mascot,
        int mascotId, bool windowedMode, QWidget *parent = nullptr);
    explicit ShijimaWidget(ShijimaWidget &old, bool windowedMode,
        QWidget *parent = nullptr);
    void tick();
    bool pointInside(QPoint const& point);
    int mascotId() { return m_mascotId; }
    void showInspector();
    void showAgentSettings();
    void showDiary();
    void markForDeletion() { m_markedForDeletion = true; }
    bool isMarkedForDeletion() const { return m_markedForDeletion; }
    bool inspectorVisible();
    bool paused() const { return m_paused || m_contextMenuVisible; }
    shijima::mascot::manager &mascot() {
        return *m_mascot;
    }
    void setEnv(std::shared_ptr<shijima::mascot::environment> env) {
        m_mascot->state->env = env;
    }
    std::shared_ptr<shijima::mascot::environment> env() {
        return m_mascot->state->env; 
    }
    MascotData *mascotData() {
        return m_data;
    }
    QString const& mascotName() {
        return m_data->name();
    }
    void showMessage(QString const& text, int duration = 0, QString const& appTarget = "", bool moveToCenter = false);
    void hideMessage();

    // 执行高阶动作指令
    void doAction(const PetActionCommand &cmd);
    bool trySetBehavior(const std::string &name);

    // 查询当前行为与导航至角落
    QString currentBehaviorName() const;
    void moveToCorner(bool toLeftCorner);

    // 划词操作与提问响应
    void onTranslateRequested(QString const& text);
    void onAskRequested(QString const& text);
    void onQuestionSubmitted(QString const& context, QString const& question);
    void showMessageHistory();
    void showTimerManager();
    void setWaitingForAgent(bool waiting);
    bool isWaitingForAgent() const { return m_isWaitingForAgent; }
    MessageBubble *messageBubble() const { return m_messageBubble; }

    PetMotionController &motionController() { return m_motion; }
    void triggerEmote(PetEmoteType type, float durationSec = 2.5f) { m_motion.triggerEmote(type, durationSec); }
    void triggerInteractionAI(const QString &interactionType);

    ~ShijimaWidget();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    void setDragTarget(ShijimaWidget *target);
    void snapToNearestBorderOrWindow();
    bool checkAndJumpToActiveIE();
    bool isMirroredRender() const;
    void closeAction();
    void contextMenuClosed(QCloseEvent *);
    void showContextMenu(QPoint const&);
    bool updateOffsets();
#ifdef __linux__
    QRegion m_windowMask;
#endif
    bool m_windowedMode;
    MascotData *m_data;
    ShimejiInspectorDialog *m_inspector;
    SoundEffectManager m_sounds;
    Asset const& getActiveAsset();
    ShijimaWidget *m_dragTarget = nullptr;
    ShijimaWidget **m_dragTargetPt = nullptr;
    std::unique_ptr<shijima::mascot::manager> m_mascot;
    QRect m_imageRect;
    QPoint m_anchorInWindow;
    double m_drawScale = 1.0;
    QPoint m_drawOrigin;
    int m_windowHeight;
    int m_windowWidth;
    bool m_visible;
    bool m_contextMenuVisible = false;
    bool m_paused = false;
    bool m_markedForDeletion = false;
    int m_mascotId;
    MessageBubble *m_messageBubble;
    QVariantAnimation *m_moveAnimation = nullptr;
    QString m_pendingMessageText;
    int m_pendingMessageDuration = 0;
    QString m_pendingAppTarget;
    bool m_isRunningToCenter = false;
    bool m_isWaitingForAgent = false;

    // 划词操作栏、提问弹窗、配置窗口与定时器窗口
    SelectionToolbar *m_selectionToolbar = nullptr;
    AskDialog *m_askDialog = nullptr;
    AgentSettingsDialog *m_settingsDialog = nullptr;
    class TimerListDialog *m_timerDialog = nullptr;
    class PetDiaryDialog *m_diaryDialog = nullptr;

    // 拖拽瞬时速度物理采样与抛物线动力学控制器
    void applyThrowPhysics(double vx, double vy);
    QPoint m_lastMousePos;
    QPoint m_dragStartGlobalPos;
    qint64 m_lastMouseMoveTime = 0;
    qint64 m_lastInteractionAITime = 0;
    double m_dragVelocityX = 0.0;
    double m_dragVelocityY = 0.0;
    QTimer *m_throwPhysicsTimer = nullptr;
    double m_throwVx = 0.0;
    double m_throwVy = 0.0;
    bool m_isThrowFlying = false;

    // 动作意图打断与小脾气感知
    enum class PetInterruptedGoal { None, ClimbingCeiling, JumpingWindow, StandingOnWindow, Breeding, RunningFast };
    PetInterruptedGoal detectCurrentGoal();
    void triggerTantrum(PetInterruptedGoal goal);
    void checkWindowVanished();
    PetInterruptedGoal m_interruptedGoal = PetInterruptedGoal::None;
    bool m_wasOnWindow = false;
    qint64 m_lastTantrumTime = 0;

    // 灵动运动与情绪控制器 (节拍律动、Squash&Stretch、表情贴纸与摸头)
    PetMotionController m_motion;
    qint64 m_lastMotionUpdateTime = 0;
    bool m_lastWasFalling = false;
};
