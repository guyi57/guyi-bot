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

#include "ShijimaWidget.hpp"
#include <QWidget>
#include <QPainter>
#include <QFile>
#include <QDir>
#include <QScreen>
#include <QMouseEvent>
#include <QMenu>
#include <QWindow>
#include <QDebug>
#include <QGuiApplication>
#include <QTextStream>
#include <QRandomGenerator>
#include <shijima/shijima.hpp>
#include "Platform/Platform.hpp"
#include "ShimejiInspectorDialog.hpp"
#include "AssetLoader.hpp"
#include "ShijimaContextMenu.hpp"
#include "ShijimaManager.hpp"
#include "SelectionToolbar.hpp"
#include "AskDialog.hpp"
#include "AgentService.hpp"
#include "LongTermMemoryEngine.hpp"
#include "AgentSettingsDialog.hpp"
#include "FileDisposalSequence.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include "TimerManager.hpp"
#include "TimerListDialog.hpp"
#include "AgentSettingsDialog.hpp"
#include "TranslateDialog.hpp"
#include "HotkeyManager.hpp"
#include "PetEventBus.hpp"
#include "PetAction.hpp"
#include "BehaviorEngine.hpp"
#include "ScoreBadgeWidget.hpp"
#include "PetStatusBarWidget.hpp"
#include "MusicPlayerManager.hpp"
#include "SettingsDb.hpp"
#include "SkillManager.hpp"
#include "MessageHistoryManager.hpp"
#include "PetDiaryDialog.hpp"
#include "SystemObserver.hpp"
#include <shimejifinder/utils.hpp>
#include <cmath>
#include <algorithm>

using namespace shijima;

ShijimaWidget::ShijimaWidget(MascotData *mascotData,
    std::unique_ptr<shijima::mascot::manager> mascot,
    int mascotId, bool windowedMode, QWidget *parent):
#if defined(__APPLE__)
    PlatformWidget(nullptr, PlatformWidget::ShowOnAllDesktops),
#else
    PlatformWidget(parent, PlatformWidget::ShowOnAllDesktops),
#endif
    m_windowedMode(windowedMode), m_data(mascotData),
    m_inspector(nullptr), m_mascotId(mascotId), m_messageBubble(nullptr)
{
    m_windowHeight = 128;
    m_windowWidth = 128;
    m_mascot = std::move(mascot);
    
    QDir dir { m_data->imgRoot() };
    if (dir.exists() && dir.cdUp() && dir.cd("sound")) {
        m_sounds.searchPaths.push_back(dir.path());
    }
    
    if (!m_windowedMode) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_MacShowFocusRect, false);
        Qt::WindowFlags flags = Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint
            | Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint
            | Qt::WindowOverridesSystemGestures;
        #if defined(__APPLE__)
        flags |= Qt::Window;
        #else
        flags |= Qt::Tool;
        #endif
        setWindowFlags(flags);
    }
    setAcceptDrops(true);
    setMouseTracking(true);
    setFixedSize(m_windowWidth, m_windowHeight);
    m_messageBubble = new MessageBubble(m_windowedMode ? parent : nullptr);
    m_messageBubble->onClosed = [this]() {
        m_isTaskBubbleUserOpened = false;
        processNextQueuedMessage();
    };
    m_messageBubble->onHoverChanged = [this](bool hovered) {
        m_bubbleHovered = hovered;
    };
    m_messageBubble->onReplySubmitted = [this](const QString &question, const QString &replyText) {
        handleUserBubbleReply(question, replyText);
    };
    m_selectionToolbar = new SelectionToolbar(m_windowedMode ? parent : nullptr);
    m_askDialog = new AskDialog(m_windowedMode ? parent : nullptr);
    m_settingsDialog = new AgentSettingsDialog(m_windowedMode ? parent : nullptr);
    BehaviorEngine::instance()->setActiveWidget(this);

    m_selectionToolbar->onTranslateRequested = [this](QString const& text) {
        onTranslateRequested(text);
    };

    m_selectionToolbar->onAskRequested = [this](QString const& text) {
        onAskRequested(text);
    };

    m_askDialog->onSubmit = [this](QString const& context, QString const& question) {
        onQuestionSubmitted(context, question);
    };

    m_askDialog->onCancel = [this]() {
        setWaitingForAgent(false);
    };

    m_askDialog->onDisplayModeChanged = [](AskDialog::DisplayMode) {
        if (auto *mgr = ShijimaManager::defaultManager()) {
            mgr->updateSystemTray();
        }
    };

    // 当设置变更时刷新全局快捷键配置
    connect(m_settingsDialog, &QDialog::accepted, this, []() {
        ShijimaManager::defaultManager()->updateGlobalHotkeys();
    });

    // 绑定定时器到期联动：自动弹出提醒气泡或触发 Agent 自动执行任务（仅主宠监听，严禁克隆体抢占）
    if (!m_isClone) {
        QPointer<ShijimaWidget> petPtr(this);
        TimerManager::instance()->onTimerTriggered = [petPtr](const ScheduledTimer &timer) {
            if (!petPtr) return;
            if (timer.type == TimerType::AiTask) {
                std::cout << "[定时器调度] 到期自动执行 Agent 任务: " << timer.title.toStdString() << std::endl;
                QString prompt = timer.taskPrompt.isEmpty() ? timer.title : timer.taskPrompt;
                petPtr->startTaskProgress("定时任务: " + timer.title, true);

                AgentService::instance()->ask("", prompt,
                    [petPtr, timer](QString const& progressMsg) {
                        if (!petPtr) return;
                        petPtr->updateTaskProgress(progressMsg);
                    },
                    [petPtr, timer](bool success, QString const& result, QString const& appTarget) {
                        if (!petPtr) return;
                        petPtr->finishTaskProgress(success, result, appTarget, timer.title);
                    }
                );
            } else {
                std::cout << "[定时器提醒] 到期弹出提醒气泡: " << timer.title.toStdString() << std::endl;
                BehaviorEngine::instance()->addAffection(2, 5);
                QString msg = QString("⏰ **定时提醒到达！**\n\n📌 **%1**\n\n*（时间: %2）*")
                    .arg(timer.title)
                    .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
                petPtr->showMessage(msg, 10000, "", true);
            }
        };
    }
}

ShijimaWidget::ShijimaWidget(ShijimaWidget &old, bool windowedMode,
    QWidget *parent) : ShijimaWidget(old.mascotData(),
    std::move(old.m_mascot), old.m_mascotId,
    windowedMode, parent) {}

void ShijimaWidget::showInspector() {
    if (m_inspector == nullptr) {
        m_inspector = new ShimejiInspectorDialog { this };
    }
    m_inspector->show();
}

bool ShijimaWidget::inspectorVisible() {
    return m_inspector != nullptr && m_inspector->isVisible();
}

