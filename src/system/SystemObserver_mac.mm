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
- (void)startListening;
- (void)stopListening;
- (void)triggerImmediateOcr;
- (void)triggerImmediateOcrWithCallback:(void(^)(NSString *appName, NSString *windowTitle, NSString *ocrText))callback;
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
        if (nsName.length == 0 || [nsName isEqualToString:@"guyi-bot"] || [nsBundle isEqualToString:@"com.guyi.guyibot"] || [nsBundle isEqualToString:@"com.guyi.pet"]) {
            return;
        }

        // 仅在已确认授权或系统预检通过时才进行后台静默 OCR，绝不在未授权时触发系统弹窗
        if (!s_hasConfirmedCaptureAccess && !CGPreflightScreenCaptureAccess()) {
            return;
        }

        // 降低后台自动 OCR 触发频率：
        // 1. 同一应用 3 分钟内无需重复自动截屏
        // 2. 跨应用切换至少间隔 45 秒
        // 3. 防抖延时由 800ms 提高到 3000ms（避免切窗口时光速触发）
        static qint64 s_lastAutoOcrTime = 0;
        static NSString *s_lastOcrBundle = @"";
        qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        bool sameApp = [s_lastOcrBundle isEqualToString:nsBundle];
        if (sameApp && (nowMs - s_lastAutoOcrTime < 180000)) {
            return;
        }
        if (!sameApp && (nowMs - s_lastAutoOcrTime < 45000)) {
            return;
        }

        // 第三层感知: 3000ms 防抖延时触发原生低开销 Vision OCR
        __weak MacSystemListener *weakSelf = self;
        dispatch_block_t taskBlock = dispatch_block_create(DISPATCH_BLOCK_INHERIT_QOS_CLASS, ^{
            s_lastAutoOcrTime = QDateTime::currentMSecsSinceEpoch();
            s_lastOcrBundle = [nsBundle copy];
            [weakSelf triggerImmediateOcr];
        });

        self.ocrDebounceBlock = taskBlock;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3000 * NSEC_PER_MSEC)), dispatch_get_main_queue(), taskBlock);
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

static bool s_hasConfirmedCaptureAccess = false;

- (void)triggerImmediateOcr {
    [self triggerImmediateOcrWithCallback:nil];
}

