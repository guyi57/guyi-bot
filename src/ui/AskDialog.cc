#include "AskDialog.hpp"
#include "AgentService.hpp"
#include "DeepSeekHarnessAdapter.hpp"
#include "SettingsDb.hpp"
#include "Platform/Platform.hpp"
#include <QHBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QCursor>
#include <cmath>

AskDialog::AskDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("向桌宠 AI 提问");
    setMinimumWidth(460);
    setAcceptDrops(true);

    bool isShortcut = SettingsDb::instance()->getBool("ask_dialog.shortcut_mode", false);
    m_displayMode = isShortcut ? DisplayMode::ShortcutBar : DisplayMode::Standard;
    m_autoHideEnabled = SettingsDb::instance()->getBool("ask_dialog.auto_hide", true);

    m_autoHideTimer = new QTimer(this);
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, &QTimer::timeout, this, &AskDialog::checkAutoHideCondition);

    auto rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_containerWidget = new QWidget(this);
    m_containerWidget->setObjectName("askContainer");
    rootLayout->addWidget(m_containerWidget);

    auto mainLayout = new QVBoxLayout(m_containerWidget);
    mainLayout->setContentsMargins(16, 14, 16, 10);
    mainLayout->setSpacing(10);

    // 1. 顶部控制栏 (标题 + 模型切换 + 贴边自动隐藏开关 + 模式切换开关 + 关闭按钮)
    auto headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_titleLabel = new QLabel("💬 向 AI 提问", this);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #1e293b;");
    headerLayout->addWidget(m_titleLabel, 1);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setStyleSheet("padding: 3px 8px; font-size: 11px; border-radius: 6px; border: 1px solid #cbd5e1; background: #f8fafc; color: #475569; font-weight: 500;");
    m_modelCombo->setToolTip("切换当前提问所使用的基础大模型");
    headerLayout->addWidget(m_modelCombo, 0);

    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) return;
        QString pId = m_modelCombo->itemData(index).toString();
        if (!pId.isEmpty()) {
            AgentService::instance()->setActiveProfile(pId);
        }
    });

    // 贴边自动隐藏开关
    m_autoHideToggleBtn = new QPushButton(this);
    m_autoHideToggleBtn->setCursor(Qt::PointingHandCursor);
    m_autoHideToggleBtn->setAutoDefault(false);
    m_autoHideToggleBtn->setDefault(false);
    m_autoHideToggleBtn->setFocusPolicy(Qt::NoFocus);
    connect(m_autoHideToggleBtn, &QPushButton::clicked, this, &AskDialog::toggleAutoHide);
    headerLayout->addWidget(m_autoHideToggleBtn, 0);

    m_modeToggleBtn = new QPushButton(this);
    m_modeToggleBtn->setCursor(Qt::PointingHandCursor);
    m_modeToggleBtn->setAutoDefault(false);
    m_modeToggleBtn->setDefault(false);
    m_modeToggleBtn->setFocusPolicy(Qt::NoFocus);
    headerLayout->addWidget(m_modeToggleBtn, 0);

    m_closeTopBtn = new QPushButton("✕", this);
    m_closeTopBtn->setFixedSize(22, 22);
    m_closeTopBtn->setCursor(Qt::PointingHandCursor);
    m_closeTopBtn->setAutoDefault(false);
    m_closeTopBtn->setDefault(false);
    m_closeTopBtn->setFocusPolicy(Qt::NoFocus);
    m_closeTopBtn->setToolTip("关闭 (Esc)");
    m_closeTopBtn->setStyleSheet(
        "QPushButton {"
        "  border: none; border-radius: 11px;"
        "  background: #f1f5f9; color: #64748b;"
        "  font-weight: bold; font-size: 11px;"
        "}"
        "QPushButton:hover { background: #fee2e2; color: #ef4444; }"
    );
    headerLayout->addWidget(m_closeTopBtn, 0);

    connect(m_modeToggleBtn, &QPushButton::clicked, this, &AskDialog::toggleDisplayMode);
    connect(m_closeTopBtn, &QPushButton::clicked, this, [this]() {
        if (m_displayMode == DisplayMode::ShortcutBar && m_snapState != EdgeSnapState::None && m_autoHideEnabled) {
            slideToHidden();
            return;
        }
        if (onCancel) onCancel();
        hide();
    });

    mainLayout->addLayout(headerLayout);

    // 2. 任务会话延续提示与新建任务按钮
    m_sessionBanner = new QWidget(this);
    m_sessionBanner->setStyleSheet("background-color: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 6px;");
    auto sessionLayout = new QHBoxLayout(m_sessionBanner);
    sessionLayout->setContentsMargins(8, 4, 8, 4);
    sessionLayout->setSpacing(6);

    m_sessionLabel = new QLabel("🔄 续接上个任务会话（上下文与生成物自动连贯继承）", m_sessionBanner);
    m_sessionLabel->setStyleSheet("color: #166534; font-size: 11px; font-weight: 500; border: none; background: transparent;");
    sessionLayout->addWidget(m_sessionLabel, 1);

    m_resetSessionBtn = new QPushButton("✨ 新建独立任务", m_sessionBanner);
    m_resetSessionBtn->setStyleSheet(
        "QPushButton {"
        "  border: 1px solid #86efac; border-radius: 4px;"
        "  background: #ffffff; color: #15803d;"
        "  padding: 2px 8px; font-size: 11px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: #dcfce7; }"
    );
    m_resetSessionBtn->setCursor(Qt::PointingHandCursor);
    m_resetSessionBtn->setToolTip("遗忘上轮生成任务与上下文，开启全新干净的提问会话");
    connect(m_resetSessionBtn, &QPushButton::clicked, this, [this]() {
        auto *dsh = AgentService::instance()->dshAdapter();
        if (dsh) {
            dsh->resetSession();
        }
        m_sessionBanner->hide();
        adjustSize();
    });
    sessionLayout->addWidget(m_resetSessionBtn, 0);
    mainLayout->addWidget(m_sessionBanner);
    m_sessionBanner->hide();

    // 3. 选中文本/上下文预览栏
    m_contextWidget = new QWidget(this);
    m_contextWidget->setStyleSheet("background-color: #f1f5f9; border-radius: 6px; padding: 4px; border: 1px dashed #cbd5e1;");
    auto contextLayout = new QHBoxLayout(m_contextWidget);
    contextLayout->setContentsMargins(6, 4, 6, 4);
    contextLayout->setSpacing(6);

    m_previewLabel = new QLabel(m_contextWidget);
    m_previewLabel->setStyleSheet("color: #4b5563; font-size: 11px; border: none; background: transparent;");
    m_previewLabel->setWordWrap(true);
    m_previewLabel->setMaximumHeight(80);
    contextLayout->addWidget(m_previewLabel, 1);

    m_clearContextBtn = new QPushButton("✕", m_contextWidget);
    m_clearContextBtn->setToolTip("清除选中文本引用（改为自由提问）");
    m_clearContextBtn->setFixedSize(20, 20);
    m_clearContextBtn->setCursor(Qt::PointingHandCursor);
    m_clearContextBtn->setStyleSheet(
        "QPushButton {"
        "  border: none;"
        "  border-radius: 10px;"
        "  background-color: #e2e8f0;"
        "  color: #64748b;"
        "  font-weight: bold;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #ef4444;"
        "  color: white;"
        "}"
    );
    contextLayout->addWidget(m_clearContextBtn, 0, Qt::AlignTop);

    mainLayout->addWidget(m_contextWidget);

    connect(m_clearContextBtn, &QPushButton::clicked, this, [this]() {
        m_contextText.clear();
        m_droppedPath.clear();
        m_contextWidget->hide();
        m_titleLabel->setText(m_displayMode == DisplayMode::ShortcutBar ? "⋮⋮ 💬 快捷提问 (吸附贴边)" : "💬 自由向 AI 提问 (保留历史记忆)");
        adjustSize();
        m_inputEdit->setFocus();
    });

    // 4. 输入栏与内嵌发送按钮
    auto inputRowLayout = new QHBoxLayout();
    inputRowLayout->setContentsMargins(0, 0, 0, 0);
    inputRowLayout->setSpacing(8);

    m_inputEdit = new QLineEdit(this);
    m_inputEdit->setPlaceholderText("💡 随时向 AI 提问 (Enter 发送，Esc 隐藏)...");
    m_inputEdit->setStyleSheet(
        "QLineEdit {"
        "  border: 1.5px solid #cbd5e1;"
        "  border-radius: 8px;"
        "  padding: 8px 12px;"
        "  font-size: 12.5px;"
        "  background: #ffffff;"
        "  color: #1e293b;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #6366f1;"
        "  background: #ffffff;"
        "}"
    );
    inputRowLayout->addWidget(m_inputEdit, 1);

    m_sendBtn = new QPushButton("发送 ↵", this);
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setAutoDefault(true);
    m_sendBtn->setDefault(true);
    m_sendBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6366f1, stop:1 #4f46e5);"
        "  color: white; border: none; border-radius: 8px;"
        "  padding: 8px 16px; font-weight: bold; font-size: 12px;"
        "}"
        "QPushButton:hover { background: #4338ca; }"
        "QPushButton:pressed { background: #3730a3; }"
    );
    inputRowLayout->addWidget(m_sendBtn, 0);

    mainLayout->addLayout(inputRowLayout);

    // 5. 底部快捷指令胶囊
    setupQuickPills(mainLayout);

    // 6. 边缘抽屉吸附呼吸指示线 (当隐藏至屏幕边缘时，露出一条科技感细线以便鼠标滑过唤醒)
    m_edgeIndicatorLine = new QWidget(m_containerWidget);
    m_edgeIndicatorLine->setFixedHeight(3);
    m_edgeIndicatorLine->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "stop:0 rgba(99, 102, 241, 0.15), stop:0.5 rgba(99, 102, 241, 0.95), stop:1 rgba(99, 102, 241, 0.15)); "
        "border-radius: 1.5px; margin: 0 45px;"
    );
    m_edgeIndicatorLine->setToolTip("💬 快捷提问栏（鼠标滑过自动呼出，移开自动隐藏）");
    mainLayout->addWidget(m_edgeIndicatorLine);

    // 7. 独立底部按钮栏 (标准弹窗模式下显示取消按钮)
    m_bottomBtnWidget = new QWidget(this);
    auto btmLayout = new QHBoxLayout(m_bottomBtnWidget);
    btmLayout->setContentsMargins(0, 0, 0, 0);
    btmLayout->addStretch();
    m_cancelBtn = new QPushButton("取消", m_bottomBtnWidget);
    m_cancelBtn->setAutoDefault(false);
    m_cancelBtn->setDefault(false);
    m_cancelBtn->setFocusPolicy(Qt::NoFocus);
    m_cancelBtn->setStyleSheet("padding: 5px 14px; border-radius: 6px; border: 1px solid #dcdfe6; background: #fff; font-size: 12px;");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    btmLayout->addWidget(m_cancelBtn);
    mainLayout->addWidget(m_bottomBtnWidget);

    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        if (onCancel) onCancel();
        reject();
    });

    auto doSend = [this]() {
        QString q = m_inputEdit->text().trimmed();
        if (!q.isEmpty()) {
            if (onSubmit) {
                onSubmit(m_contextText, q);
            }
            m_inputEdit->clear();
            if (m_displayMode == DisplayMode::ShortcutBar) {
                m_isDragging = false;
                updateEdgeSnapState();
                if (m_snapState == EdgeSnapState::None) {
                    m_snapState = EdgeSnapState::Top;
                }
                if (m_autoHideEnabled) {
                    slideToHidden();
                }
            } else {
                accept();
            }
        }
    };

    connect(m_sendBtn, &QPushButton::clicked, this, doSend);
    connect(m_inputEdit, &QLineEdit::returnPressed, this, doSend);

    applyModeFlagsAndStyle();
}

