#pragma once

#include <QString>
#include <QJsonObject>

struct SystemResourceStats {
    double cpuPercent = 0.0;
    double memPercent = 0.0;
    double memUsedGb = 0.0;
    double memTotalGb = 0.0;
    double diskPercent = 0.0;
    double diskFreeGb = 0.0;
    double diskTotalGb = 0.0;

    QString formattedSummary() const {
        return QString(
            "⚠️ 感觉电脑有点喘不过气了！当前系统资源占用：\n"
            "• 🧠 内存占用: %1% (%2 GB / %3 GB)\n"
            "• ⚡ CPU 整体负载: %4%\n"
            "• 💾 硬盘空间占用: %5% (可用 %6 GB / %7 GB)"
        ).arg(QString::number(memPercent, 'f', 1))
         .arg(QString::number(memUsedGb, 'f', 1))
         .arg(QString::number(memTotalGb, 'f', 1))
         .arg(QString::number(cpuPercent, 'f', 1))
         .arg(QString::number(diskPercent, 'f', 1))
         .arg(QString::number(diskFreeGb, 'f', 1))
         .arg(QString::number(diskTotalGb, 'f', 1));
    }
};

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
    QString currentActiveDocumentPath() const;
    QString currentActiveContentSummary() const;
    QJsonObject currentSemanticActivity() const;

    // 获取完整系统资源占比 (CPU、内存、硬盘)
    SystemResourceStats getSystemResourceStats();

    // 连续专注/写代码时长（分钟）
    int continuousWorkMinutes() const;

    // 内部由平台监听器通知更新状态
    void recordAppActivation(const QString &appName, const QString &bundleId, const QString &windowTitle, const QString &documentPath = QString());
    void updateContentSummary(const QString &contentSummary);

private:
    SystemObserver();
    ~SystemObserver();

    bool m_started = false;
    void *m_observerContext = nullptr; // 用于存储 Objective-C 观察者句柄

    QString m_activeAppName;
    QString m_activeWindowTitle;
    QString m_activeBundleId;
    QString m_activeDocumentPath;
    QString m_activeContentSummary;
    qint64 m_appStartTime = 0;
    qint64 m_workSessionStartTime = 0;
};