void ShijimaWidget::showAgentSettings() {
    if (m_settingsDialog == nullptr) {
        m_settingsDialog = new AgentSettingsDialog(m_windowedMode ? parentWidget() : nullptr);
    }
    m_settingsDialog->refreshValues();
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void ShijimaWidget::showDiary() {
    if (m_diaryDialog == nullptr) {
        m_diaryDialog = new PetDiaryDialog(m_windowedMode ? parentWidget() : nullptr);
    }
    m_diaryDialog->refreshEntries();
    m_diaryDialog->show();
    m_diaryDialog->raise();
    m_diaryDialog->activateWindow();
}

Asset const& ShijimaWidget::getActiveAsset() {
    auto &name = m_mascot->state->active_frame.get_name(m_mascot->state->looking_right);
    auto lowerName = shimejifinder::to_lower(name);
    auto imagePath = QDir::cleanPath(m_data->imgRoot()
        + QDir::separator() + QString::fromStdString(lowerName));
    return AssetLoader::defaultLoader()->loadAsset(imagePath);
}

bool ShijimaWidget::isMirroredRender() const {
    return m_mascot->state->active_frame.right_name.empty() &&
        m_mascot->state->looking_right;
}

void ShijimaWidget::paintEvent(QPaintEvent *event) {
    if (!m_visible) {
        return;
    }
    auto &asset = getActiveAsset();
    auto &image = asset.image(isMirroredRender());
    auto scaledSize = image.size() / m_drawScale;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // 1. 绘制桌宠人偶本体 (基于脚底支点应用弹性缩放、音乐律动摆角与纵向点动)
    QPointF footAnchor(m_drawOrigin.x() + scaledSize.width() / 2.0, m_drawOrigin.y() + scaledSize.height());

    painter.save();
    painter.translate(footAnchor.x(), footAnchor.y() + m_motion.bobOffset());
    painter.rotate(m_motion.swayAngle());
    // 保持 1:1 像素精度，彻底杜绝人偶被拉伸或压扁变形
    if (std::abs(m_motion.scaleX() - 1.0f) > 0.15f || std::abs(m_motion.scaleY() - 1.0f) > 0.15f) {
        painter.scale(m_motion.scaleX(), m_motion.scaleY());
    }
    painter.translate(-footAnchor.x(), -footAnchor.y());

    painter.drawImage(QRect { m_drawOrigin, scaledSize }, image);

    // 摸头抚摸状态下的可爱腮红
    if (m_motion.isPetting()) {
        painter.setBrush(QColor(251, 113, 133, 180));
        painter.setPen(Qt::NoPen);
        int headCenterX = m_drawOrigin.x() + scaledSize.width() / 2;
        int blushY = m_drawOrigin.y() + static_cast<int>(scaledSize.height() * 0.42);
        painter.drawEllipse(headCenterX - 18, blushY, 9, 6);
        painter.drawEllipse(headCenterX + 9, blushY, 9, 6);
    }
    painter.restore();

    QPoint petGlobalPos = m_windowedMode ? mapToParent(QPoint(0, 0)) : mapToGlobal(QPoint(0, 0));
    bool isAtCeiling = (petGlobalPos.y() < 80);

    // 2. 原生合并绘制暗夜双微盘状态栏 (由设置控制是否开启，默认不显示)
    bool showHeadOrb = (SettingsDb::instance()->get("ui.show_head_status_orb", "false") == "true");
    if (showHeadOrb) {
        const auto &st = BehaviorEngine::instance()->state();
        int stamina = std::clamp(st.stamina, 0, 100);
        int mood = std::clamp(st.mood, 0, 100);

        const int circleSize = 22;
        int orbCenterY = isAtCeiling ? (m_drawOrigin.y() + scaledSize.height() + 14) 
                                     : (m_drawOrigin.y() - 14);
        int centerX = m_drawOrigin.x() + scaledSize.width() / 2;
        int leftOrbX = centerX - 25;
        int rightOrbX = centerX + 3;

        // A. 左侧：翠绿体力环形微盘
        QRect staminaRect(leftOrbX, orbCenterY - circleSize / 2, circleSize, circleSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(10, 15, 29, 230));
        painter.drawEllipse(staminaRect);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(30, 41, 59, 180), 2.0));
        painter.drawEllipse(staminaRect.adjusted(1, 1, -1, -1));

        if (stamina > 0) {
            double span = (stamina / 100.0) * -360.0 * 16.0;
            painter.setPen(QPen(QColor(74, 222, 128), 2.2, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(staminaRect.adjusted(1, 1, -1, -1), 90 * 16, static_cast<int>(span));
        }
        painter.setFont(QFont("-apple-system", 9, QFont::Bold));
        painter.setPen(QColor(74, 222, 128));
        painter.drawText(staminaRect, Qt::AlignCenter, "⚡");

        // B. 右侧：紫罗兰心情环形微盘
        QRect moodRect(rightOrbX, orbCenterY - circleSize / 2, circleSize, circleSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(10, 15, 29, 230));
        painter.drawEllipse(moodRect);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(30, 41, 59, 180), 2.0));
        painter.drawEllipse(moodRect.adjusted(1, 1, -1, -1));

        int displayMood = std::clamp(mood < 0 ? (mood + 100) / 2 : (50 + mood / 2), 0, 100);
        if (displayMood > 0) {
            double span = (displayMood / 100.0) * -360.0 * 16.0;
            painter.setPen(QPen(QColor(192, 132, 252), 2.2, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(moodRect.adjusted(1, 1, -1, -1), 90 * 16, static_cast<int>(span));
        }
        painter.setFont(QFont("-apple-system", 9, QFont::Bold));
        painter.setPen(QColor(192, 132, 252));
        painter.drawText(moodRect, Qt::AlignCenter, "☻");
    }

    // 3. 原生绘制任务执行环形进度微盘 (当有活跃任务且未结束时，渲染环形进度与脉冲光晕)
    if (m_hasActiveTask) {
        const int circleSize = 24;
        int orbCenterY = isAtCeiling ? (m_drawOrigin.y() + scaledSize.height() + 14) 
                                     : (m_drawOrigin.y() - 14);
        int centerX = m_drawOrigin.x() + scaledSize.width() / 2;
        if (showHeadOrb) {
            orbCenterY = isAtCeiling ? (orbCenterY + 24) : (orbCenterY - 24);
        }

        m_taskOrbRect = QRect(centerX - circleSize / 2, orbCenterY - circleSize / 2, circleSize, circleSize);

        // A. 脉冲外发光晕 (科技天蓝)
        float pulse = (std::sin(m_taskPulsePhase) + 1.0f) * 0.5f;
        int glowAlpha = static_cast<int>(25 + pulse * 50);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(14, 165, 233, glowAlpha));
        painter.drawEllipse(m_taskOrbRect.adjusted(-3, -3, 3, 3));

        // B. 暗色玻璃质感底盘
        painter.setBrush(QColor(10, 15, 29, 235));
        painter.setPen(QPen(m_isTaskOrbHovered ? QColor(56, 189, 248, 220) : QColor(30, 41, 59, 190), 1.5));
        painter.drawEllipse(m_taskOrbRect);

        // C. 进度底槽轨道 (深灰蓝)
        QRect trackRect = m_taskOrbRect.adjusted(2, 2, -2, -2);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(30, 41, 59, 140), 2.2));
        painter.drawEllipse(trackRect);

        // D. 霓虹青蓝进度条
        int percent = std::clamp(m_taskPercent, 0, 100);
        if (percent > 0) {
            double span = (percent / 100.0) * -360.0 * 16.0;
            QPen arcPen(QColor(56, 189, 248), 2.4, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(arcPen);
            painter.drawArc(trackRect, 90 * 16, static_cast<int>(span));
        }

        // E. 核心微标 / 百分比文本
        if (m_isTaskOrbHovered) {
            painter.setFont(QFont("-apple-system", 8));
            painter.setPen(QColor(56, 189, 248));
            painter.drawText(m_taskOrbRect, Qt::AlignCenter, "👁");
        } else {
            painter.setFont(QFont("-apple-system", 7, QFont::Bold));
            painter.setPen(QColor(224, 242, 254));
            if (percent > 0) {
                painter.drawText(m_taskOrbRect, Qt::AlignCenter, QString::number(percent));
            } else {
                painter.drawText(m_taskOrbRect, Qt::AlignCenter, "⚡");
            }
        }
    } else {
        m_taskOrbRect = QRect();
    }

    // 4. 绘制灵动粒子 (音符、爱心、星星、Zzz)
    for (const auto &p : m_motion.particles()) {
        float alphaRatio = (p.maxLife > 0.0f) ? (p.life / p.maxLife) : 0.0f;
        int alpha = std::clamp(static_cast<int>(alphaRatio * 255.0f), 0, 255);
        if (alpha <= 0) continue;

        painter.save();
        QPointF drawPt(footAnchor.x() + p.pos.x(), m_drawOrigin.y() + p.pos.y());
        painter.translate(drawPt);
        painter.rotate(p.rotation);
        painter.scale(p.scale, p.scale);

        QColor textColor = p.color;
        textColor.setAlpha(alpha);
        painter.setPen(textColor);

        QFont font("-apple-system", 13, QFont::Bold);
        painter.setFont(font);
        painter.drawText(QRectF(-16, -16, 32, 32), Qt::AlignCenter, p.text);
        painter.restore();
    }

    // 4. 绘制动态二次元情绪贴纸 (Emote Badge)
    PetEmoteType curEmote = m_motion.currentEmote();
    float emoteAlphaVal = m_motion.currentEmoteAlpha();
    if (curEmote != PetEmoteType::None && emoteAlphaVal > 0.01f) {
        int alpha = std::clamp(static_cast<int>(emoteAlphaVal * 255.0f), 0, 255);
        QString emoteSymbol;
        QColor badgeBg(15, 23, 42, std::min(220, alpha));
        QColor borderColor(255, 255, 255, std::min(180, alpha));

        switch (curEmote) {
            case PetEmoteType::HappyHeart:   emoteSymbol = "💖"; break;
            case PetEmoteType::Sparkle:      emoteSymbol = "✨"; break;
            case PetEmoteType::SleepZzz:     emoteSymbol = "💤"; break;
            case PetEmoteType::AngryVein:    emoteSymbol = "💢"; break;
            case PetEmoteType::DizzySwirl:   emoteSymbol = "💫"; break;
            case PetEmoteType::ThinkingBulb: emoteSymbol = "💡"; break;
            case PetEmoteType::MusicNote:    emoteSymbol = "🎵"; break;
            default: break;
        }

        if (!emoteSymbol.isEmpty()) {
            painter.save();
            int badgeSize = 24;
            int badgeX = footAnchor.x() + 10;
            int badgeY = isAtCeiling ? (m_drawOrigin.y() + scaledSize.height() + 24) 
                                     : (m_drawOrigin.y() - 34);

            QRect badgeRect(badgeX, badgeY, badgeSize, badgeSize);

            painter.setBrush(badgeBg);
            painter.setPen(QPen(borderColor, 1.5));
            painter.drawRoundedRect(badgeRect, 12, 12);

            painter.setFont(QFont("-apple-system", 11));
            painter.setPen(QColor(255, 255, 255, alpha));
            painter.drawText(badgeRect, Qt::AlignCenter, emoteSymbol);
            painter.restore();
        }
    }

#ifdef __linux__
    if (Platform::useWindowMasks()) {
        m_windowMask = QBitmap::fromPixmap(asset.mask(isMirroredRender())
            .scaled(scaledSize));
        m_windowMask.translate(m_drawOrigin);
        auto bounding = m_windowMask.boundingRect();
        bounding.setTop(0);
        bounding.setLeft(0);
        if (bounding.width() > 0 && bounding.height() > 0) {
            setMask(m_windowMask);
        }
        else {
            setMask(QRect { m_windowWidth - 2, m_windowHeight - 2, 1, 1 });
        }
    }
#endif
}

bool ShijimaWidget::updateOffsets() {
    bool needsRepaint = false;
    auto &frame = m_mascot->state->active_frame;
    auto &asset = getActiveAsset();
    
    int originalWidth = asset.originalSize().width();
    int originalHeight = asset.originalSize().height();
    double scale = m_mascot->state->env->get_scale();
    int screenWidth = (int)(m_mascot->state->env->screen.width()
        / scale);
    int screenHeight = (int)(m_mascot->state->env->screen.height()
        / scale);
    const int topPadding = 32;
    int windowWidth = (int)(originalWidth / scale);
    int windowHeight = (int)(originalHeight / scale) + topPadding;

    if (windowWidth != m_windowWidth) {
        m_windowWidth = windowWidth;
        setFixedWidth(m_windowWidth);
        needsRepaint = true;
    }
    if (windowHeight != m_windowHeight) {
        m_windowHeight = windowHeight;
        setFixedHeight(m_windowHeight);
        needsRepaint = true;
    }

    if (isMirroredRender()) {
        m_anchorInWindow = {
            (int)((originalWidth - frame.anchor.x) / scale),
            (int)(frame.anchor.y / scale) + topPadding };
    }
    else {
        m_anchorInWindow = { (int)(frame.anchor.x / scale),
            (int)(frame.anchor.y / scale) + topPadding };
    }

    QPoint drawOffset;
    m_visible = true;
    int winX = (int)m_mascot->state->anchor.x - m_anchorInWindow.x()
        - (int)env()->screen.left;
    int winY = (int)m_mascot->state->anchor.y - m_anchorInWindow.y()
        - (int)env()->screen.top;
    if (winX < 0) {
        drawOffset.setX(winX);
        winX = 0;
    }
    else if (winX + windowWidth > screenWidth) {
        drawOffset.setX(winX - screenWidth + windowWidth);
        winX = screenWidth - windowWidth;
    }
    if (winY < 0) {
        drawOffset.setY(winY);
        winY = 0;
    }
    else if (winY + windowHeight > screenHeight) {
        drawOffset.setY(winY - screenHeight + windowHeight);
        winY = screenHeight - windowHeight;
    }
    winX += (int)env()->screen.left;
    winY += (int)env()->screen.top;

    if (isMirroredRender()) {
        drawOffset += QPoint {
            (int)((originalWidth - asset.offset().topRight().x()) / scale),
            (int)(asset.offset().topLeft().y() / scale) + topPadding };
    }
    else {
        drawOffset += asset.offset().topLeft() / scale + QPoint(0, topPadding);
    }
    if (drawOffset != m_drawOrigin) {
        needsRepaint = true;
        m_drawOrigin = drawOffset;
    }
    if (scale != m_drawScale) {
        needsRepaint = true;
        m_drawScale = scale;
    }
    move(winX, winY);

    // 实时更新鼠标悬停提示 (ToolTip 状态卡片)
    if (m_hasActiveTask && m_isTaskOrbHovered) {
        setToolTip(QString("⚡ 任务执行中 · 第 %1 步 (%2%)\n🎯 %3\n💡 点击展开/收起详情卡片")
            .arg(QString::number(m_taskStep), QString::number(m_taskPercent), m_taskActionTitle));
    } else {
        const auto &st = BehaviorEngine::instance()->state();
        setToolTip(QString("🐾 阿呆桌宠\n⚡ 体力: %1% %2\n😊 心情: %3 | 💕 亲密: %4")
            .arg(st.stamina)
            .arg(st.isRestingInCorner ? "(角落休整 💤)" : "(元气探索 🌟)")
            .arg(st.mood)
            .arg(st.affection));
    }

    if (m_messageBubble != nullptr && m_messageBubble->hasMessage()) {
        auto env = m_mascot->state->env;

        int bubbleW = m_messageBubble->width();
        int bubbleH = m_messageBubble->height();

        auto &asset = getActiveAsset();
        auto image = asset.image(isMirroredRender());
        int drawnWidth = (int)(image.width() / m_drawScale);
        int drawnHeight = (int)(image.height() / m_drawScale);

        // 核心修复：直接使用桌宠当前在屏幕上的物理绘制绝对像素矩形，紧贴桌宠头顶！
        int petLeftX = this->x() + m_drawOrigin.x();
        int petTopY = this->y() + m_drawOrigin.y();
        int petCenterX = petLeftX + (drawnWidth / 2);
        int petBottomY = petTopY + drawnHeight;

        int scrLeft = (int)env->screen.left;
        int scrRight = (int)(env->screen.left + env->screen.width());
        int scrTop = (int)env->screen.top;
        int scrBottom = (int)(env->screen.top + env->screen.height());

        // 1. 水平方向：气泡中心对准桌宠身体中心，受屏幕安全边界保护
        int rawX = petCenterX - (bubbleW / 2);
        int clampedX = std::clamp(rawX, scrLeft + 8, scrRight - bubbleW - 8);

        // 2. 垂直方向：无缝紧贴于桌宠头顶上方（间距 2px）；若贴近天花板则紧贴于脚底下方（间距 2px）
        bool isAtCeiling = (petTopY - bubbleH - 2 < scrTop + 8);
        int targetY = isAtCeiling ? (petBottomY + 2) : (petTopY - bubbleH - 2);
        int clampedY = std::clamp(targetY, scrTop + 8, scrBottom - bubbleH - 8);

        // 3. 动态计算尖角横坐标，确保尖角精准指向桌宠头顶
        int tailRelX = std::clamp(petCenterX - clampedX, 16, bubbleW - 16);
        if (std::abs(m_messageBubble->tailX() - tailRelX) >= 3 || m_messageBubble->isTailFlipped() != isAtCeiling) {
            m_messageBubble->setTailPosition(tailRelX, isAtCeiling);
        }

        QPoint targetPos(clampedX, clampedY);
        // 关键防死锁机制：气泡展示期间保持屏幕物理锚点稳定，彻底消除 Cocoa 窗口高频移动抖动与事件死锁
        bool isUserDraggingPet = (m_dragTarget && m_dragTarget->m_mascot && m_dragTarget->m_mascot->state && m_dragTarget->m_mascot->state->dragging);
        if (!m_messageBubble->isPositionInitialized() || isUserDraggingPet) {
            m_messageBubble->move(targetPos);
            m_messageBubble->setPositionInitialized(true);
        } else if (!m_messageBubble->isHovered()) {
            // 未悬停时，仅在桌宠发生大跨度位移 (>35px，如大幅爬墙或远距离跳跃) 时平稳校准，杜绝动画微小帧差引发窗口震颤
            int dx = std::abs(m_messageBubble->x() - targetPos.x());
            int dy = std::abs(m_messageBubble->y() - targetPos.y());
            if (dx > 35 || dy > 35) {
                m_messageBubble->move(targetPos);
            }
        }
    }

    return needsRepaint;
}

