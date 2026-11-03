#include "AskDialog.hpp"
#include "AgentService.hpp"
#include "AipyAdapter.hpp"
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
#include <cmath>

AskDialog::AskDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("向桌宠 AI 提问");
    setMinimumWidth(460);
    setAcceptDrops(true);

    bool isShortcut = SettingsDb::instance()->getBool("ask_dialog.shortcut_mode", false);
    m_displayMode = isShortcut ? DisplayMode::ShortcutBar : DisplayMode::Standard;

    auto rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_containerWidget = new QWidget(this);
    m_containerWidget->setObjectName("askContainer");
    rootLayout->addWidget(m_containerWidget);

    auto mainLayout = new QVBoxLayout(m_containerWidget);
    mainLayout->setContentsMargins(16, 14, 16, 14);
    mainLayout->setSpacing(10);

    // 1. 顶部控制栏 (标题 + 模型切换 + 模式切换开关 + 关闭按钮)
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

    m_modeToggleBtn = new QPushButton(this);
    m_modeToggleBtn->setCursor(Qt::PointingHandCursor);
    headerLayout->addWidget(m_modeToggleBtn, 0);

    m_closeTopBtn = new QPushButton("✕", this);
    m_closeTopBtn->setFixedSize(22, 22);
    m_closeTopBtn->setCursor(Qt::PointingHandCursor);
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

    m_resetSessionBtn = new QPushButton("开启新任务", m_sessionBanner);
    m_resetSessionBtn->setStyleSheet("QPushButton { border: 1px solid #86efac; border-radius: 4px; padding: 2px 8px; font-size: 10.5px; background: #dcfce7; color: #15803d; } QPushButton:hover { background: #bbf7d0; }");
    m_resetSessionBtn->setCursor(Qt::PointingHandCursor);
    sessionLayout->addWidget(m_resetSessionBtn, 0);

    connect(m_resetSessionBtn, &QPushButton::clicked, this, [this]() {
        auto *aipy = AgentService::instance()->aipyAdapter();
        if (aipy) aipy->resetSession();
        m_sessionBanner->hide();
        adjustSize();
    });

    m_sessionBanner->hide();
    mainLayout->addWidget(m_sessionBanner);

    // 3. 上下文引用卡片（带 X 清除按钮）
    m_contextWidget = new QWidget(this);
    m_contextWidget->setStyleSheet(
        "QWidget#contextCard {"
        "  background-color: #f5f7fa;"
        "  border: 1px solid #e4e7ed;"
        "  border-radius: 6px;"
        "}"
    );
    m_contextWidget->setObjectName("contextCard");

    auto contextLayout = new QHBoxLayout(m_contextWidget);
    contextLayout->setContentsMargins(8, 6, 8, 6);
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
        m_titleLabel->setText(m_displayMode == DisplayMode::ShortcutBar ? "⋮⋮ 💬 快捷提问 (吸附菜单栏)" : "💬 自由向 AI 提问 (保留历史记忆)");
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
        "  color: #0f172a;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #6366f1;"
        "}"
    );
    inputRowLayout->addWidget(m_inputEdit, 1);

    m_sendBtn = new QPushButton("发送 ↵", this);
    m_sendBtn->setStyleSheet(
        "QPushButton {"
        "  padding: 8px 16px; border-radius: 8px; border: none;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6366f1, stop:1 #8b5cf6);"
        "  color: white; font-weight: bold; font-size: 12px;"
        "}"
        "QPushButton:hover { background: #4f46e5; }"
    );
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    inputRowLayout->addWidget(m_sendBtn, 0);

    mainLayout->addLayout(inputRowLayout);

    // 5. 底部快捷指令胶囊
    setupQuickPills(mainLayout);

    // 6. 独立底部按钮栏 (标准模式下显示取消按钮)
    m_bottomBtnWidget = new QWidget(this);
    auto btmLayout = new QHBoxLayout(m_bottomBtnWidget);
    btmLayout->setContentsMargins(0, 0, 0, 0);
    btmLayout->addStretch();
    m_cancelBtn = new QPushButton("取消", m_bottomBtnWidget);
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
            accept();
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

        m_closeTopBtn->show();
        m_bottomBtnWidget->hide();
        m_titleLabel->setText("⋮⋮ 💬 快捷提问 (吸附菜单栏)");
        m_inputEdit->setPlaceholderText("💬 随时向 AI 提问 (Enter 发送，Esc 隐藏，拖动可吸附至菜单栏)...");
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
        m_modeToggleBtn->setToolTip("切换为快捷吸顶栏模式（可吸附于 macOS 菜单栏或 Windows 顶部边缘）");
        m_modeToggleBtn->setStyleSheet(
            "QPushButton {"
            "  border: 1px solid #c7d2fe; border-radius: 6px; padding: 3px 8px;"
            "  font-size: 11px; background: #e0e7ff; color: #4338ca; font-weight: 600;"
            "}"
            "QPushButton:hover { background: #c7d2fe; color: #3730a3; }"
        );

        m_closeTopBtn->hide();
        m_bottomBtnWidget->show();
        m_titleLabel->setText("💬 向 AI 提问");
        m_inputEdit->setPlaceholderText("💡 试试: 推荐点音乐 / 我的喜好画像 / 探索模式放歌 / 25分钟后提醒喝水...");
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
                return;
            }
        }
        // 默认居中置顶，紧贴 macOS 菜单栏下方 (或 Windows 屏幕顶部)
        int x = avail.left() + (avail.width() - width()) / 2;
        int y = avail.top() + 4;
        move(x, y);
    } else {
        resize(460, minimumSizeHint().height());
        // 标准模式屏幕中央居中
        move(avail.center().x() - width() / 2, avail.center().y() - height() / 2);
    }
}

void AskDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
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
        m_isDragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void AskDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_displayMode == DisplayMode::ShortcutBar && m_isDragging && (event->buttons() & Qt::LeftButton)) {
        QPoint newPos = event->globalPosition().toPoint() - m_dragPosition;

        // 磁吸效果预览：如果接近屏幕顶部菜单栏 (30px 内)，磁吸附到菜单栏边缘
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect avail = screen->availableGeometry();
            if (std::abs(newPos.y() - (avail.top() + 4)) < 25) {
                newPos.setY(avail.top() + 4);
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
    if (m_displayMode == DisplayMode::ShortcutBar && m_isDragging) {
        m_isDragging = false;

        // 释放时磁吸吸附菜单栏
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            QRect avail = screen->availableGeometry();
            if (y() < avail.top() + 35) {
                move(x(), avail.top() + 4);
            }
        }
        // 记忆用户自定义拖放位置
        SettingsDb::instance()->setInt("ask_dialog.shortcut_x", x());
        SettingsDb::instance()->setInt("ask_dialog.shortcut_y", y());
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

    struct QuickItem {
        QString label;
        QString text;
        QString tooltip;
    };
    QList<QuickItem> items = {
        { "🎶 推荐音乐", "推荐点音乐到播放列表", "按当前模式精选 6 首好歌并开播" },
        { "🏷️ 喜好画像", "查看我的喜好标签与画像", "查看我的偏好标签与推荐模式" },
        { "🚀 探索热歌", "用探索模式推荐 6 首全网爆款热歌", "精选全网当前最新最火爆的热点流行歌曲" },
        { "⏰ 定时提醒", "25分钟后提醒我喝水休息", "快速创建桌面定时提醒" }
    };

    for (const auto &it : items) {
        auto btn = new QPushButton(it.label, m_quickPillsWidget);
        btn->setToolTip(it.tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton {"
            "  background: #f1f5f9;"
            "  color: #475569;"
            "  font-size: 11px;"
            "  font-weight: 500;"
            "  border: 1px solid #e2e8f0;"
            "  border-radius: 12px;"
            "  padding: 3px 8px;"
            "}"
            "QPushButton:hover {"
            "  background: #e0e7ff;"
            "  color: #4f46e5;"
            "  border-color: #c7d2fe;"
            "}"
        );
        QString promptText = it.text;
        connect(btn, &QPushButton::clicked, this, [this, promptText]() {
            m_inputEdit->setText(promptText);
            m_inputEdit->setFocus();
            m_inputEdit->selectAll();
        });
        pillLayout->addWidget(btn);
    }

    auto compressBtn = new QPushButton("🖼️ 视觉压缩", m_quickPillsWidget);
    compressBtn->setToolTip("打开 Squoosh 风格的图片/目录无损与视觉压缩工坊");
    compressBtn->setCursor(Qt::PointingHandCursor);
    compressBtn->setStyleSheet(
        "QPushButton {"
        "  background: #f0fdf4;"
        "  color: #15803d;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  border: 1px solid #86efac;"
        "  border-radius: 12px;"
        "  padding: 3px 8px;"
        "}"
        "QPushButton:hover {"
        "  background: #dcfce7;"
        "  color: #166534;"
        "  border-color: #4ade80;"
        "}"
    );
    connect(compressBtn, &QPushButton::clicked, this, [this]() {
        QString url = "http://127.0.0.1:32456/compress";
        if (!m_droppedPath.isEmpty()) {
            url += "?path=" + QUrl::toPercentEncoding(m_droppedPath);
        }
        QDesktopServices::openUrl(QUrl(url));
    });
    pillLayout->addWidget(compressBtn);

    pillLayout->addStretch();
    mainLayout->addWidget(m_quickPillsWidget);
}

void AskDialog::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void AskDialog::dropEvent(QDropEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    QString localPath = urls.first().toLocalFile();
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
        m_titleLabel->setText(m_displayMode == DisplayMode::ShortcutBar ? "⋮⋮ 💬 快捷提问 (吸附菜单栏)" : "💬 自由向 AI 提问 (保留历史记忆)");
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
    raise();
    activateWindow();
    Platform::activateApp();
    m_inputEdit->setFocus();
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

    auto *aipy = AgentService::instance()->aipyAdapter();
    if (aipy && aipy->hasActiveSession()) {
        m_sessionBanner->show();
    } else {
        m_sessionBanner->hide();
    }
}

