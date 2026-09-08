// 
// Shijima-Qt - Global Hotkey Manager for macOS Implementation
// 

#include "HotkeyManager.hpp"
#include "Platform/Platform.hpp"
#include <QGuiApplication>
#include <QClipboard>
#include <QDebug>
#import <Carbon/Carbon.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <unistd.h>
#include <iostream>

static std::function<void()> s_translateCallback;
static std::function<void()> s_askCallback;
static std::function<void()> s_historyCallback;
static std::function<void()> s_musicToggleCallback;
static std::function<void()> s_musicPlayPauseCallback;
static std::function<void()> s_musicNextCallback;
static std::function<void()> s_musicPrevCallback;
static std::function<void()> s_musicFavCallback;

static UInt32 parseKeyCode(QString const& keyName) {
    QString k = keyName.trimmed().toUpper();
    if (k == "A") return 0x00;
    if (k == "S") return 0x01;
    if (k == "D") return 0x02;
    if (k == "F") return 0x03;
    if (k == "H") return 0x04;
    if (k == "G") return 0x05;
    if (k == "Z") return 0x06;
    if (k == "X") return 0x07;
    if (k == "C") return 0x08;
    if (k == "V") return 0x09;
    if (k == "B") return 0x0B;
    if (k == "Q") return 0x0C;
    if (k == "W") return 0x0D;
    if (k == "E") return 0x0E;
    if (k == "R") return 0x0F;
    if (k == "Y") return 0x10;
    if (k == "T") return 0x11;
    if (k == "1") return 0x12;
    if (k == "2") return 0x13;
    if (k == "3") return 0x14;
    if (k == "4") return 0x15;
    if (k == "6") return 0x16;
    if (k == "5") return 0x17;
    if (k == "9") return 0x19;
    if (k == "7") return 0x1A;
    if (k == "8") return 0x1C;
    if (k == "0") return 0x1D;
    if (k == "O") return 0x1F;
    if (k == "U") return 0x20;
    if (k == "I") return 0x22;
    if (k == "P") return 0x23;
    if (k == "L") return 0x25;
    if (k == "J") return 0x26;
    if (k == "K") return 0x28;
    if (k == "N") return 0x2D;
    if (k == "M") return 0x2E;
    if (k == "SPACE") return 0x31;
    if (k == "LEFT") return 0x7B;
    if (k == "RIGHT") return 0x7C;
    if (k == "DOWN") return 0x7D;
    if (k == "UP") return 0x7E;
    return 0x11; // 默认 'T'
}

static bool parseShortcut(QString const& shortcutStr, UInt32 &modifiers, UInt32 &keyCode) {
    modifiers = 0;
    keyCode = 0;
    QStringList parts = shortcutStr.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }

    for (int i = 0; i < parts.size() - 1; ++i) {
        QString mod = parts[i].trimmed().toLower();
        if (mod == "option" || mod == "alt" || mod == "⌥") {
            modifiers |= optionKey;
        } else if (mod == "cmd" || mod == "command" || mod == "⌘") {
            modifiers |= cmdKey;
        } else if (mod == "ctrl" || mod == "control" || mod == "⌃") {
            modifiers |= controlKey;
        } else if (mod == "shift" || mod == "⇧") {
            modifiers |= shiftKey;
        }
    }

    if (modifiers == 0) {
        modifiers = optionKey;
    }

    keyCode = parseKeyCode(parts.last());
    return true;
}

