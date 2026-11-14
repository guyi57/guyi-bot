#pragma once

// 
// Shijima-Qt - DeepSeek Harness (dsh) Agent Adapter
// 

#include "AgentAdapter.hpp"
#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QDateTime>

class DeepSeekHarnessAdapter : public AgentAdapter {
public:
    explicit DeepSeekHarnessAdapter();
    ~DeepSeekHarnessAdapter() override;

    QString type() const override { return "deepseek_harness"; }
    QString displayName() const override { return "DeepSeek Harness (dsh 智能体)"; }

    void setDshPath(QString const& path) { m_dshPath = path.trimmed(); }
    QString dshPath() const { return m_dshPath; }

    void setApiKey(QString const& key) { m_apiKey = key.trimmed(); }
    QString apiKey() const { return m_apiKey; }

    void setBaseUrl(QString const& url) { m_baseUrl = url.trimmed(); }
    QString baseUrl() const { return m_baseUrl; }

    void setModel(QString const& model) { m_model = model.trimmed(); }
    QString model() const { return m_model; }

    void setAutoApproval(bool autoApprove) { m_autoApproval = autoApprove; }
    bool autoApproval() const { return m_autoApproval; }

    // 探测系统中的 dsh 命令路径
    static QString findDshExecutable();

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

    // 终止当前正在运行的 dsh 子进程
    void cancelCurrentTask();

private:
    QString effectiveDshPath() const;
    QString effectiveApiKey() const;
    QString effectiveModel() const;
    QString effectiveBaseUrl() const;
    QString detectActiveWorkspace() const;

    QString m_dshPath;
    QString m_apiKey;
    QString m_baseUrl = "https://api.deepseek.com";
    QString m_model = "deepseek-chat";
    bool m_autoApproval = true;

    QString m_lastTaskId;
    QString m_lastTaskTitle;
    qint64 m_lastTaskTime = 0;
    bool m_forceNewTask = false;
    bool m_isManuallyCancelled = false;

    QPointer<QProcess> m_activeProcess;
};