bool ShijimaWidget::pointInside(QPoint const& point) {
    if (!m_visible) {
        return false;
    }
    // 活跃任务的环形进度微盘命中检测
    if (m_hasActiveTask && m_taskOrbRect.contains(point)) {
        return true;
    }
    auto &asset = getActiveAsset();
    auto image = asset.image(isMirroredRender());
    int drawnWidth = (int)(image.width() / m_drawScale);
    int drawnHeight = (int)(image.height() / m_drawScale);
    auto imagePos = point - m_drawOrigin;
    if (imagePos.x() < 0 || imagePos.y() < 0 ||
        imagePos.x() > drawnWidth || imagePos.y() > drawnHeight)
    {
        return false;
    }
    auto color = image.pixelColor(imagePos * m_drawScale);
    if (color.alpha() == 0) {
        return false;
    }
    return true;
}

void ShijimaWidget::tick() {
    if (m_markedForDeletion) {
        close();
        return;
    }
    if (paused()) {
        return;
    }

    if (m_isRunningToCenter) {
        updateOffsets();
        update();
        return;
    }

    if (m_isThrowFlying) {
        updateOffsets();
        update();
        return;
    }

    // 实时左右光标盯视追踪 (当处于看鼠标/追逐/注视状态时自然转头)
    QString curBeh = currentBehaviorName();
    if (curBeh.contains("SitAndFaceMouse", Qt::CaseInsensitive) ||
        curBeh.contains("ChaseMouse", Qt::CaseInsensitive) ||
        curBeh.contains("LookAtCursor", Qt::CaseInsensitive)) {
        // 如果气泡正在展示，禁止因光标位于气泡上方而左右频繁反转头，杜绝震荡卡顿
        if (!m_messageBubble || !m_messageBubble->isDisplaying()) {
            QPoint curPos = QCursor::pos();
            QPoint petPos = m_windowedMode ? mapToParent(QPoint(0, 0)) : mapToGlobal(QPoint(0, 0));
            int petCenterX = petPos.x() + (m_windowWidth / 2);
            if (std::abs(curPos.x() - petCenterX) > 20) {
                bool shouldLookRight = (curPos.x() >= petCenterX);
                if (m_mascot && m_mascot->state && m_mascot->state->looking_right != shouldLookRight) {
                    m_mascot->state->looking_right = shouldLookRight;
                }
            }
        }
    }

    // 弹提示与气泡展示期间，抑制跑动/走动/攀爬/起跳，强制保持原地安静姿态
    if (m_messageBubble && m_messageBubble->isDisplaying() && m_mascot && m_mascot->state) {
        QString beh = currentBehaviorName();
        if (beh.contains("Run", Qt::CaseInsensitive) ||
            beh.contains("Walk", Qt::CaseInsensitive) ||
            beh.contains("Climb", Qt::CaseInsensitive) ||
            beh.contains("Jump", Qt::CaseInsensitive)) {
            if (!trySetBehavior("SitDown")) {
                trySetBehavior("StandUp");
            }
        }
    }

    auto prev_frame = m_mascot->state->active_frame;
    try {
        m_mascot->tick();
    } catch (const std::exception &e) {
        std::cerr << "[Shijima Safe Guard] Tick recovered from: " << e.what() << std::endl;
        m_mascot->reset_position();
        m_mascot->detach_from_borders();
        m_mascot->next_behavior("Fall");
    }

    // tick 之后若触发新行为转移为移动行为，在气泡展示期间同样立即重置为坐下静止
    if (m_messageBubble && m_messageBubble->isDisplaying() && m_mascot && m_mascot->state) {
        QString beh = currentBehaviorName();
        if (beh.contains("Run", Qt::CaseInsensitive) ||
            beh.contains("Walk", Qt::CaseInsensitive) ||
            beh.contains("Climb", Qt::CaseInsensitive) ||
            beh.contains("Jump", Qt::CaseInsensitive)) {
            if (!trySetBehavior("SitDown")) {
                trySetBehavior("StandUp");
            }
        }
    }

    checkWindowVanished();

    auto &new_frame = m_mascot->state->active_frame;
    auto &new_sound = m_mascot->state->active_sound;
    bool forceRepaint = prev_frame.name != new_frame.name;
    bool offsetsChanged = updateOffsets();

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    float deltaSec = 0.04f;
    if (m_lastMotionUpdateTime > 0) {
        deltaSec = (nowMs - m_lastMotionUpdateTime) / 1000.0f;
    }
    m_lastMotionUpdateTime = nowMs;

    bool isMusicPlaying = MusicPlayerManager::instance()->isPlaying();
    QString curName = currentBehaviorName();
    bool isFalling = curName.contains("Fall", Qt::CaseInsensitive) || m_isThrowFlying;
    bool isMoving = curName.contains("Walk", Qt::CaseInsensitive) ||
                    curName.contains("Run", Qt::CaseInsensitive) ||
                    curName.contains("Climb", Qt::CaseInsensitive) ||
                    curName.contains("Jump", Qt::CaseInsensitive) ||
                    m_isRunningToCenter || m_isThrowFlying;
    bool isOnWindow = m_wasOnWindow;

    // 落地状态追踪 (使用原画自带的 Bouncing 动画，不使用全局矩阵非等比挤压，避免像素画变形)
    m_lastWasFalling = isFalling;

    // 音乐伴奏联动：如果在窗口顶栏听歌，触发晃腿动作
    if (isMusicPlaying && isOnWindow && !isFalling) {
        if (!curName.contains("Dangle", Qt::CaseInsensitive) && !curName.contains("Sit", Qt::CaseInsensitive)) {
            trySetBehavior("SitWhileDanglingLegs");
        }
    }

    m_motion.update(deltaSec, isMusicPlaying, isFalling, isMoving, isOnWindow);

    // 如果正在跳舞、形变、抚摸或有粒子活动，保持流畅重绘
    if (m_motion.isDancing() || m_motion.isPetting() || !m_motion.particles().empty() ||
        std::abs(m_motion.scaleX() - 1.0f) > 0.01f || std::abs(m_motion.scaleY() - 1.0f) > 0.01f ||
        std::abs(m_motion.swayAngle()) > 0.1f) {
        forceRepaint = true;
    }

    // 活跃任务执行中驱动微盘脉冲光晕与进度动画
    if (m_hasActiveTask) {
        m_taskPulsePhase += 0.12f;
        if (m_taskPulsePhase > 6.2831853f) {
            m_taskPulsePhase -= 6.2831853f;
        }
        forceRepaint = true;
    }

    if (m_mascot->state->dead) {
        forceRepaint = true;
        new_frame.name = "";
        new_sound = "";
        m_mascot->state->active_sound_changed = true;
        markForDeletion();
    }
    if (offsetsChanged || forceRepaint) {
        update();
    }
    if (m_mascot->state->active_sound_changed) {
        m_sounds.stop();
        if (!new_sound.empty()) {
            m_sounds.play(QString::fromStdString(new_sound));
        }
    }
    else if (!m_sounds.playing()) {
        m_mascot->state->active_sound.clear();
    }

    if (m_inspector != nullptr && m_inspector->isVisible()) {
        m_inspector->tick();
    }
}

void ShijimaWidget::contextMenuClosed(QCloseEvent *event) {
    m_contextMenuVisible = false;
}

void ShijimaWidget::showContextMenu(QPoint const& pos) {
    m_contextMenuVisible = true;
    ShijimaContextMenu *menu = new ShijimaContextMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(pos);
}

ShijimaWidget::~ShijimaWidget() {
    if (BehaviorEngine::instance()->activeWidget() == this) {
        BehaviorEngine::instance()->setActiveWidget(nullptr);
    }
    TimerManager::instance()->onTimerTriggered = nullptr;
    if (m_throwPhysicsTimer != nullptr) {
        m_throwPhysicsTimer->stop();
    }
    if (m_moveAnimation != nullptr) {
        m_moveAnimation->stop();
        delete m_moveAnimation;
        m_moveAnimation = nullptr;
    }
    if (m_dragTargetPt != nullptr) {
        *m_dragTargetPt = nullptr;
        m_dragTargetPt = nullptr;
    }
    if (m_inspector != nullptr) {
        m_inspector->close();
        delete m_inspector;
        m_inspector = nullptr;
    }
    if (m_messageBubble != nullptr) {
        m_messageBubble->close();
        delete m_messageBubble;
        m_messageBubble = nullptr;
    }
    if (m_selectionToolbar != nullptr) {
        m_selectionToolbar->close();
        delete m_selectionToolbar;
        m_selectionToolbar = nullptr;
    }
    if (m_askDialog != nullptr) {
        m_askDialog->close();
        delete m_askDialog;
        m_askDialog = nullptr;
    }
    if (m_settingsDialog != nullptr) {
        m_settingsDialog->close();
        delete m_settingsDialog;
        m_settingsDialog = nullptr;
    }
    if (m_timerDialog != nullptr) {
        m_timerDialog->close();
        delete m_timerDialog;
        m_timerDialog = nullptr;
    }
    if (m_diaryDialog != nullptr) {
        m_diaryDialog->close();
        delete m_diaryDialog;
        m_diaryDialog = nullptr;
    }
    setDragTarget(nullptr);
}

void ShijimaWidget::setDragTarget(ShijimaWidget *target) {
    if (m_dragTarget != nullptr) {
        m_dragTarget->m_dragTargetPt = nullptr;
    }
    if (target != nullptr) {
        if (target->m_dragTargetPt != nullptr) {
            throw std::runtime_error("target widget being dragged by multiple widgets");
        }
        m_dragTarget = target;
        m_dragTarget->m_dragTargetPt = &m_dragTarget;
    }
    else {
        m_dragTarget = nullptr;
    }
}

