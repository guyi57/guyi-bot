#include "SystemObserver.hpp"
#include "PetEventBus.hpp"
#include <QJsonObject>
#include <QDateTime>
#include <QCoreApplication>
#include <iostream>
#include <sys/statvfs.h>
#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>

@interface MacSystemListener : NSObject
@property (nonatomic, strong) id appActivateObserver;
@property (nonatomic, strong) id sleepObserver;
@property (nonatomic, strong) id wakeObserver;
@property (nonatomic, strong) id screenSleepObserver;
@property (nonatomic, strong) id screenWakeObserver;
@property (nonatomic) dispatch_source_t memoryPressureSource;
@end

@implementation MacSystemListener

- (void)startListening {
    NSNotificationCenter *wsCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    NSNotificationCenter *defCenter = [NSNotificationCenter defaultCenter];

    // 1. 前台应用切换监听 (严格绑定主线程 mainQueue，避免多线程并发破坏堆内存)
    self.appActivateObserver = [wsCenter addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                                     object:nil
                                                      queue:[NSOperationQueue mainQueue]
                                                 usingBlock:^(NSNotification *note) {
        NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
        if (app) {
            NSString *nsName = app.localizedName ?: @"Unknown";
            NSString *nsBundle = app.bundleIdentifier ?: @"";
            QString appName = QString::fromNSString(nsName);
            QString bundleId = QString::fromNSString(nsBundle);
            QString windowTitle = "";

            // 获取前台窗口标题 (零轮询瞬时提取并严格校验 CoreFoundation 类型安全)
            pid_t pid = app.processIdentifier;
            CFArrayRef windowList = CGWindowListCopyWindowInfo(
                kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                kCGNullWindowID
            );
            if (windowList) {
                CFIndex count = CFArrayGetCount(windowList);
                for (CFIndex i = 0; i < count; ++i) {
                    CFDictionaryRef info = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);
                    if (!info || CFGetTypeID(info) != CFDictionaryGetTypeID()) continue;
                    CFNumberRef pidRef = (CFNumberRef)CFDictionaryGetValue(info, kCGWindowOwnerPID);
                    int wPid = 0;
                    if (pidRef && CFGetTypeID(pidRef) == CFNumberGetTypeID() &&
                        CFNumberGetValue(pidRef, kCFNumberIntType, &wPid) && (pid_t)wPid == pid) {
                        CFStringRef titleRef = (CFStringRef)CFDictionaryGetValue(info, kCGWindowName);
                        if (titleRef && CFGetTypeID(titleRef) == CFStringGetTypeID() && CFStringGetLength(titleRef) > 0) {
                            windowTitle = QString::fromCFString(titleRef);
                            break;
                        }
                    }
                }
                CFRelease(windowList);
            }

            std::cout << "[SystemObserver] 捕获到应用切换: " << appName.toStdString() 
                      << " (" << bundleId.toStdString() << ")"
                      << (windowTitle.isEmpty() ? "" : (" | 标题: " + windowTitle.toStdString()))
                      << std::endl;

            QJsonObject payload;
            payload["app_name"] = appName;
            payload["bundle_id"] = bundleId;
            payload["window_title"] = windowTitle;

            SystemObserver::instance()->recordAppActivation(appName, bundleId, windowTitle);
            PetEventBus::instance()->emitEvent("system.app_activated", payload);
        }
    }];

    // 2. 系统休眠与唤醒 (严格调度在主线程)
    self.sleepObserver = [wsCenter addObserverForName:NSWorkspaceWillSleepNotification
                                               object:nil
                                                queue:[NSOperationQueue mainQueue]
                                           usingBlock:^(NSNotification *) {
        std::cout << "[SystemObserver] 捕获到系统休眠通知" << std::endl;
        QJsonObject payload;
        payload["reason"] = "system_sleep";
        PetEventBus::instance()->emitEvent("system.sleep", payload);
    }];

    self.wakeObserver = [wsCenter addObserverForName:NSWorkspaceDidWakeNotification
                                              object:nil
                                               queue:[NSOperationQueue mainQueue]
                                          usingBlock:^(NSNotification *) {
        std::cout << "[SystemObserver] 捕获到系统唤醒通知" << std::endl;
        QJsonObject payload;
        payload["reason"] = "system_wake";
        PetEventBus::instance()->emitEvent("system.wake", payload);
    }];

    // 3. 屏幕熄灭与亮屏 (注册到 defaultCenter，严格调度在主线程)
    self.screenSleepObserver = [defCenter addObserverForName:NSWorkspaceScreensDidSleepNotification
                                                      object:nil
                                                       queue:[NSOperationQueue mainQueue]
                                                  usingBlock:^(NSNotification *) {
        std::cout << "[SystemObserver] 捕获到屏幕熄灭" << std::endl;
        QJsonObject payload;
        payload["reason"] = "screen_sleep";
        PetEventBus::instance()->emitEvent("system.sleep", payload);
    }];

    self.screenWakeObserver = [defCenter addObserverForName:NSWorkspaceScreensDidWakeNotification
                                                     object:nil
                                                      queue:[NSOperationQueue mainQueue]
                                                 usingBlock:^(NSNotification *) {
        std::cout << "[SystemObserver] 捕获到屏幕点亮" << std::endl;
        QJsonObject payload;
        payload["reason"] = "screen_wake";
        PetEventBus::instance()->emitEvent("system.wake", payload);
    }];

    // 4. 系统内存压力内核通知 (绑定主线程队列 dispatch_get_main_queue)
    unsigned long mask = DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL;
    self.memoryPressureSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0, mask, dispatch_get_main_queue());
    if (self.memoryPressureSource) {
        dispatch_source_set_event_handler(self.memoryPressureSource, ^{
            unsigned long pressureLevel = dispatch_source_get_data(self.memoryPressureSource);
            QString levelStr = (pressureLevel & DISPATCH_MEMORYPRESSURE_CRITICAL) ? "critical" : "warning";
            std::cout << "[SystemObserver] 捕获到内核内存压力告警: " << levelStr.toStdString() << std::endl;

            QJsonObject payload;
            payload["pressure_level"] = levelStr;
            PetEventBus::instance()->emitEvent("system.memory_pressure", payload);
        });
        dispatch_resume(self.memoryPressureSource);
    }
}

