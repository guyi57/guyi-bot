// 
// Shijima-Qt - AI Model, Memory, Agent & Hotkey Settings Dialog Implementation
// 

#include "AgentSettingsDialog.hpp"
#include "AgentService.hpp"
#include "AipyAdapter.hpp"
#include "PersonaManager.hpp"
#include "SkillManager.hpp"
#include "McpManager.hpp"
#include "SettingsDb.hpp"
#include "ShijimaManager.hpp"
#include "UpdateManager.hpp"
#include "UpdateDialog.hpp"
#include "LongTermMemoryEngine.hpp"
#include "SensorManager.hpp"
#include <QInputDialog>
#include <QFormLayout>
#include <QMessageBox>
#include <QScreen>
#include <QGroupBox>
#include <QGuiApplication>
#include <QScrollArea>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>

AgentSettingsDialog::AgentSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("⚙️ 智能助理、角色人格与编程感知配置");
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint);
    setMinimumWidth(620);
    setMinimumHeight(560);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    auto tabWidget = new QTabWidget(this);
    tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #dcdfe6; border-radius: 8px; background: #ffffff; padding: 8px; } "
        "QTabBar::tab { background: #f4f4f5; border: 1px solid #dcdfe6; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; padding: 7px 14px; font-weight: 500; color: #606266; margin-right: 2px; } "
        "QTabBar::tab:selected { background: #ffffff; color: #409eff; font-weight: bold; border-color: #dcdfe6; } "
        "QTabBar::tab:hover { background: #ecf5ff; color: #409eff; }"
    );

    // =========================================================================
    // 🎭 TAB 1: 角色人格设定 (Persona Adapter)
    // =========================================================================
    auto personaPage = new QWidget(tabWidget);
    auto personaLayout = new QVBoxLayout(personaPage);
    personaLayout->setSpacing(12);

    auto personaGroup = new QGroupBox("🎭 桌宠人格适配器", personaPage);
    personaGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto personaForm = new QFormLayout(personaGroup);
    personaForm->setSpacing(10);
    personaForm->setLabelAlignment(Qt::AlignRight);

    m_personaCombo = new QComboBox(personaGroup);
    for (const auto &p : PersonaManager::instance()->allPersonas()) {
        m_personaCombo->addItem(p.name, p.id);
    }
    m_personaCombo->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-size: 13px;");
    personaForm->addRow("当前人格角色:", m_personaCombo);

    m_personaDescLabel = new QLabel(personaGroup);
    m_personaDescLabel->setStyleSheet("color: #606266; font-size: 12px; background: #fdf6ec; padding: 8px; border-radius: 6px; border: 1px solid #faecd8;");
    m_personaDescLabel->setWordWrap(true);
    personaForm->addRow("性格特征:", m_personaDescLabel);

    m_customPromptEdit = new QTextEdit(personaGroup);
    m_customPromptEdit->setPlaceholderText("在此输入自定义 System Prompt 人设，定义你的桌宠性格、说话口吻与偏好...");
    m_customPromptEdit->setStyleSheet("border: 1px solid #dcdfe6; border-radius: 6px; padding: 6px; font-family: monospace; font-size: 12px;");
    m_customPromptEdit->setMinimumHeight(90);
    personaForm->addRow("人设提示词:", m_customPromptEdit);

    auto personaTip = new QLabel("✨ 提示：模型回复开头附带 [action:jump]、[action:celebrate]、[action:sit] 等指令时，桌宠会自动执行对应动作动画！", personaGroup);
    personaTip->setStyleSheet("font-size: 11px; color: #909399;");
    personaTip->setWordWrap(true);
    personaLayout->addWidget(personaGroup);

    // 自主搭讪与桌面关怀
    auto banterGroup = new QGroupBox("💬 灵动拟人化与桌面关怀 (Contextual Banter)", personaPage);
    banterGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto banterLayout = new QVBoxLayout(banterGroup);
    banterLayout->setSpacing(8);

    auto freqRow = new QHBoxLayout();
    auto freqLabel = new QLabel("自主搭讪频率:", banterGroup);
    freqLabel->setStyleSheet("font-size: 13px; color: #303133;");
    m_banterFreqCombo = new QComboBox(banterGroup);
    m_banterFreqCombo->addItem("关闭 (不主动搭讪)", 0);
    m_banterFreqCombo->addItem("偶尔搭讪 (约 30 分钟一次)", 1);
    m_banterFreqCombo->addItem("适度陪伴 (约 15 分钟一次，推荐)", 2);
    m_banterFreqCombo->addItem("活跃互动 (约 8 分钟一次)", 3);
    m_banterFreqCombo->setStyleSheet("padding: 4px 8px; font-size: 13px;");
    freqRow->addWidget(freqLabel);
    freqRow->addWidget(m_banterFreqCombo, 1);
    banterLayout->addLayout(freqRow);

    m_contextualCareCheck = new QCheckBox("启用前台应用感知与健康关怀 (写代码>45m揉肩/喝水提醒、深夜作息关怀、摸鱼抓包)", banterGroup);
    m_contextualCareCheck->setStyleSheet("font-size: 12px; color: #475569;");
    banterLayout->addWidget(m_contextualCareCheck);

    personaLayout->addWidget(banterGroup);

    personaLayout->addStretch();
    tabWidget->addTab(personaPage, "🎭 角色人格");

    // =========================================================================
    // ⚡ TAB 2: 大模型与 Agent 适配器 (LLM & Agent)
    // =========================================================================
    auto llmPage = new QWidget(tabWidget);
    auto llmScroll = new QScrollArea(llmPage);
    llmScroll->setWidgetResizable(true);
    llmScroll->setFrameShape(QFrame::NoFrame);

    auto llmContainer = new QWidget();
    auto llmLayout = new QVBoxLayout(llmContainer);
    llmLayout->setSpacing(12);

    // 基础直连 LLM
    auto llmGroup = new QGroupBox("⚡ 基础大模型配置池 (支持多服务商预设、随心切换与自动故障转移)", llmContainer);
    llmGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto llmForm = new QFormLayout(llmGroup);
    llmForm->setSpacing(8);
    llmForm->setLabelAlignment(Qt::AlignRight);

    // 配置选择与增删操作条
    auto profileBarLayout = new QHBoxLayout();
    m_profileCombo = new QComboBox(llmGroup);
    m_profileCombo->setStyleSheet("padding: 4px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    
    m_setActiveProfileBtn = new QPushButton("⭐ 设为活动模型", llmGroup);
    m_setActiveProfileBtn->setStyleSheet("padding: 4px 8px; border-radius: 4px; background: #ecf5ff; color: #409eff; border: 1px solid #b3d8ff; font-weight: bold; font-size: 11px;");
    m_setActiveProfileBtn->setCursor(Qt::PointingHandCursor);

    m_addProfileBtn = new QPushButton("➕ 新建", llmGroup);
    m_addProfileBtn->setStyleSheet("padding: 4px 8px; border-radius: 4px; background: #e1f3d8; color: #67c23a; border: 1px solid #c2e7b0; font-size: 11px;");
    m_addProfileBtn->setCursor(Qt::PointingHandCursor);
    
    m_delProfileBtn = new QPushButton("🗑️ 删除", llmGroup);
    m_delProfileBtn->setStyleSheet("padding: 4px 8px; border-radius: 4px; background: #fef0f0; color: #f56c6c; border: 1px solid #fbc4c4; font-size: 11px;");
    m_delProfileBtn->setCursor(Qt::PointingHandCursor);

    profileBarLayout->addWidget(m_profileCombo, 1);
    profileBarLayout->addWidget(m_setActiveProfileBtn);
    profileBarLayout->addWidget(m_addProfileBtn);
    profileBarLayout->addWidget(m_delProfileBtn);
    llmForm->addRow("选择模型配置:", profileBarLayout);

    auto switchesLayout = new QHBoxLayout();
    m_profileEnabledCheck = new QCheckBox("启用此配置参与自动故障转移备用池", llmGroup);
    m_autoFailoverCheck = new QCheckBox("🔄 接口异常时自动无缝切换备用模型重试", llmGroup);
    m_autoFailoverCheck->setStyleSheet("font-weight: bold; color: #409eff;");
    switchesLayout->addWidget(m_profileEnabledCheck);
    switchesLayout->addWidget(m_autoFailoverCheck);
    llmForm->addRow("故障转移策略:", switchesLayout);

    m_profileNameEdit = new QLineEdit(llmGroup);
    m_profileNameEdit->setPlaceholderText("例如: DeepSeek (官方 API)");
    m_profileNameEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6;");
    llmForm->addRow("配置显示名称:", m_profileNameEdit);

    m_presetCombo = new QComboBox(llmGroup);
    m_presetCombo->addItem("-- 选择预设模板快速填充 --", 0);
    m_presetCombo->addItem("DeepSeek (官方 API: deepseek-chat)", 1);
    m_presetCombo->addItem("硅基流动 (SiliconFlow: DeepSeek-V3)", 2);
    m_presetCombo->addItem("通义千问 (阿里云百炼: qwen-plus)", 3);
    m_presetCombo->addItem("智谱清言 (GLM 官方: glm-4-flash)", 4);
    m_presetCombo->addItem("Moonshot AI (Kimi 官方: moonshot-v1-8k)", 5);
    m_presetCombo->addItem("OpenAI (官方 API: gpt-4o-mini)", 6);
    m_presetCombo->addItem("本地 Ollama (127.0.0.1:11434 离线免费)", 7);
    m_presetCombo->setStyleSheet("padding: 4px; border-radius: 4px; border: 1px solid #dcdfe6; color: #606266;");
    llmForm->addRow("快速填入模板:", m_presetCombo);

    m_apiBaseEdit = new QLineEdit(llmGroup);
    m_apiBaseEdit->setPlaceholderText("例如: https://api.deepseek.com/v1");
    m_apiBaseEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6;");
    llmForm->addRow("API 地址 (Base):", m_apiBaseEdit);

    m_apiKeyEdit = new QLineEdit(llmGroup);
    m_apiKeyEdit->setPlaceholderText("请输入 API Key (sk-...)");
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6;");
    llmForm->addRow("API 密钥 (Key):", m_apiKeyEdit);

    m_modelEdit = new QLineEdit(llmGroup);
    m_modelEdit->setPlaceholderText("例如: deepseek-chat, gpt-4o-mini, qwen2.5:7b");
    m_modelEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6;");
    llmForm->addRow("模型名称 (Model):", m_modelEdit);

    m_memoryTurnsSpin = new QSpinBox(llmGroup);
    m_memoryTurnsSpin->setRange(0, 30);
    m_memoryTurnsSpin->setValue(6);
    m_memoryTurnsSpin->setSuffix(" 轮对话");
    m_memoryTurnsSpin->setStyleSheet("padding: 4px; border-radius: 4px; border: 1px solid #dcdfe6;");
    llmForm->addRow("记忆轮数:", m_memoryTurnsSpin);

    auto testLlmLayout = new QHBoxLayout();
    m_testBtn = new QPushButton("🔌 测试模型连通性", llmGroup);
    m_testBtn->setStyleSheet("padding: 5px 12px; border-radius: 4px; border: 1px solid #409eff; color: #409eff; background: #ecf5ff; font-weight: 500;");
    m_testBtn->setCursor(Qt::PointingHandCursor);

    m_testStatusLabel = new QLabel(llmGroup);
    m_testStatusLabel->setStyleSheet("font-size: 11px; color: #666;");
    m_testStatusLabel->setWordWrap(true);

    testLlmLayout->addWidget(m_testBtn);
    testLlmLayout->addWidget(m_testStatusLabel, 1);
    llmForm->addRow("", testLlmLayout);

    llmLayout->addWidget(llmGroup);

    // aipy-pro Agent 适配器
    auto agentGroup = new QGroupBox("🤖 aipy-pro 复杂任务智能体", llmContainer);
    agentGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto agentForm = new QFormLayout(agentGroup);
    agentForm->setSpacing(8);
    agentForm->setLabelAlignment(Qt::AlignRight);

    m_agentTypeCombo = new QComboBox(agentGroup);
    m_agentTypeCombo->addItem("aipy-pro (本地 Agent 项目)", "aipy");
    m_agentTypeCombo->addItem("WorkBuddy (云端智能体)", "workbuddy");
    m_agentTypeCombo->addItem("CodeX (代码智能体)", "codex");
    m_agentTypeCombo->addItem("直连 LLM (无 Agent 模式)", "direct_llm");
    m_agentTypeCombo->setStyleSheet("padding: 4px; border-radius: 4px; border: 1px solid #dcdfe6;");
    agentForm->addRow("Agent 适配器:", m_agentTypeCombo);

    m_routingModeCombo = new QComboBox(agentGroup);
    m_routingModeCombo->addItem("🎯 智能分流 (简单问题直答，复杂任务由 Agent 执行)", "AUTO");
    m_routingModeCombo->addItem("🚀 始终使用 Agent 执行所有问题", "ALWAYS_AGENT");
    m_routingModeCombo->addItem("💬 仅使用轻量模型直答 (不委派 Agent)", "ALWAYS_LLM");
    m_routingModeCombo->setStyleSheet("padding: 4px; border-radius: 4px; border: 1px solid #dcdfe6;");
    agentForm->addRow("分流策略:", m_routingModeCombo);

    m_aipyBaseEdit = new QLineEdit(agentGroup);
    m_aipyBaseEdit->setPlaceholderText("默认: http://127.0.0.1:41970");
    m_aipyBaseEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6;");
    agentForm->addRow("aipy 地址:", m_aipyBaseEdit);

    m_aipyKeyEdit = new QLineEdit(agentGroup);
    m_aipyKeyEdit->setPlaceholderText("可留空自动读取，或手动输入 API 密钥");
    m_aipyKeyEdit->setEchoMode(QLineEdit::Password);
    m_aipyKeyEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6;");

    auto keyBtnLayout = new QHBoxLayout();
    m_autoDetectKeyBtn = new QPushButton("🔑 自动提取本地密钥", agentGroup);
    m_autoDetectKeyBtn->setStyleSheet("padding: 4px 8px; border-radius: 4px; background: #e1f3d8; color: #67c23a; border: 1px solid #c2e7b0; font-weight: 500; font-size: 11px;");
    m_autoDetectKeyBtn->setCursor(Qt::PointingHandCursor);

    m_testAipyBtn = new QPushButton("🔌 测试连通", agentGroup);
    m_testAipyBtn->setStyleSheet("padding: 4px 8px; border-radius: 4px; background: #ecf5ff; color: #409eff; border: 1px solid #d9ecff; font-weight: 500; font-size: 11px;");
    m_testAipyBtn->setCursor(Qt::PointingHandCursor);

    keyBtnLayout->addWidget(m_aipyKeyEdit);
    keyBtnLayout->addWidget(m_autoDetectKeyBtn);
    keyBtnLayout->addWidget(m_testAipyBtn);
    agentForm->addRow("aipy 密钥:", keyBtnLayout);

    m_agentStatusLabel = new QLabel(agentGroup);
    m_agentStatusLabel->setStyleSheet("font-size: 11px; color: #666;");
    m_agentStatusLabel->setWordWrap(true);
    agentForm->addRow("", m_agentStatusLabel);

    llmLayout->addWidget(agentGroup);

    llmScroll->setWidget(llmContainer);
    auto llmMainBox = new QVBoxLayout(llmPage);
    llmMainBox->setContentsMargins(0, 0, 0, 0);
    llmMainBox->addWidget(llmScroll);
    tabWidget->addTab(llmPage, "⚡ 模型与 Agent");

    // =========================================================================
    // 🤖 TAB 3: 编程助手状态感知 (Coding Agent Hook & Token Saver)
    // =========================================================================
    auto hookPage = new QWidget(tabWidget);
    auto hookLayout = new QVBoxLayout(hookPage);
    hookLayout->setSpacing(12);

    auto hookGroup = new QGroupBox("🤖 Coding Agent 状态感知与动作联动", hookPage);
    hookGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto hookForm = new QFormLayout(hookGroup);
    hookForm->setSpacing(10);
    hookForm->setLabelAlignment(Qt::AlignRight);

    m_enableStateHookCheck = new QCheckBox("启用 Coding Agent 状态感知 (监听 Cursor / Claude Code / Git Hook 状态)", hookGroup);
    m_enableStateHookCheck->setStyleSheet("font-weight: 500; font-size: 13px; color: #303133;");
    hookForm->addRow("", m_enableStateHookCheck);

    m_enableLlmNarrationCheck = new QCheckBox("启用 AI 智能口语化润色 (⚠️ 开启后完成任务时将调用大模型消耗 Token；关闭则使用 0-Token 纯本地极速播报)", hookGroup);
    m_enableLlmNarrationCheck->setStyleSheet("font-size: 12px; color: #606266;");
    hookForm->addRow("", m_enableLlmNarrationCheck);

    m_stateDebounceSpin = new QSpinBox(hookGroup);
    m_stateDebounceSpin->setRange(0, 30);
    m_stateDebounceSpin->setValue(2);
    m_stateDebounceSpin->setSuffix(" 秒");
    m_stateDebounceSpin->setStyleSheet("padding: 4px; border-radius: 4px; border: 1px solid #dcdfe6;");
    hookForm->addRow("状态防抖间隔:", m_stateDebounceSpin);

    auto apiDocLabel = new QLabel(
        "💡 <b>Webhook API 接入指南</b>：<br>"
        "通过向 <code>http://127.0.0.1:32456/api/agent/status</code> 发送 POST 请求即可驱动桌宠联动：<br>"
        "<pre style='background: #f4f4f5; padding: 6px; border-radius: 4px; font-size: 11px; margin-top: 4px;'>"
        "curl -X POST http://127.0.0.1:32456/api/agent/status \\\n"
        "  -H 'Content-Type: application/json' \\\n"
        "  -d '{\"agent_name\": \"Claude Code\", \"status\": \"working\", \"task\": \"重构数据库模块\"}'"
        "</pre>"
        "支持状态: <code>thinking</code> (思考), <code>working</code> (写代码), <code>need_approval</code> (求审批), <code>finished</code> (完成庆祝), <code>error</code> (报错)",
        hookGroup
    );
    apiDocLabel->setStyleSheet("color: #606266; font-size: 11px; line-height: 1.4;");
    apiDocLabel->setWordWrap(true);
    hookForm->addRow("", apiDocLabel);

    hookLayout->addWidget(hookGroup);
    hookLayout->addStretch();
    tabWidget->addTab(hookPage, "🤖 编程感知 (Hook)");

    // =========================================================================
    // ⌨️ TAB 4: 全局快捷键与记忆 (Hotkeys & Memory)
    // =========================================================================
    auto hkPage = new QWidget(tabWidget);
    auto hkLayout = new QVBoxLayout(hkPage);
    hkLayout->setSpacing(12);

    auto hkGroup = new QGroupBox("⌨️ 全局快捷键", hkPage);
    hkGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto hkForm = new QFormLayout(hkGroup);
    hkForm->setSpacing(8);
    hkForm->setLabelAlignment(Qt::AlignRight);

    m_hotkeyTranslateEdit = new QLineEdit(hkGroup);
    m_hotkeyTranslateEdit->setPlaceholderText("例如: Option+T");
    m_hotkeyTranslateEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    hkForm->addRow("划词翻译快捷键:", m_hotkeyTranslateEdit);

    m_hotkeyAskEdit = new QLineEdit(hkGroup);
    m_hotkeyAskEdit->setPlaceholderText("例如: Option+Q");
    m_hotkeyAskEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    hkForm->addRow("划词提问快捷键:", m_hotkeyAskEdit);

    m_hotkeyHistoryEdit = new QLineEdit(hkGroup);
    m_hotkeyHistoryEdit->setPlaceholderText("例如: Option+H 或 Alt+H");
    m_hotkeyHistoryEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    hkForm->addRow("📜 历史任务快捷键:", m_hotkeyHistoryEdit);

    m_hotkeyMusicToggleEdit = new QLineEdit(hkGroup);
    m_hotkeyMusicToggleEdit->setPlaceholderText("例如: Option+M");
    m_hotkeyMusicToggleEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    hkForm->addRow("🎵 音乐打开/隐藏:", m_hotkeyMusicToggleEdit);

    m_hotkeyMusicPlayPauseEdit = new QLineEdit(hkGroup);
    m_hotkeyMusicPlayPauseEdit->setPlaceholderText("例如: Option+Space");
    m_hotkeyMusicPlayPauseEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    hkForm->addRow("🎵 音乐播放/暂停:", m_hotkeyMusicPlayPauseEdit);

    m_hotkeyMusicNextEdit = new QLineEdit(hkGroup);
    m_hotkeyMusicNextEdit->setPlaceholderText("例如: Option+Right");
    m_hotkeyMusicNextEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    hkForm->addRow("🎵 音乐下一首:", m_hotkeyMusicNextEdit);

    m_hotkeyMusicPrevEdit = new QLineEdit(hkGroup);
    m_hotkeyMusicPrevEdit->setPlaceholderText("例如: Option+Left");
    m_hotkeyMusicPrevEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    hkForm->addRow("🎵 音乐上一首:", m_hotkeyMusicPrevEdit);

    m_hotkeyMusicFavEdit = new QLineEdit(hkGroup);
    m_hotkeyMusicFavEdit->setPlaceholderText("例如: Option+L");
    m_hotkeyMusicFavEdit->setStyleSheet("padding: 5px; border-radius: 4px; border: 1px solid #dcdfe6; font-weight: bold;");
    hkForm->addRow("🎵 音乐收藏/取消:", m_hotkeyMusicFavEdit);

    m_clearMemoryBtn = new QPushButton("🧹 清空历史对话记忆", hkGroup);
    m_clearMemoryBtn->setStyleSheet("padding: 5px 12px; color: #e6a23c; border: 1px solid #f3d19e; border-radius: 4px; background: #fdf6ec; font-weight: 500;");
    m_clearMemoryBtn->setCursor(Qt::PointingHandCursor);
    hkForm->addRow("", m_clearMemoryBtn);

    hkLayout->addWidget(hkGroup);
    hkLayout->addStretch();
    tabWidget->addTab(hkPage, "⌨️ 快捷键");

    // =========================================================================
    // 🛠️ TAB 5: 技能库 (Skills)
    // =========================================================================
    auto skillsPage = new QWidget(tabWidget);
    auto skillsLayout = new QVBoxLayout(skillsPage);
    skillsLayout->setSpacing(10);

    auto skillsTopLayout = new QHBoxLayout();
    auto skillsTitle = new QLabel("本地已加载技能模块（可勾选启用或停用）:", skillsPage);
    skillsTitle->setStyleSheet("font-weight: bold; color: #2c3e50; font-size: 13px;");
    skillsTopLayout->addWidget(skillsTitle);
    skillsTopLayout->addStretch();

    m_openSkillsDirBtn = new QPushButton("📂 打开技能目录", skillsPage);
    m_openSkillsDirBtn->setStyleSheet("padding: 4px 10px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_openSkillsDirBtn->setCursor(Qt::PointingHandCursor);
    skillsTopLayout->addWidget(m_openSkillsDirBtn);

    m_refreshSkillsBtn = new QPushButton("🔄 刷新", skillsPage);
    m_refreshSkillsBtn->setStyleSheet("padding: 4px 10px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_refreshSkillsBtn->setCursor(Qt::PointingHandCursor);
    skillsTopLayout->addWidget(m_refreshSkillsBtn);
    skillsLayout->addLayout(skillsTopLayout);

    m_skillsListWidget = new QListWidget(skillsPage);
    m_skillsListWidget->setStyleSheet("border: 1px solid #dcdfe6; border-radius: 6px; padding: 4px; font-size: 13px;");
    skillsLayout->addWidget(m_skillsListWidget, 1);

    m_skillDetailLabel = new QLabel(skillsPage);
    m_skillDetailLabel->setStyleSheet("color: #606266; font-size: 12px; background: #f8fafc; padding: 8px; border-radius: 6px; border: 1px solid #e2e8f0;");
    m_skillDetailLabel->setWordWrap(true);
    m_skillDetailLabel->setText("点击上方技能项可查看技能详情与提示词。");
    skillsLayout->addWidget(m_skillDetailLabel);

    tabWidget->addTab(skillsPage, "🛠️ 技能库");

    // =========================================================================
    // 🔌 TAB 6: MCP 服务 (Model Context Protocol)
    // =========================================================================
    auto mcpPage = new QWidget(tabWidget);
    auto mcpLayout = new QVBoxLayout(mcpPage);
    mcpLayout->setSpacing(10);

    auto mcpTopLayout = new QHBoxLayout();
    auto mcpTitle = new QLabel("MCP (Model Context Protocol) 外部服务连接状态:", mcpPage);
    mcpTitle->setStyleSheet("font-weight: bold; color: #2c3e50; font-size: 13px;");
    mcpTopLayout->addWidget(mcpTitle);
    mcpTopLayout->addStretch();

    m_openMcpConfigBtn = new QPushButton("📝 编辑 mcp_servers.json", mcpPage);
    m_openMcpConfigBtn->setStyleSheet("padding: 4px 10px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_openMcpConfigBtn->setCursor(Qt::PointingHandCursor);
    mcpTopLayout->addWidget(m_openMcpConfigBtn);

    m_reloadMcpBtn = new QPushButton("🔄 重启并重载服务", mcpPage);
    m_reloadMcpBtn->setStyleSheet("padding: 4px 10px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_reloadMcpBtn->setCursor(Qt::PointingHandCursor);
    mcpTopLayout->addWidget(m_reloadMcpBtn);
    mcpLayout->addLayout(mcpTopLayout);

    m_mcpListWidget = new QListWidget(mcpPage);
    m_mcpListWidget->setStyleSheet("border: 1px solid #dcdfe6; border-radius: 6px; padding: 4px; font-size: 13px;");
    mcpLayout->addWidget(m_mcpListWidget, 1);

    m_mcpDetailLabel = new QLabel(mcpPage);
    m_mcpDetailLabel->setStyleSheet("color: #606266; font-size: 12px; background: #f8fafc; padding: 8px; border-radius: 6px; border: 1px solid #e2e8f0;");
    m_mcpDetailLabel->setWordWrap(true);
    m_mcpDetailLabel->setText("点击上方服务项可查看该服务当前向大模型暴露的 Tool 列表。");
    mcpLayout->addWidget(m_mcpDetailLabel);

    tabWidget->addTab(mcpPage, "🔌 MCP 服务");

    // =========================================================================
    // 🧠 TAB: 长期记忆与画像 (Long-Term Memory & Profile)
    // =========================================================================
    auto memoryPage = new QWidget(tabWidget);
    auto memoryLayout = new QVBoxLayout(memoryPage);
    memoryLayout->setSpacing(10);

    // 1. 主人全局核心画像 (Core Profile)
    auto profileGroup = new QGroupBox("👤 主人全局核心画像 (L1 Core Profile)", memoryPage);
    profileGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto profileForm = new QFormLayout(profileGroup);
    profileForm->setSpacing(6);
    profileForm->setLabelAlignment(Qt::AlignRight);

    m_ownerNameEdit = new QLineEdit(profileGroup);
    m_ownerNameEdit->setPlaceholderText("例如: 古逸 / 主人");
    profileForm->addRow("称呼/姓名:", m_ownerNameEdit);

    m_ownerOccEdit = new QLineEdit(profileGroup);
    m_ownerOccEdit->setPlaceholderText("例如: 资深桌面开发工程师 / 架构师");
    profileForm->addRow("职业身份:", m_ownerOccEdit);

    m_ownerTechEdit = new QLineEdit(profileGroup);
    m_ownerTechEdit->setPlaceholderText("例如: C++, Qt, Python, Rust, Linux, macOS");
    profileForm->addRow("常用技术栈:", m_ownerTechEdit);

    m_ownerMusicEdit = new QLineEdit(profileGroup);
    m_ownerMusicEdit->setPlaceholderText("例如: 华语流行, 摇滚, 轻音乐, 周杰伦");
    profileForm->addRow("音乐偏好:", m_ownerMusicEdit);

    m_ownerHabitEdit = new QLineEdit(profileGroup);
    m_ownerHabitEdit->setPlaceholderText("例如: 经常高强度专注, 偶尔凌晨写代码");
    profileForm->addRow("作息与习惯:", m_ownerHabitEdit);

    m_ownerNotesEdit = new QTextEdit(profileGroup);
    m_ownerNotesEdit->setPlaceholderText("例如: 正在开发 xuanfu 桌宠项目，喜好极简高效的代码风格...");
    m_ownerNotesEdit->setFixedHeight(45);
    profileForm->addRow("附加长期备忘:", m_ownerNotesEdit);

    auto profileBtnLayout = new QHBoxLayout();
    profileBtnLayout->addStretch();
    m_saveOwnerProfileBtn = new QPushButton("💾 保存主人核心档案", profileGroup);
    m_saveOwnerProfileBtn->setStyleSheet("padding: 4px 14px; border-radius: 4px; border: none; background: #67c23a; color: white; font-weight: bold; font-size: 12px;");
    m_saveOwnerProfileBtn->setCursor(Qt::PointingHandCursor);
    profileBtnLayout->addWidget(m_saveOwnerProfileBtn);
    profileForm->addRow("", profileBtnLayout);

    memoryLayout->addWidget(profileGroup);

    // 2. 长程语义事实库 (Semantic Memories)
    auto memGroup = new QGroupBox("🧠 语义事实记忆库 (L3 Semantic Memories)", memoryPage);
    memGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto memGroupLayout = new QVBoxLayout(memGroup);
    memGroupLayout->setSpacing(6);

    auto searchRow = new QHBoxLayout();
    m_memSearchEdit = new QLineEdit(memGroup);
    m_memSearchEdit->setPlaceholderText("🔍 输入关键词或语义描述进行检索...");
    m_memSearchBtn = new QPushButton("搜索", memGroup);
    m_memSearchBtn->setStyleSheet("padding: 4px 12px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_memSearchBtn->setCursor(Qt::PointingHandCursor);

    m_memRefreshBtn = new QPushButton("刷新", memGroup);
    m_memRefreshBtn->setStyleSheet("padding: 4px 12px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_memRefreshBtn->setCursor(Qt::PointingHandCursor);

    searchRow->addWidget(m_memSearchEdit);
    searchRow->addWidget(m_memSearchBtn);
    searchRow->addWidget(m_memRefreshBtn);
    memGroupLayout->addLayout(searchRow);

    m_memListWidget = new QListWidget(memGroup);
    m_memListWidget->setStyleSheet("border: 1px solid #e4e7ed; border-radius: 6px; background: #ffffff; padding: 4px; font-size: 12px;");
    m_memListWidget->setFixedHeight(120);
    memGroupLayout->addWidget(m_memListWidget);

    auto memActionsRow = new QHBoxLayout();
    m_memStatsLabel = new QLabel("正在读取记忆统计...", memGroup);
    m_memStatsLabel->setStyleSheet("color: #909399; font-size: 11px;");
    memActionsRow->addWidget(m_memStatsLabel);
    memActionsRow->addStretch();

    m_memAddBtn = new QPushButton("➕ 记一条", memGroup);
    m_memAddBtn->setStyleSheet("padding: 3px 9px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_memAddBtn->setCursor(Qt::PointingHandCursor);

    m_memDelBtn = new QPushButton("🗑️ 删除选中", memGroup);
    m_memDelBtn->setStyleSheet("padding: 3px 9px; border-radius: 4px; border: 1px solid #f56c6c; background: #fef0f0; color: #f56c6c; font-size: 12px;");
    m_memDelBtn->setCursor(Qt::PointingHandCursor);

    m_memClearAllBtn = new QPushButton("⚠️ 清空全部", memGroup);
    m_memClearAllBtn->setStyleSheet("padding: 3px 9px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; color: #909399; font-size: 12px;");
    m_memClearAllBtn->setCursor(Qt::PointingHandCursor);

    memActionsRow->addWidget(m_memAddBtn);
    memActionsRow->addWidget(m_memDelBtn);
    memActionsRow->addWidget(m_memClearAllBtn);
    memGroupLayout->addLayout(memActionsRow);

    memoryLayout->addWidget(memGroup);

    // 3. 实时应用感知与自进化探针足迹 (Live App Sensing & Probes)
    auto sensorGroup = new QGroupBox("📡 实时应用感知与自进化探针 (App Sensing & Probes)", memoryPage);
    sensorGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto sensorLayout = new QVBoxLayout(sensorGroup);
    sensorLayout->setSpacing(6);

    m_sensorCurrentStatusLabel = new QLabel(sensorGroup);
    m_sensorCurrentStatusLabel->setStyleSheet("font-size: 12px; color: #409eff; background: #ecf5ff; padding: 6px 10px; border-radius: 6px; border: 1px solid #d9ecff;");
    m_sensorCurrentStatusLabel->setWordWrap(true);
    sensorLayout->addWidget(m_sensorCurrentStatusLabel);

    m_sensorActivityList = new QListWidget(sensorGroup);
    m_sensorActivityList->setStyleSheet("border: 1px solid #e4e7ed; border-radius: 6px; background: #ffffff; padding: 4px; font-size: 12px;");
    m_sensorActivityList->setFixedHeight(110);
    sensorLayout->addWidget(m_sensorActivityList);

    auto sensorActionRow = new QHBoxLayout();
    m_sensorStatsLabel = new QLabel(sensorGroup);
    m_sensorStatsLabel->setStyleSheet("color: #909399; font-size: 11px;");
    sensorActionRow->addWidget(m_sensorStatsLabel);
    sensorActionRow->addStretch();

    m_sensorRefreshBtn = new QPushButton("🔄 刷新感知", sensorGroup);
    m_sensorRefreshBtn->setStyleSheet("padding: 3px 9px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_sensorRefreshBtn->setCursor(Qt::PointingHandCursor);

    m_sensorOpenFolderBtn = new QPushButton("📂 打开探针库", sensorGroup);
    m_sensorOpenFolderBtn->setStyleSheet("padding: 3px 9px; border-radius: 4px; border: 1px solid #dcdfe6; background: #f4f4f5; font-size: 12px;");
    m_sensorOpenFolderBtn->setCursor(Qt::PointingHandCursor);

    sensorActionRow->addWidget(m_sensorRefreshBtn);
    sensorActionRow->addWidget(m_sensorOpenFolderBtn);
    sensorLayout->addLayout(sensorActionRow);

    memoryLayout->addWidget(sensorGroup);

    tabWidget->addTab(memoryPage, "🧠 长期记忆");

    // =========================================================================
    // ⚙️ TAB: 系统与更新 (System, Updates & Appearance)
    // =========================================================================
    auto sysPage = new QWidget(tabWidget);
    auto sysLayout = new QVBoxLayout(sysPage);
    sysLayout->setSpacing(12);

    // 1. 软件版本与在线更新
    auto updateGroup = new QGroupBox("🔄 软件版本与在线更新", sysPage);
    updateGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto updateForm = new QFormLayout(updateGroup);
    updateForm->setSpacing(10);
    updateForm->setLabelAlignment(Qt::AlignRight);

    auto verLabel = new QLabel(QString("v%1 (官方最新稳定构建)").arg(UpdateManager::instance()->currentVersion()), updateGroup);
    verLabel->setStyleSheet("font-weight: bold; color: #409eff; font-size: 13px;");
    updateForm->addRow("当前软件版本:", verLabel);

    auto updateBtnRow = new QHBoxLayout();
    m_checkUpdateBtn = new QPushButton("🔄 立即检查更新...", updateGroup);
    m_checkUpdateBtn->setStyleSheet("padding: 5px 14px; border-radius: 4px; border: 1px solid #409eff; color: #409eff; background: #ecf5ff; font-weight: 500; font-size: 12px;");
    m_checkUpdateBtn->setCursor(Qt::PointingHandCursor);

    m_openReleaseUrlBtn = new QPushButton("🌐 GitHub 发布页", updateGroup);
    m_openReleaseUrlBtn->setStyleSheet("padding: 5px 12px; border-radius: 4px; border: 1px solid #dcdfe6; color: #606266; background: #f4f4f5; font-size: 12px;");
    m_openReleaseUrlBtn->setCursor(Qt::PointingHandCursor);

    updateBtnRow->addWidget(m_checkUpdateBtn);
    updateBtnRow->addWidget(m_openReleaseUrlBtn);
    updateBtnRow->addStretch();
    updateForm->addRow("版本更新操作:", updateBtnRow);

    m_updateStatusLabel = new QLabel(updateGroup);
    m_updateStatusLabel->setStyleSheet("font-size: 12px; color: #606266;");
    m_updateStatusLabel->setWordWrap(true);
    m_updateStatusLabel->setText("点击上方按钮联网检查 GitHub 最新发行版。");
    updateForm->addRow("检查状态:", m_updateStatusLabel);

    m_autoCheckUpdateCheck = new QCheckBox("应用启动时自动在后台静默检查新版本", updateGroup);
    m_autoCheckUpdateCheck->setStyleSheet("font-size: 12px; color: #303133;");
    updateForm->addRow("", m_autoCheckUpdateCheck);

    sysLayout->addWidget(updateGroup);

    // 2. 桌面外观与状态显示
    auto displayGroup = new QGroupBox("🖥️ 桌面外观与状态微盘", sysPage);
    displayGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto displayForm = new QFormLayout(displayGroup);
    displayForm->setSpacing(8);
    displayForm->setLabelAlignment(Qt::AlignRight);

    m_showHeadStatusOrbCheck = new QCheckBox("头顶显示体力 (⚡) 与心情 (☻) 状态微盘", displayGroup);
    m_showHeadStatusOrbCheck->setStyleSheet("font-size: 13px; font-weight: 500; color: #303133;");
    displayForm->addRow("状态微盘开关:", m_showHeadStatusOrbCheck);

    auto displayTip = new QLabel("开启后将在桌宠头顶实时渲染暗夜环形微盘指示器；关闭后头顶保持清爽纯净（默认关闭）。", displayGroup);
    displayTip->setStyleSheet("font-size: 11px; color: #909399;");
    displayTip->setWordWrap(true);
    displayForm->addRow("", displayTip);

    sysLayout->addWidget(displayGroup);

    // 3. 本地数据与存储
    auto dataGroup = new QGroupBox("📁 本地数据与缓存", sysPage);
    dataGroup->setStyleSheet("QGroupBox { font-weight: bold; font-size: 13px; color: #2c3e50; border: 1px solid #e4e7ed; border-radius: 8px; margin-top: 8px; padding-top: 14px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }");
    auto dataForm = new QFormLayout(dataGroup);
    dataForm->setSpacing(8);
    dataForm->setLabelAlignment(Qt::AlignRight);

    auto dbPathLabel = new QLabel(SettingsDb::instance()->dbPath(), dataGroup);
    dbPathLabel->setStyleSheet("font-size: 11px; color: #909399; font-family: monospace;");
    dbPathLabel->setWordWrap(true);
    dataForm->addRow("配置数据库:", dbPathLabel);

    m_openDataDirBtn = new QPushButton("📂 打开数据存储目录", dataGroup);
    m_openDataDirBtn->setStyleSheet("padding: 5px 12px; border-radius: 4px; border: 1px solid #dcdfe6; color: #606266; background: #f4f4f5; font-size: 12px;");
    m_openDataDirBtn->setCursor(Qt::PointingHandCursor);
    dataForm->addRow("数据快捷方式:", m_openDataDirBtn);

    sysLayout->addWidget(dataGroup);

    sysLayout->addStretch();
    tabWidget->addTab(sysPage, "⚙️ 系统与更新");

    mainLayout->addWidget(tabWidget);

    // 底部按钮栏
    auto btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_cancelBtn = new QPushButton("取消", this);
    m_saveBtn = new QPushButton("保存并应用配置", this);

    m_cancelBtn->setStyleSheet("padding: 7px 18px; border-radius: 6px; border: 1px solid #dcdfe6; background: #ffffff; color: #606266; font-weight: 500;");
    m_saveBtn->setStyleSheet("padding: 7px 22px; border-radius: 6px; border: none; background: #409eff; color: white; font-weight: bold; font-size: 13px;");

    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setCursor(Qt::PointingHandCursor);

    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_saveBtn);
    mainLayout->addLayout(btnLayout);

    // 信号连接
    connect(m_profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) { onProfileSelectionChanged(idx); });
    connect(m_addProfileBtn, &QPushButton::clicked, this, [this]() { onAddProfileClicked(); });
    connect(m_delProfileBtn, &QPushButton::clicked, this, [this]() { onDeleteProfileClicked(); });
    connect(m_setActiveProfileBtn, &QPushButton::clicked, this, [this]() { onSetActiveProfileClicked(); });

    connect(m_personaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) { onPersonaChanged(idx); });
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) { applyPreset(idx); });
    connect(m_testBtn, &QPushButton::clicked, this, [this]() { testConnection(); });
    connect(m_autoDetectKeyBtn, &QPushButton::clicked, this, [this]() { autoDetectAipyKey(); });
    connect(m_testAipyBtn, &QPushButton::clicked, this, [this]() { testAipyConnection(); });

    connect(m_checkUpdateBtn, &QPushButton::clicked, this, &AgentSettingsDialog::checkForUpdates);
    connect(m_openReleaseUrlBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/" GUYI_BOT_REPO "/releases"));
    });
    connect(m_openDataDirBtn, &QPushButton::clicked, this, []() {
        QString dir = QFileInfo(SettingsDb::instance()->dbPath()).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    connect(m_openSkillsDirBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(SkillManager::instance()->skillsDirectory()));
    });
    connect(m_refreshSkillsBtn, &QPushButton::clicked, this, [this]() {
        SkillManager::instance()->scanSkills();
        refreshSkillsTab();
    });
    connect(m_skillsListWidget, &QListWidget::itemChanged, this, [](QListWidgetItem *item) {
        QString skillId = item->data(Qt::UserRole).toString();
        bool enabled = (item->checkState() == Qt::Checked);
        SkillManager::instance()->setSkillEnabled(skillId, enabled);
    });
    connect(m_skillsListWidget, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *) {
        if (!current) return;
        QString skillId = current->data(Qt::UserRole).toString();
        auto skill = SkillManager::instance()->getSkill(skillId);
        m_skillDetailLabel->setText(QString("📖 <b>%1</b> (作者: %2)<br/>%3<br/><br/><b>Prompt 片段:</b><br/>%4")
            .arg(skill.name, skill.author, skill.description.isEmpty() ? "暂无描述" : skill.description,
                 skill.prompt.left(200) + (skill.prompt.length() > 200 ? "..." : "")));
    });

    connect(m_openMcpConfigBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(McpManager::instance()->configFilePath()));
    });
    connect(m_reloadMcpBtn, &QPushButton::clicked, this, [this]() {
        McpManager::instance()->reload();
        refreshMcpTab();
        QMessageBox::information(this, "MCP 服务", "MCP 服务已触发重新加载与连接！");
    });
    connect(m_mcpListWidget, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *) {
        if (!current) return;
        QString sName = current->data(Qt::UserRole).toString();
        for (auto c : McpManager::instance()->getClients()) {
            if (c->serverName() == sName) {
                QString toolsSummary = QString("🔌 服务: <b>%1</b> | 状态: <b>%2</b><br/>执行命令: <code>%3 %4</code><br/><br/><b>暴露的 Tools 列表:</b><br/>")
                    .arg(c->serverName(), c->stateString(), c->command(), c->args().join(" "));
                auto tools = c->tools();
                if (tools.isEmpty()) {
                    toolsSummary += "<i>(暂无已注册的工具)</i>";
                } else {
                    for (const auto &t : tools) {
                        toolsSummary += QString("• <b>%1</b>: %2<br/>").arg(t.name, t.description);
                    }
                }
                m_mcpDetailLabel->setText(toolsSummary);
                return;
            }
        }
    });

    // 长期记忆信号连接
    connect(m_saveOwnerProfileBtn, &QPushButton::clicked, this, &AgentSettingsDialog::onSaveOwnerProfileClicked);
    connect(m_memSearchBtn, &QPushButton::clicked, this, &AgentSettingsDialog::onSearchMemoryClicked);
    connect(m_memSearchEdit, &QLineEdit::returnPressed, this, &AgentSettingsDialog::onSearchMemoryClicked);
    connect(m_memRefreshBtn, &QPushButton::clicked, this, &AgentSettingsDialog::refreshMemoryTab);
    connect(m_memAddBtn, &QPushButton::clicked, this, &AgentSettingsDialog::onAddMemoryClicked);
    connect(m_memDelBtn, &QPushButton::clicked, this, &AgentSettingsDialog::onDeleteMemoryClicked);
    connect(m_memClearAllBtn, &QPushButton::clicked, this, &AgentSettingsDialog::onClearAllMemoriesClicked);
    connect(m_sensorRefreshBtn, &QPushButton::clicked, this, &AgentSettingsDialog::refreshMemoryTab);
    connect(m_sensorOpenFolderBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(SensorManager::instance()->sensorDirectory()));
    });
    LongTermMemoryEngine::instance()->addMemoryListener([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            refreshMemoryTab();
        }, Qt::QueuedConnection);
    });

    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_saveBtn, &QPushButton::clicked, this, &AgentSettingsDialog::saveAndClose);
    
    connect(m_clearMemoryBtn, &QPushButton::clicked, this, [this]() {
        AgentService::instance()->clearMemory();
        QMessageBox::information(this, "提示", "历史会话记忆已全部清空！");
    });

    refreshValues();
}