void ShijimaWidget::mousePressEvent(QMouseEvent *event) {
    auto pos = event->pos();
    // 优先拦截任务进度微盘点击 (点击切换展开/收起具体任务步骤卡片)
    if (m_hasActiveTask && m_taskOrbRect.contains(pos)) {
        if (event->button() == Qt::LeftButton) {
            toggleTaskProgressBubble();
            event->accept();
            return;
        }
    }
    if (m_dragTarget != nullptr) {
        m_dragTarget->m_mascot->state->dragging = false;
    }
    if (pointInside(pos)) {
        setDragTarget(this);
    }
    else {
        QPoint envPos;
        if (m_windowedMode) {
            envPos = mapToParent(pos);
        }
        else {
            envPos = mapToGlobal(pos);
        }
        ShijimaWidget *target = ShijimaManager::defaultManager()->hitTest(envPos);
        setDragTarget(target);
        if (target == nullptr) {
            event->ignore();
            return;
        }
    }
    if (event->button() == Qt::MouseButton::LeftButton) {
        BehaviorEngine::instance()->setActiveWidget(m_dragTarget);
        if (m_dragTarget->m_throwPhysicsTimer != nullptr) {
            m_dragTarget->m_throwPhysicsTimer->stop();
        }
        m_dragTarget->m_isThrowFlying = false;
        m_dragTarget->m_mascot->state->dragging = true;
        m_dragTarget->m_lastMousePos = event->globalPosition().toPoint();
        m_dragTarget->m_dragStartGlobalPos = event->globalPosition().toPoint();
        m_dragTarget->m_lastMouseMoveTime = QDateTime::currentMSecsSinceEpoch();
        m_dragTarget->m_dragVelocityX = 0.0;
        m_dragTarget->m_dragVelocityY = 0.0;
        m_dragTarget->m_isDragStarted = false;

        // 精准抓取偏移采样：锁定鼠标与桌宠身体的物理相对位置，实现绝对 1:1 零延迟贴手
        m_dragTarget->m_dragGrabOffset = QPointF(
            event->globalPosition().x() - m_dragTarget->m_mascot->state->anchor.x,
            event->globalPosition().y() - m_dragTarget->m_mascot->state->anchor.y
        );

        // 捕获拖拽前正在执行的意图（如正在爬顶、扑窗口等）
        m_dragTarget->m_interruptedGoal = m_dragTarget->detectCurrentGoal();

        BehaviorEngine::instance()->recordUserInteraction();
        QJsonObject payload;
        payload["mascot_id"] = m_dragTarget->mascotId();
        PetEventBus::instance()->emitEvent("user.drag_pet", payload);
    }
    else if (event->button() == Qt::MouseButton::RightButton) {
        BehaviorEngine::instance()->setActiveWidget(m_dragTarget);
        BehaviorEngine::instance()->recordUserInteraction();
        auto screenPos = mapToGlobal(pos);
        m_dragTarget->showContextMenu(screenPos);
        setDragTarget(nullptr);
    }
}

void ShijimaWidget::mouseMoveEvent(QMouseEvent *event) {
    if (m_hasActiveTask) {
        bool hovered = m_taskOrbRect.contains(event->pos());
        if (hovered != m_isTaskOrbHovered) {
            m_isTaskOrbHovered = hovered;
            if (hovered) {
                setCursor(Qt::PointingHandCursor);
                setToolTip(QString("⚡ 任务执行中 · 第 %1 步 (%2%)\n🎯 %3\n💡 点击展开/收起详情卡片")
                    .arg(QString::number(m_taskStep), QString::number(m_taskPercent), m_taskActionTitle));
            } else {
                unsetCursor();
                setToolTip("");
            }
            update();
        }
    }

    if (m_dragTarget == nullptr || !m_dragTarget->m_mascot || !m_dragTarget->m_mascot->state) {
        return;
    }

    QPoint curPos = event->globalPosition().toPoint();
    int distFromStart = (curPos - m_dragTarget->m_dragStartGlobalPos).manhattanLength();

    // 仅在极微小位移 (< 15px) 且未进入正式拖拽时作为摸头手势采样
    if (distFromStart < 15 && !m_dragTarget->m_isDragStarted) {
        QRect headRect(m_drawOrigin.x(), m_drawOrigin.y(), m_windowWidth, static_cast<int>(m_windowHeight * 0.45));
        QPoint localPos = event->pos();
        bool isMouseDown = (event->buttons() & Qt::LeftButton);
        if (m_dragTarget->m_motion.handlePettingSample(localPos, isMouseDown, headRect)) {
            BehaviorEngine::instance()->addAffection(1, 1);
            update();
        }
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_dragTarget->m_lastMouseMoveTime > 0) {
        qint64 dt = now - m_dragTarget->m_lastMouseMoveTime;
        if (dt > 0 && dt < 120) {
            double vx = (curPos.x() - m_dragTarget->m_lastMousePos.x()) / (double)dt * 20.0;
            double vy = (curPos.y() - m_dragTarget->m_lastMousePos.y()) / (double)dt * 20.0;
            // 指数滑动平均滤波 (EMA) 准确记录在被拖拽的目标对象上
            m_dragTarget->m_dragVelocityX = m_dragTarget->m_dragVelocityX * 0.4 + vx * 0.6;
            m_dragTarget->m_dragVelocityY = m_dragTarget->m_dragVelocityY * 0.4 + vy * 0.6;
        }
    }
    m_dragTarget->m_lastMousePos = curPos;
    m_dragTarget->m_lastMouseMoveTime = now;

    // 当用户正在拖拽时：
    if (m_dragTarget->m_mascot->state->dragging) {
        // 位移 >= 8px 明确开始拎起，仅设置一次 Dragged 动作，绝不每帧重复触发 reset 导致卡顿
        if (distFromStart >= 8 && !m_dragTarget->m_isDragStarted) {
            m_dragTarget->m_isDragStarted = true;
            m_dragTarget->m_mascot->next_behavior("Dragged");
        }

        // 极速 1:1 跟手物理同步：
        // 直接将桌宠 anchor 绝对锁定至当前鼠标位置减去抓取时的相对偏移
        auto env = m_dragTarget->m_mascot->state->env;
        if (env) {
            m_dragTarget->m_mascot->state->anchor.x = curPos.x() - m_dragTarget->m_dragGrabOffset.x();
            m_dragTarget->m_mascot->state->anchor.y = curPos.y() - m_dragTarget->m_dragGrabOffset.y();
            env->cursor.x = curPos.x();
            env->cursor.y = curPos.y();
        }

        // 瞬时更新桌宠窗口物理位置并重绘（不调 tick 避免与主循环并发碰撞和动画超速）
        m_dragTarget->updateOffsets();
        m_dragTarget->repaint();
    }
}

void ShijimaWidget::closeAction() {
    close();
}

void ShijimaWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragTarget == nullptr) {
        return;
    }
    if (event->button() == Qt::MouseButton::LeftButton) {
        m_dragTarget->m_mascot->state->dragging = false;
        m_dragTarget->m_isDragStarted = false;

        QPoint releasePos = event->globalPosition().toPoint();
        int dragDist = (releasePos - m_dragTarget->m_dragStartGlobalPos).manhattanLength();

        // 1. 抚摸摸头互动结算 (位移很小且进入了摸头状态)
        if (m_dragTarget->m_motion.isPetting() && dragDist < 25) {
            m_dragTarget->m_motion.triggerEmote(PetEmoteType::HappyHeart, 2.8f);
            m_dragTarget->m_motion.spawnHeart(QPointF(0, -25.0f));
            BehaviorEngine::instance()->addAffection(3, 8);
            PetDiaryManager::instance()->recordPetting();

            static const QStringList petQuotes = {
                "（舒服地眯起眼睛）呼噜噜…摸得好舒服呀~ 💖",
                "蹭蹭你的手心~ 亲密度 +5！✨",
                "主人最温柔啦，还要摸头！(≧▽≦)",
                "被摸得晕乎乎的…今天也要加油哦！🌸",
                "嘿嘿，收到主人的摸头能量！充满电啦 ⚡"
            };
            int qIdx = QRandomGenerator::global()->bounded(static_cast<int>(petQuotes.size()));
            m_dragTarget->clearMessageQueue();
            m_dragTarget->showMessage(petQuotes[qIdx], 3500);

            // 摸头后轻巧坐下面对鼠标，绝不卡死
            m_dragTarget->trySetBehavior("SitAndFaceMouse");
            m_dragTarget->handlePatrolInterruption("petting");
            setDragTarget(nullptr);
            return;
        }

        // 2. 纯点击交互 (Click / Poke，位移极小，没有拖动)
        if (dragDist < 10) {
            BehaviorEngine::instance()->recordUserInteraction();
            BehaviorEngine::instance()->addAffection(1, 2);

            QJsonObject payload;
            payload["mascot_id"] = m_dragTarget->mascotId();
            PetEventBus::instance()->emitEvent("user.click_pet", payload);

            // 点击即时生动反馈：绝不卡在 SitDown！
            // 随机做生动的短动作交互 (傲娇晃头、转头看鼠标、原地站起或轻快弹跳站稳)
            static const std::vector<std::string> clickBehaviors = {
                "SitAndSpinHead", "SitAndFaceMouse", "StandUp", "Bouncing"
            };
            int bIdx = QRandomGenerator::global()->bounded(static_cast<int>(clickBehaviors.size()));
            if (!m_dragTarget->trySetBehavior(clickBehaviors[bIdx])) {
                m_dragTarget->trySetBehavior("SitAndFaceMouse") || m_dragTarget->trySetBehavior("StandUp");
            }
            m_dragTarget->m_motion.triggerEmote(PetEmoteType::Sparkle, 1.8f);

            m_dragTarget->handlePatrolInterruption("poke");
            m_dragTarget->m_interruptedGoal = PetInterruptedGoal::None;
            setDragTarget(nullptr);
            return;
        }

        // 3. 拖拽释放 (用户拎起来移动后放下)
        BehaviorEngine::instance()->recordUserInteraction();
        BehaviorEngine::instance()->addAffection(2, 5);

        // 判定用户是否在快速甩动手势中松手 (必须在最近 90ms 内有连续位移，且速度标量 >= 6.0)
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool hasRecentFlick = (m_dragTarget->m_lastMouseMoveTime > 0 && (now - m_dragTarget->m_lastMouseMoveTime) < 90);
        double throwSpeed = hasRecentFlick ? std::hypot(m_dragTarget->m_dragVelocityX, m_dragTarget->m_dragVelocityY) : 0.0;

        // 仅当用户明确快速甩手丢出 (throwSpeed >= 6.0) 时启动抛物线飞行
        if (throwSpeed >= 6.0) {
            std::cout << "[物理投掷] 成功捕获快速甩手! 速度向量: (" 
                      << m_dragTarget->m_dragVelocityX << ", " << m_dragTarget->m_dragVelocityY 
                      << "), 标量速度: " << throwSpeed << std::endl;
            m_dragTarget->applyThrowPhysics(m_dragTarget->m_dragVelocityX, m_dragTarget->m_dragVelocityY);
        } else {
            // 平稳放下：完全清空投掷物理速度，避免产生意外位移冲量
            m_dragTarget->m_isThrowFlying = false;
            m_dragTarget->m_throwVx = 0.0;
            m_dragTarget->m_throwVy = 0.0;
            if (m_dragTarget->m_throwPhysicsTimer) {
                m_dragTarget->m_throwPhysicsTimer->stop();
            }

            // 平稳着陆吸附或自然下落弹跳站稳
            m_dragTarget->snapToNearestBorderOrWindow();

            QJsonObject payload;
            payload["mascot_id"] = m_dragTarget->mascotId();
            PetEventBus::instance()->emitEvent("user.drag_pet", payload);

            // 关键：拖拽放下后 0 延迟即时反馈！清空残留排队队列并瞬时弹出萌趣台词
            static const QStringList dropQuotes = {
                "呼~ 落地啦！小短腿站稳稳~ 🐾",
                "搬家完成！新视野好开阔呀 ✨",
                "被拎在半空中晃来晃去，好晕好刺激 🌀",
                "脚踏实地的感觉最安心啦！(≧▽≦)",
                "安全着陆！主人挑的新位置真不错 🌸",
                "原地立正！继续巡逻守护主人 🫡"
            };
            int qIdx = QRandomGenerator::global()->bounded(static_cast<int>(dropQuotes.size()));
            m_dragTarget->clearMessageQueue();
            m_dragTarget->showMessage(dropQuotes[qIdx], 3200);

            // 异步触发巡逻意图更新，完全不阻塞当前气泡呈现
            m_dragTarget->handlePatrolInterruption("relocate");
            m_dragTarget->m_interruptedGoal = PetInterruptedGoal::None;
        }

        setDragTarget(nullptr);
    }
}

