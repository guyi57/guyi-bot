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

#include "ShijimaContextMenu.hpp"
#include "ShijimaWidget.hpp"
#include "ShijimaManager.hpp"
#include "BehaviorEngine.hpp"
#include "MusicPlayerDialog.hpp"
#include "DesktopLyricWidget.hpp"
#include "FileDisposalSequence.hpp"
#include "UpdateManager.hpp"
#include "UpdateDialog.hpp"
#include "AgentService.hpp"
#include "SettingsDb.hpp"
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QMap>
#include <QActionGroup>
#include <QRandomGenerator>

// 行为名称中文翻译映射表
static QString translateBehaviorName(const std::string &name) {
    static QMap<QString, QString> translations = {
        // 基础行为
        {"Fall", "下落"},
        {"Dragged", "被拖拽"},
        {"Thrown", "被投掷"},
        {"ChaseMouse", "追逐鼠标"},
        
        // 坐姿相关
        {"SitDown", "坐下"},
        {"SitAndFaceMouse", "坐着面向鼠标"},
        {"SitAndSpinHead", "坐着转头"},
        {"SitWhileDanglingLegs", "坐着晃腿"},
        {"StandUp", "站起来"},
        
        // 躺卧相关
        {"LieDown", "躺下"},
        
        // 行走与奔跑相关
        {"WalkAlongWorkAreaFloor", "沿地板行走"},
        {"RunAlongWorkAreaFloor", "沿地板奔跑"},
        {"WalkLeftAlongFloorAndSit", "向左走并坐下"},
        {"WalkRightAlongFloorAndSit", "向右走并坐下"},
        {"WalkLeftAndSit", "向左走并坐下"},
        {"WalkRightAndSit", "向右走并坐下"},
        {"WalkAndGrabBottomLeftWall", "走到左墙边"},
        {"WalkAndGrabBottomRightWall", "走到右墙边"},
        
        // 爬行相关
        {"CrawlAlongWorkAreaFloor", "沿地板爬行"},
        {"CrawlAlongIECeiling", "沿窗口顶部爬行"},
        
        // 墙壁与天花板攀爬相关
        {"ClimbAlongWall", "沿墙壁攀爬"},
        {"ClimbAlongCeiling", "沿天花板攀爬"},
        {"ClimbHalfwayAlongWall", "爬到半墙"},
        {"HoldOntoWall", "抓住墙壁"},
        {"FallFromWall", "从墙上掉落"},
        {"HoldOntoCeiling", "抓住天花板"},
        {"FallFromCeiling", "从天花板掉落"},
        {"GrabWorkAreaBottomLeftWall", "抓住左下墙角"},
        {"GrabWorkAreaBottomRightWall", "抓住右下墙角"},
        
        // 窗口 (IE / Window) 交互与攀爬
        {"HoldOntoIEWall", "抓住窗口侧边"},
        {"ClimbIEWall", "爬窗口侧边"},
        {"ClimbIEBottom", "爬窗口底部"},
        {"GrabIEBottomLeftWall", "抓住窗口左下角"},
        {"GrabIEBottomRightWall", "抓住窗口右下角"},
        {"WalkAlongIECeiling", "沿窗口顶部走"},
        {"RunAlongIECeiling", "沿窗口顶部跑"},
        {"SitOnTheLeftEdgeOfIE", "坐在窗口左边缘"},
        {"SitOnTheRightEdgeOfIE", "坐在窗口右边缘"},
        {"WalkLeftAlongIEAndSit", "在窗口上向左走并坐下"},
        {"WalkRightAlongIEAndSit", "在窗口上向右走并坐下"},
        {"WalkLeftAlongIEAndJump", "在窗口上向左走并跳下"},
        {"WalkRightAlongIEAndJump", "在窗口上向右走并跳下"},
        
        // 跳跃相关
        {"JumpFromBottomOfIE", "从底部跳起"},
        {"JumpFromLeftEdgeOfIE", "从窗口左边跳下"},
        {"JumpFromRightEdgeOfIE", "从窗口右边跳下"},
        {"JumpFromLeftWall", "从左墙跳出"},
        {"JumpFromRightWall", "从右墙跳出"},
        {"JumpOnIELeftWall", "跳到窗口左侧"},
        {"JumpOnIERightWall", "跳到窗口右侧"},
        {"Jump", "跳跃"},
        
        // 搬运与扔窗口相关
        {"ThrowIEFromLeft", "从左侧扔窗口"},
        {"ThrowIEFromRight", "从右侧扔窗口"},
        {"WalkAndThrowIEFromLeft", "从左侧走并扔窗口"},
        {"WalkAndThrowIEFromRight", "从右侧走并扔窗口"},
        
        // 繁殖与同伴相关
        {"SplitIntoTwo", "分裂成两个"},
        {"PullUpShimeji", "拉起桌宠"},
        {"PullUp", "被拉起"},
        {"Divided", "被分裂"},
        
        // 其他趣味行为
        {"Yawn", "打哈欠"},
        {"Sleep", "睡觉"},
        {"Wave", "挥手"},
        {"Dance", "跳舞"},
        {"Spin", "旋转"}
    };
    
    QString qname = QString::fromStdString(name);
    return translations.value(qname, qname);
}