void AskDialog::applyModeFlagsAndStyle()
{
    if (m_displayMode == DisplayMode::ShortcutBar) {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        Platform::setupFloatingBubbleWindow(this);

        m_modeToggleBtn->setText("🪟 还原为弹窗");
        m_modeToggleBtn->setToolTip("切换为普通居中弹窗模式");
        m_modeToggleBtn->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #cbd5e1; border-radius: 6px; padding: 3px 8px;"
            "  font-size: 11px; background: #f8fafc; color: #475569; font-weight: 500;"
            "}"
            "QPushButton:hover { background: #f1f5f9; color: #1e293b; }"
        );

        m_autoHideToggleBtn->show();
        if (m_autoHideEnabled) {
            m_autoHideToggleBtn->setText("🧲 贴边隐藏: 开");
            m_autoHideToggleBtn->setToolTip("贴边自动隐藏已启用 (吸附屏幕边缘后只留一条小线，鼠标滑过自动呼出，用完自动收起)");
            m_autoHideToggleBtn->setStyleSheet(
                "QPushButton {"
                "  border: 1px solid #a5b4fc; border-radius: 6px; padding: 3px 8px;"
                "  font-size: 11px; background: #e0e7ff; color: #4338ca; font-weight: 600;"
                "}"
                "QPushButton:hover { background: #c7d2fe; }"
            );
        } else {
            m_autoHideToggleBtn->setText("📌 贴边隐藏: 关");
            m_autoHideToggleBtn->setToolTip("贴边自动隐藏已关闭 (固定显示在屏幕边缘，不自动收缩成线)");
            m_autoHideToggleBtn->setStyleSheet(
                "QPushButton {"
                "  border: 1px solid #cbd5e1; border-radius: 6px; padding: 3px 8px;"
                "  font-size: 11px; background: #f8fafc; color: #64748b; font-weight: 500;"
                "}"
                "QPushButton:hover { background: #f1f5f9; color: #1e293b; }"
            );
        }

        m_closeTopBtn->show();
        m_bottomBtnWidget->hide();
        m_edgeIndicatorLine->show();
        m_titleLabel->setText("⋮⋮ 💬 快捷提问 (吸附贴边)");
        m_inputEdit->setPlaceholderText("💬 随时向 AI 提问 (Enter 发送，Esc 隐藏，鼠标划离自动贴边收起)...");
        m_containerWidget->setStyleSheet(
            "QWidget#askContainer {"
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(255, 255, 255, 0.98), stop:1 rgba(248, 250, 252, 0.96));"
            "  border: 1.5px solid rgba(99, 102, 241, 0.55);"
            "  border-radius: 16px;"
            "}"
        );
    } else {
        setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_TranslucentBackground, false);

        m_modeToggleBtn->setText("📌 吸附菜单栏模式");
        m_modeToggleBtn->setToolTip("切换为快捷吸顶栏模式（可吸附于 macOS 菜单栏或 Windows 屏幕边缘并贴边隐藏）");
        m_modeToggleBtn->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #c7d2fe; border-radius: 6px; padding: 3px 8px;"
            "  font-size: 11px; background: #e0e7ff; color: #4338ca; font-weight: 600;"
            "}"
            "QPushButton:hover { background: #c7d2fe; color: #3730a3; }"
        );

        m_autoHideToggleBtn->hide();
        m_edgeIndicatorLine->hide();
        m_closeTopBtn->hide();
        m_bottomBtnWidget->show();
        m_titleLabel->setText("💬 向 AI 提问");
        m_inputEdit->setPlaceholderText("💡 试试: /dsh 分析系统存储占用 / 推荐点音乐 / 25分钟后提醒喝水...");
        m_containerWidget->setStyleSheet(
            "QWidget#askContainer {"
            "  background-color: #ffffff;"
            "  border: 1px solid #e2e8f0;"
            "  border-radius: 10px;"
            "}"
        );
    }
}