void ShijimaWidget::enterEvent(QEnterEvent *event) {
    QWidget::enterEvent(event);
    m_petHovered = true;
    if (m_messageBubble && m_messageBubble->isDisplaying() && m_mascot && m_mascot->state) {
        // 用户鼠标悬停在正在说话的桌宠上，原地安静坐下，避免到处乱跑
        trySetBehavior("SitDown") || trySetBehavior("StandUp");
    }
}

void ShijimaWidget::leaveEvent(QEvent *event) {
    if (m_isTaskOrbHovered) {
        m_isTaskOrbHovered = false;
        unsetCursor();
        setToolTip("");
        update();
    }
    QWidget::leaveEvent(event);
    m_petHovered = false;
}

void ShijimaWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ShijimaWidget::dropEvent(QDropEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    QString localPath = urls.first().toLocalFile();
    if (localPath.isEmpty()) return;

    QFileInfo fi(localPath);
    QString fileName = fi.fileName();
    QPointF dropPos = mapToGlobal(event->position().toPoint());

    std::cout << "[DragDrop] 拖拽删除文件: " << localPath.toStdString() << " 位置: " << dropPos.x() << "," << dropPos.y() << std::endl;

    // 触发连贯搬运扔进黑洞废纸篓动画
    FileDisposalSequence::instance()->start(this, fileName, localPath, dropPos);
    event->acceptProposedAction();
}

void ShijimaWidget::applyThrowPhysics(double vx, double vy) {
    if (!m_mascot || !m_mascot->state || !m_mascot->state->env) return;

    if (m_throwPhysicsTimer == nullptr) {
        m_throwPhysicsTimer = new QTimer(this);
    }
    m_throwPhysicsTimer->stop();
    m_throwPhysicsTimer->disconnect();

    // 独占接管物理坐标，避免主循环 tick 并发冲突
    m_isThrowFlying = true;

    // 适度放大初速度并进行平滑钳制
    m_throwVx = std::clamp(vx * 1.3, -40.0, 40.0);
    m_throwVy = std::clamp(vy * 1.3, -40.0, 40.0);

    m_motion.triggerStretch(0.88f, 1.18f);
    m_mascot->next_behavior("Thrown");

    connect(m_throwPhysicsTimer, &QTimer::timeout, this, [this]() {
        if (!m_mascot || !m_mascot->state || !m_mascot->state->env || m_mascot->state->dragging) {
            m_isThrowFlying = false;
            if (m_throwPhysicsTimer) {
                m_throwPhysicsTimer->stop();
            }
            return;
        }

        auto env = m_mascot->state->env;
        auto &anchor = m_mascot->state->anchor;

        // 物理更新: 重力加速度 g = 0.95, 空气阻力
        m_throwVy += 0.95;
        m_throwVx *= 0.985;

        anchor.x += m_throwVx;
        anchor.y += m_throwVy;

        bool landed = false;
        QString landingBehavior = "LieDown";

        // 1. 活跃窗口碰撞判定
        if (env->active_ie.visible()) {
            auto &ie = env->active_ie;
            // 落在窗口顶梁上
            if (anchor.x >= (ie.left - 10.0) && anchor.x <= (ie.right + 10.0) &&
                anchor.y >= (ie.top - 15.0) && anchor.y <= (ie.top + 25.0) && m_throwVy > 0) {
                anchor.y = ie.top;
                landed = true;
                landingBehavior = "WalkAlongIECeiling";
            }
            // 撞到窗口左边壁
            else if (std::abs(anchor.x - ie.left) <= 18.0 &&
                     anchor.y >= ie.top && anchor.y <= ie.bottom) {
                anchor.x = ie.left;
                m_mascot->state->looking_right = false;
                landed = true;
                landingBehavior = "ClimbAlongWall";
            }
            // 撞到窗口右边壁
            else if (std::abs(anchor.x - ie.right) <= 18.0 &&
                     anchor.y >= ie.top && anchor.y <= ie.bottom) {
                anchor.x = ie.right;
                m_mascot->state->looking_right = true;
                landed = true;
                landingBehavior = "ClimbAlongWall";
            }
        }

        // 2. 地面碰撞判定
        if (!landed && anchor.y >= env->floor.y) {
            anchor.y = env->floor.y;
            if (std::abs(m_throwVy) > 7.0) {
                m_throwVy = -m_throwVy * 0.35; // 触地弹跳
                m_throwVx *= 0.6;
            } else {
                landed = true;
                landingBehavior = "LieDown";
            }
        }

        // 3. 天花板碰撞判定
        if (!landed && anchor.y <= env->ceiling.y) {
            anchor.y = env->ceiling.y;
            m_throwVy = -m_throwVy * 0.3;
        }

        // 4. 屏幕左/右主墙壁碰撞判定
        if (!landed) {
            if (anchor.x <= env->work_area.left) {
                anchor.x = env->work_area.left;
                m_mascot->state->looking_right = false;
                landed = true;
                landingBehavior = "ClimbAlongWall";
            } else if (anchor.x >= env->work_area.right) {
                anchor.x = env->work_area.right;
                m_mascot->state->looking_right = true;
                landed = true;
                landingBehavior = "ClimbAlongWall";
            }
        }

        updateOffsets();
        update();

        if (landed) {
            m_isThrowFlying = false;
            m_throwVx = 0.0;
            m_throwVy = 0.0;
            if (m_throwPhysicsTimer) {
                m_throwPhysicsTimer->stop();
            }
            m_motion.triggerSquash(1.22f, 0.78f);
            if (std::hypot(m_throwVx, m_throwVy) > 10.0) {
                m_motion.triggerEmote(PetEmoteType::DizzySwirl, 2.5f);
            }

            // 落地切换到合法的行为，绝不使用不存在的 "Stand"（避免触发 libshimeji 的天花板重置闪现）
            m_mascot->next_behavior(landingBehavior.toStdString());
            if (landingBehavior == "LieDown") {
                BehaviorEngine::instance()->startFallRecovery(this);
            }
            handlePatrolInterruption("throw");
        }
    });

    m_throwPhysicsTimer->start(16); // 60 FPS 抛物线
}

void ShijimaWidget::snapToNearestBorderOrWindow() {
    if (!m_mascot || !m_mascot->state || !m_mascot->state->env) return;

    auto env = m_mascot->state->env;
    auto &anchor = m_mascot->state->anchor;

    // 1. 如果放置在地面附近 (距离地面 <= 35px) -> 落地站稳并恢复自然漫步
    if (anchor.y >= (env->floor.y - 35.0)) {
        anchor.y = env->floor.y;
        if (!trySetBehavior("StandUp") && !trySetBehavior("WalkAlongWorkAreaFloor")) {
            m_mascot->next_behavior("SitDown");
        }
        updateOffsets();
        update();
        return;
    }

    // 2. 如果放置在活跃窗口顶梁附近 (距离 top <= 35px，且在窗口横向宽度内) -> 稳稳立在窗口顶沿
    if (env->active_ie.visible()) {
        auto &ie = env->active_ie;
        bool nearTop = std::abs(anchor.y - ie.top) <= 35.0;
        bool withinXRange = (anchor.x >= (ie.left - 15.0) && anchor.x <= (ie.right + 15.0));
        if (nearTop && withinXRange) {
            anchor.y = ie.top;
            anchor.x = std::clamp(anchor.x, ie.left + 15.0, ie.right - 15.0);
            m_mascot->next_behavior("WalkAlongIECeiling");
            updateOffsets();
            update();
            return;
        }

        // 靠近窗口左边缘 (15px 极近贴近) -> 抓壁
        if (std::abs(anchor.x - ie.left) <= 15.0 && anchor.y >= ie.top && anchor.y <= ie.bottom) {
            anchor.x = ie.left;
            m_mascot->state->looking_right = false;
            m_mascot->next_behavior("ClimbAlongWall");
            updateOffsets();
            update();
            return;
        }
        // 靠近窗口右边缘 (15px 极近贴近) -> 抓壁
        if (std::abs(anchor.x - ie.right) <= 15.0 && anchor.y >= ie.top && anchor.y <= ie.bottom) {
            anchor.x = ie.right;
            m_mascot->state->looking_right = true;
            m_mascot->next_behavior("ClimbAlongWall");
            updateOffsets();
            update();
            return;
        }
    }

    // 3. 靠近屏幕天花板 (20px 极近贴近)
    if (std::abs(anchor.y - env->ceiling.y) <= 20.0) {
        anchor.y = env->ceiling.y;
        m_mascot->next_behavior("ClimbAlongCeiling");
        updateOffsets();
        update();
        return;
    }

    // 4. 靠近屏幕最左侧/最右侧边缘 (20px 极近贴近)
    if (std::abs(anchor.x - env->work_area.left) <= 20.0) {
        anchor.x = env->work_area.left;
        m_mascot->state->looking_right = false;
        m_mascot->next_behavior("ClimbAlongWall");
        updateOffsets();
        update();
        return;
    }
    if (std::abs(anchor.x - env->work_area.right) <= 20.0) {
        anchor.x = env->work_area.right;
        m_mascot->state->looking_right = true;
        m_mascot->next_behavior("ClimbAlongWall");
        updateOffsets();
        update();
        return;
    }

    // 5. 其他任何空中位置：原地笔直自然掉落，触地后自动着陆
    m_mascot->next_behavior("Fall");
    updateOffsets();
    update();
}

bool ShijimaWidget::checkAndJumpToActiveIE() {
    if (!m_mascot || !m_mascot->state || !m_mascot->state->env) return false;
    if (m_mascot->state->dragging || m_moveAnimation != nullptr) return false;

    auto env = m_mascot->state->env;
    if (!env->active_ie.visible()) return false;

    auto &ie = env->active_ie;
    auto &anchor = m_mascot->state->anchor;

    // 如果已经在空中、窗口顶、或者爬墙中，不重复触发
    QString cur = currentBehaviorName();
    if (cur.contains("Jump") || cur.contains("Fall") || cur.contains("IECeiling") || cur.contains("Climb")) {
        return false;
    }

    static qint64 s_lastJumpTime = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if ((now - s_lastJumpTime) < 4000) {
        return false;
    }

    // 1. 窗口在桌宠正上方 (距离 0 ~ 220px) -> 纵身向上起跳跃上窗口底部
    if (anchor.x >= (ie.left - 20.0) && anchor.x <= (ie.right + 20.0)) {
        double verticalDist = anchor.y - ie.bottom;
        if (verticalDist >= 0.0 && verticalDist <= 220.0) {
            s_lastJumpTime = now;
            std::cout << "[灵动感知] 发现窗口在头顶下方，主动起跳抓取窗口!" << std::endl;
            m_mascot->next_behavior("JumpFromBottomOfIE");
            return true;
        }
    }

    // 2. 窗口在桌宠右侧 (距离 0 ~ 140px，且高度在窗口范围内) -> 跳向窗口左边壁
    if (anchor.x < ie.left && (ie.left - anchor.x) <= 140.0 &&
        anchor.y >= (ie.top - 50.0) && anchor.y <= (ie.bottom + 50.0)) {
        s_lastJumpTime = now;
        m_mascot->state->looking_right = true;
        std::cout << "[灵动感知] 发现窗口在右侧，跳向窗口左壁!" << std::endl;
        m_mascot->next_behavior("JumpOnIELeftWall");
        return true;
    }

    // 3. 窗口在桌宠左侧 (距离 0 ~ 140px，且高度在窗口范围内) -> 跳向窗口右边壁
    if (anchor.x > ie.right && (anchor.x - ie.right) <= 140.0 &&
        anchor.y >= (ie.top - 50.0) && anchor.y <= (ie.bottom + 50.0)) {
        s_lastJumpTime = now;
        m_mascot->state->looking_right = false;
        std::cout << "[灵动感知] 发现窗口在左侧，跳向窗口右壁!" << std::endl;
        m_mascot->next_behavior("JumpOnIERightWall");
        return true;
    }

    return false;
}