static OSStatus hotKeyHandler(EventHandlerCallRef, EventRef theEvent, void*) {
    EventHotKeyID hkId;
    GetEventParameter(theEvent, kEventParamDirectObject, typeEventHotKeyID, NULL, sizeof(hkId), NULL, &hkId);

    if (hkId.id == 101) {
        std::cout << "[全局快捷键] 触发 ⌥+T (划词翻译快捷键)" << std::endl;
        Platform::activateApp();
        if (s_translateCallback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                s_translateCallback();
            });
        }
    } else if (hkId.id == 102) {
        std::cout << "[全局快捷键] 触发 ⌥+Q (划词提问快捷键)" << std::endl;
        Platform::activateApp();
        if (s_askCallback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                s_askCallback();
            });
        }
    } else if (hkId.id == 103) {
        std::cout << "[全局快捷键] 触发 ⌥+H (历史任务快捷键)" << std::endl;
        Platform::activateApp();
        if (s_historyCallback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                s_historyCallback();
            });
        }
    } else if (hkId.id == 201) {
        std::cout << "[全局快捷键] 触发 音乐播放器窗口显示/隐藏" << std::endl;
        Platform::activateApp();
        if (s_musicToggleCallback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                s_musicToggleCallback();
            });
        }
    } else if (hkId.id == 202) {
        std::cout << "[全局快捷键] 触发 音乐播放/暂停" << std::endl;
        if (s_musicPlayPauseCallback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                s_musicPlayPauseCallback();
            });
        }
    } else if (hkId.id == 203) {
        std::cout << "[全局快捷键] 触发 音乐下一首" << std::endl;
        if (s_musicNextCallback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                s_musicNextCallback();
            });
        }
    } else if (hkId.id == 204) {
        std::cout << "[全局快捷键] 触发 音乐上一首" << std::endl;
        if (s_musicPrevCallback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                s_musicPrevCallback();
            });
        }
    } else if (hkId.id == 205) {
        std::cout << "[全局快捷键] 触发 音乐一键收藏" << std::endl;
        if (s_musicFavCallback) {
            dispatch_async(dispatch_get_main_queue(), ^{
                s_musicFavCallback();
            });
        }
    }
    return noErr;
}

HotkeyManager *HotkeyManager::instance() {
    static HotkeyManager s_instance;
    return &s_instance;
}

HotkeyManager::HotkeyManager() {
    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;

    EventHandlerRef ref = NULL;
    InstallApplicationEventHandler(&hotKeyHandler, 1, &eventType, NULL, &ref);
    m_eventHandlerRef = (void *)ref;
}

HotkeyManager::~HotkeyManager() {
    unregisterAll();
    if (m_eventHandlerRef) {
        RemoveEventHandler((EventHandlerRef)m_eventHandlerRef);
        m_eventHandlerRef = nullptr;
    }
}

void HotkeyManager::registerTranslateHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_translateCallback = callback;
    if (m_translateHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_translateHotKeyRef);
        m_translateHotKeyRef = nullptr;
    }

    UInt32 mods = 0, key = 0;
    if (parseShortcut(shortcutStr, mods, key)) {
        EventHotKeyID hkId;
        hkId.signature = 'SHI1';
        hkId.id = 101;
        EventHotKeyRef ref = NULL;
        OSStatus status = RegisterEventHotKey(key, mods, hkId, GetApplicationEventTarget(), 0, &ref);
        if (status == noErr) {
            m_translateHotKeyRef = (void *)ref;
            std::cout << "[全局快捷键] 成功注册翻译快捷键: " << shortcutStr.toStdString() << std::endl;
        }
    }
}

void HotkeyManager::registerAskHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_askCallback = callback;
    if (m_askHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_askHotKeyRef);
        m_askHotKeyRef = nullptr;
    }

    UInt32 mods = 0, key = 0;
    if (parseShortcut(shortcutStr, mods, key)) {
        EventHotKeyID hkId;
        hkId.signature = 'SHI2';
        hkId.id = 102;
        EventHotKeyRef ref = NULL;
        OSStatus status = RegisterEventHotKey(key, mods, hkId, GetApplicationEventTarget(), 0, &ref);
        if (status == noErr) {
            m_askHotKeyRef = (void *)ref;
            std::cout << "[全局快捷键] 成功注册提问快捷键: " << shortcutStr.toStdString() << std::endl;
        }
    }
}

void HotkeyManager::registerHistoryHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_historyCallback = callback;
    if (m_historyHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_historyHotKeyRef);
        m_historyHotKeyRef = nullptr;
    }

    UInt32 mods = 0, key = 0;
    if (parseShortcut(shortcutStr, mods, key)) {
        EventHotKeyID hkId;
        hkId.signature = 'SHI3';
        hkId.id = 103;
        EventHotKeyRef ref = NULL;
        OSStatus status = RegisterEventHotKey(key, mods, hkId, GetApplicationEventTarget(), 0, &ref);
        if (status == noErr) {
            m_historyHotKeyRef = (void *)ref;
            std::cout << "[全局快捷键] 成功注册历史任务快捷键: " << shortcutStr.toStdString() << std::endl;
        }
    }
}

void HotkeyManager::registerMusicToggleHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicToggleCallback = callback;
    if (m_musicToggleHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicToggleHotKeyRef);
        m_musicToggleHotKeyRef = nullptr;
    }

    UInt32 mods = 0, key = 0;
    if (parseShortcut(shortcutStr, mods, key)) {
        EventHotKeyID hkId;
        hkId.signature = 'MS01';
        hkId.id = 201;
        EventHotKeyRef ref = NULL;
        OSStatus status = RegisterEventHotKey(key, mods, hkId, GetApplicationEventTarget(), 0, &ref);
        if (status == noErr) {
            m_musicToggleHotKeyRef = (void *)ref;
            std::cout << "[全局快捷键] 成功注册音乐窗口快捷键: " << shortcutStr.toStdString() << std::endl;
        }
    }
}