void AskDialog::setDisplayMode(DisplayMode mode, bool reposition)
{
    if (m_displayMode == mode) return;
    m_displayMode = mode;
    SettingsDb::instance()->setBool("ask_dialog.shortcut_mode", (mode == DisplayMode::ShortcutBar));

    bool wasVisible = isVisible();
    if (wasVisible) hide();

    applyModeFlagsAndStyle();

    if (reposition) {
        positionWindow();
    }

    if (onDisplayModeChanged) {
        onDisplayModeChanged(m_displayMode);
    }

    if (wasVisible) {
        show();
        raise();
        activateWindow();
        m_inputEdit->setFocus();
    }
}

void AskDialog::toggleDisplayMode()
{
    setDisplayMode(m_displayMode == DisplayMode::ShortcutBar ? DisplayMode::Standard : DisplayMode::ShortcutBar);
}

void AskDialog::setAutoHideEnabled(bool enabled)
{
    m_autoHideEnabled = enabled;
    SettingsDb::instance()->setBool("ask_dialog.auto_hide", enabled);
    if (!m_autoHideEnabled && m_isEdgeHidden) {
        slideToExpanded();
    } else if (m_autoHideEnabled && m_snapState != EdgeSnapState::None) {
        if (m_autoHideTimer) m_autoHideTimer->start(800);
    }
    applyModeFlagsAndStyle();
}

