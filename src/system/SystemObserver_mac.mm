#include "SystemObserver.hpp"
#include "PetEventBus.hpp"
#include "SensorManager.hpp"
#include <QJsonObject>
#include <QDateTime>
#include <QCoreApplication>
#include <iostream>
#include <sys/statvfs.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Vision/Vision.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <dispatch/dispatch.h>
#import <mach/mach.h>
#import <mach/mach_host.h>
#import <sys/sysctl.h>
#import <unistd.h>

@interface MacSystemListener : NSObject
@property (nonatomic, strong) id appActivateObserver;
@property (nonatomic, strong) id sleepObserver;
@property (nonatomic, strong) id wakeObserver;
@property (nonatomic, strong) id screenSleepObserver;
@property (nonatomic, strong) id screenWakeObserver;
@property (nonatomic) dispatch_source_t memoryPressureSource;
@property (nonatomic) dispatch_source_t periodicResourceTimer;
@property (nonatomic, copy) dispatch_block_t ocrDebounceBlock;
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
        if (!app) return;

        NSString *nsName = app.localizedName ?: @"Unknown";
        NSString *nsBundle = app.bundleIdentifier ?: @"";
        QString appName = QString::fromNSString(nsName);
        QString bundleId = QString::fromNSString(nsBundle);
        QString windowTitle = "";
        QString documentPath = "";
        pid_t pid = app.processIdentifier;

        // 第一层感知: Accessibility (AXUIElement) 零开销毫秒级读取真实标题与打开文件路径
        AXUIElementRef axApp = AXUIElementCreateApplication(pid);
        if (axApp) {
            AXUIElementRef axWindow = NULL;
            if (AXUIElementCopyAttributeValue(axApp, kAXFocusedWindowAttribute, (CFTypeRef*)&axWindow) == kAXErrorSuccess && axWindow) {
                // 提取窗口真实标题
                CFTypeRef titleRef = NULL;
                if (AXUIElementCopyAttributeValue(axWindow, kAXTitleAttribute, &titleRef) == kAXErrorSuccess && titleRef) {
                    if (CFGetTypeID(titleRef) == CFStringGetTypeID()) {
                        windowTitle = QString::fromCFString((CFStringRef)titleRef);
                    }
                    CFRelease(titleRef);
                }
                // 提取文档 URL / 文件绝对路径 (Cursor, VSCode, Xcode, TextEdit, Pages 标配)
                CFTypeRef docRef = NULL;
                if (AXUIElementCopyAttributeValue(axWindow, kAXDocumentAttribute, &docRef) == kAXErrorSuccess && docRef) {
                    if (CFGetTypeID(docRef) == CFStringGetTypeID()) {
                        NSString *docStr = (__bridge NSString*)docRef;
                        if ([docStr hasPrefix:@"file://"]) {
                            NSURL *url = [NSURL URLWithString:docStr];
                            if (url && url.path) {
                                documentPath = QString::fromNSString(url.path);
                            }
                        } else {
                            documentPath = QString::fromNSString(docStr);
                        }
                    }
                    CFRelease(docRef);
                }
                // 提取划选选中文本 (若用户当前划选了文本或报错信息，免录屏权限瞬时感知)
                CFTypeRef selRef = NULL;
                if (AXUIElementCopyAttributeValue(axWindow, kAXSelectedTextAttribute, &selRef) == kAXErrorSuccess && selRef) {
                    if (CFGetTypeID(selRef) == CFStringGetTypeID()) {
                        NSString *selStr = (__bridge NSString*)selRef;
                        NSString *trimmed = [selStr stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                        if (trimmed.length > 0 && trimmed.length < 200) {
                            QString qSel = QString::fromNSString(trimmed);
                            SystemObserver::instance()->updateContentSummary(qSel);
                        }
                    }
                    CFRelease(selRef);
                }
                CFRelease(axWindow);
            }
            CFRelease(axApp);
        }

        // 第二层回退: 若 AX 未拿到标题，从 CGWindowList 提取
        if (windowTitle.isEmpty()) {
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
        }

        std::cout << "[SystemObserver] 捕获到应用切换: " << appName.toStdString() 
                  << " (" << bundleId.toStdString() << ")"
                  << (windowTitle.isEmpty() ? "" : (" | 标题: " + windowTitle.toStdString()))
                  << (documentPath.isEmpty() ? "" : (" | 文档: " + documentPath.toStdString()))
                  << std::endl;

        QJsonObject payload;
        payload["app_name"] = appName;
        payload["bundle_id"] = bundleId;
        payload["window_title"] = windowTitle;
        payload["document_path"] = documentPath;

        SystemObserver::instance()->recordAppActivation(appName, bundleId, windowTitle, documentPath);
        PetEventBus::instance()->emitEvent("system.app_activated", payload);

        // 取消上一轮未执行的 OCR 防抖 Block
        if (self.ocrDebounceBlock) {
            dispatch_block_cancel(self.ocrDebounceBlock);
            self.ocrDebounceBlock = nil;
        }

        // 桌宠自身或未知进程不进行后台截图 OCR
        if (nsName.length == 0 || [nsName isEqualToString:@"guyi-bot"] || [nsBundle isEqualToString:@"com.guyi.pet"]) {
            return;
        }

        // 零打扰权限门禁：必须静默前置探测录屏权限，未授权时绝对严禁调用 screencapture，杜绝 macOS 系统弹窗骚扰
        if (!CGPreflightScreenCaptureAccess()) {
            return;
        }

        // 第三层感知: 1.5 秒防抖延时触发原生低开销 Vision OCR
        pid_t targetPid = pid;
        NSString *targetAppName = [nsName copy];
        NSString *targetBundleId = [nsBundle copy];

        dispatch_block_t taskBlock = dispatch_block_create(DISPATCH_BLOCK_INHERIT_QOS_CLASS, ^{
            @autoreleasepool {
                // 静默前置门禁：未显式授予录屏权限时绝对严禁执行截屏，杜绝系统弹窗
                if (!CGPreflightScreenCaptureAccess()) {
                    return;
                }

                // 确认该应用依然是当前前台应用
                NSRunningApplication *front = [[NSWorkspace sharedWorkspace] frontmostApplication];
                if (!front || front.processIdentifier != targetPid) {
                    return;
                }

                // 使用 ScreenCaptureKit 进程内安全捕获，完全规避命令行子进程重复弹窗
                [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *scError) {
                    if (scError || !content) return;

                    SCWindow *targetWindow = nil;
                    for (SCWindow *win in content.windows) {
                        if (win.owningApplication && win.owningApplication.processID == targetPid) {
                            if (win.frame.size.width > 200 && win.frame.size.height > 150) {
                                targetWindow = win;
                                break;
                            }
                        }
                    }

                    if (!targetWindow) return;

                    SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:targetWindow];
                    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
                    config.showsCursor = NO;
                    CGFloat maxDim = 1280.0;
                    CGFloat w = targetWindow.frame.size.width;
                    CGFloat h = targetWindow.frame.size.height;
                    if (w > maxDim) {
                        h = (h / w) * maxDim;
                        w = maxDim;
                    }
                    config.width = (size_t)w;
                    config.height = (size_t)h;

                    [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef cgImage, NSError *imgError) {
                        if (imgError || !cgImage) return;

                        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                            @autoreleasepool {
                                VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
                                request.recognitionLevel = VNRequestTextRecognitionLevelFast;
                                request.usesLanguageCorrection = YES;
                                request.recognitionLanguages = @[@"zh-Hans", @"zh-Hant", @"en-US"];

                                VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
                                NSError *err = nil;
                                [handler performRequests:@[request] error:&err];

                                if (!err && request.results.count > 0) {
                                    NSMutableArray<NSString*> *lines = [NSMutableArray array];
                                    for (VNRecognizedTextObservation *obs in request.results) {
                                        VNRecognizedText *topCand = [obs topCandidates:1].firstObject;
                                        if (topCand && topCand.string.length > 1) {
                                            NSString *trimmed = [topCand.string stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                                            if (trimmed.length > 1) {
                                                [lines addObject:trimmed];
                                                if (lines.count >= 12) break;
                                            }
                                        }
                                    }

                                    if (lines.count > 0) {
                                        NSString *summaryStr = [lines componentsJoinedByString:@" | "];
                                        QString qSummary = QString::fromNSString(summaryStr);
                                        QString qAppName = QString::fromNSString(targetAppName);
                                        QString qBundleId = QString::fromNSString(targetBundleId);

                                        dispatch_async(dispatch_get_main_queue(), ^{
                                            SystemObserver::instance()->updateContentSummary(qSummary);
                                            SensorManager::instance()->updateOcrSummary(qAppName, qBundleId, qSummary);

                                            QJsonObject ocrPayload;
                                            ocrPayload["app_name"] = qAppName;
                                            ocrPayload["bundle_id"] = qBundleId;
                                            ocrPayload["content_summary"] = qSummary;
                                            PetEventBus::instance()->emitEvent("system.app_ocr_updated", ocrPayload);
                                        });
                                    }
                                }
                            }
                        });
                    }];
                }];
            }
        });

        self.ocrDebounceBlock = taskBlock;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1500 * NSEC_PER_MSEC)), dispatch_get_main_queue(), taskBlock);
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

            SystemResourceStats stats = SystemObserver::instance()->getSystemResourceStats();
            QJsonObject payload;
            payload["pressure_level"] = levelStr;
            payload["cpu_percent"] = stats.cpuPercent;
            payload["mem_percent"] = stats.memPercent;
            payload["disk_percent"] = stats.diskPercent;
            payload["summary"] = stats.formattedSummary();
            PetEventBus::instance()->emitEvent("system.memory_pressure", payload);
        });
        dispatch_resume(self.memoryPressureSource);
    }

    // 5. 周期性高负荷巡检 (每 60 秒检查一次，若内存 >= 86% 或 CPU >= 92%，带 5 分钟冷却告警)
    self.periodicResourceTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    if (self.periodicResourceTimer) {
        dispatch_source_set_timer(self.periodicResourceTimer, dispatch_time(DISPATCH_TIME_NOW, 60 * NSEC_PER_SEC), 60 * NSEC_PER_SEC, 5 * NSEC_PER_SEC);
        static qint64 s_lastAlertTime = 0;
        dispatch_source_set_event_handler(self.periodicResourceTimer, ^{
            SystemResourceStats stats = SystemObserver::instance()->getSystemResourceStats();
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if ((stats.memPercent >= 86.0 || stats.cpuPercent >= 92.0) && (now - s_lastAlertTime > 300000)) {
                s_lastAlertTime = now;
                std::cout << "[SystemObserver] 检测到系统高负载: 内存=" << stats.memPercent << "%, CPU=" << stats.cpuPercent << "%" << std::endl;
                QJsonObject payload;
                payload["pressure_level"] = "warning";
                payload["cpu_percent"] = stats.cpuPercent;
                payload["mem_percent"] = stats.memPercent;
                payload["disk_percent"] = stats.diskPercent;
                payload["summary"] = stats.formattedSummary();
                PetEventBus::instance()->emitEvent("system.memory_pressure", payload);
            }
        });
        dispatch_resume(self.periodicResourceTimer);
    }
}