void ShijimaWidget::queueOrShowMessage(const QString &text, int duration, const QString &appTarget, bool moveToCenter, std::function<void()> onStart, bool forceCompact, bool forceImmediate, bool allowReply) {
    (void)moveToCenter;
    if (m_messageBubble == nullptr || !m_mascot || !m_mascot->state || !m_mascot->state->env) {
        return;
    }

    // 关键防冲突队列保护机制：
    // 若当前气泡正在展示且倒计时仍在运行（用户正在阅读任务结果/交付/重要提醒），严禁粗暴打断！
    // 放入待播报队列排队，等待当前气泡自然阅读倒计时归零关闭后，优雅留白 2.2 秒再弹出。
    // 但当指定 forceImmediate（如答题反馈、用户主动操作）时，立刻原地替换展示，不进入等待队列。
    if (!forceImmediate && m_messageBubble->isDisplaying() && m_messageBubble->isCountdownRunning()) {
        std::cout << "[气泡防冲突队列] 检测到当前有正在阅读的弹窗 (剩余倒计时 " << m_messageBubble->remainingSeconds()
                  << "s)，新动作与消息已加入队列排队等待: " << text.left(25).toStdString() << std::endl;
        m_bubbleQueue.enqueue({text, duration, appTarget, moveToCenter, forceCompact, allowReply, onStart});
        return;
    }

    // 当前无弹窗或仅为无倒计时的进度提示，立即执行伴随动作与气泡展示
    if (onStart) {
        onStart();
    }

    m_isRunningToCenter = false;
    if (m_moveAnimation != nullptr) {
        m_moveAnimation->stop();
        delete m_moveAnimation;
        m_moveAnimation = nullptr;
    }

    // 设置当前发言桌宠为活跃对象
    BehaviorEngine::instance()->setActiveWidget(this);

    // 说话弹提示时如果正在奔跑、走动、攀爬或起跳，强制原地乖巧坐下或面对鼠标，避免一边说话一边全屏乱跑
    if (m_mascot && m_mascot->state) {
        QString beh = currentBehaviorName();
        if (beh.contains("Run", Qt::CaseInsensitive) ||
            beh.contains("Walk", Qt::CaseInsensitive) ||
            beh.contains("Climb", Qt::CaseInsensitive) ||
            beh.contains("Jump", Qt::CaseInsensitive)) {
            if (!trySetBehavior("SitAndFaceMouse")) {
                if (!trySetBehavior("SitDown")) {
                    trySetBehavior("StandUp");
                }
            }
        }
    }

    // 立即在当前桌宠头顶弹出气泡并计算贴合位置
    m_messageBubble->showMessage(text, duration, appTarget, forceCompact, allowReply);
    updateOffsets();
}

void ShijimaWidget::processNextQueuedMessage() {
    if (m_bubbleQueue.isEmpty()) return;

    // 前一个弹窗关闭后，静默留白 2200 毫秒（2.2秒），不抢视觉，给用户留出自然的呼吸与空歇期
    QTimer::singleShot(2200, this, [this]() {
        if (m_bubbleQueue.isEmpty()) return;
        auto item = m_bubbleQueue.dequeue();
        std::cout << "[气泡防冲突队列] 优雅留白结束，弹出下一个队列动作与气泡: " << item.text.left(25).toStdString() << std::endl;
        if (item.onStart) {
            item.onStart();
        }
        m_isRunningToCenter = false;
        if (m_moveAnimation != nullptr) {
            m_moveAnimation->stop();
            delete m_moveAnimation;
            m_moveAnimation = nullptr;
        }
        BehaviorEngine::instance()->setActiveWidget(this);
        if (m_mascot && m_mascot->state) {
            QString beh = currentBehaviorName();
            if (beh.contains("Run", Qt::CaseInsensitive) ||
                beh.contains("Walk", Qt::CaseInsensitive) ||
                beh.contains("Climb", Qt::CaseInsensitive) ||
                beh.contains("Jump", Qt::CaseInsensitive)) {
                if (!trySetBehavior("SitAndFaceMouse")) {
                    if (!trySetBehavior("SitDown")) {
                        trySetBehavior("StandUp");
                    }
                }
            }
        }
        m_messageBubble->showMessage(item.text, item.duration, item.appTarget, item.forceCompact, item.allowReply);
        updateOffsets();
    });
}

void ShijimaWidget::showMessage(QString const& text, int duration, QString const& appTarget, bool moveToCenter, bool forceCompact, bool forceImmediate, bool allowReply) {
    queueOrShowMessage(text, duration, appTarget, moveToCenter, nullptr, forceCompact, forceImmediate, allowReply);
}

void ShijimaWidget::hideMessage() {
    m_isTaskBubbleUserOpened = false;
    if (m_messageBubble != nullptr) {
        m_messageBubble->hideMessage();
    }
}

bool ShijimaWidget::trySetBehavior(const std::string &name) {
    if (!m_mascot) return false;
    auto b = m_mascot->initial_behavior_list().find(name, false);
    if (b != nullptr) {
        m_mascot->next_behavior(name);
        return true;
    }
    return false;
}

void ShijimaWidget::doAction(const PetActionCommand &cmd) {
    if (!m_mascot || !m_mascot->state) return;

    // 彻底取消自发飘字，避免不断创建临时窗口导致输入失焦
    (void)cmd.moodDelta;
    (void)cmd.affectionDelta;

    // 1. 如果有伴随的气泡文本，展示气泡（带 moveToCenter 控制）
    if (!cmd.speechText.isEmpty()) {
        showMessage(cmd.speechText, cmd.durationMs > 0 ? cmd.durationMs : 4000, cmd.appTarget, cmd.moveToCenter, false, cmd.forceImmediate, cmd.allowReply);
    }

    // 触发情绪贴纸与弹性动作
    if (cmd.emote != PetEmoteType::None) {
        m_motion.triggerEmote(cmd.emote, cmd.durationMs > 0 ? cmd.durationMs / 1000.0f : 2.5f);
    } else {
        if (cmd.type == PetActionType::Happy) m_motion.triggerEmote(PetEmoteType::Sparkle, 2.5f);
        else if (cmd.type == PetActionType::Angry) m_motion.triggerEmote(PetEmoteType::AngryVein, 2.5f);
        else if (cmd.type == PetActionType::Sleep) m_motion.triggerEmote(PetEmoteType::SleepZzz, 3.5f);
    }

    if (cmd.type == PetActionType::Jump) {
        m_motion.triggerStretch(0.90f, 1.15f);
    }

    // 2. 根据动作类型调度底层 Behavior
    switch (cmd.type) {
        case PetActionType::Idle:
            if (!trySetBehavior("SitAndFaceMouse")) {
                trySetBehavior("StandUp");
            }
            break;
        case PetActionType::Walk:
            if (!trySetBehavior("WalkAlongWorkAreaFloor")) {
                trySetBehavior("WalkAlongIECeiling");
            }
            break;
        case PetActionType::Sit:
            if (!trySetBehavior("SitDown")) {
                trySetBehavior("SitAndFaceMouse");
            }
            break;
        case PetActionType::LieDown:
        case PetActionType::Sleep:
            if (!trySetBehavior("LieDown")) {
                trySetBehavior("SitWhileDanglingLegs");
            }
            break;
        case PetActionType::Jump:
            if (!trySetBehavior("JumpFromBottomOfIE")) {
                if (!trySetBehavior("WalkLeftAlongIEAndJump")) {
                    trySetBehavior("Fall");
                }
            }
            break;
        case PetActionType::Fall:
            trySetBehavior("Fall");
            break;
        case PetActionType::ChaseMouse:
        case PetActionType::LookAtCursor:
        case PetActionType::FollowCursor:
            if (!trySetBehavior("ChaseMouse")) {
                trySetBehavior("SitAndFaceMouse");
            }
            break;
        case PetActionType::Happy:
            if (!trySetBehavior("JumpFromBottomOfIE")) {
                if (!trySetBehavior("SitAndSpinHead")) {
                    trySetBehavior("WalkAlongWorkAreaFloor");
                }
            }
            break;
        case PetActionType::Angry:
            if (!trySetBehavior("Dragged")) {
                trySetBehavior("SitAndSpinHead");
            }
            break;
        case PetActionType::CustomBehavior:
            if (!cmd.customBehaviorName.isEmpty()) {
                trySetBehavior(cmd.customBehaviorName.toStdString());
            }
            break;
        case PetActionType::Talk:
            // 说话弹提示时，如果当前在跑动，原地安静坐下面对鼠标
            if (m_mascot && m_mascot->state) {
                if (!trySetBehavior("SitAndFaceMouse")) {
                    trySetBehavior("SitDown");
                }
            }
            break;
    }
}

void ShijimaWidget::setWaitingForAgent(bool waiting) {
    m_isWaitingForAgent = waiting;
    if (waiting && m_mascot && m_mascot->state) {
        auto spinBehavior = m_mascot->initial_behavior_list().find("SitAndSpinHead", false);
        if (spinBehavior != nullptr) {
            m_mascot->next_behavior("SitAndSpinHead");
        } else {
            m_mascot->next_behavior("SitDown");
        }
        updateOffsets();
        update();
    }
}

void ShijimaWidget::startTaskProgress(const QString &initialPrompt, bool showBubbleImmediately) {
    m_hasActiveTask = true;
    m_taskPercent = 5;
    m_taskStep = 1;
    m_taskActionTitle = initialPrompt.isEmpty() ? "正在分析并规划操作..." : initialPrompt;
    m_activeTaskCardMarkdown = QString("⚡ **任务正在执行中** · `第 1 步`\n`[ ▱▱▱▱▱▱▱▱▱▱ ] 5%`\n\n> 🎯 **当前操作**: %1\n\n[⏹ 终止任务](agent://cancel)").arg(m_taskActionTitle);
    m_isTaskBubbleUserOpened = showBubbleImmediately;
    m_taskPulsePhase = 0.0f;
    setWaitingForAgent(true);

    if (showBubbleImmediately) {
        showMessage(m_activeTaskCardMarkdown, 0, "", true, false, true, false);
    } else {
        // 默认保持非侵入式安静模式：执行过程不在桌面上持续弹窗打扰用户
        if (m_messageBubble && m_messageBubble->isDisplaying()) {
            m_messageBubble->hideMessage();
        }
    }
    update();
}

void ShijimaWidget::updateTaskProgress(const QString &progressCardMarkdown) {
    if (m_hasActiveTask && m_activeTaskCardMarkdown == progressCardMarkdown) {
        return;
    }
    if (!m_hasActiveTask) {
        m_hasActiveTask = true;
        setWaitingForAgent(true);
    }
    m_activeTaskCardMarkdown = progressCardMarkdown;

    // 解析当前进度百分比与步数
    static QRegularExpression percentRegex(R"((\d{1,3})%)");
    auto percentMatch = percentRegex.match(progressCardMarkdown);
    if (percentMatch.hasMatch()) {
        m_taskPercent = std::clamp(percentMatch.captured(1).toInt(), 0, 100);
    }

    static QRegularExpression stepRegex(R"(第\s*(\d+)\s*步)");
    auto stepMatch = stepRegex.match(progressCardMarkdown);
    if (stepMatch.hasMatch()) {
        m_taskStep = stepMatch.captured(1).toInt();
    }

    static QRegularExpression actionRegex(R"(当前操作\*?\*?:\s*([^\n\r]+))");
    auto actionMatch = actionRegex.match(progressCardMarkdown);
    if (actionMatch.hasMatch()) {
        m_taskActionTitle = actionMatch.captured(1).trimmed();
    }

    // 只有在用户主动点击微盘展开气泡且气泡当前正处于可见展示状态时，才同步刷新卡片步骤
    // 一旦用户主动点击关闭按钮 ✕ 或关闭气泡，绝不再弹窗打扰，仅静默渲染微盘进度
    if (m_isTaskBubbleUserOpened && m_messageBubble && m_messageBubble->isVisible()) {
        m_messageBubble->showMessage(m_activeTaskCardMarkdown, 0, "", false, false);
    }

    update();
}