void AskDialog::toggleAutoHide()
{
    setAutoHideEnabled(!m_autoHideEnabled);
}

void AskDialog::positionWindow()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect avail = screen->availableGeometry();

    if (m_displayMode == DisplayMode::ShortcutBar) {
        int w = 560;
        resize(w, minimumSizeHint().height());

        // 如果用户之前拖拽过并记录了坐标，优先读取并校验是否在当前可用屏幕内
        if (SettingsDb::instance()->contains("ask_dialog.shortcut_x")) {
            int savedX = SettingsDb::instance()->getInt("ask_dialog.shortcut_x");
            int savedY = SettingsDb::instance()->getInt("ask_dialog.shortcut_y");
            if (savedX >= avail.left() - 100 && savedX <= avail.right() &&
                savedY >= avail.top() - 10 && savedY <= avail.bottom() - 80) {
                move(savedX, savedY);
                updateEdgeSnapState();
                if (m_autoHideEnabled && m_snapState != EdgeSnapState::None) {
                    if (m_autoHideTimer) m_autoHideTimer->start(1000);
                }
                return;
            }
        }
        // 默认居中置顶，紧贴 macOS 菜单栏下方 (或 Windows 屏幕顶部)
        int x = avail.left() + (avail.width() - width()) / 2;
        int y = avail.top() + 2;
        move(x, y);
        updateEdgeSnapState();
        if (m_autoHideEnabled && m_snapState != EdgeSnapState::None) {
            if (m_autoHideTimer) m_autoHideTimer->start(1000);
        }
    } else {
        resize(460, minimumSizeHint().height());
        // 标准模式屏幕中央居中
        move(avail.center().x() - width() / 2, avail.center().y() - height() / 2);
        m_snapState = EdgeSnapState::None;
        m_isEdgeHidden = false;
    }
}

