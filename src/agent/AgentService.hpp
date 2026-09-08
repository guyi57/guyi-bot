#pragma once

// 
// Shijima-Qt - AI Agent & Memory Service with Adapter Architecture
// 

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <map>
#include <functional>
#include "AgentAdapter.hpp"

class QNetworkAccessManager;
class AipyAdapter;

struct AIBehaviorIntent {
    QString intent = "chat";     // "chat", "seek_attention", "celebrate", "comfort", "explore", "rest", "play", "care"
    QString emotion = "happy";   // "happy", "bored", "angry", "sleepy", "curious", "caring", "proud"
    QString target = "cursor";   // "cursor", "window", "screen_edge"
    QString speech;              // 简练短句（3~20字）
    QString action;              // e.g. "jump", "bounce", "dangle", "sit", "walk", "sleep"
    QString emote;               // e.g. "💖", "✨", "💤", "💢", "💫", "💡", "🎵"
    bool blush = false;          // 是否害羞脸红
    int urgency = 1;             // 1~5
};

struct AgentStatusEvent {
    QString agentName = "Coding Agent"; // "Claude Code", "Cursor", "Codex", etc.
    QString status = "idle";            // "thinking", "working", "coding", "need_approval", "finished", "error", "idle"
    QString task;                       // e.g. "正在重构数据库连接池"
    QString details;                    // e.g. "12 个测试通过"
    QString customAction;               // e.g. "jump", "celebrate", "resist", "sit"
    qint64 timestamp = 0;
};

struct ModelProfile {
    QString id;          // "deepseek", "siliconflow", "qwen", "openai", "zhipu", "moonshot", "ollama", "custom_1" ...
    QString name;        // "DeepSeek (官方 API)", "硅基流动 (DeepSeek-V3)", "通义千问 (阿里云百炼)", etc.
    QString apiBase;     // "https://api.deepseek.com/v1", "https://api.siliconflow.cn/v1", etc.
    QString apiKey;      // Provider API Key
    QString model;       // "deepseek-chat", "deepseek-ai/DeepSeek-V3", "qwen-plus", "gpt-4o-mini", etc.
    bool enabled = true; // 是否启用此配置参与自动故障转移/轮询
};

struct AgentConfig {
    // 多大模型配置池
    QVector<ModelProfile> modelProfiles;
    QString activeProfileId;
    bool enableAutoFailover = true;

    // 当前生效的模型连接参数（用于向下兼容旧字段）
    QString apiBase = "https://api.deepseek.com/v1";
    QString apiKey;
    QString model = "deepseek-chat";
    int maxMemoryTurns = 10;
    QString hotkeyTranslate = "Option+T";
    QString hotkeyAsk = "Option+Q";
    QString hotkeyHistory = "Option+H";
    QString hotkeyMusicToggle = "Option+M";
    QString hotkeyMusicPlayPause = "Option+Space";
    QString hotkeyMusicNext = "Option+Right";
    QString hotkeyMusicPrev = "Option+Left";
    QString hotkeyMusicFav = "Option+L";

    // 智能体 Agent 适配器
    QString activeAgentType = "builtin";
    QString routingMode = "direct";
    QString aipyBase = "http://127.0.0.1:41970";
    QString aipyKey;

    // 状态感知与 Token 节流控制
    bool enableAgentStateHook = true;
    bool enableLlmTaskNarration = true;
    int stateDebounceSec = 3;

    // 灵动拟人化与自主搭讪控制
    int banterFrequencyLevel = 2; // 0: 关闭, 1: 偶尔 (30m), 2: 适度 (18m), 3: 活跃 (10m)
    bool enableContextualCare = true; // 启用前台应用感知与工作健康关怀
};

class AgentService
{
public:
    static AgentService *instance();

    void loadConfig(QString const& path = "config.json");
    void saveConfig(QString const& path = "config.json");
    AgentConfig const& config() const { return m_config; }
    void setConfig(AgentConfig const& cfg);

    // 多模型配置池管理
    QVector<ModelProfile> const& modelProfiles() const { return m_config.modelProfiles; }
    ModelProfile getActiveProfile() const;
    void setActiveProfile(const QString &profileId);
    void addOrUpdateProfile(const ModelProfile &profile);
    void deleteProfile(const QString &profileId);
    void setAutoFailover(bool enable);
    static QVector<ModelProfile> defaultBuiltinProfiles();

    // 智能翻译：中文 -> 英文，非中文 -> 中文
    void translate(QString const& text, std::function<void(bool success, QString const& result)> callback);

    // 智能提问：支持分流路由（简单问题直答，复杂任务交给 Agent 适配器执行）
    void ask(QString const& contextText,
             QString const& question,
             std::function<void(QString const& progressMsg)> progressCallback,
             std::function<void(bool success, QString const& result, QString const& appTarget)> finishCallback);

    // 测试模型接口连通性
    void testConnection(QString const& apiBase, QString const& apiKey, QString const& model, std::function<void(bool success, QString const& message)> callback);

    // 测试指定 Agent 连通性
    void testAgentConnection(QString const& agentType, std::function<void(bool success, QString const& message)> callback);

    // 打开指定任务
    void openTask(QString const& taskId);

    // 获取 aipy 适配器对象
    AipyAdapter *aipyAdapter() const;

    // AI 桌面宠物行为意图生成（人格化思考与主动交互）
    void requestPetIntent(const QJsonObject &contextInfo, std::function<void(bool success, const AIBehaviorIntent &intent)> callback);

    // 针对用户触摸、移动、摸头等物理交互的 AI 模型情感与台词反馈
    void requestPetInteractionFeedback(const QString &interactionType, const QJsonObject &petStateInfo, std::function<void(bool success, const AIBehaviorIntent &intent)> callback);

    // AI 自主合成针对未知应用的轻量只读探针脚本 (Self-Synthesizing Sensor)
    void synthesizeAppSensorScript(const QString &appName, const QString &bundleId, const QString &windowTitle, std::function<void(bool success, const QString &scriptCode)> callback);

    // 接收外部 Coding Agent 状态感知事件并驱动桌宠互动
    void handleAgentStatus(AgentStatusEvent const& event, std::function<void(bool success, QString const& message)> callback = nullptr);
    AgentStatusEvent lastAgentStatus() const { return m_lastStatus; }

    void clearMemory();
    QJsonArray const& memoryHistory() const { return m_history; }
    void sendChatCompletion(QJsonArray const& messages, std::function<void(bool success, QString const& result)> callback);

private:
    AgentService();
    void initAdapters();
    void syncAdapterConfigs();
    bool containsChinese(QString const& text);
    void appendMemory(QString const& role, QString const& content);
    void saveMemoryToFile();
    void loadMemoryFromFile();

    AgentConfig m_config;
    AgentStatusEvent m_lastStatus;
    QNetworkAccessManager *m_networkManager;
    QJsonArray m_history;
    std::map<QString, std::shared_ptr<AgentAdapter>> m_adapters;
};