void ShijimaWidget::finishTaskProgress(bool success, const QString &result, const QString &appTarget, const QString &originalQuestion) {
    m_hasActiveTask = false;
    m_isTaskBubbleUserOpened = false;
    m_taskPercent = 100;
    m_taskOrbRect = QRect(); // 清空区域，环形微盘消失
    setWaitingForAgent(false);

    if (m_isTaskOrbHovered) {
        m_isTaskOrbHovered = false;
        unsetCursor();
        setToolTip("");
    }

    // 任务交付：直接弹窗展示完整最终结果
    if (success) {
        BehaviorEngine::instance()->addAffection(3, 8);
        showMessage(result, 25000, appTarget, true, false, true, true);
        if (!originalQuestion.isEmpty()) {
            MessageHistoryManager::instance()->addRecord("ask", originalQuestion, result, appTarget);
        }
    } else {
        QString err = result.trimmed().isEmpty() ? "任务执行未成功完成" : result;
        showMessage("❌ " + err, 9000, appTarget, true, false, true, false);
    }
    update();
}

void ShijimaWidget::toggleTaskProgressBubble() {
    if (!m_hasActiveTask) return;

    if (m_messageBubble && m_messageBubble->isDisplaying()) {
        // 用户再次点击微盘，收起气泡
        m_isTaskBubbleUserOpened = false;
        m_messageBubble->hideMessage();
    } else {
        // 用户点击微盘，展开当前任务执行卡片
        m_isTaskBubbleUserOpened = true;
        QString cardText = m_activeTaskCardMarkdown;
        if (cardText.trimmed().isEmpty()) {
            cardText = QString("⚡ **任务正在执行中** · `第 %1 步`\n`[ ▱▱▱▱▱▱▱▱▱▱ ] %2%`\n\n> 🎯 **当前操作**: %3\n\n[⏹ 终止任务](agent://cancel)")
                       .arg(QString::number(m_taskStep), QString::number(m_taskPercent), m_taskActionTitle);
        }
        showMessage(cardText, 0, "", true, false, true, false);
    }
}

void ShijimaWidget::showTranslateDialog(QString const& text) {
    if (m_translateDialog == nullptr) {
        m_translateDialog = new TranslateDialog(m_windowedMode ? parentWidget() : nullptr);
        QPointer<ShijimaWidget> petPtr(this);
        m_translateDialog->onTranslated = [petPtr](const QString &, const QString &) {
            if (!petPtr) return;
            BehaviorEngine::instance()->addAffection(2, 5);
        };
    }
    m_translateDialog->promptForText(text);
}

void ShijimaWidget::onTranslateRequested(QString const& text) {
    if (text.trimmed().isEmpty()) {
        showTranslateDialog("");
        return;
    }
    showTranslateDialog(text);
}

void ShijimaWidget::onAskRequested(QString const& text) {
    setWaitingForAgent(true);
    if (m_askDialog != nullptr) {
        m_askDialog->promptForContext(text);
    }
}

void ShijimaWidget::showMessageHistory() {
    if (m_messageBubble != nullptr) {
        m_messageBubble->showHistoryDialog();
    }
}

void ShijimaWidget::showTimerManager() {
    if (m_timerDialog == nullptr) {
        m_timerDialog = new TimerListDialog(m_windowedMode ? parentWidget() : nullptr);
    }
    m_timerDialog->refreshList();
    m_timerDialog->show();
    m_timerDialog->raise();
    m_timerDialog->activateWindow();
}

void ShijimaWidget::onQuestionSubmitted(QString const& context, QString const& question) {
    startTaskProgress("正在分析问题...");

    QString effectiveContext = context;
    if (effectiveContext.trimmed().isEmpty() && m_messageBubble) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 topicAge = now - m_messageBubble->lastTopicTime();
        if (topicAge < 300000 && !m_messageBubble->lastTopicText().isEmpty()) { // 5分钟内的有效主动话题
            effectiveContext = QString("【桌面宠物刚才主动说】:\n%1").arg(m_messageBubble->lastTopicText());
        }
    }

    QPointer<ShijimaWidget> petPtr(this);
    AgentService::instance()->ask(effectiveContext, question,
        [petPtr](QString const& progressMsg) {
            if (!petPtr) return;
            petPtr->updateTaskProgress(progressMsg);
        },
        [petPtr, question](bool success, QString const& result, QString const& appTarget) {
            if (!petPtr) return;
            petPtr->finishTaskProgress(success, result, appTarget, question);
        });
}

void ShijimaWidget::handleUserBubbleReply(const QString &question, const QString &replyText) {
    QString trimmedReply = replyText.trimmed();
    if (trimmedReply.isEmpty()) return;

    QString lower = trimmedReply.toLower();
    bool isTaskOrAgent = (
        lower.contains("agent") || lower.contains("dsh") || lower.contains("智能体") ||
        lower.contains("分析") || lower.contains("执行") || lower.contains("运行") ||
        lower.contains("编写") || lower.contains("排查") || lower.contains("清理") ||
        lower.contains("脚本") || lower.contains("优化") || lower.contains("修改") ||
        lower.contains("skill") || lower.contains("技能") || lower.contains("电脑") ||
        lower.contains("桌面") || lower.contains("微信") || lower.contains("qq") ||
        lower.contains("飞书") || lower.contains("钉钉") || lower.contains("软件") ||
        lower.contains("应用") || lower.contains("打开") || lower.contains("点击") ||
        lower.contains("输入") || lower.contains("截图") || lower.contains("截屏") ||
        lower.contains("按键") || lower.contains("快捷键") || lower.contains("窗口") ||
        lower.contains("控制") || lower.contains("界面") || lower.contains("操作电脑")
    );
    if (!isTaskOrAgent) {
        for (const auto &skill : SkillManager::instance()->getAllSkills()) {
            if (skill.enabled) {
                if (lower.contains(skill.id.toLower()) || lower.contains(skill.name.toLower())) {
                    isTaskOrAgent = true;
                    break;
                }
            }
        }
    }

    std::cout << "[桌宠互动] 用户在气泡中回复桌宠: "
              << "桌宠前言=" << question.left(30).toStdString()
              << ", 主人追问/回复=" << trimmedReply.toStdString() << std::endl;

    // 1. 增加亲密度与心情
    BehaviorEngine::instance()->addAffection(4, 10);

    // 2. 状态反馈：若为任务指令则启动进度微盘静默追踪；若为日常闲聊则显示思考提示
    if (isTaskOrAgent) {
        startTaskProgress(trimmedReply);
    } else {
        showMessage("💭 正在认真思考...", 0, "", false, false, true, false);
    }

    // 3. 构建给 AgentService 的对话上下文 (承接上一轮用户提问与桌宠前言)
    QString lastUserQuery;
    const auto &hist = AgentService::instance()->memoryHistory();
    for (int i = hist.size() - 1; i >= 0; --i) {
        auto obj = hist[i].toObject();
        if (obj["role"].toString() == "user") {
            lastUserQuery = obj["content"].toString().trimmed();
            break;
        }
    }

    QString contextPrompt;
    if (!lastUserQuery.isEmpty()) {
        contextPrompt += QString("【主人上一轮提问】:\n%1\n\n").arg(lastUserQuery);
    }
    contextPrompt += QString("【桌面宠物刚才主动说】:\n%1").arg(question);

    bool mentionsResource = lower.contains("卡") || lower.contains("内存") || lower.contains("cpu") || 
                           lower.contains("硬盘") || lower.contains("磁盘") || lower.contains("配置") ||
                           lower.contains("资源") || lower.contains("硬件");
    if (mentionsResource) {
        SystemResourceStats stats = SystemObserver::instance()->getSystemResourceStats();
        contextPrompt += QString("\n\n【系统实时资源占用情况】:\n%1\n（请务必结合上述实际硬件比例数据，生动关切地回答主人，切忌凭空臆想）").arg(stats.formattedSummary());
    }

    // 判定是否包含追问/求证/详情意图
    bool isQuestion = trimmedReply.contains('?') || trimmedReply.contains(QString::fromUtf8("？")) ||
                      trimmedReply.contains("什么") || trimmedReply.contains("为什么") || trimmedReply.contains("为啥") ||
                      trimmedReply.contains("怎么") || trimmedReply.contains("谁") || trimmedReply.contains("哪") ||
                      trimmedReply.contains("细说") || trimmedReply.contains("展开") || trimmedReply.contains("查") ||
                      trimmedReply.contains("搜") || trimmedReply.contains("多少") || trimmedReply.contains("如何") ||
                      trimmedReply.contains("详细") || trimmedReply.contains("说说") || trimmedReply.contains("讲讲") ||
                      trimmedReply.endsWith("呢") || trimmedReply.endsWith("吗");

    if (isTaskOrAgent) {
        contextPrompt += "\n\n（提示：主人正在指派具体任务或调度智能体分析，请切实调度工具执行并给出详实专业的分析与方案）";
    } else if (!isQuestion) {
        contextPrompt += "\n\n（提示：主人做出了日常回应或闲聊反馈，请以生动可爱的桌宠伴侣口吻简短回应20~40字，若包含习惯偏好请记在心里）";
    } else {
        contextPrompt += "\n\n（提示：主人正在针对上述分享进行追问或请教！请结合上下文和联网检索直接给出准确、详实、条理分明的回答，切勿只说「我去查查」而没有实际内容，篇幅适中便于气泡阅读）";
    }

    // 主人的实际回复作为提问
    QString userQuery = trimmedReply;

    QPointer<ShijimaWidget> petPtr(this);
    AgentService::instance()->ask(contextPrompt, userQuery,
        [petPtr](const QString &progMsg) {
            if (!petPtr) return;
            // 无论是任务还是大模型检索/执行，中间过程一律通过静默进度微盘呈现，严禁强制弹窗打扰！
            petPtr->updateTaskProgress(progMsg);
        },
        [petPtr, question, trimmedReply, isTaskOrAgent](bool success, const QString &result, const QString &appTarget) {
            (void)appTarget;
            if (!petPtr) return;
            if (petPtr->hasActiveTask() || isTaskOrAgent) {
                petPtr->finishTaskProgress(success, result, appTarget, trimmedReply);
                return;
            }
            if (success && !result.trimmed().isEmpty()) {
                PetActionCommand cmd;
                cmd.type = PetActionType::Talk;
                cmd.speechText = result;
                cmd.durationMs = 16000;
                cmd.emote = PetEmoteType::Sparkle;
                cmd.forceImmediate = true;
                cmd.allowReply = true; // 允许继续聊下去，持续多轮交互
                petPtr->doAction(cmd);
                MessageHistoryManager::instance()->addRecord("ask", trimmedReply, result, appTarget);

                // 异步反思并提炼为长期记忆 (记住主人的回答与互动)
                AgentConfig cfg = AgentService::instance()->config();
                ModelProfile activeProfile = AgentService::instance()->getActiveProfile();
                QString apiBase = !activeProfile.apiBase.isEmpty() ? activeProfile.apiBase : cfg.apiBase;
                QString apiKey = !activeProfile.apiKey.isEmpty() ? activeProfile.apiKey : cfg.apiKey;
                QString model = !activeProfile.model.isEmpty() ? activeProfile.model : cfg.model;

                QString reflectionUserMsg = QString("桌宠前言: %1 -> 主人追问: %2").arg(question, trimmedReply);
                LongTermMemoryEngine::instance()->triggerAsyncReflection(reflectionUserMsg, result, apiBase, apiKey, model);
            } else {
                petPtr->showMessage("💖 喵呜~ 刚刚网络开小差了，主人再说一次好不好？🥺", 5000, "", false, false, true, true);
            }
        },
        true /* directLlmOnly: 桌宠互动必须直连大模型与全网搜索，绝不调度 AiPy 创建任务 */
    );
}

QString ShijimaWidget::currentBehaviorName() const {
    if (m_mascot && m_mascot->active_behavior()) {
        return QString::fromStdString(m_mascot->active_behavior()->name);
    }
    return "";
}