void AskDialog::updateEdgeSnapState()
{
    if (m_displayMode != DisplayMode::ShortcutBar) {
        m_snapState = EdgeSnapState::None;
        return;
    }

    QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect avail = screen->availableGeometry();

    int topDist = std::abs(y() - avail.top());
    int leftDist = std::abs(x() - avail.left());
    int rightDist = std::abs((avail.right() - width()) - x());

    // 优先贴近顶部（macOS 菜单栏 / Windows 任务栏对侧）
    if (topDist <= 55 || y() <= avail.top() + 10) {
        m_snapState = EdgeSnapState::Top;
        m_expandedPos = QPoint(x(), avail.top() + 2);
    } else if (leftDist <= 55 || x() <= avail.left() + 10) {
        m_snapState = EdgeSnapState::Left;
        m_expandedPos = QPoint(avail.left() + 2, y());
    } else if (rightDist <= 55 || (x() + width()) >= avail.right() - 10) {
        m_snapState = EdgeSnapState::Right;
        m_expandedPos = QPoint(avail.right() - width() - 2, y());
    } else {
        m_snapState = EdgeSnapState::None;
        m_expandedPos = pos();
    }
}

void AskDialog::slideWindow(const QPoint &targetPos, int durationMs)
{
    if (!m_slideAnim) {
        m_slideAnim = new QPropertyAnimation(this, "pos", this);
        m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_slideAnim->stop();
    m_slideAnim->setDuration(durationMs);
    m_slideAnim->setStartValue(pos());
    m_slideAnim->setEndValue(targetPos);
    m_slideAnim->start();
}

void AskDialog::slideToHidden()
{
    if (m_displayMode != DisplayMode::ShortcutBar || m_isDragging) return;
    if (m_isEdgeHidden) return;

    updateEdgeSnapState();
    if (m_snapState == EdgeSnapState::None) {
        m_snapState = EdgeSnapState::Top;
        QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
        if (!screen) screen = QGuiApplication::primaryScreen();
        QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
        m_expandedPos = QPoint(x(), avail.top() + 2);
    }

    QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    QRect avail = screen->availableGeometry();

    const int kPeek = 4; // 只留一条 4px 小线指示条
    QPoint targetPos = pos();

    if (m_snapState == EdgeSnapState::Top) {
        targetPos = QPoint(m_expandedPos.x(), avail.top() - height() + kPeek);
    } else if (m_snapState == EdgeSnapState::Left) {
        targetPos = QPoint(avail.left() - width() + kPeek, m_expandedPos.y());
    } else if (m_snapState == EdgeSnapState::Right) {
        targetPos = QPoint(avail.right() - kPeek, m_expandedPos.y());
    }

    m_isEdgeHidden = true;
    slideWindow(targetPos, 200);
}

void AskDialog::slideToExpanded()
{
    if (!m_isEdgeHidden) return;

    m_isEdgeHidden = false;
    slideWindow(m_expandedPos, 160);
    raise();
    activateWindow();
    Platform::activateApp();
    m_inputEdit->setFocus();
}

void AskDialog::checkAutoHideCondition()
{
    if (m_displayMode != DisplayMode::ShortcutBar || !m_autoHideEnabled || m_snapState == EdgeSnapState::None) return;
    if (m_isDragging || m_isEdgeHidden) return;

    // 若鼠标依然位于展开窗口矩形内，保持展开
    if (frameGeometry().contains(QCursor::pos())) {
        return;
    }

    // 若用户正在输入框中编辑且输入框有未发送文字，暂不收起
    if (m_inputEdit->hasFocus() && !m_inputEdit->text().trimmed().isEmpty()) {
        return;
    }

    slideToHidden();
}

void AskDialog::enterEvent(QEnterEvent *event)
{
    QDialog::enterEvent(event);
    if (m_displayMode == DisplayMode::ShortcutBar && m_autoHideEnabled && m_snapState != EdgeSnapState::None) {
        if (m_autoHideTimer) m_autoHideTimer->stop();
        if (m_isEdgeHidden) {
            slideToExpanded();
        }
    }
}

void AskDialog::leaveEvent(QEvent *event)
{
    QDialog::leaveEvent(event);
    if (m_displayMode == DisplayMode::ShortcutBar && m_autoHideEnabled && m_snapState != EdgeSnapState::None) {
        if (!m_isDragging && !m_isEdgeHidden && m_autoHideTimer) {
            m_autoHideTimer->start(650);
        }
    }
}

void AskDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (!isActiveWindow() && m_displayMode == DisplayMode::ShortcutBar && m_autoHideEnabled && m_snapState != EdgeSnapState::None) {
            if (!m_isDragging && !frameGeometry().contains(QCursor::pos())) {
                slideToHidden();
            }
        }
    }
}

void AskDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!event->modifiers() || event->modifiers() == Qt::KeypadModifier) {
            // 核心保障：回车键仅触发发送行为，绝不穿透给 QDialog 导致误触顶栏其他按钮（如关闭自动贴边）
            if (m_sendBtn) {
                m_sendBtn->click();
            }
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_displayMode == DisplayMode::ShortcutBar && m_snapState != EdgeSnapState::None && m_autoHideEnabled) {
            slideToHidden();
            event->accept();
            return;
        }
        if (onCancel) onCancel();
        hide();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void AskDialog::mousePressEvent(QMouseEvent *event)
{
    if (m_displayMode == DisplayMode::ShortcutBar && event->button() == Qt::LeftButton) {
        if (m_slideAnim) m_slideAnim->stop();
        if (m_autoHideTimer) m_autoHideTimer->stop();
        if (m_isEdgeHidden) {
            m_isEdgeHidden = false;
            move(m_expandedPos);
        }
        m_dragStartPosition = event->globalPosition().toPoint();
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_isDragging = false;
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void AskDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_displayMode == DisplayMode::ShortcutBar && (event->buttons() & Qt::LeftButton)) {
        if (!m_isDragging) {
            if ((event->globalPosition().toPoint() - m_dragStartPosition).manhattanLength() < 5) {
                event->accept();
                return;
            }
            m_isDragging = true;
        }

        QPoint newPos = event->globalPosition().toPoint() - m_dragPosition;

        // 磁吸效果预览：如果接近屏幕顶部菜单栏 (30px 内)，磁吸附到菜单栏边缘
        QScreen *screen = QGuiApplication::screenAt(event->globalPosition().toPoint());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect avail = screen->availableGeometry();
            if (std::abs(newPos.y() - (avail.top() + 2)) < 25) {
                newPos.setY(avail.top() + 2);
            }
        }
        move(newPos);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void AskDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_displayMode == DisplayMode::ShortcutBar) {
        bool wasDragging = m_isDragging;
        m_isDragging = false;
        updateEdgeSnapState();
        move(m_expandedPos);

        SettingsDb::instance()->setInt("ask_dialog.shortcut_x", x());
        SettingsDb::instance()->setInt("ask_dialog.shortcut_y", y());

        if (wasDragging && m_autoHideEnabled && m_snapState != EdgeSnapState::None) {
            if (m_autoHideTimer) {
                m_autoHideTimer->start(1200); // 停顿 1.2 秒给用户确认位置后，自动滑入边缘
            }
        }
        event->accept();
        return;
    }
    QDialog::mouseReleaseEvent(event);
}