void HotkeyManager::registerMusicPlayPauseHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicPlayPauseCallback = callback;
    if (m_musicPlayPauseHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicPlayPauseHotKeyRef);
        m_musicPlayPauseHotKeyRef = nullptr;
    }

    UInt32 mods = 0, key = 0;
    if (parseShortcut(shortcutStr, mods, key)) {
        EventHotKeyID hkId;
        hkId.signature = 'MS02';
        hkId.id = 202;
        EventHotKeyRef ref = NULL;
        OSStatus status = RegisterEventHotKey(key, mods, hkId, GetApplicationEventTarget(), 0, &ref);
        if (status == noErr) {
            m_musicPlayPauseHotKeyRef = (void *)ref;
            std::cout << "[全局快捷键] 成功注册音乐播放/暂停快捷键: " << shortcutStr.toStdString() << std::endl;
        }
    }
}

void HotkeyManager::registerMusicNextHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicNextCallback = callback;
    if (m_musicNextHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicNextHotKeyRef);
        m_musicNextHotKeyRef = nullptr;
    }

    UInt32 mods = 0, key = 0;
    if (parseShortcut(shortcutStr, mods, key)) {
        EventHotKeyID hkId;
        hkId.signature = 'MS03';
        hkId.id = 203;
        EventHotKeyRef ref = NULL;
        OSStatus status = RegisterEventHotKey(key, mods, hkId, GetApplicationEventTarget(), 0, &ref);
        if (status == noErr) {
            m_musicNextHotKeyRef = (void *)ref;
            std::cout << "[全局快捷键] 成功注册音乐下一首快捷键: " << shortcutStr.toStdString() << std::endl;
        }
    }
}

void HotkeyManager::registerMusicPrevHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicPrevCallback = callback;
    if (m_musicPrevHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicPrevHotKeyRef);
        m_musicPrevHotKeyRef = nullptr;
    }

    UInt32 mods = 0, key = 0;
    if (parseShortcut(shortcutStr, mods, key)) {
        EventHotKeyID hkId;
        hkId.signature = 'MS04';
        hkId.id = 204;
        EventHotKeyRef ref = NULL;
        OSStatus status = RegisterEventHotKey(key, mods, hkId, GetApplicationEventTarget(), 0, &ref);
        if (status == noErr) {
            m_musicPrevHotKeyRef = (void *)ref;
            std::cout << "[全局快捷键] 成功注册音乐上一首快捷键: " << shortcutStr.toStdString() << std::endl;
        }
    }
}

void HotkeyManager::registerMusicFavHotkey(QString const& shortcutStr, std::function<void()> callback) {
    s_musicFavCallback = callback;
    if (m_musicFavHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicFavHotKeyRef);
        m_musicFavHotKeyRef = nullptr;
    }

    UInt32 mods = 0, key = 0;
    if (parseShortcut(shortcutStr, mods, key)) {
        EventHotKeyID hkId;
        hkId.signature = 'MS05';
        hkId.id = 205;
        EventHotKeyRef ref = NULL;
        OSStatus status = RegisterEventHotKey(key, mods, hkId, GetApplicationEventTarget(), 0, &ref);
        if (status == noErr) {
            m_musicFavHotKeyRef = (void *)ref;
            std::cout << "[全局快捷键] 成功注册音乐一键收藏快捷键: " << shortcutStr.toStdString() << std::endl;
        }
    }
}

void HotkeyManager::unregisterAll() {
    if (m_translateHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_translateHotKeyRef);
        m_translateHotKeyRef = nullptr;
    }
    if (m_askHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_askHotKeyRef);
        m_askHotKeyRef = nullptr;
    }
    if (m_historyHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_historyHotKeyRef);
        m_historyHotKeyRef = nullptr;
    }
    if (m_musicToggleHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicToggleHotKeyRef);
        m_musicToggleHotKeyRef = nullptr;
    }
    if (m_musicPlayPauseHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicPlayPauseHotKeyRef);
        m_musicPlayPauseHotKeyRef = nullptr;
    }
    if (m_musicNextHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicNextHotKeyRef);
        m_musicNextHotKeyRef = nullptr;
    }
    if (m_musicPrevHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicPrevHotKeyRef);
        m_musicPrevHotKeyRef = nullptr;
    }
    if (m_musicFavHotKeyRef) {
        UnregisterEventHotKey((EventHotKeyRef)m_musicFavHotKeyRef);
        m_musicFavHotKeyRef = nullptr;
    }
}