- (void)triggerImmediateOcrWithCallback:(void(^)(NSString *appName, NSString *windowTitle, NSString *ocrText))callback {
    if (!s_hasConfirmedCaptureAccess && !CGPreflightScreenCaptureAccess()) {
        NSLog(@"[SystemObserver OCR] 尚未激活屏幕录制权限，跳过主动截屏以避免触发系统提示");
        if (callback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                callback(@"", @"", @"");
            });
        }
        return;
    }
    NSRunningApplication *front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    __block pid_t targetPid = front ? front.processIdentifier : 0;
    __block NSString *targetAppName = front ? [front.localizedName copy] : @"";
    __block NSString *targetBundleId = front ? [front.bundleIdentifier copy] : @"";

    [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES completionHandler:^(SCShareableContent *content, NSError *scError) {
        if (scError || !content) {
            NSLog(@"[SystemObserver OCR] SCShareableContent 错误: %@", scError);
            if (callback) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    callback(targetAppName, @"", @"");
                });
            }
            return;
        }

        s_hasConfirmedCaptureAccess = true;

        SCWindow *targetWindow = nil;
        // 1. 优先匹配当前活跃视窗（非桌宠自身）
        if (targetPid > 0 && ![targetAppName isEqualToString:@"guyi-bot"] && ![targetBundleId isEqualToString:@"com.guyi.guyibot"]) {
            for (SCWindow *win in content.windows) {
                if (win.owningApplication) {
                    BOOL pidMatch = (win.owningApplication.processID == targetPid);
                    BOOL bundleMatch = (targetBundleId.length > 0 && [win.owningApplication.bundleIdentifier isEqualToString:targetBundleId]);
                    BOOL nameMatch = (targetAppName.length > 0 && [win.owningApplication.applicationName isEqualToString:targetAppName]);
                    if ((pidMatch || bundleMatch || nameMatch) && win.frame.size.width > 200 && win.frame.size.height > 150) {
                        targetWindow = win;
                        break;
                    }
                }
            }
        }

        // 2. 若当前前台是桌宠自身或未能精准匹配，自动回退捕获最上层的外部有效工作视窗
        if (!targetWindow) {
            for (SCWindow *win in content.windows) {
                if (win.owningApplication && win.frame.size.width > 300 && win.frame.size.height > 200) {
                    NSString *bId = win.owningApplication.bundleIdentifier ?: @"";
                    NSString *aName = win.owningApplication.applicationName ?: @"";
                    if ([bId isEqualToString:@"com.guyi.guyibot"] || [aName isEqualToString:@"guyi-bot"]) continue;
                    if ([aName isEqualToString:@"Window Server"] || [aName isEqualToString:@"Dock"] || [aName isEqualToString:@"控制中心"] || [aName isEqualToString:@"ControlCenter"]) continue;
                    if ([aName isEqualToString:@"系统设置"] || [aName isEqualToString:@"System Settings"] || [aName isEqualToString:@"System Preferences"]) continue;
                    if ([bId isEqualToString:@"com.apple.systempreferences"] || [bId isEqualToString:@"com.apple.SecurityAgent"] || [aName isEqualToString:@"universalAccessAuthWarn"]) continue;
                    targetWindow = win;
                    targetAppName = aName;
                    targetBundleId = bId;
                    break;
                }
            }
        }

        if (!targetWindow) {
            if (callback) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    callback(targetAppName, @"", @"");
                });
            }
            return;
        }

        __block NSString *windowTitle = targetWindow ? [targetWindow.title copy] : @"";

        SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:targetWindow];
        SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
        config.showsCursor = NO;
        CGFloat maxDim = 1280.0;
        CGFloat w = targetWindow.frame.size.width;
        CGFloat h = targetWindow.frame.size.height;
        if (w < 80.0 || h < 80.0) {
            if (callback) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    callback(targetAppName, windowTitle, @"");
                });
            }
            return;
        }
        if (w > maxDim) {
            h = (h / w) * maxDim;
            w = maxDim;
        }
        config.width = (size_t)w;
        config.height = (size_t)h;

        [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:^(CGImageRef cgImage, NSError *imgError) {
            if (imgError || !cgImage) {
                if (imgError) {
                    NSLog(@"[SystemObserver OCR] captureImageWithFilter 错误: %@", imgError);
                }
                if (callback) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        callback(targetAppName, windowTitle, @"");
                    });
                }
                return;
            }

            // 关键修复: cgImage 为 CoreGraphics C 结构，ARC 不托管生命周期。
            // completionHandler 执行完毕后会被系统释放，跨线程必须显式 CGImageRetain 保护，用毕在 finally 中 CGImageRelease，彻底杜绝 UAF 闪退。
            CGImageRetain(cgImage);

            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                @autoreleasepool {
                    @try {
                        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
                        // 采用高精度神经网络 OCR 引擎（全面支持中英双语、上下文语言纠偏），彻底解决 Fast 模式将中文及 UI 噪点识别为乱码符号的问题
                        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
                        request.usesLanguageCorrection = YES;
                        request.recognitionLanguages = @[@"zh-Hans", @"zh-Hant", @"en-US"];
                        request.minimumTextHeight = 0.012; // 过滤小于 15px 的微小噪点、边框与分割线

                        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
                        NSError *err = nil;
                        [handler performRequests:@[request] error:&err];

                        if (!err && request.results.count > 0) {
                            s_hasConfirmedCaptureAccess = true;
                            NSMutableArray<NSString*> *lines = [NSMutableArray array];
                            NSCharacterSet *noiseChars = [NSCharacterSet characterSetWithCharactersInString:@" \t\r\n•*-_|~#+=/\\:;.,`'\"()[]{}<>^·…"];
                            NSRegularExpression *validContentRe = [NSRegularExpression regularExpressionWithPattern:@"[\\p{Han}]{1,}|[a-zA-Z]{3,}" options:0 error:nil];

                            for (VNRecognizedTextObservation *obs in request.results) {
                                VNRecognizedText *topCand = [obs topCandidates:1].firstObject;
                                if (!topCand || topCand.confidence < 0.40f) continue;

                                NSString *trimmed = [topCand.string stringByTrimmingCharactersInSet:noiseChars];
                                if (trimmed.length < 2) continue;

                                // 严格校验：必须包含至少 1 个汉字，或至少 1 个长度>=3 的有效英文单词（过滤乱码标点、图标杂质）
                                NSRange range = NSMakeRange(0, trimmed.length);
                                if (![validContentRe firstMatchInString:trimmed options:0 range:range]) {
                                    continue;
                                }

                                // 符号率过滤：若标点符号比例过高则判定为界面碎片
                                NSUInteger letterOrDigitCount = 0;
                                for (NSUInteger i = 0; i < trimmed.length; ++i) {
                                    unichar c = [trimmed characterAtIndex:i];
                                    if ((c >= 0x4e00 && c <= 0x9fa5) || isalnum(c)) {
                                        letterOrDigitCount++;
                                    }
                                }
                                if ((double)letterOrDigitCount / (double)trimmed.length < 0.60) {
                                    continue;
                                }

                                if (![lines containsObject:trimmed]) {
                                    [lines addObject:trimmed];
                                    if (lines.count >= 15) break;
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

                                    if (callback) {
                                        callback(targetAppName, windowTitle, summaryStr);
                                    }
                                });
                            } else if (callback) {
                                dispatch_async(dispatch_get_main_queue(), ^{
                                    callback(targetAppName, windowTitle, @"");
                                });
                            }
                        } else if (callback) {
                            dispatch_async(dispatch_get_main_queue(), ^{
                                callback(targetAppName, windowTitle, @"");
                            });
                        }
                    } @catch (NSException *ex) {
                        NSLog(@"[SystemObserver OCR] Vision OCR 处理异常: %@", ex);
                        if (callback) {
                            dispatch_async(dispatch_get_main_queue(), ^{
                                callback(targetAppName, windowTitle, @"");
                            });
                        }
                    } @finally {
                        CGImageRelease(cgImage);
                    }
                }
            });
        }];
    }];
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

void SystemObserver::triggerOcrSnapshot(std::function<void(const QString &appName, const QString &windowTitle, const QString &ocrText)> callback)
{
    if (m_observerContext) {
        MacSystemListener *listener = (__bridge MacSystemListener*)m_observerContext;
        if (callback) {
            [listener triggerImmediateOcrWithCallback:^(NSString *appName, NSString *windowTitle, NSString *ocrText) {
                QString outApp = QString::fromNSString(appName);
                QString outTitle = QString::fromNSString(windowTitle);
                QString outText = QString::fromNSString(ocrText);
                callback(outApp, outTitle, outText);
            }];
        } else {
            [listener triggerImmediateOcr];
        }
    } else if (callback) {
        callback("", "", "");
    }
}

bool SystemObserver::hasScreenCaptureAccess() const
{
    if (s_hasConfirmedCaptureAccess) {
        return true;
    }
    return CGPreflightScreenCaptureAccess();
}

void SystemObserver::requestScreenCaptureAccess()
{
    CGRequestScreenCaptureAccess();
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
