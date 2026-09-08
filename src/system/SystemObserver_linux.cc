// 
// Shijima-Qt - System Observer for Linux Implementation
// 

#include "SystemObserver.hpp"
#include "PetEventBus.hpp"
#include <QCoreApplication>
#include <QJsonObject>
#include <QDateTime>
#include <QFileInfo>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <iostream>

SystemObserver::SystemObserver() {
}

SystemObserver::~SystemObserver() {
    stop();
}

SystemObserver* SystemObserver::instance() {
    static SystemObserver s_instance;
    return &s_instance;
}

void SystemObserver::start() {
    if (m_started) return;
    m_started = true;

    std::cout << "[LinuxSystemObserver] Linux 系统感知监听器已启动" << std::endl;

    // 1. 检查根分区磁盘可用空间
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        double freeGb = static_cast<double>(stat.f_bavail * stat.f_frsize) / (1024.0 * 1024.0 * 1024.0);
        if (freeGb < 10.0) {
            QJsonObject payload;
            payload["free_gb"] = freeGb;
            PetEventBus::instance()->emitEvent("system.disk_low", payload);
        }
    }

    // 2. 检查系统内存
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        if (si.totalram > 0) {
            double usedRatio = static_cast<double>(si.totalram - si.freeram) / static_cast<double>(si.totalram);
            if (usedRatio >= 0.90) {
                QJsonObject payload;
                payload["memory_usage"] = usedRatio * 100.0;
                PetEventBus::instance()->emitEvent("system.memory_high", payload);
            }
        }
    }
}

void SystemObserver::stop() {
    if (!m_started) return;
    m_started = false;
}

QString SystemObserver::currentActiveAppName() const {
    return m_activeAppName;
}

QString SystemObserver::currentActiveWindowTitle() const {
    return m_activeWindowTitle;
}

QString SystemObserver::currentActiveBundleId() const {
    return m_activeBundleId;
}

QJsonObject SystemObserver::currentSemanticActivity() const {
    return QJsonObject();
}

int SystemObserver::continuousWorkMinutes() const {
    return 0;
}

void SystemObserver::recordAppActivation(const QString &appName, const QString &bundleId, const QString &windowTitle) {
    m_activeAppName = appName;
    m_activeBundleId = bundleId;
    m_activeWindowTitle = windowTitle;
}

