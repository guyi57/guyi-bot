#pragma once

#include <QString>
#include <QJsonObject>

class SystemObserver
{
public:
    static SystemObserver* instance();
    void start();
    void stop();

    // 查询当前前台活跃应用名称与窗口标题
    QString currentActiveAppName() const;
    QString currentActiveWindowTitle() const;
    QString currentActiveBundleId() const;
    QJsonObject currentSemanticActivity() const;

    // 连续专注/写代码时长（分钟）
    int continuousWorkMinutes() const;

    // 内部由平台监听器通知更新状态
    void recordAppActivation(const QString &appName, const QString &bundleId, const QString &windowTitle);

private:
    SystemObserver();
    ~SystemObserver();

    bool m_started = false;
    void *m_observerContext = nullptr; // 用于存储 Objective-C 观察者句柄

    QString m_activeAppName;
    QString m_activeWindowTitle;
    QString m_activeBundleId;
    qint64 m_appStartTime = 0;
    qint64 m_workSessionStartTime = 0;
};