void ShijimaWidget::moveToCorner(bool toLeftCorner) {
    if (!m_mascot || !m_mascot->state || !m_mascot->state->env) return;

    auto env = m_mascot->state->env;
    double targetX = toLeftCorner ? (env->screen.left + 120.0) : (env->screen.left + env->screen.width() - 140.0);
    double targetY = env->floor.y > 0 ? env->floor.y : env->screen.bottom;

    double currentX = m_mascot->state->anchor.x;
    double currentY = m_mascot->state->anchor.y;

    if (m_moveAnimation != nullptr) {
        m_moveAnimation->stop();
        delete m_moveAnimation;
        m_moveAnimation = nullptr;
    }

    m_mascot->state->looking_right = (targetX >= currentX);

    // 尝试播放慢步/漫步姿态
    auto walkBehavior = m_mascot->initial_behavior_list().find("WalkAlongWorkAreaFloor", false);
    if (walkBehavior != nullptr) {
        m_mascot->next_behavior("WalkAlongWorkAreaFloor");
    }

    double distance = std::hypot(targetX - currentX, targetY - currentY);
    int animDuration = std::clamp(static_cast<int>(distance * 1.5), 800, 2000); // 慢步回巢

    m_isRunningToCenter = true;
    m_moveAnimation = new QVariantAnimation(this);
    m_moveAnimation->setDuration(animDuration);
    m_moveAnimation->setStartValue(0.0);
    m_moveAnimation->setEndValue(1.0);
    m_moveAnimation->setEasingCurve(QEasingCurve::InOutQuad);

    connect(m_moveAnimation, &QVariantAnimation::valueChanged, this, [this, currentX, currentY, targetX, targetY](QVariant const& value) {
        double t = value.toDouble();
        if (m_mascot && m_mascot->state) {
            double curX = currentX + (targetX - currentX) * t;
            double curY = currentY + (targetY - currentY) * t;
            m_mascot->state->anchor = { curX, curY };
            updateOffsets();
            update();
        }
    });

    connect(m_moveAnimation, &QVariantAnimation::finished, this, [this, targetX, targetY]() {
        m_isRunningToCenter = false;
        if (m_mascot && m_mascot->state) {
            m_mascot->state->anchor = { targetX, targetY };
            auto dangleBehavior = m_mascot->initial_behavior_list().find("SitWhileDanglingLegs", false);
            if (dangleBehavior != nullptr) {
                m_mascot->next_behavior("SitWhileDanglingLegs");
            } else {
                auto sitBehavior = m_mascot->initial_behavior_list().find("SitDown", false);
                if (sitBehavior != nullptr) {
                    m_mascot->next_behavior("SitDown");
                }
            }
        }
        updateOffsets();
        update();
        if (m_moveAnimation != nullptr) {
            m_moveAnimation->deleteLater();
            m_moveAnimation = nullptr;
        }
    });

    m_moveAnimation->start();
}

ShijimaWidget::PetInterruptedGoal ShijimaWidget::detectCurrentGoal() {
    if (!m_mascot || !m_mascot->state) return PetInterruptedGoal::None;
    QString beh = currentBehaviorName();
    auto env = m_mascot->state->env;
    const auto &anchor = m_mascot->state->anchor;

    // 1. 爬墙 / 爬天花板中（离地高度超过 80px）
    if ((beh.contains("Climb", Qt::CaseInsensitive) || beh.contains("Ceiling", Qt::CaseInsensitive)) &&
        env && anchor.y < (env->floor.y - 80.0)) {
        return PetInterruptedGoal::ClimbingCeiling;
    }
    // 2. 跳跃 / 扑向 / 扔窗口
    if (beh.contains("Jump", Qt::CaseInsensitive) || beh.contains("ThrowIE", Qt::CaseInsensitive)) {
        return PetInterruptedGoal::JumpingWindow;
    }
    // 3. 站在窗口上巡逻
    if (beh.contains("IECeiling", Qt::CaseInsensitive) || beh.contains("IEWall", Qt::CaseInsensitive) ||
        beh.contains("EdgeOfIE", Qt::CaseInsensitive)) {
        return PetInterruptedGoal::StandingOnWindow;
    }
    // 4. 分裂或拔萝卜拉同伴
    if (beh.contains("PullUp", Qt::CaseInsensitive) || beh.contains("Split", Qt::CaseInsensitive)) {
        return PetInterruptedGoal::Breeding;
    }
    // 5. 快速狂奔
    if (beh.contains("Run", Qt::CaseInsensitive)) {
        return PetInterruptedGoal::RunningFast;
    }
    return PetInterruptedGoal::None;
}

void ShijimaWidget::handlePatrolInterruption(const QString &interruptType, const QString &customDetail) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastInteractionAITime < 3500) {
        return; // 3.5 秒防抖防刷屏
    }
    m_lastInteractionAITime = now;

    QString goalDesc;
    switch (m_interruptedGoal) {
    case PetInterruptedGoal::ClimbingCeiling:
        goalDesc = "原本正奋力爬墙登顶天花板看风景";
        break;
    case PetInterruptedGoal::JumpingWindow:
        goalDesc = "原本正找准角度准备大显身手跳上活动窗口";
        break;
    case PetInterruptedGoal::StandingOnWindow:
        goalDesc = "原本站在活动窗口边缘威风凛凛地巡视巡逻";
        break;
    case PetInterruptedGoal::Breeding:
        goalDesc = "原本正蹲在地上使劲要把地里的小伙伴拔出来";
        break;
    case PetInterruptedGoal::RunningFast:
        goalDesc = "原本正在屏幕地面全速冲刺狂奔巡逻";
        break;
    default:
        goalDesc = "原本正在屏幕地面悠闲漫步漫游巡逻";
        break;
    }

    QString detail = customDetail;
    if (detail.isEmpty()) {
        if (interruptType == "throw" || interruptType == "throw_flick") {
            detail = QString("桌宠%1，突然被主人一把抓住并快速甩手丢飞了出去，在地上翻滚摔倒！").arg(goalDesc);
        } else if (interruptType == "relocate") {
            detail = QString("桌宠%1，被主人一把抓起拎到半空中打断，搬到了屏幕新位置放下。").arg(goalDesc);
        } else if (interruptType == "poke") {
            detail = QString("桌宠%1，被主人用鼠标戳了戳打断。").arg(goalDesc);
        } else if (interruptType == "petting") {
            detail = QString("主人正在用鼠标温柔抚摸桌宠的头和脸颊（摸头抚慰互动）。");
        } else if (interruptType == "window_vanished") {
            detail = QString("桌宠原本站在活动窗口边缘巡逻，主人突然关掉了该窗口，导致脚下一空直接摔落到了地面。");
        } else {
            detail = QString("桌宠%1，突然被打断了巡逻。").arg(goalDesc);
        }
    }

    QJsonObject petStateInfo;
    auto state = BehaviorEngine::instance()->state();
    petStateInfo["mood"] = state.mood;
    petStateInfo["affection"] = state.affection;
    petStateInfo["boredom"] = state.boredom;
    petStateInfo["energy"] = state.energy;
    petStateInfo["current_app"] = SystemObserver::instance()->currentActiveAppName();
    petStateInfo["window_title"] = SystemObserver::instance()->currentActiveWindowTitle();
    petStateInfo["document_path"] = SystemObserver::instance()->currentActiveDocumentPath();
    petStateInfo["content_summary"] = SystemObserver::instance()->currentActiveContentSummary();

    QStringList allowedBehaviors = {
        "RunAlongWorkAreaFloor (在地面全速狂奔巡逻，充满活力或急躁赶路)",
        "WalkAlongWorkAreaFloor (在地面悠闲漫步巡逻，心情平静温和)",
        "SitDown (原地乖巧坐下，适合休息或听话)",
        "LieDown (趴在地上摆烂/打滚，适合受挫、撒娇、疲劳或被扔出去摔倒)",
        "SitWhileDanglingLegs (坐在边缘晃悠小短腿，适合好奇看戏或悠闲休息)",
        "SitAndSpinHead (原地傲娇转头，适合生气、傲娇小脾气)",
        "SplitIntoTwo (分身出一个小伙伴，适合极度开心或想要帮手)",
        "PullUpShimeji (从地里拔出一个同伴，适合玩耍互动)",
        "JumpFromBottomOfIE (从地面向上方活动窗口奋力起跳，适合兴奋或向主人报告)",
        "ClimbAlongWall (沿墙壁向上攀爬，适合探险或想要登高)"
    };

    QPointer<ShijimaWidget> safePet(this);
    AgentService::instance()->requestPetInterruptionDecision(
        interruptType,
        detail,
        petStateInfo,
        allowedBehaviors,
        [safePet](bool success, const AIBehaviorIntent &intent) {
            if (!safePet || !success || intent.speech.trimmed().isEmpty()) return;

            QMetaObject::invokeMethod(safePet.data(), [safePet, intent]() {
                if (!safePet) return;
                safePet->queueOrShowMessage(
                    intent.speech,
                    4000,
                    "",
                    false,
                    [safePet, intent]() {
                        if (!safePet) return;
                        // 1. 切换动作行为
                        if (!intent.behavior.isEmpty()) {
                            safePet->trySetBehavior(intent.behavior.toStdString());
                        }

                        // 2. 表情贴纸
                        if (intent.emote == "💖" || intent.emote == "🌸") {
                            safePet->m_motion.triggerEmote(PetEmoteType::HappyHeart, 2.5f);
                        } else if (intent.emote == "✨" || intent.emote == "🐾") {
                            safePet->m_motion.triggerEmote(PetEmoteType::Sparkle, 2.5f);
                        } else if (intent.emote == "💢") {
                            safePet->m_motion.triggerEmote(PetEmoteType::AngryVein, 2.5f);
                        } else if (intent.emote == "💫") {
                            safePet->m_motion.triggerEmote(PetEmoteType::DizzySwirl, 2.5f);
                        } else if (intent.emote == "💡") {
                            safePet->m_motion.triggerEmote(PetEmoteType::ThinkingBulb, 2.5f);
                        } else if (intent.emote == "💤") {
                            safePet->m_motion.triggerEmote(PetEmoteType::SleepZzz, 2.5f);
                        }

                        // 3. 伴随生动动作反馈 (绝不进行非等比拉伸变形，保护像素画质)
                        if (intent.action == "bounce" || intent.action == "jump") {
                            safePet->m_motion.triggerEmote(PetEmoteType::Sparkle, 2.0f);
                        }

                        if (intent.blush) {
                            safePet->m_motion.spawnHeart(QPointF(0, -25.0f));
                        }
                    }
                );
            });
        }
    );
}

void ShijimaWidget::triggerTantrum(PetInterruptedGoal goal) {
    if (goal == PetInterruptedGoal::None) return;
    m_interruptedGoal = goal;
    handlePatrolInterruption(goal == PetInterruptedGoal::StandingOnWindow ? "window_vanished" : "tantrum");
}

void ShijimaWidget::checkWindowVanished() {
    if (!m_mascot || !m_mascot->state || !m_mascot->state->env) return;
    QString beh = currentBehaviorName();
    bool currentlyOnWindow = (beh.contains("IECeiling", Qt::CaseInsensitive) || 
                              beh.contains("IEWall", Qt::CaseInsensitive) ||
                              beh.contains("EdgeOfIE", Qt::CaseInsensitive));
    
    // 如果之前在窗口上，现在突然由于窗口被关闭/移走而进入下落状态
    if (m_wasOnWindow && !currentlyOnWindow && beh.contains("Fall", Qt::CaseInsensitive)) {
        m_wasOnWindow = false;
        m_interruptedGoal = PetInterruptedGoal::StandingOnWindow;
        handlePatrolInterruption("window_vanished");
    } else {
        m_wasOnWindow = currentlyOnWindow;
    }
}

void ShijimaWidget::triggerInteractionAI(const QString &interactionType) {
    handlePatrolInterruption(interactionType);
}


