// 
// Shijima-Qt - Cross-platform shimeji simulation app for desktop
// Copyright (C) 2025 pixelomer
// 

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include "PrivateActiveWindowObserver.hpp"

// Private API
extern "C" AXError _AXUIElementGetWindow(AXUIElementRef, CGWindowID *out);

static BOOL GetDataFromUIElement(AXUIElementRef element, CGRect *outRect,
    pid_t *outPid, CGWindowID *outWindowID)
{
    AXError err;
    BOOL success;
    if (outPid != NULL) {
        err = AXUIElementGetPid(element, outPid);
        if (err != kAXErrorSuccess) return NO;
    }
    if (outRect != NULL) {
        AXValueRef posVal = NULL;
        err = AXUIElementCopyAttributeValue(element, kAXPositionAttribute,
            (CFTypeRef *)&posVal);
        if (err != kAXErrorSuccess || posVal == NULL) return NO;
        success = AXValueGetValue(posVal, (AXValueType)kAXValueCGPointType,
            &outRect->origin);
        CFRelease(posVal);
        if (!success) return NO;

        AXValueRef sizeVal = NULL;
        err = AXUIElementCopyAttributeValue(element, kAXSizeAttribute,
            (CFTypeRef *)&sizeVal);
        if (err != kAXErrorSuccess || sizeVal == NULL) return NO;
        success = AXValueGetValue(sizeVal, (AXValueType)kAXValueCGSizeType,
            &outRect->size);
        CFRelease(sizeVal);
        if (!success) return NO;
    }
    if (outWindowID != NULL) {
        err = _AXUIElementGetWindow(element, outWindowID);
        if (err != kAXErrorSuccess) return NO;
    }
    return YES;
}

static BOOL GetWindowFromCG(CGRect *outRect, pid_t *outPid, CGWindowID *outWindowID) {
    CFArrayRef windowList = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
        kCGNullWindowID
    );
    if (!windowList) return NO;

    pid_t myPid = getpid();
    BOOL found = NO;
    CFIndex count = CFArrayGetCount(windowList);

    // 获取当前鼠标位置（支持拖放吸附）
    NSPoint mouseLoc = [NSEvent mouseLocation];
    NSScreen *primary = [NSScreen screens].firstObject;
    double screenH = primary ? primary.frame.size.height : 1080.0;
    CGPoint mouseTopLeft = CGPointMake(mouseLoc.x, screenH - mouseLoc.y);

    // 优先寻找鼠标光标所在的外部窗口（精准支持拖拽放置）
    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);
        if (!dict || CFGetTypeID(dict) != CFDictionaryGetTypeID()) continue;

        CFNumberRef layerNum = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowLayer);
        int layer = -1;
        if (layerNum && CFGetTypeID(layerNum) == CFNumberGetTypeID()) CFNumberGetValue(layerNum, kCFNumberIntType, &layer);
        if (layer != 0) continue; // 只获取标准应用层窗口

        CFNumberRef pidNum = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowOwnerPID);
        int pid = 0;
        if (pidNum && CFGetTypeID(pidNum) == CFNumberGetTypeID()) CFNumberGetValue(pidNum, kCFNumberIntType, &pid);
        if ((pid_t)pid == myPid || pid <= 0) continue;

        CFDictionaryRef boundsDict = (CFDictionaryRef)CFDictionaryGetValue(dict, kCGWindowBounds);
        if (!boundsDict || CFGetTypeID(boundsDict) != CFDictionaryGetTypeID()) continue;

        CGRect rect;
        if (!CGRectMakeWithDictionaryRepresentation(boundsDict, &rect)) continue;
        if (rect.size.width < 120 || rect.size.height < 100) continue;

        // 检查光标是否在此窗口内或顶沿附近
        CGRect hoverArea = CGRectMake(rect.origin.x - 40, rect.origin.y - 60, rect.size.width + 80, rect.size.height + 80);
        if (CGRectContainsPoint(hoverArea, mouseTopLeft)) {
            CFNumberRef winIdNum = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowNumber);
            int winId = 0;
            if (winIdNum && CFGetTypeID(winIdNum) == CFNumberGetTypeID()) CFNumberGetValue(winIdNum, kCFNumberIntType, &winId);

            if (outRect) *outRect = rect;
            if (outPid) *outPid = (pid_t)pid;
            if (outWindowID) *outWindowID = (CGWindowID)winId;
            found = YES;
            break;
        }
    }

    // 若光标不在任何特定窗口内，则取屏幕最上层的活动应用窗口
    if (!found) {
        for (CFIndex i = 0; i < count; ++i) {
            CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);
            if (!dict || CFGetTypeID(dict) != CFDictionaryGetTypeID()) continue;

            CFNumberRef layerNum = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowLayer);
            int layer = -1;
            if (layerNum && CFGetTypeID(layerNum) == CFNumberGetTypeID()) CFNumberGetValue(layerNum, kCFNumberIntType, &layer);
            if (layer != 0) continue;

            CFNumberRef pidNum = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowOwnerPID);
            int pid = 0;
            if (pidNum && CFGetTypeID(pidNum) == CFNumberGetTypeID()) CFNumberGetValue(pidNum, kCFNumberIntType, &pid);
            if ((pid_t)pid == myPid || pid <= 0) continue;

            CFDictionaryRef boundsDict = (CFDictionaryRef)CFDictionaryGetValue(dict, kCGWindowBounds);
            if (!boundsDict || CFGetTypeID(boundsDict) != CFDictionaryGetTypeID()) continue;

            CGRect rect;
            if (!CGRectMakeWithDictionaryRepresentation(boundsDict, &rect)) continue;
            if (rect.size.width < 120 || rect.size.height < 100) continue;

            CFNumberRef winIdNum = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowNumber);
            int winId = 0;
            if (winIdNum && CFGetTypeID(winIdNum) == CFNumberGetTypeID()) CFNumberGetValue(winIdNum, kCFNumberIntType, &winId);

            if (outRect) *outRect = rect;
            if (outPid) *outPid = (pid_t)pid;
            if (outWindowID) *outWindowID = (CGWindowID)winId;
            found = YES;
            break;
        }
    }

    CFRelease(windowList);
    return found;
}