- (void)stopListening {
    NSNotificationCenter *wsCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    NSNotificationCenter *defCenter = [NSNotificationCenter defaultCenter];

    if (self.appActivateObserver) {
        [wsCenter removeObserver:self.appActivateObserver];
        self.appActivateObserver = nil;
    }
    if (self.sleepObserver) {
        [wsCenter removeObserver:self.sleepObserver];
        self.sleepObserver = nil;
    }
    if (self.wakeObserver) {
        [wsCenter removeObserver:self.wakeObserver];
        self.wakeObserver = nil;
    }
    if (self.screenSleepObserver) {
        [defCenter removeObserver:self.screenSleepObserver];
        self.screenSleepObserver = nil;
    }
    if (self.screenWakeObserver) {
        [defCenter removeObserver:self.screenWakeObserver];
        self.screenWakeObserver = nil;
    }
    if (self.memoryPressureSource) {
        dispatch_source_cancel(self.memoryPressureSource);
        self.memoryPressureSource = nil;
    }
}

@end

SystemObserver::SystemObserver()
{
}

SystemObserver::~SystemObserver()
{
    stop();
}

SystemObserver* SystemObserver::instance()
{
    static SystemObserver s_instance;
    return &s_instance;
}

void SystemObserver::start()
{
    if (m_started) return;
    m_started = true;

    MacSystemListener *listener = [[MacSystemListener alloc] init];
    [listener startListening];
    m_observerContext = (__bridge_retained void*)listener;

    std::cout << "[SystemObserver] macOS 系统级事件观察器已启动" << std::endl;

    // 低频检查硬盘（启动时检查一次，耗时 0.001ms）
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        double freeGb = (double)(stat.f_bavail * stat.f_frsize) / (1024.0 * 1024.0 * 1024.0);
        if (freeGb < 10.0) {
            QJsonObject payload;
            payload["free_gb"] = freeGb;
            PetEventBus::instance()->emitEvent("system.disk_low", payload);
        }
    }
}

void SystemObserver::stop()
{
    if (!m_started) return;
    m_started = false;

    if (m_observerContext) {
        MacSystemListener *listener = (__bridge_transfer MacSystemListener*)m_observerContext;
        [listener stopListening];
        m_observerContext = nullptr;
    }
}

QString SystemObserver::currentActiveAppName() const
{
    if (!m_activeAppName.isEmpty()) return m_activeAppName;
    @autoreleasepool {
        NSRunningApplication *app = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (app && app.localizedName) {
            return QString::fromNSString(app.localizedName);
        }
    }
    return "";
}

QString SystemObserver::currentActiveWindowTitle() const
{
    return m_activeWindowTitle;
}

#include "SensorManager.hpp"

QString SystemObserver::currentActiveBundleId() const
{
    return m_activeBundleId;
}

QJsonObject SystemObserver::currentSemanticActivity() const
{
    return SensorManager::instance()->currentContext();
}

void SystemObserver::recordAppActivation(const QString &appName, const QString &bundleId, const QString &windowTitle)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_activeAppName = appName;
    m_activeBundleId = bundleId;
    m_activeWindowTitle = windowTitle;
    m_appStartTime = now;

    // 触发自进化应用探针进行深度语义感知
    SensorManager::instance()->onAppActivated(appName, bundleId, windowTitle);

    // 统计工作类软件的连续专注时长
    bool isWorkApp = appName.contains("Cursor", Qt::CaseInsensitive) ||
                     appName.contains("Code", Qt::CaseInsensitive) ||
                     appName.contains("Xcode", Qt::CaseInsensitive) ||
                     appName.contains("Terminal", Qt::CaseInsensitive) ||
                     appName.contains("iTerm", Qt::CaseInsensitive) ||
                     appName.contains("IDEA", Qt::CaseInsensitive) ||
                     appName.contains("CLion", Qt::CaseInsensitive) ||
                     appName.contains("PyCharm", Qt::CaseInsensitive) ||
                     appName.contains("WebStorm", Qt::CaseInsensitive) ||
                     appName.contains("Antigravity", Qt::CaseInsensitive);

    if (isWorkApp) {
        if (m_workSessionStartTime == 0) {
            m_workSessionStartTime = now;
        }
    } else {
        // 如果切到了非工作应用超过 10 分钟，才重置专注时长
        if (m_appStartTime - now > 600000) {
            m_workSessionStartTime = 0;
        }
    }
}

int SystemObserver::continuousWorkMinutes() const
{
    if (m_workSessionStartTime == 0) return 0;
    qint64 diffMs = QDateTime::currentMSecsSinceEpoch() - m_workSessionStartTime;
    return static_cast<int>(diffMs / 60000);
}
