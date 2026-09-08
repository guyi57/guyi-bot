#pragma once

// 
// guyi-bot - AI Model, Memory, Agent, Skills & MCP Settings Dialog
// 

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include "AgentService.hpp"

class AgentSettingsDialog : public QDialog
{
public:
    explicit AgentSettingsDialog(QWidget *parent = nullptr);
    void refreshValues();

private:
    void applyPreset(int index);
    void onProfileSelectionChanged(int index);
    void onAddProfileClicked();
    void onDeleteProfileClicked();
    void onSetActiveProfileClicked();
    void saveCurrentProfileEdits();

    void onPersonaChanged(int index);
    void saveAndClose();
    void testConnection();
    void testAipyConnection();
    void autoDetectAipyKey();

    void refreshSkillsTab();
    void refreshMcpTab();
    void refreshMemoryTab();
    void onSaveOwnerProfileClicked();
    void onSearchMemoryClicked();
    void onAddMemoryClicked();
    void onDeleteMemoryClicked();
    void onClearAllMemoriesClicked();
    void checkForUpdates();

    // 多大模型配置池
    QComboBox *m_profileCombo;
    QPushButton *m_addProfileBtn;
    QPushButton *m_delProfileBtn;
    QPushButton *m_setActiveProfileBtn;
    QCheckBox *m_profileEnabledCheck;
    QCheckBox *m_autoFailoverCheck;

    QVector<ModelProfile> m_tempProfiles;
    QString m_tempActiveId;
    int m_lastSelectedProfileIdx = 0;

    // 基础模型配置
    QComboBox *m_presetCombo;
    QLineEdit *m_profileNameEdit;
    QLineEdit *m_apiBaseEdit;
    QLineEdit *m_apiKeyEdit;
    QLineEdit *m_modelEdit;
    QSpinBox *m_memoryTurnsSpin;
    QLineEdit *m_hotkeyTranslateEdit;
    QLineEdit *m_hotkeyAskEdit;
    QLineEdit *m_hotkeyHistoryEdit;
    QLineEdit *m_hotkeyMusicToggleEdit;
    QLineEdit *m_hotkeyMusicPlayPauseEdit;
    QLineEdit *m_hotkeyMusicNextEdit;
    QLineEdit *m_hotkeyMusicPrevEdit;
    QLineEdit *m_hotkeyMusicFavEdit;

    QPushButton *m_testBtn;
    QLabel *m_testStatusLabel;

    // 🎭 人格适配器 (Persona) 与 外观显示
    QComboBox *m_personaCombo;
    QLabel *m_personaDescLabel;
    QTextEdit *m_customPromptEdit;
    QCheckBox *m_showHeadStatusOrbCheck;
    QComboBox *m_banterFreqCombo;
    QCheckBox *m_contextualCareCheck;

    // 🤖 Coding Agent 状态感知与 Token 省流开关
    QCheckBox *m_enableStateHookCheck;
    QCheckBox *m_enableLlmNarrationCheck;
    QSpinBox *m_stateDebounceSpin;

    // 智能体 Agent 适配器配置
    QComboBox *m_agentTypeCombo;
    QComboBox *m_routingModeCombo;
    QLineEdit *m_aipyBaseEdit;
    QLineEdit *m_aipyKeyEdit;
    QPushButton *m_autoDetectKeyBtn;
    QPushButton *m_testAipyBtn;
    QLabel *m_agentStatusLabel;

    // 🛠️ 技能库 (Skills)
    QListWidget *m_skillsListWidget;
    QLabel *m_skillDetailLabel;
    QPushButton *m_openSkillsDirBtn;
    QPushButton *m_refreshSkillsBtn;

    // 🔌 MCP 服务
    QListWidget *m_mcpListWidget;
    QLabel *m_mcpDetailLabel;
    QPushButton *m_openMcpConfigBtn;
    QPushButton *m_reloadMcpBtn;

    // 🧠 长期记忆 (Long-Term Memory)
    QLineEdit *m_ownerNameEdit;
    QLineEdit *m_ownerOccEdit;
    QLineEdit *m_ownerTechEdit;
    QLineEdit *m_ownerMusicEdit;
    QLineEdit *m_ownerHabitEdit;
    QTextEdit *m_ownerNotesEdit;
    QPushButton *m_saveOwnerProfileBtn;

    QLineEdit *m_memSearchEdit;
    QPushButton *m_memSearchBtn;
    QPushButton *m_memRefreshBtn;
    QPushButton *m_memAddBtn;
    QPushButton *m_memDelBtn;
    QPushButton *m_memClearAllBtn;
    QListWidget *m_memListWidget;
    QLabel *m_memStatsLabel;

    // 📡 实时应用感知与探针足迹 (Live App Sensing & Probes)
    QLabel *m_sensorCurrentStatusLabel;
    QListWidget *m_sensorActivityList;
    QLabel *m_sensorStatsLabel;
    QPushButton *m_sensorRefreshBtn;
    QPushButton *m_sensorOpenFolderBtn;

    // ⚙️ 系统与更新 (System & Updates)
    QCheckBox *m_autoCheckUpdateCheck;
    QPushButton *m_checkUpdateBtn;
    QLabel *m_updateStatusLabel;
    QPushButton *m_openReleaseUrlBtn;
    QPushButton *m_openDataDirBtn;

    QPushButton *m_clearMemoryBtn;
    QPushButton *m_saveBtn;
    QPushButton *m_cancelBtn;
};