ShijimaContextMenu::ShijimaContextMenu(ShijimaWidget *parent)
    : QMenu("右键菜单", parent)
{
    QAction *action;
    QPointer<ShijimaWidget> petPtr = parent;

    // 1. 行为 (Behaviors menu: 状态卡片、智能交互、动作姿态、同伴互动)
    {
        auto behaviorsMenu = addMenu("行为");

        // 顶部桌宠状态展示卡片
        const auto &st = BehaviorEngine::instance()->state();
        QString staminaBar = "";
        int filled = std::clamp(st.stamina / 10, 0, 10);
        for (int i = 0; i < 10; ++i) {
            staminaBar += (i < filled ? "■" : "□");
        }
        QString statusText = QString("⚡ 体力: %1% [%2] %3")
            .arg(st.stamina)
            .arg(staminaBar)
            .arg(st.isRestingInCorner ? "💤 (休整中)" : "🌟 (元气满满)");
        action = behaviorsMenu->addAction(statusText);
        action->setEnabled(false);

        QString moodText = QString("😊 心情: %1  |  💕 亲密: %2")
            .arg(st.mood)
            .arg(st.affection);
        action = behaviorsMenu->addAction(moodText);
        action->setEnabled(false);

        behaviorsMenu->addSeparator();

        // 智能与趣味互动
        action = behaviorsMenu->addAction("💬 向 AI 提问");
        connect(action, &QAction::triggered, [petPtr](){
            if (petPtr) petPtr->onAskRequested("");
        });

        action = behaviorsMenu->addAction("📖 桌宠私密日记...");
        connect(action, &QAction::triggered, [petPtr](){
            if (petPtr) petPtr->showDiary();
        });

        action = behaviorsMenu->addAction("📜 消息与任务历史");
        connect(action, &QAction::triggered, [petPtr](){
            if (petPtr) petPtr->showMessageHistory();
        });

        action = behaviorsMenu->addAction("⏰ 定时任务管理");
        connect(action, &QAction::triggered, [petPtr](){
            if (petPtr) petPtr->showTimerManager();
        });

        action = behaviorsMenu->addAction("🎵 音乐工坊 (⌥M)");
        connect(action, &QAction::triggered, [](){
            MusicPlayerDialog::instance()->toggleVisibility();
        });

        QMenu *lyricMenu = behaviorsMenu->addMenu("💬 桌面歌词设置");
        DesktopLyricWidget::instance()->populateSettingsMenu(lyricMenu);

        action = behaviorsMenu->addAction("🕳️ 黑洞吞噬本地文件...");
        connect(action, &QAction::triggered, [petPtr](){
            if (!petPtr) return;
            QPoint mousePos = QCursor::pos();
            QString filePath = QFileDialog::getOpenFileName(
                nullptr,
                "选择要让桌宠推入黑洞吞噬的文件",
                QDir::homePath() + "/Desktop",
                "所有文件 (*.*)"
            );
            if (!filePath.isEmpty() && petPtr) {
                QFileInfo fi(filePath);
                FileDisposalSequence::instance()->start(petPtr.data(), fi.fileName(), filePath, mousePos);
            }
        });

        action = behaviorsMenu->addAction("🔍 桌宠检查器");
        connect(action, &QAction::triggered, [petPtr](){
            if (petPtr) petPtr->showInspector();
        });

        behaviorsMenu->addSeparator();

        // 🎭 动作姿态子菜单 (包含原有 40+ 种动作动画)
        auto posesMenu = behaviorsMenu->addMenu("🎭 动作姿态");
        std::vector<std::string> behaviors;
        auto &list = parent->m_mascot->initial_behavior_list();
        auto flat = list.flatten_unconditional();
        for (auto &behavior : flat) {
            if (!behavior->hidden) {
                behaviors.push_back(behavior->name);
            }
        }
        for (std::string &behavior : behaviors) {
            QString displayName = translateBehaviorName(behavior);
            action = posesMenu->addAction(displayName);
            connect(action, &QAction::triggered, [petPtr, behavior](){
                if (petPtr && petPtr->m_mascot) {
                    petPtr->m_mascot->next_behavior(behavior);
                }
            });
        }

        behaviorsMenu->addSeparator();

        // 同伴与多桌宠操作
        action = behaviorsMenu->addAction("➕ 召唤同伴");
        connect(action, &QAction::triggered, [petPtr](){
            if (petPtr) {
                auto bType = ShijimaManager::defaultManager()->breedingType();
                if (bType == ShijimaManager::BreedingType::RandomMascot) {
                    auto &mascots = ShijimaManager::defaultManager()->loadedMascots();
                    if (!mascots.isEmpty()) {
                        auto keys = mascots.keys();
                        int idx = QRandomGenerator::global()->bounded(keys.size());
                        ShijimaManager::defaultManager()->spawn(keys[idx].toStdString());
                        return;
                    }
                }
                ShijimaManager::defaultManager()->spawn(petPtr->mascotName().toStdString());
            }
        });

        action = behaviorsMenu->addAction("⚔️ 主宠清理克隆体");
        connect(action, &QAction::triggered, [](){
            ShijimaManager::defaultManager()->startEliminateClones(nullptr, true);
        });

        action = behaviorsMenu->addAction("👤 立即只保留一个");
        connect(action, &QAction::triggered, [petPtr](){
            if (petPtr) {
                ShijimaManager::defaultManager()->killAllButOne(petPtr.data());
            }
        });

        action = behaviorsMenu->addAction("❌ 全部关闭");
        connect(action, &QAction::triggered, [](){
            ShijimaManager::defaultManager()->killAll();
        });
    }

    // 2. 繁殖模式专属菜单
    {
        auto breedMenu = addMenu("🧬 繁殖模式");
        bool breedEnabled = ShijimaManager::defaultManager()->isBreedingEnabled();
        action = breedMenu->addAction("启用繁殖模式");
        action->setCheckable(true);
        action->setChecked(breedEnabled);
        connect(action, &QAction::triggered, [petPtr](bool checked){
            ShijimaManager::defaultManager()->setBreedingEnabled(checked);
            if (petPtr) {
                if (checked) {
                    petPtr->showMessage("🧬 繁殖模式已开启！桌宠可以自主分裂繁衍啦~", 2500);
                } else {
                    petPtr->showMessage("🛑 繁殖模式已关闭，停止克隆分裂。", 2500);
                }
            }
        });

        breedMenu->addSeparator();

        auto *styleGroup = new QActionGroup(breedMenu);
        auto breedType = ShijimaManager::defaultManager()->breedingType();

        action = breedMenu->addAction("🔘 克隆同款桌宠");
        action->setCheckable(true);
        action->setActionGroup(styleGroup);
        action->setChecked(breedType == ShijimaManager::BreedingType::SameMascot);
        connect(action, &QAction::triggered, [petPtr](){
            ShijimaManager::defaultManager()->setBreedingType(ShijimaManager::BreedingType::SameMascot);
            if (petPtr) petPtr->showMessage("🧬 繁殖样式: 仅克隆同款桌宠", 2000);
        });

        action = breedMenu->addAction("🎲 随机已有样式");
        action->setCheckable(true);
        action->setActionGroup(styleGroup);
        action->setChecked(breedType == ShijimaManager::BreedingType::RandomMascot);
        connect(action, &QAction::triggered, [petPtr](){
            ShijimaManager::defaultManager()->setBreedingType(ShijimaManager::BreedingType::RandomMascot);
            if (petPtr) petPtr->showMessage("🧬 繁殖样式: 随机所有已安装桌宠", 2000);
        });

        breedMenu->addSeparator();

        action = breedMenu->addAction("⚔️ 主宠清理克隆体");
        connect(action, &QAction::triggered, [](){
            ShijimaManager::defaultManager()->startEliminateClones(nullptr, true);
        });

        action = breedMenu->addAction("👤 立即只保留一个");
        connect(action, &QAction::triggered, [petPtr](){
            if (petPtr) {
                ShijimaManager::defaultManager()->killAllButOne(petPtr.data());
            }
        });
    }

    // 3. 显示管理器
    action = addAction("显示管理器");
    connect(action, &QAction::triggered, [](){
        ShijimaManager::defaultManager()->setManagerVisible(true);
    });

    // 3. 暂停
    action = addAction("暂停");
    action->setCheckable(true);
    action->setChecked(parent->m_paused);
    connect(action, &QAction::triggered, [this](bool checked){
        shijimaParent()->m_paused = checked;
    });

    // 4. 设置
    action = addAction("设置");
    connect(action, &QAction::triggered, [this](){
        shijimaParent()->showAgentSettings();
    });

    addSeparator();

    // 5. 关闭
    action = addAction("关闭");
    connect(action, &QAction::triggered, parent, &ShijimaWidget::closeAction);
}

void ShijimaContextMenu::closeEvent(QCloseEvent *event) {
    shijimaParent()->contextMenuClosed(event);
    QMenu::closeEvent(event);
}

/*
ShijimaContextMenu::~ShijimaContextMenu() {
    auto allActions = actions();
    for (QAction *action : allActions) {
        removeAction(action);
        delete action;
    }
}
*/
