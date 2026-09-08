#pragma once

// 
// Shijima-Qt - aipy-pro Agent Adapter
// 

#include "AgentAdapter.hpp"
#include <QObject>

class QNetworkAccessManager;

class AipyAdapter : public AgentAdapter {
public:
    explicit AipyAdapter(QNetworkAccessManager *netMgr);
    ~AipyAdapter() override = default;

    QString type() const override { return "aipy"; }
    QString displayName() const override { return "aipy-pro (本地 Agent)"; }

    void setBaseUrl(QString const& url) { m_baseUrl = url.trimmed(); }
    void setApiKey(QString const& key) { m_apiKey = key.trimmed(); }
    QString baseUrl() const { return m_baseUrl; }
    QString apiKey() const { return m_apiKey; }

    void executeTask(QString const& instruction,
                     QString const& contextText,
                     std::function<void(QString const& progressMsg)> progressCallback,
                     std::function<void(AgentTaskResult const& result)> finishCallback) override;

    void testConnection(std::function<void(bool success, QString const& message)> callback) override;
    void openTask(QString const& taskId) override;

    // 任务会话上下文延续管理
    QString lastTaskId() const { return m_lastTaskId; }
    QString lastTaskTitle() const { return m_lastTaskTitle; }
    bool hasActiveSession() const;
    void resetSession();
    void setForceNewTask(bool force) { m_forceNewTask = force; }

    // 自动从本地 aipy-pro SQLite 数据库读取 API 密钥
    static QString autoDetectLocalApiKey();

private:
    void pollTask(QString const& taskId,
                  int pollCount,
                  std::function<void(QString const& progressMsg)> progressCallback,
                  std::function<void(AgentTaskResult const& result)> finishCallback);

    void fetchFinalReply(QString const& taskId,
                         std::function<void(AgentTaskResult const& result)> finishCallback);

    QString effectiveApiKey() const;

    QString m_baseUrl;
    QString m_apiKey;
    QNetworkAccessManager *m_netMgr;

    // 最近执行的任务上下文（用于连续多轮跟进）
    QString m_lastTaskId;
    QString m_lastTaskTitle;
    qint64 m_lastTaskTime = 0;
    bool m_forceNewTask = false;
};
