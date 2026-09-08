#include "AskDialog.hpp"
#include "AgentService.hpp"
#include "AipyAdapter.hpp"
#include "Platform/Platform.hpp"
#include <QHBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QComboBox>

AskDialog::AskDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("向桌宠 AI 提问");
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    setMinimumWidth(430);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    auto headerLayout = new QHBoxLayout();
    m_titleLabel = new QLabel("💬 向 AI 提问", this);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #333;");
    headerLayout->addWidget(m_titleLabel, 1);

    m_modelCombo = new QComboBox(this);
    m_modelCombo->setStyleSheet("padding: 2px 6px; font-size: 11px; border-radius: 4px; border: 1px solid #cbd5e1; background: #f8fafc; color: #475569; font-weight: 500;");
    m_modelCombo->setToolTip("切换当前提问所使用的基础大模型");
    headerLayout->addWidget(m_modelCombo, 0);

    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) return;
        QString pId = m_modelCombo->itemData(index).toString();
        if (!pId.isEmpty()) {
            AgentService::instance()->setActiveProfile(pId);
        }
    });

    mainLayout->addLayout(headerLayout);

    // 任务会话延续提示与新建任务按钮
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

    // 上下文引用卡片（带 X 清除按钮）
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
        m_contextWidget->hide();
        m_titleLabel->setText("💬 自由向 AI 提问 (保留历史记忆)");
        adjustSize();
        m_inputEdit->setFocus();
    });

    m_inputEdit = new QLineEdit(this);
    m_inputEdit->setPlaceholderText("💡 试试: 推荐点音乐 / 我的喜好画像 / 探索模式放歌 / 25分钟后提醒喝水...");
    m_inputEdit->setStyleSheet(
        "QLineEdit {"
        "  border: 1.5px solid #cbd5e1;"
        "  border-radius: 8px;"
        "  padding: 9px 12px;"
        "  font-size: 12.5px;"
        "  background: #ffffff;"
        "  color: #0f172a;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #6366f1;"
        "}"
    );
    mainLayout->addWidget(m_inputEdit);

    setupQuickPills(mainLayout);

    auto btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_cancelBtn = new QPushButton("取消", this);
    m_sendBtn = new QPushButton("发送 ↵", this);

    m_cancelBtn->setStyleSheet("padding: 6px 14px; border-radius: 6px; border: 1px solid #dcdfe6; background: #fff;");
    m_sendBtn->setStyleSheet("padding: 6px 16px; border-radius: 6px; border: none; background: #6366f1; color: white; font-weight: bold;");

    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_sendBtn->setCursor(Qt::PointingHandCursor);

    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_sendBtn);
    mainLayout->addLayout(btnLayout);

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
    pillLayout->addStretch();
    mainLayout->addWidget(m_quickPillsWidget);
}

void AskDialog::promptForContext(QString const& contextText) {
    m_contextText = contextText.trimmed();
    if (m_contextText.isEmpty()) {
        m_titleLabel->setText("💬 自由向 AI 提问 (保留历史记忆)");
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

    // 居中显示
    if (auto screen = QGuiApplication::primaryScreen()) {
        auto geom = screen->geometry();
        move(geom.center().x() - width() / 2, geom.center().y() - height() / 2);
    }

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