// 方式一：尝试通过 Accessibility API 无感读取选中文本
static QString getSelectedTextViaAccessibility() {
    AXUIElementRef systemWide = AXUIElementCreateSystemWide();
    if (!systemWide) return QString();

    AXUIElementRef focusedApp = NULL;
    AXError err = AXUIElementCopyAttributeValue(systemWide, kAXFocusedApplicationAttribute, (CFTypeRef *)&focusedApp);
    if (err == kAXErrorSuccess && focusedApp != NULL) {
        AXUIElementRef focusedElement = NULL;
        err = AXUIElementCopyAttributeValue(focusedApp, kAXFocusedUIElementAttribute, (CFTypeRef *)&focusedElement);
        if (err == kAXErrorSuccess && focusedElement != NULL) {
            CFTypeRef selectedText = NULL;
            err = AXUIElementCopyAttributeValue(focusedElement, kAXSelectedTextAttribute, &selectedText);
            if (err == kAXErrorSuccess && selectedText != NULL) {
                if (CFGetTypeID(selectedText) == CFStringGetTypeID()) {
                    NSString *nsStr = (__bridge NSString *)selectedText;
                    QString result = QString::fromNSString(nsStr);
                    CFRelease(selectedText);
                    CFRelease(focusedElement);
                    CFRelease(focusedApp);
                    CFRelease(systemWide);
                    return result.trimmed();
                }
                CFRelease(selectedText);
            }
            CFRelease(focusedElement);
        }
        CFRelease(focusedApp);
    }
    CFRelease(systemWide);
    return QString();
}

// 方式二：针对 Electron/VSCode/微信/终端/Office 等非原生文本框，模拟极速 ⌘+C 瞬时获取选中文本
static QString simulateCmdCAndGetSelectedText() {
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSInteger oldChangeCount = [pasteboard changeCount];

    // 构造并发送 ⌘+C 快捷键事件
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef keyDown = CGEventCreateKeyboardEvent(source, (CGKeyCode)0x08, true); // 0x08 == 'C'
    CGEventRef keyUp = CGEventCreateKeyboardEvent(source, (CGKeyCode)0x08, false);

    CGEventSetFlags(keyDown, kCGEventFlagMaskCommand);
    CGEventSetFlags(keyUp, kCGEventFlagMaskCommand);

    CGEventPost(kCGHIDEventTap, keyDown);
    CGEventPost(kCGHIDEventTap, keyUp);

    CFRelease(keyDown);
    CFRelease(keyUp);
    CFRelease(source);

    // 毫秒级轮询剪贴板变化（最多等待 80ms）
    for (int i = 0; i < 8; ++i) {
        usleep(10000); // 10ms
        if ([pasteboard changeCount] != oldChangeCount) {
            NSString *newString = [pasteboard stringForType:NSPasteboardTypeString];
            if (newString && [newString length] > 0) {
                return QString::fromNSString(newString).trimmed();
            }
        }
    }

    return QString();
}

QString HotkeyManager::getActiveSelectedText() {
    // 1. 优先尝试 Accessibility 无感读取（浏览器/原生文本框）
    QString text = getSelectedTextViaAccessibility();
    if (!text.isEmpty()) {
        std::cout << "[划词引擎] 通过 Accessibility 成功获取选中文本: " << text.toStdString() << std::endl;
        return text;
    }

    // 2. 兜底调用极速 ⌘+C 模拟取词（彻底覆盖 VSCode/微信/终端/PDF/Office/Obsidian 等所有第三方软件）
    text = simulateCmdCAndGetSelectedText();
    if (!text.isEmpty()) {
        std::cout << "[划词引擎] 通过模拟 ⌘+C 成功获取选中文本: " << text.toStdString() << std::endl;
        return text;
    }

    // 3. 最后检查当前剪贴板内容
    if (auto clip = QGuiApplication::clipboard()) {
        QString clipText = clip->text().trimmed();
        if (!clipText.isEmpty()) {
            std::cout << "[划词引擎] 读取当前剪贴板文本: " << clipText.toStdString() << std::endl;
            return clipText;
        }
    }

    std::cout << "[划词引擎] 未获取到任何选中文本" << std::endl;
    return QString();
}