- (void)stopListening {
    NSNotificationCenter *wsCenter = [[NSWorkspace sharedWorkspace] notificationCenter];
    NSNotificationCenter *defCenter = [NSNotificationCenter defaultCenter];

    if (self.ocrDebounceBlock) {
        dispatch_block_cancel(self.ocrDebounceBlock);
        self.ocrDebounceBlock = nil;
    }
    if (self.periodicResourceTimer) {
        dispatch_source_cancel(self.periodicResourceTimer);
        self.periodicResourceTimer = nil;
    }
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

    std::cout << "[SystemObserver] macOS 系统级事件观察器已启动 (已启用 AXUIElement 与 Vision OCR 深度感知)" << std::endl;

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

QString SystemObserver::currentActiveBundleId() const
{
    return m_activeBundleId;
}

QString SystemObserver::currentActiveDocumentPath() const
{
    return m_activeDocumentPath;
}

QString SystemObserver::currentActiveContentSummary() const
{
    return m_activeContentSummary;
}

QJsonObject SystemObserver::currentSemanticActivity() const
{
    return SensorManager::instance()->currentContext();
}

void SystemObserver::updateContentSummary(const QString &contentSummary)
{
    m_activeContentSummary = contentSummary;
}

void SystemObserver::recordAppActivation(const QString &appName, const QString &bundleId, const QString &windowTitle, const QString &documentPath)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_activeAppName = appName;
    m_activeBundleId = bundleId;
    m_activeWindowTitle = windowTitle;
    m_activeDocumentPath = documentPath;
    m_activeContentSummary.clear(); // 切换应用时清空上一应用的 OCR 摘要
    m_appStartTime = now;

    // 触发自进化应用探针进行深度语义感知
    SensorManager::instance()->onAppActivated(appName, bundleId, windowTitle, documentPath);

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

SystemResourceStats SystemObserver::getSystemResourceStats()
{
    SystemResourceStats stats;

    // 1. 内存统计 (Mach host statistics & sysctl)
    mach_port_t hostPort = mach_host_self();
    vm_size_t pageSize = getpagesize();

    int mib[2] = { CTL_HW, HW_MEMSIZE };
    uint64_t totalMemBytes = 0;
    size_t len = sizeof(totalMemBytes);
    if (sysctl(mib, 2, &totalMemBytes, &len, NULL, 0) == 0 && totalMemBytes > 0) {
        stats.memTotalGb = static_cast<double>(totalMemBytes) / (1024.0 * 1024.0 * 1024.0);
    } else {
        stats.memTotalGb = 16.0;
    }

    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t count = sizeof(vmStats) / sizeof(natural_t);
    if (host_statistics64(hostPort, HOST_VM_INFO64, (host_info64_t)&vmStats, &count) == KERN_SUCCESS) {
        uint64_t active = (uint64_t)vmStats.active_count * pageSize;
        uint64_t wired = (uint64_t)vmStats.wire_count * pageSize;
        uint64_t compressed = (uint64_t)vmStats.compressor_page_count * pageSize;
        uint64_t usedBytes = active + wired + compressed;
        stats.memUsedGb = static_cast<double>(usedBytes) / (1024.0 * 1024.0 * 1024.0);
        if (totalMemBytes > 0) {
            stats.memPercent = (static_cast<double>(usedBytes) / static_cast<double>(totalMemBytes)) * 100.0;
        }
    }

    // 2. CPU 统计 (Mach host_cpu_load_info)
    static host_cpu_load_info_data_t s_prevCpuInfo;
    static bool s_hasPrevCpu = false;
    host_cpu_load_info_data_t cpuInfo;
    mach_msg_type_number_t cpuCount = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(hostPort, HOST_CPU_LOAD_INFO, (host_info_t)&cpuInfo, &cpuCount) == KERN_SUCCESS) {
        if (s_hasPrevCpu) {
            uint64_t user = cpuInfo.cpu_ticks[CPU_STATE_USER] - s_prevCpuInfo.cpu_ticks[CPU_STATE_USER];
            uint64_t system = cpuInfo.cpu_ticks[CPU_STATE_SYSTEM] - s_prevCpuInfo.cpu_ticks[CPU_STATE_SYSTEM];
            uint64_t nice = cpuInfo.cpu_ticks[CPU_STATE_NICE] - s_prevCpuInfo.cpu_ticks[CPU_STATE_NICE];
            uint64_t idle = cpuInfo.cpu_ticks[CPU_STATE_IDLE] - s_prevCpuInfo.cpu_ticks[CPU_STATE_IDLE];
            uint64_t total = user + system + nice + idle;
            if (total > 0) {
                stats.cpuPercent = (static_cast<double>(user + system + nice) / static_cast<double>(total)) * 100.0;
            }
        } else {
            double load[3];
            if (getloadavg(load, 3) != -1) {
                stats.cpuPercent = std::min(100.0, load[0] * 12.5);
            }
        }
        s_prevCpuInfo = cpuInfo;
        s_hasPrevCpu = true;
    }

    // 3. 根分区硬盘统计 (statvfs)
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        uint64_t totalDiskBytes = (uint64_t)stat.f_blocks * stat.f_frsize;
        uint64_t freeDiskBytes = (uint64_t)stat.f_bavail * stat.f_frsize;
        stats.diskTotalGb = static_cast<double>(totalDiskBytes) / (1024.0 * 1024.0 * 1024.0);
        stats.diskFreeGb = static_cast<double>(freeDiskBytes) / (1024.0 * 1024.0 * 1024.0);
        if (totalDiskBytes > 0) {
            uint64_t usedDiskBytes = totalDiskBytes - freeDiskBytes;
            stats.diskPercent = (static_cast<double>(usedDiskBytes) / static_cast<double>(totalDiskBytes)) * 100.0;
        }
    }

    return stats;
}
