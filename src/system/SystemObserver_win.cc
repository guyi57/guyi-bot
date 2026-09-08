// 
// Shijima-Qt - System Observer for Windows Implementation
// 

#include "SystemObserver.hpp"
#include "PetEventBus.hpp"
#include <QCoreApplication>
#include <QAbstractNativeEventFilter>
#include <QJsonObject>
#include <QDateTime>
#include <QFileInfo>
#include <windows.h>
#include <psapi.h>
#include <iostream>

static HWINEVENTHOOK s_winEventHook = NULL;

class WinPowerNativeFilter : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override {
        Q_UNUSED(result);
        if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
            MSG *msg = static_cast<MSG*>(message);
            if (msg->message == WM_POWERBROADCAST) {
                if (msg->wParam == PBT_APMSUSPEND) {
                    std::cout << "[WinSystemObserver] 捕获到系统休眠 (PBT_APMSUSPEND)" << std::endl;
                    QJsonObject payload;
                    payload["reason"] = "system_sleep";
                    PetEventBus::instance()->emitEvent("system.sleep", payload);
                } else if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) {
                    std::cout << "[WinSystemObserver] 捕获到系统唤醒 (PBT_APMRESUME)" << std::endl;
                    QJsonObject payload;
                    payload["reason"] = "system_wake";
                    PetEventBus::instance()->emitEvent("system.wake", payload);
                }
            }
        }
        return false;
    }
};

static WinPowerNativeFilter *s_powerFilter = nullptr;

static QString getProcessNameFromHwnd(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return "";

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return "";

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return "";

    wchar_t exePath[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, exePath, &size)) {
        CloseHandle(hProc);
        QString fullPath = QString::fromWCharArray(exePath);
        return QFileInfo(fullPath).baseName(); // e.g. "Code", "chrome", "WeChat"
    }

    CloseHandle(hProc);
    return "";
}

static VOID CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                                  LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    Q_UNUSED(hWinEventHook);
    Q_UNUSED(event);
    Q_UNUSED(idObject);
    Q_UNUSED(idChild);
    Q_UNUSED(dwEventThread);
    Q_UNUSED(dwmsEventTime);

    if (hwnd && IsWindow(hwnd)) {
        QString appName = getProcessNameFromHwnd(hwnd);
        if (!appName.isEmpty() && !appName.contains("Shijima", Qt::CaseInsensitive)) {
            // 获取窗口标题
            wchar_t titleBuf[512] = {0};
            GetWindowTextW(hwnd, titleBuf, 512);
            QString title = QString::fromWCharArray(titleBuf);

            std::cout << "[WinSystemObserver] 捕获到前台应用切换: " << appName.toStdString()
                      << " | 窗口标题: " << title.toStdString() << std::endl;

            QJsonObject payload;
            payload["app_name"] = appName;
            payload["bundle_id"] = appName.toLower() + ".exe";
            payload["window_title"] = title;

            if (QCoreApplication::instance()) {
                QString bundleId = appName.toLower() + ".exe";
                QMetaObject::invokeMethod(QCoreApplication::instance(), [appName, bundleId, title, payload]() {
                    SystemObserver::instance()->recordAppActivation(appName, bundleId, title);
                    PetEventBus::instance()->emitEvent("system.app_activated", payload);
                }, Qt::QueuedConnection);
            }
        }
    }
}

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

    // 1. 注册 WinEvent 前台窗口切换监听
    s_winEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        NULL,
        WinEventProc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    // 2. 注册电源/休眠唤醒事件过滤器
    if (!s_powerFilter && QCoreApplication::instance()) {
        s_powerFilter = new WinPowerNativeFilter();
        QCoreApplication::instance()->installNativeEventFilter(s_powerFilter);
    }

    std::cout << "[WinSystemObserver] Windows 系统级前台与电源感知监听器已启动" << std::endl;

    // 3. 检查系统硬盘可用空间
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        double freeGb = static_cast<double>(freeBytesAvailable.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        if (freeGb < 10.0) {
            QJsonObject payload;
            payload["free_gb"] = freeGb;
            PetEventBus::instance()->emitEvent("system.disk_low", payload);
        }
    }

    // 4. 检查系统内存压力
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        if (memInfo.dwMemoryLoad >= 90) {
            QJsonObject payload;
            payload["pressure_level"] = "critical";
            PetEventBus::instance()->emitEvent("system.memory_pressure", payload);
        }
    }
}

void SystemObserver::stop() {
    if (!m_started) return;
    m_started = false;

    if (s_winEventHook) {
        UnhookWinEvent(s_winEventHook);
        s_winEventHook = NULL;
    }

    if (s_powerFilter && QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(s_powerFilter);
        delete s_powerFilter;
        s_powerFilter = nullptr;
    }
}

QString SystemObserver::currentActiveAppName() const {
    if (!m_activeAppName.isEmpty()) return m_activeAppName;
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        return getProcessNameFromHwnd(hwnd);
    }
    return "";
}

QString SystemObserver::currentActiveWindowTitle() const {
    return m_activeWindowTitle;
}

QString SystemObserver::currentActiveBundleId() const {
    return m_activeBundleId;
}

#include "SensorManager.hpp"

QJsonObject SystemObserver::currentSemanticActivity() const {
    return SensorManager::instance()->currentContext();
}

void SystemObserver::recordAppActivation(const QString &appName, const QString &bundleId, const QString &windowTitle) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_activeAppName = appName;
    m_activeBundleId = bundleId;
    m_activeWindowTitle = windowTitle;
    m_appStartTime = now;

    SensorManager::instance()->onAppActivated(appName, bundleId, windowTitle);

    bool isWorkApp = appName.contains("Code", Qt::CaseInsensitive) ||
                     appName.contains("devenv", Qt::CaseInsensitive) ||
                     appName.contains("cmd", Qt::CaseInsensitive) ||
                     appName.contains("powershell", Qt::CaseInsensitive) ||
                     appName.contains("WindowsTerminal", Qt::CaseInsensitive) ||
                     appName.contains("idea", Qt::CaseInsensitive) ||
                     appName.contains("clion", Qt::CaseInsensitive) ||
                     appName.contains("pycharm", Qt::CaseInsensitive) ||
                     appName.contains("antigravity", Qt::CaseInsensitive);

    if (isWorkApp) {
        if (m_workSessionStartTime == 0) {
            m_workSessionStartTime = now;
        }
    } else {
        if (m_appStartTime - now > 600000) {
            m_workSessionStartTime = 0;
        }
    }
}

int SystemObserver::continuousWorkMinutes() const {
    if (m_workSessionStartTime == 0) return 0;
    qint64 diffMs = QDateTime::currentMSecsSinceEpoch() - m_workSessionStartTime;
    return static_cast<int>(diffMs / 60000);
}