void AskDialog::setupQuickPills(QVBoxLayout *mainLayout) {
    m_quickPillsWidget = new QWidget(this);
    auto pillLayout = new QHBoxLayout(m_quickPillsWidget);
    pillLayout->setContentsMargins(0, 0, 0, 0);
    pillLayout->setSpacing(6);

    auto hintLabel = new QLabel("✨ 快捷指令:", m_quickPillsWidget);
    hintLabel->setStyleSheet("font-size: 11px; color: #94a3b8; font-weight: 500;");
    pillLayout->addWidget(hintLabel);

    struct PillInfo {
        QString label;
        QString textToFill;
        QString tooltip;
    };

    QList<PillInfo> pills = {
        {"⚡ dsh 智能体", "让 dsh 分析当前系统存储占用与优化建议", "调度 DeepSeek Harness (dsh) 执行系统磁盘排查、终端命令与深度工程任务"},
        {"🎵 推荐音乐", "根据我当下的心情推荐点音乐放给我听", "让桌宠根据你的喜好画像智能推荐点播音乐"},
        {"📊 内存占用", "当前电脑性能与内存情况如何？", "查询当前系统资源占用与硬件负载"},
        {"🍅 番茄钟", "开启一个 25 分钟番茄专注工作时钟", "快速设定专注倒计时时钟"},
        {"🧹 清理废纸篓", "帮我把电脑废纸篓清空下", "执行垃圾清理动作与动作反馈"}
    };

    for (const auto &p : pills) {
        auto pillBtn = new QPushButton(p.label, m_quickPillsWidget);
        pillBtn->setCursor(Qt::PointingHandCursor);
        pillBtn->setAutoDefault(false);
        pillBtn->setDefault(false);
        pillBtn->setFocusPolicy(Qt::NoFocus);
        pillBtn->setToolTip(p.tooltip);
        pillBtn->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #e2e8f0; border-radius: 12px;"
            "  background: #f8fafc; color: #475569;"
            "  font-size: 11px; padding: 2px 10px;"
            "}"
            "QPushButton:hover {"
            "  background: #e0e7ff; color: #4338ca; border-color: #c7d2fe;"
            "}"
            "QPushButton:pressed {"
            "  background: #c7d2fe; color: #3730a3;"
            "}"
        );
        connect(pillBtn, &QPushButton::clicked, this, [this, p]() {
            m_inputEdit->setText(p.textToFill);
            m_inputEdit->setFocus();
        });
        pillLayout->addWidget(pillBtn);
    }
    pillLayout->addStretch();
    mainLayout->addWidget(m_quickPillsWidget);
}