void AgentSettingsDialog::checkForUpdates() {
    m_checkUpdateBtn->setEnabled(false);
    m_checkUpdateBtn->setText("⏳ 正在检查...");
    m_updateStatusLabel->setStyleSheet("font-size: 12px; color: #409eff;");
    m_updateStatusLabel->setText("正在连接 GitHub 获取最新版本信息...");

    UpdateManager::instance()->checkForUpdates(false, [this](const UpdateInfo &info, const QString &err) {
        m_checkUpdateBtn->setEnabled(true);
        m_checkUpdateBtn->setText("🔄 立即检查更新...");
        if (!err.isEmpty()) {
            m_updateStatusLabel->setStyleSheet("font-size: 12px; color: #f56c6c; font-weight: bold;");
            m_updateStatusLabel->setText("❌ 检查更新失败: " + err);
            QMessageBox::warning(this, "检查更新失败", err);
        } else if (info.hasUpdate) {
            m_updateStatusLabel->setStyleSheet("font-size: 12px; color: #67c23a; font-weight: bold;");
            m_updateStatusLabel->setText(QString("🎉 发现新版本 %1！").arg(info.remoteVersion));
            auto dialog = new UpdateDialog(info, this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            dialog->raise();
            dialog->activateWindow();
        } else {
            m_updateStatusLabel->setStyleSheet("font-size: 12px; color: #67c23a; font-weight: bold;");
            m_updateStatusLabel->setText(QString("🎉 当前已是最新版本 (v%1)，无需更新！").arg(info.currentVersion));
            QMessageBox::information(this, "检查更新", QString("🎉 当前已是最新版本 (v%1)！").arg(info.currentVersion));
        }
    });
}

void AgentSettingsDialog::saveCurrentProfileEdits() {
    if (m_lastSelectedProfileIdx >= 0 && m_lastSelectedProfileIdx < m_tempProfiles.size()) {
        auto &p = m_tempProfiles[m_lastSelectedProfileIdx];
        QString name = m_profileNameEdit->text().trimmed();
        if (!name.isEmpty()) p.name = name;
        p.apiBase = m_apiBaseEdit->text().trimmed();
        p.apiKey = m_apiKeyEdit->text().trimmed();
        p.model = m_modelEdit->text().trimmed();
        p.enabled = m_profileEnabledCheck->isChecked();
    }
}

void AgentSettingsDialog::onProfileSelectionChanged(int index) {
    if (index < 0 || index >= m_tempProfiles.size()) return;
    saveCurrentProfileEdits();
    m_lastSelectedProfileIdx = index;
    const auto &p = m_tempProfiles[index];
    m_profileNameEdit->setText(p.name);
    m_apiBaseEdit->setText(p.apiBase);
    m_apiKeyEdit->setText(p.apiKey);
    m_modelEdit->setText(p.model);
    m_profileEnabledCheck->setChecked(p.enabled);

    bool isActive = (p.id == m_tempActiveId);
    m_setActiveProfileBtn->setText(isActive ? "⭐ 当前活动模型" : "⭐ 设为活动模型");
    m_setActiveProfileBtn->setEnabled(!isActive);
}

void AgentSettingsDialog::onAddProfileClicked() {
    saveCurrentProfileEdits();
    ModelProfile newProfile;
    newProfile.id = "custom_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    newProfile.name = "自定义模型配置 " + QString::number(m_tempProfiles.size() + 1);
    newProfile.apiBase = "https://api.deepseek.com/v1";
    newProfile.apiKey = "";
    newProfile.model = "deepseek-chat";
    newProfile.enabled = true;
    m_tempProfiles.append(newProfile);

    m_profileCombo->blockSignals(true);
    m_profileCombo->addItem(newProfile.name, newProfile.id);
    m_profileCombo->setCurrentIndex(m_tempProfiles.size() - 1);
    m_profileCombo->blockSignals(false);

    onProfileSelectionChanged(m_tempProfiles.size() - 1);
}

void AgentSettingsDialog::onDeleteProfileClicked() {
    if (m_tempProfiles.size() <= 1) {
        QMessageBox::warning(this, "提示", "至少需要保留一个大模型配置！");
        return;
    }
    int curIdx = m_profileCombo->currentIndex();
    if (curIdx < 0 || curIdx >= m_tempProfiles.size()) return;

    QString delId = m_tempProfiles[curIdx].id;
    m_tempProfiles.removeAt(curIdx);
    if (m_tempActiveId == delId) {
        m_tempActiveId = m_tempProfiles.first().id;
    }

    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    for (int i = 0; i < m_tempProfiles.size(); ++i) {
        const auto &p = m_tempProfiles[i];
        QString prefix = (p.id == m_tempActiveId) ? "⭐ " : "";
        m_profileCombo->addItem(prefix + p.name, p.id);
    }
    int newIdx = std::min(curIdx, (int)m_tempProfiles.size() - 1);
    m_profileCombo->setCurrentIndex(newIdx);
    m_profileCombo->blockSignals(false);
    m_lastSelectedProfileIdx = newIdx;
    onProfileSelectionChanged(newIdx);
}

void AgentSettingsDialog::onSetActiveProfileClicked() {
    int curIdx = m_profileCombo->currentIndex();
    if (curIdx < 0 || curIdx >= m_tempProfiles.size()) return;
    saveCurrentProfileEdits();
    m_tempActiveId = m_tempProfiles[curIdx].id;

    m_profileCombo->blockSignals(true);
    for (int i = 0; i < m_tempProfiles.size(); ++i) {
        const auto &p = m_tempProfiles[i];
        QString prefix = (p.id == m_tempActiveId) ? "⭐ " : "";
        m_profileCombo->setItemText(i, prefix + p.name);
    }
    m_profileCombo->blockSignals(false);

    m_setActiveProfileBtn->setText("⭐ 当前活动模型");
    m_setActiveProfileBtn->setEnabled(false);
    QMessageBox::information(this, "提示", QString("已将【%1】设为当前活动模型！").arg(m_tempProfiles[curIdx].name));
}

void AgentSettingsDialog::onPersonaChanged(int index) {
    QString id = m_personaCombo->itemData(index).toString();
    auto persona = PersonaManager::instance()->getPersona(id);
    m_personaDescLabel->setText(persona.description);

    if (id == "custom") {
        m_customPromptEdit->setEnabled(true);
        m_customPromptEdit->setText(PersonaManager::instance()->customPersonaPrompt());
    } else {
        m_customPromptEdit->setEnabled(false);
        m_customPromptEdit->setText(persona.defaultSystemPrompt);
    }
}

void AgentSettingsDialog::refreshValues() {
    auto cfg = AgentService::instance()->config();
    m_tempProfiles = cfg.modelProfiles;
    if (m_tempProfiles.isEmpty()) {
        m_tempProfiles = AgentService::defaultBuiltinProfiles();
    }
    m_tempActiveId = cfg.activeProfileId;
    m_autoFailoverCheck->setChecked(cfg.enableAutoFailover);

    m_profileCombo->blockSignals(true);
    m_profileCombo->clear();
    int activeIdx = 0;
    for (int i = 0; i < m_tempProfiles.size(); ++i) {
        const auto &p = m_tempProfiles[i];
        bool isActive = (p.id == m_tempActiveId);
        if (isActive) activeIdx = i;
        m_profileCombo->addItem(QString("%1%2").arg(isActive ? "⭐ " : "", p.name), p.id);
    }
    m_profileCombo->setCurrentIndex(activeIdx);
    m_profileCombo->blockSignals(false);
    m_lastSelectedProfileIdx = -1;
    onProfileSelectionChanged(activeIdx);

    m_memoryTurnsSpin->setValue(cfg.maxMemoryTurns);
    m_hotkeyTranslateEdit->setText(cfg.hotkeyTranslate.isEmpty() ? "Option+T" : cfg.hotkeyTranslate);
    m_hotkeyAskEdit->setText(cfg.hotkeyAsk.isEmpty() ? "Option+Q" : cfg.hotkeyAsk);
    m_hotkeyHistoryEdit->setText(cfg.hotkeyHistory.isEmpty() ? "Option+H" : cfg.hotkeyHistory);

    m_hotkeyMusicToggleEdit->setText(cfg.hotkeyMusicToggle.isEmpty() ? "Option+M" : cfg.hotkeyMusicToggle);
    m_hotkeyMusicPlayPauseEdit->setText(cfg.hotkeyMusicPlayPause.isEmpty() ? "Option+Space" : cfg.hotkeyMusicPlayPause);
    m_hotkeyMusicNextEdit->setText(cfg.hotkeyMusicNext.isEmpty() ? "Option+Right" : cfg.hotkeyMusicNext);
    m_hotkeyMusicPrevEdit->setText(cfg.hotkeyMusicPrev.isEmpty() ? "Option+Left" : cfg.hotkeyMusicPrev);
    m_hotkeyMusicFavEdit->setText(cfg.hotkeyMusicFav.isEmpty() ? "Option+L" : cfg.hotkeyMusicFav);

    int idx = m_agentTypeCombo->findData(cfg.activeAgentType);
    if (idx >= 0) m_agentTypeCombo->setCurrentIndex(idx);

    int rIdx = m_routingModeCombo->findData(cfg.routingMode);
    if (rIdx >= 0) m_routingModeCombo->setCurrentIndex(rIdx);

    m_aipyBaseEdit->setText(cfg.aipyBase.isEmpty() ? "http://127.0.0.1:41970" : cfg.aipyBase);
    m_aipyKeyEdit->setText(cfg.aipyKey);

    // 人格与状态感知刷新
    QString activePersona = PersonaManager::instance()->activePersonaId();
    int pIdx = m_personaCombo->findData(activePersona);
    if (pIdx >= 0) {
        m_personaCombo->setCurrentIndex(pIdx);
    } else {
        m_personaCombo->setCurrentIndex(0);
    }
    onPersonaChanged(m_personaCombo->currentIndex());

    m_enableStateHookCheck->setChecked(cfg.enableAgentStateHook);
    m_enableLlmNarrationCheck->setChecked(cfg.enableLlmTaskNarration);
    m_stateDebounceSpin->setValue(cfg.stateDebounceSec);

    m_showHeadStatusOrbCheck->setChecked(SettingsDb::instance()->get("ui.show_head_status_orb", "false") == "true");
    m_autoCheckUpdateCheck->setChecked(SettingsDb::instance()->getBool("sys.auto_check_update", true));
    m_updateStatusLabel->setStyleSheet("font-size: 12px; color: #606266;");
    m_updateStatusLabel->setText(QString("当前运行版本: v%1 (官方最新稳定构建)").arg(UpdateManager::instance()->currentVersion()));

    int bIdx = m_banterFreqCombo->findData(cfg.banterFrequencyLevel);
    if (bIdx >= 0) m_banterFreqCombo->setCurrentIndex(bIdx);
    m_contextualCareCheck->setChecked(cfg.enableContextualCare);

    m_testStatusLabel->clear();
    m_agentStatusLabel->clear();

    refreshSkillsTab();
    refreshMcpTab();
    refreshMemoryTab();
}

void AgentSettingsDialog::refreshSkillsTab() {
    m_skillsListWidget->blockSignals(true);
    m_skillsListWidget->clear();
    auto skills = SkillManager::instance()->getAllSkills();
    for (const auto &skill : skills) {
        auto item = new QListWidgetItem(m_skillsListWidget);
        item->setText(QString("✨ %1 (%2)").arg(skill.name, skill.id));
        item->setData(Qt::UserRole, skill.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(skill.enabled ? Qt::Checked : Qt::Unchecked);
    }
    m_skillsListWidget->blockSignals(false);
    if (m_skillsListWidget->count() > 0) {
        m_skillsListWidget->setCurrentRow(0);
    } else {
        m_skillDetailLabel->setText("暂无已加载的技能模块，可点击「打开技能目录」添加 SKILL.md。");
    }
}

void AgentSettingsDialog::refreshMcpTab() {
    m_mcpListWidget->clear();
    auto clients = McpManager::instance()->getClients();
    for (auto c : clients) {
        auto item = new QListWidgetItem(m_mcpListWidget);
        QString statusIcon = (c->state() == McpState::Connected) ? "🟢" : (c->state() == McpState::Connecting ? "🟡" : "🔴");
        item->setText(QString("%1 %2 — %3").arg(statusIcon, c->serverName(), c->stateString()));
        item->setData(Qt::UserRole, c->serverName());
    }
    if (m_mcpListWidget->count() > 0) {
        m_mcpListWidget->setCurrentRow(0);
    } else {
        m_mcpDetailLabel->setText("未运行任何 MCP 服务。可点击「编辑 mcp_servers.json」配置并启用外部 MCP 服务器。");
    }
}

void AgentSettingsDialog::autoDetectAipyKey() {
    QString key = AipyAdapter::autoDetectLocalApiKey();
    if (!key.isEmpty()) {
        m_aipyKeyEdit->setText(key);
        m_agentStatusLabel->setStyleSheet("font-size: 11px; color: #67c23a; font-weight: bold;");
        m_agentStatusLabel->setText("✅ 成功从本地 aipy-pro 数据库提取到 API Key！");
    } else {
        m_agentStatusLabel->setStyleSheet("font-size: 11px; color: #e6a23c;");
        m_agentStatusLabel->setText("⚠️ 未在默认路径找到 aipy-pro 数据库，请确认 aipy-pro 已启动并开启 API");
    }
}

void AgentSettingsDialog::testAipyConnection() {
    QString baseUrl = m_aipyBaseEdit->text().trimmed();
    QString key = m_aipyKeyEdit->text().trimmed();
    if (baseUrl.isEmpty()) baseUrl = "http://127.0.0.1:41970";

    if (auto aipy = AgentService::instance()->aipyAdapter()) {
        aipy->setBaseUrl(baseUrl);
        aipy->setApiKey(key);
        m_testAipyBtn->setEnabled(false);
        m_testAipyBtn->setText("⏳ 测试中...");

        aipy->testConnection([this](bool success, QString const& msg) {
            m_testAipyBtn->setEnabled(true);
            m_testAipyBtn->setText("🔌 测试连通");
            if (success) {
                m_agentStatusLabel->setStyleSheet("font-size: 11px; color: #67c23a; font-weight: bold;");
                m_agentStatusLabel->setText("✅ " + msg);
                QMessageBox::information(this, "aipy 连接成功", "🎉 " + msg);
            } else {
                m_agentStatusLabel->setStyleSheet("font-size: 11px; color: #f56c6c; font-weight: bold;");
                m_agentStatusLabel->setText("❌ " + msg);
                QMessageBox::warning(this, "aipy 连接失败", msg);
            }
        });
    }
}

void AgentSettingsDialog::applyPreset(int index) {
    if (index == 1) { // DeepSeek
        m_apiBaseEdit->setText("https://api.deepseek.com/v1");
        m_modelEdit->setText("deepseek-chat");
        if (m_profileNameEdit->text().isEmpty() || m_profileNameEdit->text().contains("自定义") || m_profileNameEdit->text().contains("新建")) {
            m_profileNameEdit->setText("DeepSeek (官方 API)");
        }
    } else if (index == 2) { // 硅基流动
        m_apiBaseEdit->setText("https://api.siliconflow.cn/v1");
        m_modelEdit->setText("deepseek-ai/DeepSeek-V3");
        if (m_profileNameEdit->text().isEmpty() || m_profileNameEdit->text().contains("自定义") || m_profileNameEdit->text().contains("新建")) {
            m_profileNameEdit->setText("硅基流动 (SiliconFlow)");
        }
    } else if (index == 3) { // 通义千问
        m_apiBaseEdit->setText("https://dashscope.aliyuncs.com/compatible-mode/v1");
        m_modelEdit->setText("qwen-plus");
        if (m_profileNameEdit->text().isEmpty() || m_profileNameEdit->text().contains("自定义") || m_profileNameEdit->text().contains("新建")) {
            m_profileNameEdit->setText("通义千问 (阿里云百炼)");
        }
    } else if (index == 4) { // 智谱清言
        m_apiBaseEdit->setText("https://open.bigmodel.cn/api/paas/v4");
        m_modelEdit->setText("glm-4-flash");
        if (m_profileNameEdit->text().isEmpty() || m_profileNameEdit->text().contains("自定义") || m_profileNameEdit->text().contains("新建")) {
            m_profileNameEdit->setText("智谱清言 (GLM-4-Flash)");
        }
    } else if (index == 5) { // Moonshot
        m_apiBaseEdit->setText("https://api.moonshot.cn/v1");
        m_modelEdit->setText("moonshot-v1-8k");
        if (m_profileNameEdit->text().isEmpty() || m_profileNameEdit->text().contains("自定义") || m_profileNameEdit->text().contains("新建")) {
            m_profileNameEdit->setText("Moonshot (Kimi 官方)");
        }
    } else if (index == 6) { // OpenAI
        m_apiBaseEdit->setText("https://api.openai.com/v1");
        m_modelEdit->setText("gpt-4o-mini");
        if (m_profileNameEdit->text().isEmpty() || m_profileNameEdit->text().contains("自定义") || m_profileNameEdit->text().contains("新建")) {
            m_profileNameEdit->setText("OpenAI (官方 API)");
        }
    } else if (index == 7) { // Ollama
        m_apiBaseEdit->setText("http://127.0.0.1:11434/v1");
        m_modelEdit->setText("qwen2.5:7b");
        if (m_apiKeyEdit->text().isEmpty()) {
            m_apiKeyEdit->setText("ollama");
        }
        if (m_profileNameEdit->text().isEmpty() || m_profileNameEdit->text().contains("自定义") || m_profileNameEdit->text().contains("新建")) {
            m_profileNameEdit->setText("本地 Ollama (127.0.0.1:11434)");
        }
    }
}

void AgentSettingsDialog::testConnection() {
    QString apiBase = m_apiBaseEdit->text().trimmed();
    QString apiKey = m_apiKeyEdit->text().trimmed();
    QString model = m_modelEdit->text().trimmed();

    if (apiBase.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入 API 地址");
        return;
    }
    if (apiKey.isEmpty() && !apiBase.contains("127.0.0.1") && !apiBase.contains("localhost")) {
        QMessageBox::warning(this, "提示", "请输入 API 密钥 (Key)");
        return;
    }
    if (model.isEmpty()) {
        model = "deepseek-chat";
    }

    m_testBtn->setEnabled(false);
    m_testBtn->setText("⏳ 测试中...");
    m_testStatusLabel->setStyleSheet("font-size: 11px; color: #409eff;");
    m_testStatusLabel->setText("正在发送握手请求...");

    AgentService::instance()->testConnection(apiBase, apiKey, model, [this](bool success, QString const& message) {
        m_testBtn->setEnabled(true);
        m_testBtn->setText("🔌 测试模型连通性");

        if (success) {
            m_testStatusLabel->setStyleSheet("font-size: 11px; color: #67c23a; font-weight: bold;");
            m_testStatusLabel->setText("✅ " + message);
            QMessageBox::information(this, "连接成功", "🎉 " + message);
        } else {
            m_testStatusLabel->setStyleSheet("font-size: 11px; color: #f56c6c; font-weight: bold;");
            m_testStatusLabel->setText("❌ " + message);
            QMessageBox::warning(this, "连接测试失败", message);
        }
    });
}

void AgentSettingsDialog::saveAndClose() {
    saveCurrentProfileEdits();

    AgentConfig cfg = AgentService::instance()->config();
    cfg.modelProfiles = m_tempProfiles;
    cfg.activeProfileId = m_tempActiveId;
    cfg.enableAutoFailover = m_autoFailoverCheck->isChecked();

    // 将活动模型数据同步回 cfg
    for (const auto &p : m_tempProfiles) {
        if (p.id == m_tempActiveId) {
            cfg.apiBase = p.apiBase;
            cfg.apiKey = p.apiKey;
            cfg.model = p.model;
            break;
        }
    }

    cfg.maxMemoryTurns = m_memoryTurnsSpin->value();
    cfg.hotkeyTranslate = m_hotkeyTranslateEdit->text().trimmed();
    cfg.hotkeyAsk = m_hotkeyAskEdit->text().trimmed();
    cfg.hotkeyHistory = m_hotkeyHistoryEdit->text().trimmed();
    cfg.hotkeyMusicToggle = m_hotkeyMusicToggleEdit->text().trimmed();
    cfg.hotkeyMusicPlayPause = m_hotkeyMusicPlayPauseEdit->text().trimmed();
    cfg.hotkeyMusicNext = m_hotkeyMusicNextEdit->text().trimmed();
    cfg.hotkeyMusicPrev = m_hotkeyMusicPrevEdit->text().trimmed();
    cfg.hotkeyMusicFav = m_hotkeyMusicFavEdit->text().trimmed();

    cfg.activeAgentType = m_agentTypeCombo->currentData().toString();
    cfg.routingMode = m_routingModeCombo->currentData().toString();
    cfg.aipyBase = m_aipyBaseEdit->text().trimmed();
    cfg.aipyKey = m_aipyKeyEdit->text().trimmed();

    // 状态感知与 Token 省流
    cfg.enableAgentStateHook = m_enableStateHookCheck->isChecked();
    cfg.enableLlmTaskNarration = m_enableLlmNarrationCheck->isChecked();
    cfg.stateDebounceSec = m_stateDebounceSpin->value();

    if (cfg.apiBase.isEmpty()) cfg.apiBase = "https://api.deepseek.com/v1";
    if (cfg.model.isEmpty()) cfg.model = "deepseek-chat";
    if (cfg.hotkeyTranslate.isEmpty()) cfg.hotkeyTranslate = "Option+T";
    if (cfg.hotkeyAsk.isEmpty()) cfg.hotkeyAsk = "Option+Q";
    if (cfg.hotkeyHistory.isEmpty()) cfg.hotkeyHistory = "Option+H";
    if (cfg.hotkeyMusicToggle.isEmpty()) cfg.hotkeyMusicToggle = "Option+M";
    if (cfg.hotkeyMusicPlayPause.isEmpty()) cfg.hotkeyMusicPlayPause = "Option+Space";
    if (cfg.hotkeyMusicNext.isEmpty()) cfg.hotkeyMusicNext = "Option+Right";
    if (cfg.hotkeyMusicPrev.isEmpty()) cfg.hotkeyMusicPrev = "Option+Left";
    if (cfg.hotkeyMusicFav.isEmpty()) cfg.hotkeyMusicFav = "Option+L";
    if (cfg.aipyBase.isEmpty()) cfg.aipyBase = "http://127.0.0.1:41970";

    // 保存人格设置
    QString chosenPersonaId = m_personaCombo->currentData().toString();
    PersonaManager::instance()->setActivePersonaId(chosenPersonaId);
    if (chosenPersonaId == "custom") {
        PersonaManager::instance()->setCustomPersonaPrompt(m_customPromptEdit->toPlainText().trimmed());
    }

    // 保存外观、系统与自主搭讪设置
    bool showOrb = m_showHeadStatusOrbCheck->isChecked();
    SettingsDb::instance()->set("ui.show_head_status_orb", showOrb ? "true" : "false");
    SettingsDb::instance()->setBool("sys.auto_check_update", m_autoCheckUpdateCheck->isChecked());
    for (auto pet : ShijimaManager::defaultManager()->mascots()) {
        if (pet) pet->repaint();
    }

    cfg.banterFrequencyLevel = m_banterFreqCombo->currentData().toInt();
    cfg.enableContextualCare = m_contextualCareCheck->isChecked();

    AgentService::instance()->setConfig(cfg);
    accept();
}

void AgentSettingsDialog::refreshMemoryTab() {
    auto profile = LongTermMemoryEngine::instance()->coreProfile();
    m_ownerNameEdit->setText(profile.name);
    m_ownerOccEdit->setText(profile.occupation);
    m_ownerTechEdit->setText(profile.preferredLangs);
    m_ownerMusicEdit->setText(profile.musicTaste);
    m_ownerHabitEdit->setText(profile.workHabits);
    m_ownerNotesEdit->setPlainText(profile.notes);

    m_memListWidget->blockSignals(true);
    m_memListWidget->clear();
    auto memories = LongTermMemoryEngine::instance()->getAllActiveMemories();
    for (const auto &mem : memories) {
        auto item = new QListWidgetItem(m_memListWidget);
        QString stars = QString("★").repeated(mem.importance);
        QString timeStr = QDateTime::fromMSecsSinceEpoch(mem.createdAt).toString("MM-dd HH:mm");
        item->setText(QString("[%1 | %2] %3 (%4)").arg(mem.category, stars, mem.content, timeStr));
        item->setData(Qt::UserRole, mem.id);
        item->setToolTip(QString("记忆ID: %1\n分类: %2 | 重要度: %3\n记录时间: %4\n检索引用: %5 次")
            .arg(mem.id, mem.category, QString::number(mem.importance), timeStr, QString::number(mem.accessCount)));
    }
    m_memListWidget->blockSignals(false);

    int count = memories.size();
    m_memStatsLabel->setText(QString("📊 长期事实库共归档: %1 条 | 存储: guyi_bot_settings.db").arg(count));

    // 刷新应用感知与探针足迹
    auto curCtx = SensorManager::instance()->currentContext();
    QString curApp = curCtx["app_name"].toString();
    QString curAct = curCtx["semantic_activity"].toString();
    QString curDet = curCtx["detail"].toString();
    QString curFile = curCtx["active_file"].toString();
    QString curUrl = curCtx["url"].toString();

    if (!curApp.isEmpty()) {
        QString statusText = QString("💡 <b>当前实时感知</b>: [%1] %2").arg(curApp, curAct);
        if (!curFile.isEmpty()) statusText += QString(" | 编辑: <code>%1</code>").arg(curFile);
        else if (!curUrl.isEmpty()) statusText += QString(" | 浏览: %1").arg(curUrl);
        else if (!curDet.isEmpty()) statusText += QString(" | %1").arg(curDet);
        m_sensorCurrentStatusLabel->setText(statusText);
    } else {
        m_sensorCurrentStatusLabel->setText("💡 <b>当前实时感知</b>: 等待前台应用切换与探针捕获中...");
    }

    m_sensorActivityList->blockSignals(true);
    m_sensorActivityList->clear();
    auto histList = SensorManager::instance()->recentActivityHistory();
    for (const auto &histObj : histList) {
        QString app = histObj["app_name"].toString();
        QString act = histObj["semantic_activity"].toString();
        QString det = histObj["detail"].toString();
        qint64 ts = histObj["timestamp"].toVariant().toLongLong();
        QString timeStr = QDateTime::fromMSecsSinceEpoch(ts).toString("HH:mm:ss");

        auto item = new QListWidgetItem(m_sensorActivityList);
        item->setText(QString("[%1] %2: %3 %4").arg(timeStr, app, act, det.isEmpty() ? "" : ("(" + det + ")")));
    }
    m_sensorActivityList->blockSignals(false);

    int sensorCount = SensorManager::instance()->installedSensors().size();
    m_sensorStatsLabel->setText(QString("📡 已部署专属探针: %1 个 | 内存足迹: %2 条 | 永久入库: episodic_events").arg(sensorCount).arg(histList.size()));
}

void AgentSettingsDialog::onSaveOwnerProfileClicked() {
    CoreUserProfile p;
    p.name = m_ownerNameEdit->text().trimmed();
    p.occupation = m_ownerOccEdit->text().trimmed();
    p.preferredLangs = m_ownerTechEdit->text().trimmed();
    p.musicTaste = m_ownerMusicEdit->text().trimmed();
    p.workHabits = m_ownerHabitEdit->text().trimmed();
    p.notes = m_ownerNotesEdit->toPlainText().trimmed();
    LongTermMemoryEngine::instance()->updateCoreProfile(p);
    QMessageBox::information(this, "主人画像", "主人全局核心档案已成功保存至长期记忆数据库！");
}

void AgentSettingsDialog::onSearchMemoryClicked() {
    QString q = m_memSearchEdit->text().trimmed();
    if (q.isEmpty()) {
        refreshMemoryTab();
        return;
    }

    auto memories = LongTermMemoryEngine::instance()->searchMemories(q, 15);
    m_memListWidget->blockSignals(true);
    m_memListWidget->clear();
    for (const auto &mem : memories) {
        auto item = new QListWidgetItem(m_memListWidget);
        QString stars = QString("★").repeated(mem.importance);
        QString timeStr = QDateTime::fromMSecsSinceEpoch(mem.createdAt).toString("MM-dd HH:mm");
        item->setText(QString("[%1 | %2] %3 (%4)").arg(mem.category, stars, mem.content, timeStr));
        item->setData(Qt::UserRole, mem.id);
    }
    m_memListWidget->blockSignals(false);
    m_memStatsLabel->setText(QString("🔍 检索到匹配「%1」的相关记忆: %2 条").arg(q, QString::number(memories.size())));
}

void AgentSettingsDialog::onAddMemoryClicked() {
    bool ok = false;
    QString fact = QInputDialog::getText(this, "记一条新事实", "请输入需要桌宠记住的关于您或项目的长期信息:", QLineEdit::Normal, "", &ok);
    if (ok && !fact.trimmed().isEmpty()) {
        LongTermMemoryEngine::instance()->addSemanticMemory("fact", fact.trimmed(), 3);
        refreshMemoryTab();
    }
}

void AgentSettingsDialog::onDeleteMemoryClicked() {
    auto cur = m_memListWidget->currentItem();
    if (!cur) {
        QMessageBox::warning(this, "提示", "请先在上方列表中选中需要删除的记忆条目。");
        return;
    }
    QString id = cur->data(Qt::UserRole).toString();
    LongTermMemoryEngine::instance()->deleteSemanticMemory(id);
    refreshMemoryTab();
}

void AgentSettingsDialog::onClearAllMemoriesClicked() {
    auto reply = QMessageBox::question(this, "危险操作", "确定要彻底清空长期语义事实记忆库吗？该操作不可恢复。", QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        LongTermMemoryEngine::instance()->clearAllSemanticMemories();
        refreshMemoryTab();
    }
}

