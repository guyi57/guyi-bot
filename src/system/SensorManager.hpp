#pragma once

#include <QString>
#include <QJsonObject>
#include <QRecursiveMutex>
#include <functional>
#include <memory>
#include <unordered_set>

struct AppSemanticContext
{
    QString bundleId;
    QString appName;
    QString windowTitle;
    QString semanticActivity; // 例如: "正在调试 MessageBubble.cc 气泡排版崩溃"
    QString detail;           // 例如: "修改了 2 个源文件，git 状态为 modified"
    QString activeFile;       // 例如: "src/ui/MessageBubble.cc"
    QString url;              // 例如: "https://github.com/..."
    QString workspace;        // 例如: "xuanfu"
    QString focusLevel;       // "high", "normal", "low"
    qint64 timestamp = 0;

    QJsonObject toJson() const;
    static AppSemanticContext fromJson(const QJsonObject &obj);
};

class SensorManager
{
public:
    static SensorManager* instance();

    // 初始化探针目录并预置主流应用探针
    void init();

    // 前台应用切换通知
    void onAppActivated(const QString &appName, const QString &bundleId, const QString &windowTitle);

    // 获取当前前台应用的最新语义感知数据
    QJsonObject currentContext() const;

    // 获取前一个工作应用上下文（用于切回恢复判定）
    QJsonObject previousWorkContext() const;

    // 探针脚本管理
    QString sensorDirectory() const;
    bool hasSensor(const QString &bundleId, const QString &appName) const;
    QString getSensorPath(const QString &bundleId, const QString &appName) const;
    void saveSensor(const QString &identifier, const QString &scriptContent);

    // 执行探针（带 1.5s 超时安全熔断机制）
    QJsonObject runSensor(const QString &scriptPath, const QString &appName, const QString &bundleId, const QString &windowTitle);

    // 触发大模型为新应用编写探针
    void requestSensorSynthesis(const QString &appName, const QString &bundleId, const QString &windowTitle);

    // 黑名单检查（敏感安全软件硬编码跳过）
    bool isBlacklisted(const QString &bundleId, const QString &appName) const;

    // 获取近期捕获的应用感知流水（用于 UI 展示）
    QList<QJsonObject> recentActivityHistory() const;

    // 获取当前已安装的所有探针文件名
    QStringList installedSensors() const;

private:
    SensorManager();
    ~SensorManager() = default;

    void deployBuiltinSensors();

    mutable QRecursiveMutex m_mutex;
    QString m_sensorsDir;

    AppSemanticContext m_currentContext;
    AppSemanticContext m_previousWorkContext;
    qint64 m_lastProbeTime = 0;
    QString m_lastProbedTarget;

    std::unordered_set<std::string> m_pendingSynthesis;
    QList<QJsonObject> m_activityHistory;
};