void AskDialog::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void AskDialog::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    QList<QUrl> urlList = mimeData->urls();
    if (urlList.isEmpty()) return;

    QString localPath = urlList.first().toLocalFile();
    if (localPath.isEmpty()) return;

    m_droppedPath = localPath;
    QFileInfo fi(localPath);
    QString displayName = fi.fileName();

    if (fi.isDir()) {
        promptForContext(QString("📁 已载入文件夹: %1\n(可点击【🖼️ 视觉压缩】进行 Squoosh 对比调节，或输入文字让 AI 处理)").arg(localPath));
    } else {
        promptForContext(QString("🖼️ 已载入图片: %1\n(可点击【🖼️ 视觉压缩】进行 Squoosh 对比调节，或输入文字让 AI 处理)").arg(displayName));
    }
    m_inputEdit->setText("帮我压缩这个图片/目录并优化画质");
    m_inputEdit->selectAll();
    m_inputEdit->setFocus();
    event->acceptProposedAction();
}

void AskDialog::promptForContext(QString const& contextText) {
    m_contextText = contextText.trimmed();
    if (m_contextText.isEmpty()) {
        m_titleLabel->setText(m_displayMode == DisplayMode::ShortcutBar ? "⋮⋮ 💬 快捷提问 (吸附贴边)" : "💬 自由向 AI 提问 (保留历史记忆)");
        m_contextWidget->hide();
    } else {
        m_titleLabel->setText("💬 结合选中文本提问 (AI 问答)");
        QString preview = m_contextText;
        if (preview.length() > 100) {
            preview = preview.left(100) + "...";
        }
        m_previewLabel->setText("📌 参考选中文本: " + preview);
        m_contextWidget->show();
    }
    
    m_inputEdit->clear();
    adjustSize();

    positionWindow();

    refreshModelList();

    show();
    if (m_isEdgeHidden) {
        slideToExpanded();
    } else {
        raise();
        activateWindow();
        Platform::activateApp();
        m_inputEdit->setFocus();
    }
}

void AskDialog::refreshModelList() {
    if (!m_modelCombo) return;
    m_modelCombo->blockSignals(true);
    m_modelCombo->clear();

    const auto &cfg = AgentService::instance()->config();
    int activeIdx = 0;
    for (int i = 0; i < cfg.modelProfiles.size(); ++i) {
        const auto &p = cfg.modelProfiles[i];
        bool isActive = (p.id == cfg.activeProfileId);
        if (isActive) activeIdx = i;
        m_modelCombo->addItem(QString("⚡ %1 (%2)").arg(p.name, p.model), p.id);
    }
    m_modelCombo->setCurrentIndex(activeIdx);
    m_modelCombo->blockSignals(false);
}

void AskDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    refreshModelList();

    auto *dsh = AgentService::instance()->dshAdapter();
    if (dsh && dsh->hasActiveSession()) {
        m_sessionBanner->show();
    } else {
        m_sessionBanner->hide();
    }
}