namespace Platform {

PrivateActiveWindowObserver::PrivateActiveWindowObserver() {
    AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)@{
        (__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES
    });
}

ActiveWindow PrivateActiveWindowObserver::getActiveWindow() {
    static ActiveWindow s_cachedWindow = {};
    static qint64 s_lastQueryTime = 0;
    qint64 now = (qint64)([[NSDate date] timeIntervalSince1970] * 1000);

    // 缓存 300ms，避免 25Hz 高频 IPC 查询 WindowServer，将 CPU 占用直接降至 ~0.1%
    if (now - s_lastQueryTime < 300 && s_cachedWindow.available) {
        return s_cachedWindow;
    }
    s_lastQueryTime = now;

    @autoreleasepool {
        // 1. 优先尝试 Quartz Window Server（无需无障碍权限、零延迟、支持光标命中检测与非前台检测）
        CGRect cgRect;
        pid_t cgPid = 0;
        CGWindowID cgWinId = 0;
        if (GetWindowFromCG(&cgRect, &cgPid, &cgWinId)) {
            m_activePid = cgPid;
            QString uid = QString::fromStdString(std::to_string(cgPid) + "-" + std::to_string(cgWinId));
            return s_cachedWindow = m_activeWindow = { uid, (long)cgPid, cgRect.origin.x,
                cgRect.origin.y, cgRect.size.width, cgRect.size.height };
        }

        // 2. 兜底尝试 Accessibility API
        NSRunningApplication *frontmost = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (frontmost != nil && frontmost.processIdentifier != getpid()) {
            AXUIElementRef appRef = AXUIElementCreateApplication(frontmost.processIdentifier);
            if (appRef != NULL) {
                AXUIElementRef focusedWindowRef = NULL;
                AXError result = AXUIElementCopyAttributeValue(appRef, kAXFocusedWindowAttribute,
                    (CFTypeRef*)&focusedWindowRef);
                CFRelease(appRef);

                if (result == kAXErrorSuccess && focusedWindowRef != NULL) {
                    CGRect rect;
                    pid_t pid;
                    CGWindowID windowID;
                    BOOL gotData = GetDataFromUIElement(focusedWindowRef, &rect, &pid, &windowID);
                    CFRelease(focusedWindowRef);

                    if (gotData) {
                        m_activePid = pid;
                        QString uid = QString::fromStdString(std::to_string(pid) + "-" + std::to_string(windowID));
                        return s_cachedWindow = m_activeWindow = { uid, (long)pid, rect.origin.x,
                            rect.origin.y, rect.size.width, rect.size.height };
                    }
                }
            }
        }

        s_cachedWindow = {};
        return m_activeWindow = {};
    }
}

}